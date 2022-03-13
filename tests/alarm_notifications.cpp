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

#define EXPECT_NOTIFICATION(PROPS) NAMED_REQUIRE_CALL(events, notified(trompeloeil::eq(PROPS))).IN_SEQUENCE(seq1)

#define FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE) dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational)["/alarm-list/alarm[resource='" RESOURCE "'][alarm-type-id='" ID "'][alarm-type-qualifier='" QUALIFIER "']/last-changed"]

#define CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(SESS, ID, QUALIFIER, RESOURCE, SEVERITY, TEXT) \
    {                                                                                           \
        auto rpcInput = std::map<std::string, std::string>{                                     \
            {"resource", RESOURCE},                                                             \
            {"alarm-type-id", ID},                                                              \
            {"alarm-type-qualifier", QUALIFIER},                                                \
            {"severity", SEVERITY},                                                             \
            {"alarm-text", TEXT},                                                               \
        };                                                                                      \
                                                                                                \
        PropsWithTimeTest expectProps = {                                                       \
            {"alarm-type-id", ID},                                                              \
            {"resource", RESOURCE},                                                             \
            {"alarm-text", TEXT},                                                               \
            {"perceived-severity", SEVERITY},                                                   \
            {"time", SHORTLY_AFTER(std::chrono::system_clock::now())},                          \
        };                                                                                      \
        if (!std::string(QUALIFIER).empty()) {                                                  \
            expectProps["alarm-type-qualifier"] = QUALIFIER;                                    \
        }                                                                                       \
        expectations.push_back(EXPECT_NOTIFICATION(expectProps));                               \
                                                                                                \
        rpcFromSysrepo(*SESS, rpcPrefix, rpcInput);                                             \
        lastChangedTimesInSysrepo.push_back(FETCH_TIME_CHANGED(ID, QUALIFIER, RESOURCE));       \
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

    NotificationWatcher events(*userSess, alarmStatusNotification);

    // checking that time of the notification exactly equals to the time announced in leaf last-changed
    // because notifications are coming async (but in order), the easiest way is probably to store the times of last-changed (fetch from sysrepo after each change) and then check all in one go at the end
    std::vector<std::string> lastChangedTimesInSysrepo;
    std::vector<std::string> lastChangedTimesFromNotifications;
    auto sub = userSess->onNotification(
        ietfAlarmsModule,
        [&](auto, auto, sysrepo::NotificationType type, const std::optional<libyang::DataNode> tree, auto) {
            if (type != sysrepo::NotificationType::Realtime) {
                return;
            }

            auto node = tree->findPath("time");
            REQUIRE(node);
            REQUIRE(node->isTerm());
            lastChangedTimesFromNotifications.emplace_back(node->asTerm().valueStr());
        },
        alarmStatusNotification);

    CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "warning", "Hey, I'm overheating.");
    CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli2Sess, "alarms-test:alarm-1", "qual", "idk", "minor", ":-)");
    CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "major", "Hey, I'm overheating.");
    CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli2Sess, "alarms-test:alarm-1", "", "edfa", "major", "Text change.");
    CLIENT_ALARM_RPC_AND_EXPECT_NOTIFICATION(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "Temperature normal.");
    CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "", "edfa", "cleared", "Temperature normal.");

    waitForCompletionAndBitMore(seq1);

    REQUIRE(lastChangedTimesInSysrepo == lastChangedTimesFromNotifications);
}
