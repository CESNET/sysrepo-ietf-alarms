/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#include <algorithm>
#include "PurgeFilter.h"
#include "utils/libyang.h"

namespace alarms {

PurgeFilter::PurgeFilter(const libyang::DataNode& filterInput)
{
    auto clearanceStatus = utils::childValue(filterInput, "alarm-clearance-status");
    m_filters.emplace_back([clearanceStatus](const libyang::DataNode& alarmNode) {
        auto isCleared = utils::childValue(alarmNode, "is-cleared");

        if (clearanceStatus == "any") {
            return true;
        } else if (clearanceStatus == "cleared") {
            return isCleared == "true";
        } else if (clearanceStatus == "not-cleared") {
            return isCleared == "false";
        } else {
            throw std::runtime_error("Invalid alarm-clearance-status value");
        }
    });
}

bool PurgeFilter::operator()(const libyang::DataNode& alarmNode) const
{
    return std::all_of(m_filters.begin(), m_filters.end(), [&](const Filter& filter) { return filter(alarmNode); });
}

}
