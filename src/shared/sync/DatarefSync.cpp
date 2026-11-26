// shared_cockpit - sharing the datarefs of the cockpit (step 2, stage 2)

#include "shared/sync/DatarefSync.h"

#include <algorithm>
#include <cmath>

#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/sync/FlightInputs.h"

namespace sc::sync {

namespace {

using Clock = std::chrono::steady_clock;

// "float|double" etc. for the log.
std::string TypesText_cai(XPLMDataTypeID types)
{
    std::string text;
    auto lmbda_add = [&text](const char* part) { text += (text.empty() ? "" : "|") + std::string(part); };
    if (types & xplmType_Int) lmbda_add("int");
    if (types & xplmType_Float) lmbda_add("float");
    if (types & xplmType_Double) lmbda_add("double");
    if (types & xplmType_IntArray) lmbda_add("int[]");
    if (types & xplmType_FloatArray) lmbda_add("float[]");
    if (types & xplmType_Data) lmbda_add("data");
    return text.empty() ? std::string("unknown") : text;
}

} // namespace

DatarefSync::DatarefSync(analysis::Session& analysis, net::SessionHub& hub, const settings::Settings& settings)
    : mAnalysis(analysis), mHub(hub), mSettings(settings)
{
}

DatarefSync::~DatarefSync()
{
    Stop_cai("the plugin stopped");
}

// Builds the list and reads the starting values.
void DatarefSync::Start_cai(const analysis::AnalysisData& data)
{
    net::SyncStats& stats = mHub.Sync_cai();
    stats.readOnly.clear();
    mEntries.clear();
    std::size_t missing = 0;
    std::size_t flown = 0;
    for (const analysis::SharedDataref& shared : analysis::SharedDatarefs_cai(data)) {
        if (FlightInputs::OwnsDataref_cai(shared.name)) {
            // Yoke, rudder, brakes, throttle / prop / mixture: only from the PIC's hands (FlightSync).
            LOG_DEBUG("  {}: shared by the flight inputs (from the PIC), not as a value", shared.ref);
            ++flown;
            continue;
        }
        Entry entry;
        entry.name = shared.name;
        entry.element = shared.element;
        entry.label = shared.label;
        entry.ref = XPLMFindDataRef(shared.name.c_str());
        if (!entry.ref) {
            stats.missing.push_back(shared.ref);
            ++missing;
        } else {
            const XPLMDataTypeID types = XPLMGetDataRefTypes(entry.ref);
            if (shared.element >= 0)
                entry.kind = (types & xplmType_FloatArray) ? Kind::FloatArray : (types & xplmType_IntArray) ? Kind::IntArray : Kind::None;
            else
                entry.kind = (types & xplmType_Float) ? Kind::Float : (types & xplmType_Double) ? Kind::Double
                           : (types & xplmType_Int) ? Kind::Int : Kind::None;
            if (entry.kind == Kind::None) {
                LOG_INFO("  {}: type {} cannot be shared (skipped)", shared.ref, TypesText_cai(types));
                stats.readOnly.push_back(shared.ref + " (" + TypesText_cai(types) + ")");
            } else if (!XPLMCanWriteDataRef(entry.ref)) {
                stats.readOnly.push_back(shared.ref);
            } else {
                entry.usable = true;
                entry.last = Read_cai(entry);
            }
        }
        mEntries.push_back(std::move(entry));
    }
    stats.datarefs = mEntries.size();
    auto lmbda_usable = [](const Entry& e) { return e.usable; };
    stats.datarefsOk = static_cast<std::size_t>(std::count_if(mEntries.begin(), mEntries.end(), lmbda_usable));
    mRunning = true;
    stats.running = true;
    LOG_INFO("sharing datarefs: {}, {} usable, {} missing, {} read-only / other type; {} updates a second while moving{}",
             stats.datarefs, stats.datarefsOk, missing, stats.readOnly.size(), mHub.Session_cai().Rules_cai().rate,
             flown ? fmt::format("; {} flight controls / levers come from the PIC's hands instead", flown) : std::string());
    for (const std::string& name : stats.readOnly)
        LOG_INFO("  not writable here (skipped): {}", name);
}

// Stops.
void DatarefSync::Stop_cai(const char* why)
{
    if (!mRunning)
        return;
    LOG_INFO("sharing datarefs stopped ({})", why);
    mEntries.clear();
    mRunning = false;
}

// Reads one entry as a double.
double DatarefSync::Read_cai(const Entry& entry) const
{
    switch (entry.kind) {
        case Kind::Int:    return XPLMGetDatai(entry.ref);
        case Kind::Float:  return XPLMGetDataf(entry.ref);
        case Kind::Double: return XPLMGetDatad(entry.ref);
        case Kind::IntArray: {
            int value = 0;
            XPLMGetDatavi(entry.ref, &value, entry.element, 1);
            return value;
        }
        case Kind::FloatArray: {
            float value = 0.0f;
            XPLMGetDatavf(entry.ref, &value, entry.element, 1);
            return value;
        }
        case Kind::None: break;
    }
    return 0.0;
}

// Writes one entry.
void DatarefSync::Write_cai(const Entry& entry, double value)
{
    switch (entry.kind) {
        case Kind::Int:    XPLMSetDatai(entry.ref, static_cast<int>(std::lround(value))); break;
        case Kind::Float:  XPLMSetDataf(entry.ref, static_cast<float>(value)); break;
        case Kind::Double: XPLMSetDatad(entry.ref, value); break;
        case Kind::IntArray: {
            int v = static_cast<int>(std::lround(value));
            XPLMSetDatavi(entry.ref, &v, entry.element, 1);
            break;
        }
        case Kind::FloatArray: {
            float v = static_cast<float>(value);
            XPLMSetDatavf(entry.ref, &v, entry.element, 1);
            break;
        }
        case Kind::None: break;
    }
}

// Different values (ints exactly, floats with a small tolerance).
bool DatarefSync::Differs_cai(const Entry& entry, double a, double b)
{
    if (entry.kind == Kind::Int || entry.kind == Kind::IntArray)
        return std::lround(a) != std::lround(b);
    return std::fabs(a - b) > 1e-5 * std::max(1.0, std::fabs(a));
}

// "1" / "0.75".
std::string DatarefSync::ValueText_cai(const Entry& entry, double value)
{
    if (entry.kind == Kind::Int || entry.kind == Kind::IntArray)
        return fmt::format("{}", std::lround(value));
    return fmt::format("{:.3g}", value);
}

// "Bob: FUEL PUMP ON [1] = 1".
std::string DatarefSync::Describe_cai(const std::string& who, const Entry& entry, double value) const
{
    if (log::IsEnabled_cai(log::Level::Debug))
        return fmt::format("{}: {} = {} ({}{})", who, entry.label, ValueText_cai(entry, value), entry.name,
                           entry.element >= 0 ? fmt::format("[{}]", entry.element) : std::string());
    return fmt::format("{}: {} = {}", who, entry.label, ValueText_cai(entry, value));
}

// Writes values from the host.
void DatarefSync::Apply_cai(const net::RemoteValues& values)
{
    const net::NetSession& session = mHub.Session_cai();
    net::SyncStats& stats = mHub.Sync_cai();
    std::size_t written = 0;
    for (const net::DataValue& v : values.values) {
        if (v.index >= mEntries.size() || !mEntries[v.index].usable)
            continue;
        Entry& entry = mEntries[v.index];
        Write_cai(entry, v.value);
        entry.last = Read_cai(entry);         // as X-Plane keeps it (an int, a clamped value)
        entry.pending = false;                // the session's value wins over a change not sent yet
        ++written;
        if (values.kind == net::RemoteValues::Kind::Change) {
            const std::string who = session.NameOf_cai(values.author);
            ++stats.applied;
            mHub.CountChange_cai();
            stats.last = Describe_cai(who, entry, v.value);
            stats.lastAt = Clock::now();
            mHub.NoteMerged_cai(net::SessionEvent::Kind::Notice, stats.last, fmt::format("ref {} {}", values.author, v.index));
            LOG_DEBUG("value from '{}': {}{} = {}", who, entry.name,
                      entry.element >= 0 ? fmt::format("[{}]", entry.element) : std::string(), v.value);
        }
    }
    if (values.kind == net::RemoteValues::Kind::Correction) {
        stats.rejected += values.values.size();
        const std::string what = values.values.size() == 1 && values.values[0].index < mEntries.size()
                                     ? mEntries[values.values[0].index].label
                                     : fmt::format("{} controls", values.values.size());
        mHub.Note_cai(net::SessionEvent::Kind::Declined, fmt::format("Your {}: back to the session's value ({})", what, values.text));
    } else if (values.kind == net::RemoteValues::Kind::Snapshot) {
        LOG_INFO("snapshot: {} values written", written);
        mHub.Position_cai().snapshotAt = Clock::now();
        mHub.Note_cai(net::SessionEvent::Kind::Notice, fmt::format("The cockpit was set to the session's state ({} values)", written));
    }
}

// Finds the changes made here.
void DatarefSync::Scan_cai()
{
    net::NetSession& session = mHub.Session_cai();
    net::SyncStats& stats = mHub.Sync_cai();
    const net::Person* me = session.Find_cai(session.MyId_cai());
    const bool observer = me && me->role == net::Role::Observer;
    const auto now = Clock::now();
    const auto interval = std::chrono::milliseconds(1000 / std::clamp(session.Rules_cai().rate, 1, 60));
    std::vector<net::DataValue> send;
    for (std::size_t i = 0; i < mEntries.size(); ++i) {
        Entry& entry = mEntries[i];
        if (!entry.usable)
            continue;
        const double now_value = Read_cai(entry);
        if (Differs_cai(entry, now_value, entry.last)) {
            if (observer) {
                Write_cai(entry, entry.last);           // locked: back to the session's value
                ++stats.blocked;
                mHub.NoteMerged_cai(net::SessionEvent::Kind::Declined, "Observer: the controls are locked", "blocked");
                LOG_DEBUG("observer: {} put back to {}", entry.name, entry.last);
                continue;
            }
            entry.last = now_value;
            entry.pending = true;
        }
        if (entry.pending && now - entry.lastSent >= interval) {
            entry.pending = false;
            entry.lastSent = now;
            send.push_back({static_cast<std::uint16_t>(i), entry.last});
            ++stats.sent;
            mHub.CountChange_cai();
            stats.last = Describe_cai("You", entry, entry.last);
            stats.lastAt = now;
            LOG_DEBUG("value changed here: {}{} = {}", entry.name,
                      entry.element >= 0 ? fmt::format("[{}]", entry.element) : std::string(), entry.last);
        }
    }
    if (!send.empty()) {
        std::string why;
        session.SendValues_cai(send, why);
    }
}

// The host answers snapshot requests with the values read here now.
void DatarefSync::AnswerSnapshots_cai()
{
    net::NetSession& session = mHub.Session_cai();
    const std::vector<std::uint8_t> wanted = session.TakeSnapshotRequests_cai();
    if (wanted.empty())
        return;
    std::vector<net::DataValue> values;
    for (std::size_t i = 0; i < mEntries.size(); ++i)
        if (mEntries[i].usable)
            values.push_back({static_cast<std::uint16_t>(i), Read_cai(mEntries[i])});
    for (std::uint8_t id : wanted)
        session.SendSnapshot_cai(id, values);
}

// From the flight loop.
void DatarefSync::Tick_cai()
{
    net::NetSession& session = mHub.Session_cai();
    if (!session.Active_cai()) {
        if (mRunning)
            Stop_cai("the session ended");
        session.TakeValues_cai();
        return;
    }
    if (!mRunning) {
        if (session.HostProfile_cai())
            Start_cai(*session.HostProfile_cai());
        else if (mAnalysis.Data_cai())
            Start_cai(*mAnalysis.Data_cai());
        else
            return;
    }
    for (const net::RemoteValues& values : session.TakeValues_cai())
        Apply_cai(values);
    if (session.IsHost_cai())
        AnswerSnapshots_cai();
    // While the aircraft is reloaded (a move to the PIC) its own start-up changes are not shared;
    // afterwards the values read here become the last known ones.
    const bool hold = mHub.Position_cai().hold;
    if (mWasHeld && !hold)
        for (Entry& entry : mEntries)
            if (entry.usable) {
                entry.last = Read_cai(entry);
                entry.pending = false;
            }
    mWasHeld = hold;
    if (!hold)
        Scan_cai();
}

} // namespace sc::sync
