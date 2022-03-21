/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#pragma once
#include <libyang-cpp/DataNode.hpp>

namespace alarms {

struct AlarmKey;

bool shelfMatch(const libyang::DataNode& shelfNode, const AlarmKey& key);
}
