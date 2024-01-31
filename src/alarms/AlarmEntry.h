#pragma once
#include <chrono>
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

struct AlarmEntry {
    TimePoint created;
    TimePoint lastRaised;
    TimePoint lastChanged;
    std::string text;
    std::optional<std::string> shelf;
    int32_t lastSeverity;
    bool isCleared;

    struct WhatChanged {
        bool changed;
        bool shouldNotify;
    };

    WhatChanged updateByRpc(
        const bool wasPresent,
        const TimePoint now,
        const libyang::DataNode& input,
        const std::optional<std::string> shelf,
        const NotifyStatusChanges notifyStatusChanges,
        const std::optional<int32_t> notifySeverityThreshold);
};
}
