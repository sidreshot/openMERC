# OpenMerc architecture

## Principle: same module boundaries as the original

OpenMerc keeps the original's split into an engine and five module kinds and implements exactly the
specified interfaces between them:

| Boundary | Spec | OpenMerc side |
|---|---|---|
| engine ↔ game module | SPEC-001 (+ SPEC-010/011 Ghoul objects) | `engine/` provides the 99-entry import table; `game/` provides the export table |
| engine ↔ renderer | SPEC-002 | `engine/src/client/refresh/gl3` built as a renderer module with the SoF export/import tables |
| engine ↔ player module | SPEC-003 | `player/` |
| engine ↔ sound backend | SPEC-004, SPEC-AMEND-001 | `modules/sound` (OMsnd, done) |
| Ghoul model system (engine-owned, used by all) | SPEC-010…014, SPEC-120/121 | `engine/src/ghoul/` (C++) |

Consequences:
- In the 32-bit Windows build every OpenMerc module can run against the user's original modules, and the
  OpenMerc engine can host original modules. This is the main test method (see PLAN.md): swap one module,
  compare behaviour with the original.
- On 64-bit and non-Windows targets the same source compiles natively; binary compatibility with original
  modules is then irrelevant and not required. Interfaces are declared in shared headers with fixed-width
  types where the 32-bit layout matters (savegames, network, files) and native types elsewhere.
- C++ interfaces (Ghoul) are declared as abstract classes with methods in the specified vtable order. MSVC x86
  `thiscall` gives binary compatibility; other compilers just need source compatibility.
- Interfaces passing an MSVC 6 `std::string` (SPEC-002 fonts) use a small `om_msvc6_string` layout struct in
  the 32-bit Windows build and `const char*` adapters elsewhere.

## Source tree

```
engine/                     Yamagi Quake II base, becoming the SoF engine
  src/common/               filesystem (SPEC-300), cvars/commands (SPEC-302), savegame container (SPEC-303),
                            string packages (SPEC-131/313), netchan + messages (SPEC-200..203), GameSpy (SPEC-204)
  src/server/               server for protocol 33, ghoul stream writer (SPEC-013), game module host (SPEC-001)
  src/client/               client: protocol 33 parser, prediction, view/HUD (SPEC-310), input (SPEC-312),
                            demos (SPEC-311), server browser
  src/client/menu/          RMF menu runtime (SPEC-130, SPEC-800..803)
  src/client/fx/            effect system and temp entities (SPEC-142, SPEC-400/401)
  src/client/refresh/gl3/   renderer (SPEC-002, SPEC-410..413), world done; Ghoul models, effects, 2D
  src/ghoul/                Ghoul model system, C++ (SPEC-010..014, SPEC-120/121)
game/                       game module (SPEC-001 exports), C++: world core (760/761), AI (700..732),
                            navigation/bots (740/741), scripts (750..752), props (770/771), player side
                            (780..782), game modes (790/791)
player/                     player module (SPEC-003, SPEC-600..602)
modules/sound/              sound backend (SPEC-004) — done
tools/                      format readers (Python), network tools, build scripts, spec gate
docs/                       plan, status, specs, process
```

## Build targets

- `win32-compat` (MSVC x86): engine + all modules as DLLs with original names and ABIs → interchangeable with
  the user's original files.
- `native` (MSVC x64, GCC/Clang x86-64 and ARM64): same sources; modules may be linked statically.

## Rules for implementers

- Work only from approved specs (`docs/specs/`, check `ERRATA.md` and `OPEN_QUESTIONS.md`), Quake II GPL code
  and Yamagi; record spec gaps in `OPEN_QUESTIONS.md`.
- Stay inside your package's directories; shared headers live in `engine/src/common/header/sof/` and are
  changed only by the package that owns them (listed at the top of each header).
- Every source file: SPDX GPL-2.0-or-later header (or the original GPL header for modified Yamagi files) and
  `Implements: SPEC-…`.
- Verify against the original as a black box (oracle install `D:\Games\SoF-oracle`), not by reading it.
