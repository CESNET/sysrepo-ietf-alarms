/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 */

#include <libyang-cpp/Set.hpp>
#include <set>
#include <stack>
#include "AlarmKey.h"
#include "ShelfMatch.h"
#include "utils/libyang.h"

#include <spdlog/spdlog.h>

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


struct identityCmp {
    bool operator()(const libyang::Identity& a, const libyang::Identity& b) const
    {
        if (a.module().name() == b.module().name()) {
            return a.name() < b.name();
        }

        return a.module().name() < b.module().name();
    }
};

libyang::Identity getIdentityDerivedFrom(const libyang::Identity& base, const std::string& identityMod, const std::string& identityName)
{
    std::stack<libyang::Identity> stack;
    std::set<libyang::Identity, identityCmp> visited;

    stack.push(base);
    visited.insert(base);

    while (!stack.empty()) {
        auto currentIdentity = stack.top();
        stack.pop();

        if (currentIdentity.module().name() == identityMod && currentIdentity.name() == identityName) {
            return currentIdentity;
        }

        for (const auto& derived : currentIdentity.derived()) {
            if (auto ins = visited.insert(derived); ins.second) {
                stack.push(derived);
            }
        }
    }

    throw std::invalid_argument("not found");
}

std::vector<libyang::Identity> getIdentitiesDerivedFrom(const libyang::Identity& base)
{
    std::stack<libyang::Identity> stack;
    std::set<libyang::Identity, identityCmp> visited;

    stack.push(base);
    visited.insert(base);

    while (!stack.empty()) {
        auto currentIdentity = stack.top();
        stack.pop();

        for (const auto& derived : currentIdentity.derived()) {
            if (auto ins = visited.insert(derived); ins.second) {
                stack.push(derived);
            }
        }
    }

    return std::vector<libyang::Identity>(visited.begin(), visited.end());
}

bool typeMatch(const libyang::Set<libyang::DataNode>& alarmTypeNodes, const alarms::AlarmKey& alarmKey)
{
    for (const auto& alarmTypeNode : alarmTypeNodes) {
        auto typeNode = alarmTypeNode.findPath("alarm-type-id");
        auto valueIdentityRef = std::get<libyang::IdentityRef>(typeNode->asTerm().value());

        auto iref = typeNode->schema().asLeaf().valueType().asIdentityRef();
        auto identity = getIdentityDerivedFrom(iref.bases()[0], valueIdentityRef.module, valueIdentityRef.name);
        spdlog::get("main")->error("Expecting children derived from {}:{}", valueIdentityRef.module, valueIdentityRef.name);

        bool typeMatch = false;
        for (const auto& d : getIdentitiesDerivedFrom(identity)) {
            spdlog::get("main")->error("   ... is derived {}:{}", d.module().name(), d.name());
            if (std::string(d.module().name()) + ":" + std::string(d.name()) == alarmKey.alarmTypeId) {
                typeMatch = true;
                break;
            }
        }

        bool qualifierMatch = alarmKey.alarmTypeQualifier == alarms::utils::childValue(alarmTypeNode, "alarm-type-qualifier-match"); // FIXME regexp

        if (typeMatch && qualifierMatch) {
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
