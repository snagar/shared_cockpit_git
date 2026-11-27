// shared_cockpit - the network messages as plain C++ types, and their bytes
//
// One struct per message type (Protocol.h). EncodeMessage_cai turns a message
// into the bytes sent by ENet; DecodeMessage_cai does the opposite and checks
// everything (header, protocol version, lengths). Only this file knows the
// byte layout: integers little-endian, text as u16 length + UTF-8 bytes.

#pragma once

#include "shared/net/SessionRules.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "shared/net/Protocol.h"

namespace sc::net {

using Bytes = std::vector<std::uint8_t>;
using Nonce = std::array<std::uint8_t, kNonceSize>;

struct MsgHello {
    std::string name;
    std::string pluginVersion;      // "0.2.2", for people (the protocol version is in the header)
    std::string acfName;            // "Cessna_172SP_G1000.acf"
    std::string acfStamp;
    std::string profileHash;        // ProfileHash_cai of the joiner's analysis (empty = none)
};
struct MsgChallenge { Nonce nonce{}; };
struct MsgProof     { std::string proof; };            // PasswordProof_cai(password, nonce)
struct MsgWelcome {
    std::uint8_t yourId = kNoId;
    Role         yourRole = Role::Crew;
    std::string  hostName;
    std::string  acfName;
    std::string  acfStamp;
    std::string  profileHash;       // the host's
};
struct MsgRefused    { RefuseReason reason = RefuseReason::HostNotReady; std::string text; };
struct MsgProfile    { std::string hash; std::string text; };        // the host's analysis file text
struct MsgProfileAck { bool ok = false; std::uint16_t missing = 0; std::string text; };

struct Person {
    std::uint8_t  id = kNoId;
    std::string   name;
    Role          role = Role::Crew;
    bool          master = false;
    bool          pic = false;
    std::uint16_t pingMs = 0;
    std::uint8_t  flags = kFlagNone;
    std::uint8_t  seat = kNoSeat;       // in the host's seats (protocol 10); kNoSeat: none picked
};
struct MsgPeople { std::vector<Person> people; };

struct MsgControlRequest { std::uint8_t fromId = kNoId; };           // fromId filled in by the host
struct MsgControlOffer   { std::uint8_t fromId = kNoId; std::uint8_t toId = kNoId; };
// An answer to a request or an offer. To the host: askerId = who asked / offered. From the
// host to the asker (only for a "no"): answerId = who said no. The host fills answerId.
struct MsgControlAnswer  { std::uint8_t askerId = kNoId; std::uint8_t answerId = kNoId; bool accept = false; };
struct MsgControlChanged { std::uint8_t newPic = kNoId; std::uint8_t oldPic = kNoId; std::string reason; };
struct MsgBye            { ByeReason reason = ByeReason::Leaving; std::string text; };
struct MsgNotice         { std::string text; };

// A shared command (index in analysis::SharedCommands_cai). To the host: author = kNoId
// (the host fills it in); from the host: who pressed it.
struct MsgCommand        { std::uint16_t index = 0; CommandPhase phase = CommandPhase::Begin; std::uint8_t author = kNoId; };
// The host did not share a command / change of `index`: `winner` changed it first (or the sender is an observer).
struct MsgReject         { std::uint16_t index = 0; std::uint8_t winner = kNoId; std::string text; };

// One shared dataref's value (index in analysis::SharedDatarefs_cai). Sent as a double:
// exact for ints, floats and doubles.
struct DataValue { std::uint16_t index = 0; double value = 0.0; };
// Values someone changed. To the host: author = kNoId; from the host: who changed them.
struct MsgValues     { std::uint8_t author = kNoId; std::vector<DataValue> values; };
// To the author of changes the host did not share: the session's values (snap back).
struct MsgCorrection { std::uint8_t winner = kNoId; std::string text; std::vector<DataValue> values; };
// Every shared dataref value (part `part` of `parts`), host -> joiner.
struct MsgSnapshot   { std::uint8_t part = 0; std::uint8_t parts = 1; std::vector<DataValue> values; };
// The joiners' priority order (ids, highest first; the host is always first and not listed).
struct MsgPriority   { std::vector<std::uint8_t> order; };

// Where an aircraft is (sent every kPositionInterval by everybody).
struct PositionInfo {
    double       latitude = 0.0, longitude = 0.0;   // degrees
    double       elevation = 0.0;                    // metres MSL
    float        agl = 0.0f;                         // metres above the ground
    float        heading = 0.0f;                     // degrees true
    float        pitch = 0.0f, roll = 0.0f;          // degrees
    float        groundSpeed = 0.0f;                 // m/s
    bool         onGround = true;
    std::uint8_t engines = 0;                        // engines running
    std::uint8_t engineCount = 0;                    // engines of the aircraft
};
// Someone's position. To the host: id = kNoId; from the host: whose.
struct MsgPosition   { std::uint8_t id = kNoId; PositionInfo position; };
// The host's weather and time (indices in sync::WeatherSync's fixed list), host -> everybody.
struct MsgWeather    { std::vector<DataValue> values; };
// A joiner asks for the snapshot again (its aircraft was reloaded).
struct MsgSnapshotRequest { };
// The PIC's flight inputs (option "Inputs"): what the PIC's hands and feet do. Arrays: one
// value per engine (engines = how many are sent, at most kMaxEngines).
struct FlightInputs {
    float yokePitch = 0.0f, yokeRoll = 0.0f, yokeHeading = 0.0f;   // -1..1 (heading = rudder)
    float brakeLeft = 0.0f, brakeRight = 0.0f, parkingBrake = 0.0f; // 0..1
    float speedbrake = 0.0f;                                        // the handle: -0.5 armed, 0 in .. 1 out
    float trimElevator = 0.0f, trimAileron = 0.0f, trimRudder = 0.0f; // -1..1
    float flapRequest = 0.0f;                                       // the flap handle, 0..1
    std::vector<std::uint8_t> propMode;                             // per engine: 0 feather, 1 normal, 2 beta, 3 reverse
    std::vector<float> throttle, prop, mixture;                     // per engine, 0..1 (prop = the collective of a helicopter)
    bool operator==(const FlightInputs&) const = default;
};
// From the PIC (to the host: id = kNoId; from the host: whose).
struct MsgFlightInputs { std::uint8_t id = kNoId; std::uint32_t sequence = 0; FlightInputs inputs; };
// The copilot's stick / yoke / pedals (session rule copilotAxes): X-Plane's copilot side, -1..1.
struct CopilotInputs {
    float pitch = 0.0f, roll = 0.0f, heading = 0.0f;
    bool operator==(const CopilotInputs&) const = default;
};
struct MsgCopilotInputs { std::uint8_t id = kNoId; CopilotInputs inputs; };
// The PIC's pause state (from the host: whose).
struct MsgPause { std::uint8_t id = kNoId; bool paused = false; };
// The PIC's aircraft (session rule Follow): where it is and how it moves. Attitude in true degrees;
// speeds in X-Plane's local frame (x east, y up, z south), m/s; rotation rates in deg/s (body).
struct AircraftState {
    double latitude = 0.0, longitude = 0.0, elevation = 0.0;   // degrees, metres MSL
    float  heading = 0.0f, pitch = 0.0f, roll = 0.0f;          // true heading, pitch, roll (degrees)
    float  vx = 0.0f, vy = 0.0f, vz = 0.0f;                    // local speeds
    float  p = 0.0f, q = 0.0f, r = 0.0f;                       // roll / pitch / yaw rates
    float  agl = 0.0f;                                         // metres above the ground
    bool   onGround = true;
    bool operator==(const AircraftState&) const = default;
};
// timeMs: the PIC's own clock (ms) when it read the state: the followers work out rates from it.
struct MsgFlightState { std::uint8_t id = kNoId; std::uint32_t sequence = 0; std::uint32_t timeMs = 0; AircraftState state; };
// The PIC's fuel and payload. X-Plane 12 aircraft with payload stations use `stations` (kg each);
// aircraft without them (and X-Plane 11 aircraft) use `payload` (the total) and the CG offsets.
struct LoadInfo {
    std::vector<float> fuel;            // kg per tank (sim/flightmodel/weight/m_fuel)
    std::vector<float> stations;        // kg per payload station (m_stations, X-Plane 12); empty: none
    float payload = 0.0f;               // kg (m_fixed: the stations' sum when they are used)
    float cgz = 0.0f, cgx = 0.0f;       // CG offsets, metres (cgz_ref_to_default / cgx_ref_to_default)
    bool  useStations = false;          // the PIC's payload is in its stations
    bool operator==(const LoadInfo&) const = default;
};
struct MsgLoad { std::uint8_t id = kNoId; LoadInfo load; };
// A seat: one of the aircraft's vrconfig teleport hotspots (aircraft coordinates, metres; degrees).
struct SeatInfo {
    std::string name;
    bool  sitting = true;               // SITTING (else STANDING)
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float psi = 0.0f, the = 0.0f, phi = 0.0f;
    bool operator==(const SeatInfo&) const = default;
};
struct MsgSeats { std::vector<SeatInfo> seats; };
// A joiner picked a seat (index in the host's seats, kNoSeat: none). To the host: id = kNoId.
struct MsgSeatChoice { std::uint8_t id = kNoId; std::uint8_t seat = kNoSeat; };
// The session's rules (host -> everybody, on admit and on a change).
struct MsgRules { SessionRules rules; };

using Message = std::variant<MsgHello, MsgChallenge, MsgProof, MsgWelcome, MsgRefused, MsgProfile, MsgProfileAck,
                             MsgPeople, MsgControlRequest, MsgControlOffer, MsgControlAnswer, MsgControlChanged,
                             MsgBye, MsgNotice, MsgCommand, MsgReject, MsgValues, MsgCorrection, MsgSnapshot,
                             MsgPriority, MsgPosition, MsgWeather, MsgSnapshotRequest, MsgRules, MsgFlightInputs, MsgPause, MsgCopilotInputs,
                             MsgFlightState, MsgLoad, MsgSeats, MsgSeatChoice>;

// The type of a message.
MsgType TypeOf_cai(const Message& message);

// The bytes of a message (header included).
Bytes EncodeMessage_cai(const Message& message);

// A message from bytes. Empty with `error` when the bytes are not a valid message of
// this protocol version (`otherProtocol` is set when only the version differs).
std::optional<Message> DecodeMessage_cai(const std::uint8_t* data, std::size_t size, std::string& error,
                                         bool& otherProtocol);

// The proof a joiner sends for `password` and the host's `nonce` (a hash, so the
// password never crosses the network).
std::string PasswordProof_cai(const std::string& password, const Nonce& nonce);

// A random nonce.
Nonce MakeNonce_cai();

} // namespace sc::net
