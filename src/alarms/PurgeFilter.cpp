/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#include <algorithm>
#include <chrono>
#include <libyang-cpp/Value.hpp>
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
    m_filters.emplace_back([clearanceStatus](const libyang::DataNode& alarmNode) {
        auto isCleared = utils::childValue(alarmNode, "is-cleared");

        if (clearanceStatus == "any") {
            return true;
        } else if (clearanceStatus == "cleared") {
            return isCleared == "true";
        } else if (clearanceStatus == "not-cleared") {
            return isCleared == "false";
        } else {
            throw std::logic_error("Invalid alarm-clearance-status value");
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
            throw std::logic_error("Invalid choice value");
        }

        m_filters.emplace_back([severityCheck](const libyang::DataNode& alarmNode) {
            auto severityNode = alarmNode.findPath("perceived-severity");
            auto enumVal = std::get<libyang::Enum>(severityNode->asTerm().value()).value;
            return severityCheck(enumVal);
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
            throw std::logic_error("Invalid choice value");
        }

        m_filters.emplace_back([threshold](const libyang::DataNode& alarmNode) {
            auto lastChangedNode = alarmNode.findPath("last-changed");
            auto alarmTime = utils::fromYangTimeFormat(std::string(lastChangedNode->asTerm().valueStr()));
            return alarmTime < threshold;
        });
    }
}

bool PurgeFilter::matches(const libyang::DataNode& alarmNode) const
{
    return std::all_of(m_filters.begin(), m_filters.end(), [&](const Filter& filter) { return filter(alarmNode); });
}

}
