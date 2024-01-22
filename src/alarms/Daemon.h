#pragma once
#include <optional>
#include <mutex>
#include <sysrepo-cpp/Connection.hpp>
#include <unordered_map>
#include <unordered_set>
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

    // FIXME: consider boost::concurrent_flat_set (Boost 1.84+) or boost::unordered_flat_set (Boost 1.81+) everywhere

    struct InventoryData {
        std::unordered_set<std::string, boost::hash<std::string>> resources;
        std::unordered_set<std::string, boost::hash<std::string>> severities;
    };

private:
    sysrepo::Connection m_connection;
    sysrepo::Session m_session;
    std::optional<sysrepo::Subscription> m_alarmSub;
    std::optional<sysrepo::Subscription> m_inventorySub;
    alarms::Log m_log;
    NotifyStatusChanges m_notifyStatusChanges;
    std::optional<int32_t> m_notifySeverityThreshold;
    std::mutex m_mtx;
    std::unordered_map<Type, InventoryData, boost::hash<Type>> m_inventory;

    sysrepo::ErrorCode submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input);
    sysrepo::ErrorCode purgeAlarms(const std::string& rpcPath, const std::string& alarmListXPath, const libyang::DataNode& rpcInput, libyang::DataNode output);
    libyang::DataNode createStatusChangeNotification(const libyang::DataNode& alarmNode);
    std::optional<std::string> inventoryValidationError(const InstanceKey& key, const std::string& severity);
    void reshelve();
};

}
