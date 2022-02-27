#include "trompeloeil_doctest.h"
#include <chrono>
#include <date/date.h>
#include <date/tz.h>
#include "utils/time.h"

using namespace std::chrono_literals;

TEST_CASE("Time manipulation")
{
    auto palindromicTimeNoTZ = date::local_days{date::February / 22 / 2022} + 22h + 20min + 22s + 20ms;
    auto palindromicTime = date::make_zoned(date::locate_zone("Europe/Prague"), palindromicTimeNoTZ).get_sys_time();

    REQUIRE(alarms::utils::yangTimeFormat(palindromicTime, "Europe/Prague") == "2022-02-22T22:20:22.020000000+01:00");
    REQUIRE(alarms::utils::yangTimeFormat(palindromicTime, "UTC") == "2022-02-22T21:20:22.020000000+00:00");
    REQUIRE(alarms::utils::yangTimeFormat(palindromicTime, "Asia/Tokyo") == "2022-02-23T06:20:22.020000000+09:00");
    REQUIRE(alarms::utils::yangTimeFormat(palindromicTime, "America/New_York") == "2022-02-22T16:20:22.020000000-05:00");
    REQUIRE(alarms::utils::yangTimeFormat(palindromicTime, "Australia/Eucla") == "2022-02-23T06:05:22.020000000+08:45");

    REQUIRE(alarms::utils::fromYangTimeFormat("2022-02-22T22:20:22.020000000+01:00") == palindromicTime);
    REQUIRE(alarms::utils::fromYangTimeFormat("2022-02-22T21:20:22.020000000+00:00") == palindromicTime);
    REQUIRE(alarms::utils::fromYangTimeFormat("2022-02-23T06:20:22.020000000+09:00") == palindromicTime);
    REQUIRE(alarms::utils::fromYangTimeFormat("2022-02-22T16:20:22.020000000-05:00") == palindromicTime);
    REQUIRE(alarms::utils::fromYangTimeFormat("2022-02-23T06:05:22.020000000+08:45") == palindromicTime);
}
