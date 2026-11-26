// shared_cockpit - flying together, way 1: the PIC's inputs for everybody (0.8)
//
// Every X-Plane flies its own flight model. The PIC's X-Plane reads what the pilot's hands and
// feet do - yoke pitch / roll, rudder, toe brakes, parking brake, and per engine the throttle,
// prop and mixture levers - and sends it about 30 times a second while it changes (once a second
// when still), on the unreliable channel (a late packet is replaced by the next one).
// The other X-Planes write it every frame:
//   * yoke and rudder:  sim/operation/override/override_joystick on, then sim/joystick/yoke_*_ratio
//                       (their own joystick no longer moves anything);
//   * toe brakes:       override_toe_brakes on (when X-Plane has it), then left / right_brake_ratio;
//   * throttles:        override_throttles on (when X-Plane has it), then ENGN_thro_use and the
//                       cockpit's throttle_ratio (animation);
//   * speed brake:      the handle (speedbrake_ratio), every frame;
//   * trims:            elevator / aileron / rudder trim, every frame (a hardware trim wheel moving
//                       them back is counted like a lever);
//   * flaps:            the flap handle (flap_handle_request_ratio), every frame;
//   * reverse / beta:   per engine prop_mode (feather, normal, beta, reverse), with the throttle;
//   * collective:       a helicopter's collective is X-Plane's prop lever (prop_ratio): on a helicopter
//                       override_prop_pitch is switched on too (on an aeroplane it would stop the
//                       prop governor, so not there);
//   * prop, mixture:    the cockpit's prop_ratio / mixture_ratio, every frame (a hardware lever
//                       moving them back is counted: "held N a second" on the screens).
// Copilot axes (opt-in session rule): one Crew member binds their hardware to X-Plane's Copilot
// Pitch / Roll / Yaw axes. Their X-Plane leaves its copilot side to that hardware; the plugin reads
// it (yoke_*_ratio_copilot) and sends it. Every other X-Plane switches its copilot side to the plugin
// (override_joystick_*_copilot) and writes it; X-Plane combines both sides itself: linked controls -
// the larger deflection wins; independent (fly-by-wire) sticks - added. Older than 0.5 s: centred.
// When I become the PIC everything is given back to my own hardware (and the other way round).
// Handover blend (0.9.1, session rule handoverBlendMs): my hardware's yoke and rudder may be
// anywhere when I take control. The joystick override stays on and, over the blend time, the
// plugin writes the old PIC's yoke and rudder moving to my hardware's - read from
// sim/joystick/joy_mapped_axis_value (pitch 1, roll 2, yaw 3; -1..1 like yoke_*_ratio) - then my
// joystick is mine. 0.9.2: no probe frame (it read stale values). Throttle and brakes: at once.
// These datarefs are not shared by DatarefSync (OwnsDataref_cai) - only from the PIC's hands.
// The aircraft still drift apart a little (separate flight models): "Move to PIC".

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"

#include "shared/net/SessionHub.h"
#include "shared/sync/FlightSync.h"

namespace sc::sync {

class FlightInputs : public FlightMode {
public:
    explicit FlightInputs(net::SessionHub& hub);
    ~FlightInputs() override;

    void Tick_cai(bool iAmPic) override;
    void OnPicChanged_cai(bool iAmPic) override;
    void Stop_cai() override;
    const char* Name_cai() const override { return "Inputs"; }

    // True for the datarefs this way shares itself (DatarefSync leaves them alone).
    static bool OwnsDataref_cai(std::string_view name);

private:
    using Clock = std::chrono::steady_clock;

    struct Refs {
        XPLMDataRef yokePitch = nullptr, yokeRoll = nullptr, yokeHeading = nullptr;
        XPLMDataRef brakeLeft = nullptr, brakeRight = nullptr, parking = nullptr, speedbrake = nullptr;
        XPLMDataRef trimElevator = nullptr, trimAileron = nullptr, trimRudder = nullptr, flapRequest = nullptr;
        XPLMDataRef propMode = nullptr;
        XPLMDataRef throttle = nullptr, prop = nullptr, mixture = nullptr, throttleUse = nullptr;
        XPLMDataRef engines = nullptr;
        XPLMDataRef overrideJoystick = nullptr, overrideToeBrakes = nullptr, overrideThrottles = nullptr;
        XPLMDataRef overridePropPitch = nullptr;     // a helicopter's collective (prop_ratio)
        XPLMDataRef isHelicopter = nullptr;
        XPLMDataRef copPitch = nullptr, copRoll = nullptr, copHeading = nullptr;            // the copilot side
        XPLMDataRef copOverride[3] = {nullptr, nullptr, nullptr};                           // its overrides
        XPLMDataRef mappedAxes = nullptr;              // sim/joystick/joy_mapped_axis_value (the raw axes)
    };
    // The handover blend (I became the PIC).
    struct Blend {
        enum class Step { None, Probe, Blending, Check };   // Check: one frame after, logs how close the blend came
        Step              step = Step::None;
        float             from[3] = {0.0f, 0.0f, 0.0f};     // the old PIC's yoke pitch / roll / rudder
        Clock::time_point start;
        double            seconds = 1.0;
        float             target[3] = {0.0f, 0.0f, 0.0f};   // my hardware as the blend ended (the raw axes)
        int               checkFrames = 0;           // frames to wait before the check
    };

    // Finds the datarefs (once).
    void FindRefs_cai();
    // How many engines this aircraft has (1..kMaxEngines).
    int Engines_cai() const;
    // What my hands and feet do now.
    net::FlightInputs Read_cai() const;
    // The PIC: sends when changed (30 a second at most) or once a second.
    void Send_cai();
    // A follower: writes the PIC's inputs (every frame).
    void Write_cai(const net::FlightInputs& inputs);
    // A follower: my own hardware off (true) or back on (false).
    void Overrides_cai(bool on);
    // Copilot axes: sends mine (the copilot) or writes the copilot's (everybody else).
    void TickCopilot_cai(bool iAmPic);
    // The copilot side's overrides on (the plugin writes it) or off (X-Plane's copilot axes).
    void CopilotOverrides_cai(bool on);
    // The PIC: one step of the handover blend (before sending).
    void TickBlend_cai();
    // Ends the handover blend: my joystick is mine.
    void EndBlend_cai(const char* why);
    // Reads an array (count elements) into a vector.
    static std::vector<float> ReadArray_cai(XPLMDataRef ref, int count);

    net::SessionHub&          mHub;
    Refs                      mRefs;
    bool                      mFound = false;
    bool                      mOverridden = false;
    int                       mBefore[4] = {0, 0, 0, 0};    // the overrides before I switched them on
    std::optional<net::FlightInputs> mLast;                 // a follower: the PIC's newest inputs
    net::FlightInputs         mSent;                        // the PIC: what went out last
    Clock::time_point         mSentAt;
    // A follower: frames where my hardware had moved a lever away from the PIC's value.
    std::vector<float>        mWritten[3];
    int                       mHeld = 0;
    float                     mTrim[3] = {0.0f, 0.0f, 0.0f};   // the trims written last frame
    // Copilot axes.
    bool                      mCopOverridden = false;
    int                       mCopBefore[3] = {0, 0, 0};
    net::CopilotInputs        mCopSent;
    Clock::time_point         mCopSentAt;
    std::optional<net::NetSession::CopilotIn> mCopLast;      // the copilot's newest
    Clock::time_point         mCopAt;
    bool                      mTrimWritten = false;
    Clock::time_point         mHeldSince;
    Blend                     mBlend;
};

} // namespace sc::sync
