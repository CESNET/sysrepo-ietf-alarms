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
    alarms::Log m_log;

    sysrepo::ErrorCode submitAlarm(const libyang::DataNode& input);
};

}
