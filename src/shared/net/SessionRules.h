// shared_cockpit - the session's rules: how the sync works (0.7, protocol 5; Follow, load: 0.9, protocol 8)
//
// The host decides them (Session settings page, saved in its shared_cockpit.ini [session]) and
// sends them to every joiner when it joins and whenever they change; they apply at once. A
// joiner's own [session] values are used only when it hosts. Personal settings (name, the
// flight deck, the log level) are not rules.

#pragma once

#include <algorithm>
#include <cstdlib>

namespace sc::net {

// How the aircraft fly together.
//   Follow: (0.9, the default) the PIC's aircraft leads; every other aircraft is placed where the
//           PIC's is, every frame (the PIC's inputs still move their levers and yokes).
//   Inputs: every X-Plane flies its own flight model; the PIC's yoke, rudder, brakes and levers
//           go to everybody (the others' own hardware is switched off). They drift: "Move to PIC".
enum class FlightMode : int { Inputs = 0, Follow = 1 };

struct SessionRules {
    int  rate = 10;              // knob / lever updates a second while moving: 5, 10, 20
    int  conflictMs = 1000;      // two changes of one control within this: the higher rank wins (500, 1000, 2000)
    int  groundM = 5;            // "Move to PIC" on the ground: farther than this (1..100 m)
    int  headingDeg = 3;         //   ... or the heading off more than this (1..45 degrees)
    int  airM = 250;             // "Move to PIC" in the air: farther than this (50..2000 m)
    bool askOnJoin = true;       // ask joiners to move when they join
    bool weather = true;         // joiners follow the host's weather
    int  weatherS = 60;          // the host sends it every 10, 30 or 60 s (+ on a change, at most every 10 s)
    bool time = true;            // joiners follow the host's time and date
    int  timeToleranceS = 30;    // the clock is corrected only when off by more (5..600 s)
    FlightMode flight = FlightMode::Follow;   // how the aircraft fly together
    int  smoothingMs = 100;      // Follow: how fast an aircraft closes a gap to the PIC's (50, 100, 200 ms)
    bool followStrict = false;   // Follow: the followers' flight model off (exact, but no own physics)
    bool shareLoad = true;       // the PIC's fuel and payload for everybody
    int  handoverBlendMs = 1000; // a new PIC: its yoke and rudder blend in from the old PIC's over this (0, 1000, 2000 ms)
    bool copilotAxes = false;    // opt-in: one Crew member's stick / yoke / pedals act on X-Plane's copilot side (risks: see the page)

    // The values inside their ranges (a choice list snaps to the nearest choice).
    void Clamp_cai()
    {
        auto lmbda_nearest = [](int value, int a, int b, int c) {
            const int da = std::abs(value - a), db = std::abs(value - b), dc = std::abs(value - c);
            return da <= db && da <= dc ? a : db <= dc ? b : c;
        };
        rate = lmbda_nearest(rate, 5, 10, 20);
        conflictMs = lmbda_nearest(conflictMs, 500, 1000, 2000);
        groundM = std::clamp(groundM, 1, 100);
        headingDeg = std::clamp(headingDeg, 1, 45);
        airM = std::clamp(airM, 50, 2000);
        weatherS = lmbda_nearest(weatherS, 10, 30, 60);
        timeToleranceS = std::clamp(timeToleranceS, 5, 600);
        smoothingMs = lmbda_nearest(smoothingMs, 50, 100, 200);
        handoverBlendMs = lmbda_nearest(handoverBlendMs, 0, 1000, 2000);
        if (flight != FlightMode::Inputs && flight != FlightMode::Follow)
            flight = FlightMode::Follow;
    }

    bool operator==(const SessionRules&) const = default;
};

} // namespace sc::net
