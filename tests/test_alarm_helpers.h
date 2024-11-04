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

namespace {
using namespace std::string_literals;

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";

const auto ietfAlarmsModule = "ietf-alarms";
const auto ietfAlarms = "/ietf-alarms:alarms";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";
const auto purgeShelvedRpcPrefix = "/ietf-alarms:alarms/shelved-alarms/purge-shelved-alarms";
const auto alarmInventoryPrefix = "/ietf-alarms:alarms/alarm-inventory"s;
const auto alarmList = "/ietf-alarms:alarms/alarm-list";
const auto alarmListInstances = "/ietf-alarms:alarms/alarm-list/alarm";
const auto shelvedAlarmList = "/ietf-alarms:alarms/shelved-alarms";
const auto shelvedAlarmListInstances = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
const auto controlShelf = "/ietf-alarms:alarms/control/alarm-shelving/shelf";
const auto alarmStatusNotification = "/ietf-alarms:alarm-notification";
const auto inventoryNotification = "/ietf-alarms:alarm-inventory-changed";

}

#define CLIENT_ALARM_RPC(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT) \
    [&]() { \
        auto inp = std::map<std::string, std::string>{ \
            {"resource", RESOURCE}, \
            {"alarm-type-id", ID}, \
            {"alarm-type-qualifier", QUALIFIER}, \
            {"severity", SEVERITY}, \
            {"alarm-text", TEXT}, \
        }; \
\
        auto intervalStart = std::chrono::system_clock::now(); \
        rpcFromSysrepo(*SESS, rpcPrefix, inp); \
        auto intervalEnd = std::chrono::system_clock::now(); \
        return AnyTimeBetween{intervalStart, intervalEnd}; \
    }()

#define CLIENT_INTRODUCE_ALARM_VECTOR(SESS, ID, QUALIFIER, RESOURCES, SEVERITIES, DESCRIPTION) \
    { \
        alarms::utils::ScopedDatastoreSwitch s(*SESS, sysrepo::Datastore::Operational); \
\
        SESS->setItem(alarmInventoryPrefix + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/description", DESCRIPTION); \
        SESS->setItem(alarmInventoryPrefix + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/will-clear", "true"); \
        for (const auto& resource : RESOURCES) { \
            SESS->setItem(alarmInventoryPrefix + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/resource", resource.c_str()); \
        } \
        for (const auto& severity : SEVERITIES) { \
            SESS->setItem(alarmInventoryPrefix + "/alarm-type[alarm-type-id='" + ID + "'][alarm-type-qualifier='" + QUALIFIER + "']/severity-level", severity.c_str()); \
        } \
        SESS->applyChanges(); \
    }

#define CLIENT_INTRODUCE_ALARM(SESS, ID, QUALIFIER, RESOURCES, SEVERITIES, DESCRIPTION) \
    CLIENT_INTRODUCE_ALARM_VECTOR(SESS, ID, QUALIFIER, std::vector<std::string> RESOURCES, std::vector<std::string> SEVERITIES, DESCRIPTION)

#define CLIENT_PURGE_RPC_IMPL(SESS, RPCPATH, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS, TIMEOUT) \
    [&]() { \
        auto inp = std::map<std::string, std::string> ADDITIONAL_PARAMS; \
        inp["alarm-clearance-status"] = CLEARANCE_STATUS; \
        auto intervalStart = std::chrono::system_clock::now(); \
        REQUIRE(rpcFromSysrepo(*SESS, RPCPATH, inp, TIMEOUT) == std::map<std::string, std::string>{{"/purged-alarms", std::to_string(EXPECTED_NUMBER_OF_PURGED_ALARMS)}}); \
        auto intervalEnd = std::chrono::system_clock::now(); \
        return AnyTimeBetween{intervalStart, intervalEnd}; \
    }();

#define CLIENT_PURGE_RPC(SESS, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS) CLIENT_PURGE_RPC_IMPL(SESS, purgeRpcPrefix, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS, std::chrono::milliseconds{0})
#define CLIENT_PURGE_SHELVED_RPC(SESS, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS) CLIENT_PURGE_RPC_IMPL(SESS, purgeShelvedRpcPrefix, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS, std::chrono::milliseconds{0})
#define CLIENT_PURGE_RPC_SLOW(SESS, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS, TIMEOUT) CLIENT_PURGE_RPC_IMPL(SESS, purgeRpcPrefix, EXPECTED_NUMBER_OF_PURGED_ALARMS, CLEARANCE_STATUS, ADDITIONAL_PARAMS, TIMEOUT)

struct Summary {
    int cleared;
    int notCleared;
};

#define ALARM_SUMMARY_IMPL(SEVERITY, SUMMARY) \
    {"/summary/alarm-summary[severity='" SEVERITY "']", ""}, \
    {"/summary/alarm-summary[severity='" SEVERITY "']/severity", SEVERITY}, \
    {"/summary/alarm-summary[severity='" SEVERITY "']/cleared", std::to_string(SUMMARY.cleared)}, \
    {"/summary/alarm-summary[severity='" SEVERITY "']/not-cleared", std::to_string(SUMMARY.notCleared)}, \
    {"/summary/alarm-summary[severity='" SEVERITY "']/total", std::to_string(SUMMARY.cleared + SUMMARY.notCleared)}

#define CRITICAL(SUMMARY) ALARM_SUMMARY_IMPL("critical", SUMMARY)
#define WARNING(SUMMARY) ALARM_SUMMARY_IMPL("warning", SUMMARY)
#define MAJOR(SUMMARY) ALARM_SUMMARY_IMPL("major", SUMMARY)
#define MINOR(SUMMARY) ALARM_SUMMARY_IMPL("minor", SUMMARY)
#define INDETERMINATE(SUMMARY) ALARM_SUMMARY_IMPL("indeterminate", SUMMARY)
#define ALARM_SUMMARY(...) \
    {"/summary", ""}, \
    __VA_ARGS__
