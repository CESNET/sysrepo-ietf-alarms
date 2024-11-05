#include "trompeloeil_doctest.h"
#include <chrono>
#include <experimental/iterator>
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"
#include "utils/libyang.h"

using namespace std::chrono_literals;
using namespace std::string_literals;

namespace {

struct ShelvedAlarm : public alarms::InstanceKey {
    std::string shelfName;

    bool operator==(const ShelvedAlarm&) const = default;
};

std::vector<ShelvedAlarm> extractShelvedAlarms(sysrepo::Session session)
{
    alarms::utils::ScopedDatastoreSwitch sw(session, sysrepo::Datastore::Operational);

    std::vector<ShelvedAlarm> res;
    if (auto data = session.getData(ietfAlarms)) {
        for (const auto& node : data->findXPath(shelvedAlarmListInstances)) {
            res.emplace_back(alarms::InstanceKey::fromNode(node), alarms::utils::childValue(node, "shelf-name"));
        }
    }

    return res;
}

std::vector<alarms::InstanceKey> extractAlarms(sysrepo::Session session)
{
    alarms::utils::ScopedDatastoreSwitch sw(session, sysrepo::Datastore::Operational);

    std::vector<alarms::InstanceKey> res;
    if (auto data = session.getData(ietfAlarms)) {
        for (const auto& node : data->findXPath(alarmListInstances)) {
            res.push_back(alarms::InstanceKey::fromNode(node));
        }
    }

    return res;
}

struct ShelfControl {
    std::string name;
    std::vector<std::string> resources;
    std::vector<alarms::Type> alarmTypes;

    bool operator==(const ShelfControl& o) const = default;
};

std::vector<ShelfControl> shelfControl(sysrepo::Session session)
{
    alarms::utils::ScopedDatastoreSwitch sw(session, sysrepo::Datastore::Running);

    std::vector<ShelfControl> res;

    if (auto data = session.getData(ietfAlarms)) {
        for (const auto& node : data->findXPath(controlShelf)) {
            ShelfControl ctrl;
            ctrl.name = alarms::utils::childValue(node, "name");

            for (const auto& resourceNode : node.findXPath("resource")) {
                ctrl.resources.emplace_back(resourceNode.asTerm().valueStr());
            }

            for (const auto& alarmTypeNode : node.findXPath("alarm-type")) {
                ctrl.alarmTypes.emplace_back(alarms::Type{alarms::utils::childValue(alarmTypeNode, "alarm-type-id"), alarms::utils::childValue(alarmTypeNode, "alarm-type-qualifier-match")});
            }

            res.emplace_back(std::move(ctrl));
        }
    }

    return res;
}
}

namespace doctest {
template <>
struct StringMaker<ShelvedAlarm> {
    static String convert(const ShelvedAlarm& obj)
    {
        std::ostringstream oss;
        oss << "{" << obj.resource << ", " << obj.type.id << ", " << obj.type.qualifier << ", " << obj.shelfName << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<ShelfControl> {
    static String convert(const ShelfControl& obj)
    {
        std::ostringstream oss;
        oss << "ShelfControl{name: " << obj.name << ", resources: [";
        std::copy(obj.resources.begin(), obj.resources.end(), std::experimental::make_ostream_joiner(oss, ", "));
        oss << "], alarm-types: [";
        std::transform(obj.alarmTypes.begin(), obj.alarmTypes.end(), std::experimental::make_ostream_joiner(oss, ", "),
                [](const alarms::Type t) {
                    return "id: " + t.id + ", qualifier: " + t.qualifier;
                });
        oss << "]}";
        return oss.str().c_str();
    }
};
}

AnyTimeBetween getExecutionTimeInterval(const std::function<void()>& foo)
{
    auto start = std::chrono::system_clock::now();
    foo();
    return AnyTimeBetween{start, std::chrono::system_clock::now()};
}

#define SHELF_SUMMARY(SESS, NUMBER_OF_SHELVED_ALARMS, SHELVED_ALARMS_LAST_CHANGE) \
    { \
        auto data = dataFromSysrepo(SESS, "/ietf-alarms:alarms/shelved-alarms", sysrepo::Datastore::Operational); \
        REQUIRE(data["/number-of-shelved-alarms"] == std::to_string(NUMBER_OF_SHELVED_ALARMS)); \
        REQUIRE(data["/shelved-alarms-last-changed"] == SHELVED_ALARMS_LAST_CHANGE); \
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

    CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-2-1", "high", {}, {}, "Alarm 1");
    CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-2-1", "low", {}, {}, "Alarm 1");
    CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-1", "high", {}, {}, "Alarm 1");

    REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                {"/alarm-inventory", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/will-clear", "true"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-list", ""},
                {"/alarm-list/number-of-alarms", "0"},
                {"/alarm-list/last-changed", initTime},
                {"/shelved-alarms", ""},
                {"/shelved-alarms/number-of-shelved-alarms", "0"},
                {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                {"/control", ""},
                {"/control/alarm-shelving", ""},
                {"/control/max-alarm-status-changes", "32"},
                {"/control/notify-status-changes", "all-state-changes"},
                {"/shelved-alarms", ""},
                ALARM_SUMMARY(
                        CRITICAL(Summary({.cleared = 0, .notCleared = 0})),
                        WARNING(Summary({.cleared = 0, .notCleared = 0})),
                        MAJOR(Summary({.cleared = 0, .notCleared = 0})),
                        MINOR(Summary({.cleared = 0, .notCleared = 0})),
                        INDETERMINATE(Summary({.cleared = 0, .notCleared = 0}))),
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
        if (shelved) {
            REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>({{{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "shelf"}}));
            REQUIRE(extractAlarms(*userSess).empty());
            SHELF_SUMMARY(*userSess, 1, origTime);
        } else {
            REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>{{{"alarms-test:alarm-2-1", "high"}, "edfa"}});
            REQUIRE(extractShelvedAlarms(*userSess).empty());
            REQUIRE(dataFromSysrepo(*userSess, alarmListInstances + "[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']"s, sysrepo::Datastore::Operational)["/time-created"] == origTime);
            SHELF_SUMMARY(*userSess, 0, initTime);
        }
    }

    SECTION("Alarms are moved from/to shelf when alarm-shelving control changes")
    {
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "high", "edfa", "warning", "text");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-1", "high", "wss", "warning", "text");
        CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "low", "wss", "warning", "text");

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", std::nullopt);
        userSess->applyChanges();

        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({
                    {{"alarms-test:alarm-2-1", "high"}, "edfa"},
                    {{"alarms-test:alarm-1", "high"}, "wss"},
                    {{"alarms-test:alarm-2-1", "low"}, "wss"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{});

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']");
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
        auto changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {"wss"}, {{"alarms-test:alarm-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({
                    {{"alarms-test:alarm-2-1", "high"}, "edfa"},
                    {{"alarms-test:alarm-2-1", "low"}, "wss"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-1", "high"}, "wss"}, "shelf"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);

        // append a value to shelf leaf-list and test again
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
        userSess->applyChanges();

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {"edfa", "wss"}, {{"alarms-test:alarm-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({
                    {{"alarms-test:alarm-2-1", "high"}, "edfa"},
                    {{"alarms-test:alarm-2-1", "low"}, "wss"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-1", "high"}, "wss"}, "shelf"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {"edfa", "wss"}, {{"alarms-test:alarm-1", "high"}, {"alarms-test:alarm-2-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({
                    {{"alarms-test:alarm-2-1", "low"}, "wss"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-1", "high"}, "wss"}, "shelf"},
                    {{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "shelf"},
                });
        SHELF_SUMMARY(*userSess, 2, changedTime);

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']");
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {}, {}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({}));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-1", "high"}, "wss"}, "shelf"},
                    {{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "shelf"},
                    {{{"alarms-test:alarm-2-1", "low"}, "wss"}, "shelf"},
                });
        SHELF_SUMMARY(*userSess, 3, changedTime);

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']");
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf-replacement", {}, {}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({}));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-1", "high"}, "wss"}, "shelf-replacement"},
                    {{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "shelf-replacement"},
                    {{{"alarms-test:alarm-2-1", "low"}, "wss"}, "shelf-replacement"},
                });
        SHELF_SUMMARY(*userSess, 3, changedTime);

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{
                    {"shelf-replacement", {}, {{"alarms-test:alarm-2-1", "high"}}},
                    {"aashelf", {}, {{"alarms-test:alarm-2-1", "high"}}},
                });
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({
                    {{"alarms-test:alarm-1", "high"}, "wss"},
                    {{"alarms-test:alarm-2-1", "low"}, "wss"},
                }));
        REQUIRE(dataFromSysrepo(*userSess, alarmListInstances + "[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']"s, sysrepo::Datastore::Operational)["/time-created"] == changedTime);
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "shelf-replacement"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']");
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"aashelf", {}, {{"alarms-test:alarm-2-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>({
                    {{"alarms-test:alarm-1", "high"}, "wss"},
                    {{"alarms-test:alarm-2-1", "low"}, "wss"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "aashelf"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);
    }

    SECTION("Alarm history is updated for shelved alarms")
    {
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelve all']", std::nullopt);
        userSess->setItem("/ietf-alarms:alarms/control/max-alarm-status-changes", "2");
        userSess->applyChanges();

        std::vector<AnyTimeBetween> changedTimes;
        changedTimes.emplace_back(CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "high", "edfa", "warning", "warning text"));
        changedTimes.emplace_back(CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "high", "edfa", "critical", "critical text"));
        changedTimes.emplace_back(CLIENT_ALARM_RPC(cli1Sess, "alarms-test:alarm-2-1", "high", "edfa", "cleared", "idk"));

        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>());
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {{{"alarms-test:alarm-2-1", "high"}, "edfa"}, "shelve all"},
                });

        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                {"/alarm-inventory", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/will-clear", "true"},
                {"/alarm-list", ""},
                {"/alarm-list/last-changed", initTime},
                {"/alarm-list/number-of-alarms", "0"},
                {"/control", ""},
                {"/control/alarm-shelving", ""},
                {"/control/alarm-shelving/shelf[name='shelve all']", ""},
                {"/control/alarm-shelving/shelf[name='shelve all']/name", "shelve all"},
                {"/control/max-alarm-status-changes", "2"},
                {"/control/notify-status-changes", "all-state-changes"},
                {"/shelved-alarms", ""},
                {"/shelved-alarms/number-of-shelved-alarms", "1"},
                {"/shelved-alarms/shelved-alarms-last-changed", changedTimes[2]},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "idk"},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", changedTimes[2]},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", changedTimes[0]},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "critical"},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                {"/shelved-alarms/shelved-alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/shelf-name", "shelve all"},
                SHELVED_ALARM_STATUS_CHANGE(1, "edfa", "alarms-test:alarm-2-1", "high", changedTimes[1], "critical", "critical text"),
                SHELVED_ALARM_STATUS_CHANGE(2, "edfa", "alarms-test:alarm-2-1", "high", changedTimes[2], "cleared", "idk"),
                ALARM_SUMMARY(
                        CRITICAL(Summary({.cleared = 0, .notCleared = 0})),
                        WARNING(Summary({.cleared = 0, .notCleared = 0})),
                        MAJOR(Summary({.cleared = 0, .notCleared = 0})),
                        MINOR(Summary({.cleared = 0, .notCleared = 0})),
                        INDETERMINATE(Summary({.cleared = 0, .notCleared = 0}))),
                });

        // alarm history is retained after unshelving
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelve all']");
        auto reshelveTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(extractAlarms(*userSess) == std::vector<alarms::InstanceKey>{
                    {{"alarms-test:alarm-2-1", "high"}, "edfa"},
                });
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{});
        REQUIRE(dataFromSysrepo(*userSess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational) == PropsWithTimeTest{
                {"/alarm-inventory", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/will-clear", "true"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']", ""},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/alarm-type-qualifier", "low"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/description", "Alarm 1"},
                {"/alarm-inventory/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']/will-clear", "true"},
                {"/alarm-list", ""},
                {"/alarm-list/last-changed", reshelveTime},
                {"/alarm-list/number-of-alarms", "1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']", ""},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-text", "idk"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-id", "alarms-test:alarm-2-1"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/alarm-type-qualifier", "high"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/is-cleared", "true"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-changed", changedTimes[2]},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/last-raised", changedTimes[0]},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/perceived-severity", "critical"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/resource", "edfa"},
                {"/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']/time-created", reshelveTime},
                ALARM_STATUS_CHANGE(1, "edfa", "alarms-test:alarm-2-1", "high", changedTimes[1], "critical", "critical text"),
                ALARM_STATUS_CHANGE(2, "edfa", "alarms-test:alarm-2-1", "high", changedTimes[2], "cleared", "idk"),
                {"/control", ""},
                {"/control/alarm-shelving", ""},
                {"/control/max-alarm-status-changes", "2"},
                {"/control/notify-status-changes", "all-state-changes"},
                {"/shelved-alarms", ""},
                {"/shelved-alarms/number-of-shelved-alarms", "0"},
                {"/shelved-alarms/shelved-alarms-last-changed", reshelveTime},
                ALARM_SUMMARY(
                        CRITICAL(Summary({.cleared = 1, .notCleared = 0})),
                        WARNING(Summary({.cleared = 0, .notCleared = 0})),
                        MAJOR(Summary({.cleared = 0, .notCleared = 0})),
                        MINOR(Summary({.cleared = 0, .notCleared = 0})),
                        INDETERMINATE(Summary({.cleared = 0, .notCleared = 0}))),
                });
    }

    copyStartupDatastore("ietf-alarms"); // cleanup after last run so we can cleanly uninstall modules
}
