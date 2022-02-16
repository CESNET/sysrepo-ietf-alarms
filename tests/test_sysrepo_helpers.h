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
std::map<std::string, std::string> rpcFromSysrepo(sysrepo::Session session, const std::string& rpcPath, std::map<std::string, std::string> input);
void copyStartupDatastore(const std::string& module);

#define TEST_SYSREPO_INIT                \
    auto srConn = sysrepo::Connection{}; \
    auto srSess = srConn.sessionStart();

#define TEST_SYSREPO_INIT_CLIENT             \
    auto clientConn = sysrepo::Connection{}; \
    auto client = clientConn.sessionStart();

#define TEST_SYSREPO_INIT_SP(conn, sess)                 \
    auto conn = std::make_shared<sysrepo::Connection>(); \
    auto sess = std::make_shared<sysrepo::Session>(conn->sessionStart());
