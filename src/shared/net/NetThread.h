// shared_cockpit - the network thread: the only code that touches ENet
//
// An ENet host is not thread-safe, so one background thread owns it and nothing
// else calls ENet. The main thread talks to it through two queues:
//   commands  main -> net   (host, connect, send, disconnect, stop)
//   events    net -> main   (started, connected, received, disconnected, ping, stopped)
// Both queues are a mutex + deque, the same pattern as the analysis Scanner.
//
// The thread knows bytes and connections only. What the bytes mean (the messages,
// the people, the roles) is NetSession's job, on the main thread.
// Every transport action is logged (Info: start, connect, disconnect; Debug: every
// packet with its size and channel).

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "shared/util/StopThread.h"

namespace sc::net {

// A connection, as numbered by the network thread (never reused while it runs).
using LinkId = std::uint32_t;
inline constexpr LinkId kNoLink = 0;

struct NetEvent {
    enum class Kind {
        Started,        // hosting: listening on `port` / joining: connecting to `text`
        StartFailed,    // could not create the ENet host (`text` says why)
        Connected,      // a link is up (`text` = the other side's address)
        Disconnected,   // a link is down (`code` = the ByeReason it sent, 0 = lost / timeout)
        Received,       // `data` arrived on `link`
        Ping,           // `code` = round trip time in ms of `link`
        Stopped,        // the thread ended
    };
    Kind                      kind = Kind::Stopped;
    LinkId                    link = kNoLink;
    std::uint32_t             code = 0;
    std::uint16_t             port = 0;
    std::string               text;
    std::vector<std::uint8_t> data;
};

class NetThread {
public:
    NetThread() = default;
    ~NetThread();

    NetThread(const NetThread&)            = delete;
    NetThread& operator=(const NetThread&) = delete;

    // Starts the thread as the host: listens on `port` for at most `maxLinks` connections.
    void StartHost_cai(std::uint16_t port, int maxLinks);
    // Starts the thread as a joiner: connects to `address`:`port` (a name or an IP).
    void StartJoin_cai(const std::string& address, std::uint16_t port);
    // Asks the thread to close every link politely (ByeReason `reason`) and to end.
    // The thread reports Stopped; Join_cai waits for it.
    void Stop_cai(std::uint32_t reason);
    // Waits for the thread to end (after Stop_cai). Safe when it is not running.
    void Join_cai();
    // True while the thread runs.
    bool Running_cai() const;

    // Queues `data` for `link` (reliable or not).
    void Send_cai(LinkId link, std::vector<std::uint8_t> data, bool reliable);
    // Closes `link` after everything queued for it was sent (`reason` reaches the other side).
    void Disconnect_cai(LinkId link, std::uint32_t reason);

    // Takes every event that arrived since the last call.
    std::vector<NetEvent> TakeEvents_cai();

private:
    struct Command {
        enum class Kind { Send, Disconnect, Stop };
        Kind                      kind = Kind::Stop;
        LinkId                    link = kNoLink;
        std::uint32_t             code = 0;
        bool                      reliable = true;
        std::vector<std::uint8_t> data;
    };

    // Starts the thread with these settings.
    void Start_cai(bool host, const std::string& address, std::uint16_t port, int maxLinks);
    // The thread body.
    void Run_cai(const StopToken& token, bool host, std::string address, std::uint16_t port, int maxLinks);
    // Adds an event for the main thread.
    void Push_cai(NetEvent event);
    // Takes the queued commands.
    std::vector<Command> TakeCommands_cai();

    mutable std::mutex   mMutex;
    std::deque<Command>  mCommands;
    std::deque<NetEvent> mEvents;
    bool                 mRunning = false;
    StopThread           mThread;
};

} // namespace sc::net
