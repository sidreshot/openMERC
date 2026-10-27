# Status

Updated: 2026-10-22 05:10

## Milestones

| # | Milestone | State |
|---|---|---|
| M0 | Tooling | done: R-team analysis workspace (private) with the master function inventory; engine base (Yamagi Quake II) builds with MSVC for Win32 x86 |
| M1 | Interfaces | done: game (SPEC-001), renderer (SPEC-002), player (SPEC-003), sound (SPEC-004), Ghoul C++ interfaces (SPEC-010/011) |
| M2 | Data formats | readers for all 29 stock file types; every stock file parses. Remaining UNKNOWN fields listed per spec |
| M3 | Engine | specified. Done: world loading/rendering of all 75 maps, protocol-33 client (joins original servers), GameSpy replacement. In progress: engine core (I1a), Ghoul library (I3), module host + server (I1b), client effects (I5), menus (I6m), view/HUD/input/demos (I6h) |
| M4 | Renderer | specified (SPEC-002, 410–413); 2D/sky/fog/effects (I4a) in progress, Ghoul model drawing (I4b) after I3 |
| M5 | Sound module | done (first version): OMsnd.dll runs inside the original 1.07f executable; backend internals being specified |
| M6 | Player module | specified (SPEC-003, 600–602); implementation I7 in progress |
| M7 | Game module | fully specified (all 10 work packages approved). Script VM library done (G6a). In progress: world core/player/items (G18), AI (G2), roster/helicopters/vehicles (G4), damage/gore/bodies (G3), navigation/bots (G5), props/effects (G7), game modes/Ghoul glue (G9), script wiring (G6b) |
| M9 | Android | deferred until the desktop code is complete |

## Reverse-engineering coverage

**Complete.** Original modules: 13,084 function entries, of which 5,724 are compiler/library code (C runtime, STL,
exception-handling funclets) that needs no reimplementation. All 7,360 functions of the game's own code are covered
(100%: matching public Quake II code, or understood and specified) in all seven modules: engine executable,
renderer, player module, game module and the three sound backends. Claims that could only be checked in the running
game are marked UNVERIFIED in the specs and are verified by the implementation packages' black-box tests.

## Black-box oracle

`D:\Games\SoF-oracle`: vanilla 1.07f (original paks hard-linked, network library restored to the system one).
Client: 504 cvars, 239 commands; dedicated server: 275 cvars, 75 commands.

## Supported game sources (planned)

- installed game directory (GOG 1.07f; retail installs updated to 1.07f);
- original CD image with InstallShield archives;
- repacks are not supported directly: install them, then point OpenMerc at the installed directory.
