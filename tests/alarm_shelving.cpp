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

#define LIST_LAST_CHANGED_EQUALS_TO(RESOURCE, ID, QUALIFIER) \
    actualDataFromSysrepo["/alarm-list/last-changed"] == actualDataFromSysrepo["/alarm-list/alarm[resource='" RESOURCE "'][alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/last-changed"]

TEST_CASE("Alarm shelving")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto initTime = std::chrono::system_clock::now();
    auto daemon = std::make_unique<alarms::Daemon>();
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                {"/alarm-list", ""},
                {"/alarm-list/number-of-alarms", "0"},
                {"/alarm-list/last-changed", SHORTLY_AFTER(initTime)},
                {"/control", ""},
            });

    auto origTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
    std::map<std::string, std::string> actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
    REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                {"/alarm-list", ""},
                {"/alarm-list/number-of-alarms", "1"},
                {"/alarm-list/last-changed", IN_INTERVAL(origTime)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(origTime)},
                {"/control", ""},
            });
    REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

    SECTION("Client and daemon disconnects")
    {
        cli1Sess.reset();

        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(origTime)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

        daemon.reset();
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/control", ""},
                });
    }

    SECTION("Another client creates an alarm")
    {
        auto origTime1 = CLIENT_ALARM_RPC(cli2Sess, "alarms-test:alarm-2-1", "", "psu-1", "major", "More juice pls.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", IN_INTERVAL(origTime1)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("psu-1", "alarms-test:alarm-2-1", ""));

        cli1Sess.reset();
        cli2Sess.reset();

        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", IN_INTERVAL(origTime1)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("psu-1", "alarms-test:alarm-2-1", ""));
    }

    SECTION("Client disconnects, then connects again and clears the alarm")
    {
        TEST_SYSREPO_CLIENT_DISCONNECT_AND_RESTORE(cli1Sess);

        SECTION("Clears the alarm that was set before and then sets it back")
        {
            auto clearedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", "Functioning within normal parameters.");
            actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", IN_INTERVAL(clearedTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Functioning within normal parameters."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(clearedTime)},
                        {"/control", ""},
                    });
            REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

            auto raisedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
            actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", IN_INTERVAL(raisedTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(raisedTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(raisedTime)},
                        {"/control", ""},
                    });
            REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));
        }

        SECTION("Clearing a non-existent alarm results in no-op")
        {
            CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "psu", "cleared", "Functioning within normal parameters.");
            actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", IN_INTERVAL(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(origTime)},
                        {"/control", ""},
                    });
            REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));
        }
    }

    SECTION("Updating state")
    {
        auto changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "indeterminate", "Something happen but we don't know what and how serious it is.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", IN_INTERVAL(changedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "indeterminate"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Something happen but we don't know what and how serious it is."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(changedTime)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "minor", "No worries.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", IN_INTERVAL(changedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "No worries."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(changedTime)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", "Functioning within normal parameters.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", IN_INTERVAL(changedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Functioning within normal parameters."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(changedTime)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

        auto reraisedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "major", "Hey, I'm overheating.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", IN_INTERVAL(reraisedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(reraisedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(reraisedTime)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "critical", "Hey, I'm overheating.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", IN_INTERVAL(changedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "critical"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(reraisedTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(changedTime)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("edfa", "alarms-test:alarm-1", "high"));
    }

    SECTION("Properly escaped resource string")
    {
        auto origTime1 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "", "/ietf-interfaces:interface[name='eth1']", "minor", "Link operationally down but administratively up.");
        auto origTime2 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "/ietf-interfaces:interface[name=\"eth2\"]", "minor", "Link operationally down but administratively up.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "3"},
                    {"/alarm-list/last-changed", IN_INTERVAL(origTime2)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", IN_INTERVAL(origTime)},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "/ietf-interfaces:interface[name='eth1']"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "Link operationally down but administratively up."},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", IN_INTERVAL(origTime1)},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-2"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/resource", "/ietf-interfaces:interface[name=\"eth2\"]"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-text", "Link operationally down but administratively up."},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/time-created", IN_INTERVAL(origTime2)},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/last-raised", IN_INTERVAL(origTime2)},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/last-changed", IN_INTERVAL(origTime2)},
                    {"/control", ""},
                });
        REQUIRE(LIST_LAST_CHANGED_EQUALS_TO("/ietf-interfaces:interface[name=\"eth2\"]", "alarms-test:alarm-2-2", ""));
    }

    SECTION("Not properly escaped resource string throws")
    {
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "/some:hardware/entry[n1='ahoj\"'][n2=\"cau']`", "minor", "A text") }(), "Couldn't send RPC: SR_ERR_CALLBACK_FAILED");
    }
}
