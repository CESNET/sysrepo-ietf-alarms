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
#include "alarms/AlarmEntry.h"
#include "alarms/Key.h"
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
struct StringMaker<std::variant<std::string, AnyTimeBetween>> {
    static String convert(const std::variant<std::string, AnyTimeBetween>& prop)
    {
        std::ostringstream os;
        trompeloeil::print(os, prop);
        return os.str().c_str();
    }
};

template <class Key, class Value>
struct StringMaker<std::map<Key, Value>> {
    static String convert(const std::map<Key, Value>& map)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& [key, value] : map) {
            os << "  \"" << StringMaker<Key>::convert(key) << "\": \"" << StringMaker<Value>::convert(value) << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <class T>
struct StringMaker<std::vector<T>> {
    static String convert(const std::vector<T>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& e : v) {
            os << "  \"" << StringMaker<T>::convert(e) << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <class T>
struct StringMaker<std::set<T>> {
    static String convert(const std::set<T>& v)
    {
        std::ostringstream os;
        os << "{" << std::endl;
        for (const auto& e : v) {
            os << "  \"" << StringMaker<T>::convert(e) << "\"," << std::endl;
        }
        os << "}";
        return os.str().c_str();
    }
};

template <>
struct StringMaker<alarms::InstanceKey> {
    static String convert(const alarms::InstanceKey& obj)
    {
        std::ostringstream oss;
        oss << "{" << obj.resource << ", " << obj.type.id << ", " << obj.type.qualifier << "}";
        return oss.str().c_str();
    }
};

template <>
struct StringMaker<alarms::StatusChange> {
    static String convert(const alarms::StatusChange& obj)
    {
        std::ostringstream oss;
        oss << "{" << obj.time << ", " << obj.perceivedSeverity << ", " << obj.text << "}";
        return oss.str().c_str();
    }
};
}
