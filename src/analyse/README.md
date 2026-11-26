# analyse - Step 1: analyse the airplane and pick what to share

| File | What it does |
|---|---|
| `AnalysePage` | Pick the airplane, read its files, the filter box, Type / System views, Show filter, ticks, Save / Discard, the "What does this mean?" popup |
| `AircraftFinder` | Background search for every `.acf` below X-Plane's Aircraft folder ("Other aircraft...") - only this screen needs it |

Uses: `shared/analysis/Session` (it starts the reading, holds the ticks, saves), `shared/analysis/Builder`
(the explanation text), `shared/ui/ShareCard`, `shared/ui/Widgets`.
Opened by: `PluginWindow`; its Home button goes back to `home/HomePage`.
