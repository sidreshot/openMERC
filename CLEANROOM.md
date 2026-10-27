# Clean-room process

OpenMerc is an independent engine that runs the data files of *Soldier of Fortune* (Raven Software, 2000).
It contains no code from the original game. Interoperability information was obtained by a
two-team clean-room process described here. This document is part of the project record:
every spec and every source file can be traced back through it.

This is a defensive engineering process, not legal advice. Before a public release the record should be
reviewed by a lawyer in the publishing jurisdiction.

## Roles

| Role | May access | Produces | Never |
|---|---|---|---|
| **R — reverse team** ("dirty room") | the user's own legally obtained copy of the game binaries, disassembly/decompilation tools, debugger traces | private notes; *spec drafts* | commits to this repository; writes engine code |
| **G — gatekeeper** | spec drafts, this repository | approved specs in `docs/specs/` | reads disassembly, decompiler output or R's notes |
| **C — clean team** | `docs/specs/`, Quake II GPL source, other public documentation, the game's *data* files, black-box observation of the original game (screenshots, console output, network captures, timings) | all code in this repository | reads disassembly, decompiler output, R's notes or anything outside `docs/specs/` produced by R |

The private R workspace lives outside this repository and is never published or copied into it.

## What may cross from R to C (the spec rules)

A spec describes **what** the original does, never **how its code is written**.

Allowed — facts needed for interoperability:
- binary layouts of files, network messages and module interfaces: field order, offsets, sizes, types,
  magic numbers, version numbers;
- numeric constants and tables (speeds, damage values, timings, limits);
- names that are part of the external interface: file extensions, console commands, cvars, exported
  DLL symbols, script opcodes, network message identifiers, strings shown to the player;
- observable behaviour, described in prose, formulas, state diagrams or test vectors
  (input → expected output).

Not allowed:
- source or pseudocode transcribed from the decompiler, or prose that walks its control flow
  line by line;
- internal (non-interface) function, variable or type names recovered from the binary;
- comments, structure of functions, or code organisation copied from the original;
- addresses inside the original binaries (kept in R's private inventory only).

G rejects a draft that breaks these rules; R rewrites it. Each approved spec carries a `SPEC-NNN` id,
the date, and the list of binaries it describes.

## Provenance

- Every source file names the specs it implements (`/* Implements: SPEC-012, SPEC-031 */`).
- Commits that implement behaviour reference spec ids.
- Code that comes from the Quake II GPL release or its GPL derivatives keeps its original copyright
  header and is listed in `THIRD_PARTY.md`.
- Game data (maps, models, textures, sounds, scripts) is never committed. The user supplies it
  from their own copy; tests that need it skip when it is absent.

## Separation in practice

R and C work in separate sessions with separate context; C is instructed not to open the R workspace or
the original binaries' code and gets only `docs/specs/` and public sources. The coordinator acts as G and
C and does not read disassembly or R notes.

Disclosure: before the process started, the coordinator ran a statistical survey of the binaries
(section sizes, import/export tables, counts of printable strings, a heuristic function count).
No code was read.
