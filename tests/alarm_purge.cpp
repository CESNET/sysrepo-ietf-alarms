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
}

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "edfa", "major", "A major issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    CLIENT_PURGE_RPC(userSess, 0, "cleared");
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "A cleared issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    SECTION("purge cleared followed by purge all")
    {
        CLIENT_PURGE_RPC(userSess, 1, "cleared")
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                    "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                });

        CLIENT_PURGE_RPC(userSess, 1, "any");
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
    }

    SECTION("purge not cleared")
    {
        CLIENT_PURGE_RPC(userSess, 1, "not-cleared");
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                    "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                });
    }

    SECTION("purge all")
    {
        CLIENT_PURGE_RPC(userSess, 2, "any");
        REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
    }
}
