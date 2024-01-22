/*
 * Copyright (C) 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Tomáš Pecka <pecka@cesnet.cz>
 *
 */

#pragma once
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
    bool operator==(const Type& other) const = default;
};


/** @short Identification of an alarm within the `alarm-list` */
struct InstanceKey {
    Type type;
    std::string resource;

    std::string alarmPath() const;
    std::string shelvedAlarmPath() const;
    static InstanceKey fromNode(const libyang::DataNode& node);
};
}
