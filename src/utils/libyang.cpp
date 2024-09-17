/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <libyang-cpp/DataNode.hpp>
#include "utils/libyang.h"

namespace alarms::utils {
/** @brief Extract text value of a leaf which is a child of the given parent */
std::string childValue(const libyang::DataNode& node, const std::string& leafName)
{
    auto leaf = node.findPath(leafName);

    if (!leaf) {
        throw std::runtime_error("Selected child '" + leafName + "' does not exist");
    }
    if (!leaf->isTerm()) {
        throw std::runtime_error("Selected child '" + leafName + "' is not a leaf");
    }

    return leaf->asTerm().valueStr();
}
}
