#include <libyang-cpp/DataNode.hpp>
#include "Key.h"
#include "utils/libyang.h"

using namespace std::string_literals;

namespace {

/** @brief Escapes key with the other type of quotes than found in the string.
 *
 *  @throws std::invalid_argument if both single and double quotes used in the input
 * */
std::string escapeListKey(const std::string& str)
{
    auto singleQuotes = str.find('\'') != std::string::npos;
    auto doubleQuotes = str.find('\"') != std::string::npos;

    if (singleQuotes && doubleQuotes) {
        throw std::invalid_argument("Encountered mixed single and double quotes in XPath; can't properly escape.");
    } else if (singleQuotes) {
        return '\"' + str + '\"';
    } else {
        return '\'' + str + '\'';
    }
}

std::string alarmListKeyPredicates(const alarms::Key& alarmKey)
{
    return "[alarm-type-id='"s + alarmKey.alarmTypeId + "'][alarm-type-qualifier='" + alarmKey.alarmTypeQualifier + "'][resource=" + escapeListKey(alarmKey.resource) + "]";
}

}

namespace alarms {

Key Key::fromNode(const libyang::DataNode& node)
{
    return {
        alarms::utils::childValue(node, "alarm-type-id"),
        alarms::utils::childValue(node, "alarm-type-qualifier"),
        alarms::utils::childValue(node, "resource")};
}

std::string Key::alarmPath() const
{
    return "/ietf-alarms:alarms/alarm-list/alarm" + alarmListKeyPredicates(*this);
}

std::string Key::shelvedAlarmPath() const
{
    return "/ietf-alarms:alarms/shelved-alarms/shelved-alarm" + alarmListKeyPredicates(*this);
}

}
