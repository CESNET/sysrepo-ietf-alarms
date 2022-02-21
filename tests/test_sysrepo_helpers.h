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

#define TEST_SYSREPO_CLIENT_INIT_SP(conn, sess)          \
    auto conn = std::make_shared<sysrepo::Connection>(); \
    auto sess = std::make_shared<sysrepo::Session>(conn->sessionStart());


#define TEST_SYSREPO_CLIENT_DISCONNECT(conn, sess) \
    sess.reset();                                  \
    conn.reset();

#define TEST_SYSREPO_CLIENT_DISCONNECT_AND_RESTORE_SP(conn, sess) \
    TEST_SYSREPO_CLIENT_DISCONNECT(conn, sess)                    \
    conn = std::make_shared<sysrepo::Connection>();               \
    sess = std::make_shared<sysrepo::Session>(conn->sessionStart());
