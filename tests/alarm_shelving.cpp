#include "trompeloeil_doctest.h"
#include <chrono>
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

const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto ietfAlarms = "/ietf-alarms:alarms";
const auto alarmListInstances = "/ietf-alarms:alarms/alarm-list/alarm";
const auto shelvedAlarmsListInstances = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
const auto controlShelf = "/ietf-alarms:alarms/control/alarm-shelving/shelf";


struct ShelvedAlarm {
    std::string resource;
    std::string alarmType;
    std::string alarmTypeQualifier;
    std::string shelfName;

    auto operator<=>(const ShelvedAlarm&) const = default;
};

struct Alarm {
    std::string resource;
    std::string alarmType;
    std::string alarmTypeQualifier;

    auto operator<=>(const Alarm&) const = default;
};

std::vector<ShelvedAlarm> extractShelvedAlarms(sysrepo::Session session)
{
    alarms::utils::ScopedDatastoreSwitch sw(session, sysrepo::Datastore::Operational);

    std::vector<ShelvedAlarm> res;
    if (auto data = session.getData(ietfAlarms)) {
        for (const auto& node : data->findXPath(shelvedAlarmsListInstances)) {
            res.push_back({alarms::utils::childValue(node, "resource"),
                           alarms::utils::childValue(node, "alarm-type-id"),
                           alarms::utils::childValue(node, "alarm-type-qualifier"),
                           alarms::utils::childValue(node, "shelf-name")});
        }
    }

    return res;
}

std::vector<Alarm> extractAlarms(sysrepo::Session session)
{
    alarms::utils::ScopedDatastoreSwitch sw(session, sysrepo::Datastore::Operational);

    std::vector<Alarm> res;
    if (auto data = session.getData(ietfAlarms)) {
        for (const auto& node : data->findXPath(alarmListInstances)) {
            res.push_back({alarms::utils::childValue(node, "resource"),
                           alarms::utils::childValue(node, "alarm-type-id"),
                           alarms::utils::childValue(node, "alarm-type-qualifier")});
        }
    }

    return res;
}

struct ShelfControl {
    struct AlarmType {
        std::string id;
        std::string qualifierMatch;

        bool operator==(const AlarmType& o) const
        {
            return std::tie(id, qualifierMatch) == std::tie(o.id, o.qualifierMatch);
        }
    };

    std::string name;
    std::vector<std::string> resources;
    std::vector<AlarmType> alarmTypes;

    bool operator==(const ShelfControl& o) const
    {
        return std::tie(name, resources, alarmTypes) == std::tie(o.name, o.resources, o.alarmTypes);
    }
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
                ctrl.alarmTypes.emplace_back(ShelfControl::AlarmType{alarms::utils::childValue(alarmTypeNode, "alarm-type-id"), alarms::utils::childValue(alarmTypeNode, "alarm-type-qualifier-match")});
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
        oss << "{" << obj.resource << ", " << obj.alarmType << ", " << obj.alarmTypeQualifier << ", " << obj.shelfName << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<Alarm> {
    static String convert(const Alarm& obj)
    {
        std::ostringstream oss;
        oss << "{" << obj.resource << ", " << obj.alarmType << ", " << obj.alarmTypeQualifier << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<std::vector<ShelvedAlarm>> {
    static String convert(const std::vector<ShelvedAlarm>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& e : v) {
            os << "  \"" << StringMaker<ShelvedAlarm>::convert(e) << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    };
};

template <>
struct StringMaker<std::vector<Alarm>> {
    static String convert(const std::vector<Alarm>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& e : v) {
            os << "  \"" << StringMaker<Alarm>::convert(e) << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    };
};
}

AnyTimeBetween getExecutionTimeInterval(const std::function<void()>& foo)
{
    auto start = std::chrono::system_clock::now();
    foo();
    return AnyTimeBetween{start, std::chrono::system_clock::now()};
}

#define SHELF_SUMMARY(SESS, NUMBER_OF_SHELVED_ALARMS, SHELVED_ALARMS_LAST_CHANGE)                                 \
    {                                                                                                             \
        auto data = dataFromSysrepo(SESS, "/ietf-alarms:alarms/shelved-alarms", sysrepo::Datastore::Operational); \
        REQUIRE(data["/number-of-shelved-alarms"] == std::to_string(NUMBER_OF_SHELVED_ALARMS));                   \
        REQUIRE(data["/shelved-alarms-last-changed"] == SHELVED_ALARMS_LAST_CHANGE);                              \
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
                {"/shelved-alarms", ""},
                {"/shelved-alarms/number-of-shelved-alarms", "0"},
                {"/shelved-alarms/shelved-alarms-last-changed", initTime},
                {"/control", ""},
                {"/control/alarm-shelving", ""},
                {"/shelved-alarms", ""},
                {"/summary", ""},
                {"/summary/alarm-summary[severity='critical']", ""},
                {"/summary/alarm-summary[severity='critical']/cleared", "0"},
                {"/summary/alarm-summary[severity='critical']/not-cleared", "0"},
                {"/summary/alarm-summary[severity='critical']/severity", "critical"},
                {"/summary/alarm-summary[severity='critical']/total", "0"},
                {"/summary/alarm-summary[severity='indeterminate']", ""},
                {"/summary/alarm-summary[severity='indeterminate']/cleared", "0"},
                {"/summary/alarm-summary[severity='indeterminate']/not-cleared", "0"},
                {"/summary/alarm-summary[severity='indeterminate']/severity", "indeterminate"},
                {"/summary/alarm-summary[severity='indeterminate']/total", "0"},
                {"/summary/alarm-summary[severity='major']", ""},
                {"/summary/alarm-summary[severity='major']/cleared", "0"},
                {"/summary/alarm-summary[severity='major']/not-cleared", "0"},
                {"/summary/alarm-summary[severity='major']/severity", "major"},
                {"/summary/alarm-summary[severity='major']/total", "0"},
                {"/summary/alarm-summary[severity='minor']", ""},
                {"/summary/alarm-summary[severity='minor']/cleared", "0"},
                {"/summary/alarm-summary[severity='minor']/not-cleared", "0"},
                {"/summary/alarm-summary[severity='minor']/severity", "minor"},
                {"/summary/alarm-summary[severity='minor']/total", "0"},
                {"/summary/alarm-summary[severity='warning']", ""},
                {"/summary/alarm-summary[severity='warning']/cleared", "0"},
                {"/summary/alarm-summary[severity='warning']/not-cleared", "0"},
                {"/summary/alarm-summary[severity='warning']/severity", "warning"},
                {"/summary/alarm-summary[severity='warning']/total", "0"},
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
            REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>({{"edfa", "alarms-test:alarm-2-1", "high", "shelf"}}));
            REQUIRE(extractAlarms(*userSess).empty());
            SHELF_SUMMARY(*userSess, 1, origTime);
        } else {
            REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>{{"edfa", "alarms-test:alarm-2-1", "high"}});
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

        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({
                    {"edfa", "alarms-test:alarm-2-1", "high"},
                    {"wss", "alarms-test:alarm-1", "high"},
                    {"wss", "alarms-test:alarm-2-1", "low"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{});

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']");
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
        auto changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {"wss"}, {{"alarms-test:alarm-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({
                    {"edfa", "alarms-test:alarm-2-1", "high"},
                    {"wss", "alarms-test:alarm-2-1", "low"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"wss", "alarms-test:alarm-1", "high", "shelf"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);

        // append a value to shelf leaf-list and test again
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']", std::nullopt);
        userSess->applyChanges();

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {"wss", "edfa"}, {{"alarms-test:alarm-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({
                    {"edfa", "alarms-test:alarm-2-1", "high"},
                    {"wss", "alarms-test:alarm-2-1", "low"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"wss", "alarms-test:alarm-1", "high", "shelf"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {"wss", "edfa"}, {{"alarms-test:alarm-1", "high"}, {"alarms-test:alarm-2-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({
                    {"wss", "alarms-test:alarm-2-1", "low"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"wss", "alarms-test:alarm-1", "high", "shelf"},
                    {"edfa", "alarms-test:alarm-2-1", "high", "shelf"},
                });
        SHELF_SUMMARY(*userSess, 2, changedTime);

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']");
        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='edfa']");
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf", {}, {}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({}));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"wss", "alarms-test:alarm-1", "high", "shelf"},
                    {"edfa", "alarms-test:alarm-2-1", "high", "shelf"},
                    {"wss", "alarms-test:alarm-2-1", "low", "shelf"},
                });
        SHELF_SUMMARY(*userSess, 3, changedTime);

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']");
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"shelf-replacement", {}, {}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({}));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"wss", "alarms-test:alarm-1", "high", "shelf-replacement"},
                    {"edfa", "alarms-test:alarm-2-1", "high", "shelf-replacement"},
                    {"wss", "alarms-test:alarm-2-1", "low", "shelf-replacement"},
                });
        SHELF_SUMMARY(*userSess, 3, changedTime);

        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='aashelf']/alarm-type[alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier-match='high']", std::nullopt);
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{
                    {"shelf-replacement", {}, {{"alarms-test:alarm-2-1", "high"}}},
                    {"aashelf", {}, {{"alarms-test:alarm-2-1", "high"}}},
                });
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({
                    {"wss", "alarms-test:alarm-1", "high"},
                    {"wss", "alarms-test:alarm-2-1", "low"},
                }));
        REQUIRE(dataFromSysrepo(*userSess, alarmListInstances + "[resource='wss'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='low']"s, sysrepo::Datastore::Operational)["/time-created"] == changedTime);
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"edfa", "alarms-test:alarm-2-1", "high", "shelf-replacement"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);

        userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf-replacement']");
        changedTime = getExecutionTimeInterval([&]() { userSess->applyChanges(); });

        REQUIRE(shelfControl(*userSess) == std::vector<ShelfControl>{{"aashelf", {}, {{"alarms-test:alarm-2-1", "high"}}}});
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>({
                    {"wss", "alarms-test:alarm-1", "high"},
                    {"wss", "alarms-test:alarm-2-1", "low"},
                }));
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>{
                    {"edfa", "alarms-test:alarm-2-1", "high", "aashelf"},
                });
        SHELF_SUMMARY(*userSess, 1, changedTime);
    }

    copyStartupDatastore("ietf-alarms"); // cleanup after last run so we can cleanly uninstall modules
}

