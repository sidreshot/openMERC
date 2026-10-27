# SPEC-013: Ghoul network replication and savegame images
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (server and client Ghoul systems; instance savegame methods)
Depends on: SPEC-201 Appendix B (bit stream, string coding, record framing), SPEC-010/011 (interfaces, ids),
SPEC-012 (instance state and playback), SPEC-014 §12 (packed rotation), SPEC-303 (savegame container)

## Purpose

SPEC-201 Appendix B gives the framing of the Ghoul bit streams of svc 26/27 but leaves the meanings of the
object-definition lists and of most instance-state groups open. This spec gives:
- the meaning of every field;
- the server's rules for when and how fields are written (quantisation, tolerances, delta bases);
- the client's rules for applying them;
- the complete byte/bit layout of the instance savegame image written by instance method 37 and read by
  method 38. Original saves must be loadable, so this layout matters.

## Interface / layout

### 1. String table (reliable section A)
- Each system has one string table holding every name registered on it: sequence paths, part, note, material and
  skin names. Index 0 is always "".
- A new string gets the next index; adding an existing string returns its index. Indices are 16-bit.
- Only system method 8 (Reset) clears the table.
- The server sends its table in index order starting at 1. The client stores received strings in a separate table,
  so names it registers locally do not disturb the server's indices.
- A path that fails to load still occupies an index (SPEC-012 §1).

### 2. Object definition (reliable section B; completes SPEC-201 B.5)
| List | Count | Entries |
|---|---|---|
| 1 | number of sequences | string index of the normalised path of sequence id 1 … n |
| 2 | number of parts | part name of part id 1 … n |
| 3 | number of notes | note name of note id 1 … n |
| 4 | number of materials | material name of material id 1 … n |
| 5 | number of skins | per skin id 1 … n: the "has extra" bit, the extra value, then the skin name |

List 5 details:
- "Has extra" = 1 when this skin's material differs from the previous skin's; it is always 1 for the first skin.
- The extra value is the material id in id coding. Material 0 is written as present bit 1 + value 0.

Difference coding of indices is modulo 2^16: d = index − (previous index + 1) as a 16-bit value, and the receiver
adds it back with 16-bit wrap-around. The indices need not increase; a decrease produces a large 16-bit d.
SPEC-201's "strictly increasing" holds only for the typical case.

The receiver applies a definition as follows:
1. It creates the object (replacing an existing one with the same id).
2. It registers list 1 with object method 0, list 2 with method 6, list 3 with method 11, list 4 with method 15, and
   list 5 with method 19 (material = the current extra). Client ids therefore equal server ids. For received objects
   a sequence whose file is missing still takes its id (SPEC-012 §1).
3. It prepares the object with object method 2, passing the specular flag that came with the reliable data (SPEC-010 system method 24).
4. It applies the baseline state.

Bit widths for instance states: w0 = bits(sequences), w1 = bits(parts), w2 = bits(materials), w3 = bits(skins),
where bits(c) = the smallest b ≥ 0 with c < 2^b.

**Baseline state.** It is a snapshot taken once: at the object's first preparation for drawing (object method 2) or
its first definition write, whichever comes first. The snapshot is of the object's lowest-id instance at that
moment, coded against the default state (section 3). With no instance yet it equals the default state. It never
changes afterwards. Instance records with frame offset 0 are coded against it.

Removed objects (section C) and removed instances (section D) destroy the client's object/instance with the
negated id. Destroying a client object destroys its instances.

### 3. Instance state fields (completes SPEC-201 B.6)
| Field | Default (new instance) |
|---|---|
| on | 1 |
| speed index | 4 |
| paused | **1** (Play clears it) |
| reverse | 0 |
| attach bolt (part id) | 0 |
| sequence id | 0 |
| mode | 0 |
| A (auxiliary time: last Play/Pause/Resume/switch-on; mode-3 fraction) | 0.0 |
| S (start time) | 0.0 |
| pause-end time (1e30 after Pause; never used by playback) | 0.0 |
| tint R, G, B, A (bytes) | 255, 255, 255, 255 |
| transform (68-byte matrix) | identity |
| part flags (one byte per object part) | all 1 |
| bolt-on map (child instance id → own bolt) | empty |
| skin overrides ((material, part) → skin) | empty |

Instance-state groups of SPEC-201 B.6, field by field:

| # | Bits | Field | Server writes it when / how |
|---|---|---|---|
| 1 | 1 | on | always |
| 2 | 1 + 4 | speed index | ≠ base |
| 3 | 1 | paused | always (gates group 9) |
| 4 | 1 | reverse | always |
| 5 | 1 + w1 (only if w1 > 0) | attach bolt | ≠ base; with w1 = 0 the reader sets 0 |
| 6 | 1 + w0 (only if w0 > 0) | sequence id | ≠ base; with w0 = 0 the reader sets 0 |
| 7 | 1 + 2 | mode | ≠ base |
| 8 | 1 + time t1 + time t2 | A and S | when S or A differs from base (exact compare). t1 = trunc((A − frame time)·120 + 0.5), t2 = trunc((S − A)·120 + 0.5), truncation toward zero (so −2.4 → −1). Reader: A = frame time + t1/120, S = A + t2/120 |
| 9 | (only if paused) 1 + {1 bit infinite; else 32-bit float} | pause-end time | ≠ base. The writer chooses "infinite" (reader value 1e30) when the **base** value is > 1e10 (writer quirk), else it writes the current value |
| 10 | 1 + 4 × (1 + 8) | tint R, G, B, A | the group when any byte differs; each byte only when it differs |
| 11 | 1 + transform | transform | when not equal to base within 1e-4 (sub-format below) |
| 12 | 1 + run-length mask | part flags | XOR against base (sub-format below) |
| 13 | 1 + add list + remove list | bolt-on map | add: entries absent from base, as {id child instance id, w1-bit bolt}, terminated by id 0. Remove: base entries absent now, same form. Only children with positive (server) ids |
| 14 | 1 + add list + remove list | skin overrides | add: while bit 1: {w2 material, w1 part, w3 skin} for keys that are new or have a different skin; then bit 0. Remove: while bit 1: {w2 material, w1 part} for base keys now absent; then bit 0 |

"Frame time" = the game time of the svc_frame in the same packet (SPEC-201).

**Transform sub-format** (group 11):
1. Rotation: 1 bit; if 1, a 32-bit packed rotation (SPEC-014 §12) of the current matrix. The writer sends it when
   the packed rotation of the current matrix differs from the packed rotation of the base matrix.
2. Scale: 1 bit; if 1, a 32-bit float = the length of row 0. It is sent when that length is ≥ 1.01 or ≤ 0.99. The
   reader's default is 1.0 when the bit is 0; this field is **not** delta-coded.
3. Translation: 1 bit "any axis present", then per axis x, y, z: 1 bit present; if present, 1 bit kind; kind 1 =
   11 bits v′ with v = v′/16 − 64; kind 0 = 32-bit float.
   - An axis is sent when it differs from base.
   - The 11-bit form is used when −64 ≤ v ≤ 64, with v′ = clamp(⌊(v + 64)·16⌋, 0, 2047); otherwise the float form.
4. Reader: starts from the base matrix. If a rotation was sent, the 3×3 is replaced by the decoded rotation. Rows
   0–2 are then renormalised and multiplied by the scale, the sent axes are overwritten, and the inverse,
   determinant sign and scale are recomputed.

**Part-flag mask** (group 12): the XOR of current and base part bytes, over all parts in id order, coded in runs.
- Mode 1 (bit 1): 1 bit value v + 5 bits c → the next c + 6 entries are XORed with v. The writer uses it for runs of
  6…37 equal XOR bits.
- Mode 0 (bit 0): the next up to 4 entries each XORed with one bit. The writer uses it otherwise.
- The reader continues until it has covered all parts.

### 4. Server rules
- **Equality** (whether an instance is sent at all): the transform within 1e-4; S and A within 1/30 s; the
  pause-end compared only when paused; all other fields exact; part bytes, bolt-on map and skin map exact. An
  instance equal to its base is not written.
- **Scheduling** (instance method 52, called by the engine per client frame):
  - Instances are queued in a map ordered by ascending priority value: the distance from the client's view point,
    −1.0 for the view weapon, and bolt-ons offered first at the parent's priority − 0.1.
  - An instance whose object the client does not know yet is refused, and the result is 0.
  - An instance with no sequence and no parent gives 0.
- **Writing** (system method 18):
  - The queued instances are written in ascending priority order, one record each (instance id, object id, frame
    offset to the base, state).
  - The first record that does not fit the byte budget is dropped with all later ones for this frame (records are
    written whole, SPEC-201 B.1), then the list terminator is written. The queue is emptied every frame.
- **Delta bases:**
  - Per instance and client the server stores the states it sent, keyed by frame, keeping at most 20. Inserting a
    21st drops the oldest.
  - Per client it keeps at most 100 frames of "what was sent".
  - An acknowledgement (system method 19) prunes states older than the acknowledged frame. A negative
    acknowledgement resets the client to full states.
  - The base for a record is the state stored for the client's acknowledged frame (frame offset = current −
    acknowledged), or the object baseline (offset 0) when none is stored.

### 5. Client rules
- An unknown instance id is created from the record's object id. Object id 0 is fatal (`Ghoul :Null object to
  create remote instance from.`), and an unknown object is fatal (`Ghoul :No object found to create remote instance
  from.`).
- New state = a copy of the base (baseline, or the state stored for frame current − offset). The record is decoded
  on top of it and stored under the current frame. Stored states older than the base are discarded. More than 1000
  stored states is fatal: `Ghoul :More than 1000 unacked deltas from server`.
- The live instance takes the decoded state.
- If the bolt-on map changed, or the offset was 0, every replicated child (negative id) is detached. Each map entry
  is then re-attached as client instance −(child id) at the given bolt. A missing child prints `Missing Child`
  (non-fatal).
- Duration, frame time and frame count are recomputed (as instance method 27 for the current sequence and mode).
  If the sequence, mode or times changed, the event cursor is reset: cursor time := A, cursor phase := the raw
  phase at A (SPEC-012 §5).

### 6. Instance savegame image (instance methods 37/38)
The image is a Ghoul bit stream (SPEC-201 B.1: least-significant bit first), written field by field:

| # | Bits | Field |
|---|---|---|
| 1 | variable | model name, as a Ghoul string (SPEC-201 B.3: dictionary or literal) |
| 2 | 16 | instance id |
| 3 | 1152 | 144 raw bytes: transform (16 f32 row-major + u32 flags), inverse transform (same layout), i32 determinant sign, f32 scale |
| 4 | 16 | attach bolt |
| 5 | 8 | on |
| 6 | 32 | speed index (i32) |
| 7 | 32 | S (f32) |
| 8 | 32 | A (f32) |
| 9 | 32 | pause-end (f32) |
| 10 | 8 | paused |
| 11 | 8 | reverse |
| 12 | 4 × 8 | tint R, G, B, A |
| 13 | 16 | sequence id |
| 14 | 32 | mode (i32) |
| 15 | 32 + 8·P | part count P, then P part bytes |
| 16 | 32 + 128·K | override count K, then K entries of u16 material, u16 unused, u16 part, u16 unused, i32 reserved (0), u16 skin, u16 unused — in ascending (material, part) order |
| 17 | 32 | event cursor time (f32) |
| 18 | 32 | event cursor phase (f32) |
| 19 | 8 | note-processing flag |
| 20 | 8 | matrix-processing flag |
| 21 | 8 | "has model lights" flag |
| 22 | 32 + 48·C | note-callback count C, then C × {i32 table index (−1 when the callback is not in the caller's table), u16 note id}, ascending note id |
| 23 | 32 + 96·M | matrix-callback count M, then M × {i32 table index, u16 bolt, u16 unused, i32 matrix mode} |

All multi-bit fields are written as little-endian values in the LSB-first bit order. Length in bytes =
⌈(model-name bits + 1632 + 8P + 128K + 48C + 96M) / 8⌉. More than the given capacity is fatal: `Ghoul :GhoulInst
savegame packet overflow`, checked after writing.

Bolt-on children and the parent link are **not** saved. The game re-attaches them.

**Loading** (method 38, on a new instance of the same object):
1. The model name is compared case-sensitively with the caller's model name. A difference is fatal: `Ghoul :Inst has
   different model than expected`.
2. P ≠ the object's part count is fatal: `Ghoul :Number of parts mismatch in savegame`.
3. Each skin entry needs material 1…#materials, skin 1…#skins, and the skin's material equal to the material. Any
   failure is fatal: `Ghoul :Skin mismatch in savegame`. A part > P is fatal: `Ghoul :Part mismatch in savegame`.
4. Callbacks whose index is < 0 or ≥ the table count are dropped.
5. Render caches are reset and duration, frame time and frame count recomputed. The result is the saved instance id.

The original game stores each image in a savegame chunk `GHLI` (SPEC-303 container), uses a 16384-byte buffer and a
callback table of 51 entries (SPEC-011).

## Constants
| Name | Value |
|---|---|
| Time unit in states | 1/120 s, rounding trunc(x·120 + 0.5) toward zero |
| Equality tolerances | transform 1e-4; times 1/30 s |
| Scale sent | when the row-0 length is ≥ 1.01 or ≤ 0.99 |
| Short translation | 11 bits, v = v′/16 − 64, for −64 ≤ v ≤ 64 |
| Pause-end "infinite" | 1e30 (written when the base value > 1e10) |
| States kept per instance and client | 20 |
| Frames of sent records kept per client | 100 |
| Client stored states | fatal above 1000 |
| Part-flag runs | mode 1 for 6…37 equal bits, 5-bit length c + 6 |
| Savegame fixed part | 1632 bits + model name |
| Original game buffer / callback table | 16384 bytes / 51 entries |

## Test vectors / how to verify
1. **Minimal state record.** Object with 3 sequences (w0 = 2) and 5 parts (w1 = 3), coded against the default
   state. The instance has been given Play(sequence 2, t = 10.0, offset 0, restart 1, mode 1) and is written in a
   frame with game time 10.0. The bits in stream order are 1,0,0,0, 0, 1,0,1, 1,1,0, 1,0,0, 0,0,0,0,0 (19 bits) =
   bytes `A1 0B 00`:
   - on;
   - no speed;
   - paused 0, reverse 0;
   - no bolt;
   - sequence 2;
   - mode 1;
   - times (0, 0);
   - no tint, transform, parts, bolt-ons or skins.
2. **Time coding.** A − frame time = −0.0125 s → t1 = −1: bits present 1, sign 1, n 00, 8 bits `01`, which decodes
   to −1/120 s. S − A = −0.02 s → t2 = −1 (not −2).
3. **Short translation.**
   - v = 10.0 → v′ = 1184, which decodes to 10.0.
   - v = 64.0 → 2047, which decodes to 63.9375.
   - v = 64.5 → float form.
4. **Default-instance savegame image** (P parts, no skins or callbacks): ⌈(S + 1632 + 8P)/8⌉ bytes, where S = the
   bit length of the model-name string. After the string, these bytes follow, shifted by S bits:
   - id (2 bytes);
   - identity matrix (16 floats) and flags 1, then the same again for the inverse;
   - `01 00 00 00` (determinant sign), `00 00 80 3F` (scale);
   - `00 00` (attach bolt), `01` (on), `04 00 00 00` (speed);
   - 12 zero bytes (S, A, pause-end);
   - `01` (paused), `00` (reverse), `FF FF FF FF` (tint);
   - `00 00` (sequence), `00 00 00 00` (mode);
   - P as LE32 and P × `01`;
   - `00 00 00 00` (no skins), cursor time, cursor phase;
   - `00`, `00`, has-lights flag;
   - `00 00 00 00`, `00 00 00 00`.
5. **Savegame load** into an instance of a different object: fatal `Ghoul :Inst has different model than expected`.
6. **Capture checks.**
   - In a multiplayer capture, a weapon bolted to a player appears in group 13 of the player's instance as
     (weapon instance id, bolt).
   - Changing a player's skin produces a group-14 add entry, with the material taken from list 5's extra values.

## Open questions
- Whether any 1.07f code ever produces a pause-end value other than 0 or 1e30. If not, the group-9 writer quirk is
  harmless.
- Exact timing of the baseline snapshot relative to the first network client (prepare vs first definition write)
  in a dedicated-server capture.

## Amendments to approved specs (proposed)
- **SPEC-201 B.5:**
  - The five lists are sequences, parts, notes, materials and skins (extra = the skin's material when it changes).
  - The difference coding wraps at 16 bits.
  - The baseline is a one-time snapshot of the object's lowest-id instance.
- **SPEC-201 B.6:** the field meanings are as in section 3 of this spec, groups 1…14:
  - 1 on, 2 speed index, 3 paused, 4 reverse, 5 attach bolt, 6 sequence, 7 mode;
  - 8 times (A = frame time + t1/120, S = A + t2/120), 9 pause-end;
  - 10 tint, 11 transform, 12 part flags, 13 bolt-on map (child id → bolt), 14 skin overrides.
- **SPEC-011 "Savegames":** the image layout is section 6 of this spec (no longer "private to the Ghoul system").
