// shared_cockpit - the PIC's fuel and payload for everybody (0.9, session rule shareLoad)
//
// The same aircraft with another load accelerates, climbs and turns differently. The PIC's X-Plane
// reads its load every 2 s and sends it when it changed (a tank by more than 1 kg, the payload by
// more than 0.5 kg, the CG by more than 1 cm) and when it becomes the PIC; the host keeps the last
// one for new joiners. The others write it when it arrives and again after a move to the PIC.
//   * fuel:    per tank, sim/flightmodel/weight/m_fuel (kg);
//   * payload: X-Plane 12 aircraft with payload stations - per station, m_stations (kg); X-Plane
//              works out the total (m_fixed) and the CG from them. Aircraft without stations (and
//              X-Plane 11 aircraft): the total, m_fixed, and the CG offsets cgz_ref_to_default /
//              cgx_ref_to_default. The PIC's stations decide: when they hold weight, the stations
//              are used (where the follower has them too); otherwise the total (the follower's
//              stations are emptied first, so the weight is not counted twice).
// Once in flight every X-Plane burns its own fuel; the PIC's newer tank values keep coming.

#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "XPLMDataAccess.h"

#include "shared/net/SessionHub.h"

namespace sc::sync {

class LoadSync {
public:
    explicit LoadSync(net::SessionHub& hub);

    LoadSync(const LoadSync&)            = delete;
    LoadSync& operator=(const LoadSync&) = delete;

    // From the flight loop.
    void Tick_cai();

private:
    using Clock = std::chrono::steady_clock;

    // Finds the datarefs (once).
    void FindRefs_cai();
    // My fuel and payload now.
    net::LoadInfo Read_cai() const;
    // Writes the PIC's fuel and payload into my aircraft.
    void Apply_cai(const net::LoadInfo& load, const char* why);
    // True when the two loads differ by more than the tolerances.
    static bool Differs_cai(const net::LoadInfo& a, const net::LoadInfo& b);
    // The elements of an array dataref (0 when missing).
    static int Count_cai(XPLMDataRef ref);

    net::SessionHub&             mHub;
    bool                         mFound = false;
    XPLMDataRef                  mFuel = nullptr, mStations = nullptr, mPayload = nullptr, mCgz = nullptr, mCgx = nullptr;
    bool                         mWasPic = false;
    bool                         mWasActive = false;
    Clock::time_point            mCheckAt;           // the PIC: the next look at my load
    std::optional<net::LoadInfo> mSent;              // the PIC: what went out last
    std::optional<net::LoadInfo> mLast;              // a follower: the PIC's last (written again after a move)
};

} // namespace sc::sync
