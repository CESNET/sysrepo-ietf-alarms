/*
 * Copyright (C) 2016-2022 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
 */

#pragma once

#include "trompeloeil_doctest.h"
#include <sysrepo-cpp/Subscription.hpp>
#include "test_log_setup.h"

/** @short Watch for a given YANG notification

When a real-time notification is recieved, the `notified()` method is invoked with stringified values
of all terminals that were passed to the original notification.
*/
struct NotificationWatcher {
    using data_t = std::map<std::string, std::string>;
    using cb_t = std::function<void(const std::optional<libyang::DataNode>&)>;
    NotificationWatcher(sysrepo::Session& session, const std::string& xpath, cb_t cb = cb_t());
    MAKE_MOCK1(notified, void(const data_t&));

private:
    sysrepo::Subscription m_sub;
};
