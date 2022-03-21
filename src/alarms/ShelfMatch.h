/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#pragma once
#include <libyang-cpp/DataNode.hpp>
#include <optional>

namespace alarms {

struct AlarmKey;

std::optional<std::string> findMatchingShelf(const AlarmKey& key, const libyang::Set<libyang::DataNode>& shelfs);

}
