// shared_cockpit - chimes for the moments that need attention (0.9.12)
#include "shared/sync/Chimes.h"

#include <algorithm>
#include <cmath>

#include "XPLMUtilities.h"

#include "shared/log/Log.h"

namespace sc::sync {

namespace {

constexpr int   kRate      = 44100;     // Hz, mono, 16 bit
constexpr int   kPcm16     = 2;         // FMOD_SOUND_FORMAT_PCM16
constexpr int   kBusUi     = 8;         // xplm_AudioUI
constexpr double kPi       = 3.14159265358979;

struct Note { double hz; double seconds; double gapAfter; };

} // namespace

// Finds X-Plane 12's sound call.
Chimes::Chimes(net::SessionHub& hub, const settings::Settings& settings) : mHub(hub), mSettings(settings)
{
    mPlay = reinterpret_cast<PlayFn>(XPLMFindSymbol("XPLMPlayPCMOnBus"));
    LOG_INFO("chimes: {}", mPlay ? "ready (X-Plane's UI sound)" : "this X-Plane has no sound for plugins (X-Plane 12 needed)");
    sInstance = this;
}

// No instance any more.
Chimes::~Chimes()
{
    if (sInstance == this)
        sInstance = nullptr;
}

// The samples of one chime.
std::vector<std::int16_t> Chimes::Make_cai(Kind kind, float volume)
{
    std::vector<Note> notes;
    switch (kind) {
        case Kind::Question: notes = {{659.3, 0.16, 0.04}, {880.0, 0.24, 0.18}, {659.3, 0.16, 0.04}, {880.0, 0.28, 0.0}}; break;
        case Kind::Control:  notes = {{523.3, 0.12, 0.03}, {659.3, 0.12, 0.03}, {784.0, 0.30, 0.0}}; break;
        case Kind::Message:  notes = {{987.8, 0.22, 0.0}}; break;
        case Kind::Lost:     notes = {{440.0, 0.20, 0.05}, {329.6, 0.32, 0.0}}; break;
    }
    std::vector<std::int16_t> out;
    for (const Note& note : notes) {
        const int n = static_cast<int>(note.seconds * kRate);
        for (int i = 0; i < n; ++i) {
            const double t = static_cast<double>(i) / kRate;
            // A soft bell: quick rise, exponential fall, a little second harmonic.
            const double attack = std::min(1.0, t / 0.008);
            const double decay = std::exp(-t * 6.0);
            const double tail = std::min(1.0, (note.seconds - t) / 0.02);     // no click at the end
            const double wave = std::sin(2.0 * kPi * note.hz * t) + 0.25 * std::sin(4.0 * kPi * note.hz * t);
            const double v = 0.45 * volume * attack * decay * tail * wave;
            out.push_back(static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0)));
        }
        out.insert(out.end(), static_cast<std::size_t>(note.gapAfter * kRate), 0);
    }
    return out;
}

// Plays one now.
bool Chimes::Play_cai(Kind kind)
{
    if (!mPlay)
        return false;
    const int volume = std::clamp(mSettings.alerts.volume, 0, 100);
    if (volume != mBufferVolume) {                  // the buffers again at the new volume
        for (int k = 0; k < 4; ++k)
            mBuffers[k] = Make_cai(static_cast<Kind>(k), static_cast<float>(volume) / 100.0f);
        mBufferVolume = volume;
    }
    std::vector<std::int16_t>& buffer = mBuffers[static_cast<int>(kind)];
    if (buffer.empty() || volume == 0)
        return true;
    mPlay(buffer.data(), static_cast<int>(buffer.size() * sizeof(std::int16_t)), kPcm16, kRate, 1, 0, kBusUi, nullptr, nullptr);
    return true;
}

// From the flight loop.
void Chimes::Tick_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const bool active = session.Active_cai();
    const std::uint32_t questions = mHub.QuestionCount_cai();
    const std::uint32_t notices = mHub.NoticeCount_cai();
    const std::uint8_t pic = active ? session.PicId_cai() : 0xFF;
    const bool holding = active && mHub.Flight_cai().holding;
    if (!mStarted) {                                // nothing old chimes at the start
        mStarted = true;
        mQuestions = questions;
        mNotices = notices;
        mPic = pic;
        mHolding = holding;
        return;
    }
    const bool on = mSettings.alerts.chimes;
    if (questions != mQuestions && on)
        Play_cai(Kind::Question);
    else if (pic != mPic && active && pic == session.MyId_cai() && mPic != 0xFF && on)
        Play_cai(Kind::Control);
    else if (holding && !mHolding && on)
        Play_cai(Kind::Lost);
    else if (notices != mNotices && on)
        Play_cai(Kind::Message);
    mQuestions = questions;
    mNotices = notices;
    mPic = pic;
    mHolding = holding;
}

} // namespace sc::sync
