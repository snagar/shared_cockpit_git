// shared_cockpit - network protocol constants (version 1)
//
// Every number both sides must agree on: the message header, the default port,
// the channels, the limits and the timeouts. Change kProtocolVersion whenever
// a message changes its fields: plugins with another version refuse each other.
// See src/shared/net/README.md for the message list and the rules.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sc::net {

// --- Message header: 'S' 'C' <protocol> <type> ------------------------------------------
inline constexpr std::uint8_t kMagic0          = 'S';
inline constexpr std::uint8_t kMagic1          = 'C';
inline constexpr std::uint8_t kProtocolVersion = 10;  // 2: commands (0.4), 3: datarefs, priority (0.5), 4: position, weather (0.6), 5: rules (0.7), 6: flight inputs, pause (0.8), 7: copilot axes (0.8.3), 8: follow, load (0.9), 9: handover blend (0.9.1), 10: seats (0.9.6)
inline constexpr std::size_t  kHeaderSize      = 4;

// --- Connections ---------------------------------------------------------------------
inline constexpr std::uint16_t kDefaultPort  = 39100;   // UDP; editable on the Connect page
inline constexpr int           kMaxPeople    = 4;       // the host + 3 joiners
inline constexpr int           kEnetPeers    = kMaxPeople;   // one spare slot: a 4th joiner gets "session full"
inline constexpr int           kChannels     = 2;

// ENet channels.
inline constexpr std::uint8_t kChannelReliable   = 0;   // handshake, people, roles, control (later commands, switches)
inline constexpr std::uint8_t kChannelUnreliable = 1;   // later: the position stream

// --- Timeouts (seconds) ----------------------------------------------------------------
inline constexpr double kConnectTimeout   = 10.0;   // ENet connect until the host answers
inline constexpr double kHandshakeTimeout = 5.0;    // connected until Welcome / Refused
inline constexpr double kRequestTimeout   = 15.0;   // a control request / offer not answered
inline constexpr double kPeopleInterval   = 2.0;    // the host re-sends People (pings)
inline constexpr double kProfileTimeout   = 15.0;   // Welcome until ProfileAck (the profile can be large)
inline constexpr std::uint32_t kPeerTimeoutMinMs = 5000;   // ENet: a silent peer is lost after 5..8 s
inline constexpr std::uint32_t kPeerTimeoutMaxMs = 8000;

// --- Limits ---------------------------------------------------------------------------
inline constexpr std::size_t kMaxNameLength    = 32;
inline constexpr std::size_t kMaxProfileBytes  = 4u * 1024u * 1024u;
inline constexpr std::size_t kNonceSize        = 16;

// The message types (the 4th header byte). Never renumber: add new ones at the end.
enum class MsgType : std::uint8_t {
    Hello          = 1,
    Challenge      = 2,
    Proof          = 3,
    Welcome        = 4,
    Refused        = 5,
    Profile        = 6,
    ProfileAck     = 7,
    People         = 8,
    ControlRequest = 10,
    ControlOffer   = 11,
    ControlAnswer  = 12,
    ControlChanged = 13,
    Bye            = 14,
    Notice         = 15,
    Command        = 16,    // a shared command's Begin / End (protocol 2)
    Reject         = 17,    // the host did not share someone's change (protocol 2)
    Values         = 18,    // shared dataref values (protocol 3)
    Correction     = 19,    // the host's values for changes it did not share: snap back (protocol 3)
    Snapshot       = 20,    // every shared dataref value, to a joiner (protocol 3)
    Priority       = 21,    // the priority order of the joiners (protocol 3)
    Position       = 22,    // where someone's aircraft is (protocol 4)
    Weather        = 23,    // the host's weather and time (protocol 4)
    SnapshotRequest = 24,   // a joiner asks for the snapshot again (after a move that reloaded the aircraft)
    Rules          = 25,    // the session's rules, host -> everybody (protocol 5)
    FlightInputs   = 26,    // the PIC's yoke, rudder, brakes, levers (unreliable, ~30 a second; protocol 6)
    Pause          = 27,    // the PIC's pause state (protocol 6)
    CopilotInputs  = 28,    // the copilot's stick / yoke / pedals on X-Plane's copilot side (unreliable; protocol 7)
    FlightState    = 29,    // the PIC's aircraft: position, attitude, speeds (unreliable, ~30 a second; protocol 8)
    Load           = 30,    // the PIC's fuel and payload (protocol 8)
    Seats          = 31,    // the host's seats (its aircraft's vrconfig hotspots), host -> everybody (protocol 10)
    SeatChoice     = 32,    // a joiner picked a seat, joiner -> host (protocol 10)
};

// How often everybody sends its position, and the host its weather (seconds).
inline constexpr double kPositionInterval = 1.0;
inline constexpr double kFlightInputsRate = 30.0;  // the PIC's flight inputs a second (while they change; 1 a second when still)
inline constexpr int    kMaxEngines       = 16;
inline constexpr double kCopilotStaleS    = 0.5;   // copilot inputs older than this: centred (a lost link must not hold a deflection)    // array elements sent per lever (throttle, prop, mixture)
inline constexpr double kFlightStateRate  = 30.0;  // the PIC's aircraft state a second (Follow)
inline constexpr double kFollowMaxM       = 20000.0; // Follow: farther from the PIC than this, "Move to PIC" (a new flight) first
inline constexpr int    kMaxLoadValues    = 32;
inline constexpr int    kMaxSeats         = 16;    // seats sent at most
inline constexpr std::uint8_t kNoSeat     = 0xFF;  // Person::seat: none picked    // fuel tanks / payload stations sent at most
inline constexpr double kWeatherMinGap    = 10.0;   // a weather change is sent at once, but not more often than this

// The most values in one message (a snapshot is sent in parts of this size).
inline constexpr std::size_t kMaxValuesPerMessage = 1000;

// A command's phase on the network (a quick click = Begin then End).
enum class CommandPhase : std::uint8_t { Begin = 0, End = 1 };

// Two changes of the same control closer than this: the higher rank wins (host, PIC, priority).
// The conflict window is a session rule now (SessionRules::conflictMs).

// A person's seat. The master (the host in v1) and the pilot in command are flags on top.
enum class Role : std::uint8_t {
    Observer = 0,     // sees everything, touches nothing
    Crew     = 1,     // works the shared switches and knobs
};

// Why the host refused a joiner.
enum class RefuseReason : std::uint8_t {
    ProtocolVersion = 1,
    WrongPassword   = 2,
    SessionFull     = 3,
    OtherAircraft   = 4,
    NameInUse       = 5,
    HostNotReady    = 6,
    BadName         = 7,
};

// Why a connection ended (Bye, or seen by the network thread).
enum class ByeReason : std::uint8_t {
    Leaving      = 1,     // the person left
    HostEnded    = 2,     // the host ended the session
    Kicked       = 3,     // the master removed the person
    Refused      = 4,     // after Refused
    Timeout      = 5,     // no answer (handshake or ENet)
    ProtocolError = 6,    // a message could not be read
};

// Warning flags shown in the people list.
enum PersonFlags : std::uint8_t {
    kFlagNone            = 0,
    kFlagAcfVersion      = 1 << 0,   // same .acf name, different stamp
    kFlagHostProfile     = 1 << 1,   // uses the host's profile (its own differs)
    kFlagMissingControls = 1 << 2,   // the host shares controls this aircraft does not have
};

// The id of the host in the people list (joiners get 1, 2, 3, ... from the host).
inline constexpr std::uint8_t kHostId = 0;
inline constexpr std::uint8_t kNoId   = 0xFF;

// Names for the log and the screens.
const char* MsgTypeName_cai(MsgType type);
const char* RoleName_cai(Role role);
const char* RefuseText_cai(RefuseReason reason);
const char* ByeText_cai(ByeReason reason);

} // namespace sc::net
