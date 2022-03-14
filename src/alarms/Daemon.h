#pragma once
#include <optional>
#include <sysrepo-cpp/Connection.hpp>
#include "utils/log-fwd.h"

namespace alarms {

class Daemon {
public:
    Daemon();

private:
    sysrepo::Connection m_connection;
    sysrepo::Session m_session;
    std::optional<sysrepo::Subscription> m_rpcSub;
    std::optional<sysrepo::Subscription> m_inventorySub;
    alarms::Log m_log;

    sysrepo::ErrorCode submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input);
    sysrepo::ErrorCode purgeAlarms(const libyang::DataNode& input, libyang::DataNode output);
    libyang::DataNode createStatusChangeNotification(const std::string& alarmNodePath);
};

}
