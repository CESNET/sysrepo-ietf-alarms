/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#include <libyang-cpp/Set.hpp>
#include <libyang-cpp/Type.hpp>
#include "AlarmKey.h"
#include "ShelfMatch.h"
#include "utils/libyang.h"

namespace {
bool resourceMatch(const libyang::Set<libyang::DataNode>& resourceNodes, const alarms::AlarmKey& alarmKey)
{
    for (const auto& resource : resourceNodes) {
        if (std::string(resource.asTerm().valueStr()) == alarmKey.resource) { // FIXME regexp
            return true;
        }
    }

    return false;
}

libyang::Identity identityLookup(const libyang::Identity& base, const std::string& module, const std::string& name)
{
    auto allDerivedFromBase = alarms::utils::getIdentitiesDerivedFrom(base);

    if (auto it = std::find_if(allDerivedFromBase.begin(), allDerivedFromBase.end(), [&module, &name](const libyang::Identity& identity) { return identity.module().name() == module && identity.name() == name; }); it != allDerivedFromBase.end()) {
        return *it;
    }

    throw std::invalid_argument("No such derived identity exists.");
}

bool typeIdMatch(const libyang::DataNode& alarmTypeNode, const alarms::AlarmKey& alarmKey)
{
    auto typeIdNode = alarmTypeNode.findPath("alarm-type-id");
    auto type = typeIdNode->schema().asLeaf().valueType().asIdentityRef().bases()[0]; // bases()[0] should be safe because we know that there is only one base (typedef alarm-type-id { type identityref { base alarm-type-id; } })

    auto value = std::get<libyang::IdentityRef>(typeIdNode->asTerm().value());
    auto valueAsIdentity = identityLookup(type, value.module, value.name);

    for (const auto& d : alarms::utils::getIdentitiesDerivedFrom(valueAsIdentity)) {
        if (std::string(d.module().name()) + ":" + std::string(d.name()) == alarmKey.alarmTypeId) {
            return true;
        }
    }

    return false;
}

bool typeQualifierMatch(const libyang::DataNode& alarmTypeNode, const alarms::AlarmKey& alarmKey)
{
    return alarmKey.alarmTypeQualifier == alarms::utils::childValue(alarmTypeNode, "alarm-type-qualifier-match"); // FIXME regexp
}

bool typeMatch(const libyang::Set<libyang::DataNode>& alarmTypeNodes, const alarms::AlarmKey& alarmKey)
{
    // libyang::SetIterator does not meet the requirements of LegacyIterator so std::any_of can't be used
    for (const auto& alarmTypeListNode : alarmTypeNodes) {
        if (typeIdMatch(alarmTypeListNode, alarmKey) && typeQualifierMatch(alarmTypeListNode, alarmKey)) {
            return true;
        }
    }

    return false;
}
}

namespace alarms {

bool shelfMatch(const libyang::DataNode& shelfNode, const AlarmKey& key)
{
    /* Each entry defines the criteria for shelving alarms.
     * Criteria are ANDed. If no criteria are specified, all alarms will be shelved.
     */
    bool match = true;

    if (auto resources = shelfNode.findXPath("resource"); !resources.empty()) {
        match &= resourceMatch(resources, key);
    }

    if (auto alarmTypes = shelfNode.findXPath("alarm-type"); !alarmTypes.empty()) {
        match &= typeMatch(alarmTypes, key);
    }

    return match;
}
}
