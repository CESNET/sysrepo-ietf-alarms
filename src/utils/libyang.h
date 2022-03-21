/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once
#include <string>
#include <vector>

namespace libyang {
class DataNode;
class Identity;
}

namespace alarms::utils {

std::string childValue(const libyang::DataNode& node, const std::string& name);
std::vector<libyang::Identity> getIdentitiesDerivedFrom(const libyang::Identity& baseIdentity);

}
