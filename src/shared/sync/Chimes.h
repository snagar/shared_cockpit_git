// shared_cockpit - chimes for the moments that need attention (0.9.12)
//
// A short tone pattern, played on X-Plane's UI sound bus:
//   question  - someone offers me control or asks me for it: two rising notes, twice;
//   control   - I have control now: three rising notes;
//   message   - a message someone wrote (not session notices: levers, roles, joins): one soft note;
//   lost      - following stopped (no state from the PIC): two falling low notes.
// The tones are made here (sine waves with a soft start and end): no sound files, nobody else's
// recordings. X-Plane 12's XPLMPlayPCMOnBus, found at run time (XPLMFindSymbol): no chimes on
// X-Plane 11. [alerts] chimes / volume (Home > About).

#pragma once

#include <cstdint>
#include <vector>

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync {

class Chimes {
public:
    enum class Kind { Question, Control, Message, Lost };

    Chimes(net::SessionHub& hub, const settings::Settings& settings);
    ~Chimes();
    Chimes(const Chimes&)            = delete;
    Chimes& operator=(const Chimes&) = delete;

    // The plugin's chimes (null before start / after stop), for About's "Test".
    static Chimes* Instance_cai() { return sInstance; }

    // From the flight loop: plays a chime when something new happened.
    void Tick_cai();
    // Plays one now (About's "Test"); false when X-Plane has no sound for plugins.
    bool Play_cai(Kind kind);
    // True when this X-Plane can play them (X-Plane 12).
    bool Available_cai() const { return mPlay != nullptr; }

private:
    using PlayFn = void* (*)(void*, int, int, int, int, int, int, void (*)(void*, int), void*);

    // The samples of one chime at `volume` (0..1).
    static std::vector<std::int16_t> Make_cai(Kind kind, float volume);

    inline static Chimes*      sInstance = nullptr;
    net::SessionHub&           mHub;
    const settings::Settings&  mSettings;
    PlayFn                     mPlay = nullptr;
    std::vector<std::int16_t>  mBuffers[4];        // kept alive while X-Plane plays them
    int                        mBufferVolume = -1; // the volume the buffers were made at
    std::uint32_t              mQuestions = 0, mNotices = 0;
    std::uint8_t               mPic = 0xFF;
    bool                       mHolding = false;
    bool                       mStarted = false;
};

} // namespace sc::sync
