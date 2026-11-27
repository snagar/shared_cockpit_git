# shared/sync - sharing the controls (uses X-Plane)

Step 2 of Shared Cockpit. The rules (who may, who wins, held commands of people who leave)
are in `shared/net/NetSession` (no X-Plane); this folder applies them to X-Plane.

| File | What it does |
|---|---|
| `CommandSync.h/.cpp` | Stage 1: the shared **commands** |
| `DatarefSync.h/.cpp` | Stage 2: the shared **datarefs** - values, throttled while moving, snapshot for joiners |
| `PositionSync.h/.cpp` | 0.6: everybody's **position**, and the move to the PIC |
| `WeatherSync.h/.cpp` | 0.6: the host's **weather and time** for everybody |
| `FlightSync.h/.cpp` | 0.8: **flying together** - chooses the way (session rule), knows who is PIC, **pause** |
| `FlightFollow.h/.cpp` | 0.9: **Follow the PIC** (the default) - the others' aircraft are placed where the PIC's is, every frame |
| `FlightInputs.h/.cpp` | 0.8: the PIC's **yoke, rudder, brakes, levers** for everybody (way "inputs only", and inside Follow) |
| `LoadSync.h/.cpp` | 0.9: the PIC's **fuel and payload** (payload stations / total + CG) |
| `FlightTrace.h/.cpp` | 0.8.4 (debug): `[debug] flight_trace=1` - a CSV of the flight, 10 rows a second |

## Commands
* The list: `analysis::SharedCommands_cai` - the commands of the ticked command-type controls,
  unique and sorted. Everybody builds it from the session's analysis (the host's), so a command
  travels as its index (2 bytes).
* A handler (before X-Plane's own) on each command found in this X-Plane:
  * pressed here by crew: runs here at once, Begin / End go to the host
  * pressed here by an observer: consumed (nothing happens), "the controls are locked"
  * someone else's: `XPLMCommandBegin` / `XPLMCommandEnd` while `mApplying` is set, so the
    handler lets it through and does not send it back (no echoes). A held button stays held.
* Within 1 s on the same command the higher rank wins (host, PIC, then join order); the
  host does not send a lower-ranked press on and tells its author ("Your X was not shared").
  A command has no value, so the author's own press is not undone.
* Not in this X-Plane (another version of the aircraft): skipped and listed.
* The session ends / someone leaves: every command still held for them is ended.

## Datarefs (values)
* The list: `analysis::SharedDatarefs_cai` - the datarefs of the ticked dataref-type controls
  (`name[3]` = one element of an array), unique and sorted; a value travels as index + double.
* Each frame every usable entry is read and compared with its last known (session) value
  (ints exactly, floats with a 1e-5 relative tolerance):
  * changed here by crew: sent to the host - at most the session's rate (5 / 10 / 20, Session settings) updates a second per
    control while it moves (default 10), and always its final value
  * changed here by an observer: written back at once (locked)
* Values from the host are written and read back (X-Plane may round or clamp), and that becomes
  the last known value, so they are not sent back (no echoes):
  * `Change`: someone else moved it (the feed: "Bob: FUEL PUMP ON [1] = 1")
  * `Correction`: someone ranked higher moved it within 1 s - mine snaps back
  * `Snapshot`: the session's values when I joined (the host reads its own cockpit)
* Skipped and listed: not in this X-Plane, read-only here, or a type we do not share (byte data).
* Command-only controls are not in the snapshot (a command has no value): a late joiner's
  command-driven switches may differ until someone uses them.

## Position and the move to the PIC (0.6)
* Everybody sends its position once a second. Each simulator compares its own with the PIC's:
  on the ground farther than 5 m, the heading more than 3° off, or the engines differ; in the air
  farther than 250 m (session rules, Session settings page) -> "Move to the PIC" is
  offered. Right after joining it asks once; later only the buttons show (Connect page, deck).
* **Direct move** (under 20 km, same engine state): the flight model is switched off
  (`sim/operation/override/override_planepath[0]`), the position (`local_x/y/z`, the ground under
  it probed plus this aircraft's height on its gear, acf_h_eqlbm), attitude (`psi/theta/phi`, `q`) and speeds are
  written every frame for 2 s (the ground re-probed), then the flight model is switched on again; the next 3 s the lowest height above ground and the highest g-load are logged. The cockpit is not touched.
  In the air the PIC's position is moved ahead by its speed and the position's age.
* **New flight** (farther, or the engines differ): `XPLMInitFlight` (X-Plane 12.4, looked up with
  `XPLMFindSymbol` so older versions still load the plugin) with `lle_ground_start` /
  `lle_air_start` and the PIC's engine state. The aircraft reloads: the session does not end
  (it is our own reload), the sync engines **hold** (the aircraft's start-up changes are not
  sent), 3 s after the reload the snapshot is asked again, written, and the hold ends.
  Before 12.4: in the air `XPLMPlaceUserAtLocation`; on the ground a message to move by hand.
* After a move the distance is measured again and written to the feed ("0.4 m away").
* With Follow (the default) a move is needed only farther than 20 km (`kFollowMaxM`): following closes
  anything nearer by itself; the "Move to PIC" limits apply to "inputs only".

## Weather and time (0.6)
* The host reads a fixed list (X-Plane 12 `sim/weather/region/...`: visibility, pressure,
  temperature, rain, friction, variability, thermals, 13 wind / shear / turbulence / dewpoint /
  temperature layers, 3 cloud layers; `sim/time/zulu_time_sec`, `local_date_days`) and sends it
  to a new joiner at once, every 10 / 30 / 60 s (session rule, default 60 - X-Plane rebuilds its
  weather about once a minute) and when the weather changes (at most every 10 s). Joiners write
  it as the session's rules say (weather, time), then set `update_immediately`; after a move they
  write the last one again (the clock moved on by its age). The clock is written only when off by
  more than the tolerance (30 s).
* To check in tests: with real weather on a joiner, X-Plane may overwrite the values - the log
  prints the weather source; skipped datarefs are listed (Connect page tooltip, log).

## Flying together (0.8)
`FlightSync` runs one `FlightMode` - the session rule `flight` decides which:
* **Inputs** (`FlightInputs`): every X-Plane flies its own flight model. The PIC's X-Plane sends its
  yoke pitch / roll, rudder (`sim/joystick/yoke_*_ratio`), toe brakes, parking brake, the speed brake
  handle (`speedbrake_ratio`) and, per engine, throttle / prop / mixture (`sim/cockpit2/engine/actuators/
  *_ratio` arrays, acf_num_engines elements). A helicopter's collective is X-Plane's prop lever
  (`prop_ratio`); on a helicopter (`sim/aircraft2/metadata/is_helicopter`) the follower also switches on
  `override_prop_pitch` (not on aeroplanes: it would stop the prop governor). Also the elevator /
  aileron / rudder trims, the flap handle (`flap_handle_request_ratio`) and per engine `prop_mode`
  (feather / normal / beta / reverse - written only when it differs).
  The others write them every frame with their own hardware off: `override_joystick`,
  `override_toe_brakes` and `override_throttles` (+ `ENGN_thro_use`) when this X-Plane has them; prop
  and mixture are written every frame and a hardware lever moving them back is counted ("held N a
  second"). Becoming the PIC gives the hardware back. These datarefs are not shared as values by
  `DatarefSync` (`OwnsDataref_cai`). The aircraft still drift a little: "Move to PIC".
* **Copilot axes** (opt-in session rule `copilotAxes`, protocol 7): one Crew member (not the PIC) binds
  their hardware to X-Plane's Copilot Pitch / Roll / Yaw. Their X-Plane leaves its copilot side to it;
  the plugin reads `sim/cockpit2/controls/yoke_*_ratio_copilot` and sends it (unreliable, 30 a second
  while moving, every 250 ms when still). Every other X-Plane sets `override_joystick_*_copilot` and
  writes it; X-Plane combines the sides (linked: the larger deflection wins; fly-by-wire: added).
  Older than 0.5 s: centred. The host keeps one copilot seat (highest rank among those sending).
  The Session settings page shows the risks in a confirm popup before it turns on.
* **Follow** (`FlightFollow`, 0.9, the default): the PIC's X-Plane sends its aircraft (`AircraftState`:
  lat / lon / elevation, true heading / pitch / roll, local speeds, P / Q / R, on the ground) ~30 times a
  second (5 standing still) with its own clock; the others take the newest (older sequence numbers are
  dropped), work out the angle rates from the PIC's clock, move it ahead by its age + the network delay
  (`PicDelayMs_cai`, half the pings; at most 0.5 s) and close the gap each frame by 1 - e^(-frame /
  smoothing). Over 30 m on the ground / 300 m in the air: placed at once. The flight model keeps
  running: the PIC's speeds and rates are written too; on the ground the gear keeps the follower's
  height, pitch and roll (strict: `override_planepath` on, everything placed, the height from a terrain
  probe + `acf_h_eqlbm`). No state for 1 s: holding (flying on its own). Farther than 20 km: not
  followed ("Move to PIC"). A move by PositionSync pauses following. The PIC's inputs still come over:
  `FlightFollow` runs a `FlightInputs` inside.
* **Handover** (0.9.1, 0.9.2): the new PIC's `FlightInputs` blends from the old PIC's yoke pitch / roll
  and rudder to its own hardware over `handoverBlendMs` (rule; 0 / 1000 / 2000, smoothstep).
  `override_joystick` stays on; the hardware is read from `sim/joystick/joy_mapped_axis_value[1..3]`
  (-1..1). 0.9.1 probed one frame with the joystick free, but X-Plane had not written `yoke_*_ratio`
  yet, so the blend held the old values and stepped at its end. One frame after the blend a log line
  compares the yoke with the blend's end value. The old PIC (now following) closes the gap over at
  least 0.5 s for 1.5 s.
* **Load** (`LoadSync`, 0.9, rule `shareLoad`): the PIC reads `m_fuel`, `m_stations` (X-Plane 12),
  `m_fixed` and the CG offsets every 2 s and sends them when they changed (1 kg fuel, 0.5 kg payload,
  1 cm CG) or it became the PIC; the host keeps the last for new joiners. The PIC's stations holding
  weight: written into the follower's stations (X-Plane works out the total and CG); otherwise the
  follower's stations are emptied and the total + CG written. Written again after a move.
* **Pause**, both ways: the PIC's `sim/time/paused` goes to everybody (on a change, and to new
  joiners); the others pause / resume to match (`sim/operation/pause_on` / `pause_off`). Anybody else's
  pause command (key, menu, button) is stopped before X-Plane sees it - when it would take that X-Plane
  away from the PIC's state - with "Only the PIC pauses the flight". A pause from anywhere else is undone.

## Who wins (both engines)
Within 1 s on the same control the lower rank wins: 0 the host, 1 the PIC, then 2 + the place
in the priority list (join order; the master reorders it with up / down on the Connect page).
Changes more than 1 s apart: the latest wins.

## Watch out (to check in tests)
Aircraft plugins or scripts that fire shared commands themselves (on every simulator) would be
shared too - each simulator would send the same command. The same for datarefs an aircraft plugin
writes every frame (animations, computed positions): they should not be ticked in the analysis -
"N a minute" in the Sharing box climbing with nobody touching anything points to one.
The log's Debug lines show who sent what (and the full dataref name).

* **Seats** (`SeatSync`, 0.9.6): the loaded aircraft's vrconfig teleport hotspots
  (`analysis/VrSeats`), re-read when the aircraft or the file changes; the host's go to everybody.
  Picking one writes `sim/graphics/view/pilots_head_x/y/z/psi/the/phi` in the 3D cockpit view
  (switching to it first; applied for 3 frames). Not in VR. Commands: back_to_my_seat, next, previous.
