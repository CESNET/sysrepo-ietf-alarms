#include "trompeloeil_doctest.h"
#include <iostream>
#include <string>
#include <sysrepo-cpp/Connection.hpp>
#include "alarms/Daemon.h"
#include "test_alarm_helpers.h"
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "test_time_interval.h"

using namespace std::string_literals;

TEST_CASE("Basic alarm publishing and updating")
{
    TEST_SYSREPO_INIT_LOGS;
    spdlog::set_level(spdlog::level::info);
    spdlog::get("sysrepo")->set_level(spdlog::level::warn);
    auto mainLog = spdlog::get("main");
    copyStartupDatastore("ietf-alarms");
    auto daemon = std::make_unique<alarms::Daemon>();
    TEST_SYSREPO_CLIENT_INIT(userSess);

    constexpr auto NUM_RESOURCES = 200;
    constexpr auto FAILING_RESOURCES = 200;
    constexpr auto NOOP_PURGES = 200;

    SECTION("inventory: sequential resources") {
        auto start = std::chrono::system_clock::now();
        int i = 0;
        CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-2", "" /* qualifier is useless */, ({"r1", "r2"}), {}, "desc");
        ++i;
        CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-2-1", "something very dynamic", {}, {}, "desc");
        ++i;
        CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-2-2", "" /* qualifier is useless */, {}, ({"critical", "major", "minor"}), "desc");
        ++i;
        for (; i < NUM_RESOURCES; ++i) {
            CLIENT_INTRODUCE_ALARM(userSess, "alarms-test:alarm-1", "" /* qualifier is useless */, {"resource-" + std::to_string(i)}, {"critical"}, "desc");
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
        mainLog->error("Sequentially pushing {} inventory entries: {}ms", NUM_RESOURCES, ms);
    }

    SECTION("inventory: batched") {
        std::vector<std::string> resources;
        for (int i = 0; i < NUM_RESOURCES; ++i) {
            resources.emplace_back("resource-" + std::to_string(i));
        }

        {
            auto start = std::chrono::system_clock::now();
            CLIENT_INTRODUCE_ALARM_VECTOR(userSess, "alarms-test:alarm-1", "" /* qualifier is useless */, resources, std::vector<std::string>{{"critical"}}, "desc");
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            mainLog->error("Pushing {} inventory entries at once: {}ms", NUM_RESOURCES, ms);
        }

        {
            auto start = std::chrono::system_clock::now();
            for (int i = 0; i < FAILING_RESOURCES; ++i) {
                CLIENT_ALARM_RPC(userSess, "alarms-test:alarm-1", "", "resource-" + std::to_string(i), "critical", "xxx");
            }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            mainLog->error("Sending {} alarms: {}ms", FAILING_RESOURCES, ms);
        }

        {
            auto start = std::chrono::system_clock::now();
            for (int i = 0; i < NOOP_PURGES; ++i) {
                CLIENT_PURGE_RPC(userSess, 0, "cleared", ({{"severity/below", "indeterminate"}}));
            }
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            mainLog->error("Issuing {} purge requests which don't purge anything: {}ms", NOOP_PURGES, ms);
        }

        {
            auto start = std::chrono::system_clock::now();
            CLIENT_PURGE_RPC(userSess, FAILING_RESOURCES, "any", {});
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
            mainLog->error("Final purge of everything: {}ms", ms);
        }
    }
}
