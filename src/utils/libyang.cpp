/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <libyang-cpp/DataNode.hpp>
#include "utils/libyang.h"

namespace alarms::utils {
std::string childValue(const libyang::DataNode& node, const std::string& leafName)
{
    auto leaf = node.findPath(leafName.c_str());

    if (!leaf) {
        throw std::runtime_error("Selected child does not exist");
    }
    if (!leaf->isTerm()) {
        throw std::runtime_error("Selected child is not a leaf");
    }

    return std::string(leaf->asTerm().valueStr());
}
}
