/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "test_alarm_helpers.h"

std::map<std::string, std::string> createAlarmNode(const std::string& id, const std::string& qualifier, const std::string& resource, const std::string& severity, std::map<std::string, std::string> props)
{
    props["resource"] = resource;
    props["alarm-type-id"] = id;
    props["alarm-type-qualifier"] = qualifier;
    props["severity"] = severity;
    return props;
}
