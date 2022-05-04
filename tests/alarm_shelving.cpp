#include "trompeloeil_doctest.h"
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

struct ShelvedAlarm {
    std::string resource;
    std::string alarmType;
    std::string alarmTypeQualifier;
    std::string shelfName;

    bool operator==(const ShelvedAlarm& other) const
    {
        return std::tie(resource, alarmType, alarmTypeQualifier, shelfName) == std::tie(other.resource, other.alarmType, other.alarmTypeQualifier, other.shelfName);
    }
};

struct Alarm {
    std::string resource;
    std::string alarmType;
    std::string alarmTypeQualifier;

    bool operator==(const Alarm& other) const
    {
        return std::tie(resource, alarmType, alarmTypeQualifier) == std::tie(other.resource, other.alarmType, other.alarmTypeQualifier);
    }
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
        REQUIRE(extractShelvedAlarms(*userSess) == std::vector<ShelvedAlarm>({{"edfa", "alarms-test:alarm-2-1", "high", "shelf"}}));
    } else {
        REQUIRE(extractAlarms(*userSess) == std::vector<Alarm>{{"edfa", "alarms-test:alarm-2-1", "high"}});
        REQUIRE(dataFromSysrepo(*userSess, alarmListInstances + "[resource='edfa'][alarm-type-id='alarms-test:alarm-2-1'][alarm-type-qualifier='high']"s, sysrepo::Datastore::Operational)["/time-created"] == origTime);
    }

    copyStartupDatastore("ietf-alarms"); // cleanup after last run so we can cleanly uninstall modules
}

