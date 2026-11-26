# connect - Step 2: host or join a session

`ConnectPage` - one page of the main window:

* **Top**: Home, the loaded aircraft, the analysis state. A bar per control question (Yes / No, 15 s).
* **Left**: your name, Host | Join.
  * Join: address (or `address:port`), port, password, the last 5 addresses.
  * Host: port, password, max people, joiners start as Crew (automatic) or Observer (I assign seats).
    Needs the loaded aircraft's analysis saved; joining does not (the host's is used).
  * Connecting: progress and Cancel. In a session: a summary, a message box, your addresses (host).
* **Gear** (top bar): the Session settings page (`SessionSettingsPage`): the session's rules - controls
  (update rate, conflict window, joiners start as), flying together (the PIC's inputs; Follow later; opt-in copilot axes, with a risks confirm popup), position ("Move to PIC" limits, ask on join),
  weather and time (share, every 10 / 30 / 60 s, time tolerance). The host edits (changes apply at
  once and go to everybody; saved in `[session]`), anybody while idle (used when hosting), a joiner
  sees the host's values read-only. Reset to defaults.
* **Right**: the people (priority, seat, control, ping, warnings). Prio: 1 the host, then 2, 3, 4 -
  who wins when two people move the same control within 1 s (after the host and the PIC); the
  master moves people up / down. The actions are always visible on every row
  and disabled when they do not apply: Offer (the PIC), seat crew / observer and Remove (the master).
  Ask for control, Take control (the master). "Before you start" / "Before you fly" checks.
  **Flight**: whose yoke, pedals and levers fly my aircraft (and how fresh), a hardware lever being
  held, missing controls, "Paused by the PIC".
  **Position**: how far I am from the PIC (with a "Move to PIC" button while off), and the weather and
  time line (sent by the host / followed by the joiners). After joining, a bar asks once:
  "412 m from Ann (PIC), heading 87° off, engines differ - move there?" Move / Not now.
  **Sharing**: commands and values found here (hover for the skipped ones; listed in Debug), the
  last change, changes a minute, sent / from others / not shared / blocked.
* **Bottom**: the status, Leave / End session, Flight deck (hides this window, 2D only), the last events.

The session lives in `shared/net/SessionHub`, polled from a flight loop: closing the window keeps it.
Loading another aircraft leaves the session. The settings (never the passwords) are saved in
`<plugin folder>/shared_cockpit.ini` (`shared/settings`).
