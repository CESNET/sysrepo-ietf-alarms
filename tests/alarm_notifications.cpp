#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "events.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

const auto ietfAlarmsModule = "ietf-alarms";
const auto alarmStatusNotification = "/"s + ietfAlarmsModule + ":alarm-notification";
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto expectedTimeDegreeOfFreedom = 300ms;
}

struct StatusNotifications {
    MAKE_CONST_MOCK1(notify, void(const std::map<std::string, std::string>&));
};

#define EXPECT_NOTIFICATION(PROPS) NAMED_REQUIRE_CALL(statusNotifications, notify(trompeloeil::eq(PROPS))).IN_SEQUENCE(seq1)

#define FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE) dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational)["/alarm-list/alarm[resource='" RESOURCE "'][alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/last-changed"]

#define CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT)   \
    {                                                                                             \
        auto rpcInput = std::map<std::string, std::string>{                                       \
            {"resource", RESOURCE},                                                               \
            {"alarm-type-id", ID},                                                                \
            {"alarm-type-qualifier", QUALIFIER},                                                  \
            {"severity", SEVERITY},                                                               \
            {"alarm-text", TEXT},                                                                 \
        };                                                                                        \
                                                                                                  \
        PropsWithTimeTest expectProps = {                                                         \
            {alarmStatusNotification, ""},                                                        \
            {alarmStatusNotification + "/alarm-type-id", ID},                                     \
            {alarmStatusNotification + "/resource", RESOURCE},                                    \
            {alarmStatusNotification + "/alarm-text", TEXT},                                      \
            {alarmStatusNotification + "/perceived-severity", SEVERITY},                          \
            {alarmStatusNotification + "/time", SHORTLY_AFTER(std::chrono::system_clock::now())}, \
        };                                                                                        \
        if (!std::string(QUALIFIER).empty()) {                                                    \
            expectProps[alarmStatusNotification + "/alarm-type-qualifier"] = QUALIFIER;           \
        }                                                                                         \
        expectations.push_back(EXPECT_NOTIFICATION(expectProps));                                 \
                                                                                                  \
        rpcFromSysrepo(*SESS, rpcPrefix, rpcInput);                                               \
        lastChangedTimes.push_back(FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE));                  \
    }

TEST_CASE("Receiving alarm notifications")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    TEST_SYSREPO_CLIENT_INIT(cli1Sess);
    TEST_SYSREPO_CLIENT_INIT(cli2Sess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    trompeloeil::sequence seq1;
    std::vector<std::unique_ptr<trompeloeil::expectation>> expectations;

    StatusNotifications statusNotifications;
    EventWatcher events([&statusNotifications](const EventWatcher::Event& event) { statusNotifications.notify(event.data); });
    auto statusChangeSubscription = userSess->onNotification(ietfAlarmsModule, events, alarmStatusNotification.c_str());

    // checking that time of the notification exactly equals to the time announced in leaf last-changed
    // because notifications are coming async, the easiest way is probably to store the times of last-changed (fetch from sysrepo after each change) and then check all in one go at the end
    std::vector<std::string> lastChangedTimes;

    SECTION("notifications: all-status-changes")
    {
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "Hey, I'm overheating.");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli2Sess, "alarms-test:alarm-1", "qual", "idk", "minor", ":-)");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "major", "Hey, I'm overheating.");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli2Sess, "alarms-test:alarm-1", "", "edfa", "major", "Text change.");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "Temperature normal.");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "Temperature normal.");
    }

    SECTION("notifications: raise-and-clear")
    {
        userSess->setItem("/ietf-alarms:alarms/control/notify-status-changes", "raise-and-clear");
        userSess->applyChanges();

        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "Hey, I'm overheating.");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli2Sess, "alarms-test:alarm-1", "qual", "idk", "minor", ":-)");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "major", "Hey, I'm overheating.");
        CLIENT_ALARM_RPC(cli2Sess, "alarms-test:alarm-1", "", "edfa", "major", "Text change.");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "Temperature normal.");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "Temperature normal.");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "Hey, I'm overheating.");
    }

    SECTION("notifications: severity-level")
    {
        userSess->setItem("/ietf-alarms:alarms/control/notify-status-changes", "severity-level");
        userSess->setItem("/ietf-alarms:alarms/control/notify-severity-level", "major");
        userSess->applyChanges();

        /* The following test case is from YANG model ietf-alarms@2019-09-11,
         * found under description to the leaf ietf-alarms:alarms/control/notify-status-changes
         *
         * [(Time, severity, clear)]
         * [(T1, major, -), (T2, minor, -), (T3, warning, -), (T4, minor, -),
         *  (T5, major, -), (T6, critical, -), (T7, major.  -), (T8, major, clear)]

         * In that case, notifications will be sent at times
         * T1, T2, T5, T6, T7, and T8.
         */

        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "major", "T1");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "minor", "T2");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "T3");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "minor", "T4");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "major", "T5");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "critical", "T6");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "major", "T7");
        CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "T8");
    }

    waitForCompletionAndBitMore(seq1);

    REQUIRE(events.count() == lastChangedTimes.size());
    for (size_t i = 0; i < events.count(); i++) {
        auto event = events.peek(i);
        if (i > 0) {
            REQUIRE(events.peek(i - 1).received < event.received);
        }
        REQUIRE(event.data[alarmStatusNotification + "/time"] == lastChangedTimes[i]);
    }
}
