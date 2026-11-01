# SnowDesk

**[Download SnowDesk-Setup-1.1.2.exe](https://github.com/jbosnakis/SnowDesk/releases/latest/download/SnowDesk-Setup-1.1.2.exe)**
for Windows 10 and 11. Run it and follow the wizard: no administrator rights needed. The same setup is
[`SnowDesk-Setup.exe`](SnowDesk-Setup.exe) in this repository, so it also comes with a clone or a ZIP download. Older
versions are under [Releases](https://github.com/jbosnakis/SnowDesk/releases).

Snow for the Windows desktop. It falls behind your windows and keeps piling up there, settles on the real
shapes of desktop icons, on the taskbar and on the top edges of windows, and reacts to the mouse and to anything
you move. Winds change sides and grow stronger at night, the sun thaws patches during the day, and children
come out to play wherever there is snow and room: snowmen and snowball fights on the ground or on top of a window,
and climbing onto icons, from where they only come down onto snow. With none below they hop to the next icon, or
gather an armful, throw it down and jump after it; that heap cannot be swept away until they have landed.

Snow builds up the way it really does: flakes melt on contact at first, after a couple of minutes it takes hold in the
coldest patches, and those spread into a blanket over about half an hour. Settling speed in the panel changes the pace.

Low CPU mode in the panel runs only soft, steady flakes, with no snow settling, wind, children or mouse effects.

SnowDesk lives in the notification area: click the snowflake for the settings panel, right-click
for pause, clearing the snow and exit. Everything in the panel applies at once and is saved to
`%APPDATA%\SnowDesk\settings.ini`.

## Build

`tools\build.ps1` (Visual Studio 2022 with C++ tools, the Windows 10/11 SDK and Inno Setup 6). Output:
`build\SnowDesk.exe`, a single file with no dependencies, and `build\setup\SnowDesk-Setup-<version>.exe`, the setup
wizard, also copied to `SnowDesk-Setup.exe` at the top of the repository. The setup installs for the current user with no administrator rights, closes a running copy first, and its
uninstaller keeps `%APPDATA%\SnowDesk\settings.ini`.

`tools\build.ps1 release` builds only the exe. `tools\build.ps1 dev` builds `build\dev\SnowDesk.exe` with the developer
switches below, which the release build does not contain; the tools use the dev build. `tools\build.ps1 noauto` builds
`build\noauto\SnowDesk.exe` without the Start with Windows option.

## Security

The app has no network access, no installer and needs no administrator rights. The release build uses Control Flow
Guard, EH continuation metadata, CET, ASLR and DEP, and loads the DLLs it depends on from System32 only.
Start with Windows is a `SnowDesk.lnk` shortcut in the user's Startup folder, nothing in the registry.
`tools\fuzz.bat` runs the simulation under AddressSanitizer, `tools\attack-test.ps1` attacks a dev build (settings files,
arguments, window messages, planted DLLs; `tools\plant-test.bat` builds its harmless DLL), `tools\inspect-exe.bat` lists
protections and sensitive imports, and `tools\sign.ps1` signs the exe once a code signing certificate exists.

## Layout

| File | Purpose |
|---|---|
| `src/world.*` | Snow simulation for one monitor: cells, flakes, thrown snow. No Win32. |
| `src/cursor.*` | Snow collected on the pointer. No Win32. |
| `src/weather.*` | Wind, gusts, snowfall and sun, driven by the clock. No Win32. |
| `src/playground.*` | The children, their scenes, snowmen and snowballs. No Win32. |
| `src/render.*` | Direct3D 11 + DirectComposition transparent overlay and shaders. |
| `src/scene.cpp` | Windows, taskbar, fullscreen detection, moved windows and icons. |
| `src/icons.*` | Desktop icon positions and silhouettes, read from Explorer on a worker thread. |
| `src/pointer.cpp` | Mouse tracking, pushing snow, icon drags. |
| `src/panel.cpp` | The settings window, drawn with Direct2D. |
| `src/settings.*` | The settings and their file. |
| `src/shell.cpp` | Tray icon, system messages, applying settings. |
| `src/diagnostics.cpp` | Developer switches below. |

## Developer switches

`--log FILE` stats every second · `--fast N` N× the flakes · `--test-window` scripted window ·
`--sim-cursor` scripted pointer instead of the real mouse · `--ignore-fullscreen` · `--dump-mask FILE` ·
`--quit-after SECONDS` · `--settings FILE` use this settings file · `--panel` open the panel at start · `--hour H` pretend it is this time of day · `--scene snowman|fight|climb` start with this scene

`tools/playtest.bat` runs the children's rules on a made-up desktop, with no window and no mouse.
`tools/panel-test.ps1 FOLDER` drives the panel with window messages and checks that options take effect.
`tools/shot.ps1` captures the screen, `tools/align_mask.ps1` measures the icon mask against a clean screenshot,
`tools/stop.ps1` closes a running copy cleanly (needed before rebuilding, the exe is locked while it runs).
`tools/cpu-bench.ps1` compares builds in turns by CPU cycles, `tools/cpu-threads.ps1` splits them by thread. Use these,
not `TotalProcessorTime`: Windows charges that per clock tick, which misreads a program waking 120 times a second.
