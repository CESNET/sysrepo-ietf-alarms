/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <date/date.h>
#include <iomanip>
#include <sstream>
#include <utils/time.h>

using namespace std::string_literals;

namespace {

/** @brief Format string required by yang:date-and-time. Represents time in UTC TZ. */
const auto formatStr = "%Y-%m-%dT%H:%M:%S-00:00";

}

/** @short Utilitary functions for various needs */
namespace alarms::utils {

/** @brief Converts a time_point to a UTC timezone textual representation required by yang:date-and-time. */
std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint)
{
    return date::format(formatStr, timePoint);
}

/** @brief Converts a UTC timezone textual representation required by yang:date-and-time to std::time_point */
std::chrono::time_point<std::chrono::system_clock> fromYangTimeFormat(const std::string& timeStr)
{
    std::istringstream iss(timeStr);
    std::chrono::time_point<std::chrono::system_clock> timePoint;

    if (!(iss >> date::parse(formatStr, timePoint))) {
        throw std::invalid_argument("Invalid date for format string '"s + formatStr + "'");
    }

    return timePoint;
}

}
