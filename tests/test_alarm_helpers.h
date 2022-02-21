/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#pragma once
#include <map>
#include <string>

#define CLIENT_ALARM_RPC(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT)       \
    rpcFromSysrepo(*SESS, rpcPrefix, std::map<std::string, std::string>{      \
                                         {"resource", RESOURCE},              \
                                         {"alarm-type-id", ID},               \
                                         {"alarm-type-qualifier", QUALIFIER}, \
                                         {"severity", SEVERITY},              \
                                         {"alarm-text", TEXT},                \
                                     })
