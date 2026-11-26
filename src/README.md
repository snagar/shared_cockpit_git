# src - the plugin frame

The files directly in `src/` start the plugin and hold the window; each screen has its own
folder, and code that several screens use is in `shared/`.

| File | What it does | Used by |
|---|---|---|
| `shared_cockpit.cpp` | X-Plane entry points (`XPlugin*`), menu, toggle command, VR in/out, aircraft loaded | X-Plane |
| `shared_cockpit.sym` | Linux: exports only the five entry points | CMake |
| `PluginWindow` | The main ImgWindow; shows one screen at a time (Home, Analyse) | `shared_cockpit.cpp` |
| `SharedInfo.h` | Fixed values: version, names, window sizes, file names | everything |
| `Globals` | Runtime values: plugin / X-Plane / Aircraft folders, X-Plane version, VR | everything |

How the pieces connect:

```
X-Plane -> shared_cockpit.cpp -> PluginWindow -> home/HomePage
                    |                         -> analyse/AnalysePage
                    +-- owns the shared/analysis/Session that both screens use
```
