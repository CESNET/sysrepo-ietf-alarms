#include <docopt.h>
#include <memory>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/spdlog.h>
#include "SYSREPO_IETF_ALARMS_VERSION.h"
#include "alarms/Daemon.h"
#include "utils/exceptions.h"
#include "utils/journal.h"
#include "utils/log-init.h"
#include "utils/waitUntilSignalled.h"

spdlog::level::level_enum parseLogLevel(const std::string& name, const docopt::value& option)
{
    long x;
    try {
        x = option.asLong();
    } catch (std::invalid_argument&) {
        throw std::runtime_error(name + " log level: expecting integer");
    }
    static_assert(spdlog::level::trace < spdlog::level::off, "spdlog::level levels have changed");
    static_assert(spdlog::level::off == 6, "spdlog::level levels have changed");
    if (x < 0 || x > 5)
        throw std::runtime_error(name + " log level invalid or out-of-range");

    return static_cast<spdlog::level::level_enum>(5 - x);
}

static const char usage[] =
    R"(Monitor system health status.

Usage:
  sysrepo-ietf-alarmsd
    [--log-level=<Level>]
    [--sysrepo-log-level=<Level>]
  sysrepo-ietf-alarmsd (-h | --help)
  sysrepo-ietf-alarmsd --version

Options:
  -h --help                  Show this screen.
  --version                  Show version.
  --log-level=<N>            Log level for alarms [default: 3]
  --sysrepo-log-level=<N>    Log level for the sysrepo library [default: 2]
                             (0 -> critical, 1 -> error, 2 -> warning, 3 -> info,
                             4 -> debug, 5 -> trace)
)";

int main(int argc, char* argv[])
{
    std::shared_ptr<spdlog::sinks::sink> loggingSink;
    if (alarms::utils::isJournaldActive()) {
        loggingSink = alarms::utils::create_journald_sink();
    } else {
        loggingSink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
    }

    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true, "sysrepo-ietf-alarmsd " SYSREPO_IETF_ALARMS_VERSION, true);

    alarms::utils::initLogs(loggingSink);
    spdlog::get("main")->set_level(spdlog::level::info);
    spdlog::get("sysrepo")->set_level(spdlog::level::warn);

    try {
        spdlog::get("main")->set_level(parseLogLevel("Main logger", args["--log-level"]));
        spdlog::get("sysrepo")->set_level(parseLogLevel("Sysrepo logger", args["--sysrepo-log-level"]));

        auto daemon = std::make_unique<alarms::Daemon>();
        spdlog::get("main")->info("Alarms daemon initialized");

        alarms::utils::waitUntilSignaled();

        return 0;
    } catch (std::exception& e) {
        alarms::utils::fatalException(spdlog::get("main"), e, "main");
    }
}
