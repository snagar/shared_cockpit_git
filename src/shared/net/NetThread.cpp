// shared_cockpit - the network thread: the only code that touches ENet

#include "shared/net/NetThread.h"

#include <chrono>
#include <map>

#include "enet/enet.h"

#include "shared/log/Log.h"
#include "shared/net/Protocol.h"

namespace sc::net {

namespace {

std::mutex gEnetMutex;
int        gEnetUsers = 0;

// enet_initialize for the first user (ENet keeps global state on Windows: WSAStartup).
bool EnetAcquire_cai()
{
    std::lock_guard lock(gEnetMutex);
    if (gEnetUsers == 0) {
        if (enet_initialize() != 0)
            return false;
        LOG_INFO("ENet {}.{}.{} initialized", ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH);
    }
    ++gEnetUsers;
    return true;
}

// enet_deinitialize after the last user.
void EnetRelease_cai()
{
    std::lock_guard lock(gEnetMutex);
    if (--gEnetUsers == 0) {
        enet_deinitialize();
        LOG_DEBUG("ENet deinitialized");
    }
}

// "192.168.1.20:39100" for an ENet address.
std::string AddressText_cai(const ENetAddress& address)
{
    char ip[64] = {};
    if (enet_address_get_host_ip(&address, ip, sizeof(ip)) != 0)
        return fmt::format("?:{}", address.port);
    return fmt::format("{}:{}", ip, address.port);
}

// The link id stored in a peer.
LinkId LinkOf_cai(const ENetPeer* peer)
{
    return static_cast<LinkId>(reinterpret_cast<std::uintptr_t>(peer->data));
}

// True for the People update (every 2 s) and the Position / Weather streams: their packets are not logged (the
// session logs the changes), so the log is not buried under heartbeats.
bool IsHeartbeat_cai(const std::uint8_t* data, std::size_t size)
{
    return size >= kHeaderSize && data[0] == kMagic0 && data[1] == kMagic1 &&
           (static_cast<MsgType>(data[3]) == MsgType::People || static_cast<MsgType>(data[3]) == MsgType::Position ||
            static_cast<MsgType>(data[3]) == MsgType::Weather);
}

// "Hello" for logging the bytes of a message (they start with the header).
const char* PacketName_cai(const std::uint8_t* data, std::size_t size)
{
    if (size < kHeaderSize || data[0] != kMagic0 || data[1] != kMagic1)
        return "(not SC)";
    return MsgTypeName_cai(static_cast<MsgType>(data[3]));
}

// An event (the data, when any, is set by the caller).
NetEvent MakeEvent_cai(NetEvent::Kind kind, LinkId link = kNoLink, std::uint32_t code = 0, std::uint16_t port = 0,
                       std::string text = {})
{
    NetEvent event;
    event.kind = kind;
    event.link = link;
    event.code = code;
    event.port = port;
    event.text = std::move(text);
    return event;
}

} // namespace

NetThread::~NetThread()
{
    Stop_cai(static_cast<std::uint32_t>(ByeReason::Leaving));
    Join_cai();
}

// Starts the thread as the host.
void NetThread::StartHost_cai(std::uint16_t port, int maxLinks)
{
    Start_cai(true, {}, port, maxLinks);
}

// Starts the thread as a joiner.
void NetThread::StartJoin_cai(const std::string& address, std::uint16_t port)
{
    Start_cai(false, address, port, 1);
}

// Starts the thread (an old one is stopped first).
void NetThread::Start_cai(bool host, const std::string& address, std::uint16_t port, int maxLinks)
{
    Stop_cai(static_cast<std::uint32_t>(ByeReason::Leaving));
    Join_cai();
    {
        std::lock_guard lock(mMutex);
        mCommands.clear();
        mEvents.clear();
        mRunning = true;
    }
    auto lmbda_body = [this, host, address, port, maxLinks](StopToken token) {
        Run_cai(token, host, address, port, maxLinks);
    };
    mThread = StopThread(lmbda_body);
}

// Asks the thread to close every link and end.
void NetThread::Stop_cai(std::uint32_t reason)
{
    std::lock_guard lock(mMutex);
    if (!mRunning)
        return;
    Command command;
    command.kind = Command::Kind::Stop;
    command.code = reason;
    mCommands.push_back(std::move(command));
}

// Waits for the thread to end.
void NetThread::Join_cai()
{
    if (mThread.Joinable_cai())
        mThread.Join_cai();
}

// True while the thread runs.
bool NetThread::Running_cai() const
{
    std::lock_guard lock(mMutex);
    return mRunning;
}

// Queues bytes for a link.
void NetThread::Send_cai(LinkId link, std::vector<std::uint8_t> data, bool reliable)
{
    std::lock_guard lock(mMutex);
    if (!mRunning)
        return;
    Command command;
    command.kind = Command::Kind::Send;
    command.link = link;
    command.reliable = reliable;
    command.data = std::move(data);
    mCommands.push_back(std::move(command));
}

// Closes a link after its queued packets.
void NetThread::Disconnect_cai(LinkId link, std::uint32_t reason)
{
    std::lock_guard lock(mMutex);
    if (!mRunning)
        return;
    Command command;
    command.kind = Command::Kind::Disconnect;
    command.link = link;
    command.code = reason;
    mCommands.push_back(std::move(command));
}

// Takes the events.
std::vector<NetEvent> NetThread::TakeEvents_cai()
{
    std::lock_guard lock(mMutex);
    std::vector<NetEvent> out(std::make_move_iterator(mEvents.begin()), std::make_move_iterator(mEvents.end()));
    mEvents.clear();
    return out;
}

// Adds an event.
void NetThread::Push_cai(NetEvent event)
{
    std::lock_guard lock(mMutex);
    mEvents.push_back(std::move(event));
}

// Takes the commands.
std::vector<NetThread::Command> NetThread::TakeCommands_cai()
{
    std::lock_guard lock(mMutex);
    std::vector<Command> out(std::make_move_iterator(mCommands.begin()), std::make_move_iterator(mCommands.end()));
    mCommands.clear();
    return out;
}

// The thread body: creates the ENet host, then services it until stopped.
void NetThread::Run_cai(const StopToken& token, bool host, std::string address, std::uint16_t port, int maxLinks)
{
    using Clock = std::chrono::steady_clock;
    auto lmbda_finish = [this]() {
        {
            std::lock_guard lock(mMutex);
            mRunning = false;
        }
        Push_cai(MakeEvent_cai(NetEvent::Kind::Stopped));
        LOG_INFO_AS("lmbda_finish", "network thread ended");
    };

    if (!EnetAcquire_cai()) {
        LOG_ERROR("enet_initialize failed");
        Push_cai(MakeEvent_cai(NetEvent::Kind::StartFailed, kNoLink, 0, port, "the network library could not start"));
        lmbda_finish();
        return;
    }

    const char* side = host ? "host" : "join";    // in front of every line: who logged it
    ENetHost* enetHost = nullptr;
    std::map<LinkId, ENetPeer*> links;
    std::map<LinkId, std::uint32_t> closing;     // links this side closed, with the reason it gave
    LinkId nextLink = 1;

    if (host) {
        ENetAddress listen{};
        listen.host = ENET_HOST_ANY;
        listen.port = port;
        enetHost = enet_host_create(&listen, static_cast<std::size_t>(maxLinks), kChannels, 0, 0);
        if (!enetHost) {
            LOG_ERROR("{}: cannot listen on UDP port {} (is another program or X-Plane using it?)", side, port);
            Push_cai(MakeEvent_cai(NetEvent::Kind::StartFailed, kNoLink, 0, port,
                      fmt::format("cannot listen on UDP port {} (already in use?)", port)));
            EnetRelease_cai();
            lmbda_finish();
            return;
        }
        LOG_INFO("{}: hosting: listening on UDP port {} for up to {} connections, {} channels", side, port, maxLinks, kChannels);
        Push_cai(MakeEvent_cai(NetEvent::Kind::Started, kNoLink, 0, port, "hosting"));
    } else {
        enetHost = enet_host_create(nullptr, 1, kChannels, 0, 0);
        ENetAddress target{};
        target.port = port;
        if (!enetHost || enet_address_set_host(&target, address.c_str()) != 0) {
            const std::string why = enetHost ? fmt::format("cannot find the address '{}'", address)
                                             : std::string("cannot create the network connection");
            LOG_ERROR("{}: join {}:{}: {}", side, address, port, why);
            Push_cai(MakeEvent_cai(NetEvent::Kind::StartFailed, kNoLink, 0, port, why));
            if (enetHost)
                enet_host_destroy(enetHost);
            EnetRelease_cai();
            lmbda_finish();
            return;
        }
        ENetPeer* peer = enet_host_connect(enetHost, &target, kChannels, 0);
        if (!peer) {
            LOG_ERROR("{}: enet_host_connect to {} failed", side, AddressText_cai(target));
            Push_cai(MakeEvent_cai(NetEvent::Kind::StartFailed, kNoLink, 0, port, "cannot start the connection"));
            enet_host_destroy(enetHost);
            EnetRelease_cai();
            lmbda_finish();
            return;
        }
        enet_peer_timeout(peer, 0, kPeerTimeoutMinMs, kPeerTimeoutMaxMs);
        const LinkId link = nextLink++;
        peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link));
        links[link] = peer;
        LOG_INFO("{}: joining: connecting to {} ('{}'), link {}", side, AddressText_cai(target), address, link);
        Push_cai(MakeEvent_cai(NetEvent::Kind::Started, link, 0, port, fmt::format("{}:{}", address, port)));
    }

    // The unreliable stream (flight inputs, aircraft state: up to 30 a second) is counted, not logged per packet.
    std::uint64_t streamSent = 0, streamReceived = 0;
    Clock::time_point streamLogAt = Clock::now();
    auto lmbda_handleEvent = [&](ENetEvent& event) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                LinkId link = LinkOf_cai(event.peer);
                if (link == kNoLink) {                       // a new joiner (host side)
                    link = nextLink++;
                    event.peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(link));
                    links[link] = event.peer;
                }
                enet_peer_timeout(event.peer, 0, kPeerTimeoutMinMs, kPeerTimeoutMaxMs);
                const std::string from = AddressText_cai(event.peer->address);
                LOG_INFO_AS("lmbda_handleEvent", "{}: link {} connected ({})", side, link, from);
                Push_cai(MakeEvent_cai(NetEvent::Kind::Connected, link, 0, event.peer->address.port, from));
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                const LinkId link = LinkOf_cai(event.peer);
                NetEvent out = MakeEvent_cai(NetEvent::Kind::Received, link);
                out.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
                if (event.channelID == kChannelUnreliable)
                    ++streamReceived;
                else if (!IsHeartbeat_cai(out.data.data(), out.data.size()))
                    LOG_DEBUG_AS("lmbda_handleEvent", "{}: recv link {} ch {} {} bytes: {}", side, link,
                                 static_cast<int>(event.channelID), out.data.size(),
                                 PacketName_cai(out.data.data(), out.data.size()));
                enet_packet_destroy(event.packet);
                Push_cai(std::move(out));
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                const LinkId link = LinkOf_cai(event.peer);
                links.erase(link);
                event.peer->data = nullptr;
                if (const auto own = closing.find(link); own != closing.end()) {
                    event.data = own->second;            // closed by this side: report our own reason
                    closing.erase(own);
                }
                if (event.data == 0)
                    LOG_INFO_AS("lmbda_handleEvent", "{}: link {} lost ({}): no answer / timeout", side, link,
                                AddressText_cai(event.peer->address));
                else
                    LOG_INFO_AS("lmbda_handleEvent", "{}: link {} disconnected ({}): {}", side, link,
                                AddressText_cai(event.peer->address), ByeText_cai(static_cast<ByeReason>(event.data)));
                Push_cai(MakeEvent_cai(NetEvent::Kind::Disconnected, link, event.data));
                break;
            }
            case ENET_EVENT_TYPE_NONE:
                break;
        }
    };

    auto lastPing = Clock::now();
    std::uint32_t stopReason = static_cast<std::uint32_t>(ByeReason::Leaving);
    bool stopping = false;

    while (!stopping) {
        if (token.StopRequested_cai())
            break;
        for (Command& command : TakeCommands_cai()) {
            if (command.kind == Command::Kind::Stop) {
                stopping = true;
                stopReason = command.code;
                break;
            }
            const auto found = links.find(command.link);
            if (found == links.end()) {
                LOG_DEBUG("{}: command for link {} ignored: the link is gone", side, command.link);
                continue;
            }
            if (command.kind == Command::Kind::Send) {
                ENetPacket* packet = enet_packet_create(command.data.data(), command.data.size(),
                                                        command.reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
                const std::uint8_t channel = command.reliable ? kChannelReliable : kChannelUnreliable;
                if (!command.reliable)
                    ++streamSent;
                else if (!IsHeartbeat_cai(command.data.data(), command.data.size()))
                    LOG_DEBUG("{}: send link {} ch {} {} bytes: {}", side, command.link, static_cast<int>(channel),
                              command.data.size(), PacketName_cai(command.data.data(), command.data.size()));
                if (!packet || enet_peer_send(found->second, channel, packet) != 0) {
                    LOG_WARN("{}: send to link {} failed ({} bytes)", side, command.link, command.data.size());
                    if (packet)
                        enet_packet_destroy(packet);
                }
            } else {
                LOG_INFO("{}: closing link {}: {}", side, command.link, ByeText_cai(static_cast<ByeReason>(command.code)));
                closing[command.link] = command.code;
                enet_peer_disconnect_later(found->second, command.code);
            }
        }
        if (stopping)
            break;

        ENetEvent event;
        if (Clock::now() - streamLogAt >= std::chrono::minutes(1)) {
            if (streamSent || streamReceived)
                LOG_DEBUG("{}: stream in the last minute: {} packets sent, {} received", side, streamSent, streamReceived);
            streamSent = streamReceived = 0;
            streamLogAt = Clock::now();
        }
        int result = enet_host_service(enetHost, &event, 5);
        while (result > 0) {
            lmbda_handleEvent(event);
            result = enet_host_check_events(enetHost, &event);
        }
        if (result < 0)
            LOG_WARN("{}: enet_host_service reported an error", side);

        if (Clock::now() - lastPing >= std::chrono::seconds(1)) {
            lastPing = Clock::now();
            for (const auto& [link, peer] : links)
                if (peer->state == ENET_PEER_STATE_CONNECTED)
                    Push_cai(MakeEvent_cai(NetEvent::Kind::Ping, link, peer->roundTripTime));
        }
    }

    // Close every link politely, give the goodbyes up to half a second to leave.
    if (!links.empty()) {
        LOG_INFO("{}: closing {} link(s): {}", side, links.size(), ByeText_cai(static_cast<ByeReason>(stopReason)));
        for (const auto& [link, peer] : links)
            enet_peer_disconnect_later(peer, stopReason);
        const auto until = Clock::now() + std::chrono::milliseconds(500);
        ENetEvent event;
        while (!links.empty() && Clock::now() < until) {
            if (enet_host_service(enetHost, &event, 10) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE)
                    enet_packet_destroy(event.packet);
                else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                    links.erase(LinkOf_cai(event.peer));
            }
        }
        for (const auto& [link, peer] : links) {
            LOG_DEBUG("{}: link {} reset (no goodbye in time)", side, link);
            enet_peer_reset(peer);
        }
    }
    enet_host_flush(enetHost);
    enet_host_destroy(enetHost);
    EnetRelease_cai();
    lmbda_finish();
}

} // namespace sc::net
