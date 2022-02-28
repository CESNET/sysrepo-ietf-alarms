/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#pragma once
#include <functional>
#include <libyang-cpp/DataNode.hpp>

namespace alarms {

class PurgeFilter {
public:
    PurgeFilter(const libyang::DataNode& filterInput);
    bool matches(const libyang::DataNode& alarmNode) const;

private:
    using Filter = std::function<bool(const libyang::DataNode&)>;
    std::vector<Filter> m_filters;
};

}

