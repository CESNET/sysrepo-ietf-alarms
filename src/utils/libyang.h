/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once
#include <string>

namespace libyang {
class DataNode;
}

namespace alarms::utils {
std::string childValue(const libyang::DataNode& node, const std::string& name);
}
