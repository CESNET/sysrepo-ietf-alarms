#include "trompeloeil_doctest.h"
#include <chrono>
#include <date/date.h>
#include "utils/time.h"

using namespace std::chrono_literals;

TEST_CASE("Time manipulation")
{
    static const auto timestamp = 1645564822; // 2022-02-22 22:20:22
    static const std::chrono::time_point<std::chrono::system_clock> palindromicTime(std::chrono::seconds(timestamp) + 20ms); // 2022-02-22 22:20:22.02
    static const std::string palindromicTimeYangString = "2022-02-22T21:20:22.020000000+00:00";

    REQUIRE(alarms::utils::yangTimeFormat(palindromicTime) == palindromicTimeYangString);

    auto loaded = alarms::utils::fromYangTimeFormat(palindromicTimeYangString);
    REQUIRE(palindromicTime == loaded);
}
