/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#pragma once
#include <boost/container_hash/hash.hpp>
#include <string>

namespace libyang {
class DataNode;
}

namespace alarms {

/** @short Alarm type as per https://datatracker.ietf.org/doc/html/rfc8632#section-3.2
 *
 * The alarm type identifies an alarm in the inventory. In other uses (e.g., shelving and `alarm-list`), the alarm type
 * is used together with the resource identification, and the result is represented as an `InstanceKey`.
 *
 */
struct Type {
    std::string id; /**< Static identity, `alarm-type-id` from RFC 8632 */
    std::string qualifier; /**< Dynamic qualifier, `alarm-type-qualifier` from RFC 8632 */

    std::string xpathIndex() const;
    auto operator<=>(const Type& other) const = default;
};

inline std::size_t hash_value(const Type& t)
{
    std::size_t seed = 0;
    boost::hash_combine(seed, t.id);
    boost::hash_combine(seed, t.qualifier);
    return seed;

}

/** @short Identification of an alarm within the `alarm-list` */
struct InstanceKey {
    Type type;
    std::string resource;

    std::string xpathIndex() const;
    static InstanceKey fromNode(const libyang::DataNode& node);
    auto operator<=>(const InstanceKey& other) const = default;
};

inline std::size_t hash_value(const InstanceKey& k)
{
    std::size_t seed = 0;
    boost::hash_combine(seed, k.type);
    boost::hash_combine(seed, k.resource);
    return seed;

}
}
