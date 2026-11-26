// shared_cockpit - the one live session of the plugin, and what happened in it

#include "shared/net/SessionHub.h"

#include <chrono>
#include <ctime>

#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/settings/Settings.h"

namespace sc::net {

namespace {

// Local time now as "10:41:07".
std::string ClockNow_cai()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return fmt::format("{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
}

} // namespace

// Counts one change.
void SessionHub::CountChange_cai()
{
    mRecentChanges.push_back(std::chrono::steady_clock::now());
}

// Polls the network and keeps its events.
void SessionHub::Poll_cai()
{
    mSession.Poll_cai();
    const auto now = std::chrono::steady_clock::now();
    while (!mRecentChanges.empty() && now - mRecentChanges.front() > std::chrono::minutes(1))
        mRecentChanges.pop_front();
    mSync.perMinute = mRecentChanges.size();
    for (SessionEvent& event : mSession.TakeEvents_cai()) {
        if (event.kind == SessionEvent::Kind::People || event.kind == SessionEvent::Kind::Question ||
            event.kind == SessionEvent::Kind::ControlChanged || event.kind == SessionEvent::Kind::State)
            ++mChangeRevision;
        if (event.kind == SessionEvent::Kind::People || event.text.empty())
            continue;            // the people list itself shows these
        Note_cai(event.kind, std::move(event.text));
    }
}

// Adds a feed line.
void SessionHub::Note_cai(SessionEvent::Kind kind, std::string text)
{
    FeedLine line;
    line.time = ClockNow_cai();
    line.kind = kind;
    line.text = std::move(text);
    line.at = std::chrono::steady_clock::now();
    mFeed.push_back(std::move(line));
    while (mFeed.size() > kFeedSize)
        mFeed.pop_front();
    ++mFeedRevision;
}

// Adds a line, or counts it on the last one with the same merge key.
void SessionHub::NoteMerged_cai(SessionEvent::Kind kind, std::string text, const std::string& merge)
{
    const auto now = std::chrono::steady_clock::now();
    if (!mFeed.empty() && !merge.empty() && mFeed.back().merge == merge && now - mFeed.back().at < std::chrono::seconds(3)) {
        FeedLine& last = mFeed.back();
        ++last.count;
        last.text = fmt::format("{} x {}", text, last.count);
        last.time = ClockNow_cai();
        last.at = now;
        ++mFeedRevision;
        return;
    }
    Note_cai(kind, std::move(text));
    mFeed.back().merge = merge;
}

// Ends the session with a reason.
void SessionHub::Leave_cai(const std::string& why)
{
    if (mSession.State_cai() == SessionState::Idle)
        return;
    LOG_INFO("leaving the session: {}", why);
    mSession.Leave_cai();
    mSession.TakeEvents_cai();
    Note_cai(SessionEvent::Kind::State, why);
    ++mChangeRevision;
}

// A short status line.
std::string StatusLine_cai(const NetSession& session)
{
    const std::size_t people = session.People_cai().size();
    const std::string pic = session.PicId_cai() == session.MyId_cai() ? std::string("you")
                                                                     : session.NameOf_cai(session.PicId_cai());
    switch (session.State_cai()) {
        case SessionState::Idle:        return "Not connected";
        case SessionState::Connecting:  return "Connecting...";
        case SessionState::Handshaking: return "Connected, joining...";
        case SessionState::Hosting:
            return people <= 1 ? fmt::format("Hosting on port {} - waiting for others", session.HostSettings_cai().port)
                               : fmt::format("Hosting - {} people - PIC {}", people, pic);
        case SessionState::InSession:
            return fmt::format("In {}'s session - {} people - PIC {}", session.NameOf_cai(kHostId), people, pic);
        case SessionState::Failed:      return fmt::format("Could not connect: {}", session.FailText_cai());
        case SessionState::Lost:        return fmt::format("Session ended: {}", session.FailText_cai());
    }
    return {};
}

// Follow: how my aircraft follows the PIC's.
StatusText FollowLine_cai(const NetSession& session, const FlightStats& flight)
{
    StatusText out;
    if (session.Rules_cai().flight != FlightMode::Follow || session.People_cai().size() <= 1 || !flight.running)
        return out;
    using Mark = StatusText::Mark;
    const std::string pic = session.NameOf_cai(session.PicId_cai());
    const auto now = std::chrono::steady_clock::now();
    out.tooltip = "Follow the PIC: the PIC's aircraft leads, the others are placed where it is every frame.\n"
                  "Your own joystick and pedals are switched off while you are not the PIC - take control to fly.";
    if (flight.iAmPic) {
        const std::size_t others = session.People_cai().size() - 1;
        out.mark = Mark::Ok;
        out.text = fmt::format("Follow: you lead - {} aircraft follow{} yours", others, others == 1 ? "s" : "");
    } else if (flight.farAway) {
        out.mark = Mark::Warn;
        out.text = fmt::format("Too far to follow {} (more than 20 km): use Move to PIC", pic);
    } else if (flight.holding) {
        const double s = std::chrono::duration<double>(now - flight.stateAt).count();
        out.mark = Mark::Warn;
        out.text = fmt::format("Following {}: nothing new for {:.0f} s - your aircraft flies on its own", pic, s);
    } else if (flight.following) {
        out.mark = flight.gapM < 10.0f ? Mark::Ok : Mark::Info;
        out.text = fmt::format("Following {}'s aircraft: {:.1f} m, {:.1f}° off, {:.0f} ms behind{}", pic, flight.gapM, flight.gapDeg,
                               flight.delayMs, flight.strict ? " (strict)" : "");
        out.tooltip += "\n\nm / degrees: how far your aircraft was from the PIC's before each frame's correction.\n"
                       "ms behind: how old the PIC's newest state is, plus the network delay (it is moved ahead by that).";
    } else {
        out.mark = Mark::Info;
        out.text = fmt::format("Following: waiting for {}'s aircraft", pic);
    }
    return out;
}

// Fuel and payload.
StatusText LoadLine_cai(const NetSession& session, const FlightStats& flight)
{
    StatusText out;
    if (!session.Rules_cai().shareLoad || session.People_cai().size() <= 1 || !flight.running)
        return out;
    using Mark = StatusText::Mark;
    out.tooltip = "The PIC's fuel per tank and payload: X-Plane 12 payload stations when the aircraft has them, "
                  "otherwise the total payload and the CG. Sent on joining, after a move, and when they change.";
    if (flight.iAmPic) {
        out.mark = Mark::Ok;
        out.text = "Fuel and payload: yours go to everybody";
    } else if (flight.loadAt == std::chrono::steady_clock::time_point{}) {
        out.mark = Mark::Info;
        out.text = "Fuel and payload: waiting for the PIC's";
    } else {
        out.mark = Mark::Ok;
        out.text = fmt::format("Fuel and payload: the PIC's ({} tank{}, payload {})", flight.loadTanks, flight.loadTanks == 1 ? "" : "s",
                               flight.loadStations ? "per station" : "as the total");
    }
    return out;
}

// The sizes of the core's main classes, as compiled into the core library.
std::string CoreLayout_cai()
{
    return fmt::format("hub={} session={} settings={}", sizeof(SessionHub), sizeof(NetSession), sizeof(settings::Settings));
}

} // namespace sc::net
