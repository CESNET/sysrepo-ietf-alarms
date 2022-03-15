#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;

namespace {

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto expectedTimeDegreeOfFreedom = 300ms;
}

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto daemon = std::make_unique<alarms::Daemon>();
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    auto origTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                {"/alarm-list", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},
                {"/control", ""},
            });

    SECTION("Client and daemon disconnects")
    {
        cli1Sess.reset();

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},
                    {"/control", ""},
                });

        daemon.reset();
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/control", ""},
                });
    }

    SECTION("Another client creates an alarm")
    {
        auto origTime1 = CLIENT_ALARM_RPC(cli2Sess, "alarms-test:alarm-2-1", "", "psu-1", "major", "More juice pls.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},

                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime1)},
                    {"/control", ""},
                });

        cli1Sess.reset();
        cli2Sess.reset();

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime1)},
                    {"/control", ""},
                });
    }

    SECTION("Client disconnects, then connects again and clears the alarm")
    {
        TEST_SYSREPO_CLIENT_DISCONNECT_AND_RESTORE(cli1Sess);

        SECTION("Clears the alarm that was set before and then sets it back")
        {
            auto clearedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", "Functioning within normal parameters.");
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Functioning within normal parameters."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(clearedTime)},
                        {"/control", ""},
                    });

            auto raisedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(raisedTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(raisedTime)},
                        {"/control", ""},
                    });
        }

        SECTION("Clearing a non-existent alarm results in no-op")
        {
            CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "psu", "cleared", "Functioning within normal parameters.");
            REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                        {"/alarm-list", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},
                        {"/control", ""},
                    });
        }
    }

    SECTION("Updating state")
    {
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "indeterminate", "Something happen but we don't know what and how serious it is.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "indeterminate"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Something happen but we don't know what and how serious it is."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},
                    {"/control", ""},
                });

        auto changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "minor", "No worries.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "No worries."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(changedTime)},
                    {"/control", ""},
                });

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", "Functioning within normal parameters.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Functioning within normal parameters."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(changedTime)},
                    {"/control", ""},
                });

        auto reraisedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "major", "Hey, I'm overheating.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(reraisedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(reraisedTime)},
                    {"/control", ""},
                });

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "critical", "Hey, I'm overheating.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "critical"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(reraisedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(changedTime)},
                    {"/control", ""},
                });
    }

    SECTION("Properly escaped resource string")
    {
        auto origTime1 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "", "/ietf-interfaces:interface[name='eth1']", "minor", "Link operationally down but administratively up.");
        auto origTime2 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "/ietf-interfaces:interface[name=\"eth2\"]", "minor", "Link operationally down but administratively up.");
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", SHORTLY_AFTER(origTime)},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "/ietf-interfaces:interface[name='eth1']"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "Link operationally down but administratively up."},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime1)},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-2"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/resource", "/ietf-interfaces:interface[name=\"eth2\"]"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-text", "Link operationally down but administratively up."},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/time-created", SHORTLY_AFTER(origTime2)},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/last-raised", SHORTLY_AFTER(origTime2)},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/last-changed", SHORTLY_AFTER(origTime2)},
                    {"/control", ""},
                });
    }

    SECTION("Not properly escaped resource string throws")
    {
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "/some:hardware/entry[n1='ahoj\"'][n2=\"cau']`", "minor", "A text") }(), "Couldn't send RPC: SR_ERR_CALLBACK_FAILED");
    }
}
