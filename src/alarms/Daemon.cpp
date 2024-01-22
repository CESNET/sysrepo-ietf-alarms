#include <chrono>
#include <map>
#include <string>
#include "Daemon.h"
#include "Key.h"
#include "PurgeFilter.h"
#include "ShelfMatch.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"
#include "utils/time.h"

using namespace std::string_literals;

namespace {
const auto rootPath = "/ietf-alarms:alarms"s;
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto ietfAlarmsModule = "ietf-alarms";
const auto alarmList = "/ietf-alarms:alarms/alarm-list";
const auto alarmListInstances = "/ietf-alarms:alarms/alarm-list/alarm";
const auto shelvedAlarmList = "/ietf-alarms:alarms/shelved-alarms";
const auto shelvedAlarmListInstances = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";
const auto purgeShelvedRpcPrefix = "/ietf-alarms:alarms/shelved-alarms/purge-shelved-alarms";
const auto alarmInventoryPrefix = "/ietf-alarms:alarms/alarm-inventory";
const auto controlPrefix = "/ietf-alarms:alarms/control";
const auto ctrlNotifyStatusChanges = controlPrefix + "/notify-status-changes"s;
const auto ctrlNotifySeverityLevel = controlPrefix + "/notify-severity-level"s;
const auto alarmSummaryPrefix = "/ietf-alarms:alarms/summary";

/** @brief returns number of list instances in the list specified by xPath */
size_t numberOfListInstances(const libyang::DataNode& alarmRoot, const std::string& xPath)
{
    return alarmRoot.findXPath(xPath).size();
}

void updateStats(libyang::DataNode& edit, const std::string& prefix, const std::string& alarmsCountLeafName, size_t alarmCount, const std::string& lastChangedLeafName, const std::chrono::time_point<std::chrono::system_clock>& lastChanged)
{
    // number-of-alarms is of type yang:gauge32. If we ever support more than 2^32-1 alarms then we will have to deal with cropping the value.
    edit.newPath(prefix + "/" + alarmsCountLeafName, std::to_string(alarmCount));
    edit.newPath(prefix + "/" + lastChangedLeafName, alarms::utils::yangTimeFormat(lastChanged));
}

void updateAlarmListStats(libyang::DataNode& edit, size_t alarmCount, const std::chrono::time_point<std::chrono::system_clock>& lastChanged)
{
    updateStats(edit, alarmList, "number-of-alarms", alarmCount, "last-changed", lastChanged);
}

void updateShelvedAlarmListStats(libyang::DataNode& edit, size_t alarmCount, const std::chrono::time_point<std::chrono::system_clock>& lastChanged)
{
    updateStats(edit, shelvedAlarmList, "number-of-shelved-alarms", alarmCount, "shelved-alarms-last-changed", lastChanged);
}

void updateAlarmSummary(sysrepo::Session session)
{
    static const std::vector<std::string> SEVERITIES = {"indeterminate", "warning", "minor", "major", "critical"};
    struct SeveritySummary {
        unsigned total;
        unsigned cleared;
    };
    using AlarmSummary = std::map<std::string, SeveritySummary>;

    auto alarmRoot = session.getData(rootPath);
    assert(alarmRoot);

    AlarmSummary alarmSummary;
    for (const auto& severity : SEVERITIES) {
        alarmSummary[severity];
    }

    for (const auto& alarmNode : alarmRoot->findXPath(alarmListInstances)) {
        const auto severity = alarms::utils::childValue(alarmNode, "perceived-severity");
        const auto isCleared = alarms::utils::childValue(alarmNode, "is-cleared");

        alarmSummary[severity].total++;
        if (isCleared == "true") {
            alarmSummary[severity].cleared++;
        }
    }

    auto edit = session.getContext().newPath(alarmSummaryPrefix);
    for (const auto& [severity, summ] : alarmSummary) {
        const auto prefix = alarmSummaryPrefix + "/alarm-summary[severity='"s + severity + "']";
        edit.newPath(prefix + "/total", std::to_string(summ.total));
        edit.newPath(prefix + "/not-cleared", std::to_string(summ.total - summ.cleared));
        edit.newPath(prefix + "/cleared", std::to_string(summ.cleared));
    }

    session.editBatch(edit, sysrepo::DefaultOperation::Merge);
    session.applyChanges();
}

bool valueChanged(const std::optional<libyang::DataNode>& oldNode, const libyang::DataNode& newNode, const char* leafName)
{
    bool oldLeafExists = oldNode && oldNode->findPath(leafName);
    bool newLeafExists = newNode.findPath(leafName).has_value();

    // leaf was deleted or created
    if (oldLeafExists != newLeafExists) {
        return true;
    } else if (!oldLeafExists && !newLeafExists) {
        return false;
    } else {
        return alarms::utils::childValue(*oldNode, leafName) != alarms::utils::childValue(newNode, leafName);
    }
}

/** @brief Checks if we should notify about changes made based on the values changed and current settings in control container */
bool shouldNotifyStatusChange(
        const std::optional<libyang::DataNode>& oldNode,
        const libyang::DataNode& edit,
        const alarms::Daemon::NotifyStatusChanges notifyStatusChanges,
        const std::optional<int32_t> notifySeverityThreshold
        )
{
    bool raised = edit.findPath("is-cleared") && alarms::utils::childValue(edit, "is-cleared") == "false" && valueChanged(oldNode, edit, "is-cleared");
    bool cleared = edit.findPath("is-cleared") && alarms::utils::childValue(edit, "is-cleared") == "true" && valueChanged(oldNode, edit, "is-cleared");

    if (cleared || notifyStatusChanges == alarms::Daemon::NotifyStatusChanges::All) {
        return true;
    }

    if (notifyStatusChanges == alarms::Daemon::NotifyStatusChanges::RaiseAndClear) {
        return raised || cleared;
    }

    /* Notifications are sent for status changes equal to or above the specified severity level.
     * Notifications shall also be sent for state changes that make an alarm less severe than the specified level.
     */

    auto newSeverity = std::get<libyang::Enum>(edit.findPath("perceived-severity")->asTerm().value()).value;
    if (!oldNode) {
        return newSeverity >= *notifySeverityThreshold;
    }

    auto oldSeverity = std::get<libyang::Enum>(oldNode->findPath("perceived-severity")->asTerm().value()).value;
    return newSeverity >= *notifySeverityThreshold || (oldSeverity >= *notifySeverityThreshold && newSeverity < *notifySeverityThreshold);
}

/* @brief Checks if the alarm keys match any entry in ietf-alarms:alarms/control/alarm-shelving. If so, return name of the matched shelf */
std::optional<std::string> shouldBeShelved(sysrepo::Session session, const alarms::InstanceKey& key)
{
    alarms::utils::ScopedDatastoreSwitch s(session, sysrepo::Datastore::Running);

    std::optional<std::string> shelfName;
    if (auto data = session.getData("/ietf-alarms:alarms/control/alarm-shelving")) {
        shelfName = findMatchingShelf(key, data->findXPath("/ietf-alarms:alarms/control/alarm-shelving/shelf"));
    }

    return shelfName;
}

/** @brief Helper for validating alarm against alarm-inventory */
bool validateInventoryField(const libyang::DataNode& node, const std::string& list, const std::string& searchFor)
{
    if (auto listNodes = node.findXPath(list); !listNodes.empty()) {
        std::vector<std::string> elements;
        std::transform(listNodes.begin(), listNodes.end(), std::back_inserter(elements), [&](const auto& node) { return std::string{node.asTerm().valueStr()}; });

        if (auto it = std::find(elements.begin(), elements.end(), searchFor); it == elements.end()) {
            return false;
        }
    }

    return true;
}
}

namespace alarms {

Daemon::Daemon()
    : m_connection(sysrepo::Connection{})
    , m_session(m_connection.sessionStart(sysrepo::Datastore::Operational))
    , m_log(spdlog::get("main"))
    , m_notifyStatusChanges(NotifyStatusChanges::All)
{
    utils::ensureModuleImplemented(m_session, ietfAlarmsModule, "2019-09-11", {"alarm-shelving", "alarm-summary"});
    utils::ensureModuleImplemented(m_session, "sysrepo-ietf-alarms", "2022-02-17");

    {
        auto edit = m_session.getContext().newPath(alarmList);
        auto now = std::chrono::system_clock::now();
        updateAlarmListStats(edit, 0, now);
        updateShelvedAlarmListStats(edit, 0, now);
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();

        updateAlarmSummary(m_session);
    }

    m_alarmSub = m_session.onRPCAction(rpcPrefix, [&](sysrepo::Session session, auto, auto, const libyang::DataNode input, auto, auto, auto) {
        if (session.getOriginatorName() == "netopeer2"
                || session.getOriginatorName() == "rousette"
                || session.getOriginatorName() == "sysrepo-cli") {
            session.setNetconfError({.type = "application",
                                     .tag = "operation-not-supported",
                                     .appTag = std::nullopt,
                                     .path = std::nullopt,
                                     .message = "Internal RPCs cannot be called.",
                                     .infoElements = {}});
            return sysrepo::ErrorCode::OperationFailed;
        }
        return submitAlarm(session, input);
    });
    m_alarmSub->onRPCAction(purgeRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(purgeRpcPrefix, alarmListInstances, input, output); });
    m_alarmSub->onRPCAction(purgeShelvedRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(purgeShelvedRpcPrefix, shelvedAlarmListInstances, input, output); });

    {
        utils::ScopedDatastoreSwitch sw(m_session, sysrepo::Datastore::Running);
        m_alarmSub->onModuleChange(
            ietfAlarmsModule,
            [&](auto session, auto, auto, auto, auto, auto) {
                bool needsReshelve = false;
                for (const auto& change : session.getChanges()) {
                    std::string xpath = change.node.path();
                    if (xpath.find(controlPrefix + "/alarm-shelving"s) == 0) {
                        needsReshelve = true;
                        continue;
                    }
                    if (xpath == ctrlNotifyStatusChanges) {
                        auto val = std::get<libyang::Enum>(change.node.asTerm().value());
                        if (val.name == "all-state-changes") {
                            m_notifyStatusChanges = NotifyStatusChanges::All;
                            m_log->info("Will notify about any alarm state changes");
                        } else if (val.name == "raise-and-clear") {
                            m_notifyStatusChanges = NotifyStatusChanges::RaiseAndClear;
                            m_log->info("Will notify about raised and cleared alarms");
                        } else if (val.name == "severity-level") {
                            m_notifyStatusChanges = NotifyStatusChanges::BySeverity;
                        } else {
                            throw std::runtime_error{"Cannot handle " + ctrlNotifyStatusChanges + " value " + val.name};
                        }
                        continue;
                    }
                    if (xpath == ctrlNotifySeverityLevel) {
                        if (change.operation == sysrepo::ChangeOperation::Deleted) {
                            m_notifySeverityThreshold = std::nullopt;
                        } else {
                            auto value = std::get<libyang::Enum>(change.node.asTerm().value());
                            m_notifySeverityThreshold = value.value;
                            m_log->info("Will notify about alarms with perceived-severity >= {}", value.name);
                        }
                        continue;
                    }
                }
                if (needsReshelve) {
                    reshelve();
                }
                return sysrepo::ErrorCode::Ok;
            },
            controlPrefix,
            0,
            sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly);
    }

    m_inventorySub = m_session.onModuleChange(
        ietfAlarmsModule, [&](auto, auto, auto, auto, auto, auto) {
            m_session.sendNotification(m_session.getContext().newPath("/ietf-alarms:alarm-inventory-changed", std::nullopt), sysrepo::Wait::No);
            return sysrepo::ErrorCode::Ok;
        },
        alarmInventoryPrefix,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);
}

/** @brief Check whether published alarm is in alarm-inventory container
 *
 * According to RFC 8632 the system MUST publish all possible alarm types in the alarm inventory.
 * This is to ensure that alarm operators know what alarms might appear.
 *
 * See RFC 8632, section 4.2
 *
 * @return optional<string> containing the error message if validation fails
 * */
std::optional<std::string> Daemon::inventoryValidationError(const libyang::DataNode& alarmRoot, const InstanceKey& key, const std::string& severity)
{
    const std::string msgPrefix = "Published or cleared alarm id='" + key.type.id + "'"
        + " qualifier='" + key.type.qualifier + "'"
        + " resource='" + key.resource + "' severity='" + severity + "'";

    const auto inventoryNodesXPath = alarmInventoryPrefix + "/alarm-type"s + key.type.xpathIndex();
    auto inventoryNodes = alarmRoot.findXPath(inventoryNodesXPath);
    if (inventoryNodes.empty()) {
        return msgPrefix + " but this alarm is not listed in the alarm inventory";
    } else if (inventoryNodes.size() > 1) {
        throw std::runtime_error("Internal error: too many matching alarms in alarm-inventory");
    }

    if (!validateInventoryField(*inventoryNodes.begin(), "resource", key.resource)) {
        return msgPrefix + " but the resource is not listed in the alarm inventory for this alarm";
    }

    if (severity != "cleared" && !validateInventoryField(*inventoryNodes.begin(), "severity-level", severity)) {
        return msgPrefix + " but the severity is not listed in the alarm inventory for this alarm";
    }

    return std::nullopt;
}

sysrepo::ErrorCode Daemon::submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input)
{
    const auto& alarmKey = InstanceKey::fromNode(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";
    const auto now = std::chrono::system_clock::now();

    const auto alarmRoot = m_session.getData(rootPath);
    assert(alarmRoot);

    if (auto inventoryError = inventoryValidationError(*alarmRoot, alarmKey, severity)) {
        rpcSession.setNetconfError({.type = "application",
                                    .tag = "data-missing",
                                    .appTag = std::nullopt,
                                    .path = std::nullopt,
                                    .message = (inventoryError.value() + " Violation of RFC8632 (sec. 4.1).").c_str(),
                                    .infoElements = {}});
        m_log->warn(inventoryError.value());
        return sysrepo::ErrorCode::OperationFailed;
    }

    auto matchedShelf = shouldBeShelved(m_session, alarmKey);
    std::string alarmNodePath;

    if (matchedShelf) {
        alarmNodePath = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
    } else {
        alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm";
    }

    try {
        alarmNodePath = (matchedShelf ? shelvedAlarmListInstances : alarmListInstances) + alarmKey.xpathIndex();
        m_log->trace("Alarm node: {}", alarmNodePath);
    } catch (const std::invalid_argument& e) {
        m_log->debug("submitAlarm exception: {}", e.what());
        rpcSession.setErrorMessage(e.what());
        return sysrepo::ErrorCode::InvalidArgument;
    }

    const auto existingAlarmNode = alarmRoot->findPath(alarmNodePath);

    auto edit = m_session.getContext().newPath(alarmNodePath, std::nullopt, libyang::CreationOptions::Update);

    if (is_cleared && (!existingAlarmNode || (existingAlarmNode && utils::childValue(*existingAlarmNode, "is-cleared") == "true"))) {
        // if passing is-cleared=true the alarm either doesn't exist or exists but is inactive (is-cleared=true), do nothing, it's a NOOP
        return sysrepo::ErrorCode::Ok;
    } else if (!existingAlarmNode && !matchedShelf) {
        edit.newPath(alarmNodePath + "/time-created", utils::yangTimeFormat(now));
    }

    edit.newPath(alarmNodePath + "/is-cleared", is_cleared ? "true" : "false", libyang::CreationOptions::Update);
    if (!is_cleared && (!existingAlarmNode || (existingAlarmNode && utils::childValue(*existingAlarmNode, "is-cleared") == "true"))) {
        edit.newPath(alarmNodePath + "/last-raised", utils::yangTimeFormat(now));
    }

    if (!is_cleared) {
        edit.newPath(alarmNodePath + "/perceived-severity", severity, libyang::CreationOptions::Update);
    }

    edit.newPath(alarmNodePath + "/alarm-text", std::string{input.findPath("alarm-text").value().asTerm().valueStr()}, libyang::CreationOptions::Update);

    const auto& editAlarmNode = edit.findPath(alarmNodePath);
    if (valueChanged(existingAlarmNode, *editAlarmNode, "alarm-text") || valueChanged(existingAlarmNode, *editAlarmNode, "is-cleared") || valueChanged(existingAlarmNode, *editAlarmNode, "perceived-severity")) {
        edit.newPath(alarmNodePath + "/last-changed", utils::yangTimeFormat(now));
    }

    m_log->trace("Update: {}", std::string(*edit.printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Shrink)));

    if (!matchedShelf) {
        updateAlarmListStats(edit, numberOfListInstances(*alarmRoot, alarmListInstances) + static_cast<int>(editAlarmNode->findPath("time-created").has_value()), now);
    } else {
        edit.newPath(alarmNodePath + "/shelf-name", matchedShelf);
        if (!existingAlarmNode) {
            updateShelvedAlarmListStats(edit, numberOfListInstances(*alarmRoot, shelvedAlarmListInstances) + 1, now);
        }
    }

    m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
    m_session.applyChanges();

    updateAlarmSummary(m_session);

    if (shouldNotifyStatusChange(existingAlarmNode, *editAlarmNode, m_notifyStatusChanges, m_notifySeverityThreshold)) {
        m_session.sendNotification(createStatusChangeNotification(edit.findPath(alarmNodePath).value()), sysrepo::Wait::No);
    }

    return sysrepo::ErrorCode::Ok;
}

libyang::DataNode Daemon::createStatusChangeNotification(const libyang::DataNode& alarmNode)
{
    static const std::string prefix = "/ietf-alarms:alarm-notification";

    auto notification = m_session.getContext().newPath(prefix + "/resource", utils::childValue(alarmNode, "resource"));
    notification.newPath(prefix + "/alarm-type-id", utils::childValue(alarmNode, "alarm-type-id"));
    notification.newPath(prefix + "/time", utils::childValue(alarmNode, "last-changed"));
    notification.newPath(prefix + "/alarm-text", utils::childValue(alarmNode, "alarm-text"));

    if (auto qualifier = utils::childValue(alarmNode, "alarm-type-qualifier"); !qualifier.empty()) {
        notification.newPath(prefix + "/alarm-type-qualifier", qualifier);
    }

    notification.newPath(prefix + "/perceived-severity", utils::childValue(alarmNode, "is-cleared") == "true" ? "cleared" : utils::childValue(alarmNode, "perceived-severity"));

    return notification;
}

sysrepo::ErrorCode Daemon::purgeAlarms(const std::string& rpcPath, const std::string& alarmListXPath, const libyang::DataNode& rpcInput, libyang::DataNode output)
{
    const auto now = std::chrono::system_clock::now();
    PurgeFilter filter(rpcInput);
    std::vector<std::string> toDelete;

    auto alarmRoot = m_session.getData(rootPath);
    assert(alarmRoot);

    for (const auto& alarmNode : alarmRoot->findXPath(alarmListXPath)) {
        if (filter.matches(alarmNode)) {
            toDelete.push_back(std::string(alarmNode.path()));
        }
    }

    if (!toDelete.empty()) {
        auto edit = m_session.getContext().newPath(alarmList);

        if (rpcPath == purgeRpcPrefix) {
            updateAlarmListStats(edit, numberOfListInstances(*alarmRoot, alarmListInstances) - toDelete.size(), now);
        } else {
            updateShelvedAlarmListStats(edit, numberOfListInstances(*alarmRoot, shelvedAlarmListInstances) - toDelete.size(), now);
        }

        utils::removeFromOperationalDS(m_session.getContext(), edit, toDelete);
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();

        updateAlarmSummary(m_session);
    }

    output.newPath(rpcPath + "/purged-alarms", std::to_string(toDelete.size()), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}

namespace {

/** @brief Copy contents of shared leaves from existing alarm node into edit. */
void createCommonAlarmNodeProps(libyang::DataNode& edit, const libyang::DataNode& alarm, const std::string& prefix)
{
    for (const auto& leafName : {"is-cleared", "last-raised", "last-changed", "perceived-severity", "alarm-text"}) {
        edit.newPath(prefix + "/" + leafName, utils::childValue(alarm, leafName));
    }
}

/** @brief Creates an edit with shelved-alarm list node based on existing alarm node */
void createShelvedAlarmNodeFromExistingNode(libyang::DataNode& edit, const libyang::DataNode& alarm, const InstanceKey& alarmKey, const std::string& shelfName)
{
    const auto key = shelvedAlarmListInstances + alarmKey.xpathIndex();
    edit.newPath(key + "/shelf-name", shelfName);
    createCommonAlarmNodeProps(edit, alarm, key);
}

/** @brief Creates an edit with alarm-list node based on existing alarm node */
void createAlarmNodeFromExistingNode(libyang::DataNode& edit, const libyang::DataNode& alarm, const InstanceKey& alarmKey, const std::chrono::time_point<std::chrono::system_clock>& now)
{
    const auto key = alarmListInstances + alarmKey.xpathIndex();
    edit.newPath(key + "/time-created", utils::yangTimeFormat(now));
    createCommonAlarmNodeProps(edit, alarm, key);
}
}

void Daemon::reshelve()
{
    auto alarmRoot = m_session.getData(rootPath);
    assert(alarmRoot);

    auto edit = m_session.getContext().newPath(rootPath, std::nullopt);
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> toErase;
    bool change = false;
    bool movedBetweenShelfs = false;
    size_t shelvedCount = 0;
    size_t unshelvedCount = 0;

    for (const auto& node : alarmRoot->findXPath(alarmListInstances)) {
        const auto alarmKey = InstanceKey::fromNode(node);
        if (auto shelf = shouldBeShelved(m_session, alarmKey)) {
            createShelvedAlarmNodeFromExistingNode(edit, node, alarmKey, *shelf);
            m_log->trace("Alarm {} shelved ({})", node.path(), *shelf);
            toErase.emplace_back(node.path());
            shelvedCount += 1;
            change = true;
        }
    }

    for (const auto& node : alarmRoot->findXPath(shelvedAlarmListInstances)) {
        const auto alarmKey = InstanceKey::fromNode(node);
        if (auto shelf = shouldBeShelved(m_session, alarmKey)) {
            if (*shelf != utils::childValue(node, "shelf-name")) {
                edit.newPath(shelvedAlarmListInstances + alarmKey.xpathIndex() + "/shelf-name", *shelf);
                m_log->trace("Alarm {} moved between shelfs ({} -> {})", node.path(), utils::childValue(node, "shelf-name"), *shelf);
                change = true;
                movedBetweenShelfs = true;
            }
        } else {
            createAlarmNodeFromExistingNode(edit, node, alarmKey, now);
            m_log->trace("Alarm {} moved from shelf", node.path());
            toErase.emplace_back(node.path());
            unshelvedCount += 1;
            change = true;
        }
    }

    if (change) {
        if (shelvedCount > 0 || unshelvedCount > 0) {
            updateAlarmListStats(edit, numberOfListInstances(*alarmRoot, alarmListInstances) - shelvedCount + unshelvedCount, now);
        }
        if (shelvedCount > 0 || unshelvedCount > 0 || movedBetweenShelfs) {
            updateShelvedAlarmListStats(edit, numberOfListInstances(*alarmRoot, shelvedAlarmListInstances) + shelvedCount - unshelvedCount, now);
        }

        utils::removeFromOperationalDS(m_session.getContext(), edit, toErase);
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();

        updateAlarmSummary(m_session);
    }
}
}
