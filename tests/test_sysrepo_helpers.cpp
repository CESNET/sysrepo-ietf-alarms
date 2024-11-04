/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "trompeloeil_doctest.h"
#include <map>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Enum.hpp>
#include <sysrepo-cpp/Session.hpp>
#include "test_log_setup.h"
#include "test_sysrepo_helpers.h"
#include "utils/string.h"
#include "utils/sysrepo.h"

using namespace std::chrono_literals;

/** @brief Replaces timestamps in status-change keys with event order number.
 *
 * Currently, we compare sysrepo data with expected data. But here, we have a problem with timestamps in status-change keys.
 * We can't compare them directly, because they are different in every run. So we replace them with event order number.
 *
 * This is a hack, I know, but it's probably the easiest way to solve this problem.
 */
std::map<std::string, std::string> replaceStatusChangeTimestampsWithEventOrder(const std::map<std::string, std::string> data)
{
    std::map<std::string, std::string> res;

    unsigned eventOrder = 0;
    std::optional<std::tuple<std::string, std::string, std::string>> lastKeys;

    static const std::regex keyRegex{R"(\[(.*)='(.*)'\])"};
    static const std::regex timestampRegex{R"(\[time='(.*)'\])"};

    for (const auto& [key, value] : data) {
        if (key.find("]/status-change[") == std::string::npos) {
            res.emplace(key, value);
            continue;
        }

        // parse keys using the regex
        std::smatch match;
        std::regex_search(key, match, keyRegex);
        auto alarmType = match[1].str();
        auto alarmQual = match[2].str();
        auto timestamp = match[3].str();

        if (std::tie(alarmType, alarmQual, timestamp) != lastKeys) {
            lastKeys = std::make_tuple(alarmType, alarmQual, timestamp);
            eventOrder += 1;
        }

        auto newKey = std::regex_replace(key, timestampRegex, "[time='" + std::to_string(eventOrder) + "']");
        res.emplace(newKey, value);
    }

    return res;
}

/** @short Return a subtree from sysrepo, compacting the XPath */
std::map<std::string, std::string> dataFromSysrepo(const sysrepo::Session session, const std::string& xpath)
{
    std::map<std::string, std::string> res;
    auto data = session.getData(xpath + "/*");
    REQUIRE(data);
    for (const auto& sibling : data->findXPath(xpath)) { // Use findXPath here in case the xpath is list without keys.
        for (const auto& node : sibling.childrenDfs()) {
            const auto briefXPath = node.path().substr(alarms::utils::endsWith(xpath, ":*") ? xpath.size() - 1 : xpath.size());
            // We ignore the thing that's exactly the xpath we're retrieving to avoid having {"": ""} entries.
            if (briefXPath.empty()) {
                continue;
            }
            res.emplace(briefXPath, node.isTerm() ? node.asTerm().valueStr() : "");
        }
    }
    return replaceStatusChangeTimestampsWithEventOrder(res);
}

/** @short Execute an RPC or action, return result, compacting the XPath. The rpcPath and input gets concatenated. */
std::map<std::string, std::string> rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input, std::chrono::milliseconds timeout)
{
    spdlog::get("main")->debug("Calling from test: {}", rpcPath);
    auto inputNode = session.getContext().newPath(rpcPath, std::nullopt);
    for (const auto& [k, v] : input) {
        inputNode.newPath(rpcPath + "/" + k, v);
    }
    auto output = session.sendRPC(inputNode, timeout);
    std::map<std::string, std::string> res;
    for (const auto& node : output.childrenDfs()) {
        const auto briefXPath = node.path().substr(rpcPath.size());
        // We ignore the thing that's exactly the xpath we're retrieving to avoid having {"": ""} entries.
        if (briefXPath.empty()) {
            continue;
        }
        res.emplace(briefXPath, node.isTerm() ? node.asTerm().valueStr() : "");
    }
    return res;
}

/** @short Return a subtree from specified sysrepo's datastore, compacting the XPath */
std::map<std::string, std::string> dataFromSysrepo(sysrepo::Session session, const std::string& xpath, sysrepo::Datastore datastore)
{
    alarms::utils::ScopedDatastoreSwitch s(session, datastore);
    return dataFromSysrepo(session, xpath);
}

/** @short Returns xPaths of list instances from the list specified by path */
std::vector<std::string> listInstancesFromSysrepo(sysrepo::Session session, const std::string& path, sysrepo::Datastore datastore)
{
    auto oldDs = session.activeDatastore();
    session.switchDatastore(datastore);

    auto lists = session.getData(path);

    session.switchDatastore(oldDs);

    if (!lists) {
        return {};
    }

    std::vector<std::string> res;
    for (const auto& instance : lists->findXPath(path)) {
        res.emplace_back(instance.path());
    }
    return res;
}

void copyStartupDatastore(const std::string& module)
{
    sysrepo::Connection{}.sessionStart().copyConfig(sysrepo::Datastore::Startup, module, 1000ms);
}
