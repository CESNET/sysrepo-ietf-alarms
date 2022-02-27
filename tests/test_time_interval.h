#pragma once
#include <chrono>
#include <map>
#include <string>
#include <variant>

class AnyTimeBetween {
    std::chrono::time_point<std::chrono::system_clock> m_least;
    std::chrono::time_point<std::chrono::system_clock> m_most;

public:
    AnyTimeBetween(std::chrono::time_point<std::chrono::system_clock> least, std::chrono::time_point<std::chrono::system_clock> most);
    bool operator==(const std::string& str) const;

    friend std::ostream& operator<<(std::ostream& os, const AnyTimeBetween& o);
};

bool operator==(const std::string& str, const AnyTimeBetween& ts);

bool operator==(const std::map<std::string, std::string>& lhs, const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& rhs);

#define EXPECT_TIME_INTERVAL(point) AnyTimeBetween(point, point + expectedTimeDegreeOfFreedom)
