// shared_cockpit - the PIC's fuel and payload for everybody (0.9)

#include "shared/sync/LoadSync.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "fmt/format.h"

#include "shared/log/Log.h"

namespace sc::sync {

namespace {
constexpr float kFuelToleranceKg    = 1.0f;
constexpr float kPayloadToleranceKg = 0.5f;
constexpr float kCgToleranceM       = 0.01f;
constexpr float kStationUsedKg      = 0.1f;   // a station holding more than this counts as used
constexpr auto  kCheckEvery         = std::chrono::seconds(2);

// The sum of a list.
float Sum_cai(const std::vector<float>& values)
{
    return std::accumulate(values.begin(), values.end(), 0.0f);
}
} // namespace

LoadSync::LoadSync(net::SessionHub& hub) : mHub(hub)
{
}

// Finds the datarefs (once).
void LoadSync::FindRefs_cai()
{
    if (mFound)
        return;
    mFound = true;
    mFuel = XPLMFindDataRef("sim/flightmodel/weight/m_fuel");
    mStations = XPLMFindDataRef("sim/flightmodel/weight/m_stations");      // X-Plane 12 only
    mPayload = XPLMFindDataRef("sim/flightmodel/weight/m_fixed");
    mCgz = XPLMFindDataRef("sim/flightmodel/misc/cgz_ref_to_default");
    mCgx = XPLMFindDataRef("sim/flightmodel/misc/cgx_ref_to_default");
    LOG_INFO("load: fuel {}, payload stations {}, payload total {}, CG {}", mFuel ? "yes" : "missing",
             mStations ? fmt::format("yes ({})", Count_cai(mStations)) : std::string("no (X-Plane 11)"),
             mPayload ? "yes" : "missing", mCgz ? "yes" : "missing");
}

// The elements of an array dataref.
int LoadSync::Count_cai(XPLMDataRef ref)
{
    return ref ? std::clamp(XPLMGetDatavf(ref, nullptr, 0, 0), 0, net::kMaxLoadValues) : 0;
}

// My fuel and payload now.
net::LoadInfo LoadSync::Read_cai() const
{
    net::LoadInfo load;
    load.fuel.resize(static_cast<std::size_t>(Count_cai(mFuel)));
    if (!load.fuel.empty())
        XPLMGetDatavf(mFuel, load.fuel.data(), 0, static_cast<int>(load.fuel.size()));
    load.stations.resize(static_cast<std::size_t>(Count_cai(mStations)));
    if (!load.stations.empty())
        XPLMGetDatavf(mStations, load.stations.data(), 0, static_cast<int>(load.stations.size()));
    auto lmbda_used = [](float kg) { return kg > kStationUsedKg; };
    load.useStations = std::any_of(load.stations.begin(), load.stations.end(), lmbda_used);
    load.payload = mPayload ? XPLMGetDataf(mPayload) : 0.0f;
    load.cgz = mCgz ? XPLMGetDataf(mCgz) : 0.0f;
    load.cgx = mCgx ? XPLMGetDataf(mCgx) : 0.0f;
    return load;
}

// True when the two loads differ by more than the tolerances.
bool LoadSync::Differs_cai(const net::LoadInfo& a, const net::LoadInfo& b)
{
    auto lmbda_listDiffers = [](const std::vector<float>& x, const std::vector<float>& y, float tolerance) {
        if (x.size() != y.size())
            return true;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i] - y[i]) > tolerance)
                return true;
        return false;
    };
    return a.useStations != b.useStations || lmbda_listDiffers(a.fuel, b.fuel, kFuelToleranceKg) ||
           lmbda_listDiffers(a.stations, b.stations, kPayloadToleranceKg) || std::fabs(a.payload - b.payload) > kPayloadToleranceKg ||
           std::fabs(a.cgz - b.cgz) > kCgToleranceM || std::fabs(a.cgx - b.cgx) > kCgToleranceM;
}

// Writes the PIC's fuel and payload into my aircraft.
void LoadSync::Apply_cai(const net::LoadInfo& load, const char* why)
{
    net::FlightStats& stats = mHub.Flight_cai();
    const int tanks = std::min(Count_cai(mFuel), static_cast<int>(load.fuel.size()));
    if (tanks > 0) {
        std::vector<float> fuel(load.fuel.begin(), load.fuel.begin() + tanks);
        XPLMSetDatavf(mFuel, fuel.data(), 0, tanks);
    }
    const int myStations = Count_cai(mStations);
    const bool stations = load.useStations && myStations > 0;
    if (stations) {
        // X-Plane 12 payload stations: X-Plane works out the total and the CG from them.
        std::vector<float> values(static_cast<std::size_t>(myStations), 0.0f);
        std::copy_n(load.stations.begin(), std::min(load.stations.size(), values.size()), values.begin());
        XPLMSetDatavf(mStations, values.data(), 0, myStations);
    } else {
        if (myStations > 0) {
            std::vector<float> empty(static_cast<std::size_t>(myStations), 0.0f);   // not counted twice
            XPLMSetDatavf(mStations, empty.data(), 0, myStations);
        }
        if (mPayload)
            XPLMSetDataf(mPayload, load.useStations ? Sum_cai(load.stations) : load.payload);
        if (mCgz)
            XPLMSetDataf(mCgz, load.cgz);
        if (mCgx)
            XPLMSetDataf(mCgx, load.cgx);
    }
    stats.loadAt = Clock::now();
    stats.loadStations = stations;
    stats.loadTanks = tanks;
    const float payload = stations ? Sum_cai(load.stations) : (load.useStations ? Sum_cai(load.stations) : load.payload);
    LOG_INFO("load: the PIC's fuel and payload written ({}): fuel {:.1f} kg in {} tank(s), payload {:.1f} kg {}", why,
             Sum_cai(load.fuel), tanks, payload,
             stations ? fmt::format("in {} station(s)", myStations)
                      : fmt::format("as the total, CG {:+.2f} / {:+.2f} m{}", load.cgz, load.cgx,
                                    load.useStations ? " (the PIC's stations - I have none)" : ""));
}

// From the flight loop.
void LoadSync::Tick_cai()
{
    net::NetSession& session = mHub.Session_cai();
    const bool active = session.Active_cai();
    if (!active) {
        if (mWasActive) {
            mSent.reset();
            mLast.reset();
        }
        mWasActive = false;
        return;
    }
    FindRefs_cai();
    const bool iAmPic = session.PicId_cai() == session.MyId_cai();
    const bool share = session.Rules_cai().shareLoad;
    if (!mWasActive || iAmPic != mWasPic) {
        mSent.reset();                                // a new PIC sends at once
        mCheckAt = {};
    }
    mWasActive = true;
    mWasPic = iAmPic;
    if (iAmPic) {
        session.TakeLoad_cai();
        mHub.Position_cai().reapplyLoad = false;
        const auto now = Clock::now();
        if (!share || now < mCheckAt)
            return;
        mCheckAt = now + kCheckEvery;
        const net::LoadInfo mine = Read_cai();
        if (!mSent || Differs_cai(mine, *mSent)) {
            LOG_INFO("load: sending my fuel ({:.1f} kg) and payload ({:.1f} kg{}) as the PIC", Sum_cai(mine.fuel),
                     mine.useStations ? Sum_cai(mine.stations) : mine.payload, mine.useStations ? ", stations" : "");
            session.SendLoad_cai(mine);
            mSent = mine;
            mHub.Flight_cai().loadAt = now;
        }
        return;
    }
    if (const auto load = session.TakeLoad_cai()) {
        mLast = *load;
        if (share)
            Apply_cai(*load, "it arrived");
    }
    net::PositionStats& position = mHub.Position_cai();
    if (position.reapplyLoad) {
        position.reapplyLoad = false;
        if (share && mLast)
            Apply_cai(*mLast, "again after the move");
    }
}

} // namespace sc::sync
