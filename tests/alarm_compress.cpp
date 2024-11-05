#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo_types.h>
#include <thread>
#include "alarms/AlarmEntry.h"
#include "alarms/Daemon.h"
#include "alarms/Key.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::chrono_literals;

bool includesAll(const std::map<std::string, std::string>& haystack, const PropsWithTimeTest& needles)
{
    return std::all_of(needles.begin(), needles.end(), [&haystack](const auto& e) {
        auto it = haystack.find(e.first);
        return it != haystack.end() && it->second == e.second;
    });
}

std::map<alarms::InstanceKey, std::set<alarms::StatusChange>> statusChanges(const sysrepo::Session& session, const std::string& xpath)
{
    std::map<alarms::InstanceKey, std::set<alarms::StatusChange>> res;

    auto data = session.getData(xpath);
    REQUIRE(!!data);

    for (const auto& e : data->findXPath(xpath + "/status-change")) {
        auto keyObj = alarms::InstanceKey::fromNode(*e.parent());
        alarms::StatusChange change{
            .time = libyang::fromYangTimeFormat<typename alarms::TimePoint::clock>(e.findPath("time")->asTerm().valueStr()),
            .perceivedSeverity = std::get<libyang::Enum>(e.findPath("perceived-severity")->asTerm().value()).value,
            .text = e.findPath("alarm-text")->asTerm().valueStr()};
        res[keyObj].insert(change);
    }

    return res;
}

struct StatusChangeAnyTimeBetween {
    AnyTimeBetween time;
    int32_t perceivedSeverity;
    std::string text;

    auto operator<=>(const StatusChangeAnyTimeBetween&) const = default;
};

bool operator==(const alarms::StatusChange& lhs, const StatusChangeAnyTimeBetween& rhs)
{
    return rhs.time.start <= lhs.time && lhs.time <= rhs.time.end && lhs.perceivedSeverity == rhs.perceivedSeverity && lhs.text == rhs.text;
}

bool operator==(const std::set<alarms::StatusChange>& lhs, const std::set<StatusChangeAnyTimeBetween>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

bool operator==(const std::map<alarms::InstanceKey, std::set<alarms::StatusChange>>& lhs, const std::map<alarms::InstanceKey, std::set<StatusChangeAnyTimeBetween>>& rhs)
{
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](const auto& l, const auto& r) {
        return l.first == r.first && l.second == r.second;
    });
}

namespace alarms {
bool operator<(const StatusChange& lhs, const StatusChange& rhs)
{
    return lhs.time < rhs.time;
}
}

namespace doctest {
template <>
struct StringMaker<StatusChangeAnyTimeBetween> {
    static String convert(const StatusChangeAnyTimeBetween& obj)
    {
        std::ostringstream oss;
        oss << "{" << obj.time << ", perceivedSeverity: " << obj.perceivedSeverity << ", text: \"" << obj.text << "\"}";
        return oss.str().c_str();
    }
};
}

enum Severities {
    CLEARED = 1,
    INDETERMINATE = 2,
    WARNING = 3,
    MINOR = 4,
    MAJOR = 5,
    CRITICAL = 6,
};

TEST_CASE("Compress alarms RPC")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    TEST_SYSREPO_CLIENT_INIT(cliSess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
    userSess->applyChanges();
    userSess->switchDatastore(sysrepo::Datastore::Operational);

    CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-1", "", {}, {}, "Alarm 1");
    CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-2", "", {}, {}, "Alarm 2");

    CLIENT_COMPRESS_RPC(userSess, 0, {});

    std::vector<AnyTimeBetween> edfaAlarm1, edfaAlarm2, wssAlarm1, wssAlarm2, fansAlarm1;

    edfaAlarm1.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning"));
    edfaAlarm2.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "edfa", "major", "A major issue"));
    wssAlarm1.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "wss", "warning", "shelf"));
    wssAlarm2.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "wss", "major", "shelf"));

    REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    edfaAlarm1.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "cleared", "A warning"));
    edfaAlarm2.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "edfa", "cleared", "A warning"));
    wssAlarm2.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "wss", "cleared", "shelf"));

    fansAlarm1.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "fans", "warning", "temperature high"));
    fansAlarm1.emplace_back(CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "fans", "critical", "temperature high"));

    SECTION("Compress all")
    {
        CLIENT_COMPRESS_RPC(userSess, 3, {});
        REQUIRE(statusChanges(*userSess, alarmListInstances) == std::map<alarms::InstanceKey, std::set<StatusChangeAnyTimeBetween>>{
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "edfa"}, {StatusChangeAnyTimeBetween{.time = edfaAlarm1[1], .perceivedSeverity = CLEARED, .text = "A warning"}}},
                    {alarms::InstanceKey{{"alarms-test:alarm-2", ""}, "edfa"}, {StatusChangeAnyTimeBetween{.time = edfaAlarm2[1], .perceivedSeverity = CLEARED, .text = "A warning"}}},
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "fans"}, {StatusChangeAnyTimeBetween{.time = fansAlarm1[1], .perceivedSeverity = CRITICAL, .text = "temperature high"}}},
                });
        CLIENT_COMPRESS_RPC(userSess, 0, {});

        CLIENT_COMPRESS_SHELVED_RPC(userSess, 1, {});
        REQUIRE(statusChanges(*userSess, shelvedAlarmListInstances) == std::map<alarms::InstanceKey, std::set<StatusChangeAnyTimeBetween>>{
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "wss"}, {StatusChangeAnyTimeBetween{.time = wssAlarm1[0], .perceivedSeverity = WARNING, .text = "shelf"}}},
                    {alarms::InstanceKey{{"alarms-test:alarm-2", ""}, "wss"}, {StatusChangeAnyTimeBetween{.time = wssAlarm2[1], .perceivedSeverity = CLEARED, .text = "shelf"}}},
                });
        CLIENT_COMPRESS_SHELVED_RPC(userSess, 0, {});

    }

    SECTION("Compress by resource")
    {
        CLIENT_COMPRESS_RPC(userSess, 0, ({{"resource", "wss"}}));
        REQUIRE(statusChanges(*userSess, alarmListInstances) == std::map<alarms::InstanceKey, std::set<StatusChangeAnyTimeBetween>>{
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "edfa"}, {
                        StatusChangeAnyTimeBetween{.time = edfaAlarm1[0], .perceivedSeverity = WARNING, .text = "A warning"},
                        StatusChangeAnyTimeBetween{.time = edfaAlarm1[1], .perceivedSeverity = CLEARED, .text = "A warning"},
                    }},
                    {alarms::InstanceKey{{"alarms-test:alarm-2", ""}, "edfa"}, {
                        StatusChangeAnyTimeBetween{.time = edfaAlarm2[0], .perceivedSeverity = MAJOR, .text = "A major issue"},
                        StatusChangeAnyTimeBetween{.time = edfaAlarm2[1], .perceivedSeverity = CLEARED, .text = "A warning"},
                    }},
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "fans"}, {
                        StatusChangeAnyTimeBetween{.time = fansAlarm1[0], .perceivedSeverity = WARNING, .text = "temperature high"},
                        StatusChangeAnyTimeBetween{.time = fansAlarm1[1], .perceivedSeverity = CRITICAL, .text = "temperature high"},
                    }},
                });

        CLIENT_COMPRESS_RPC(userSess, 2, ({{"resource", "edfa"}}));
        REQUIRE(statusChanges(*userSess, alarmListInstances) == std::map<alarms::InstanceKey, std::set<StatusChangeAnyTimeBetween>>{
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "edfa"}, {StatusChangeAnyTimeBetween{.time = edfaAlarm1[1], .perceivedSeverity = 1, .text = "A warning"}}},
                    {alarms::InstanceKey{{"alarms-test:alarm-2", ""}, "edfa"}, {StatusChangeAnyTimeBetween{.time = edfaAlarm2[1], .perceivedSeverity = 1, .text = "A warning"}}},
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "fans"}, {
                        StatusChangeAnyTimeBetween{.time = fansAlarm1[0], .perceivedSeverity = WARNING, .text = "temperature high"},
                        StatusChangeAnyTimeBetween{.time = fansAlarm1[1], .perceivedSeverity = CRITICAL, .text = "temperature high"},
                    }},
                    });
    }

    SECTION("Compress by multiple criteria")
    {
        CLIENT_COMPRESS_RPC(userSess, 1, ({{"resource", "edfa"}, {"alarm-type-id", "alarms-test:alarm-1"}}));
        REQUIRE(statusChanges(*userSess, alarmListInstances) == std::map<alarms::InstanceKey, std::set<StatusChangeAnyTimeBetween>>{
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "edfa"}, {
                        StatusChangeAnyTimeBetween{.time = edfaAlarm1[1], .perceivedSeverity = CLEARED, .text = "A warning"},
                    }},
                    {alarms::InstanceKey{{"alarms-test:alarm-2", ""}, "edfa"}, {
                        StatusChangeAnyTimeBetween{.time = edfaAlarm2[0], .perceivedSeverity = MAJOR, .text = "A major issue"},
                        StatusChangeAnyTimeBetween{.time = edfaAlarm2[1], .perceivedSeverity = CLEARED, .text = "A warning"},
                    }},
                    {alarms::InstanceKey{{"alarms-test:alarm-1", ""}, "fans"}, {
                        StatusChangeAnyTimeBetween{.time = fansAlarm1[0], .perceivedSeverity = WARNING, .text = "temperature high"},
                        StatusChangeAnyTimeBetween{.time = fansAlarm1[1], .perceivedSeverity = CRITICAL, .text = "temperature high"},
                    }},
                });
    }
}
