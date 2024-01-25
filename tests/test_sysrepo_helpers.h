/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 *
 */

#include "trompeloeil_doctest.h"
#include <map>
#include <sysrepo-cpp/Session.hpp>
#include "test_log_setup.h"
#include "utils/string.h"

std::map<std::string, std::string> dataFromSysrepo(const sysrepo::Session session, const std::string& xpath);
std::map<std::string, std::string> dataFromSysrepo(sysrepo::Session session, const std::string& xpath, sysrepo::Datastore datastore);
std::map<std::string, std::string> rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input, std::chrono::milliseconds timeout = std::chrono::milliseconds{0});
std::vector<std::string> listInstancesFromSysrepo(sysrepo::Session session, const std::string& path, sysrepo::Datastore datastore);
void copyStartupDatastore(const std::string& module);

#define TEST_INIT_SESSION std::make_unique<sysrepo::Session>(sysrepo::Connection{}.sessionStart())

#define TEST_SYSREPO_CLIENT_INIT(SESSION_NAME) \
    auto SESSION_NAME = TEST_INIT_SESSION;

#define TEST_SYSREPO_CLIENT_DISCONNECT(SESSION_NAME) \
    SESSION_NAME.reset();

#define TEST_SYSREPO_CLIENT_DISCONNECT_AND_RESTORE(SESSION_NAME) \
    TEST_SYSREPO_CLIENT_DISCONNECT(SESSION_NAME) \
    SESSION_NAME = TEST_INIT_SESSION;
