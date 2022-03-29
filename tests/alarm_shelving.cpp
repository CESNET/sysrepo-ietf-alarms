#include "trompeloeil_doctest.h"
#include <chrono>
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;

namespace {

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
}

AnyTimeBetween getExecutionTimeInterval(const std::function<void()>& foo)
{
    auto start = std::chrono::system_clock::now();
    foo();
    return AnyTimeBetween{start, std::chrono::system_clock::now()};
}

TEST_CASE("Alarm shelving")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    std::unique_ptr<alarms::Daemon> daemon;
    AnyTimeBetween initTime;

    {
        auto start = std::chrono::system_clock::now();
        daemon = std::make_unique<alarms::Daemon>();
        initTime = AnyTimeBetween{start, std::chrono::system_clock::now()};
    }

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
                {"/shelved-alarms", ""},
            });

    SECTION("Test shelving on publish action")
    {
        bool shelved;

        SECTION("Empty criteria")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']", std::nullopt);
            shelved = true;
        }

        SECTION("Exact match")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("Multiple resources; ours included")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='ahoj']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("Multiple resources; ours not included")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='ahoj']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = false;
        }

        SECTION("No resources")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("Multiple alarm-types, ours included")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier-match='low']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("No resources; multiple alarm-types, ours included")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-2'][alarm-type-qualifier-match='low']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("No resources; multiple alarm-types, ours not included")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='low']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("No resources; multiple alarm-types, matching types but not qualifiers")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='low']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='not-high']", std::nullopt);
            shelved = false;
        }

        SECTION("Accept descendants of alarm-2")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("Accept all descendants of alarm-2 but with other qualifier")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier-match='not-high']", std::nullopt);
            shelved = false;
        }

        SECTION("Accept all descendants of alarm-1")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = false;
        }

        SECTION("Accept all descendants of alarm-2 and alarm-1")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier-match='high']", std::nullopt);
            shelved = true;
        }

        SECTION("Accept all descendants of alarm-2 with an incorrect qualifier and alarm-1 with a correct one")
        {
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", std::nullopt);
            userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier-match='low']", std::nullopt);
            shelved = false;
        }

        userSess->applyChanges();

        auto origTime = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "high", "edfa", "warning", "Hey, I'm overheating.");
        std::map<std::string, std::string> actualDataFromSysrepo = dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
        if (shelved) {
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-inventory", ""},
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "0"},
                        {"/alarm-list/last-changed", initTime},
                        {"/shelved-alarms", ""},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime},
                        {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                        {"/control", ""},
                        {"/control/alarm-shelving", ""},
                    });
        } else {
            REQUIRE(actualDataFromSysrepo == PropsWithTimeTest{
                        {"/alarm-inventory", ""},
                        {"/alarm-list", ""},
                        {"/alarm-list/number-of-alarms", "1"},
                        {"/alarm-list/last-changed", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "Hey, I'm overheating."},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/time-created", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime},
                        {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime},
                        {"/control", ""},
                        {"/control/alarm-shelving", ""},
                        {"/shelved-alarms", ""},
                    });
        }
    }

    SECTION("Alarms are moved from/to shelf when alarm-shelving control changes")
    {
        auto origTime1 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "high", "edfa", "warning", "text");
        auto origTime2 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "wss", "warning", "text");
        auto origTime3 = CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "low", "wss", "warning", "text");

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", std::nullopt);
        userSess->applyChanges();

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "3"},
                    {"/alarm-list/last-changed", origTime3},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/time-created", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/time-created", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                });

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']");
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
        auto changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='shelf']", ""},
                    {"/shelf[name='shelf']/name", "shelf"},
                    {"/shelf[name='shelf']/resource[.='wss']", "wss"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/time-created", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/time-created", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                });

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
        userSess->applyChanges();

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='shelf']", ""},
                    {"/shelf[name='shelf']/name", "shelf"},
                    {"/shelf[name='shelf']/resource[.='wss']", "wss"},
                    {"/shelf[name='shelf']/resource[.='edfa']", "edfa"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/time-created", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/time-created", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                });

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='shelf']", ""},
                    {"/shelf[name='shelf']/name", "shelf"},
                    {"/shelf[name='shelf']/resource[.='wss']", "wss"},
                    {"/shelf[name='shelf']/resource[.='edfa']", "edfa"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "1"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/time-created", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                });

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']");
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='shelf']", ""},
                    {"/shelf[name='shelf']/name", "shelf"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "0"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/shelf-name", "shelf"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "shelf"},
                });

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']");
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']", std::nullopt);
        userSess->applyChanges();
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='shelf-replacement']", ""},
                    {"/shelf[name='shelf-replacement']/name", "shelf-replacement"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "0"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/shelf-name", "shelf-replacement"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/shelf-name", "shelf-replacement"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "shelf-replacement"},
                });

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='aashelf']", ""},
                    {"/shelf[name='aashelf']/name", "aashelf"},
                    {"/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                    {"/shelf[name='shelf-replacement']", ""},
                    {"/shelf[name='shelf-replacement']/name", "shelf-replacement"},
                    {"/shelf[name='shelf-replacement']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='shelf-replacement']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelf[name='shelf-replacement']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/time-created", changedTime},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "shelf-replacement"},
                });

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']");
        userSess->applyChanges();

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms/control/alarm-shelving", sysrepo::Datastore::Running) == std::map<std::string, std::string>{
                    {"/shelf[name='aashelf']", ""},
                    {"/shelf[name='aashelf']/name", "aashelf"},
                    {"/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", ""},
                    {"/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']/alarm-type-qualifier-match", "high"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                    {"/alarm-inventory", ""},
                    {"/alarm-list", ""},
                    {"/alarm-list/number-of-alarms", "2"},
                    {"/alarm-list/last-changed", changedTime},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-raised", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/last-changed", origTime3},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/time-created", changedTime},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/resource", "wss"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-raised", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/last-changed", origTime2},
                    {"/alarm-list/alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/time-created", changedTime},
                    {"/control", ""},
                    {"/control/alarm-shelving", ""},
                    {"/shelved-alarms", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "false"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "warning"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "text"},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", origTime1},
                    {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "aashelf"},
                });
    }

    copyStartupDatastore("ietf-alarms"); // cleanup after last run so we can cleanly uninstall modules
}

