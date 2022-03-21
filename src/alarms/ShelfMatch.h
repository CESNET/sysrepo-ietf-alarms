/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#pragma once
#include <functional>
#include <libyang-cpp/DataNode.hpp>

namespace alarms {

struct AlarmKey;

class ShelfMatch {
public:
    ShelfMatch(const libyang::DataNode& shelfNode);
    bool match(const AlarmKey& alarmKey);

private:
    libyang::DataNode m_shelfNode;
    std::vector<std::function<bool(const AlarmKey& alarmKey)>> m_criteria;
};
}
