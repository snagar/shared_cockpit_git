// shared_cockpit - the plugin's own settings (shared_cockpit.ini in the plugin folder)
//
// What the user typed last time, so the Connect page opens ready: the name, the
// mode, the ports, the host options and the last 5 addresses. Passwords are
// never saved. Plain C++ only (no X-Plane): the test tools can use it too.
//
//   [connect]
//   name=Ann
//   mode=join                    ; join | host
//   join_port=39100
//   host_port=39100
//   max_people=4
//   joiners_start_as=crew        ; crew | observer
//   recent=100.64.12.7           ; newest first, at most 5
//   recent=192.168.1.20

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "shared/net/SessionRules.h"

namespace sc::settings {

struct ConnectSettings {
    std::string              name;
    bool                     hostMode = false;
    std::uint16_t            joinPort = 39100;
    std::uint16_t            hostPort = 39100;
    int                      maxPeople = 4;
    bool                     joinersObserve = false;   // true = "Observer (I assign seats)"
    std::vector<std::string> recent;                   // newest first
};

// The flight deck window (2D): its size and where it was.
//   [flightdeck]
//   size=small                   ; minimal | line | small
//   left=1500                    ; 2D position (top-left corner), -1 = not placed yet
//   top=1000
//   opacity=50                   ; % of the background while the mouse is not over the deck (20..100)
struct FlightDeckSettings {
    int size = 2;                 // 0 minimal, 1 line, 2 small
    int left = -1;
    int top  = -1;
    int opacity = 50;
};

// The session's rules (net::SessionRules), used when I host; the host's apply while I join.
//   [session]
//   rate=10                      ; knob / lever updates a second while moving: 5, 10, 20
//   conflict_ms=1000             ; two changes of one control within this: the higher rank wins (500, 1000, 2000)
//   ground_m=5                   ; "Move to PIC" on the ground: farther than this (1..100 m)
//   heading_deg=3                ;   ... or the heading off more (1..45)
//   air_m=250                    ; "Move to PIC" in the air: farther than this (50..2000 m)
//   ask_on_join=1                ; ask joiners to move when they join
//   weather=1                    ; joiners follow the host's weather
//   weather_s=60                 ; sent every 10, 30 or 60 s (+ on a change, at most every 10 s)
//   time=1                       ; joiners follow the host's time and date
//   time_tolerance_s=30          ; the clock is corrected when off by more (5..600)
//   flight=inputs                ; inputs: the PIC's yoke / pedals / levers for everybody; follow: (phase 2)
//   copilot_axes=0               ; 1: one Crew member's stick / pedals on X-Plane's copilot side (opt-in, risks)
// 0.5 / 0.6 wrote [sharing] rate and [position] ground_m, air_m, heading_deg, weather: still read once.

// Getting attention (personal, 0.9.12).
//   [alerts]
//   pulse=1                      ; questions and "you have control" pulse (0: steady colours)
//   chimes=1                     ; a chime for questions, control, messages, a lost PIC (X-Plane 12)
//   volume=70                    ; chime volume, % (0..100)
//   vr_window_follows=1          ; VR: the window comes to me after a seat change (0.9.26)
struct AlertSettings {
    bool pulse = true;
    bool chimes = true;
    int  volume = 70;
    bool vrWindowFollows = true;
};

// Debugging helps (personal, not session rules).
//   [debug]
//   flight_trace=0               ; 1: shared_cockpit_trace.csv, 10 rows a second while in a session
//   log_level=default            ; default: info in a Release build, debug in a Debug build; info; debug
struct DebugSettings {
    bool flightTrace = false;
    int  logLevel = -1;          // -1 the build's default, 0 info, 1 debug (Home > About)
};

class Settings {
public:
    inline static constexpr std::size_t kMaxRecent = 5;

    // Reads `file` (a missing file keeps the defaults). False with `error` when it cannot be read.
    bool Load_cai(const std::filesystem::path& file, std::string& error);
    // Writes the file it was loaded from. False with `error`.
    bool Save_cai(std::string& error) const;
    // Puts `address` first in the recent list (no duplicates, at most kMaxRecent).
    void AddRecent_cai(const std::string& address);

    ConnectSettings    connect;
    FlightDeckSettings flightDeck;
    net::SessionRules  rules;
    DebugSettings      debug;
    AlertSettings      alerts;

private:
    std::filesystem::path mFile;
};

} // namespace sc::settings
