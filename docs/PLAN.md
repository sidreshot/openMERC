# OpenMerc plan

Goal: a GPL engine that runs the *Soldier of Fortune* (2000) data supplied by the user (installed game
directory or CD image), plays single player and online with the existing PC community (protocol of
version 1.07f), and builds for Windows, Linux, macOS and Android.

## Why module-by-module

The original game is split into modules with stable interfaces:

| Module | Role | Interface |
|---|---|---|
| `SoF.exe` | engine: filesystem, console, network, client, server, Ghoul model system | exports a small API to sound modules; loads the others |
| `ref_gl.dll` | renderer | `GetRefAPI` |
| `Defsnd.dll` / `EAXSnd.dll` / `A3Dsnd.dll` | sound backends | `S_*` exports |
| `player.dll` | first-person weapons, shared player code | `GetPlayerClientAPI`, `GetPlayerServerAPI` |
| `base/gamex86.dll` | game logic: entities, AI, scripts | `GetGameAPI` |

Once an interface is specified, the matching OpenMerc module can be swapped in against the user's own
copy of the other original modules (32-bit Windows build). Each replacement is then checked against the
original module in the same setup — the original is the oracle. When every module is replaced, the
32-bit/x86 constraint goes away and the engine is built for 64-bit and ARM targets.

## Replacement order

Small modules first, each one tested inside the original game before the next:
sound backend → player module → renderer → game module → engine. Every replacement is a 32-bit
Windows DLL with exactly the specified interface, so the user's original executable can load it.

Verification at module boundaries: a tracing proxy (written from the interface spec only) sits
between the engine and a module, records every call in both directions with arguments and results,
and the same scripted session is replayed with the original module and with ours. Traces, console
output, screenshots and network captures are compared. Only black-box behaviour is compared; no
original code is involved on the clean side.

## Milestones

| # | Milestone | Done when |
|---|---|---|
| M0 | Tooling | static analysis of all binaries (R team); function inventory with subsystem tags; base engine builds |
| M1 | Interfaces | specs for every module interface (game, ref, sound, player, Ghoul services) |
| M2 | Data formats | specs + readers for every file type in the stock paks; every stock file parses; viewers |
| M3 | Engine | OpenMerc engine runs the original modules: menus, single player, connect to a 1.07f server |
| M4 | Renderer | own renderer (GL 3.3 / GLES 3) replaces `ref_gl.dll`; screenshot diff against the original |
| M5 | Sound | own OpenAL Soft backend (EAX environments → EFX) replaces the sound modules |
| M6 | Player | own `player` module; weapon/view behaviour matches recorded traces |
| M7 | Game | own game module: entities, AI, navigation, scripts, gore; campaign completable; bots |
| M8 | Portable | 64-bit clean, Linux/macOS builds, no original module needed |
| M9 | Android | APK: data import from directory or ISO, GLES 3, touch controls, gyro, gamepad, online play |

Order: M9 starts only after M8 — first a complete open-source engine and game on desktop.

| # | Milestone | Done when |
|---|---|---|
| M10 | Visual upgrades (optional, all switchable, "classic" look stays default) | widescreen FOV/HUD scaling, MSAA/SMAA, anisotropic filtering; per-pixel dynamic lights, soft particles, bloom, fog volumes from the BSP volume lump, water reflection/refraction, SSAO; later shadows and normal/specular maps |

Texture packs: OpenMerc never ships original or upscaled original textures (derivative works of the
original art). It loads packs the user installs, and may offer a local upscaling step at import time that
runs on the user's machine from the user's own copy. Only newly created, freely licensed art may be bundled.

## Inventory

Reverse-engineering coverage is tracked privately by the reverse team (function → subsystem → spec id).
The public number is the share of functions covered by approved specs, reported per module in
`docs/STATUS.md`.
