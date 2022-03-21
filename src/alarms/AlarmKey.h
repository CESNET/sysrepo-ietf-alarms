/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#pragma once
#include <string>

namespace alarms {

struct AlarmKey {
    std::string alarmTypeId;
    std::string alarmTypeQualifier;
    std::string resource;
};
}
