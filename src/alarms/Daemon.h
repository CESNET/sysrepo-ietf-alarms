#pragma once
#include <optional>
#include <sysrepo-cpp/Connection.hpp>
#include "Key.h"
#include "utils/log-fwd.h"

namespace alarms {

class Daemon {
public:
    Daemon();
    enum class NotifyStatusChanges {
        All,
        RaiseAndClear,
        BySeverity,
    };

private:
    sysrepo::Connection m_connection;
    sysrepo::Session m_session;
    std::optional<sysrepo::Subscription> m_alarmSub;
    std::optional<sysrepo::Subscription> m_inventorySub;
    alarms::Log m_log;
    NotifyStatusChanges m_notifyStatusChanges;
    std::optional<int32_t> m_notifySeverityThreshold;

    sysrepo::ErrorCode submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input);
    sysrepo::ErrorCode purgeAlarms(const std::string& rpcPath, const std::string& alarmListXPath, const libyang::DataNode& rpcInput, libyang::DataNode output);
    libyang::DataNode createStatusChangeNotification(const libyang::DataNode& alarmNode);
    std::optional<std::string> inventoryValidationError(const libyang::DataNode& alarmRoot, const Key& key, const std::string& severity);
    void reshelve();
};

}
