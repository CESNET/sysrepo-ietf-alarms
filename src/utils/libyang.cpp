/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <tomas.pecka@fit.cvut.cz>
 *
 */

#include <libyang-cpp/DataNode.hpp>
#include <stack>
#include "utils/libyang.h"

namespace alarms::utils {
/** @brief Extract text value of a leaf which is a child of the given parent */
std::string childValue(const libyang::DataNode& node, const std::string& leafName)
{
    auto leaf = node.findPath(leafName);

    if (!leaf) {
        throw std::runtime_error("Selected child does not exist");
    }
    if (!leaf->isTerm()) {
        throw std::runtime_error("Selected child is not a leaf");
    }

    return std::string(leaf->asTerm().valueStr());
}

/** @brief Get all libyang identities that are derived from base identity */
std::vector<libyang::Identity> getIdentitiesDerivedFrom(const libyang::Identity& baseIdentity)
{
    struct LibyangIdentityCompare {
        bool operator()(const libyang::Identity& a, const libyang::Identity& b) const
        {
            if (a.module().name() == b.module().name()) {
                return a.name() < b.name();
            }

            return a.module().name() < b.module().name();
        }
    };

    std::stack<libyang::Identity> stack({baseIdentity});
    std::set<libyang::Identity, LibyangIdentityCompare> visited{baseIdentity};

    while (!stack.empty()) {
        auto currentIdentity = stack.top();
        stack.pop();

        for (const auto& derived : currentIdentity.derived()) {
            if (auto ins = visited.insert(derived); ins.second) {
                stack.push(derived);
            }
        }
    }

    return {visited.begin(), visited.end()};
}
}
