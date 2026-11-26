# Shared Cockpit

X-Plane plugin that will share one cockpit between two X-Plane simulators.
Version 0.1.1 delivers the first step: **analyse an airplane and pick what to share**.

**Before a shared flight, read [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md)** - what is shared, what
is not, and what to expect.

Drawn with [Dear ImGui](https://github.com/ocornut/imgui) through
[ImgWindow](https://github.com/slgoldberg/ImgWindow), like vrconfig_helper.

## Using it in X-Plane

* Menu: **Plugins > Shared Cockpit > Toggle Window**, or the command
  `shared_cockpit/toggle_main_window` (bind it to a key, joystick or VR controller button).
* The window is 780 x 540 by default and can be resized (640 x 480 up to 1600 x 1100).
  Opened in VR, it appears in VR and follows X-Plane in and out of VR.
* **Home** shows the steps of a shared flight. In 0.1.1 only step 1 works; Connect and
  Flight deck are shown, disabled.
* **Step 1 - Analyse airplane & pick what to share**
  1. *Pick the airplane*: the loaded aircraft, or any other `.acf` below X-Plane's
     Aircraft folder (searched in the background, with a filter).
  2. *Read the aircraft files*: reads the `.acf` object list and every OBJ file
     (`ATTR_manip_*` lines) in the background, with progress and Stop. Click areas of the
     same control are merged into one control.
  3. *Review and pick what to share*:
     * the **filter box** shows one row per category (Commands, Datarefs, Can't sync) in
       the *Type* view, or per name space (`sim`, `laminar`, `aerobask`, ...) in the
       *System* view; each pill shows or hides a group; an orange pill `7/8` has unticked
       controls;
     * **Group by Type / System**: two views of the same controls and ticks;
     * group headers have a fold arrow and a three-state tick (all / some / none);
       **All**, **None** and **Fold all** apply to the visible groups;
     * a ticked control is shared with the other pilot; an unticked one is kept local
       (struck through); can't-sync controls (`ATTR_manip_device` touchscreens) are locked;
     * the **Shareability** card predicts, from the files, how well each system can be
       shared (Full, Mostly, Partial, None, Check in sim) and gives the reason.
  4. **Save** writes `<acf name>.shared_cockpit.json` next to the `.acf`, plus a copy in
     `Resources/plugins/shared_cockpit/backups/`. When the aircraft's file is missing but
     the copy exists (an installer replaced the aircraft folder), **Restore backup** puts
     it back. **Read again** keeps your ticks: controls are matched by type and commands /
     datarefs; new controls get the default rules.

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
