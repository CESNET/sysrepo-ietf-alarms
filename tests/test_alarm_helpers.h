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

#define P(k, v) \
    {           \
        k, v    \
    }

#define PARAMS(...) \
    {               \
        __VA_ARGS__ \
    }

#define CLIENT_ALARM_RPC(sess, id, qualifier, resource, severity, ...) \
    rpcFromSysrepo(*sess, rpcPrefix, createAlarmNode(id, qualifier, resource, severity, __VA_ARGS__));
