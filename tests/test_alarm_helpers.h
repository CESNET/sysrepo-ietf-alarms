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
std::map<std::string, std::string> createPurgeNode(const std::string& alarmClearanceStatus, std::map<std::string, std::string> props);

#define P(k, v) \
    {           \
        k, v    \
    }

#define PARAMS(...) \
    {               \
        __VA_ARGS__ \
    }

#define CLIENT_ALARM_RPC(time, sess, id, qualifier, resource, severity, ...)        \
    std::chrono::time_point<std::chrono::system_clock> time;                        \
    {                                                                               \
        auto inp = createAlarmNode(id, qualifier, resource, severity, __VA_ARGS__); \
        time = std::chrono::system_clock::now();                                    \
        rpcFromSysrepo(*sess, rpcPrefix, inp);                                      \
    }

#define CLIENT_PURGE_RPC(sess, expectedPurgedAlarms, clearanceStatus, ...)                                                                                   \
    {                                                                                                                                                        \
        auto inp = createPurgeNode(clearanceStatus, __VA_ARGS__);                                                                                            \
        REQUIRE(rpcFromSysrepo(*sess, purgeRpcPrefix, inp) == std::map<std::string, std::string>{{"/purged-alarms", std::to_string(expectedPurgedAlarms)}}); \
    }
