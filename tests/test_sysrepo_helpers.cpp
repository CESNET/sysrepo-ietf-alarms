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

using namespace std::chrono_literals;

/** @short Return a subtree from sysrepo, compacting the XPath */
std::map<std::string, std::string> dataFromSysrepo(const sysrepo::Session session, const std::string& xpath)
{
    std::map<std::string, std::string> res;
    auto data = session.getData((xpath + "/*").c_str());
    REQUIRE(data);
    for (const auto& sibling : data->findXPath(xpath.c_str())) { // Use findXPath here in case the xpath is list without keys.
        for (const auto& node : sibling.childrenDfs()) {
            const auto briefXPath = std::string(node.path()).substr(alarms::utils::endsWith(xpath, ":*") ? xpath.size() - 1 : xpath.size());
            // We ignore the thing that's exactly the xpath we're retrieving to avoid having {"": ""} entries.
            if (briefXPath.empty()) {
                continue;
            }
            res.emplace(briefXPath, node.isTerm() ? node.asTerm().valueStr() : "");
        }
    }
    return res;
}

/** @short Execute an RPC or action, return result, compacting the XPath. The rpcPath and input gets concatenated. */
std::map<std::string, std::string> rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input)
{
    spdlog::get("main")->info("rpcFromSysrepo {}", rpcPath);
    auto inputNode = session.getContext().newPath(rpcPath.c_str(), nullptr);
    for (const auto& [k, v] : input) {
        inputNode.newPath((rpcPath + "/" + k).c_str(), v.c_str());
    }
    auto output = session.sendRPC(inputNode);
    std::map<std::string, std::string> res;
    for (const auto& node : output.childrenDfs()) {
        const auto briefXPath = std::string{node.path()}.substr(rpcPath.size());
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
    auto oldDatastore = session.activeDatastore();
    session.switchDatastore(datastore);
    auto res = dataFromSysrepo(session, xpath);
    session.switchDatastore(oldDatastore);
    return res;
}

/** @short Returns xPaths of list instances from the list specified by path */
std::vector<std::string> listInstancesFromSysrepo(sysrepo::Session session, const std::string& path, sysrepo::Datastore datastore)
{
    auto oldDs = session.activeDatastore();
    session.switchDatastore(datastore);

    auto lists = session.getData(path.c_str());

    session.switchDatastore(oldDs);

    if (!lists) {
        return {};
    }

    std::vector<std::string> res;
    for (const auto& instance : lists->findXPath(path.c_str())) {
        res.emplace_back(instance.path());
    }
    std::sort(res.begin(), res.end());
    return res;
}

void copyStartupDatastore(const std::string& module)
{
    sysrepo::Connection{}.sessionStart().copyConfig(sysrepo::Datastore::Startup, module.c_str(), 1000ms);
}
