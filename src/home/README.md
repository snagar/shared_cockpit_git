# home - the Home screen

| File | What it does |
|---|---|
| `HomePage` | The step buttons (Analyse, Connect, Flight deck), the aircraft card, the shareability summary, the session strip, the About popup |

Uses: `shared/analysis/Session` (the analysis of the loaded aircraft), `shared/ui/ShareCard`,
`shared/ui/Widgets`, `shared/xp/XPlane` (which aircraft is loaded).
Opened by: `PluginWindow`. Its Analyse button asks `PluginWindow` to show `analyse/AnalysePage`.
