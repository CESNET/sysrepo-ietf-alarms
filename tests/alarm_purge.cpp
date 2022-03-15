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

namespace {

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";
const auto expectedTimeDegreeOfFreedom = 300ms;
}

bool includes(const std::map<std::string, std::string>& sequence, const PropsWithTimeTest& subseq)
{
    for (const auto& [k, v] : subseq) {
        if (auto it = sequence.find(k); it == sequence.end() || !(it->second == v)) {
            return false;
        }
    }
    return true;
}

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    std::chrono::time_point<std::chrono::system_clock> time;
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "edfa", "major", "A major issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    time = CLIENT_PURGE_RPC(userSess, 0, "cleared", {});
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                        {"/last-changed", BEFORE(time)},
                                                                                                                        {"/number-of-alarms", "2"},
                                                                                                                    }));

    time = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "A cleared issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                        {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                        {"/number-of-alarms", "2"},
                                                                                                                    }));

    SECTION("Purge by clearance status")
    {
        SECTION("purge cleared followed by purge all")
        {
            time = CLIENT_PURGE_RPC(userSess, 1, "cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                    });
            REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                {"/number-of-alarms", "1"},
                                                                                                                            }));

            time = CLIENT_PURGE_RPC(userSess, 1, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
            REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                {"/number-of-alarms", "0"},
                                                                                                                            }));
            time = CLIENT_PURGE_RPC(userSess, 0, "any", {});
            REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                {"/last-changed", BEFORE(time)},
                                                                                                                                {"/number-of-alarms", "0"},
                                                                                                                            }));
        }

        SECTION("purge not cleared")
        {
            time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                    });
            REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                {"/number-of-alarms", "1"},
                                                                                                                            }));
        }

        SECTION("purge all")
        {
            time = CLIENT_PURGE_RPC(userSess, 2, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
            REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                {"/last-changed", SHORTLY_AFTER(time)},
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
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/below", "indeterminate"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"severity/below", "major"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/below", "major"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "1"},
                                                                                                                                }));
            }
            SECTION("below critical")
            {
                time = CLIENT_PURGE_RPC(userSess, 2, "any", ({{"severity/below", "critical"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "0"},
                                                                                                                                }));
            }
            SECTION("below critical and cleared")
            {
                time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/below", "critical"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "1"},
                                                                                                                                }));
            }
        }

        SECTION("is")
        {
            SECTION("is indeterminate/critical/warning")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/is", "indeterminate"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/is", "critical"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"severity/is", "warning"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/is", "warning"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "1"},
                                                                                                                                }));
            }
            SECTION("is major")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/is", "major"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"severity/is", "major"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                        });
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "1"},
                                                                                                                                }));
            }
        }

        SECTION("above")
        {
            SECTION("above critical/major/warning")
            {
                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/above", "critical"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/above", "major"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/above", "warning"}}));
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", BEFORE(time)},
                                                                                                                                    {"/number-of-alarms", "2"},
                                                                                                                                }));

                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "warning"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                        });
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "1"},
                                                                                                                                }));
            }
            SECTION("above indeterminate")
            {
                time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/above", "indeterminate"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "1"},
                                                                                                                                }));
                time = CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "indeterminate"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
                REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                                    {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                                    {"/number-of-alarms", "0"},
                                                                                                                                }));
            }
        }
    }

    SECTION("Purge by clearance status and age")
    {
        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/minutes", "1"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "2"},
                                                                                                                        }));

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/hours", "1"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "2"},
                                                                                                                        }));

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/days", "1"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "2"},
                                                                                                                        }));

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/weeks", "1"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "2"},
                                                                                                                        }));

        std::this_thread::sleep_for(1.5s); // let some time pass by so we can effectively use seconds filter

        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/seconds", "30"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "2"},
                                                                                                                        }));

        time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"older-than/seconds", "1"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                    "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                });
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                            {"/number-of-alarms", "1"},
                                                                                                                        }));
        time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"older-than/seconds", "0"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                            {"/number-of-alarms", "0"},
                                                                                                                        }));
        time = CLIENT_PURGE_RPC(userSess, 0, "any", ({{"older-than/seconds", "0"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "0"},
                                                                                                                        }));
    }

    SECTION("Purge by clearance status, severity, and age")
    {
        time = CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"older-than/seconds", "30"}, {"severity/above", "indeterminate"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "2"},
                                                                                                                        }));

        std::this_thread::sleep_for(1.5s); // let some time pass by so we can effectively use seconds filter

        time = CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"older-than/seconds", "1"}, {"severity/above", "indeterminate"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                    "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                });
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                            {"/number-of-alarms", "1"},
                                                                                                                        }));
        time = CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"older-than/seconds", "1"}, {"severity/above", "indeterminate"}}));
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", BEFORE(time)},
                                                                                                                            {"/number-of-alarms", "1"},
                                                                                                                        }));

        time = CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"older-than/seconds", "1"}, {"severity/is", "major"}}));
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
        REQUIRE(includes(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list", sysrepo::Datastore::Operational), PropsWithTimeTest{
                                                                                                                            {"/last-changed", SHORTLY_AFTER(time)},
                                                                                                                            {"/number-of-alarms", "0"},
                                                                                                                        }));
    }
}
