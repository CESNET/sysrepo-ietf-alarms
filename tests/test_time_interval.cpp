/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include <algorithm>
#include <iostream>
#include "test_time_interval.h"
#include "utils/time.h"

AnyTimeBetween::AnyTimeBetween(std::chrono::time_point<std::chrono::system_clock> least, std::chrono::time_point<std::chrono::system_clock> most)
    : m_least(std::move(least))
    , m_most(std::move(most))
{
}

bool AnyTimeBetween::operator==(const std::string& str) const
{
    auto tp = alarms::utils::fromYangTimeFormat(str);
    return m_least <= tp && tp <= m_most;
}

bool operator==(const std::string& str, const AnyTimeBetween& ts)
{
    return ts == str;
}

std::ostream& operator<<(std::ostream& os, const AnyTimeBetween& o)
{
    return os << "(AnyTimeBetween [" << alarms::utils::yangTimeFormat(o.m_least) << "] and [" << alarms::utils::yangTimeFormat(o.m_most) << "])";
}

bool operator==(const std::map<std::string, std::string>& lhs, const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const auto& lhsKv, const auto& rhsKv) {
        if (lhsKv.first != rhsKv.first) {
            return false;
        }

        return std::visit([&lhsKv](auto&& arg) { return lhsKv.second == arg; }, rhsKv.second);
    });
}
