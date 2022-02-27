/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#pragma once

#include <chrono>
#include <string>

namespace alarms::utils {

std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint);
std::chrono::time_point<std::chrono::system_clock> fromYangTimeFormat(const std::string& timeStr);

}
