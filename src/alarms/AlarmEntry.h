#pragma once
#include <chrono>
#include <optional>
#include <string>

namespace alarms {
using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

struct AlarmEntry {
    TimePoint created;
    TimePoint lastRaised;
    TimePoint lastChanged;
    std::string text;
    std::optional<std::string> shelf;
    int32_t lastSeverity;
    bool isCleared;
};
}
