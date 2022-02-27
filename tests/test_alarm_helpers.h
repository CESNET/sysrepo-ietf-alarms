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
