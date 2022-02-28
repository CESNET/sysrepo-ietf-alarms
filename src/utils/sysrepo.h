/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 */

#pragma once

#include <map>
#include <string>
#include <sysrepo-cpp/Session.hpp>

namespace alarms::utils {

void initLogsSysrepo();
void ensureModuleImplemented(const sysrepo::Session& session, const std::string& module, const std::string& revision);

void valuesToYang(::sysrepo::Session session, const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::optional<libyang::DataNode>& parent);

}
