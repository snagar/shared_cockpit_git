# flightdeck - the small window used while flying (2D)

`FlightDeckWindow` (an ImgWindow, self-decorated, the whole window drags) shows a
`FlightDeckPanel` (the content, drawable without a window - the UI tests use it).

| Size | About | Shows |
|---|---|---|
| Minimal | 136 x 36 | the light, ping (joiner) or people count (host), the size button |
| Line | 310 x 68 | the light, "Ann's session · PIC Bob", size + close; the open question with ✓ ✗, or the last event |
| Small | 340 x 146 | the title, PIC + your seat, Ask for control / Take (master), the question or the last event |

* The **light** is a button: it shows / hides the main window (on the Connect page).
  Green = in a session, orange = connecting, red = ended, grey = not connected.
* A **control question** grows the deck to Small until it is answered or expires.
* **Position (0.6)**: a pin button appears next to the size button, in every size, only while my
  aircraft is off the PIC's position (on the ground more than 5 m / 3°, or other engine state;
  in the air more than 250 m). Pressing it moves me to the PIC. The question after joining
  ("412 m from Ann: move?" ✓ ✗) grows the deck to Small like a control question.
* Opened by the Connect page's "Flight deck (hides this window)", Home step 3, the menu
  Plugins > Shared Cockpit > Toggle Flight Deck (2D), or the command `shared_cockpit/toggle_flight_deck`.
* 2D only: entering VR hides the deck and shows the main window. Closing the deck keeps the session.
* The size and the position are saved in `shared_cockpit.ini`, section `[flightdeck]`.
* Window actions asked for while a window draws (open the deck, toggle the main window, resize)
  run from the plugin's flight loop, never inside another window's ImGui frame.

Seat row (0.9.6, Small size): a list of the seats (who sits where) and Back to my seat; the
buttons act from the flight loop (SeatSync).
