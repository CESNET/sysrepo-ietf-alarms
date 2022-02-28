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

    CLIENT_PURGE_RPC(userSess, 0, "cleared", {});
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "A cleared issue");
    REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    SECTION("Purge by clearance status")
    {
        SECTION("purge cleared followed by purge all")
        {
            CLIENT_PURGE_RPC(userSess, 1, "cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                    });

            CLIENT_PURGE_RPC(userSess, 1, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
        }

        SECTION("purge not cleared")
        {
            CLIENT_PURGE_RPC(userSess, 1, "not-cleared", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                        "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                    });
        }

        SECTION("purge all")
        {
            CLIENT_PURGE_RPC(userSess, 2, "any", {});
            REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
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
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
            }
            SECTION("below critical")
            {
                CLIENT_PURGE_RPC(userSess, 2, "any", ({{"severity/below", "critical"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
            }
            SECTION("below critical and cleared")
            {
                CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/below", "critical"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
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
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
            }
            SECTION("is major")
            {
                CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/is", "major"}}));
                CLIENT_PURGE_RPC(userSess, 1, "not-cleared", ({{"severity/is", "major"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                        });
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
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                        });
            }
            SECTION("above indeterminate")
            {
                CLIENT_PURGE_RPC(userSess, 1, "cleared", ({{"severity/above", "indeterminate"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{
                            "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
                        });
                CLIENT_PURGE_RPC(userSess, 1, "any", ({{"severity/above", "indeterminate"}}));
                REQUIRE(listInstancesFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-list/alarm", sysrepo::Datastore::Operational) == std::vector<std::string>{});
            }
        }
    }
}
