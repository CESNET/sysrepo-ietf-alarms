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
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto ietfAlarmsModule = "ietf-alarms";
const auto alarmList = "/ietf-alarms:alarms/alarm-list";
const auto alarmListInstances = "/ietf-alarms:alarms/alarm-list/alarm";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";
const auto alarmInventoryPrefix = "/ietf-alarms:alarms/alarm-inventory";
const auto controlPrefix = "/ietf-alarms:alarms/control";

/** @brief returns number of list instances in the list specified by xPath */
size_t numberOfListInstances(sysrepo::Session& session, const std::string& xPath)
{
    auto data = session.getData(xPath);
    return data ? data->findXPath(xPath).size() : 0;
}

void updateAlarmListStats(libyang::DataNode& edit, size_t alarmCount, const std::chrono::time_point<std::chrono::system_clock>& lastChanged)
{
    // number-of-alarms is of type yang:gauge32. If we ever support more than 2^32-1 alarms then we will have to deal with cropping the value.
    edit.newPath("/ietf-alarms:alarms/alarm-list/number-of-alarms", std::to_string(alarmCount));
    edit.newPath("/ietf-alarms:alarms/alarm-list/last-changed", alarms::utils::yangTimeFormat(lastChanged));
}

/** @brief Returns node specified by xpath in the tree */
std::optional<libyang::DataNode> activeAlarmExist(sysrepo::Session& session, const std::string& path)
{
    if (auto data = session.getData(path)) {
        return data->findPath(path);
    }
    return std::nullopt;
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
bool shouldNotifyStatusChange(sysrepo::Session session, const std::optional<libyang::DataNode>& oldNode, const libyang::DataNode& edit)
{
    auto oldDatastore = session.activeDatastore();
    session.switchDatastore(sysrepo::Datastore::Running);
    auto data = session.getData(controlPrefix);
    auto notifyStatusChangesNode = data->findPath(controlPrefix + "/notify-status-changes"s);
    session.switchDatastore(oldDatastore);

    bool raised = edit.findPath("is-cleared") && alarms::utils::childValue(edit, "is-cleared") == "false" && valueChanged(oldNode, edit, "is-cleared");
    bool cleared = edit.findPath("is-cleared") && alarms::utils::childValue(edit, "is-cleared") == "true" && valueChanged(oldNode, edit, "is-cleared");

    auto notifyStatusChangesValue = notifyStatusChangesNode->asTerm().valueStr();
    if (cleared || notifyStatusChangesValue == "all-state-changes") {
        return true;
    }

    if (notifyStatusChangesValue == "raise-and-clear") {
        return raised || cleared;
    }

    /* Notifications are sent for status changes equal to or above the specified severity level.
     * Notifications shall also be sent for state changes that make an alarm less severe than the specified level.
     */

    auto notifySeverityLevelValue = std::get<libyang::Enum>(data->findPath(controlPrefix + "/notify-severity-level"s)->asTerm().value()).value;

    auto newSeverity = std::get<libyang::Enum>(edit.findPath("perceived-severity")->asTerm().value()).value;
    if (!oldNode) {
        return newSeverity >= notifySeverityLevelValue;
    }

    auto oldSeverity = std::get<libyang::Enum>(oldNode->findPath("perceived-severity")->asTerm().value()).value;
    return newSeverity >= notifySeverityLevelValue || (oldSeverity >= notifySeverityLevelValue && newSeverity < notifySeverityLevelValue);
}

/* @brief Checks if the alarm keys match any entry in ietf-alarms:alarms/control/alarm-shelving. If so, return name of the matched shelf */
std::optional<std::string> shouldBeShelved(sysrepo::Session session, const alarms::Key& key)
{
    auto oldDatastore = session.activeDatastore();
    session.switchDatastore(sysrepo::Datastore::Running);

    std::optional<std::string> shelfName;
    if (auto data = session.getData("/ietf-alarms:alarms/control/alarm-shelving")) {
        shelfName = findMatchingShelf(key, data->findXPath("/ietf-alarms:alarms/control/alarm-shelving/shelf"));
    }

    session.switchDatastore(oldDatastore);
    return shelfName;
}
}

namespace alarms {

Daemon::Daemon()
    : m_connection(sysrepo::Connection{})
    , m_session(m_connection.sessionStart(sysrepo::Datastore::Operational))
    , m_log(spdlog::get("main"))
{
    utils::ensureModuleImplemented(m_session, ietfAlarmsModule, "2019-09-11", {"alarm-shelving"});
    utils::ensureModuleImplemented(m_session, "sysrepo-ietf-alarms", "2022-02-17");

    {
        auto edit = m_session.getContext().newPath(alarmList);
        updateAlarmListStats(edit, 0, std::chrono::system_clock::now());
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();
    }

    m_rpcSub = m_session.onRPCAction(rpcPrefix, [&](sysrepo::Session session, auto, auto, const libyang::DataNode input, auto, auto, auto) { return submitAlarm(session, input); });
    m_rpcSub->onRPCAction(purgeRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(input, output); });

    m_inventorySub = m_session.onModuleChange(
        ietfAlarmsModule, [&](auto, auto, auto, auto, auto, auto) {
            m_session.sendNotification(m_session.getContext().newPath("/ietf-alarms:alarm-inventory-changed", std::nullopt), sysrepo::Wait::No);
            return sysrepo::ErrorCode::Ok;
        },
        alarmInventoryPrefix,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);
}

sysrepo::ErrorCode Daemon::submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input)
{
    const auto& alarmKey = Key::fromNode(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";
    const auto now = std::chrono::system_clock::now();

    auto matchedShelf = shouldBeShelved(m_session, alarmKey);
    std::string alarmNodePath;

    if (matchedShelf) {
        alarmNodePath = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
    } else {
        alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm";
    }

    try {
        alarmNodePath = matchedShelf ? alarmKey.shelvedAlarmPath() : alarmKey.alarmPath();
        m_log->trace("Alarm node: {}", alarmNodePath);
    } catch (const std::invalid_argument& e) {
        m_log->debug("submitAlarm exception: {}", e.what());
        rpcSession.setErrorMessage(e.what());
        return sysrepo::ErrorCode::InvalidArgument;
    }

    const auto existingAlarmNode = activeAlarmExist(m_session, alarmNodePath);

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
        updateAlarmListStats(edit, numberOfListInstances(m_session, alarmListInstances) + static_cast<int>(editAlarmNode->findPath("time-created").has_value()), now);
    } else {
        edit.newPath(alarmNodePath + "/shelf-name", matchedShelf);
    }

    m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
    m_session.applyChanges();

    if (shouldNotifyStatusChange(m_session, existingAlarmNode, *editAlarmNode)) {
        m_session.sendNotification(createStatusChangeNotification(alarmNodePath), sysrepo::Wait::No);
    }

    return sysrepo::ErrorCode::Ok;
}

libyang::DataNode Daemon::createStatusChangeNotification(const std::string& alarmNodePath)
{
    static const std::string prefix = "/ietf-alarms:alarm-notification";
    libyang::DataNode alarmNode = activeAlarmExist(m_session, alarmNodePath).value();

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

sysrepo::ErrorCode Daemon::purgeAlarms(const libyang::DataNode& rpcInput, libyang::DataNode output)
{
    const auto now = std::chrono::system_clock::now();
    PurgeFilter filter(rpcInput);
    std::vector<std::string> toDelete;

    if (auto rootNode = m_session.getData("/ietf-alarms:alarms")) {
        for (const auto& alarmNode : rootNode->findXPath("/ietf-alarms:alarms/alarm-list/alarm")) {
            if (filter.matches(alarmNode)) {
                toDelete.push_back(std::string(alarmNode.path()));
            }
        }
    }

    if (!toDelete.empty()) {
        /* FIXME: This does not ensure atomicity of the operation, i.e., keeping alarm-list's number-of-alarms leaf up to date is not synced with the deletion.
         * At first, the alarms are removed one-by-one and only after all alarms are deleted we update the alarm counter and commit the edit with updated counter.
         */
        utils::removeFromOperationalDS(m_connection, toDelete);

        auto edit = m_session.getContext().newPath(alarmList);
        updateAlarmListStats(edit, numberOfListInstances(m_session, alarmListInstances), now);
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();
    }

    output.newPath(purgeRpcPrefix + "/purged-alarms"s, std::to_string(toDelete.size()), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}
}
