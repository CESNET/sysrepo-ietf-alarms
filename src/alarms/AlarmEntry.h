#pragma once
#include <chrono>
#include <deque>
#include <libyang-cpp/DataNode.hpp>
#include <optional>
#include <string>

namespace alarms {
using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

enum class NotifyStatusChanges {
    All,
    RaiseAndClear,
    BySeverity,
};

struct StatusChange {
    TimePoint time;
    int32_t perceivedSeverity;
    std::string text;
};

struct AlarmEntry {
    TimePoint created;
    TimePoint lastRaised;
    TimePoint lastChanged;
    std::string text;
    std::optional<std::string> shelf;
    int32_t lastSeverity;
    bool isCleared;
    std::deque<StatusChange> statusChanges;

    struct WhatChanged {
        bool changed;
        bool shouldNotify;
        std::vector<TimePoint> removedStatusChanges;
    };

    std::vector<TimePoint> shrinkStatusChanges(const std::optional<uint16_t> maxAlarmStatusChanges);

    WhatChanged updateByRpc(
        const bool wasPresent,
        const TimePoint now,
        const libyang::DataNode& input,
        const std::optional<std::string> shelf,
        const NotifyStatusChanges notifyStatusChanges,
        const std::optional<int32_t> notifySeverityThreshold,
        const std::optional<uint16_t> maxAlarmStatusChanges);
};
}
