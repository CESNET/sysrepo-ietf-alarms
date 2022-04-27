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

    std::string alarmPath() const;
    static Key fromNode(const libyang::DataNode& node);
};

}
