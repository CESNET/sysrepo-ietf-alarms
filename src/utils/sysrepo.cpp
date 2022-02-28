/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 */

#include "sysrepo.h"
#include "utils/log.h"

extern "C" {
#include <sysrepo.h>
}

extern "C" {
/** @short Propagate sysrepo events to spdlog */
static void spdlog_sr_log_cb(sr_log_level_t level, const char* message)
{
    // Thread safety note: this is, as far as I know, thread safe:
    // - the static initialization itself is OK
    // - all loggers which we instantiate are thread-safe
    // - std::shared_ptr::operator-> is const, and all const members of that class are documented to be thread-safe
    static auto log = spdlog::get("sysrepo");
    assert(log);
    switch (level) {
    case SR_LL_NONE:
    case SR_LL_ERR:
        log->error(message);
        break;
    case SR_LL_WRN:
        log->warn(message);
        break;
    case SR_LL_INF:
        log->info(message);
        break;
    case SR_LL_DBG:
        log->debug(message);
        break;
    }
}
}

namespace alarms::utils {

/** @short Setup sysrepo log forwarding
You must call cla::utils::initLogs prior to this function.
*/
void initLogsSysrepo()
{
    sr_log_set_cb(spdlog_sr_log_cb);
}

/** @brief Checks whether a module is implemented in Sysrepo and throws if not. */
void ensureModuleImplemented(const sysrepo::Session& session, const std::string& module, const std::string& revision)
{
    if (auto mod = session.getContext().getModule(module.c_str(), revision.c_str()); !mod || !mod->implemented()) {
        throw std::runtime_error(module + "@" + revision + " is not implemented in sysrepo.");
    }
}

void valuesToYang(::sysrepo::Session session, const std::map<std::string, std::string>& values, const std::vector<std::string>& removePaths, std::optional<libyang::DataNode>& parent)
{
    auto log = spdlog::get("main");
    auto netconf = session.getContext().getModuleImplemented("ietf-netconf");

    for (const auto& propertyName : removePaths) {
        log->trace("Processing node deletion {}", propertyName);

        if (!parent) {
            parent = session.getContext().newPath(propertyName.c_str(), nullptr, libyang::CreationOptions::Update);
        } else {
            parent->newPath(propertyName.c_str(), nullptr, libyang::CreationOptions::Update);
        }

        auto deletion = parent->findPath(propertyName.c_str());
        if (!deletion) {
            throw std::logic_error{"Cannot find XPath " + propertyName + " for deletion in libyang's new_path() output"};
        }
        deletion->newMeta(*netconf, "operation", "remove");
    }

    for (const auto& [propertyName, value] : values) {
        log->trace("Processing node update {} -> {}", propertyName, value);

        if (!parent) {
            parent = session.getContext().newPath(propertyName.c_str(), value.c_str(), libyang::CreationOptions::Update);
        } else {
            parent->newPath(propertyName.c_str(), value.c_str(), libyang::CreationOptions::Update);
        }
    }
}

}
