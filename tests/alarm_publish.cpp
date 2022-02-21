#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"

namespace {

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";

std::map<std::string, std::string> createAlarmNode(const std::string& id, const std::string& qualifier, const std::string& resource, const std::string& severity, const std::optional<std::string> text = std::nullopt)
{
    std::map<std::string, std::string> res;
    res["alarm-type-id"] = id;
    res["alarm-type-qualifier"] = qualifier;
    res["resource"] = resource;
    res["severity"] = severity;

    if (text) {
        res["alarm-text"] = *text;
    }

    return res;
}

}

#define DISCONNECT_AND_RESTORE(conn, sess)          \
    sess.reset();                                   \
    conn.reset();                                   \
    conn = std::make_shared<sysrepo::Connection>(); \
    sess = std::make_shared<sysrepo::Session>(conn->sessionStart());

#define ALARM_TEXT_NONE std::nullopt
#define CLI_UPSERT_ALARM(sess, id, qualifier, resource, severity, text) \
    rpcFromSysrepo(*sess, rpcPrefix, createAlarmNode(id, qualifier, resource, severity, text));

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto daemon = std::make_unique<alarms::Daemon>();
    TEST_SYSREPO_INIT_SP(cli1Conn, cli1Sess);
    TEST_SYSREPO_INIT_SP(cli2Conn, cli2Sess);
    TEST_SYSREPO_INIT_SP(userConn, userSess);

    SECTION("Create a single alarm by cli1")
    {
        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");

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
        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
        CLI_UPSERT_ALARM(cli2Sess, "alarms-test:alarm-2-1", "", "psu-1", "major", "More juice pls.");

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
        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "disconnected", "psu-1", "major", ALARM_TEXT_NONE);
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

        DISCONNECT_AND_RESTORE(cli1Conn, cli1Sess);

        SECTION("Clears the alarm")
        {
            CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "disconnected", "psu-1", "cleared", ALARM_TEXT_NONE);
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
                CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "disconnected", "psu-1", "major", ALARM_TEXT_NONE);
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
            CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", ALARM_TEXT_NONE);
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
        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "indeterminate", ALARM_TEXT_NONE);

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

        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "minor", "No worries.");
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

        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "critical", ALARM_TEXT_NONE);
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

        CLI_UPSERT_ALARM(cli1Sess, "alarms-test:alarm-2-1", "qual", "psu-1", "cleared", ALARM_TEXT_NONE);
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
