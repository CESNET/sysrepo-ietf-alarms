#include "trompeloeil_doctest.h"
#include <mutex>
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "events.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;

#define EXPECT_NOTIFICATION(PROPS) NAMED_REQUIRE_CALL(eventsAlarmStatus, notified(trompeloeil::eq(PROPS))).IN_SEQUENCE(seq1)

#define FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE) dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational)["/alarm-list/alarm[resource='" RESOURCE "'][alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/last-changed"]

#define CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT) \
    { \
        auto rpcInput = std::map<std::string, std::string>{ \
            {"resource", RESOURCE}, \
            {"alarm-type-id", ID}, \
            {"alarm-type-qualifier", QUALIFIER}, \
            {"severity", SEVERITY}, \
            {"alarm-text", TEXT}, \
        }; \
\
        auto now = std::chrono::system_clock::now(); \
        PropsWithTimeTest expectProps = { \
            {"alarm-type-id", ID}, \
            {"resource", RESOURCE}, \
            {"alarm-text", TEXT}, \
            {"perceived-severity", SEVERITY}, \
            {"time", AnyTimeBetween{now, now + 300ms}}, \
        }; \
        if (!std::string(QUALIFIER).empty()) { \
            expectProps["alarm-type-qualifier"] = QUALIFIER; \
        } \
        expectations.push_back(EXPECT_NOTIFICATION(expectProps)); \
\
        rpcFromSysrepo(*SESS, rpcPrefix, rpcInput); \
        lastChangedTimesInSysrepo.push_back(FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE)); \
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

    // Checking that time of the notification exactly equals to the time announced in leaf last-changed.
    // Because notifications are coming async (but in order), the easiest way is probably to store the times of
    // last-changed (fetch from sysrepo after each change) and then check all in one go at the end.
    // Note that upstream says that this might be fragile; we have two client sessions which are invoking RPCs
    // "close to each other", and the notification delivery happens in a background, sysrepo-managed thread.
    // So when these sequences no longer pass in future, we know why that is.
    std::vector<std::string> lastChangedTimesInSysrepo;
    std::vector<std::string> lastChangedTimesFromNotifications;
    std::mutex mtx;

    SECTION("Alarm status changes (/ietf-alarms:alarms/alarm-notification)")
    {
        NotificationWatcher eventsAlarmStatus(*userSess, alarmStatusNotification, [&mtx, &lastChangedTimesFromNotifications](const std::optional<libyang::DataNode>& tree) {
            auto node = tree->findPath("time");
            REQUIRE(node);
            REQUIRE(node->isTerm());
            std::lock_guard lck(mtx);
            lastChangedTimesFromNotifications.emplace_back(node->asTerm().valueStr());
        });

        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-1", "", {}, {}, "");
        CLIENT_INTRODUCE_ALARM(cli2Sess, "alarms-test:alarm-1", "", {}, {}, "");
        CLIENT_INTRODUCE_ALARM(cli2Sess, "alarms-test:alarm-1", "qual", {}, {}, "");
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

        std::lock_guard lck(mtx);
        REQUIRE(lastChangedTimesInSysrepo == lastChangedTimesFromNotifications);
    }

    SECTION("Inventory status changes (/ietf-alarms:alarms/alarm-inventory-changed)")
    {
        NotificationWatcher eventsInventory(*userSess, inventoryNotification);

        REQUIRE_CALL(eventsInventory, notified(NotificationWatcher::data_t{})).IN_SEQUENCE(seq1).TIMES(2);
        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-1", "high", {}, ({"minor"}), "Some description");
        CLIENT_INTRODUCE_ALARM(cli1Sess, "alarms-test:alarm-2", "", ({"edfa", "wss"}), ({"minor", "major"}), "Another description");

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
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "true"},
                });

        REQUIRE_CALL(eventsInventory, notified(NotificationWatcher::data_t{})).IN_SEQUENCE(seq1);
        CLIENT_INTRODUCE_ALARM(cli2Sess, "alarms-test:alarm-2-1", "", ({"edfa"}), {}, "Another description");

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
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "true"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/alarm-type-qualifier", ""},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/description", "Another description"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/resource[1]", "edfa"},
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='']/will-clear", "true"},
                });

        REQUIRE_CALL(eventsInventory, notified(NotificationWatcher::data_t{})).IN_SEQUENCE(seq1);
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
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "true"},
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
                    {"/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']/will-clear", "true"},
                });

        waitForCompletionAndBitMore(seq1);
    }
}
