/*
 * Copyright (C) 2016-2018 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once

#include <doctest/doctest.h>
#include <map>
#include <sstream>
#include <trompeloeil.hpp>
#include <variant>
#include "test_time_interval.h"

namespace trompeloeil {
template <>
struct printer<std::variant<std::string, AnyTimeBetween>> {
    static void print(std::ostream& os, const std::variant<std::string, AnyTimeBetween>& prop)
    {
        std::visit([&os](auto&& arg) { os << arg; }, prop);
    }
};

template <>
struct printer<PropsWithTimeTest> {
    static void print(std::ostream& os, const PropsWithTimeTest& props)
    {
        os << "{" << std::endl;
        for (const auto& [key, value] : props) {
            os << "  \"" << key << "\": \"";
            trompeloeil::print(os, value);
            os << "\"," << std::endl;
        }
        os << "}";
    }
};
}


namespace doctest {

template <>
struct StringMaker<std::map<std::string, std::string>> {
    static String convert(const std::map<std::string, std::string>& map)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << key << "\": \"" << value << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <>
struct StringMaker<std::map<std::string, std::variant<std::string, AnyTimeBetween>>> {
    static String convert(const std::map<std::string, std::variant<std::string, AnyTimeBetween>>& props)
    {
        std::ostringstream oss;
        trompeloeil::print(oss, props);
        return oss.str().c_str();
    }
};
template <>
struct StringMaker<std::map<std::string, int64_t>> {
    static String convert(const std::map<std::string, int64_t>& map)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << key << "\": " << value << "," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

}
