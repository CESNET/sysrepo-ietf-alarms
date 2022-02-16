/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Miroslav Mareš <mmares@cesnet.cz>
 *
 */

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <regex>
#include <sstream>
#include "utils/string.h"

/** @short Utilitary functions for various needs */
namespace alarms::utils {

/** @short Returns true if str ends with a given suffix */
bool endsWith(const std::string& str, const std::string& suffix)
{
    if (suffix.size() > str.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

}
