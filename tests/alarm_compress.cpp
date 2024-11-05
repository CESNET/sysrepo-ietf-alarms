#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo_types.h>
#include <thread>
#include "alarms/Daemon.h"
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

TEST_CASE("Compress alarms RPC")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    alarms::Daemon daemon;
    TEST_SYSREPO_CLIENT_INIT(cliSess);
    TEST_SYSREPO_CLIENT_INIT(userSess);

    userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/resource[.='wss']", std::nullopt);
    userSess->applyChanges();

    CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-1", "", {}, {}, "Alarm 1");
    CLIENT_INTRODUCE_ALARM(cliSess, "alarms-test:alarm-2", "", {}, {}, "Alarm 2");

    CLIENT_COMPRESS_RPC(userSess, 0, {});

    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "edfa", "major", "A major issue");
    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "wss", "warning", "shelf");
    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "wss", "major", "shelf");

    REQUIRE(listInstancesFromSysrepo(*userSess, alarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/alarm-list/alarm[resource='edfa'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });
    REQUIRE(listInstancesFromSysrepo(*userSess, shelvedAlarmListInstances, sysrepo::Datastore::Operational) == std::vector<std::string>{
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier='']",
                "/ietf-alarms:alarms/shelved-alarms/shelved-alarm[resource='wss'][alarm-type-id='alarms-test:alarm-2'][alarm-type-qualifier='']",
            });

    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "cleared", "A warning");
    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "edfa", "cleared", "A warning");
    CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-2", "", "wss", "cleared", "shelf");
    CLIENT_COMPRESS_RPC(userSess, 2, {});
    CLIENT_COMPRESS_RPC(userSess, 0, {});
    CLIENT_COMPRESS_SHELVED_RPC(userSess, 1, {});

    REQUIRE()

    SECTION("Compress by resource")
    {
        CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
        CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
        CLIENT_ALARM_RPC(cliSess, "alarms-test:alarm-1", "", "edfa", "warning", "A warning");
    }
}
