#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo_types.h>
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

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    auto origTime1 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
    auto origTime2 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "edfa", "major", "A major issue");
    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                {"/alarm-list", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-text", "A warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-text", "A major issue"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime2)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime2)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime2)},
                {"/control", ""},
            });

    CLIENT_PURGE_RPC(userSess, 0, "cleared", {});
    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                {"/alarm-list", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-text", "A warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-text", "A major issue"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime2)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime2)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime2)},
                {"/control", ""},
            });

    auto clearedTime1 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "A cleared issue");
    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                {"/alarm-list", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "true"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-text", "A cleared issue"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(clearedTime1)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-text", "A major issue"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime2)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime2)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime2)},
                {"/control", ""},
            });

    SECTION("Purge by clearance status")
    {
        SECTION("purge cleared")
        {
            CLIENT_PURGE_RPC(userSess, 1, "cleared", {})
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-text", "A major issue"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime2)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime2)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime2)},
                        {"/control", ""},
                    });

            SECTION("follow by purge all")
            {
                CLIENT_PURGE_RPC(userSess, 1, "any", {});
                REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                            {"/alarm-list", ""},
                            {"/control", ""},
                        });
            }
        }

        SECTION("purge not cleared")
        {
            CLIENT_PURGE_RPC(userSess, 1, "not-cleared", {});
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "true"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-text", "A cleared issue"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime1)},
                        {"/control", ""},
                    });
        }

        SECTION("purge all")
        {
            CLIENT_PURGE_RPC(userSess, 2, "any", {});
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                        {"/alarm-list", ""},
                        {"/control", ""},
                    });
        }
    }

    SECTION("Purge by clearance status and severity")
    {
        SECTION("below")
        {
            SECTION("below warning/indeterminate/major")
            {
                CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/below", "warning"}}));
                CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/below", "indeterminate"}}));
                CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"severity/below", "major"}}));
                CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/below", "major"}}));
            }
            SECTION("below critical")
            {
                CLIENT_PURGE_RPC(userSess, 2, "any", ({{"severity/below", "critical"}}));
            }
            SECTION("below critical and cleared")
            {
                CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/below", "critical"}}));
            }
        }

        SECTION("is")
        {
            SECTION("is indeterminate/critical/warning")
            {
                CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/is", "indeterminate"}}));
                CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/is", "critical"}}));
                CLIENT_PURGE_RPC(userSess, 0, "not-cleared", ({{"severity/is", "warning"}}));
                CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/is", "warning"}}));
            }
            SECTION("is major")
            {
                CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/is", "major"}}));
                CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"severity/is", "major"}}));
            }
        }

        SECTION("above")
        {
            SECTION("above critical/major/warning")
            {
                CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/above", "critical"}}));
                CLIENT_PURGE_RPC(userSess, 0, "any", ({{"severity/above", "major"}}));
                CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/above", "warning"}}));
                CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "warning"}}));
            }
            SECTION("above indeterminate")
            {
                CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/above", "indeterminate"}}));
                CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "indeterminate"}}));
            }
        }
    }
}
