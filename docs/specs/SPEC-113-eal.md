# SPEC-113 — `.eal` environmental audio (EAX) per map

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — every `.eal` in the stock paks (121 files: 52 in `pak0` `maps/`, `maps/dm/`;
69 in `pak2` `maps/`, `maps/dm/`). No game binary was consulted.
Implementation: `tools/formats/eal.py`

## Purpose

One `.eal` per map (`maps/<map>.eal`, `maps/dm/<map>.eal`), next to the `.bsp`. It is a project file of
Creative Labs' EAX authoring tools (the RIFF form type is `eal `; the chunk set — listener/environment presets,
source presets, materials, geometry sets — matches the feature list of Creative's EAGLE editor and EAX 2.0).
No public description of the file layout was found; the property blocks inside it are the public EAX 2.0
structures `EAXLISTENERPROPERTIES` / `EAXBUFFERPROPERTIES` (Creative "EAX 2.0 Extensions SDK" `eax.h`;
reproduced in OpenAL Soft, <https://github.com/kcat/openal-soft>, `al/eax/api.h`) and DirectSound3D listener and
buffer parameters (Microsoft DirectX SDK, `DS3DLISTENER`, `DS3DBUFFER`).

In the stock data these files carry **no per-map acoustics**: all 121 hold the same default environment
("Room") and default source; only `sib1` has one named environment and one source preset. For the engine
(M5, EAX → EFX) the practical content is: default reverb = EAX 2.0 *Room* preset, distance factor
0.0381 m per world unit.

## Layout

Standard RIFF: `"RIFF"`, uint32 size (= file size − 8 in all files), form type `"eal "`, then chunks
`id[4]`, uint32 size, payload (no padding needed: all sizes are even). `LIST` chunks contain a 4-character list
type followed by sub-chunks.

Three layouts occur:

| files | `majv`/`minv` | top-level chunk order |
|---|---|---|
| 52 (all pak0 files: 32 `maps/`, 20 `maps/dm/`; 1516 B, `sib1` 2084 B) | 2 / 0 | `majv minv exe cmd path lisa denv dsrc dfil dmat LIST(envp) LIST(srcp) LIST(matp) LIST(gems)` |
| 52 (pak2, same 52 names as pak0; 640 B, `sib1` 1112 B) | 2 / 1 | `majv minv lisa denv dsrc dfil dmat LIST(envp) LIST(srcp) LIST(matp)` |
| 17 (pak2 only: new `maps/dm/*con*`, `*ctb*` maps; 1244 B) | 2 / 1 | `majv minv exep cmds gdfm lisa denv dsrc dfil dmat LIST(envp) LIST(srcp) LIST(matp) LIST(gemp)` |

The 52 pak2 files of the second layout replace the pak0 files of the same name (newer minor version, without
tool paths, without GUIDs and without the geometry list); their property values are identical.

### Top-level chunks

| id | size | type | meaning / observed values |
|---|---|---|---|
| `majv` | 4 | uint32 | 2 |
| `minv` | 4 | uint32 | 0 or 1 |
| `exe ` | 260 | char[260] | authoring tool's game executable path: `C:\projects\sof\sh_debug\SoF.exe` |
| `cmd ` | 260 | char[260] | its command line: `+set cddir c:/projects/sof +set developer 7 -set user c:/user +set vid_fullscreen 0` |
| `path` | 260 | char[260] | empty |
| `exep` | 260 | char[260] | as `exe `, first byte overwritten by NUL (`\0:\Raven\Sof\SoF.exe`), rest of buffer uninitialised |
| `cmds` | 260 | char[260] | uninitialised memory (no string) |
| `gdfm` | 12 | int32, float32, uint32 | −6000, 0.25, 90 in all 17 files. UNKNOWN (maybe default geometry material: occlusion −6000 mB, LF ratio 0.25, …) |
| `lisa` | 12 | float32 ×3 | **0.0381, 1.0, 1.0** in all files — listener distance factor (metres per world unit), rolloff factor, Doppler factor (DS3D listener order; hypothesis from the values) |
| `denv` | 56 | `EAXLISTENERPROPERTIES` | default environment, identical in all 121 files (table below) |
| `dsrc` | 92 | `EAXBUFFERPROPERTIES` + 40 bytes | default source, identical in all files (table below) |
| `dfil` | 260 | char[260] | empty (default file?) |
| `dmat` | 16 | 4 × 4 bytes | all zero. UNKNOWN (default material) |
| `LIST` | | | preset lists, see below |

All authoring-tool strings are informational; nothing in the game data refers to them.

### `denv` / `lisp` — EAX 2.0 listener properties (56 bytes)

| Offset | Type | Field (eax.h order) | `denv` value (all files) | `sib1` "Environment1" |
|---|---|---|---|---|
| 0 | int32 | room (mB) | −1000 | −10000 |
| 4 | int32 | room HF (mB) | −454 | −10000 |
| 8 | float32 | room rolloff factor | 0.0 | 0.0 |
| 12 | float32 | decay time (s) | 0.4 | 1.49 |
| 16 | float32 | decay HF ratio | 0.83 | 0.83 |
| 20 | int32 | reflections (mB) | −1646 | −2602 |
| 24 | float32 | reflections delay (s) | 0.002 | 0.007 |
| 28 | int32 | reverb (mB) | 53 | 200 |
| 32 | float32 | reverb delay (s) | 0.003 | 0.011 |
| 36 | uint32 | environment index | 2 (= EAX `ROOM`) | 0 (= `GENERIC`) |
| 40 | float32 | environment size (m) | 1.9 | 7.5 |
| 44 | float32 | environment diffusion | 1.0 | 1.0 |
| 48 | float32 | air absorption HF (mB) | −5.0 | −5.0 |
| 52 | uint32 | flags | 0x3F | 0x3F |

The `denv` values are exactly the published EAX 2.0 *Room* preset; the `sib1` values are the *Generic* preset
with room and room-HF at −10000 mB (reverb muted). Cross-check with the EFX equivalents in OpenAL Soft
`include/AL/efx-presets.h`: `EFX_REVERB_PRESET_ROOM` has gain 0.3162 = 10^(−1000/2000), gain HF 0.5929 =
10^(−454/2000), reflections gain 0.1503 = 10^(−1646/2000), late gain 1.0629 = 10^(53/2000), decay 0.40 s,
HF ratio 0.83, delays 0.002/0.003 s — so an EFX backend can take `denv` through the usual mB → gain mapping.

### `dsrc` / `srca` — source properties (92 bytes)

| Offset | Type | Field | `dsrc` value (all files) |
|---|---|---|---|
| 0 | int32 | direct (mB) | 0 |
| 4 | int32 | direct HF (mB) | 0 |
| 8 | int32 | room (mB) | 0 |
| 12 | int32 | room HF (mB) | 0 |
| 16 | float32 | room rolloff factor | 0.0 |
| 20 | int32 | obstruction (mB) | 0 |
| 24 | float32 | obstruction LF ratio | 0.0 |
| 28 | int32 | occlusion (mB) | 0 |
| 32 | float32 | occlusion LF ratio | 0.25 |
| 36 | float32 | occlusion room ratio | 0.5 |
| 40 | int32 | outside volume HF (mB) | 0 |
| 44 | float32 | air absorption factor | 1.0 |
| 48 | uint32 | EAX buffer flags | 7 |
| 52 | uint32 | inside cone angle (°) (hypothesis) | 360 |
| 56 | uint32 | outside cone angle (°) (hypothesis) | 360 |
| 60 | int32 | cone outside volume (hypothesis) | 0 |
| 64 | float32 ×3 | cone orientation (hypothesis) | (0, 0, −1) |
| 76 | float32 | minimum distance (world units; hypothesis) | 262.467 (= 10.0 m × 1/0.0381); `sib1` source preset: 20.0 |
| 80 | float32 | maximum distance (hypothesis) | 1.0e9 (= DirectSound3D default maximum distance) |
| 84 | uint32 | UNKNOWN | 32 |
| 88 | uint32 | UNKNOWN | 0 |

Offsets 0–51 are the EAX 2.0 buffer structure with its documented default values.

### Preset lists

`LIST` payload: 4-character type, then sub-chunks.

| list type | sub-chunks (in order) | record sizes |
|---|---|---|
| `envp` environments | `num ` (uint32 count), `nams`, `guid` (2.0 only), `lisp` | name 32 B, GUID 16 B, listener 56 B |
| `srcp` sources | `num `, `nams`, `guid` (2.0 only), `fils`, `srca` | name 32 B, GUID 16 B, file path 260 B, source 92 B |
| `matp` materials | `num `, `nams`, `guid` (2.0 only), `mata` | (no stock records) |
| `gems` (2.0) / `gemp` (2.1 dm) geometry sets | `num `, `nams`, `fils`, `gema` | (no stock records) |

Sub-chunk sizes are `count × record size` (0 when the count is 0). Stock counts: all zero, except `sib1`
(both versions): `envp` 1 (`Environment1`), `srcp` 1 (`FireFX-FlareLP`, file
`C:\projects\sof\base\sound\Ambient\Gen\FireFX\FlareLP.wav` in 2.0 / `\projects\sof\base\…` in 2.1).

## Semantics

- Use: default reverb for the whole map = `denv`; per-sound defaults = `dsrc`. With every stock map at the
  Room preset, any per-area reverb the original applies must come from elsewhere (UNKNOWN; Open question 1).
- `lisa` distance factor 0.0381 m/unit (1 unit = 1.5 in) relates world units to EAX metres; `dsrc` minimum
  distance 262.467 units = 10.0 m confirms the factor.
- GUIDs, tool paths and `cmd` strings have no runtime meaning for OpenMerc.

## Validation

`python tools/formats/eal.py validate <extract>`: **121 / 121** files parsed; every chunk accounted for; RIFF
size = file size in all files; 1 distinct `lisa`, 1 distinct `denv`, 1 distinct `dsrc` across all files;
preset lists empty except `sib1` (2 files).

## Open questions

1. Does the original game read `.eal` at all (only with the EAX sound module?), and is per-area reverb defined
   anywhere else (e.g. per-texture/material or per-room data in the `.bsp`, or console variables)? Black-box
   test: with an EAX-capable setup (or an EAX→EFX wrapper such as DSOAL with logging), compare the reverb in
   two acoustically different stock maps; then edit `denv` in a copy of one map's `.eal` (e.g. environment 8
   *Cave*) in a mod pak and check whether the reverb changes.
2. `dsrc` bytes 52–91 field names are hypotheses from values (cone angles 360, max distance 1e9 = DirectSound3D
   defaults); 84 (`32`) and 88 (`0`) are UNKNOWN.
3. `dmat` (16 zero bytes) and `gdfm` (−6000, 0.25, 90): UNKNOWN; no stock file has materials or geometry.
4. `lisa` field order (distance, rolloff, Doppler) is inferred from DirectSound3D listener defaults; only the
   first value differs from 1.0, so rolloff vs Doppler cannot be told apart from data.
