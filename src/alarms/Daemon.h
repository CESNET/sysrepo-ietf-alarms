#pragma once
#include <sysrepo-cpp/Connection.hpp>

namespace alarms {

class Daemon {
public:
    Daemon();

private:
    sysrepo::Connection m_connection;
    sysrepo::Session m_session;
    sysrepo::Subscription m_rpcSub;

    sysrepo::ErrorCode rpcHandler(const libyang::DataNode& input);
    sysrepo::ErrorCode purgeRpcHandler(const libyang::DataNode& input, libyang::DataNode output);
};

}
