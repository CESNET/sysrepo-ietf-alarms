/*
 * Copyright (C) 2022-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#include <algorithm>
#include <iostream>
#include "test_time_interval.h"
#include "utils/time.h"

bool AnyTimeBetween::operator==(const std::string& str) const
{
    auto tp = alarms::utils::fromYangTimeFormat(str);
    return start <= tp && tp < end;
}

bool operator==(const std::string& str, const AnyTimeBetween& ts)
{
    return ts == str;
}

bool operator==(const std::string& str, const std::variant<std::string, AnyTimeBetween>& v)
{
    return std::visit([&str](auto&& arg) { return str == arg; }, v);
}

std::ostream& operator<<(std::ostream& os, const AnyTimeBetween& o)
{
    return os << "(AnyTimeBetween [" << alarms::utils::yangTimeFormat(o.start) << "] and [" << alarms::utils::yangTimeFormat(o.end) << "])";
}

bool operator==(const std::map<std::string, std::string>& lhs, const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const auto& lhsKv, const auto& rhsKv) {
        return lhsKv.first == rhsKv.first && lhsKv.second == rhsKv.second;
    });
}
