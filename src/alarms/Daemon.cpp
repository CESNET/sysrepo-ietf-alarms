#include <string>
#include "AlarmKey.h"
#include "Daemon.h"
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
const auto alarmInventory = "/ietf-alarms:alarms/alarm-inventory";
const auto control = "/ietf-alarms:alarms/control";

/** @brief Escapes key with the other type of quotes than found in the string.
 *
 *  @throws std::invalid_argument if both single and double quotes used in the input
 * */
std::string escapeListKey(const std::string& str)
{
    auto singleQuotes = str.find('\'') != std::string::npos;
    auto doubleQuotes = str.find('\"') != std::string::npos;

    if (singleQuotes && doubleQuotes) {
        throw std::invalid_argument("Encountered mixed single and double quotes in XPath; can't properly escape.");
    } else if (singleQuotes) {
        return '\"' + str + '\"';
    } else {
        return '\'' + str + '\'';
    }
}

/** @brief returns number of list instances in the list specified by xPath */
size_t numberOfListInstances(sysrepo::Session& session, const std::string& xPath)
{
    auto data = session.getData(xPath.c_str());
    return data ? data->findXPath(xPath.c_str()).size() : 0;
}

void updateAlarmListStats(libyang::DataNode& edit, size_t alarmCount, const std::chrono::time_point<std::chrono::system_clock>& lastChanged)
{
    // number-of-alarms is of type yang:gauge32. If we ever support more than 2^32-1 alarms then we will have to deal with cropping the value.
    edit.newPath("/ietf-alarms:alarms/alarm-list/number-of-alarms", std::to_string(alarmCount).c_str());
    edit.newPath("/ietf-alarms:alarms/alarm-list/last-changed", alarms::utils::yangTimeFormat(lastChanged).c_str());
}

alarms::AlarmKey getKey(const libyang::DataNode& node)
{
    return {
        alarms::utils::childValue(node, "alarm-type-id"),
        alarms::utils::childValue(node, "alarm-type-qualifier"),
        alarms::utils::childValue(node, "resource")};
}

/** @brief Returns node specified by xpath in the tree */
std::optional<libyang::DataNode> activeAlarmExist(sysrepo::Session& session, const std::string& path)
{
    if (auto data = session.getData(path.c_str())) {
        return data->findPath(path.c_str());
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

/* @brief Checks if we should notify about changes made based on the values changed and current settings in control container */
bool shouldNotifyStatusChange(sysrepo::Session session, const std::optional<libyang::DataNode>& oldNode, const libyang::DataNode& edit)
{
    auto oldds = session.activeDatastore();
    session.switchDatastore(sysrepo::Datastore::Running);
    auto data = session.getData(control);
    auto notifyStatusChangesNode = data->findPath((control + "/notify-status-changes"s).c_str());
    session.switchDatastore(oldds);

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

    auto notifySeverityLevelNode = data->findPath((control + "/notify-severity-level"s).c_str());
    auto notifySeverityLevelValue = std::get<libyang::Enum>(notifySeverityLevelNode->asTerm().value()).value;

    auto newSeverity = std::get<libyang::Enum>(edit.findPath("perceived-severity")->asTerm().value()).value;
    if (!oldNode) {
        return newSeverity >= notifySeverityLevelValue;
    }

    auto oldSeverity = std::get<libyang::Enum>(oldNode->findPath("perceived-severity")->asTerm().value()).value;
    return newSeverity >= notifySeverityLevelValue || (newSeverity < oldSeverity && oldSeverity >= notifySeverityLevelValue && newSeverity < notifySeverityLevelValue);
}

/* @brief Checks if the alarm keys match any entry in ietf-alarms:alarms/control/alarm-shelving. If so, return name of the matched shelf */
std::optional<std::string> shouldBeShelved(sysrepo::Session session, const alarms::AlarmKey& key)
{
    auto data = session.getData("/ietf-alarms:alarms/control/alarm-shelving");
    if (!data) {
        return std::nullopt;
    }

    auto shelfs = data->findXPath("/ietf-alarms:alarms/control/alarm-shelving/shelf");
    for (const auto& shelfNode : shelfs) {
        const auto shelfName = alarms::utils::childValue(shelfNode, "name");
        spdlog::trace("Shelf {}", shelfName);

        if (alarms::ShelfMatch(shelfNode).match(key)) {
            return shelfName;
        }

        /* Each entry defines the criteria for shelving alarms.
         * Criteria are ANDed. If no criteria are specified, all alarms will be shelved.
         */
    }

    return std::nullopt;
}
}

namespace alarms {

Daemon::Daemon()
    : m_connection(sysrepo::Connection{})
    , m_session(m_connection.sessionStart(sysrepo::Datastore::Operational))
    , m_shelvingEnabled(false)
    , m_log(spdlog::get("main"))
{
    utils::ensureModuleImplemented(m_session, ietfAlarmsModule, "2019-09-11");
    utils::ensureModuleImplemented(m_session, "sysrepo-ietf-alarms", "2022-02-17");

    if (utils::featureEnabled(m_session, ietfAlarmsModule, "2019-09-11", "alarm-shelving")) {
        m_shelvingEnabled = true;
        m_log->info("Enabled ietf-alarms feature alarm-shelving");
    }

    m_rpcSub = m_session.onRPCAction(rpcPrefix, [&](sysrepo::Session session, auto, auto, const libyang::DataNode input, auto, auto, auto) { return submitAlarm(session, input); });
    m_rpcSub->onRPCAction(purgeRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(input, output); });

    m_inventorySub = m_session.onModuleChange(
        ietfAlarmsModule, [&](auto, auto, auto, auto, auto, auto) {
            auto notification = m_session.getContext().newPath("/ietf-alarms:alarm-inventory-changed", nullptr);
            m_session.sendNotification(notification, sysrepo::Wait::No);
            return sysrepo::ErrorCode::Ok;
        },
        alarmInventory,
        0,
        sysrepo::SubscribeOptions::DoneOnly | sysrepo::SubscribeOptions::Passive);


    libyang::DataNode edit = m_session.getContext().newPath(alarmList);
    updateAlarmListStats(edit, 0, std::chrono::system_clock::now());
    m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
    m_session.applyChanges();
}

sysrepo::ErrorCode Daemon::submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input)
{
    const auto& alarmKey = getKey(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";
    const auto now = std::chrono::system_clock::now();

    std::string alarmNodePath;

    try {
        if (m_shelvingEnabled && shouldBeShelved(m_session, alarmKey)) {
            alarmNodePath = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
        } else {
            alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm";
        }

        alarmNodePath += "[alarm-type-id='"s + alarmKey.alarmTypeId + "'][alarm-type-qualifier='" + alarmKey.alarmTypeQualifier + "'][resource=" + escapeListKey(alarmKey.resource) + "]";
        m_log->trace("Alarm node: {}", alarmNodePath);
    } catch (const std::invalid_argument& e) {
        m_log->debug("submitAlarm exception: {}", e.what());
        rpcSession.setErrorMessage(e.what());
        return sysrepo::ErrorCode::InvalidArgument;
    }

    const auto existingAlarmNode = activeAlarmExist(m_session, alarmNodePath);

    auto edit = m_session.getContext().newPath(alarmNodePath.c_str(), nullptr, libyang::CreationOptions::Update);

    if (is_cleared && (!existingAlarmNode || (existingAlarmNode && utils::childValue(*existingAlarmNode, "is-cleared") == "true"))) {
        // if passing is-cleared=true the alarm either doesn't exist or exists but is inactive (is-cleared=true), do nothing, it's a NOOP
        return sysrepo::ErrorCode::Ok;
    } else if (!existingAlarmNode) {
        edit.newPath((alarmNodePath + "/time-created").c_str(), utils::yangTimeFormat(now).c_str());
    }

    edit.newPath((alarmNodePath + "/is-cleared").c_str(), is_cleared ? "true" : "false", libyang::CreationOptions::Update);
    if (!is_cleared && (!existingAlarmNode || (existingAlarmNode && utils::childValue(*existingAlarmNode, "is-cleared") == "true"))) {
        edit.newPath((alarmNodePath + "/last-raised").c_str(), utils::yangTimeFormat(now).c_str());
    }

    if (!is_cleared) {
        edit.newPath((alarmNodePath + "/perceived-severity").c_str(), severity.c_str(), libyang::CreationOptions::Update);
    }

    edit.newPath((alarmNodePath + "/alarm-text").c_str(), std::string(input.findPath("alarm-text").value().asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);

    const auto& editAlarmNode = edit.findPath(alarmNodePath.c_str());
    if (valueChanged(existingAlarmNode, *editAlarmNode, "alarm-text") || valueChanged(existingAlarmNode, *editAlarmNode, "is-cleared") || valueChanged(existingAlarmNode, *editAlarmNode, "perceived-severity")) {
        edit.newPath((alarmNodePath + "/last-changed").c_str(), utils::yangTimeFormat(now).c_str());
    }

    m_log->trace("Update: {}", std::string(*edit.printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Shrink)));

    updateAlarmListStats(edit, numberOfListInstances(m_session, alarmListInstances) + static_cast<int>(static_cast<bool>(editAlarmNode->findPath("time-created"))), now);
    m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
    m_session.applyChanges();

    if (shouldNotifyStatusChange(m_session, existingAlarmNode, *editAlarmNode)) {
        auto notification = createStatusChangeNotification(alarmNodePath);
        m_session.sendNotification(notification, sysrepo::Wait::No);
    }

    return sysrepo::ErrorCode::Ok;
}

libyang::DataNode Daemon::createStatusChangeNotification(const std::string& alarmNodePath)
{
    static const std::string prefix = "/ietf-alarms:alarm-notification";
    libyang::DataNode alarmNode = activeAlarmExist(m_session, alarmNodePath).value();

    auto notification = m_session.getContext().newPath((prefix + "/resource").c_str(), utils::childValue(alarmNode, "resource").c_str());
    notification.newPath((prefix + "/alarm-type-id").c_str(), utils::childValue(alarmNode, "alarm-type-id").c_str());
    notification.newPath((prefix + "/time").c_str(), utils::childValue(alarmNode, "last-changed").c_str());
    notification.newPath((prefix + "/alarm-text").c_str(), utils::childValue(alarmNode, "alarm-text").c_str());

    if (auto qualifier = utils::childValue(alarmNode, "alarm-type-qualifier"); !qualifier.empty()) {
        notification.newPath((prefix + "/alarm-type-qualifier").c_str(), qualifier.c_str());
    }

    notification.newPath((prefix + "/perceived-severity").c_str(), utils::childValue(alarmNode, "is-cleared") == "true" ? "cleared" : utils::childValue(alarmNode, "perceived-severity").c_str());

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
        std::optional<libyang::DataNode> edit;
        utils::removeNodes(m_session, toDelete, edit);
        updateAlarmListStats(*edit, numberOfListInstances(m_session, alarmListInstances) - toDelete.size(), now);
        m_session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();
    }

    output.newPath((purgeRpcPrefix + "/purged-alarms"s).c_str(), std::to_string(toDelete.size()).c_str(), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}
}
