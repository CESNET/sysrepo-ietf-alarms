#include <string>
#include "Daemon.h"
#include "PurgeFilter.h"
#include "utils/libyang.h"
#include "utils/sysrepo.h"
#include "utils/time.h"

using namespace std::string_literals;

namespace {
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";

struct AlarmKey {
    std::string m_alarmTypeId;
    std::string m_alarmTypeQualifier;
    std::string m_resource;
};

AlarmKey getKey(const libyang::DataNode& node)
{
    return {
        alarms::utils::childValue(node, "alarm-type-id"),
        alarms::utils::childValue(node, "alarm-type-qualifier"),
        alarms::utils::childValue(node, "resource")};
}

std::optional<libyang::DataNode> alarmExists(sysrepo::Session& session, const std::string& path)
{
    if (auto rootNode = session.getData("/ietf-alarms:alarms")) {
        for (const auto& node : rootNode->findXPath(path.c_str())) {
            return node;
        }
    }

    return std::nullopt;
}

}


namespace alarms {

Daemon::Daemon()
    : m_connection(sysrepo::Connection{})
    , m_session(m_connection.sessionStart(sysrepo::Datastore::Operational))
{
    utils::ensureModuleImplemented(m_session, "ietf-alarms", "2019-09-11");
    utils::ensureModuleImplemented(m_session, "sysrepo-ietf-alarms", "2022-02-17");

    m_rpcSub = m_session.onRPCAction(rpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, auto) { return rpcHandler(input); });
    m_rpcSub->onRPCAction(purgeRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeRpcHandler(input, output); });
}

sysrepo::ErrorCode Daemon::rpcHandler(const libyang::DataNode& input)
{
    const auto& alarmKey = getKey(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";
    const auto now = std::chrono::system_clock::now();

    const auto alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + alarmKey.m_alarmTypeId + "'][alarm-type-qualifier='" + alarmKey.m_alarmTypeQualifier + "'][resource='" + alarmKey.m_resource + "']";

    const auto existingAlarmNode = alarmExists(m_session, alarmNodePath);
    std::map<std::string, std::string> res;

    // if passing is-cleared=true the alarm either doesn't exist or exists but is inactive (is-cleared=true), do nothing, it's a NOOP
    if (is_cleared && (!existingAlarmNode || (existingAlarmNode && utils::childValue(*existingAlarmNode, "is-cleared") == "true"))) {
        return sysrepo::ErrorCode::Ok;
    } else if (!existingAlarmNode) {
        res[alarmNodePath + "/time-created"] = utils::yangTimeFormat(now);
    }

    res[alarmNodePath + "/is-cleared"] = is_cleared ? "true" : "false";
    if (!is_cleared && (!existingAlarmNode || (existingAlarmNode && utils::childValue(*existingAlarmNode, "is-cleared") == "true"))) {
        res[alarmNodePath + "/last-raised"] = utils::yangTimeFormat(now);
    }

    if (!is_cleared) {
        res[alarmNodePath + "/perceived-severity"] = severity;
    }

    if (auto node = input.findPath("alarm-text")) {
        res[alarmNodePath + "/alarm-text"] = node.value().asTerm().valueStr();
    }

    std::optional<libyang::DataNode> edit = m_session.getData("/ietf-alarms:alarms");
    utils::valuesToYang(m_session, res, {}, edit);
    m_session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
    m_session.applyChanges();

    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode Daemon::purgeRpcHandler(const libyang::DataNode& input, libyang::DataNode output)
{
    PurgeFilter filter(input);
    std::vector<std::string> toDelete;

    if (auto rootNode = m_session.getData("/ietf-alarms:alarms")) {
        for (const auto& alarmNode : rootNode->findXPath("/ietf-alarms:alarms/alarm-list/alarm")) {
            if (filter(alarmNode)) {
                toDelete.push_back(std::string(alarmNode.path()));
            }
        }
    }

    std::optional<libyang::DataNode> edit = m_session.getData("/ietf-alarms:alarms");
    utils::valuesToYang(m_session, {}, toDelete, edit);
    m_session.editBatch(*edit, sysrepo::DefaultOperation::Merge);
    m_session.applyChanges();

    output.newPath((purgeRpcPrefix + "/purged-alarms"s).c_str(), std::to_string(toDelete.size()).c_str(), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}
}
