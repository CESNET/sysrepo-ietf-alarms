/*
 * Copyright (C) 2020 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <chrono>
#include <date/date.h>
#include <date/tz.h>
#include <iomanip>
#include <sstream>
#include <utils/time.h>

using namespace std::string_literals;

namespace {

/** @brief Format string for timezone-aware datetimes compatible with yang:date-and-time string representation */
const auto formatStr = "%Y-%m-%dT%H:%M:%S%Ez";

std::string yangTimeFormatTZ(const std::chrono::time_point<std::chrono::system_clock>& timePoint, const date::time_zone* tz)
{
    return date::format(formatStr, date::make_zoned(tz, timePoint));
}

}

/** @short Utilitary functions for various needs */
namespace alarms::utils {

/** @brief Converts a time_point<system_clock> to a textual representation yang:date-and-time with provided timezone.
 *
 *  @throws std::runtime_error when tz not found in timezone database
 * */
std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint, const std::string& tz)
{
    return yangTimeFormatTZ(timePoint, date::locate_zone(tz));
}

/** @brief Converts a time_point<system_clock> to a textual representation yang:date-and-time with local timezone.
 *
 *  @throws std::runtime_error when local timezone could not be retrieved
 * */
std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint)
{
    return yangTimeFormatTZ(timePoint, date::current_zone());
}

/** @brief Converts a textual representation yang:date-and-time to std::time_point<std::system_clock> */
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
