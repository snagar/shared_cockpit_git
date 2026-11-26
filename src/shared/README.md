# shared - code used by more than one screen

A file lives here when two or more screens (or the plugin frame) use it. Nothing here
includes a screen folder, so the arrows always point into `shared/`, never out of it.

| Folder | What it holds | Used by |
|---|---|---|
| `analysis/` | The analysis data model and its engine (see its README) | Home, Analyse, later Connect and sync |
| `store/` | Reading / writing the analysis file (JSON), backups | `analysis/Session`, `net/`, `tools/` |
| `log/` | `Log` - loguru logging, `LOG_INFO(...)` macros; no X-Plane. Everything goes to the plugin log; the plugin mirrors only warnings, errors and its start line (`Mirror_cai`) to `Log.txt` via `XPLMDebugString`; the tools mirror all to the console | everything, `tools/` |
| `net/` | The network: protocol, messages, the ENet thread, the session (people, seats, control) - see its README | later Connect, Flight deck, `tools/sc_net_test` |
| `sync/` | Sharing the controls in X-Plane: `CommandSync` (commands), `DatarefSync` (values), `PositionSync` (position, move to the PIC), `WeatherSync` (weather, time). Plugin only | flight loop |
| `settings/` | `Settings` - shared_cockpit.ini: name, ports, host options, recent addresses (no passwords) | Connect page |
| `ui/` | `Theme` (colours), `Widgets` (buttons, chips, three-state tick, popups), `Fonts`, `ShareCard` | every screen |
| `util/` | `Utils` (paths, files, text - no dependencies), `StopThread` (background thread) | everything |
| `xp/` | `XPlane` - the X-Plane side: user aircraft, folders, version | frame, screens |
