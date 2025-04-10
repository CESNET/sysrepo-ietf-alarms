/*
 * Copyright (C) 2020, 2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 * Written by Tomáš Pecka <tomas.pecka@cesnet.cz>
 */

#pragma once

#include <libyang-cpp/Context.hpp>
#include <string>
#include <sysrepo-cpp/Session.hpp>
#include <vector>

namespace alarms::utils {

void initLogsSysrepo();
void ensureModuleImplemented(const sysrepo::Session& session, const std::string& module, const std::string& revision, const std::vector<std::string>& features = {});

/** @brief Ensures that session switches to provided datastore and when the object gets destroyed the session switches back to the original datastore. */
class ScopedDatastoreSwitch {
    sysrepo::Session m_session;
    sysrepo::Datastore m_oldDatastore;

public:
    ScopedDatastoreSwitch(sysrepo::Session session, sysrepo::Datastore ds);
    ~ScopedDatastoreSwitch();
    ScopedDatastoreSwitch(const ScopedDatastoreSwitch&) = delete;
    ScopedDatastoreSwitch(ScopedDatastoreSwitch&&) = delete;
    ScopedDatastoreSwitch& operator=(const ScopedDatastoreSwitch&) = delete;
    ScopedDatastoreSwitch& operator=(ScopedDatastoreSwitch&&) = delete;
};

}
