#pragma once
#include <optional>
#include <mutex>
#include <sysrepo-cpp/Connection.hpp>
#include <unordered_map>
#include <unordered_set>
#include "AlarmEntry.h"
#include "Key.h"
#include "utils/log-fwd.h"

namespace alarms {

class Daemon {
public:
    Daemon();

    // FIXME: consider boost::concurrent_flat_set (Boost 1.84+) or boost::unordered_flat_set (Boost 1.81+) everywhere

    struct InventoryData {
        std::unordered_set<std::string, boost::hash<std::string>> resources;
        std::set<int32_t> severities;
    };

private:
    sysrepo::Connection m_connection;
    sysrepo::Session m_session;
    alarms::Log m_log;
    std::mutex m_mtx;
    NotifyStatusChanges m_notifyStatusChanges;
    std::optional<int32_t> m_notifySeverityThreshold;
    std::optional<uint16_t> m_maxAlarmStatusChanges;
    bool m_inventoryDirty;
    std::unordered_map<Type, InventoryData, boost::hash<Type>> m_inventory;
    std::unordered_map<InstanceKey, AlarmEntry, boost::hash<InstanceKey>> m_alarms;
    TimePoint m_alarmListLastChanged, m_shelfListLastChanged;
    std::optional<libyang::DataNode> m_shelvingRules;
    std::optional<sysrepo::Subscription> m_alarmSub;
    std::optional<sysrepo::Subscription> m_inventorySub;
    std::optional<libyang::DataNode> m_edit;

    sysrepo::ErrorCode submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input);
    sysrepo::ErrorCode purgeAlarms(const std::string& rpcPath, const libyang::DataNode& rpcInput, libyang::DataNode output);
    sysrepo::ErrorCode compressAlarms(const std::string& rpcPath, const libyang::DataNode& rpcInput, libyang::DataNode output);
    libyang::DataNode createStatusChangeNotification(const libyang::DataNode& alarmNode);
    std::optional<std::string> inventoryValidationError(const InstanceKey& key, const int32_t severity);
    void reshelve(sysrepo::Session running);
    void shrinkStatusChangesLists();
    void rebuildInventory(const libyang::DataNode& dataWithInventory);
    void updateStatistics();
};

}
