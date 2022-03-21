/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#include <algorithm>
#include <libyang-cpp/Set.hpp>
#include <libyang-cpp/Type.hpp>
#include "AlarmKey.h"
#include "ShelfMatch.h"
#include "utils/libyang.h"

namespace {
bool matchesResource(const libyang::Set<libyang::DataNode>& resourceNodes, const alarms::AlarmKey& alarmKey)
{
    return std::any_of(resourceNodes.begin(), resourceNodes.end(), [&alarmKey](const auto& node) {
        return node.asTerm().valueStr() == alarmKey.resource; // FIXME regexp matcher here
    });
}

bool matchesTypeId(const libyang::DataNode& alarmTypeNode, const alarms::AlarmKey& alarmKey)
{
    auto typeIdIdentity = std::get<libyang::IdentityRef>(alarmTypeNode.findPath("alarm-type-id")->asTerm().value()).schema;

    auto derivedIdentities = alarms::utils::getIdentitiesDerivedFrom(typeIdIdentity);
    return std::any_of(derivedIdentities.begin(), derivedIdentities.end(), [&alarmKey](const auto& d) {
        return std::string(d.module().name()) + ":" + std::string(d.name()) == alarmKey.alarmTypeId;
    });
}

bool mathesTypeQualifier(const libyang::DataNode& alarmTypeNode, const alarms::AlarmKey& alarmKey)
{
    return alarmKey.alarmTypeQualifier == alarms::utils::childValue(alarmTypeNode, "alarm-type-qualifier-match"); // FIXME regexp matcher here
}

bool matchesType(const libyang::Set<libyang::DataNode>& alarmTypeNodes, const alarms::AlarmKey& alarmKey)
{
    return std::any_of(alarmTypeNodes.begin(), alarmTypeNodes.end(), [&alarmKey](const auto& node) {
        return matchesTypeId(node, alarmKey) && mathesTypeQualifier(node, alarmKey);
    });
}

bool matchesShelf(const libyang::DataNode& node, const alarms::AlarmKey& alarmKey)
{
    /* Each entry defines the criteria for shelving alarms.
     * Criteria are ANDed. If no criteria are specified, all alarms will be shelved.
     */
    bool match = true;

    if (auto resources = node.findXPath("resource"); !resources.empty()) {
        match &= matchesResource(resources, alarmKey);
    }

    if (auto alarmTypes = node.findXPath("alarm-type"); !alarmTypes.empty()) {
        match &= matchesType(alarmTypes, alarmKey);
    }

    return match;
}

}

namespace alarms {

std::optional<std::string> findMatchingShelf(const AlarmKey& key, const libyang::Set<libyang::DataNode>& shelves)
{
    if (auto it = std::find_if(shelves.begin(), shelves.end(), [&key](const auto& node) { return matchesShelf(node, key); }); it != shelves.end()) {
        return utils::childValue(*it, "name");
    }
    return std::nullopt;
}

}
