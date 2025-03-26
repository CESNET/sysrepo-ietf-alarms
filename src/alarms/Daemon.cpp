#include <boost/algorithm/string/predicate.hpp>
#include <chrono>
#include <libyang-cpp/Time.hpp>
#include <map>
#include <span>
#include <string>
#include "Daemon.h"
#include "Filters.h"
#include "Key.h"
#include "ShelfMatch.h"
#include "utils/benchmark.h"
#include "utils/libyang.h"
#include "utils/log.h"
#include "utils/sysrepo.h"

using namespace std::string_literals;

namespace {
const auto rootPath = "/ietf-alarms:alarms"s;
const auto rpcPrefix = "/sysrepo-ietf-alarms:create-or-update-alarm";
const auto ietfAlarmsModule = "ietf-alarms";
const auto alarmList = "/ietf-alarms:alarms/alarm-list"s;
const auto alarmListInstances = "/ietf-alarms:alarms/alarm-list/alarm";
const auto shelvedAlarmList = "/ietf-alarms:alarms/shelved-alarms"s;
const auto shelvedAlarmListInstances = "/ietf-alarms:alarms/shelved-alarms/shelved-alarm";
const auto purgeRpcPrefix = "/ietf-alarms:alarms/alarm-list/purge-alarms";
const auto purgeShelvedRpcPrefix = "/ietf-alarms:alarms/shelved-alarms/purge-shelved-alarms";
const auto compressAlarmsRpcPrefix = "/ietf-alarms:alarms/alarm-list/compress-alarms";
const auto compressShelvedAlarmsRpcPrefix = "/ietf-alarms:alarms/shelved-alarms/compress-shelved-alarms";
const auto alarmInventoryPrefix = "/ietf-alarms:alarms/alarm-inventory";
const auto controlPrefix = "/ietf-alarms:alarms/control";
const auto ctrlNotifyStatusChanges = controlPrefix + "/notify-status-changes"s;
const auto ctrlNotifySeverityLevel = controlPrefix + "/notify-severity-level"s;
const auto ctrlShelving = controlPrefix + "/alarm-shelving"s;
const auto ctrlMaxAlarmStatusChanges = controlPrefix + "/max-alarm-status-changes"s;
const auto alarmSummaryPrefix = "/ietf-alarms:alarms/summary";

const int32_t ClearedSeverity = 1; // from the RFC

const std::array Severities{
    "_", // just a dummy on index 0
    "cleared",
    "indeterminate",
    "warning",
    "minor",
    "major",
    "critical",
};

/* @brief Checks if the alarm keys match any entry in ietf-alarms:alarms/control/alarm-shelving. If so, return name of the matched shelf */
std::optional<std::string> shouldBeShelved(const libyang::DataNode& shelvingRules, const alarms::InstanceKey& key)
{
    return findMatchingShelf(key, shelvingRules.findXPath(ctrlShelving + "/shelf"));
}

std::string yangTimeFormat(const std::chrono::time_point<std::chrono::system_clock>& timePoint)
{
    return libyang::yangTimeFormat(timePoint, libyang::TimezoneInterpretation::Local);
}
}

namespace alarms {

Daemon::~Daemon()
{
    std::unique_lock lck{m_mtx};
    m_edit = std::nullopt;
}

Daemon::Daemon()
    : m_connection(sysrepo::Connection{})
    , m_session(m_connection.sessionStart(sysrepo::Datastore::Operational))
    , m_log(spdlog::get("main"))
    , m_notifyStatusChanges(NotifyStatusChanges::All)
    , m_inventoryDirty(true)
    , m_alarmListLastChanged(TimePoint::clock::now())
    , m_shelfListLastChanged(TimePoint::clock::now())
{
    utils::ensureModuleImplemented(m_session, ietfAlarmsModule, "2019-09-11", {"alarm-shelving", "alarm-summary", "alarm-history"});
    utils::ensureModuleImplemented(m_session, "sysrepo-ietf-alarms", "2022-02-17");

    {
        WITH_TIME_MEASUREMENT{"initializing stats"};
        m_edit = m_session.getContext().newPath(alarmList, std::nullopt, libyang::CreationOptions::Update);
        updateStatistics();
        applyEdit();
    }

    m_alarmSub = m_session.onRPCAction(rpcPrefix, [&](sysrepo::Session session, auto, auto, const libyang::DataNode input, auto, auto, auto) {
        if (session.getOriginatorName() == "netopeer2"
                || session.getOriginatorName() == "rousette"
                || session.getOriginatorName() == "sysrepo-cli") {
            session.setNetconfError({.type = "application",
                                     .tag = "operation-not-supported",
                                     .appTag = std::nullopt,
                                     .path = std::nullopt,
                                     .message = "Internal RPCs cannot be called.",
                                     .infoElements = {}});
            return sysrepo::ErrorCode::OperationFailed;
        }
        return submitAlarm(session, input);
    });
    m_alarmSub->onRPCAction(purgeRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(purgeRpcPrefix, input, output); });
    m_alarmSub->onRPCAction(purgeShelvedRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return purgeAlarms(purgeShelvedRpcPrefix, input, output); });
    m_alarmSub->onRPCAction(compressAlarmsRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return compressAlarms(compressAlarmsRpcPrefix, input, output); });
    m_alarmSub->onRPCAction(compressShelvedAlarmsRpcPrefix, [&](auto, auto, auto, const libyang::DataNode input, auto, auto, libyang::DataNode output) { return compressAlarms(compressShelvedAlarmsRpcPrefix, input, output); });

    {
        utils::ScopedDatastoreSwitch sw(m_session, sysrepo::Datastore::Running);
        m_alarmSub->onModuleChange(
            ietfAlarmsModule,
            [&](auto session, auto, auto, auto, auto, auto) {
                WITH_TIME_MEASUREMENT{controlPrefix};
                bool needsReshelve = false;
                bool needsStatusChangesShrink = false;
                std::unique_lock lck{m_mtx};
                for (const auto& change : session.getChanges()) {
                    const auto xpath = change.node.path();
                    if (boost::algorithm::starts_with(xpath, ctrlShelving)) {
                        needsReshelve = true;
                        break;
                    }
                    if (xpath == ctrlNotifyStatusChanges) {
                        auto val = std::get<libyang::Enum>(change.node.asTerm().value());
                        if (val.name == "all-state-changes") {
                            m_notifyStatusChanges = NotifyStatusChanges::All;
                            m_log->debug("Will notify about any alarm state changes");
                        } else if (val.name == "raise-and-clear") {
                            m_notifyStatusChanges = NotifyStatusChanges::RaiseAndClear;
                            m_log->debug("Will notify about raised and cleared alarms");
                        } else if (val.name == "severity-level") {
                            m_notifyStatusChanges = NotifyStatusChanges::BySeverity;
                        } else {
                            throw std::runtime_error{"Cannot handle " + ctrlNotifyStatusChanges + " value " + val.name};
                        }
                        continue;
                    }
                    if (xpath == ctrlNotifySeverityLevel) {
                        if (change.operation == sysrepo::ChangeOperation::Deleted) {
                            m_notifySeverityThreshold = std::nullopt;
                        } else {
                            auto value = std::get<libyang::Enum>(change.node.asTerm().value());
                            m_notifySeverityThreshold = value.value;
                            m_log->debug("Will notify about alarms with perceived-severity >= {}", value.name);
                        }
                        continue;
                    }
                    if (xpath == ctrlMaxAlarmStatusChanges) {
                        auto oldVal = m_maxAlarmStatusChanges;
                        auto node = change.node.asTerm();

                        // type of the node is union{uint16, Enum{infinite}}
                        if (std::holds_alternative<uint16_t>(node.value())) {
                            m_maxAlarmStatusChanges = std::get<uint16_t>(node.value());
                        } else {
                            m_maxAlarmStatusChanges = std::nullopt;
                        }
                        m_log->debug("Will limit status changes history to {}", node.valueStr());

                        if (m_maxAlarmStatusChanges && (!oldVal || *m_maxAlarmStatusChanges < *oldVal)) {
                            needsStatusChangesShrink = true;
                        }
                    }
                }
                bool changed = false;
                if (needsReshelve) {
                    changed |= reshelve(session);
                }
                if (needsStatusChangesShrink) {
                    changed |= shrinkStatusChangesLists();
                }
                if (changed) {
                    utils::ScopedDatastoreSwitch sw(m_session, sysrepo::Datastore::Operational);
                    applyEdit();
                }
                return sysrepo::ErrorCode::Ok;
            },
            controlPrefix,
            0,
            sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly);
    }

    m_inventorySub = m_session.onModuleChange(
        ietfAlarmsModule, [&](auto, auto, auto, auto, auto, auto) {
            WITH_TIME_MEASUREMENT{alarmInventoryPrefix};
            {
                std::unique_lock lck{m_mtx};
                m_inventoryDirty = true;
            }
            m_session.sendNotification(m_session.getContext().newPath("/ietf-alarms:alarm-inventory-changed", std::nullopt), sysrepo::Wait::No);
            return sysrepo::ErrorCode::Ok;
        },
        alarmInventoryPrefix,
        0,
        sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly);
}

/** @brief Check whether published alarm is in alarm-inventory container
 *
 * According to RFC 8632 the system MUST publish all possible alarm types in the alarm inventory.
 * This is to ensure that alarm operators know what alarms might appear.
 *
 * See RFC 8632, section 4.2
 *
 * @return optional<string> containing the error message if validation fails
 * */
std::optional<std::string> Daemon::inventoryValidationError(const InstanceKey& key, const int32_t severity)
{
    auto it = m_inventory.find(key.type);
    if (it == m_inventory.end()) {
        return "No alarm inventory entry for " + key.type.xpathIndex();
    }

    if (it->second.resources.size() && !it->second.resources.contains(key.resource)) {
        return "Alarm inventory doesn't allow resource '" + key.resource + "' for " + key.type.xpathIndex();
    }

    if (it->second.severities.size() && severity != ClearedSeverity && !it->second.severities.contains(severity)) {
        return "Alarm inventory doesn't allow severity '"s + Severities[severity] + "' for " + key.type.xpathIndex();
    }

    return std::nullopt;
}

/** @short Propagate changes from an incoming RPC to the cached alarm info
 *
 * The entry that's being modified might not have existed in the cache. In that case, it's a new alarm,
 * and this function is called on a default-constructed AlarmEntry.
 * */
AlarmEntry::WhatChanged AlarmEntry::updateByRpc(
        const bool wasPresent,
        const TimePoint now,
        const libyang::DataNode& input,
        const std::optional<std::string> shelf,
        const NotifyStatusChanges notifyStatusChanges,
        const std::optional<int32_t> notifySeverityThreshold,
        const std::optional<uint16_t> maxAlarmStatusChanges)
{
    const auto severity = std::get<libyang::Enum>(input.findPath("severity").value().asTerm().value()).value;
    const bool isClearedNow = severity == ClearedSeverity;

    // If the update clears and alarm and that alarm was not present before, we don't know
    // what to store into the `isCleared`. This must be handled prior to default-constructing
    // the new entry in m_alarms in Daemon::submitAlarm.
    assert(!isClearedNow || wasPresent);

    WhatChanged res {
        .changed = false,
        .shouldNotify = false,
        .removedStatusChanges = {},
    };

    if (!wasPresent) {
        // Shelving status does not depend on "alarm updates" (which are handled here); whether an alarm is shelved
        // or not shelved is only determined by the /ietf-alarms:alarms/control/alarm-shelving.
        // We don't have to track updates here.
        this->shelf = shelf;

        // this one only gets assigned at the very beginning of an alarm lifetime
        this->created = now;

        // -1 is an invalid value which compares different to anything legal later on
        this->lastSeverity = -1;
    }

    if (wasPresent && this->isCleared != isClearedNow) {
        res.changed = true;
    }

    if (auto text = utils::childValue(input, "alarm-text"); text != this->text) {
        this->text = text;
        res.changed = true;
    }

    if (isClearedNow || notifyStatusChanges == NotifyStatusChanges::All) {
        res.shouldNotify = true;
    } else if (notifyStatusChanges == NotifyStatusChanges::RaiseAndClear) {
        res.shouldNotify = !wasPresent || (isClearedNow != this->isCleared);
    }

    if (severity != this->lastSeverity /* evaluates true also for previously unseen alarm keys */) {
        res.changed = true;
    }

    if (!isClearedNow) {
        if (!wasPresent || (wasPresent && this->isCleared)) {
            this->lastRaised = now;
        }
        if (notifyStatusChanges == NotifyStatusChanges::BySeverity &&
                (severity >= *notifySeverityThreshold || this->lastSeverity >= *notifySeverityThreshold)) {
            res.shouldNotify = true;
        }
        if (severity > ClearedSeverity) {
            this->lastSeverity = severity;
        }
    }
    this->isCleared = severity == ClearedSeverity;

    if (res.changed) {
        this->lastChanged = now;

        this->statusChanges.emplace_back(now, this->lastSeverity, this->text);
        res.removedStatusChanges = shrinkStatusChanges(maxAlarmStatusChanges);
    }

    return res;
}

std::vector<TimePoint> AlarmEntry::shrinkStatusChanges(const std::optional<uint16_t> maxAlarmStatusChanges)
{
    std::vector<TimePoint> res;

    if (maxAlarmStatusChanges && statusChanges.size() > *maxAlarmStatusChanges) {
        size_t toErase = statusChanges.size() - *maxAlarmStatusChanges;

        std::transform(statusChanges.begin(), statusChanges.begin() + toErase, std::back_inserter(res), [](const auto& statusChange) {
            return statusChange.time;
        });

        statusChanges.erase(statusChanges.begin(), statusChanges.begin() + toErase);
    }

    return res;
}

/** @brief Adds new entry to alarm's status-change list */
void updateStatusChangeList(libyang::DataNode& edit, const std::string& alarmNodePath, AlarmEntry& alarm, const std::vector<TimePoint>& removedStatusChanges)
{
    /* ietf-alarms specifies the status-change list as follows:
     * > The entry with latest timestamp in this list MUST correspond to the leafs 'is-cleared', 'perceived-severity', and 'alarm-text' for the alarm.
     * > This list is ordered according to the timestamps of alarm state changes. The first item corresponds to the latest state change.
     *
     * This means that we have to insert new status-change entries at the beginning of the list.
     *
     * Further,
     * > The 'status-change' entries are kept in a circular list per alarm.
     * > When this number is exceeded, the oldest status change entry is automatically removed.
     * > If the value is 'infinite', the status-change entries are accumulated infinitely.
     *
     * This means we must be clearing the oldest entries when the list is too long.
     * Our implementation does not use circular list, but std::vector, so we std::move stuff in the vector, but that is our implementation detail.
     */

    auto firstExistingChange = [&]() -> std::optional<libyang::DataNode> {
        auto alarmNode = edit.findPath(alarmNodePath);
        for (const auto& child : alarmNode->immediateChildren()) {
            if (child.schema().name() == "status-change" && child.schema().module().name() == "ietf-alarms") {
                return child;
            }
        }
        return std::nullopt;
    }();

    auto statusChange = statusChangeXPath(alarmNodePath, alarm.lastChanged);
    auto node = *edit.newPath2(statusChange, std::nullopt).createdNode;
    node.newPath(statusChange + "/perceived-severity", Severities[alarm.isCleared ? ClearedSeverity : alarm.lastSeverity]);
    node.newPath(statusChange + "/alarm-text", alarm.text);

    if (firstExistingChange) {
        // move to the correct position as the first node in that list
        node.insertBefore(*firstExistingChange);
    }

    for (const auto& time: removedStatusChanges) {
        edit.findPath(statusChangeXPath(alarmNodePath, time))->unlink();
    }
}

bool Daemon::shrinkStatusChangesLists()
{
    WITH_TIME_MEASUREMENT{};
    bool changed = false;

    if (!m_maxAlarmStatusChanges) {
        return false;
    }

    m_log->debug("Trimming status changes history because max-alarm-status-changes changed to {}", *m_maxAlarmStatusChanges);

    for (auto& [alarmKey, alarm] : m_alarms) {
        for (const auto& time : alarm.shrinkStatusChanges(m_maxAlarmStatusChanges)) {
            const auto& prefix = alarm.shelf ? shelvedAlarmListInstances : alarmListInstances;
            const auto xpath = statusChangeXPath(prefix + alarmKey.xpathIndex(), time);
            m_edit->findPath(xpath)->unlink();
            changed = true;
        }
    }

    return changed;
}

sysrepo::ErrorCode Daemon::submitAlarm(sysrepo::Session rpcSession, const libyang::DataNode& input)
{
    WITH_TIME_MEASUREMENT{};
    const auto now = TimePoint::clock::now();
    const auto& alarmKey = InstanceKey::fromNode(input);
    const auto severity = std::get<libyang::Enum>(input.findPath("severity").value().asTerm().value()).value;
    const bool isClearedNow = severity == ClearedSeverity;
    m_log->trace("RPC {}: {}", rpcPrefix, *input.printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Shrink));

    std::string keyXPath;
    try {
        keyXPath = alarmKey.xpathIndex();
    } catch (std::logic_error& e) {
        rpcSession.setErrorMessage(e.what());
        return sysrepo::ErrorCode::InvalidArgument;
    }

    std::unique_lock lck{m_mtx};
    if (m_inventoryDirty) {
        WITH_TIME_MEASUREMENT{"submitAlarm/rebuildInventory"};
        const auto alarmRoot = m_session.getData(rootPath);
        assert(alarmRoot);
        rebuildInventory(*alarmRoot);
    }
    if (auto inventoryError = inventoryValidationError(alarmKey, severity)) {
        rpcSession.setNetconfError({.type = "application",
                                    .tag = "data-missing",
                                    .appTag = std::nullopt,
                                    .path = std::nullopt,
                                    .message = (inventoryError.value() + " -- see RFC8632 (sec. 4.1).").c_str(),
                                    .infoElements = {}});
        m_log->warn(inventoryError.value());
        return sysrepo::ErrorCode::OperationFailed;
    }

    if (auto it = m_alarms.find(alarmKey); isClearedNow && ((it == m_alarms.end()) || ((it != m_alarms.end()) && it->second.isCleared))) {
        m_log->trace("No update for already-cleared alarm {}", keyXPath);
        return sysrepo::ErrorCode::Ok;
    }

    auto matchedShelf = shouldBeShelved(*m_shelvingRules, alarmKey);
    auto alarmNodePath = (matchedShelf ? shelvedAlarmListInstances : alarmListInstances) + keyXPath;
    m_edit->newPath(alarmNodePath, std::nullopt, libyang::CreationOptions::Update);
    auto [it, wasInserted] = m_alarms.try_emplace(alarmKey);
    auto res = it->second.updateByRpc(!wasInserted, now, input, matchedShelf, m_notifyStatusChanges, m_notifySeverityThreshold, m_maxAlarmStatusChanges);

    if (res.changed) {
        m_edit->newPath(alarmNodePath + "/is-cleared", it->second.isCleared ? "true" : "false", libyang::CreationOptions::Update);
        m_edit->newPath(alarmNodePath + "/last-raised", yangTimeFormat(it->second.lastRaised), libyang::CreationOptions::Update);
        m_edit->newPath(alarmNodePath + "/last-changed", yangTimeFormat(it->second.lastChanged), libyang::CreationOptions::Update);
        m_edit->newPath(alarmNodePath + "/perceived-severity", Severities[it->second.lastSeverity], libyang::CreationOptions::Update);
        m_edit->newPath(alarmNodePath + "/alarm-text", it->second.text, libyang::CreationOptions::Update);
        if (it->second.shelf) {
            m_edit->newPath(alarmNodePath + "/shelf-name", *it->second.shelf, libyang::CreationOptions::Update);
        } else {
            m_edit->newPath(alarmNodePath + "/time-created", yangTimeFormat(it->second.created), libyang::CreationOptions::Update);
        }

        updateStatusChangeList(*m_edit, alarmNodePath, it->second, res.removedStatusChanges);

        m_log->debug("Updated alarm: {}", *m_edit->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Shrink));
        updateStatistics();
        applyEdit("submitAlarm");

        if (res.shouldNotify) {
            WITH_TIME_MEASUREMENT{"submitAlarm/sendNotification"};
            m_session.sendNotification(createStatusChangeNotification(m_edit->findPath(alarmNodePath).value()), sysrepo::Wait::No);
        }
    }
    return sysrepo::ErrorCode::Ok;
}

libyang::DataNode Daemon::createStatusChangeNotification(const libyang::DataNode& alarmNode)
{
    static const std::string prefix = "/ietf-alarms:alarm-notification";

    auto notification = m_session.getContext().newPath(prefix + "/resource", utils::childValue(alarmNode, "resource"), libyang::CreationOptions::Update);
    notification.newPath(prefix + "/alarm-type-id", utils::childValue(alarmNode, "alarm-type-id"), libyang::CreationOptions::Update);
    notification.newPath(prefix + "/time", utils::childValue(alarmNode, "last-changed"), libyang::CreationOptions::Update);
    notification.newPath(prefix + "/alarm-text", utils::childValue(alarmNode, "alarm-text"), libyang::CreationOptions::Update);

    if (auto qualifier = utils::childValue(alarmNode, "alarm-type-qualifier"); !qualifier.empty()) {
        notification.newPath(prefix + "/alarm-type-qualifier", qualifier, libyang::CreationOptions::Update);
    }

    notification.newPath(prefix + "/perceived-severity", utils::childValue(alarmNode, "is-cleared") == "true" ? "cleared" : utils::childValue(alarmNode, "perceived-severity"), libyang::CreationOptions::Update);

    return notification;
}

sysrepo::ErrorCode Daemon::purgeAlarms(const std::string& rpcPath, const libyang::DataNode& rpcInput, libyang::DataNode output)
{
    WITH_TIME_MEASUREMENT{};
    const auto now = std::chrono::system_clock::now();
    bool doingShelved = rpcPath == purgeShelvedRpcPrefix;
    PurgeFilter filter(rpcInput);
    uint32_t purgedAlarms = 0;

    std::unique_lock lck{m_mtx};

    for (auto it = m_alarms.begin(); it != m_alarms.end(); /* nothing */) {
        const auto& [index, entry] = *it;
        if (doingShelved != !!entry.shelf) {
            // when purging through the "shelved" RPC, only consider shelved list and vice verse
            ++it;
            continue;
        }
        if (!filter.matches(index, entry)) {
            ++it;
            continue;
        }
        ++purgedAlarms;
        m_edit->findPath((doingShelved ? shelvedAlarmListInstances : alarmListInstances) + index.xpathIndex())->unlink();
        it = m_alarms.erase(it);
    }

    if (purgedAlarms) {
        if (doingShelved) {
            m_shelfListLastChanged = now;
        } else {
            m_alarmListLastChanged = now;
        }
        updateStatistics();
        m_log->trace("purgeAlarms: removing entries in sysrepo");
        applyEdit();
    }

    output.newPath(rpcPath + "/purged-alarms", std::to_string(purgedAlarms), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode Daemon::compressAlarms(const std::string& rpcPath, const libyang::DataNode& rpcInput, libyang::DataNode output)
{
    WITH_TIME_MEASUREMENT{};

    bool doingShelved = rpcPath == compressShelvedAlarmsRpcPrefix;
    CompressFilter filter(rpcInput);
    uint32_t compressedAlarmEntries = 0;

    std::unique_lock lck{m_mtx};

    for (auto& [key, alarm] : m_alarms) {
        if (doingShelved == !!alarm.shelf && filter.matches(key, alarm)) {
            auto discardTimestamps = alarm.shrinkStatusChanges(1);

            if (!discardTimestamps.empty()) {
                ++compressedAlarmEntries;
            }

            for (const auto& time : discardTimestamps) {
                const auto& prefix = (doingShelved ? shelvedAlarmListInstances : alarmListInstances);
                const auto xpath = statusChangeXPath(prefix + key.xpathIndex(), time);
                m_edit->findPath(xpath)->unlink();
            }
        }
    }

    if (compressedAlarmEntries) {
        applyEdit();
    }

    output.newPath(rpcPath + "/compressed-alarms", std::to_string(compressedAlarmEntries), libyang::CreationOptions::Output);
    return sysrepo::ErrorCode::Ok;
}

namespace {

/** @brief Copy contents of shared leaves from existing alarm node into edit. */
void createCommonAlarmNodeProps(libyang::DataNode& edit, const libyang::DataNode& alarm, const std::string& prefix)
{
    for (const auto& leafName : {"is-cleared", "last-raised", "last-changed", "perceived-severity", "alarm-text"}) {
        edit.newPath(prefix + "/" + leafName, utils::childValue(alarm, leafName), libyang::CreationOptions::Update);
    }

    for (const auto& statusChange : alarm.findXPath("status-change")) {
        const auto time = utils::childValue(statusChange, "time");
        edit.newPath(prefix + "/status-change[time='" + time + "']/perceived-severity", utils::childValue(statusChange, "perceived-severity"));
        edit.newPath(prefix + "/status-change[time='" + time + "']/alarm-text", utils::childValue(statusChange, "alarm-text"));
    }
}

/** @brief Creates an edit with shelved-alarm list node based on existing alarm node */
void createShelvedAlarmNodeFromExistingNode(libyang::DataNode& edit, const libyang::DataNode& alarm, const InstanceKey& alarmKey, const std::string& shelfName)
{
    const auto key = shelvedAlarmListInstances + alarmKey.xpathIndex();
    edit.newPath(key + "/shelf-name", shelfName, libyang::CreationOptions::Update);
    createCommonAlarmNodeProps(edit, alarm, key);
}

/** @brief Creates an edit with alarm-list node based on existing alarm node */
void createAlarmNodeFromExistingNode(libyang::DataNode& edit, const libyang::DataNode& alarm, const InstanceKey& alarmKey, const std::chrono::time_point<std::chrono::system_clock>& now)
{
    const auto key = alarmListInstances + alarmKey.xpathIndex();
    edit.newPath(key + "/time-created", yangTimeFormat(now), libyang::CreationOptions::Update);
    createCommonAlarmNodeProps(edit, alarm, key);
}
}

bool Daemon::reshelve(sysrepo::Session running)
{
    WITH_TIME_MEASUREMENT{};
    assert(running.activeDatastore() == sysrepo::Datastore::Running);

    auto now = std::chrono::system_clock::now();
    std::vector<std::string> toErase;
    bool change = false;
    m_shelvingRules = running.getData(ctrlShelving);
    assert(m_shelvingRules);

    for (const auto& [alarmKey, alarm] : m_alarms) {
        const auto& shelf = shouldBeShelved(*m_shelvingRules, alarmKey);
        const auto& pathShelved = shelvedAlarmListInstances + alarmKey.xpathIndex();
        const auto& pathUnshelved = alarmListInstances + alarmKey.xpathIndex();
        if (alarm.shelf && !shelf) {
            change = true;
            auto node = *m_edit->findPath(pathShelved);
            m_alarms[alarmKey].shelf = std::nullopt;
            m_alarmListLastChanged = now;
            m_shelfListLastChanged = now;
            createAlarmNodeFromExistingNode(*m_edit, node, alarmKey, now);
            node.unlink();
            m_log->trace("Alarm {} moved from shelf", alarmKey.xpathIndex());
        } else if (!alarm.shelf && shelf) {
            change = true;
            auto node = *m_edit->findPath(pathUnshelved);
            m_alarms[alarmKey].shelf = shelf;
            m_alarmListLastChanged = now;
            m_shelfListLastChanged = now;
            createShelvedAlarmNodeFromExistingNode(*m_edit, node, alarmKey, *shelf);
            node.unlink();
            m_log->trace("Alarm {} shelved ({})", alarmKey.xpathIndex(), *shelf);
        } else if (alarm.shelf && shelf && *alarm.shelf != *shelf) {
            change = true;
            m_alarms[alarmKey].shelf = shelf;
            m_shelfListLastChanged = now;
            m_edit->newPath(pathShelved + "/shelf-name", *shelf, libyang::CreationOptions::Update);
            m_log->trace("Alarm {} moved between shelfs ({} -> {})", alarmKey.xpathIndex(), *alarm.shelf, *shelf);
        }
    }

    if (change) {
        m_log->trace("reshelve: updating stats");
        updateStatistics();
    }

    return change;
}

void Daemon::rebuildInventory(const libyang::DataNode& dataWithInventory)
{
    const auto data = dataWithInventory.findPath(alarmInventoryPrefix);
    m_inventory.clear();
    if (data->child()) {
        for (const auto& entry : data->child()->siblings()) {
            decltype(InventoryData::resources) resources;
            decltype(InventoryData::severities) severities;
            for (const auto& child : entry.immediateChildren()) {
                const auto shortName = child.schema().name();
                if (shortName == "resource") {
                    resources.emplace(child.asTerm().valueStr());
                } else if (shortName == "severity-level") {
                    severities.emplace(std::get<libyang::Enum>(child.asTerm().value()).value);
                }
            }
            m_inventory.emplace(
                Type{
                    .id = utils::childValue(entry, "alarm-type-id"),
                    .qualifier = utils::childValue(entry, "alarm-type-qualifier")},
                InventoryData{
                    .resources = resources,
                    .severities = severities,
                });
        }
    }
    m_inventoryDirty = false;
}

void Daemon::updateStatistics()
{
    struct PerSeveritySummary {
        unsigned total;
        unsigned cleared;
    };
    std::map<int32_t, PerSeveritySummary> summary;
    int32_t totalList{0}, totalShelved{0};

    // make sure that all severities are present *and* initialize counters to zero
    for (unsigned i = 2 /* #0: dummy, #1: cleared, #2: the first real one */; i < Severities.size(); ++i) {
        summary[i] = PerSeveritySummary{
            .total = 0,
            .cleared = 0,
        };
    }

    for (const auto& [key, entry] : m_alarms) {
        if (entry.shelf) {
            // shelved alarms do not affect the alarm-summary
            ++totalShelved;
            if (entry.lastChanged >= m_shelfListLastChanged) {
                m_shelfListLastChanged = entry.lastChanged;
            }
            continue;
        }

        ++totalList;
        ++summary[entry.lastSeverity].total;
        if (entry.isCleared) {
            ++summary[entry.lastSeverity].cleared;
        }
        if (entry.lastChanged >= m_alarmListLastChanged) {
            m_alarmListLastChanged = entry.lastChanged;
        }
    }

    for (const auto& [severity, summ] : summary) {
        const auto prefix = alarmSummaryPrefix + "/alarm-summary[severity='"s + Severities[severity] + "']";
        m_edit->newPath(prefix + "/total", std::to_string(summ.total), libyang::CreationOptions::Update);
        m_edit->newPath(prefix + "/not-cleared", std::to_string(summ.total - summ.cleared), libyang::CreationOptions::Update);
        m_edit->newPath(prefix + "/cleared", std::to_string(summ.cleared), libyang::CreationOptions::Update);
    }

    m_edit->newPath(alarmList + "/number-of-alarms", std::to_string(totalList), libyang::CreationOptions::Update);
    m_edit->newPath(alarmList + "/last-changed", yangTimeFormat(m_alarmListLastChanged), libyang::CreationOptions::Update);
    m_edit->newPath(shelvedAlarmList + "/number-of-shelved-alarms", std::to_string(totalShelved), libyang::CreationOptions::Update);
    m_edit->newPath(shelvedAlarmList + "/shelved-alarms-last-changed", yangTimeFormat(m_shelfListLastChanged), libyang::CreationOptions::Update);
}

void Daemon::applyEdit(const std::optional<std::string>& benchmarkName)
{
    // It can happen that edit is no longer the first sibling, in that case Session::editBatch would ignore the previous siblings
    m_edit = m_edit->firstSibling();

    m_session.editBatch(*m_edit, sysrepo::DefaultOperation::Replace);

    {
        WITH_TIME_MEASUREMENT{benchmarkName.value_or("") + "/applyChanges"};
        m_session.applyChanges();
    }
}

std::string statusChangeXPath(const std::string& alarmNodePath, const TimePoint& time)
{
    return alarmNodePath + "/status-change[time='" + yangTimeFormat(time) + "']";
}
}
