# shared/net - the network (protocol 9)

No X-Plane and no ImGui here: the plugin and `tools/sc_net_test` use the same code.

| File | What it does |
|---|---|
| `Protocol.h` | Every number both sides must agree on: header, port 39100, channels, limits, timeouts, message / role / reason ids |
| `Messages.h/.cpp` | One plain struct per message, in a `std::variant`; the only code that knows the byte layout |
| `NetThread.h/.cpp` | The background thread that owns ENet (ENet is not thread-safe, so nothing else calls it). Queues: commands in, events out |
| `NetSession.h/.cpp` | The session on the main thread: handshake, people, seats, PIC, notices, kicks, timeouts |
| `SessionHub.h/.cpp` | The plugin's one session + the feed (last 100 events) for every screen; polled by a flight loop |
| `LocalAddresses.h/.cpp` | This computer's IPv4 addresses (LAN / ZeroTier / Tailscale) for the host to tell the others |

```
 main thread                              network thread (NetThread)
 ───────────                              ──────────────────────────
 NetSession ── Send / Disconnect / Stop ──►  command queue ──► ENet host ──► UDP
     ▲                                                              │
     └──── TakeEvents (Connected, Received, Disconnected, Ping) ◄───┘
```

## Connections
* Star: the joiners connect to the host only; the host forwards. Up to 4 people (the host + 3).
* UDP port **39100** by default, set on the host and the joiners. Internet: forward the port on the
  host's router, or use ZeroTier / Tailscale (then use their IP). No NAT punching in v1.
* Channel 0 reliable (everything in v1), channel 1 unreliable (later: the position stream).

## Bytes
`'S' 'C' <protocol> <type>` then the fields. Integers little-endian, text = u16 length + UTF-8,
the profile text = u32 length (at most 4 MB). A message with another protocol number is refused
with "different Shared Cockpit version". Never renumber a message type; change `kProtocolVersion`
when a message changes its fields.

## Handshake
```
 joiner                         host
   │ ── ENet connect ──────────► │   (10 s, then "no answer from the host")
   │ ── Hello ─────────────────► │   name, plugin version, .acf name + stamp, profile hash
   │                             │   checks: name, full, name used, same .acf name
   │ ◄──────────── Challenge ─── │   (only with a password) random nonce
   │ ── Proof ─────────────────► │   hash(password, nonce): the password never crosses the network
   │ ◄────────────── Welcome ─── │   your id, your seat, host name, host .acf + stamp, host profile hash
   │ ◄────────────── Profile ─── │   (only when the hashes differ) the host's analysis text
   │ ── ProfileAck ────────────► │   ok + how many shared controls this aircraft does not have
   │ ◄─────── People + Notice ── │   to everybody
```
Every step has 5 s (the profile 15 s). Refused carries a reason: other version, wrong password,
full, other aircraft, name in use, host not ready, bad name.
* A different **.acf name** is refused. A different **.acf stamp** is allowed: warning flag `acf-version`.
* The **host's profile wins** for the session, in memory only - the joiner's own file is not changed.
  Flags: `host-profile`, `missing-controls`.
* The password check is not strong cryptography: on the internet, a VPN (ZeroTier / Tailscale) is the safe way.

## Seats and control (v1)
* The host is the **master** for the whole session. Changing the master is v2. The host leaving ends the session.
* Joiners start as **Crew**, or as **Observer** when the host chose "I assign seats" - also changeable while
  hosting (for the next joiners; the people already in keep their seats). The master changes seats.
* **PIC** (pilot in command): starts with the host.
  * Request: someone asks, the PIC answers yes / no (15 s, then "did not answer").
  * Offer: the PIC offers, the other person answers.
  * The master takes control at once.
  * The PIC leaves or becomes an observer: control returns to the host.
  * Observers cannot get control.
* The host re-sends People every 2 s (pings). ENet drops a silent link after 5-8 s.

## Sharing controls (step 2)
Protocol 9 (0.9.1; older cannot join): `Rules` carries `handoverBlendMs`.

Protocol 8 (0.9) added:
* `FlightState` (29) = id, sequence, the PIC's clock (ms), latitude / longitude / elevation (f64), true
  heading / pitch / roll, local vx / vy / vz, P / Q / R, height above the ground, on the ground. Only
  from the PIC, **unreliable**, ~30 a second (5 standing still); the host sends it on and drops it from
  anybody else. Used by Follow the PIC.
* `Load` (30) = id, fuel per tank, payload per station (each list at most 32 - more is refused), the
  total payload, CG z / x, "stations used". From the PIC when it changes; the host keeps the last and
  gives it to new joiners (rule `shareLoad`).
* `Rules` carries `smoothingMs`, `followStrict`, `shareLoad`; `flight` is Follow by default.
* The unreliable stream is no longer logged packet by packet: the net thread logs a count once a minute.

Protocol 7 (0.8.3) added:
* `CopilotInputs` = id, pitch, roll, heading (X-Plane's copilot side). From one Crew member who is not
  the PIC, when the session rule `copilotAxes` is on (unreliable); the host keeps one copilot seat (the
  highest rank among those sending in the last second) and sends it on. `Rules` carries `copilotAxes`.

Protocol 6 (0.8) added:
* `FlightInputs` = id, sequence, yoke pitch / roll / heading, toe brakes, parking brake, and per
  engine (at most 16) throttle, prop, mixture. Only from the PIC, on the **unreliable** channel
  (~30 a second while changing, 1 a second when still); the host sends it on and drops it from
  anybody else (a late packet after a handover).
* `Pause` = id, paused. From the PIC when it changes; the host keeps it and gives it to new joiners.
* `Rules` carries `flight` (Inputs / Follow).

Protocol 5 (0.7) added:
* `Rules` = the session's rules (`SessionRules.h`: update rate, conflict window, "Move to PIC" limits,
  ask on join, weather on / every 10-30-60 s, time on / tolerance). The host sends them first on
  admit and to everybody whenever it changes them (they apply at once); a joiner's own rules are
  used only when it hosts. The conflict window is a rule now (was 1 s fixed).

Protocol 4 (0.6) added:
* `Position` = id + latitude, longitude, elevation (f64), height above ground, heading, pitch, roll,
  ground speed (f32), on ground, engines running, engine count. Everybody sends its own every second;
  the host sends each one on to the others. Not logged one by one.
* `Weather` = values (index in `sync::WeatherSync`'s fixed list), host -> everybody every 10 s and on a change.
* `SnapshotRequest` (no fields): a joiner whose aircraft was reloaded (a move to the PIC) asks again;
  the host's own reloaded aircraft gets the session's values from `RequestSnapshot_cai` directly.

Protocol 3 (0.5):
* `Command` = index, Begin/End, author; `Reject` = index, winner, text.
* `Values` = author + (index, double) pairs; `Correction` = winner, text, values (snap back);
  `Snapshot` = part / parts + values (at most 1000 a message); `Priority` = the order of ids.

The host checks each change and sends it to everybody but its author; a lower-ranked one goes
back to its author as `Reject` (commands) or `Correction` (values, with the session's value).
Observers' values are corrected too. It ends the held commands of people who leave. A new
joiner is admitted, then the host's sync engine is asked for a snapshot (`TakeSnapshotRequests_cai`
/ `SendSnapshot_cai`); the host keeps the session's last values for corrections.

* **Priority** decides who wins when two people change the same control within **1 second**
  (more than the network lag): the host first, then the pilot in command, then the priority list.
* The priority list defaults to the join order (host 1, then 2, 3, 4); the master can reorder it
  on the Connect page (not shown on the flight deck).
* Changes more than 1 second apart: the latest one wins. The loser's control snaps to the winner's value.
* Observers never change shared controls.

## Logging
Info: states, every handshake step, refusals, seats, control, notices, connects and disconnects (with the reason).
Debug: every message sent / received (type, size, link) and every packet in the network thread
(`host:` / `join:` in front, channel, size). The test tool: menu 5, or `--debug`.

## Testing
`sc_net_test --selftest` runs a host and joiners in one process and checks every rule above (32 checks).
Two computers: `sc_net_test --host` on one, `sc_net_test --join <ip>` on the other, then the menus.
