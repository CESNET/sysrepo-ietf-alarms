/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#pragma once
#include <map>
#include <string>

#define CLIENT_ALARM_RPC(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT) \
    [&]() {                                                             \
        auto inp = std::map<std::string, std::string>{                  \
            {"resource", RESOURCE},                                     \
            {"alarm-type-id", ID},                                      \
            {"alarm-type-qualifier", QUALIFIER},                        \
            {"severity", SEVERITY},                                     \
            {"alarm-text", TEXT},                                       \
        };                                                              \
                                                                        \
        auto time = std::chrono::system_clock::now();                   \
        rpcFromSysrepo(*SESS, rpcPrefix, inp);                          \
        return time;                                                    \
    }();

#define CLIENT_PURGE_RPC(SESS, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS)                                                                    \
    {                                                                                                                                                                    \
        auto inp = std::map<std::string, std::string> ADDITIONAL_PARAMS;                                                                                                 \
        inp["alarm-clearance-status"] = CLEARANCE_STATUS;                                                                                                                \
        REQUIRE(rpcFromSysrepo(*SESS, purgeRpcPrefix, inp) == std::map<std::string, std::string>{{"/purged-alarms", std::to_string(EXPECTED_NUMBER_OF_PURGED_ALARMS)}}); \
    }
