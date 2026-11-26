# shared/analysis - what an aircraft offers for sharing

| File | What it does | Uses |
|---|---|---|
| `Analysis` | The data model in plain C++ (controls grouped by type, grades, signals) | - |
| `ManipTypes` | Reads `sc_manip_types.ini` (types, refs, "kept local" words) | `Analysis` |
| `ObjScan` | Reads the `.acf` object list and the OBJ files' `ATTR_manip_` lines | `ManipTypes` |
| `Scanner` | Runs `ObjScan` in a background thread with progress / stop | `ObjScan` |
| `Builder` | Scan -> analysis: groups, keeps earlier ticks, grades, explanation text | `ObjScan`, `ManipTypes` |
| `Session` | The analysis being viewed / edited: aircraft, ticks, unsaved changes, save, backup | all of the above, `store/` |

Why it is shared: `Session` is used by Home and Analyse (and later Connect and sync), and it
drives `Scanner` and `Builder`, so the whole engine belongs here.
`Analysis`, `ManipTypes`, `ObjScan`, `Builder` and `store/` make no X-Plane calls: they are
also built into `tools/sc_analyse_test`.
