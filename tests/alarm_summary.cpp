#include "trompeloeil_doctest.h"
#include <string>
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::string_literals;

namespace {

using summary_t = std::map<std::string, std::map<std::string, unsigned>>;

summary_t getSummary(sysrepo::Session sess)
{
    auto data = dataFromSysrepo(sess, "/ietf-alarms:alarms", sysrepo::Datastore::Operational);
    summary_t res;

    for (const auto& severity : {"indeterminate", "warning", "minor", "major", "critical"}) {
        res[severity]["total"] = std::stoi(data["/summary/alarm-summary[severity='"s + severity + "']/total"]);
        res[severity]["cleared"] = std::stoi(data["/summary/alarm-summary[severity='"s + severity + "']/cleared"]);
        res[severity]["not-cleared"] = std::stoi(data["/summary/alarm-summary[severity='"s + severity + "']/not-cleared"]);
    }

    return res;
}
}

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;

    copyStartupDatastore("ietf-alarms");

    auto daemon = std::make_unique<alarms::Daemon>();

    TEST_SYSREPO_CLIENT_INIT(userSess);

    CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-1", "high", {}, {}, "Alarm 1");
    CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-1", "blabla", {}, {}, "Alarm 1");

    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"major", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"critical", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
            });

    CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "high", "r1", "warning", "");
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
                {"minor", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"major", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"critical", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
            });

    CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "high", "r1", "minor", "");
    CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "high", "r2", "minor", "");
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 2}, {"cleared", 0}, {"not-cleared", 2}}},
                {"major", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"critical", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
            });

    CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "high", "r1", "critical", "");
    CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "high", "r2", "cleared", "");
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 1}, {"cleared", 1}, {"not-cleared", 0}}},
                {"major", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"critical", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
            });

    CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "blabla", "r2", "major", "");
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 1}, {"cleared", 1}, {"not-cleared", 0}}},
                {"major", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
                {"critical", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
            });

    userSess->setItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']/alarm-type[alarm-type-id='alarms-test:alarm-1'][alarm-type-qualifier-match='high']", std::nullopt);
    userSess->applyChanges();
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"major", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
                {"critical", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
            });

    userSess->deleteItem("/ietf-alarms:alarms/control/alarm-shelving/shelf[name='shelf']");
    userSess->applyChanges();
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 1}, {"cleared", 1}, {"not-cleared", 0}}},
                {"major", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
                {"critical", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
            });

    CLIENT_PURGE_RPC(userSess, 1, "cleared", {});
    REQUIRE(getSummary(*userSess) == summary_t{
                {"indeterminate", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"warning", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"minor", {{"total", 0}, {"cleared", 0}, {"not-cleared", 0}}},
                {"major", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
                {"critical", {{"total", 1}, {"cleared", 0}, {"not-cleared", 1}}},
            });
}
