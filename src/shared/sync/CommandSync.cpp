// shared_cockpit - sharing the commands of the cockpit (step 2, stage 1)

#include "shared/sync/CommandSync.h"

#include "fmt/format.h"

#include "shared/log/Log.h"

namespace sc::sync {

namespace {

constexpr auto kSummaryEvery = std::chrono::seconds(60);

} // namespace

CommandSync::CommandSync(analysis::Session& analysis, net::SessionHub& hub) : mAnalysis(analysis), mHub(hub) {}

CommandSync::~CommandSync()
{
    Stop_cai("the plugin stopped");
}

// X-Plane's handler: forwards to the object.
int CommandSync::Handler_cai(XPLMCommandRef, XPLMCommandPhase phase, void* refcon)
{
    const auto* ref = static_cast<const Refcon*>(refcon);
    return ref && ref->self ? ref->self->OnCommand_cai(ref->index, phase) : 1;
}

// A phase of a shared command, pressed here or run by Apply_cai.
int CommandSync::OnCommand_cai(std::size_t index, XPLMCommandPhase phase)
{
    if (mApplying || index >= mEntries.size())
        return 1;                                       // someone else's command, run by us: let it through
    net::NetSession& session = mHub.Session_cai();
    if (!session.Active_cai() || mHub.Position_cai().hold)
        return 1;                                       // not in a session / the aircraft is reloading: local only
    Entry& entry = mEntries[index];
    net::SyncStats& stats = mHub.Sync_cai();
    switch (phase) {
        case xplm_CommandBegin: {
            std::string why;
            if (!session.SendCommand_cai(static_cast<std::uint16_t>(index), net::CommandPhase::Begin, why)) {
                // An observer: nothing happens here either.
                entry.blockedHeld = true;
                ++stats.blocked;
                LOG_DEBUG("command {} blocked: {}", entry.name, why);
                mHub.NoteMerged_cai(net::SessionEvent::Kind::Declined, "Observer: the controls are locked", "blocked");
                return 0;
            }
            entry.localHeld = true;
            ++stats.sent;
            Count_cai();
            stats.last = Describe_cai("You", entry);
            stats.lastAt = std::chrono::steady_clock::now();
            return 1;
        }
        case xplm_CommandContinue:
            return entry.blockedHeld ? 0 : 1;
        case xplm_CommandEnd: {
            if (entry.blockedHeld) {
                entry.blockedHeld = false;
                return 0;
            }
            if (entry.localHeld) {
                entry.localHeld = false;
                std::string why;
                session.SendCommand_cai(static_cast<std::uint16_t>(index), net::CommandPhase::End, why);
            }
            return 1;
        }
        default:
            return 1;
    }
}

// Builds the list and registers the handlers.
void CommandSync::Start_cai(const analysis::AnalysisData& data)
{
    net::SyncStats& stats = mHub.Sync_cai();
    stats.missing.clear();
    stats.sent = stats.applied = stats.rejected = stats.blocked = 0;
    stats.last.clear();
    mEntries.clear();
    mRefcons.clear();
    for (const analysis::SharedCommand& shared : analysis::SharedCommands_cai(data)) {
        Entry entry;
        entry.name = shared.name;
        entry.label = shared.label;
        entry.ref = XPLMFindCommand(shared.name.c_str());
        if (!entry.ref)
            stats.missing.push_back(shared.name);
        mEntries.push_back(std::move(entry));
    }
    for (std::size_t i = 0; i < mEntries.size(); ++i) {
        if (!mEntries[i].ref)
            continue;
        mRefcons.push_back(std::make_unique<Refcon>(Refcon{this, i}));
        XPLMRegisterCommandHandler(mEntries[i].ref, Handler_cai, 1, mRefcons.back().get());
    }
    auto lmbda_namer = [this](std::uint16_t index) {
        return index < mEntries.size() ? fmt::format("{} ({})", mEntries[index].label, mEntries[index].name) : std::string();
    };
    mHub.Session_cai().SetCommandNamer_cai(lmbda_namer);
    stats.running = true;
    stats.commands = mEntries.size();
    stats.commandsOk = mEntries.size() - stats.missing.size();
    mRunning = true;
    mNextSummary = std::chrono::steady_clock::now() + kSummaryEvery;
    LOG_INFO("sharing commands: {}, {} found in this X-Plane, {} missing", stats.commands, stats.commandsOk,
             stats.missing.size());
    for (const std::string& name : stats.missing)
        LOG_INFO("  not in this X-Plane (skipped): {}", name);
}

// Unregisters everything and ends the commands held for others.
void CommandSync::Stop_cai(const char* why)
{
    if (!mRunning)
        return;
    for (Entry& entry : mEntries) {
        if (entry.remoteHeld && entry.ref) {
            mApplying = true;
            XPLMCommandEnd(entry.ref);
            mApplying = false;
            LOG_INFO("command {} was held for someone else: ended", entry.name);
        }
        entry.remoteHeld = false;
    }
    std::size_t refcon = 0;
    for (std::size_t i = 0; i < mEntries.size(); ++i)
        if (mEntries[i].ref && refcon < mRefcons.size())
            XPLMUnregisterCommandHandler(mEntries[i].ref, Handler_cai, 1, mRefcons[refcon++].get());
    const net::SyncStats& stats = mHub.Sync_cai();
    LOG_INFO("sharing stopped ({}): {} sent, {} run for others, {} not shared, {} blocked (commands and values)", why, stats.sent, stats.applied,
             stats.rejected, stats.blocked);
    mHub.Session_cai().SetCommandNamer_cai(nullptr);
    mEntries.clear();
    mRefcons.clear();
    mHub.Sync_cai().running = false;
    mRunning = false;
}

// Runs one command of someone else.
void CommandSync::Apply_cai(const net::RemoteCommand& command)
{
    if (command.index >= mEntries.size()) {
        LOG_WARN("command #{} is not in this session's list ({} commands): ignored", command.index, mEntries.size());
        return;
    }
    Entry& entry = mEntries[command.index];
    if (!entry.ref) {
        LOG_DEBUG("command {} is not in this X-Plane: skipped", entry.name);
        return;
    }
    const net::NetSession& session = mHub.Session_cai();
    if (command.phase == net::CommandPhase::Begin) {
        if (entry.remoteHeld) {                               // a new Begin without End: end the old one first
            mApplying = true;
            XPLMCommandEnd(entry.ref);
            mApplying = false;
        }
        mApplying = true;
        XPLMCommandBegin(entry.ref);
        mApplying = false;
        entry.remoteHeld = true;
        entry.remoteAuthor = command.author;
        net::SyncStats& stats = mHub.Sync_cai();
        ++stats.applied;
        Count_cai();
        const std::string who = session.NameOf_cai(command.author);
        stats.last = Describe_cai(who, entry);
        stats.lastAt = std::chrono::steady_clock::now();
        mHub.NoteMerged_cai(net::SessionEvent::Kind::Notice, stats.last, fmt::format("cmd {} {}", command.author, command.index));
        LOG_DEBUG("run for '{}': {} Begin", who, entry.name);
    } else if (entry.remoteHeld) {
        mApplying = true;
        XPLMCommandEnd(entry.ref);
        mApplying = false;
        entry.remoteHeld = false;
        LOG_DEBUG("run for '{}': {} End", session.NameOf_cai(command.author), entry.name);
    }
}

// "Bob: LANDING LIGHTS ON", plus the command name when the log is at Debug.
std::string CommandSync::Describe_cai(const std::string& who, const Entry& entry) const
{
    if (log::IsEnabled_cai(log::Level::Debug))
        return fmt::format("{}: {} ({})", who, entry.label, entry.name);
    return fmt::format("{}: {}", who, entry.label);
}

// Counts a change for the per-minute figure.
void CommandSync::Count_cai()
{
    mHub.CountChange_cai();
}

// From the flight loop.
void CommandSync::Tick_cai()
{
    net::NetSession& session = mHub.Session_cai();
    const bool active = session.Active_cai();
    if (!active) {
        if (mRunning)
            Stop_cai("the session ended");
        session.TakeCommands_cai();
        return;
    }
    if (!mRunning) {
        // The session's analysis: the host's (a joiner with another one), or our own.
        if (session.HostProfile_cai())
            Start_cai(*session.HostProfile_cai());
        else if (mAnalysis.Data_cai())
            Start_cai(*mAnalysis.Data_cai());
        else
            return;
    }
    for (const net::RemoteCommand& command : session.TakeCommands_cai())
        Apply_cai(command);
    net::SyncStats& stats = mHub.Sync_cai();
    for (const net::Rejection& rejection : session.TakeRejections_cai()) {
        ++stats.rejected;
        const std::string what = rejection.index < mEntries.size() ? mEntries[rejection.index].label : std::string("?");
        mHub.Note_cai(net::SessionEvent::Kind::Declined, fmt::format("Your {} was not shared: {}", what, rejection.text));
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= mNextSummary) {
        mNextSummary = now + kSummaryEvery;
        if (stats.perMinute > 0)                // only after a minute with changes
            LOG_INFO("sharing: {} sent, {} run for others, {} not shared, {} blocked, {} in the last minute", stats.sent,
                 stats.applied, stats.rejected, stats.blocked, stats.perMinute);
    }
}

} // namespace sc::sync
