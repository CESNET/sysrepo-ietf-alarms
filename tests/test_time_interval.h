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

struct AnyTimeBetween {
    std::chrono::time_point<std::chrono::system_clock> start;
    std::chrono::time_point<std::chrono::system_clock> end;

    bool operator==(const std::string& str) const;

    friend std::ostream& operator<<(std::ostream& os, const AnyTimeBetween& o);
};

bool operator==(const std::string& str, const AnyTimeBetween& ts);
bool operator==(const std::string& str, const std::variant<std::string, AnyTimeBetween>& v);
bool operator==(const std::map<std::string, std::string>& lhs, const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& rhs);

using PropsWithTimeTest = std::map<std::string, std::variant<std::string, AnyTimeBetween>>;

#define SHORTLY_AFTER(point) AnyTimeBetween{point, point + std::chrono::milliseconds(300)}
#define BEFORE_INTERVAL(interval) AnyTimeBetween{{}, interval.start}
