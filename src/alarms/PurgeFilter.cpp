/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#include <algorithm>
#include <chrono>
#include <libyang-cpp/DataNode.hpp>
#include <libyang-cpp/Value.hpp>
#include "AlarmEntry.h"
#include "PurgeFilter.h"
#include "utils/libyang.h"
#include "utils/time.h"

namespace {

template <class T>
T getValue(const libyang::DataNode& node)
{
    return std::get<T>(node.asTerm().value());
}
}
namespace alarms {

PurgeFilter::PurgeFilter(const libyang::DataNode& filterInput)
{
    auto clearanceStatus = utils::childValue(filterInput, "alarm-clearance-status");
    m_filters.emplace_back([clearanceStatus](const AlarmEntry& alarm) {
        if (clearanceStatus == "any") {
            return true;
        } else if (clearanceStatus == "cleared") {
            return alarm.isCleared;
        } else if (clearanceStatus == "not-cleared") {
            return !alarm.isCleared;
        } else {
            throw std::logic_error("purge: Invalid alarm-clearance-status value");
        }
    });

    if (auto severityContainer = filterInput.findPath("severity")) {
        std::function<bool(int32_t)> severityCheck;

        if (auto choice = severityContainer->findPath("above")) {
            auto sev = std::get<libyang::Enum>(choice->asTerm().value()).value;
            severityCheck = [sev](int32_t alarmValue) { return sev < alarmValue; };
        } else if (auto choice = severityContainer->findPath("is")) {
            auto sev = std::get<libyang::Enum>(choice->asTerm().value()).value;
            severityCheck = [sev](int32_t alarmValue) { return sev == alarmValue; };
        } else if (auto choice = severityContainer->findPath("below")) {
            auto sev = std::get<libyang::Enum>(choice->asTerm().value()).value;
            severityCheck = [sev](int32_t alarmValue) { return sev > alarmValue; };
        } else {
            throw std::logic_error("purge: Invalid choice value below severity");
        }

        m_filters.emplace_back([severityCheck](const AlarmEntry& alarm) {
            return severityCheck(alarm.lastSeverity);
        });
    }

    if (auto olderThanContainer = filterInput.findPath("older-than")) {
        auto threshold = std::chrono::system_clock::now();

        if (auto choice = olderThanContainer->findPath("seconds")) {
            threshold = threshold - std::chrono::seconds(getValue<uint16_t>(*choice));
        } else if (auto choice = olderThanContainer->findPath("minutes")) {
            threshold = threshold - std::chrono::minutes(getValue<uint16_t>(*choice));
        } else if (auto choice = olderThanContainer->findPath("hours")) {
            threshold = threshold - std::chrono::hours(getValue<uint16_t>(*choice));
        } else if (auto choice = olderThanContainer->findPath("days")) {
            threshold = threshold - std::chrono::days(getValue<uint16_t>(*choice));
        } else if (auto choice = olderThanContainer->findPath("weeks")) {
            threshold = threshold - 7 * std::chrono::days(getValue<uint16_t>(*choice));
        } else {
            throw std::logic_error("purge: Invalid choice value below older-than");
        }

        m_filters.emplace_back([threshold](const AlarmEntry& alarm) {
            return alarm.lastChanged < threshold;
        });
    }
}

bool PurgeFilter::matches(const AlarmEntry& alarm) const
{
    return std::all_of(m_filters.begin(), m_filters.end(), [&](const Filter& filter) { return filter(alarm); });
}

}
