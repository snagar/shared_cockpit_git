# Shared Cockpit - what to expect (compatibility)

Shared Cockpit lets up to 4 X-Plane simulators share one cockpit. It does this by sharing what
people *do* in the cockpit (clicks, levers, the pilot's hands) and - with **Follow the PIC**, the
default - where the PIC's aircraft is. Every X-Plane still runs its own copy of the aircraft and its
systems. Most of what follows comes from that.

> **Only the PIC flies. A copilot's (Crew's) or observer's joystick, yoke, pedals and throttle
> quadrant do nothing while they are not the PIC: the plugin switches them off so they cannot fight
> the PIC. To fly with your own hardware, take control - become the PIC.**
> **The one exception is the opt-in "copilot axes" (section 4b): it puts one copilot's stick and
> pedals on X-Plane's copilot side - with real risks, read them first.**

This page is for version **0.9.5** (network protocol **9**). Everybody in a session needs the same
protocol - the About box shows it.

---

## 1. Requirements

| | |
|---|---|
| **X-Plane** | X-Plane 12. Moving to a far-away PIC (another airport) needs **12.4 or newer** (`XPLMInitFlight`); nearby moves work on any 12. |
| **Same aircraft** | Everybody loads the **same `.acf`** (same file name). The same version of it is best: a different version is a warning, not a block (controls may be missing). |
| **Same plugin** | The same Shared Cockpit protocol (0.9.5 = 9). Older versions are refused with a clear message. |
| **Analysis** | The host needs a saved analysis of the aircraft (Home, step 1). Joiners use the host's, in memory - their own file is not changed. |
| **Network** | UDP, port **39100** by default. LAN: nothing to do. Internet: ZeroTier or Tailscale (no router changes), or forward the UDP port on the host's router. |
| **People** | Up to **4**: the host and 3 joiners. |
| **2D / VR** | The flight deck window is 2D only. The main window works in VR. |

---

## 2. What is shared, and how

| What | How | Who can change it |
|---|---|---|
| **Switches, buttons, knobs** (ticked in the analysis) | as **commands** (run on every X-Plane) or **values** (written into every X-Plane), 10 updates a second while a knob turns | the host, the PIC and **Crew**; **Observers** are locked |
| **The pilot's hands** - yoke / stick, rudder / pedals, toe brakes, parking brake, speed brake, trims, flap handle, and per engine throttle, prop (= a helicopter's **collective**), mixture, reverse / beta | the **PIC's** X-Plane sends them ~30 times a second; the others write them every frame | **only the PIC** |
| **The PIC's aircraft** (Follow the PIC, the default) | position, attitude, speeds and rotation rates ~30 times a second; every other aircraft is placed there every frame | **only the PIC** |
| **Fuel and payload** | the PIC's fuel per tank and payload: X-Plane 12 **payload stations** when the aircraft has them, otherwise the **total payload** and the CG; on joining, after a move, and when they change | **only the PIC** |
| **Position** | everybody sends it once a second; "**Move to PIC**" is offered when an aircraft is off (with Follow: only when more than 20 km away) | each person moves their own aircraft |
| **Weather and time** | the host's, every 10 / 30 / 60 s (session setting), right away to a new joiner, after a move, and on a change | the host |
| **Pause** | the PIC's pause is everybody's | **only the PIC** |
| **Session settings** | the host's rules (Session settings page) apply to everybody, at once | the host |

**Conflicts:** two people moving the same control within the conflict window (1 s by default):
the host wins, then the PIC, then the priority list on the Connect page. The other one's control
snaps back. Changes further apart: the latest wins.

---

## 3. Flying together - set your expectations

There are two ways (Session settings, the host chooses):

**Follow the PIC (default, recommended).** The PIC's aircraft leads. Its X-Plane sends where the
aircraft is and how it moves about 30 times a second; every other X-Plane places its aircraft there
every frame - moved ahead by the network delay and blended in over the **smoothing** time (50 / 100 /
200 ms), so it looks smooth. The aircraft stay together, on the ground and in the air.

* The followers' flight model keeps running: engine, instruments, sounds and systems work as usual,
  and the PIC's yoke, pedals and levers move theirs. On the ground their gear keeps its own height,
  pitch and roll; only position and heading follow.
* Expect the follower to be **a few tens of milliseconds behind** (LAN) or ~100 ms (internet), and
  a small gap (well under a metre) while manoeuvring. The Connect page shows it live.
* **Strict** (session setting): the followers' flight model is switched off while following - exact
  placement, but nothing of their own physics acts. Try it if following looks jittery.
* If the PIC's state stops arriving for 1 s, a follower **holds**: it flies on its own with the PIC's
  inputs until the state comes back, then closes the gap smoothly (or at once when it is large).
* Farther than **20 km** from the PIC (a joiner at another airport): "Move to PIC" starts a new flight
  there first; then following starts by itself.
* Each X-Plane still runs its own **systems** (fuel burn, electrics, an add-on's plugin): they can
  differ slowly. The PIC's fuel keeps coming (when it changes by more than 1 kg).

**Share the PIC's inputs only.** Every X-Plane runs its own flight model with the PIC's hands. Tests
showed that this does **not** keep two aircraft together: with exactly the same rudder, brakes and
throttle, a follower was slower and turned less (different load, timing, wind and ground friction),
and nothing pulls it back - taxiing, the paths split within a minute. Use it only if you want each
X-Plane to fly by itself; **Move to PIC** brings an aircraft back (on the ground exact to well under a
metre, about 2 seconds with the flight model off).

**Handing over control** (Follow): the new PIC's aircraft simply flies on from where it was, and the
old PIC's starts following - tests showed steps of 0.1-0.6 m in the air, closed gently over half a
second. The new PIC's own yoke and rudder may be anywhere when they take control: the aircraft goes
from the old PIC's yoke and rudder to theirs over **1 s** (session setting: at once / 1 s / 2 s).
Throttle and brakes switch at once - have them roughly where the PIC's are before you take control.

**Who flies:** only the **PIC** (pilot in command). To fly, ask for control (or the master offers it);
the handover is instant.

---

## 4. Your hardware (joystick, throttle quadrant, pedals, trim wheel)

**While you are not the PIC - Crew (copilot) or Observer - your joystick, yoke, rudder pedals and
throttle quadrant are not used: moving them does nothing.** This is on purpose: the plugin switches
your own hardware off where X-Plane allows it, so it cannot fight the PIC's. Take control to fly with it.

| Control | Your hardware while you are not the PIC |
|---|---|
| Yoke / stick, rudder / pedals | **off** (X-Plane's joystick override) |
| Toe brakes | **off** (when X-Plane has the toe brake override) |
| Throttles | **off** (when X-Plane has the throttle override) |
| Collective (helicopters) | **off** (prop pitch override, helicopters only) |
| Prop / mixture levers, trim wheel, flap lever, speed brake lever | **not switched off** - X-Plane has no override for them. The PIC's position is written every frame and wins, but your lever keeps pulling. The Connect page shows "A hardware lever here moves the levers (N times a second) - held". It is harmless, but the lever animation may flicker. **Unbind those axes**, or leave them where the PIC has them. |

When you become the PIC, everything comes back to your hardware at once.

**Two X-Plane copies on one computer** (testing): both copies read the same USB devices. Unbind every
axis and button in the copy that is not the PIC (X-Plane Settings > Joystick - each copy keeps its own
settings), or the levers X-Plane cannot switch off will fight.

---

## 4b. Copilot axes (opt-in) - read the risks

The host can turn on **"Send the copilot's axes too"** (Session settings > Flying together). It asks
for a confirmation that shows these risks first.

**How it works:** one Crew member (not the PIC) binds their hardware to X-Plane's **Copilot Pitch,
Copilot Roll and Copilot Yaw** axes (X-Plane Settings > Joystick). Their stick / yoke and rudder pedals
then act on X-Plane's **copilot side** on every X-Plane in the session, and X-Plane combines both sides
the way the aircraft is built:

* **linked controls** (most aircraft, yokes linked like a Boeing): **the larger deflection wins** -
  inputs are not added;
* **independent fly-by-wire sticks** (Airbus-style): **both inputs are added**.

**Toe brakes and the nosewheel tiller stay with the PIC** - X-Plane documents no copilot side for them.

**The risks:**

* **Two people fly at once.** With linked controls the copilot can overpower the PIC just by pushing
  further; with fly-by-wire sticks two pilots correcting the same bump double the correction.
* **Delay and drift.** The copilot's inputs arrive a little late on the other X-Planes (a few ms on a
  LAN, 30-150 ms over the internet), and every X-Plane flies its own aircraft, which drifts a little.
  Reacting to a slightly different, slightly late picture can start a **pilot-induced oscillation** -
  pitching or rolling back and forth.
* **A stick that is not centred** adds a small constant input for everybody. Calibrate it and give it a
  small dead zone.
* **A lost link** centres the copilot side after 0.5 s; until then the last input stays in.
* **One copilot at a time:** the highest-ranked Crew member who moves their hardware (priority list).
* **Observers** never get the copilot side.

**Agree before the flight who flies.** Where the aircraft has a priority (takeover) button, the PIC can
use it. Leave copilot axes off unless both of you want to fly together.

---

## 5. What a copilot (Crew, not PIC) can do

* Switches, buttons, knobs, radios, autopilot panel - everything ticked in the analysis. **Yes.**
* Throttle, prop, mixture, flap, trim, speed brake **levers**: these follow the PIC's hands, so
  dragging them with the mouse on a non-PIC X-Plane does **not** stick - the PIC's position comes back.
  Flaps through their **commands** (e.g. a key bound to *flaps down*) do work when those commands
  are ticked in the analysis: the command runs on the PIC's X-Plane too, and the handle follows.
* **A copilot's joystick, yoke, pedals and throttle quadrant do nothing** (section 4) - unless the host
  turned on copilot axes and the copilot bound them to X-Plane's Copilot axes (section 4b).
* To fly or work the levers, take control (become the PIC).

---

## 6. Aircraft compatibility

| Aircraft | What to expect |
|---|---|
| **Laminar default aircraft** (Cessna 172, Baron, King Air, ...) | **Best.** Their cockpit controls are X-Plane commands and datarefs the analysis finds directly. |
| **Add-on aircraft without their own systems plugin** (most freeware, many payware GA) | **Good**, like the default aircraft. |
| **Add-ons with their own systems plugin** (study-level airliners, complex GA with a custom FMS, electrical or hydraulic model) | **Partly.** The clicks reach every X-Plane, but the aircraft's own plugin runs separately in each: FMS routes and pages, IRS alignment, timers and internal failures can **drift apart**. Tick what you click; expect to set up the FMS on every X-Plane (or let one person own it). |
| **Helicopters** | Cyclic, pedals and the collective are part of the PIC's hands. Governor and correlator behaviour is computed by each X-Plane. |
| **Aircraft whose plugin writes a control every frame** (animations, computed positions) | **Do not tick** that control in the analysis: every X-Plane would send it all the time. Sign: "N a minute" in the Sharing box climbing while nobody touches anything. |

---

## 7. Not shared (today)

* **FMS / GPS flight plans** and **navigation databases**.
* **Failures** (set them on every X-Plane, or not at all).
* **Fuel and payload**: shared from the PIC (session setting, on by default). Off: load the same on
  every X-Plane before the flight.
* **Views and cameras**, ATC, the map.
* **Liveries**: each person sees their own.
* **Command-only controls for a late joiner**: a switch that is only a command (no value) is not in
  the snapshot a joiner gets; it may differ until somebody uses it.
* **Other people's aircraft**: you sit in the same cockpit; there is no second aircraft to see.

---

## 8. Weather and time

* X-Plane 12 region weather (wind, clouds, visibility, pressure, temperature, rain, turbulence,
  runway friction) and the time and date come from the **host**.
* X-Plane does **not** change its weather at once: a change shows after about **1 to 2 minutes**.
  Sending more often does not make it sooner.
* With **real weather** switched on in a joiner's X-Plane, X-Plane may overwrite the host's values
  (the log shows the weather source). Use manual weather on the joiners.
* The clock is corrected only when it is off by more than 30 s (session setting), so it does not jump.

---

## 9. Network

| | |
|---|---|
| Latency | LAN: 1-15 ms. Internet: depends on distance; over ~300 ms raise the conflict window. |
| Bandwidth | small: the PIC's hands ~3 KB/s and its aircraft ~2.5 KB/s per joiner while flying (less standing still), everything else a few hundred bytes a second. |
| Lost connection | the joiner leaves the session; join again - you get the snapshot and the weather again. |
| Firewall | allow UDP 39100 (or your port) on the host. |

---

## 10. When something is off

* The **Connect page** shows what is shared and what is skipped (hover the lines for the lists).
* Every X-Plane writes `Resources/plugins/shared_cockpit/shared_cockpit.log` (the last 2 are kept);
  X-Plane's `Log.txt` gets only the start line, warnings and errors.
* A debug build of the plugin logs more (every command / value with its full dataref name).
* Send both logs (host and joiner) when reporting a problem.
* **Flight trace:** Home > About > "Write a flight trace" (or `[debug] flight_trace=1` in
  `shared_cockpit.ini`; it applies at once) makes every X-Plane write
  `shared_cockpit_trace.csv` (10 rows a second while in a session): position, the inputs, the control
  deflections, nosewheel angle, brakes, throttle, RPM, and with Follow how far the aircraft was from
  the PIC's and how late its state was. Send the traces of both X-Planes to compare.
* **Plugin log detail:** Home > About > "Plugin log: Normal / Detailed (debug)" (or `[debug]
  log_level=info|debug|default`). Detailed works in Release builds too; saved, applies at once.
