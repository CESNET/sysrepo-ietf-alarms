/*
 * Copyright (C) 2022-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#include <algorithm>
#include <iostream>
#include "test_time_interval.h"
#include "utils/time.h"

AnyTimeBetween::AnyTimeBetween(const std::chrono::time_point<std::chrono::system_clock>& start, const std::chrono::time_point<std::chrono::system_clock>& end)
    : m_start(start)
    , m_end(end)
{
}

bool AnyTimeBetween::operator==(const std::string& str) const
{
    auto tp = alarms::utils::fromYangTimeFormat(str);
    return m_start <= tp && tp <= m_end;
}

bool operator==(const std::string& str, const AnyTimeBetween& ts)
{
    return ts == str;
}

std::ostream& operator<<(std::ostream& os, const AnyTimeBetween& o)
{
    return os << "(AnyTimeBetween [" << alarms::utils::yangTimeFormat(o.m_start) << "] and [" << alarms::utils::yangTimeFormat(o.m_end) << "])";
}

bool operator==(const std::map<std::string, std::string>& lhs, const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const auto& lhsKv, const auto& rhsKv) {
        if (lhsKv.first != rhsKv.first) {
            return false;
        }

        // call operator== on std::string and the actual type stored in the variant
        return std::visit([&lhsKv](auto&& arg) { return lhsKv.second == arg; }, rhsKv.second);
    });
}
