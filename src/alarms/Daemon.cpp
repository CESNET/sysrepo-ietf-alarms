#include <string>
#include "Daemon.h"
#include "utils/libyang.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";

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

bool activeAlarmExist(sysrepo::Session& session, const std::string& path)
{
    if (auto rootNode = session.getData("/ietf-alarms:alarms")) {
        for (const auto& node : rootNode->findXPath(path.c_str())) {
            if (alarms::utils::childValue(node, "is-cleared") == "false") {
                return true;
            }
        }
    }

    return false;
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
}

sysrepo::ErrorCode Daemon::rpcHandler(const libyang::DataNode& input)
{
    const auto& alarmKey = getKey(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";

    const auto alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + alarmKey.m_alarmTypeId + "'][alarm-type-qualifier='" + alarmKey.m_alarmTypeQualifier + "'][resource='" + alarmKey.m_resource + "']";

    // if passing is-cleared=true the alarm either doesn't exist or exists but is inactive (is-cleared=true), do nothing, it's a NOOP
    if (auto exists = activeAlarmExist(m_session, alarmNodePath); !exists && is_cleared) {
        return sysrepo::ErrorCode::Ok;
    }

    m_session.setItem(alarmNodePath.c_str(), nullptr);
    m_session.setItem((alarmNodePath + "/is-cleared").c_str(), is_cleared ? "true" : "false");

    if (!is_cleared) {
        m_session.setItem((alarmNodePath + "/perceived-severity").c_str(), severity.c_str());
    }

    if (auto node = input.findPath("alarm-text")) {
        m_session.setItem((alarmNodePath + "/alarm-text").c_str(), std::string(node.value().asTerm().valueStr()).c_str());
    }

    m_session.applyChanges();

    return sysrepo::ErrorCode::Ok;
}

}
