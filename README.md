# Shared Cockpit

X-Plane 12 plugin: up to 4 simulators fly one aircraft together. One person is the **PIC** (pilot in
command) and flies; the others are **Crew** (work the shared switches, knobs and - when the host gives
them - levers) or **Observers**. Control is handed over with a click. Version 0.9.12 (beta, network
protocol 11) for Linux, Windows and macOS.

**Before a shared flight, read [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)** - what is shared, what
is not, and what to expect.

Drawn with [Dear ImGui](https://github.com/ocornut/imgui) through
[ImgWindow](https://github.com/slgoldberg/ImgWindow). The libraries and their licenses: Home > About >
Libraries.

## Using it in X-Plane

* Menu: **Plugins > Shared Cockpit > Toggle Window**, or the command
  `shared_cockpit/toggle_main_window`. The main window works in VR too.
* **Step 1 - Analyse airplane & pick what to share** (the host): the plugin reads the aircraft's files,
  finds every clickable control and proposes what to share; a Shareability card grades each system.
  Joiners receive the host's analysis when they connect.
* **Step 2 - Connect**: host (UDP port 39100 by default) or join by address or machine name. Your name
  and addresses show in aqua before hosting. The host's Session settings apply to everybody.
* **Step 3 - Fly** with the small **Flight deck** window: who is PIC, ask for / take control, your seat.
* **Flying together: "Follow the PIC"** - every aircraft stays on the PIC's (the only supported way in
  this beta). The PIC's fuel and payload, the host's weather and time, and pause are shared; the
  crew's real weather is switched off for the session.
* **Handover**: offers and requests wait 25 s, pulse and chime; the new PIC's yoke and rudder blend in
  over 1 s.
* **Crew levers**: on the Connect page the host gives a Crew member levers (throttle, prop, mixture,
  flaps, speed brake, trims, parking brake); they work them, everybody follows. The yoke, rudder and
  toe brakes always stay with the PIC.
* **Seats**: the aircraft's vrconfig teleport hotspots (the host's, for everybody); pick one on the
  Flight deck; commands `shared_cockpit/seat/back_to_my_seat`, `next`, `previous`. Not in VR yet.
* **Home > About**: version, flight trace and plugin log detail (for testing), this
  computer's addresses, the libraries.

## The analysis file

`<aircraft folder>/sc_<acf name>.json`, written by `store/JsonAnalysisStore.cpp`, the only
file that knows JSON. The rest of the plugin uses the plain C++ types of
`analysis/Analysis.h`; another format (e.g. INI) only needs another `AnalysisStore`.

* `file`, `aircraft`, `files` - format, versions, stamps (size;time) to see changes
* `grades` - `source` (predicted), `overall`, one entry per system with its reason
* `signals` - the counts the grades come from (so old files can be graded again)
* `controls` - grouped by manipulator type: `category`, `count`, `items`, one control
  per line: `share` (true / false / null = can't share), `refs`, `tip`, `file`

Comments are allowed when reading, and lost when the plugin saves.

## Rules for manipulator types: sc_manip_types.ini

Shipped next to the plugin. To change it, copy it to `custom_sc_manip_types.ini` in the same
folder and edit the copy (used instead of the shipped one). It lists every
`ATTR_manip_` type (parameters, where the commands / datarefs are, `kind` = command,
dataref or device), the types that are counted as "not used" (`none`, `noop`, `wheel`,
`keyframe`), and the words that make a control start unticked (`[local_by_default]`:
popups, brightness, volume, EFB, armrests, ...).

## Layout

One folder per screen; code that several screens use is in `src/shared/`. Every folder has a
`README.md` saying what is in it and who uses it.

```
CMakeLists.txt
src/
  shared_cockpit.cpp   plugin entry points, command, menu, VR handling
  PluginWindow         the main window: shows one screen at a time
  SharedInfo.h         fixed information: version, names, window sizes, file names
  Globals              runtime values (folders, X-Plane version, VR)
  home/                Home screen (HomePage)
  analyse/             Step 1 screen (AnalysePage, AircraftFinder)
  connect/             Step 2 screen (later)
  flightdeck/          the small in-flight window (later)
  shared/
    analysis/          data model + engine: Analysis, ManipTypes, ObjScan, Scanner, Builder, Session
    store/             AnalysisStore (interface, paths, backup), JsonAnalysisStore
    log/               Log (loguru + fmt)
    ui/                Theme, Widgets, Fonts, ShareCard
    util/              Utils (no dependencies), StopThread
    xp/                XPlane helpers
tools/
  sc_analyse_test.cpp  command-line test: scan, grade, JSON round trip, merge
libs/
  imgui, ImgWindow, loguru, fonts, GL (Windows glext.h), fmt, json, enet, XPSDK.zip,
  sc_manip_types.ini
```

## Build

The same commands on Linux, Windows and macOS (CMake 3.20+, a C++20 compiler):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
cmake --install build --prefix "<X-Plane>/Resources/plugins"
```

Before the first build, unpack `libs/XPSDK.zip` (SDK 4.4.0) so that its `SDK/` folder becomes
`libs/xpsdk/` (or pass `-DXPSDK_DIR=<folder>`). Without it, CMake stops with an error.
After each build the plugin is also copied into `~/xp12`, `~/xp12d`, `~/xp12b` and
`~/xp12steam` when they exist (`-DSC_DEPLOY=OFF` to stop it,
`-DSC_DEPLOY_XP_ROOTS="a;b"` for other folders). `-DSC_BUILD_TOOLS=ON` also builds
`sc_analyse_test`:

```
build/sc_analyse_test libs/sc_manip_types.ini out.json "<aircraft>/<name>.acf"
```

## Code standards

1. Text formatting with `fmt::format`. Networking with ENet 1.3.18 (`libs/enet`, built from source with the plugin).
2. Logging with loguru through `Log.h` (`LOG_INFO`, `LOG_WARN`, ...); the last 2 logs are kept.
3. Windows drawn with ImgWindow / ImGui, styled like vrconfig_helper.
4. Every function we write ends with `_cai`. Exceptions: names X-Plane or ImgWindow
   require (`XPlugin*`, `beforeBegin`, `buildInterface`), constructors, destructors,
   operators and `main`.
5. Shared values: `src/SharedInfo.h` (fixed) and `src/Globals.h` (runtime).
6. Shared helpers with no dependencies: `src/shared/util/Utils.h`.
   Folders: one per screen; code used by two or more screens goes into `src/shared/`.
7. Screens are designed first, then coded.
8. Every function has a short description above it.
9. Packages are named `shared_cockpit_{version}.{iteration}.zip`.
10. Lambdas are named `lmbda_{name}` (and log with the `LOG_*_AS` macros).

Designed by Snagar.Dev, coded by Claude.Ai. License: Free.
