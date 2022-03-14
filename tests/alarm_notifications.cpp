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
const auto inventoryNotification = "/"s + ietfAlarmsModule + ":alarm-inventory-changed";
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto expectedTimeDegreeOfFreedom = 300ms;
}

struct StatusNotifications {
    MAKE_CONST_MOCK1(notify, void(const std::map<std::string, std::string>&));
};

struct InventoryNotifications {
    MAKE_CONST_MOCK1(notify, void(const std::string&));
};

#define EXPECT_NOTIFICATION(PROPS) NAMED_REQUIRE_CALL(statusNotifications, notify(trompeloeil::eq(PROPS))).IN_SEQUENCE(seq1)

#define FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE) dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational)["/alarm-list/alarm[resource='" RESOURCE "'][alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/last-changed"]

#define CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT) \
    [&]() {                                                                                     \
        auto rpcInput = std::map<std::string, std::string>{                                     \
            {"resource", RESOURCE},                                                             \
            {"alarm-type-id", ID},                                                              \
            {"alarm-type-qualifier", QUALIFIER},                                                \
            {"severity", SEVERITY},                                                             \
            {"alarm-text", TEXT},                                                               \
        };                                                                                      \
                                                                                                \
        auto time = std::chrono::system_clock::now();                                           \
                                                                                                \
        PropsWithTimeTest expectProps = {                                                       \
            {alarmStatusNotification, ""},                                                      \
            {alarmStatusNotification + "/alarm-type-id", ID},                                   \
            {alarmStatusNotification + "/resource", RESOURCE},                                  \
            {alarmStatusNotification + "/alarm-text", TEXT},                                    \
            {alarmStatusNotification + "/perceived-severity", SEVERITY},                        \
            {alarmStatusNotification + "/time", SHORTLY_AFTER(time)},                           \
        };                                                                                      \
        if (!std::string(QUALIFIER).empty()) {                                                  \
            expectProps[alarmStatusNotification + "/alarm-type-qualifier"] = QUALIFIER;         \
        }                                                                                       \
        expectations.push_back(EXPECT_NOTIFICATION(expectProps));                               \
                                                                                                \
        rpcFromSysrepo(*SESS, rpcPrefix, rpcInput);                                             \
        lastChangedTimes.push_back(FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE));                \
        return time;                                                                            \
    }();

#define CLIENT_ALARM_INVENTORY(SESS, ID, QUALIFIER, WILL_CLEAR, DESCRIPTION, RESOURCES, SEVERITY_LEVELS)                                                         \
    SESS->setItem("/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/will-clear", WILL_CLEAR);        \
    SESS->setItem("/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/description", DESCRIPTION);      \
    for (const auto& e : std::vector<std::string> RESOURCES) {                                                                                                   \
        SESS->setItem("/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/resource", e.c_str());       \
    }                                                                                                                                                            \
    for (const auto& e : std::vector<std::string> SEVERITY_LEVELS) {                                                                                             \
        SESS->setItem("/ietf-alarms:alarms/alarm-inventory/alarm-type[alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/severity-level", e.c_str()); \
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


    SECTION("Alarm status changes (/ietf-alarms:alarms/alarm-notification)")
    {
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

    SECTION("Alarm status changes (/ietf-alarms:alarms/alarm-notification)")
    {
        InventoryNotifications inventoryNotificationsMock;
        EventWatcher events([&inventoryNotificationsMock](const EventWatcher::Event& event) { inventoryNotificationsMock.notify(event.xPath); });
        auto statusChangeSubscription = userSess->onNotification(ietfAlarmsModule, events, inventoryNotification.c_str());

        cli1Sess->switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE_CALL(inventoryNotificationsMock, notify(inventoryNotification)).IN_SEQUENCE(seq1);
        CLIENT_ALARM_INVENTORY(cli1Sess, "alarms-test:alarm-1", "high", "true", "Some description", {}, ({"minor"}));
        CLIENT_ALARM_INVENTORY(cli1Sess, "alarms-test:alarm-2", "", "false", "Another description", ({"edfa", "wss"}), ({"minor", "major"}));
        cli1Sess->applyChanges();
        cli1Sess->switchDatastore(sysrepo::Datastore::Running);

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Some description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/description", "Another description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[1]", "edfa"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[2]", "wss"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[2]", "major"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "false"},
                });

        cli2Sess->switchDatastore(sysrepo::Datastore::Operational);
        REQUIRE_CALL(inventoryNotificationsMock, notify(inventoryNotification)).IN_SEQUENCE(seq1);
        CLIENT_ALARM_INVENTORY(cli2Sess, "alarms-test:alarm-2-1", "", "false", "Another description", ({"edfa"}), {});
        cli2Sess->applyChanges();
        cli2Sess->switchDatastore(sysrepo::Datastore::Running);

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Some description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/description", "Another description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[1]", "edfa"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[2]", "wss"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[2]", "major"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "false"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/description", "Another description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource[1]", "edfa"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/will-clear", "false"},
                });

        REQUIRE_CALL(inventoryNotificationsMock, notify(inventoryNotification)).IN_SEQUENCE(seq1);
        TEST_SYSREPO_CLIENT_DISCONNECT(cli2Sess);
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Some description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/description", "Another description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[1]", "edfa"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[2]", "wss"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[2]", "major"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "false"},
                });

        cli2Sess = TEST_INIT_SESSION;
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/alarm-inventory", sysrepo::Datastore::Operational) == std::map<std::string, std::string>{
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Some description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/description", "Another description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[1]", "edfa"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/resource[2]", "wss"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[1]", "minor"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/severity-level[2]", "major"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "false"},
                });

        waitForCompletionAndBitMore(seq1);
    }
}
