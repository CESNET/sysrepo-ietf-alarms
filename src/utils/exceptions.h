/*
 * Copyright (C) 2016-2019 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#include "utils/log-fwd.h"

namespace alarms::utils {
void fatalException [[noreturn]] (alarms::Log log, const std::exception& e, const std::string& when);
}
