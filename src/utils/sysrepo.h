/*
 * Copyright (C) 2020, 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#pragma once

#include <string>
#include <vector>

namespace sysrepo {
class Connection;
class Session;
}

namespace alarms::utils {

void initLogsSysrepo();
void ensureModuleImplemented(const sysrepo::Session& session, const std::string& module, const std::string& revision);

void removeFromOperationalDS(::sysrepo::Connection connection, const std::vector<std::string>& removePaths);
}
