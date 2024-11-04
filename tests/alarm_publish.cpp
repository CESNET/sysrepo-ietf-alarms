#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

namespace {

bool checkAlarmListLastChanged(const auto& dataFromSysrepo, const std::string& resource, const char* alarmTypeId, const char* alarmTypeQualifier)
{
    return dataFromSysrepo.at("/alarm-list/last-changed") == dataFromSysrepo.at("/alarm-list/alarm[resource='" + resource + "'][alarm-type-id='" + alarmTypeId + "'][alarm-type-qualifier='" + alarmTypeQualifier + "']/last-changed");
}

}

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto initTimeStart = std::chrono::system_clock::now();
    auto daemon = std::make_unique<alarms::Daemon>();
    auto initTime = AnyTimeBetween{initTimeStart, std::chrono::system_clock::now()};

    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                {"/alarm-inventory", ""},
                {"/alarm-list", ""},
                {"/alarm-list/number-of-alarms", "0"},
                {"/alarm-list/last-changed", initTime},
                {"/control", ""},
                {"/control/alarm-shelving", ""},
                {"/control/max-alarm-status-changes", "32"},
                {"/control/notify-status-changes", "all-state-changes"},
                {"/shelved-alarms", ""},
                {"/shelved-alarms/number-of-shelved-alarms", "0"},
                {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                ALARM_SUMMARY(
                        CRITICAL(CLEARED(0), NOTCLEARED(0)),
                        WARNING(CLEARED(0), NOTCLEARED(0)),
                        MAJOR(CLEARED(0), NOTCLEARED(0)),
                        MINOR(CLEARED(0), NOTCLEARED(0)),
                        INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                )
            });

    CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-1", "high", {}, {}, "High temperature on any resource with any severity");
    auto origTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
    std::map<std::string, std::string> actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
    REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                {"/alarm-inventory", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-list", ""},
                {"/alarm-list/number-of-alarms", "1"},
                {"/alarm-list/last-changed", origTime},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime},
                {"/control", ""},
                {"/control/alarm-shelving", ""},
                {"/control/max-alarm-status-changes", "32"},
                {"/control/notify-status-changes", "all-state-changes"},
                {"/shelved-alarms", ""},
                {"/shelved-alarms/number-of-shelved-alarms", "0"},
                {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                {"/summary", ""},
                ALARM_SUMMARY(
                        CRITICAL(CLEARED(0), NOTCLEARED(0)),
                        WARNING(CLEARED(0), NOTCLEARED(1)),
                        MAJOR(CLEARED(0), NOTCLEARED(0)),
                        MINOR(CLEARED(0), NOTCLEARED(0)),
                        INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                )

            });
    REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

    SECTION("Client and daemon disconnects")
    {
        cli1Sess.reset();

        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(1)),
                            MAJOR(CLEARED(0), NOTCLEARED(0)),
                            MINOR(CLEARED(0), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

        daemon.reset();
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/summary", ""},
                });
    }

    SECTION("Another client creates an alarm")
    {
        CLIENT_INTRODUCE_ALARM(cli2Sess, "alarms-test:alarm-2-1", "", ({"psu-1"}), ({"minor", "major"}), "Alarm with specific severity and resource.");
        auto origTime1 = CLIENT_ALARM_RPC(cli2Sess, "alarms-test:alarm-2-1", "", "psu-1", "major", "More juice pls.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource[1]", "psu-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/severity-level[1]", "minor"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/severity-level[2]", "major"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/description", "Alarm with specific severity and resource."},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", origTime1},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", origTime1},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", origTime1},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(1)),
                            MAJOR(CLEARED(0), NOTCLEARED(1)),
                            MINOR(CLEARED(0), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "psu-1", "alarms-test:alarm-2-1", ""));

        cli1Sess.reset();
        cli2Sess.reset();

        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "psu-1"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "More juice pls."},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", origTime1},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", origTime1},
                    {"/alarm-list/alarm[resource='psu-1'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", origTime1},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(1)),
                            MAJOR(CLEARED(0), NOTCLEARED(1)),
                            MINOR(CLEARED(0), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "psu-1", "alarms-test:alarm-2-1", ""));
    }

    SECTION("Client disconnects, then connects again and clears the alarm")
    {
        TEST_SYSREPO_CLIENT_DISCONNECT_AND_RESTORE(cli1Sess);
        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-1", "high", {}, {}, "High temperature on any resource with any severity");

        SECTION("Clears the alarm that was set before and then sets it back")
        {
            auto clearedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", "Hey, I'm overheating.");
            actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-inventory", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", clearedTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", clearedTime},
                        {"/control", ""},
                        {"/control/alarm-shelving", ""},
                        {"/control/max-alarm-status-changes", "32"},
                        {"/control/notify-status-changes", "all-state-changes"},
                        {"/shelved-alarms", ""},
                        {"/shelved-alarms/number-of-shelved-alarms", "0"},
                        {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                        ALARM_SUMMARY(
                                CRITICAL(CLEARED(0), NOTCLEARED(0)),
                                WARNING(CLEARED(1), NOTCLEARED(0)),
                                MAJOR(CLEARED(0), NOTCLEARED(0)),
                                MINOR(CLEARED(0), NOTCLEARED(0)),
                                INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                        )
                    });
            REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

            auto raisedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "warning", "Hey, I'm overheating.");
            actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-inventory", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", raisedTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", raisedTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", raisedTime},
                        {"/control", ""},
                        {"/control/alarm-shelving", ""},
                        {"/control/max-alarm-status-changes", "32"},
                        {"/control/notify-status-changes", "all-state-changes"},
                        {"/shelved-alarms", ""},
                        {"/shelved-alarms/number-of-shelved-alarms", "0"},
                        {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                        ALARM_SUMMARY(
                                CRITICAL(CLEARED(0), NOTCLEARED(0)),
                                WARNING(CLEARED(0), NOTCLEARED(1)),
                                MAJOR(CLEARED(0), NOTCLEARED(0)),
                                MINOR(CLEARED(0), NOTCLEARED(0)),
                                INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                        )
                    });
            REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));
        }

        SECTION("Clearing a non-existent alarm results in no-op")
        {
            CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2", "", {}, {}, "Just for this test...");
            CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2", "", "psu", "cleared", "Functioning within normal parameters.");
            actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-inventory", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/description", "Just for this test..."},
                        {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "true"},
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime},
                        {"/control", ""},
                        {"/control/alarm-shelving", ""},
                        {"/control/max-alarm-status-changes", "32"},
                        {"/control/notify-status-changes", "all-state-changes"},
                        {"/shelved-alarms", ""},
                        {"/shelved-alarms/number-of-shelved-alarms", "0"},
                        {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                        ALARM_SUMMARY(
                                CRITICAL(CLEARED(0), NOTCLEARED(0)),
                                WARNING(CLEARED(0), NOTCLEARED(1)),
                                MAJOR(CLEARED(0), NOTCLEARED(0)),
                                MINOR(CLEARED(0), NOTCLEARED(0)),
                                INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                        )
                    });
            REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));
        }
    }

    SECTION("Updating state")
    {
        auto changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "indeterminate", "Something happen but we don't know what and how serious it is.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "indeterminate"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Something happen but we don't know what and how serious it is."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(0)),
                            MAJOR(CLEARED(0), NOTCLEARED(0)),
                            MINOR(CLEARED(0), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(1)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "minor", "No worries.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "No worries."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(0)),
                            MAJOR(CLEARED(0), NOTCLEARED(0)),
                            MINOR(CLEARED(0), NOTCLEARED(1)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "cleared", "Hey, I'm overheating.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(0)),
                            MAJOR(CLEARED(0), NOTCLEARED(0)),
                            MINOR(CLEARED(1), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

        auto reraisedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "major", "Hey, I'm overheating.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", reraisedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "major"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", reraisedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", reraisedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(0)),
                            MAJOR(CLEARED(0), NOTCLEARED(1)),
                            MINOR(CLEARED(0), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));

        changedTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "edfa", "critical", "Hey, I'm overheating.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "critical"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", reraisedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(1)),
                            WARNING(CLEARED(0), NOTCLEARED(0)),
                            MAJOR(CLEARED(0), NOTCLEARED(0)),
                            MINOR(CLEARED(0), NOTCLEARED(0)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "edfa", "alarms-test:alarm-1", "high"));
    }

    SECTION("Properly escaped resource string")
    {
        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2-1", "", {"/ietf-interfaces:interface[name='eth1']"}, {}, "For escaping test");
        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2-2", "", {"/ietf-interfaces:interface[name=\"eth2\"]"}, {}, "For escaping test");
        auto origTime1 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "", "/ietf-interfaces:interface[name='eth1']", "minor", "Link operationally down but administratively up.");
        auto origTime2 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "/ietf-interfaces:interface[name=\"eth2\"]", "minor", "Link operationally down but administratively up.");
        actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "High temperature on any resource with any severity"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/description", "For escaping test"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource[1]", "/ietf-interfaces:interface[name='eth1']"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/will-clear", "true"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-2"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/description", "For escaping test"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/resource[1]", "/ietf-interfaces:interface[name=\"eth2\"]"},
                    {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/will-clear", "true"},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "3"},
                    {"/alarm-list/last-changed", origTime2},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource", "/ietf-interfaces:interface[name='eth1']"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-text", "Link operationally down but administratively up."},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/time-created", origTime1},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-raised", origTime1},
                    {"/alarm-list/alarm[resource=\"/ietf-interfaces:interface[name='eth1']\"][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/last-changed", origTime1},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-2"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/resource", "/ietf-interfaces:interface[name=\"eth2\"]"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/perceived-severity", "minor"},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/alarm-text", "Link operationally down but administratively up."},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/time-created", origTime2},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/last-raised", origTime2},
                    {"/alarm-list/alarm[resource='/ietf-interfaces:interface[name=\"eth2\"]'][alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='']/last-changed", origTime2},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/control/max-alarm-status-changes", "32"},
                    {"/control/notify-status-changes", "all-state-changes"},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/number-of-shelved-alarms", "0"},
                    {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                    ALARM_SUMMARY(
                            CRITICAL(CLEARED(0), NOTCLEARED(0)),
                            WARNING(CLEARED(0), NOTCLEARED(1)),
                            MAJOR(CLEARED(0), NOTCLEARED(0)),
                            MINOR(CLEARED(0), NOTCLEARED(2)),
                            INDETERMINATE(CLEARED(0), NOTCLEARED(0)),
                    )
                });
        REQUIRE(checkAlarmListLastChanged(actualDataFromSysrepo, "/ietf-interfaces:interface[name=\"eth2\"]", "alarms-test:alarm-2-2", ""));
    }

    SECTION("Not properly escaped resource string throws")
    {
        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2-2", "", {}, {}, "For escaping test");
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "/some:hardware/entry[n1='ahoj\"'][n2=\"cau']`", "minor", "A text"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " Encountered mixed single and double quotes in XPath; can't properly escape. (SR_ERR_OPERATION_FAILED)");
    }

    SECTION("Validation against inventory")
    {
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "a-qual", "a-resource", "minor", "A text"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " No alarm inventory entry for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='a-qual'] -- see RFC8632 (sec. 4.1). (SR_ERR_OPERATION_FAILED)\n"
                            " NETCONF: application: data-missing: No alarm inventory entry for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='a-qual'] -- see RFC8632 (sec. 4.1).");

        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2-2", "", ({"a-resource", "another-resource"}), ({"minor", "major", "critical"}), "test");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "a-resource", "minor", "A text");

        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "a-qual", "a-resource", "minor", "Invalid qualifier"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " No alarm inventory entry for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='a-qual'] -- see RFC8632 (sec. 4.1). (SR_ERR_OPERATION_FAILED)\n"
                            " NETCONF: application: data-missing: No alarm inventory entry for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='a-qual'] -- see RFC8632 (sec. 4.1).");
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "a-qual", "a-resource", "major", "Invalid qualifier"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " No alarm inventory entry for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='a-qual'] -- see RFC8632 (sec. 4.1). (SR_ERR_OPERATION_FAILED)\n"
                            " NETCONF: application: data-missing: No alarm inventory entry for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier='a-qual'] -- see RFC8632 (sec. 4.1).");
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "invalid-resource", "major", "Invalid resource"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " Alarm inventory doesn't allow resource 'invalid-resource' for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier=''] -- see RFC8632 (sec. 4.1). (SR_ERR_OPERATION_FAILED)\n"
                            " NETCONF: application: data-missing: Alarm inventory doesn't allow resource 'invalid-resource' for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier=''] -- see RFC8632 (sec. 4.1).");
        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "another-resource", "indeterminate", "Invalid severity"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " Alarm inventory doesn't allow severity 'indeterminate' for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier=''] -- see RFC8632 (sec. 4.1). (SR_ERR_OPERATION_FAILED)\n"
                            " NETCONF: application: data-missing: Alarm inventory doesn't allow severity 'indeterminate' for [alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier=''] -- see RFC8632 (sec. 4.1).");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "another-resource", "critical", "valid");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-2", "", "another-resource", "cleared", "valid");
    }
}

TEST_CASE("Netopeer2 clients can't publish alarms")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto daemon = std::make_unique<alarms::Daemon>();

    TEST_SYSREPO_CLIENT_INIT(cliSess);
    CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-1", "", {}, {"warning"}, "High temperature on any resource with any severity");

    SECTION("remote clients")
    {
        SECTION("netopeer2")
        {
            cliSess->setOriginatorName("netopeer2");
        }

        SECTION("rousette")
        {
            cliSess->setOriginatorName("rousette");
        }

        SECTION("sysrpeo-cli")
        {
            cliSess->setOriginatorName("sysrepo-cli");
        }

        REQUIRE_THROWS_WITH([&]() { CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "", "warning", "High temperature on any resource with any severity"); }(),
                            "Couldn't send RPC: SR_ERR_OPERATION_FAILED\n"
                            " Internal RPCs cannot be called. (SR_ERR_OPERATION_FAILED)\n"
                            " NETCONF: application: operation-not-supported: Internal RPCs cannot be called.");
    }

    SECTION("Other clients")
    {
        SECTION("Originator name set to something non-netopeerish")
        {
            cliSess->setOriginatorName("net'o'peer2");
        }
        SECTION("No originator name")
        {
        }

        CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "", "warning", "High temperature on any resource with any severity");
        auto actualDataFromSysrepo = dataFromSysrepo(*cliSess, "/ietf-alarms:alarms/summary", sysrepo::Datastore::Operational);
        REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                    {"/alarm-summary[severity='critical']", ""},
                    {"/alarm-summary[severity='critical']/cleared", "0"},
                    {"/alarm-summary[severity='critical']/not-cleared", "0"},
                    {"/alarm-summary[severity='critical']/severity", "critical"},
                    {"/alarm-summary[severity='critical']/total", "0"},
                    {"/alarm-summary[severity='indeterminate']", ""},
                    {"/alarm-summary[severity='indeterminate']/cleared", "0"},
                    {"/alarm-summary[severity='indeterminate']/not-cleared", "0"},
                    {"/alarm-summary[severity='indeterminate']/severity", "indeterminate"},
                    {"/alarm-summary[severity='indeterminate']/total", "0"},
                    {"/alarm-summary[severity='major']", ""},
                    {"/alarm-summary[severity='major']/cleared", "0"},
                    {"/alarm-summary[severity='major']/not-cleared", "0"},
                    {"/alarm-summary[severity='major']/severity", "major"},
                    {"/alarm-summary[severity='major']/total", "0"},
                    {"/alarm-summary[severity='minor']", ""},
                    {"/alarm-summary[severity='minor']/cleared", "0"},
                    {"/alarm-summary[severity='minor']/not-cleared", "0"},
                    {"/alarm-summary[severity='minor']/severity", "minor"},
                    {"/alarm-summary[severity='minor']/total", "0"},
                    {"/alarm-summary[severity='warning']", ""},
                    {"/alarm-summary[severity='warning']/cleared", "0"},
                    {"/alarm-summary[severity='warning']/not-cleared", "1"},
                    {"/alarm-summary[severity='warning']/severity", "warning"},
                    {"/alarm-summary[severity='warning']/total", "1"},
                });
    }

    SECTION("First action for an alarm is 'cleared'")
    {
        CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-1", "foo", {}, {}, "test1");
        CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-2-1", "bar", {}, {}, "test2");

        CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "foo", "", "cleared", "test1");
        CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2-1", "bar", "", "minor", "test2");
        REQUIRE(dataFromSysrepo(*cliSess, "/ietf-alarms:alarms/summary", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-summary[severity='critical']", ""},
                    {"/alarm-summary[severity='critical']/cleared", "0"},
                    {"/alarm-summary[severity='critical']/not-cleared", "0"},
                    {"/alarm-summary[severity='critical']/severity", "critical"},
                    {"/alarm-summary[severity='critical']/total", "0"},
                    {"/alarm-summary[severity='indeterminate']", ""},
                    {"/alarm-summary[severity='indeterminate']/cleared", "0"},
                    {"/alarm-summary[severity='indeterminate']/not-cleared", "0"},
                    {"/alarm-summary[severity='indeterminate']/severity", "indeterminate"},
                    {"/alarm-summary[severity='indeterminate']/total", "0"},
                    {"/alarm-summary[severity='major']", ""},
                    {"/alarm-summary[severity='major']/cleared", "0"},
                    {"/alarm-summary[severity='major']/not-cleared", "0"},
                    {"/alarm-summary[severity='major']/severity", "major"},
                    {"/alarm-summary[severity='major']/total", "0"},
                    {"/alarm-summary[severity='minor']", ""},
                    {"/alarm-summary[severity='minor']/cleared", "0"},
                    {"/alarm-summary[severity='minor']/not-cleared", "1"},
                    {"/alarm-summary[severity='minor']/severity", "minor"},
                    {"/alarm-summary[severity='minor']/total", "1"},
                    {"/alarm-summary[severity='warning']", ""},
                    {"/alarm-summary[severity='warning']/cleared", "0"},
                    {"/alarm-summary[severity='warning']/not-cleared", "0"},
                    {"/alarm-summary[severity='warning']/severity", "warning"},
                    {"/alarm-summary[severity='warning']/total", "0"},
                });
    }
}
