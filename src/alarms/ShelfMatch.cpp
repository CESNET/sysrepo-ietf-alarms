/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#include <algorithm>
#include <libyang-cpp/Set.hpp>
#include <libyang-cpp/Type.hpp>
#include <libyang-cpp/Utils.hpp>
#include "Key.h"
#include "ShelfMatch.h"
#include "utils/libyang.h"

namespace {

/** @brief Checks whether alarm's alarm-type-id is derived from identity */
bool matchesIdentity(const libyang::Identity& identity, const alarms::Key& alarmKey)
{
    auto derivedIdentities = identity.derivedRecursive();
    return std::any_of(derivedIdentities.begin(), derivedIdentities.end(), [&alarmKey](const auto& d) {
        return libyang::qualifiedName(d) == alarmKey.alarmTypeId;
    });
}

/** @brief Checks whether alarm matches a single shelf's type setting. Both alarm alarm-type-id and alarm-type-qualifier must match.
 *
 * @param node A set of /ietf-alarms:control/alarm-shelving/shelf/alarm-type nodes
 * */
bool matchesType(const libyang::Set<libyang::DataNode>& alarmTypeNodes, const alarms::Key& alarmKey)
{
    return std::any_of(alarmTypeNodes.begin(), alarmTypeNodes.end(), [&alarmKey](const auto& node) {
        return alarmKey.alarmTypeQualifier == alarms::utils::childValue(node, "alarm-type-qualifier-match") && // FIXME regexp matcher for qualifier
            matchesIdentity(std::get<libyang::IdentityRef>(node.findPath("alarm-type-id")->asTerm().value()).schema, alarmKey);
    });
}

/** @brief Checks whether alarm matches a single shelf
 *
 * @param node A single /ietf-alarms:control/alarm-shelving/shelf node
 * */
bool matchesShelf(const libyang::DataNode& node, const alarms::Key& alarmKey)
{
    /* Each entry defines the criteria for shelving alarms.
     * Criteria are ANDed. If no criteria are specified, all alarms will be shelved.
     */

    if (auto resources = node.findXPath("resource"); !resources.empty() && std::none_of(resources.begin(), resources.end(), [&alarmKey](const auto& resourceNode) { return resourceNode.asTerm().valueStr() == alarmKey.resource; })) {
        return false;
    }

    if (auto alarmTypes = node.findXPath("alarm-type"); !alarmTypes.empty() && !matchesType(alarmTypes, alarmKey)) {
        return false;
    }

    return true;
}
}

namespace alarms {

/** @brief Returns name of the matching shelf for an alarm
 *
 * @param shelves Set of /ietf-alarms:control/alarm-shelving/shelf nodes
 * */
std::optional<std::string> findMatchingShelf(const Key& alarmKey, const libyang::Set<libyang::DataNode>& shelves)
{
    if (auto it = std::find_if(shelves.begin(), shelves.end(), [&alarmKey](const auto& node) { return matchesShelf(node, alarmKey); }); it != shelves.end()) {
        return utils::childValue(*it, "name");
    }
    return std::nullopt;
}

}
