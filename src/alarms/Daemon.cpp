#include <string>
#include "Daemon.h"
#include "PurgeFilter.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"
#include "utils/time.h"

using namespace std::string_literals;

namespace {
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";

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

struct AlarmKey {
    std::string alarmTypeId;
    std::string alarmTypeQualifier;
    std::string resource;
};

AlarmKey getKey(const libyang::DataNode& node)
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
}

namespace alarms {

Daemon::Daemon()
    : m_connection(sysrepo::Connection{})
    , m_session(m_connection.sessionStart(sysrepo::Datastore::Operational))
    , m_log(spdlog::get("main"))
{
    utils::ensureModuleImplemented(m_session, "ietf-alarms", "2019-09-11");
    utils::ensureModuleImplemented(m_session, "sysrepo-ietf-alarms", "2022-02-17");

    m_rpcSub = m_session.onRPCAction(rpcPrefix, [&](sysrepo::Session session, auto, auto, const libyang::DataNode input, auto, auto, auto) { return submitAlarm(session, input); });
    m_rpcSub->onRPCAction(purgeRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(input, output); });
}

sysrepo::ErrorCode Daemon::submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input)
{
    const auto& alarmKey = getKey(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";
    const auto now = std::chrono::system_clock::now();

    try {
        const auto alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + alarmKey.alarmTypeId + "'][alarm-type-qualifier='" + alarmKey.alarmTypeQualifier + "'][resource=" + escapeListKey(alarmKey.resource) + "]";
        m_log->trace("Alarm node: {}", alarmNodePath);
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
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();

        auto notification = createStatusChangeNotification(alarmNodePath);
        m_session.sendNotification(notification, sysrepo::Wait::No);

        return sysrepo::ErrorCode::Ok;
    } catch (const std::invalid_argument& e) {
        m_log->debug("submitAlarm exception: {}", e.what());
        rpcSession.setErrorMessage(e.what());
        return sysrepo::ErrorCode::InvalidArgument;
    }
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
        m_session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();
    }

    output.newPath((purgeRpcPrefix + "/purged-alarms"s).c_str(), std::to_string(toDelete.size()).c_str(), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}
}
