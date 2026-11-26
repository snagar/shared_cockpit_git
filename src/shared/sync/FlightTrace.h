// shared_cockpit - flight trace (debug, 0.8.4)
//
// With [debug] flight_trace=1 in shared_cockpit.ini, every X-Plane in a session writes
// shared_cockpit_trace.csv next to its log, 10 rows a second: where the aircraft is, what the
// pilot's inputs are (the PIC's own, or the ones received), and what X-Plane did with them - the
// control deflections, the nosewheel steering angle, brakes, throttle in use, engine RPM, the
// overrides. Two traces side by side (the wall-clock column lines them up) show whether two
// aircraft split because the inputs are applied differently or because the flight models diverge.
// A new session starts a new file (the old one becomes shared_cockpit_trace.1.csv).
// 0.9.1: switched on / off on Home > About too; it follows the setting at once (a file opens at
// the next frame of a session, closes when switched off).

#pragma once

#include <chrono>
#include <cstdio>
#include <filesystem>

#include "XPLMDataAccess.h"

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync {

class FlightTrace {
public:
    FlightTrace(net::SessionHub& hub, std::filesystem::path folder, const settings::Settings& settings);
    ~FlightTrace();

    FlightTrace(const FlightTrace&)            = delete;
    FlightTrace& operator=(const FlightTrace&) = delete;

    // From the flight loop: writes a row every 100 ms while a session runs.
    void Tick_cai();

private:
    using Clock = std::chrono::steady_clock;

    // Opens a new file (the old one kept as .1).
    void Open_cai();
    // Closes the file.
    void Close_cai();
    // Finds the datarefs (once).
    void FindRefs_cai();
    // A float (element `index` for an array), 0 when missing.
    static float Float_cai(XPLMDataRef ref, int index = -1);

    net::SessionHub&      mHub;
    std::filesystem::path mFolder;
    const settings::Settings& mSettings;            // [debug] flight_trace, read every frame
    bool                  mFailed = false;           // the file could not be opened: not again until switched off and on
    std::FILE*            mFile = nullptr;
    Clock::time_point     mStart, mNext;
    bool                  mFound = false;
    struct {
        XPLMDataRef lat, lon, psi, gs, onGround, frame;
        XPLMDataRef inPitch, inRoll, inYaw, totalPitch, totalRoll, totalYaw, steer;
        XPLMDataRef brakeL, brakeR, parking, throttle, throttleUse, rpm, propMode, trim, flap;
        XPLMDataRef ovJoystick, ovThrottles, ovToeBrakes;
    } mRefs{};
};

} // namespace sc::sync
