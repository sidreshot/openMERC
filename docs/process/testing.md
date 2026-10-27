# Clean-team rules and test-run conventions

Rules for the clean team (C, see `CLEANROOM.md`) and conventions every test harness in this repository follows.

## Hard rules

- Do not open, list or search the reverse team's private workspace, and do not disassemble, decompile or
  hex-inspect the code sections of the original executables/DLLs (`SoF.exe`, `*.dll` in the game folder). Do not
  run disassemblers, decompilers or debuggers on them.
- Do not use leaked source code, leaked SDKs, or code from other projects unless its license is GPL-compatible and
  it is recorded in `THIRD_PARTY.md`.

## What may be used

- `docs/specs/` in this repository.
- The Quake II GPL source (id Software 3.19) and Yamagi Quake II.
- The game's **data** files from your own copy (`base\*.pak`: pak0-pak3, zmain, zsond, gs; ignore third-party paks).
  List/extract with `python tools/pak.py`; extract only into a data directory outside the repository (game data is
  never committed). The harnesses call it `openmerc-data`.
- Public documentation on the web (format notes, community wikis), cited by URL in the spec.
- Black-box observation of the running game.

## Method for a data format

1. Collect many stock files; compare sizes; hex-dump headers; find magic, versions, counts, offsets, fixed-size records.
2. Hypothesis, then check it by parsing **every** stock file of that type without error.
3. Decode to something viewable (PNG, OBJ/glTF, WAV, JSON) and look at it.
4. Where the meaning of a field cannot be inferred from data, mark it `UNKNOWN` and list it under *Open questions*.

## Running games for tests

- Never force focus to a game window and never send synthetic input to the desktop; feed input through the
  game's own console/command line (`+exec`, `+wait`, cvars).
- Always start the engine with `-portable` and an explicit data/home directory under the data directory, so nothing
  is written to `Documents\YamagiQ2` or `Documents\openMERC`.
- Don't edit system files (hosts, registry) unless a test explicitly requires it; if it must, back up and restore
  byte-identically.
- Close every window and kill every process a test started.
- Network ports: test servers on one PC can pull each other's clients (the original client accepts `client_connect`
  from any address). Every run, original or openMERC, server and client, uses its package's own ports:
  `+set port <base>` for servers and `+set clientport <base + 1>` for clients (further instances: base + 2 ...
  base + 9). Never use the defaults (27910, any port). Bases: I1b 29400, I4c 29410, I5 29420, I7 29430,
  F5 29440, G18 29450, G2 29460, G4 29470, G7 29480, G9 29490, I1a 29500, G3 29510, R-team probes 29520,
  I5s-2 29610 (uses 29611/29612), G10 29530, I1c 29540, I2d 29550, I1d 29560, R movement 29570, X1 29580,
  I1b-2 29590, V1 29600. If a run sees a second server's serverdata or a connect it did not ask for, discard the run.
- The original SoF.exe crashes with more than 50 command-line words; put long setups in an `+exec` file.
- Comparison hygiene: cap both games to the same frame rate (`cl_maxfps` in the original, `vid_maxfps` in
  openMERC) - effects and animations step per frame above 60 fps; compare like with like (the same role: a
  spectator's view with a spectator's, the shooter's own view weapon with the shooter's); align runs by an in-game
  event (a trigger touched, a frame number), not by the capture clock; before reporting a difference, run the
  original twice and state its own run-to-run spread.
- The original SoF.exe as a dedicated server needs `developer 1`, set **inside an exec'd cfg** (`set developer 1`
  in e.g. `base\g18dev.cfg`, started with `+exec`): with `developer 0` the single-player shim setups stop after
  "Server Initialization"; `+set developer 1` on the command line opens a modal "Developer Mode" box. Clients
  don't need `developer`.
- Listen server of the original SoF.exe with a non-stock game module: the local client is refused with "Server
  violence settings are incompatible" (the pure bit of `sv_violence` is set only with the stock module checksums,
  SPEC-AMEND-005 GS.15, and a client whose registry lock words do not match forces all locks). Use a dedicated
  server plus a client, the single-player proxy/shim setups of G6b/G2, or `player/test/sof_locks.py` unlock -> run
  -> byte-identical restore (as `player/test/run_session.ps1` does).

## Outputs of a format package

- Spec: `docs/specs/SPEC-NNN-<slug>.md` with a `Status:` header and sections Purpose / Layout / Semantics /
  Validation / Open questions. Layout as tables (offset, size, type, meaning).
- Code: `tools/formats/<format>.py` (Python 3, stdlib + Pillow/numpy) with `# SPDX-License-Identifier:
  GPL-2.0-or-later` and `# Implements: SPEC-NNN`.
- Validation report at the end of the spec: how many stock files parsed, any failures.
