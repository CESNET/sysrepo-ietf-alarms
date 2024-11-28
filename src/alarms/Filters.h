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

class AlarmFilter {
public:
    bool matches(const InstanceKey& key, const AlarmEntry& alarmNode) const;

protected:
    AlarmFilter() = default; // disable public instantiation of this class
    std::vector<std::function<bool(const InstanceKey&, const AlarmEntry&)>> m_filters;
};

class PurgeFilter : public AlarmFilter {
public:
    PurgeFilter(const libyang::DataNode& filterInput);
};

class CompressFilter : public AlarmFilter {
public:
    CompressFilter(const libyang::DataNode& filterInput);
};

}

