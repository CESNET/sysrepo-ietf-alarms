#include <string>
#include "Daemon.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";

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

/** @brief Checks whether node specified by xpath exists in the tree */
bool activeAlarmExist(sysrepo::Session& session, const std::string& path)
{
    return static_cast<bool>(session.getData(path.c_str()));
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
}

sysrepo::ErrorCode Daemon::submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input)
{
    const auto& alarmKey = getKey(input);
    const auto severity = std::string(input.findPath("severity").value().asTerm().valueStr());
    const bool is_cleared = severity == "cleared";

    try {
        const auto alarmNodePath = "/ietf-alarms:alarms/alarm-list/alarm[alarm-type-id='"s + alarmKey.alarmTypeId + "'][alarm-type-qualifier='" + alarmKey.alarmTypeQualifier + "'][resource=" + escapeListKey(alarmKey.resource) + "]";
        m_log->trace("Alarm node: {}", alarmNodePath);

        // if passing is-cleared=true the alarm either doesn't exist or exists but is inactive (is-cleared=true), do nothing, it's a NOOP
        if (auto exists = activeAlarmExist(m_session, alarmNodePath); !exists && is_cleared) {
            return sysrepo::ErrorCode::Ok;
        }

        auto edit = m_session.getContext().newPath(alarmNodePath.c_str(), nullptr, libyang::CreationOptions::Update);

        edit.newPath((alarmNodePath + "/is-cleared").c_str(), is_cleared ? "true" : "false", libyang::CreationOptions::Update);

        if (!is_cleared) {
            edit.newPath((alarmNodePath + "/perceived-severity").c_str(), severity.c_str(), libyang::CreationOptions::Update);
        }

        edit.newPath((alarmNodePath + "/alarm-text").c_str(), std::string(input.findPath("alarm-text").value().asTerm().valueStr()).c_str(), libyang::CreationOptions::Update);

        m_log->trace("Update: {}", std::string(*edit.printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Shrink)));
        m_session.editBatch(edit, sysrepo::DefaultOperation::Merge);
        m_session.applyChanges();

        return sysrepo::ErrorCode::Ok;
    } catch (const std::invalid_argument& e) {
        m_log->debug("submitAlarm exception: {}", e.what());
        rpcSession.setErrorMessage(e.what());
        return sysrepo::ErrorCode::InvalidArgument;
    }
}

}
