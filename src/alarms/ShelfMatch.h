/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#pragma once
#include <libyang-cpp/DataNode.hpp>
#include <optional>

namespace alarms {

struct Key;

std::optional<std::string> findMatchingShelf(const Key& key, const libyang::Set<libyang::DataNode>& shelves);

}
