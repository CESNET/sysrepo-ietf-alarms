#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

namespace {

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
}

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto daemon = std::make_unique<alarms::Daemon>();
    TEST_SYSREPO_CLIENT_INIT_SP(cli1Conn, cli1Sess);
    TEST_SYSREPO_CLIENT_INIT_SP(cli2Conn, cli2Sess);
    TEST_SYSREPO_CLIENT_INIT_SP(userConn, userSess);

    SECTION("Create a single alarm by cli1")
    {
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", PARAMS(P("alarm-text", "Hey, I'm overheating.")));

        SECTION("cli1 disconnection does not delete data")
        {
            cli1Sess.reset();
            cli1Conn.reset();
        }

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/control", ""},
                });

        SECTION("demon disconnection deletes data")
        {
            daemon.reset();
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                        {"/control", ""},
                    });
        }
    }

    SECTION("Create two alarms, one per each of the two clients")
    {
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", PARAMS(P("alarm-text", "Hey, I'm overheating.")));
        CLIENT_ALARM_RPC(cli2Sess, "alarms-test:alarm-2-1", "", "psu-1", "major", PARAMS(P("alarm-text", "More juice pls.")));

        SECTION("Disconnection of any client does not delete any data")
        {
            SECTION("cli1")
            {
                cli1Sess.reset();
                cli1Conn.reset();
            }

            SECTION("cli2")
            {
                cli2Sess.reset();
                cli2Conn.reset();
            }
        }

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},

                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/control", ""},
                });
    }

    SECTION("Client sets alarm, disconnects, then connects again and clears the alarm")
    {
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "disconnected", "psu-1", "major", {});
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
                    {"/control", ""},
                });

        TEST_SYSREPO_CLIENT_DISCONNECT_AND_RESTORE_SP(cli1Conn, cli1Sess);

        SECTION("Clears the alarm")
        {
            CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "disconnected", "psu-1", "cleared", {});
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']", ""},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-id", "alarms-test:alarm-2-1"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/is-cleared", "true"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
                        {"/control", ""},
                    });

            SECTION("Sets the alarm back")
            {
                CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "disconnected", "psu-1", "major", {});
                REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                            {"/alarm-list", ""},
                            {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']", ""},
                            {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-id", "alarms-test:alarm-2-1"},
                            {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
                            {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
                            {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
                            {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
                            {"/control", ""},
                        });
            }
        }

        SECTION("Clears a non-existent alarm -> no-op")
        {
            CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", {});
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']", ""},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-id", "alarms-test:alarm-2-1"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/alarm-type-qualifier", "disconnected"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/resource", "psu-1"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='disconnected']/perceived-severity", "major"},
                        {"/control", ""},
                    });
        }
    }

    SECTION("Update state")
    {
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "indeterminate", {});

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-qualifier", "qual"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/perceived-severity", "indeterminate"},
                    {"/control", ""},
                });

        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "minor", PARAMS(P("alarm-text", "No worries.")));
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-qualifier", "qual"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-text", "No worries."},
                    {"/control", ""},
                });

        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "critical", PARAMS());
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-qualifier", "qual"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/perceived-severity", "critical"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-text", "No worries."},
                    {"/control", ""},
                });

        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "cleared", PARAMS());
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-type-qualifier", "qual"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/is-cleared", "true"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/perceived-severity", "critical"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='qual']/alarm-text", "No worries."},
                    {"/control", ""},
                });
    }
}
