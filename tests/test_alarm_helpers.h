/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#pragma once
#include <map>
#include <string>
#include <test_time_interval.h>
#include "utils/sysrepo.h"

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
        auto intervalStart = std::chrono::system_clock::now();          \
        rpcFromSysrepo(*SESS, rpcPrefix, inp);                          \
        auto intervalEnd = std::chrono::system_clock::now();            \
        return AnyTimeBetween{intervalStart, intervalEnd};              \
    }();

#define CLIENT_INTRODUCE_ALARM(SESS, ID, QUALIFIER, RESOURCES, SEVERITIES, DESCRIPTION)                                                                                              \
    {                                                                                                                                                                                \
        alarms::utils::ScopedDatastoreSwitch s(*SESS, sysrepo::Datastore::Operational);                                                                                              \
                                                                                                                                                                                     \
        SESS->setItem(std::string{alarmInventoryPrefix} + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/description", DESCRIPTION);             \
        SESS->setItem(std::string{alarmInventoryPrefix} + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/will-clear", "true");                   \
        for (const auto& resource : std::vector<std::string> RESOURCES) {                                                                                                            \
            SESS->setItem(std::string{alarmInventoryPrefix} + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/resource", resource.c_str());       \
        }                                                                                                                                                                            \
        for (const auto& severity : std::vector<std::string> SEVERITIES) {                                                                                                           \
            SESS->setItem(std::string{alarmInventoryPrefix} + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/severity-level", severity.c_str()); \
        }                                                                                                                                                                            \
        SESS->applyChanges();                                                                                                                                                        \
    }

#define CLIENT_PURGE_RPC_IMPL(SESS, RPCPATH, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS)                                               \
    [&]() {                                                                                                                                                       \
        auto inp = std::map<std::string, std::string> ADDITIONAL_PARAMS;                                                                                          \
        inp["alarm-clearance-status"] = CLEARANCE_STATUS;                                                                                                         \
        auto intervalStart = std::chrono::system_clock::now();                                                                                                    \
        REQUIRE(rpcFromSysrepo(*SESS, RPCPATH, inp) == std::map<std::string, std::string>{{"/purged-alarms", std::to_string(EXPECTED_NUMBER_OF_PURGED_ALARMS)}}); \
        auto intervalEnd = std::chrono::system_clock::now();                                                                                                      \
        return AnyTimeBetween{intervalStart, intervalEnd};                                                                                                        \
    }();

#define CLIENT_PURGE_RPC(SESS, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS) CLIENT_PURGE_RPC_IMPL(SESS, purgeRpcPrefix, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS)
#define CLIENT_PURGE_SHELVED_RPC(SESS, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS) CLIENT_PURGE_RPC_IMPL(SESS, purgeShelvedRpcPrefix, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS)
