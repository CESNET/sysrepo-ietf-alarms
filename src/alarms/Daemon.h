#pragma once
#include <optional>
#include <sysrepo-cpp/Connection.hpp>

namespace alarms {

class Daemon {
public:
    Daemon();

private:
    sysrepo::Connection m_connection;
    sysrepo::Session m_session;
    std::optional<sysrepo::Subscription> m_rpcSub;

    sysrepo::ErrorCode rpcHandler(const libyang::DataNode& input);
};

}
