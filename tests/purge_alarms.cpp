#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo_types.h>
#include "alarms/Daemon.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;

namespace {

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto prugeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";

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

std::map<std::string, std::string> createPurgeNode(const std::string& alarmClearanceStatus, const std::optional<std::pair<std::string, std::string>>& severity)
{
    std::map<std::string, std::string> res;
    res["alarm-clearance-status"] = alarmClearanceStatus;

    if (severity) {
        res["severity/" + severity->first] = severity->second;
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
#define CLI_UPSERT_ALARM(time, sess, id, qualifier, resource, severity, text) \
    std::chrono::time_point<std::chrono::system_clock> time;                  \
    {                                                                         \
        auto in = createAlarmNode(id, qualifier, resource, severity, text);   \
        time = std::chrono::system_clock::now();                              \
        rpcFromSysrepo(*sess, rpcPrefix, in);                                 \
    }

#define SEVERITY_NONE std::nullopt
#define CLI_PURGE(sess, clearance, severity, expectedPurgedNodes) \
    REQUIRE(rpcFromSysrepo(*sess, prugeRpcPrefix, createPurgeNode(clearance, severity)) == std::map<std::string, std::string>{{"/purged-alarms", std::to_string(expectedPurgedNodes)}});

#define EXPECT_TIME_INTERVAL(point) AnyTimeBetween(point, point + 300ms)

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto daemon = std::make_unique<alarms::Daemon>();
    TEST_SYSREPO_INIT_SP(cli1Conn, cli1Sess);
    TEST_SYSREPO_INIT_SP(userConn, userSess);

    SECTION("Purge by clearance status")
    {
        CLI_UPSERT_ALARM(time1, cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", ALARM_TEXT_NONE);
        CLI_UPSERT_ALARM(time2, cli1Sess, "alarms-test:alarm-2", "", "edfa", "major", ALARM_TEXT_NONE);

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time2)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time2)},
                    {"/control", ""},
                });

        CLI_UPSERT_ALARM(time3, cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", ALARM_TEXT_NONE);

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "true"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time2)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time2)},
                    {"/control", ""},
                });

        SECTION("purge cleared")
        {
            CLI_PURGE(userSess, "cleared", SEVERITY_NONE, 1);

            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time2)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time2)},
                        {"/control", ""},
                    });

            SECTION("follow by purge all")
            {
                CLI_PURGE(userSess, "any", SEVERITY_NONE, 1);

                REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                            {"/alarm-list", ""},
                            {"/control", ""},
                        });
            }
        }

        SECTION("purge not cleared")
        {
            CLI_PURGE(userSess, "not-cleared", SEVERITY_NONE, 1);

            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "true"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time1)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time1)},
                        {"/control", ""},
                    });
        }

        SECTION("purge all")
        {
            CLI_PURGE(userSess, "any", SEVERITY_NONE, 2);

            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                        {"/alarm-list", ""},
                        {"/control", ""},
                    });
        }
    }

    SECTION("Purge by clearance status and severity")
    {
        CLI_UPSERT_ALARM(time1, cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", ALARM_TEXT_NONE);
        CLI_UPSERT_ALARM(time2, cli1Sess, "alarms-test:alarm-2", "", "edfa", "major", ALARM_TEXT_NONE);

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time2)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time2)},
                    {"/control", ""},
                });

        CLI_UPSERT_ALARM(time3, cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", ALARM_TEXT_NONE);

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::variant<std::string, AnyTimeBetween>>{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/is-cleared", "true"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/time-created", EXPECT_TIME_INTERVAL(time2)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/last-raised", EXPECT_TIME_INTERVAL(time2)},
                    {"/control", ""},
                });

        SECTION("below")
        {
            SECTION("below warning")
            {
                CLI_PURGE(userSess, "any", std::make_pair("below", "warning"), 0);
            }
            SECTION("below indeterminate")
            {
                CLI_PURGE(userSess, "any", std::make_pair("below", "indeterminate"), 0);
            }
            SECTION("below major")
            {
                CLI_PURGE(userSess, "any", std::make_pair("below", "major"), 1);
            }
            SECTION("below critical")
            {
                CLI_PURGE(userSess, "any", std::make_pair("below", "critical"), 2);
            }
        }

        SECTION("above")
        {
            SECTION("above warning")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "warning"), 1);
            }
            SECTION("above indeterminate")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "indeterminate"), 2);
            }
            SECTION("above major")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "major"), 0);
            }
            SECTION("above critical")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "critical"), 0);
            }
        }

        SECTION("is")
        {
            SECTION("is warning")
            {
                CLI_PURGE(userSess, "any", std::make_pair("is", "warning"), 1);
            }
            SECTION("is indeterminate")
            {
                CLI_PURGE(userSess, "any", std::make_pair("is", "indeterminate"), 0);
            }
            SECTION("is major")
            {
                CLI_PURGE(userSess, "any", std::make_pair("is", "major"), 1);
            }
            SECTION("is critical")
            {
                CLI_PURGE(userSess, "any", std::make_pair("is", "critical"), 0);
            }
        }

        SECTION("above")
        {
            SECTION("above warning")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "warning"), 1);
            }
            SECTION("above indeterminate")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "indeterminate"), 2);
            }
            SECTION("above major")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "major"), 0);
            }
            SECTION("above critical")
            {
                CLI_PURGE(userSess, "any", std::make_pair("above", "critical"), 0);
            }
        }
    }
}
