#include <libyang-cpp/DataNode.hpp>
#include "Key.h"
#include "utils/libyang.h"

namespace alarms {

Key getKey(const libyang::DataNode& node)
{
    return {
        alarms::utils::childValue(node, "alarm-type-id"),
        alarms::utils::childValue(node, "alarm-type-qualifier"),
        alarms::utils::childValue(node, "resource")};
}

}
