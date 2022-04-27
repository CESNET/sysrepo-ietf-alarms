/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#pragma once
#include <string>

namespace libyang {
class DataNode;
}

namespace alarms {

struct Key {
    std::string alarmTypeId;
    std::string alarmTypeQualifier;
    std::string resource;
};

Key getKey(const libyang::DataNode& node);
std::string constructAlarmNodePath(const Key& alarmKey);

}
