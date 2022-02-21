/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once
#include <map>
#include <string>

std::map<std::string, std::string> createAlarmNode(const std::string& id, const std::string& qualifier, const std::string& resource, const std::string& severity, std::map<std::string, std::string> props);

#define P(K, V) \
    {           \
        K, V    \
    }

#define PARAMS(...) \
    {               \
        __VA_ARGS__ \
    }

#define CLIENT_ALARM_RPC(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, ...) \
    rpcFromSysrepo(*SESS, rpcPrefix, createAlarmNode(ID, QUALIFIER, RESOURCE, SEVERITY, __VA_ARGS__));
