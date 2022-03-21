/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#include <libyang-cpp/Set.hpp>
#include "AlarmKey.h"
#include "ShelfMatch.h"
#include "utils/libyang.h"

namespace alarms {

ShelfMatch::ShelfMatch(const libyang::DataNode& shelfNode)
    : m_shelfNode(shelfNode)
{
    if (m_shelfNode.findPath("resource")) {
        m_criteria.emplace_back([&](const AlarmKey& alarmKey) {
            for (const auto& resource : m_shelfNode.findXPath("resource")) {
                if (std::string(resource.asTerm().valueStr()) == alarmKey.resource) {
                    return true;
                }
            }

            return false;
        });
    }

    if (m_shelfNode.findPath("alarm-type")) {
        m_criteria.emplace_back([&](const AlarmKey& alarmKey) {
            for (const auto& alarmTypeNode : m_shelfNode.findXPath("alarm-type")) {
                // FIXME
                bool type = alarmKey.alarmTypeId == alarms::utils::childValue(alarmTypeNode, "alarm-type-id");
                bool qual = alarmKey.alarmTypeQualifier == alarms::utils::childValue(alarmTypeNode, "alarm-type-qualifier-match");
                if (type && qual) {
                    return true;
                }
            }

            return false;
        });
    }
}

bool ShelfMatch::match(const AlarmKey& alarmKey)
{
    return std::all_of(m_criteria.begin(), m_criteria.end(), [&](const auto& crit) { return crit(alarmKey); });
}

}
