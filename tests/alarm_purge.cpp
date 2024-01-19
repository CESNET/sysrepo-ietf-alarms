#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo_types.h>
#include <thread>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;

bool includesAll(const std::map<std::string, std::string>& haystack, const PropsWithTimeTest& needles)
{
    return std::all_of(needles.begin(), needles.end(), [&haystack](const auto& e) {
        auto it = haystack.find(e.first);
        return it != haystack.end() && it->second == e.second;
    });
}

TEST_CASE("Purge alarms RPC")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    // check that we can ask for purge even without any config
    CLIENT_PURGE_RPC(userSess, 0, "cleared", {});

    userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
    userSess->applyChanges();

    CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-1", "", {}, {}, "Alarm 1");
    CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2", "", {}, {}, "Alarm 2");

    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "edfa", "major", "A major issue");
    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "wss", "warning", "A warning");
    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "wss", "major", "A major issue");

    REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    auto time = CLIENT_PURGE_RPC(userSess, 0, "cleared", {});
    REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                    {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                    {"/number-of-alarms", "2"},
                                                                                                }));

    auto timeShelf = CLIENT_PURGE_SHELVED_RPC(userSess, 0, "cleared", {});
    REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(includesAll(dataFromSysrepo(*userSess, shelvedAlarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                           {"/shelved-alarms-last-changed", BEFORE_INTERVAL(timeShelf)},
                                                                                                           {"/number-of-shelved-alarms", "2"},
                                                                                                       }));

    time = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "A cleared issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                    {"/last-changed", time},
                                                                                                    {"/number-of-alarms", "2"},
                                                                                                }));

    timeShelf = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "wss", "cleared", "A cleared issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(includesAll(dataFromSysrepo(*userSess, shelvedAlarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                           {"/shelved-alarms-last-changed", BEFORE_INTERVAL(timeShelf)},
                                                                                                           {"/number-of-shelved-alarms", "2"},
                                                                                                       }));

    SECTION("Purge by clearance status")
    {
        SECTION("purge cleared followed by purge all")
        {
            time = CLIENT_PURGE_RPC(userSess, 1, "cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                    });
            REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                            {"/last-changed", time},
                                                                                                            {"/number-of-alarms", "1"},
                                                                                                        }));

            timeShelf = CLIENT_PURGE_SHELVED_RPC(userSess, 1, "cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                    });
            REQUIRE(includesAll(dataFromSysrepo(*userSess, shelvedAlarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                   {"/shelved-alarms-last-changed", timeShelf},
                                                                                                                   {"/number-of-shelved-alarms", "1"},
                                                                                                               }));

            time = CLIENT_PURGE_RPC(userSess, 1, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
            REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                            {"/last-changed", time},
                                                                                                            {"/number-of-alarms", "0"},
                                                                                                        }));

            timeShelf = CLIENT_PURGE_SHELVED_RPC(userSess, 1, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
            REQUIRE(includesAll(dataFromSysrepo(*userSess, shelvedAlarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                   {"/shelved-alarms-last-changed", timeShelf},
                                                                                                                   {"/number-of-shelved-alarms", "0"},
                                                                                                               }));


            CLIENT_PURGE_RPC(userSess, 0, "any", {});
            REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                            {"/last-changed", time},
                                                                                                            {"/number-of-alarms", "0"},
                                                                                                        }));

            CLIENT_PURGE_SHELVED_RPC(userSess, 0, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
            REQUIRE(includesAll(dataFromSysrepo(*userSess, shelvedAlarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                   {"/shelved-alarms-last-changed", timeShelf},
                                                                                                                   {"/number-of-shelved-alarms", "0"},
                                                                                                               }));
        }

        SECTION("purge not cleared")
        {
            time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                    });
            REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                            {"/last-changed", time},
                                                                                                            {"/number-of-alarms", "1"},
                                                                                                        }));
        }

        SECTION("purge all")
        {
            time = CLIENT_PURGE_RPC(userSess, 2, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
            REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                            {"/last-changed", time},
                                                                                                            {"/number-of-alarms", "0"},
                                                                                                        }));
        }
    }

    SECTION("Purge by clearance status and severity")
    {
        SECTION("below")
        {
            SECTION("below warning/indeterminate/major")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/below", "warning"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/below", "indeterminate"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"severity/below", "major"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/below", "major"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "1"},
                                                                                                            }));
            }
            SECTION("below critical")
            {
                time = CLIENT_PURGE_RPC(userSess, 2, "any", ({{"severity/below", "critical"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "0"},
                                                                                                            }));
            }
            SECTION("below critical and cleared")
            {
                time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/below", "critical"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "1"},
                                                                                                            }));
            }
        }

        SECTION("is")
        {
            SECTION("is indeterminate/critical/warning")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/is", "indeterminate"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/is", "critical"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"severity/is", "warning"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/is", "warning"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "1"},
                                                                                                            }));
            }
            SECTION("is major")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/is", "major"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"severity/is", "major"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                        });
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "1"},
                                                                                                            }));
            }
        }

        SECTION("above")
        {
            SECTION("above critical/major/warning")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/above", "critical"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/above", "major"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/above", "warning"}}));
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                                {"/number-of-alarms", "2"},
                                                                                                            }));

                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "warning"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                        });
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "1"},
                                                                                                            }));
            }
            SECTION("above indeterminate")
            {
                time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/above", "indeterminate"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "1"},
                                                                                                            }));
                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "indeterminate"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
                REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                {"/last-changed", time},
                                                                                                                {"/number-of-alarms", "0"},
                                                                                                            }));
            }
        }
    }

    SECTION("Purge by clearance status and age")
    {
        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/minutes", "1"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "2"},
                                                                                                    }));

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/hours", "1"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "2"},
                                                                                                    }));

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/days", "1"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "2"},
                                                                                                    }));

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/weeks", "1"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "2"},
                                                                                                    }));

        std::this_thread::sleep_for(1.5s); // let some time pass by so we can effectively use seconds filter

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/seconds", "30"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "2"},
                                                                                                    }));

        time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"older-than/seconds", "1"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                    "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                });
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", time},
                                                                                                        {"/number-of-alarms", "1"},
                                                                                                    }));
        time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"older-than/seconds", "0"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", time},
                                                                                                        {"/number-of-alarms", "0"},
                                                                                                    }));
        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/seconds", "0"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "0"},
                                                                                                    }));
    }

    SECTION("Purge by clearance status, severity, and age")
    {
        time = CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"older-than/seconds", "30"}, {"severity/above", "indeterminate"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "2"},
                                                                                                    }));

        std::this_thread::sleep_for(1.5s); // let some time pass by so we can effectively use seconds filter

        time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"older-than/seconds", "1"}, {"severity/above", "indeterminate"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                    "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                });
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", time},
                                                                                                        {"/number-of-alarms", "1"},
                                                                                                    }));
        time = CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"older-than/seconds", "1"}, {"severity/above", "indeterminate"}}));
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", BEFORE_INTERVAL(time)},
                                                                                                        {"/number-of-alarms", "1"},
                                                                                                    }));

        time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"older-than/seconds", "1"}, {"severity/is", "major"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{});
        REQUIRE(includesAll(dataFromSysrepo(*userSess, alarmList, sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                        {"/last-changed", time},
                                                                                                        {"/number-of-alarms", "0"},
                                                                                                    }));
    }
}
