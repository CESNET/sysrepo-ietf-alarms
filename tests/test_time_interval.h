/*
 * Copyright (C) 2022-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#pragma once
#include <chrono>
#include <map>
#include <string>
#include <variant>

class AnyTimeBetween {
    std::chrono::time_point<std::chrono::system_clock> m_start;
    std::chrono::time_point<std::chrono::system_clock> m_end;

public:
    AnyTimeBetween(const std::chrono::time_point<std::chrono::system_clock>& least, const std::chrono::time_point<std::chrono::system_clock>& most);
    bool operator==(const std::string& str) const;

    friend std::ostream& operator<<(std::ostream& os, const AnyTimeBetween& o);
};

bool operator==(const std::string& str, const AnyTimeBetween& ts);
bool operator==(const std::string& str, const std::variant<std::string, AnyTimeBetween>& v);

bool operator==(const std::map<std::string, std::string>& lhs, const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& rhs);

using PropsWithTimeTest = std::map<std::string, std::variant<std::string, AnyTimeBetween>>;

#define SHORTLY_AFTER(point) AnyTimeBetween(point, point + expectedTimeDegreeOfFreedom)
#define BEFORE_INTERVAL(interval) AnyTimeBetween({}, interval.first)
#define IN_INTERVAL(interval) AnyTimeBetween(interval.first, interval.second)
