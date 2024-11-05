/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#pragma once
#include <functional>
#include "alarms/Key.h"

namespace libyang {
class DataNode;
}

namespace alarms {

struct AlarmEntry;

class PurgeFilter {
public:
    PurgeFilter(const libyang::DataNode& filterInput);
    bool matches(const AlarmEntry& alarmNode) const;

private:
    using Filter = std::function<bool(const AlarmEntry&)>;
    std::vector<Filter> m_filters;
};

class CompressFilter {
public:
    CompressFilter(const libyang::DataNode& filterInput);
    bool matches(const InstanceKey& key) const;

private:
    using Filter = std::function<bool(const InstanceKey&)>;
    std::vector<Filter> m_filters;
};

}

