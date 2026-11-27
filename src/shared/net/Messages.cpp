// shared_cockpit - the network messages as plain C++ types, and their bytes

#include "shared/net/Messages.h"

#include <algorithm>

#include <cstring>
#include <random>

#include "fmt/format.h"

#include "shared/util/Utils.h"

namespace sc::net {

namespace {

// Appends values to a byte buffer (little-endian).
class Writer {
public:
    explicit Writer(Bytes& out) : mOut(out) {}
    // One byte.
    void U8_cai(std::uint8_t value) { mOut.push_back(value); }
    // Two bytes.
    void U16_cai(std::uint16_t value) { U8_cai(static_cast<std::uint8_t>(value)); U8_cai(static_cast<std::uint8_t>(value >> 8)); }
    // Four bytes.
    void U32_cai(std::uint32_t value) { U16_cai(static_cast<std::uint16_t>(value)); U16_cai(static_cast<std::uint16_t>(value >> 16)); }
    // A bool as one byte.
    void Bool_cai(bool value) { U8_cai(value ? 1 : 0); }
    // Text with a u16 length (cut at 65535 bytes).
    void Str_cai(const std::string& text)
    {
        const std::size_t size = std::min<std::size_t>(text.size(), 0xFFFF);
        U16_cai(static_cast<std::uint16_t>(size));
        mOut.insert(mOut.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(size));
    }
    // Long text with a u32 length (the profile).
    void LongStr_cai(const std::string& text)
    {
        U32_cai(static_cast<std::uint32_t>(text.size()));
        mOut.insert(mOut.end(), text.begin(), text.end());
    }
    // Raw bytes.
    void Raw_cai(const std::uint8_t* data, std::size_t size) { mOut.insert(mOut.end(), data, data + size); }
    // A double, as its 8 bytes little-endian.
    void F64_cai(double value)
    {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        U32_cai(static_cast<std::uint32_t>(bits));
        U32_cai(static_cast<std::uint32_t>(bits >> 32));
    }
    // A list of values (u16 count).
    void F32_cai(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        U32_cai(bits);
    }

    void Values_cai(const std::vector<DataValue>& values)
    {
        const std::size_t count = std::min<std::size_t>(values.size(), 0xFFFF);
        U16_cai(static_cast<std::uint16_t>(count));
        for (std::size_t i = 0; i < count; ++i) {
            U16_cai(values[i].index);
            F64_cai(values[i].value);
        }
    }

private:
    Bytes& mOut;
};

// Reads values from bytes; every read checks the length and fails (once) when short.
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : mData(data), mSize(size) {}
    // False once a read ran past the end.
    bool Ok_cai() const { return mOk; }
    // True when every byte was read.
    bool AtEnd_cai() const { return mAt == mSize; }
    // Marks the message as unreadable (a value out of its range).
    void Fail_cai() { mOk = false; }
    // One byte.
    std::uint8_t U8_cai() { return Need_cai(1) ? mData[mAt++] : 0; }
    // Two bytes.
    std::uint16_t U16_cai() { const std::uint16_t low = U8_cai(); return static_cast<std::uint16_t>(low | (U8_cai() << 8)); }
    // Four bytes.
    std::uint32_t U32_cai() { const std::uint32_t low = U16_cai(); return low | (static_cast<std::uint32_t>(U16_cai()) << 16); }
    // A bool.
    bool Bool_cai() { return U8_cai() != 0; }
    // Text with a u16 length.
    std::string Str_cai() { return Take_cai(U16_cai()); }
    // Long text with a u32 length (at most `limit` bytes).
    std::string LongStr_cai(std::size_t limit)
    {
        const std::uint32_t size = U32_cai();
        if (size > limit) {
            mOk = false;
            return {};
        }
        return Take_cai(size);
    }
    // A double.
    float F32_cai()
    {
        const std::uint32_t bits = U32_cai();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    double F64_cai()
    {
        const std::uint64_t low = U32_cai();
        const std::uint64_t bits = low | (static_cast<std::uint64_t>(U32_cai()) << 32);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    // A list of values.
    std::vector<DataValue> Values_cai()
    {
        const std::uint16_t count = U16_cai();
        std::vector<DataValue> out;
        for (std::uint16_t i = 0; i < count && mOk; ++i) {
            DataValue v;
            v.index = U16_cai();
            v.value = F64_cai();
            out.push_back(v);
        }
        return out;
    }
    // `size` raw bytes.
    void Raw_cai(std::uint8_t* out, std::size_t size)
    {
        if (Need_cai(size)) {
            std::copy(mData + mAt, mData + mAt + size, out);
            mAt += size;
        }
    }

private:
    // True when `size` more bytes are there.
    bool Need_cai(std::size_t size)
    {
        if (!mOk || mSize - mAt < size) {
            mOk = false;
            return false;
        }
        return true;
    }
    // The next `size` bytes as text.
    std::string Take_cai(std::size_t size)
    {
        if (!Need_cai(size))
            return {};
        std::string text(reinterpret_cast<const char*>(mData + mAt), size);
        mAt += size;
        return text;
    }

    const std::uint8_t* mData;
    std::size_t         mSize;
    std::size_t         mAt = 0;
    bool                mOk = true;
};

// The type of each alternative of Message, in variant order.
constexpr MsgType kTypes[] = {MsgType::Hello, MsgType::Challenge, MsgType::Proof, MsgType::Welcome, MsgType::Refused,
                              MsgType::Profile, MsgType::ProfileAck, MsgType::People, MsgType::ControlRequest,
                              MsgType::ControlOffer, MsgType::ControlAnswer, MsgType::ControlChanged, MsgType::Bye,
                              MsgType::Notice, MsgType::Command, MsgType::Reject, MsgType::Values,
                              MsgType::Correction, MsgType::Snapshot, MsgType::Priority, MsgType::Position,
                              MsgType::Weather, MsgType::SnapshotRequest, MsgType::Rules, MsgType::FlightInputs,
                              MsgType::Pause, MsgType::CopilotInputs, MsgType::FlightState, MsgType::Load,
                              MsgType::Seats, MsgType::SeatChoice};

} // namespace

// The name of a message type, for the log.
const char* MsgTypeName_cai(MsgType type)
{
    switch (type) {
        case MsgType::Hello:          return "Hello";
        case MsgType::Challenge:      return "Challenge";
        case MsgType::Proof:          return "Proof";
        case MsgType::Welcome:        return "Welcome";
        case MsgType::Refused:        return "Refused";
        case MsgType::Profile:        return "Profile";
        case MsgType::ProfileAck:     return "ProfileAck";
        case MsgType::People:         return "People";
        case MsgType::ControlRequest: return "ControlRequest";
        case MsgType::ControlOffer:   return "ControlOffer";
        case MsgType::ControlAnswer:  return "ControlAnswer";
        case MsgType::ControlChanged: return "ControlChanged";
        case MsgType::Bye:            return "Bye";
        case MsgType::Notice:         return "Notice";
        case MsgType::Command:        return "Command";
        case MsgType::Reject:         return "Reject";
        case MsgType::Values:         return "Values";
        case MsgType::Correction:     return "Correction";
        case MsgType::Snapshot:       return "Snapshot";
        case MsgType::Priority:       return "Priority";
        case MsgType::Position:       return "Position";
        case MsgType::Weather:        return "Weather";
        case MsgType::SnapshotRequest: return "SnapshotRequest";
        case MsgType::Rules:          return "Rules";
        case MsgType::FlightInputs:   return "FlightInputs";
        case MsgType::Pause:          return "Pause";
        case MsgType::CopilotInputs:  return "CopilotInputs";
        case MsgType::FlightState:    return "FlightState";
        case MsgType::Load:           return "Load";
        case MsgType::Seats:          return "Seats";
        case MsgType::SeatChoice:     return "SeatChoice";
    }
    return "?";
}

// The name of a role.
const char* RoleName_cai(Role role)
{
    return role == Role::Observer ? "Observer" : "Crew";
}

// The text of a refusal reason.
const char* RefuseText_cai(RefuseReason reason)
{
    switch (reason) {
        case RefuseReason::ProtocolVersion: return "different Shared Cockpit version";
        case RefuseReason::WrongPassword:   return "wrong password";
        case RefuseReason::SessionFull:     return "the session is full";
        case RefuseReason::OtherAircraft:   return "the host flies another aircraft";
        case RefuseReason::NameInUse:       return "this name is already in the session";
        case RefuseReason::HostNotReady:    return "the host is not ready (no analysis saved)";
        case RefuseReason::BadName:         return "the name is empty or too long";
    }
    return "refused";
}

// The text of a bye reason.
const char* ByeText_cai(ByeReason reason)
{
    switch (reason) {
        case ByeReason::Leaving:       return "left";
        case ByeReason::HostEnded:     return "the host ended the session";
        case ByeReason::Kicked:        return "removed by the master";
        case ByeReason::Refused:       return "refused";
        case ByeReason::Timeout:       return "no answer (timeout)";
        case ByeReason::ProtocolError: return "a message could not be read";
    }
    return "disconnected";
}

// The type of a message.
MsgType TypeOf_cai(const Message& message)
{
    return kTypes[message.index()];
}

// The bytes of a message (header included).
Bytes EncodeMessage_cai(const Message& message)
{
    Bytes out;
    Writer w(out);
    w.U8_cai(kMagic0);
    w.U8_cai(kMagic1);
    w.U8_cai(kProtocolVersion);
    w.U8_cai(static_cast<std::uint8_t>(TypeOf_cai(message)));
    auto lmbda_encode = [&w](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, MsgHello>) {
            w.Str_cai(m.name); w.Str_cai(m.pluginVersion); w.Str_cai(m.acfName); w.Str_cai(m.acfStamp); w.Str_cai(m.profileHash);
        } else if constexpr (std::is_same_v<T, MsgChallenge>) {
            w.Raw_cai(m.nonce.data(), m.nonce.size());
        } else if constexpr (std::is_same_v<T, MsgProof>) {
            w.Str_cai(m.proof);
        } else if constexpr (std::is_same_v<T, MsgWelcome>) {
            w.U8_cai(m.yourId); w.U8_cai(static_cast<std::uint8_t>(m.yourRole)); w.Str_cai(m.hostName);
            w.Str_cai(m.acfName); w.Str_cai(m.acfStamp); w.Str_cai(m.profileHash);
        } else if constexpr (std::is_same_v<T, MsgRefused>) {
            w.U8_cai(static_cast<std::uint8_t>(m.reason)); w.Str_cai(m.text);
        } else if constexpr (std::is_same_v<T, MsgProfile>) {
            w.Str_cai(m.hash); w.LongStr_cai(m.text);
        } else if constexpr (std::is_same_v<T, MsgProfileAck>) {
            w.Bool_cai(m.ok); w.U16_cai(m.missing); w.Str_cai(m.text);
        } else if constexpr (std::is_same_v<T, MsgPeople>) {
            w.U8_cai(static_cast<std::uint8_t>(m.people.size()));
            for (const Person& p : m.people) {
                w.U8_cai(p.id); w.Str_cai(p.name); w.U8_cai(static_cast<std::uint8_t>(p.role)); w.Bool_cai(p.master);
                w.Bool_cai(p.pic); w.U16_cai(p.pingMs); w.U8_cai(p.flags); w.U8_cai(p.seat);
            }
        } else if constexpr (std::is_same_v<T, MsgControlRequest>) {
            w.U8_cai(m.fromId);
        } else if constexpr (std::is_same_v<T, MsgControlOffer>) {
            w.U8_cai(m.fromId); w.U8_cai(m.toId);
        } else if constexpr (std::is_same_v<T, MsgControlAnswer>) {
            w.U8_cai(m.askerId); w.U8_cai(m.answerId); w.Bool_cai(m.accept);
        } else if constexpr (std::is_same_v<T, MsgControlChanged>) {
            w.U8_cai(m.newPic); w.U8_cai(m.oldPic); w.Str_cai(m.reason);
        } else if constexpr (std::is_same_v<T, MsgBye>) {
            w.U8_cai(static_cast<std::uint8_t>(m.reason)); w.Str_cai(m.text);
        } else if constexpr (std::is_same_v<T, MsgNotice>) {
            w.Str_cai(m.text);
        } else if constexpr (std::is_same_v<T, MsgCommand>) {
            w.U16_cai(m.index); w.U8_cai(static_cast<std::uint8_t>(m.phase)); w.U8_cai(m.author);
        } else if constexpr (std::is_same_v<T, MsgReject>) {
            w.U16_cai(m.index); w.U8_cai(m.winner); w.Str_cai(m.text);
        } else if constexpr (std::is_same_v<T, MsgValues>) {
            w.U8_cai(m.author); w.Values_cai(m.values);
        } else if constexpr (std::is_same_v<T, MsgCorrection>) {
            w.U8_cai(m.winner); w.Str_cai(m.text); w.Values_cai(m.values);
        } else if constexpr (std::is_same_v<T, MsgSnapshot>) {
            w.U8_cai(m.part); w.U8_cai(m.parts); w.Values_cai(m.values);
        } else if constexpr (std::is_same_v<T, MsgPriority>) {
            w.U8_cai(static_cast<std::uint8_t>(m.order.size()));
            for (std::uint8_t id : m.order)
                w.U8_cai(id);
        } else if constexpr (std::is_same_v<T, MsgPosition>) {
            const PositionInfo& p = m.position;
            w.U8_cai(m.id); w.F64_cai(p.latitude); w.F64_cai(p.longitude); w.F64_cai(p.elevation); w.F32_cai(p.agl);
            w.F32_cai(p.heading); w.F32_cai(p.pitch); w.F32_cai(p.roll); w.F32_cai(p.groundSpeed); w.Bool_cai(p.onGround);
            w.U8_cai(p.engines); w.U8_cai(p.engineCount);
        } else if constexpr (std::is_same_v<T, MsgWeather>) {
            w.Values_cai(m.values);
        } else if constexpr (std::is_same_v<T, MsgSnapshotRequest>) {
            // no fields
        } else if constexpr (std::is_same_v<T, MsgFlightInputs>) {
            const FlightInputs& f = m.inputs;
            w.U8_cai(m.id); w.U32_cai(m.sequence);
            w.F32_cai(f.yokePitch); w.F32_cai(f.yokeRoll); w.F32_cai(f.yokeHeading);
            w.F32_cai(f.brakeLeft); w.F32_cai(f.brakeRight); w.F32_cai(f.parkingBrake); w.F32_cai(f.speedbrake);
            auto lmbda_array = [&w](const std::vector<float>& values) {
                const std::size_t n = std::min<std::size_t>(values.size(), kMaxEngines);
                w.U8_cai(static_cast<std::uint8_t>(n));
                for (std::size_t i = 0; i < n; ++i)
                    w.F32_cai(values[i]);
            };
            lmbda_array(f.throttle); lmbda_array(f.prop); lmbda_array(f.mixture);
            w.F32_cai(f.trimElevator); w.F32_cai(f.trimAileron); w.F32_cai(f.trimRudder); w.F32_cai(f.flapRequest);
            const std::size_t modes = std::min<std::size_t>(f.propMode.size(), kMaxEngines);
            w.U8_cai(static_cast<std::uint8_t>(modes));
            for (std::size_t i = 0; i < modes; ++i)
                w.U8_cai(f.propMode[i]);
        } else if constexpr (std::is_same_v<T, MsgPause>) {
            w.U8_cai(m.id); w.Bool_cai(m.paused);
        } else if constexpr (std::is_same_v<T, MsgCopilotInputs>) {
            w.U8_cai(m.id); w.F32_cai(m.inputs.pitch); w.F32_cai(m.inputs.roll); w.F32_cai(m.inputs.heading);
        } else if constexpr (std::is_same_v<T, MsgFlightState>) {
            const AircraftState& a = m.state;
            w.U8_cai(m.id); w.U32_cai(m.sequence); w.U32_cai(m.timeMs);
            w.F64_cai(a.latitude); w.F64_cai(a.longitude); w.F64_cai(a.elevation);
            w.F32_cai(a.heading); w.F32_cai(a.pitch); w.F32_cai(a.roll);
            w.F32_cai(a.vx); w.F32_cai(a.vy); w.F32_cai(a.vz);
            w.F32_cai(a.p); w.F32_cai(a.q); w.F32_cai(a.r);
            w.F32_cai(a.agl); w.Bool_cai(a.onGround);
        } else if constexpr (std::is_same_v<T, MsgLoad>) {
            const LoadInfo& l = m.load;
            auto lmbda_list = [&w](const std::vector<float>& values) {
                const std::size_t n = std::min<std::size_t>(values.size(), kMaxLoadValues);
                w.U8_cai(static_cast<std::uint8_t>(n));
                for (std::size_t i = 0; i < n; ++i)
                    w.F32_cai(values[i]);
            };
            w.U8_cai(m.id); lmbda_list(l.fuel); lmbda_list(l.stations);
            w.F32_cai(l.payload); w.F32_cai(l.cgz); w.F32_cai(l.cgx); w.Bool_cai(l.useStations);
        } else if constexpr (std::is_same_v<T, MsgSeats>) {
            const std::size_t n = std::min<std::size_t>(m.seats.size(), kMaxSeats);
            w.U8_cai(static_cast<std::uint8_t>(n));
            for (std::size_t i = 0; i < n; ++i) {
                const SeatInfo& s = m.seats[i];
                w.Str_cai(s.name); w.Bool_cai(s.sitting);
                w.F32_cai(s.x); w.F32_cai(s.y); w.F32_cai(s.z); w.F32_cai(s.psi); w.F32_cai(s.the); w.F32_cai(s.phi);
            }
        } else if constexpr (std::is_same_v<T, MsgSeatChoice>) {
            w.U8_cai(m.id); w.U8_cai(m.seat);
        } else if constexpr (std::is_same_v<T, MsgRules>) {
            const SessionRules& r = m.rules;
            w.U16_cai(static_cast<std::uint16_t>(r.rate)); w.U16_cai(static_cast<std::uint16_t>(r.conflictMs));
            w.U16_cai(static_cast<std::uint16_t>(r.groundM)); w.U16_cai(static_cast<std::uint16_t>(r.headingDeg));
            w.U16_cai(static_cast<std::uint16_t>(r.airM)); w.Bool_cai(r.askOnJoin); w.Bool_cai(r.weather);
            w.U16_cai(static_cast<std::uint16_t>(r.weatherS)); w.Bool_cai(r.time); w.U16_cai(static_cast<std::uint16_t>(r.timeToleranceS));
            w.U8_cai(static_cast<std::uint8_t>(r.flight)); w.Bool_cai(r.copilotAxes);
            w.U16_cai(static_cast<std::uint16_t>(r.smoothingMs)); w.Bool_cai(r.followStrict); w.Bool_cai(r.shareLoad);
            w.U16_cai(static_cast<std::uint16_t>(r.handoverBlendMs));
        }
    };
    std::visit(lmbda_encode, message);
    return out;
}

// A message from bytes, fully checked.
std::optional<Message> DecodeMessage_cai(const std::uint8_t* data, std::size_t size, std::string& error, bool& otherProtocol)
{
    otherProtocol = false;
    if (size < kHeaderSize || data[0] != kMagic0 || data[1] != kMagic1) {
        error = "not a Shared Cockpit message";
        return std::nullopt;
    }
    if (data[2] != kProtocolVersion) {
        otherProtocol = true;
        error = fmt::format("protocol version {} (this plugin speaks {})", data[2], kProtocolVersion);
        return std::nullopt;
    }
    Reader r(data + kHeaderSize, size - kHeaderSize);
    std::optional<Message> out;
    switch (static_cast<MsgType>(data[3])) {
        case MsgType::Hello: {
            MsgHello m; m.name = r.Str_cai(); m.pluginVersion = r.Str_cai(); m.acfName = r.Str_cai(); m.acfStamp = r.Str_cai();
            m.profileHash = r.Str_cai(); out = m; break;
        }
        case MsgType::Challenge: { MsgChallenge m; r.Raw_cai(m.nonce.data(), m.nonce.size()); out = m; break; }
        case MsgType::Proof:     { MsgProof m; m.proof = r.Str_cai(); out = m; break; }
        case MsgType::Welcome: {
            MsgWelcome m; m.yourId = r.U8_cai(); m.yourRole = static_cast<Role>(r.U8_cai()); m.hostName = r.Str_cai();
            m.acfName = r.Str_cai(); m.acfStamp = r.Str_cai(); m.profileHash = r.Str_cai(); out = m; break;
        }
        case MsgType::Refused:    { MsgRefused m; m.reason = static_cast<RefuseReason>(r.U8_cai()); m.text = r.Str_cai(); out = m; break; }
        case MsgType::Profile:    { MsgProfile m; m.hash = r.Str_cai(); m.text = r.LongStr_cai(kMaxProfileBytes); out = m; break; }
        case MsgType::ProfileAck: { MsgProfileAck m; m.ok = r.Bool_cai(); m.missing = r.U16_cai(); m.text = r.Str_cai(); out = m; break; }
        case MsgType::People: {
            MsgPeople m;
            const std::uint8_t count = r.U8_cai();
            for (std::uint8_t i = 0; i < count && r.Ok_cai(); ++i) {
                Person p; p.id = r.U8_cai(); p.name = r.Str_cai(); p.role = static_cast<Role>(r.U8_cai()); p.master = r.Bool_cai();
                p.pic = r.Bool_cai(); p.pingMs = r.U16_cai(); p.flags = r.U8_cai(); p.seat = r.U8_cai();
                m.people.push_back(std::move(p));
            }
            out = m; break;
        }
        case MsgType::ControlRequest: { MsgControlRequest m; m.fromId = r.U8_cai(); out = m; break; }
        case MsgType::ControlOffer:   { MsgControlOffer m; m.fromId = r.U8_cai(); m.toId = r.U8_cai(); out = m; break; }
        case MsgType::ControlAnswer:  { MsgControlAnswer m; m.askerId = r.U8_cai(); m.answerId = r.U8_cai(); m.accept = r.Bool_cai(); out = m; break; }
        case MsgType::ControlChanged: { MsgControlChanged m; m.newPic = r.U8_cai(); m.oldPic = r.U8_cai(); m.reason = r.Str_cai(); out = m; break; }
        case MsgType::Bye:            { MsgBye m; m.reason = static_cast<ByeReason>(r.U8_cai()); m.text = r.Str_cai(); out = m; break; }
        case MsgType::Notice:         { MsgNotice m; m.text = r.Str_cai(); out = m; break; }
        case MsgType::Command: {
            MsgCommand m; m.index = r.U16_cai(); const std::uint8_t phase = r.U8_cai(); m.author = r.U8_cai();
            if (phase > 1) {
                error = fmt::format("Command with phase {}", phase);
                return std::nullopt;
            }
            m.phase = static_cast<CommandPhase>(phase); out = m; break;
        }
        case MsgType::Values:     { MsgValues m; m.author = r.U8_cai(); m.values = r.Values_cai(); out = m; break; }
        case MsgType::Correction: { MsgCorrection m; m.winner = r.U8_cai(); m.text = r.Str_cai(); m.values = r.Values_cai(); out = m; break; }
        case MsgType::Snapshot:   { MsgSnapshot m; m.part = r.U8_cai(); m.parts = r.U8_cai(); m.values = r.Values_cai(); out = m; break; }
        case MsgType::Priority: {
            MsgPriority m;
            const std::uint8_t count = r.U8_cai();
            for (std::uint8_t i = 0; i < count && r.Ok_cai(); ++i)
                m.order.push_back(r.U8_cai());
            out = m; break;
        }
        case MsgType::Position: {
            MsgPosition m; PositionInfo& p = m.position;
            m.id = r.U8_cai(); p.latitude = r.F64_cai(); p.longitude = r.F64_cai(); p.elevation = r.F64_cai(); p.agl = r.F32_cai();
            p.heading = r.F32_cai(); p.pitch = r.F32_cai(); p.roll = r.F32_cai(); p.groundSpeed = r.F32_cai(); p.onGround = r.Bool_cai();
            p.engines = r.U8_cai(); p.engineCount = r.U8_cai();
            out = m; break;
        }
        case MsgType::Weather:         { MsgWeather m; m.values = r.Values_cai(); out = m; break; }
        case MsgType::SnapshotRequest: { out = MsgSnapshotRequest{}; break; }
        case MsgType::FlightInputs: {
            MsgFlightInputs m; FlightInputs& f = m.inputs;
            m.id = r.U8_cai(); m.sequence = r.U32_cai();
            f.yokePitch = r.F32_cai(); f.yokeRoll = r.F32_cai(); f.yokeHeading = r.F32_cai();
            f.brakeLeft = r.F32_cai(); f.brakeRight = r.F32_cai(); f.parkingBrake = r.F32_cai(); f.speedbrake = r.F32_cai();
            auto lmbda_array = [&r](std::vector<float>& values) {
                const std::size_t n = std::min<std::size_t>(r.U8_cai(), kMaxEngines);
                values.resize(n);
                for (std::size_t i = 0; i < n; ++i)
                    values[i] = r.F32_cai();
            };
            lmbda_array(f.throttle); lmbda_array(f.prop); lmbda_array(f.mixture);
            f.trimElevator = r.F32_cai(); f.trimAileron = r.F32_cai(); f.trimRudder = r.F32_cai(); f.flapRequest = r.F32_cai();
            f.propMode.resize(std::min<std::size_t>(r.U8_cai(), kMaxEngines));
            for (std::uint8_t& mode : f.propMode)
                mode = r.U8_cai();
            out = m; break;
        }
        case MsgType::Pause: { MsgPause m; m.id = r.U8_cai(); m.paused = r.Bool_cai(); out = m; break; }
        case MsgType::CopilotInputs: {
            MsgCopilotInputs m; m.id = r.U8_cai();
            m.inputs.pitch = r.F32_cai(); m.inputs.roll = r.F32_cai(); m.inputs.heading = r.F32_cai();
            out = m; break;
        }
        case MsgType::FlightState: {
            MsgFlightState m; AircraftState& a = m.state;
            m.id = r.U8_cai(); m.sequence = r.U32_cai(); m.timeMs = r.U32_cai();
            a.latitude = r.F64_cai(); a.longitude = r.F64_cai(); a.elevation = r.F64_cai();
            a.heading = r.F32_cai(); a.pitch = r.F32_cai(); a.roll = r.F32_cai();
            a.vx = r.F32_cai(); a.vy = r.F32_cai(); a.vz = r.F32_cai();
            a.p = r.F32_cai(); a.q = r.F32_cai(); a.r = r.F32_cai();
            a.agl = r.F32_cai(); a.onGround = r.Bool_cai();
            out = m; break;
        }
        case MsgType::Load: {
            MsgLoad m; LoadInfo& l = m.load;
            auto lmbda_list = [&r](std::vector<float>& values) {
                const std::size_t n = r.U8_cai();
                if (n > static_cast<std::size_t>(kMaxLoadValues)) {
                    r.Fail_cai();
                    return;
                }
                values.resize(n);
                for (float& v : values)
                    v = r.F32_cai();
            };
            m.id = r.U8_cai(); lmbda_list(l.fuel); lmbda_list(l.stations);
            l.payload = r.F32_cai(); l.cgz = r.F32_cai(); l.cgx = r.F32_cai(); l.useStations = r.Bool_cai();
            out = m; break;
        }
        case MsgType::Seats: {
            MsgSeats m;
            const std::size_t n = r.U8_cai();
            if (n > static_cast<std::size_t>(kMaxSeats)) {
                r.Fail_cai();
            } else {
                m.seats.resize(n);
                for (SeatInfo& s : m.seats) {
                    s.name = r.Str_cai(); s.sitting = r.Bool_cai();
                    s.x = r.F32_cai(); s.y = r.F32_cai(); s.z = r.F32_cai();
                    s.psi = r.F32_cai(); s.the = r.F32_cai(); s.phi = r.F32_cai();
                }
            }
            out = m; break;
        }
        case MsgType::SeatChoice: { MsgSeatChoice m; m.id = r.U8_cai(); m.seat = r.U8_cai(); out = m; break; }
        case MsgType::Rules: {
            MsgRules m; SessionRules& x = m.rules;
            x.rate = r.U16_cai(); x.conflictMs = r.U16_cai(); x.groundM = r.U16_cai(); x.headingDeg = r.U16_cai();
            x.airM = r.U16_cai(); x.askOnJoin = r.Bool_cai(); x.weather = r.Bool_cai(); x.weatherS = r.U16_cai();
            x.time = r.Bool_cai(); x.timeToleranceS = r.U16_cai();
            x.flight = static_cast<FlightMode>(r.U8_cai()); x.copilotAxes = r.Bool_cai();
            x.smoothingMs = r.U16_cai(); x.followStrict = r.Bool_cai(); x.shareLoad = r.Bool_cai();
            x.handoverBlendMs = r.U16_cai();
            x.Clamp_cai();
            out = m; break;
        }
        case MsgType::Reject:         { MsgReject m; m.index = r.U16_cai(); m.winner = r.U8_cai(); m.text = r.Str_cai(); out = m; break; }
        default:
            error = fmt::format("unknown message type {}", data[3]);
            return std::nullopt;
    }
    if (!r.Ok_cai() || !r.AtEnd_cai()) {
        error = fmt::format("{} message with a wrong length ({} bytes)", MsgTypeName_cai(static_cast<MsgType>(data[3])), size);
        return std::nullopt;
    }
    return out;
}

// The proof for `password` and `nonce`: a hash of both, as text.
std::string PasswordProof_cai(const std::string& password, const Nonce& nonce)
{
    std::string text = password;
    text.append(reinterpret_cast<const char*>(nonce.data()), nonce.size());
    // Hashed twice with the nonce in between: cheap, and the password is not readable on the wire.
    return Utils::Hash_cai(Utils::Hash_cai(text) + text);
}

// A random nonce.
Nonce MakeNonce_cai()
{
    std::random_device device;
    Nonce nonce{};
    for (auto& byte : nonce)
        byte = static_cast<std::uint8_t>(device() & 0xFF);
    return nonce;
}

} // namespace sc::net
