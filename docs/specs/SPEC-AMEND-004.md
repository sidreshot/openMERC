# SPEC-AMEND-004: Last behaviours found while closing the function inventory (Ghoul caches, menu models, EAX and A3D)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: SoF.exe, EAXSnd.dll, A3Dsnd.dll, version 1.07f
Depends on: SPEC-011 (render-parameter block and its cull object), SPEC-012, SPEC-803, SPEC-040, SPEC-113
Amends: SPEC-012, SPEC-803, SPEC-040

## Purpose

The last functions of the engine and of the two hardware sound libraries whose behaviour was not yet written down
were examined. Most of them are fully covered by approved specs or are compiler and library code. The remaining facts
are listed here, as text to merge into the amended specs. Items are:
- **Refinement**: the old statement is true but incomplete in a way that matters for matching the original;
- **Resolution**: the old statement was marked UNVERIFIED or was an open question, and the answer is now known.

## Interface / layout

No new binary layouts. Names used below that come from public SDKs are interface names:
- EAX 2.0 (Creative): property set `DSPROPSETID_EAX20_ListenerProperties` = {0306A6A8-B224-11D2-99E5-0000E8D8C722},
  property set `DSPROPSETID_EAX20_BufferProperties` = {0306A6A7-B224-11D2-99E5-0000E8D8C722}, their property numbers,
  and the DirectSound property-set support query (KSPROPERTY_SUPPORT_GET = 1, KSPROPERTY_SUPPORT_SET = 2).
- EAX Environmental Audio Manager (`eaxman.dll`): the source dynamic-attribute query.
- Aureal A3D 2.0: source methods for gain, pitch, position, distance-model scale, equaliser and render mode. The
  identification of the last three by the A3D 2.0 SDK's method order is UNVERIFIED (no SDK or A3D hardware was
  available); the values passed are verified.

## Behaviour

### 1. SPEC-012 Ghoul runtime, "Caches and limits"

**1.1 Process lifetime of the image cache and of the decoded-chunk heap (Refinement).**
- Add to the table: the image cache (image lists and images, shared by the three systems) and the decoded-chunk heap
  exist once per process. The heap is allocated as one 3 MiB block when the process starts, before any Ghoul system
  exists.
- Both are released when the process exits: every remaining image list and image is destroyed, then the heap block is
  freed. Nothing is printed and nothing is written.
- A reimplementation may leave this to the operating system; there is no observable effect.

### 2. SPEC-803 Menu models, section 1 "Rendering", step 2

**2.1 Menu models are never culled (Refinement).** The render-parameter block that a menu model is drawn with carries
a render-cull object (SPEC-011) that always answers "not outside". So the instance draw never skips a menu model, its
bolt-ons or a `gpm` preview because of its position, whatever the page layout. This holds for every model drawn by
`ghoul` and `gpm` items.
- Verify: place a `ghoul` item with `xoff` so that most of the model lies off the left screen edge. The visible part
  is still drawn, and no frame drops the whole model.

### 3. SPEC-040 Sound backends

**3.1 EAXSnd §6.1, EAX 2.0 detection (Refinement).** Replace "Without the EAX 2.0 listener property set it prints …" by:
- After the listener is created, each of the 16 EAX 2.0 listener properties (numbers 0–15 of
  `DSPROPSETID_EAX20_ListenerProperties`) is tested once.
  - A property counts as available only when the support query succeeds **and** reports both get and set support
    (support value & 3 = 3).
  - Get-only or set-only support counts as not available.
- When none of the 16 is available, "NO EAX2.0 Listener Properties were detected." is printed and start-up
  continues.
- Later listener changes need these properties:
  - environment changes (map presets, S_SetSoundProcType, SNDEAX_SetEnvironment, `.eal` environment changes) are
    applied only when property 11 (environment) is available;
  - writing or reading the full listener record (56 bytes) needs property 1 (all parameters). It is written for the
    `.eal` listener attributes at S_BeginRegistration and for manager environment changes in S_Update, and read by
    the console commands `snd_eaxparams` and `snd_eaxenvironment` (SPEC-004);
  - otherwise the change is skipped silently. Exceptions: `snd_eaxenvironment` prints `SetEnvironment Failed - `
    instead of `SetEnvironment Succeeded - `, and `snd_eaxparams` prints `Unable to retrieve EAX Listener properties`.
- Verify: on a driver that reports EAX 2.0 listener properties as get-only, the message is printed, presets do not
  change the reverb and `snd_eaxenvironment 5` prints `SetEnvironment Failed - `.

**3.2 EAXSnd §6.5, obstruction and occlusion with the Environmental Audio Manager (Resolution of the UNVERIFIED
"exact property set" and of open question 2 in part).** Replace the last bullet of §6.5 by:
- While the manager exists, the voice's source position is sent to the manager's source dynamic-attribute query:
  - geometry set 0;
  - the position in manager axes (x, z, y), i.e. game y and z exchanged;
  - flags 0.
- When it is asked:
  - when a one-shot or a loop sound starts on a voice (with its start position);
  - every frame, with its current position, for each playing voice whose sound was started on an entity number
    ≥ 0. These are the sounds that follow an entity, and the entity-0 sounds of S_StartSound that are placed in front
    of the listener (SPEC-040 §6.3).
  - Voices started at an explicit origin, and the general ambient sounds (started with a negative entity number), are
    queried only at their start.
- When the query succeeds, five EAX 2.0 buffer properties (`DSPROPSETID_EAX20_BufferProperties`) of that voice are set,
  each a 4-byte value, in this order:

  | Property | Number | Value from the manager |
  |---|---|---|
  | obstruction | 7 | obstruction (integer, hundredths of dB) |
  | obstruction LF ratio | 8 | obstruction low-frequency ratio (float) |
  | occlusion | 9 | occlusion (integer, hundredths of dB) |
  | occlusion LF ratio | 10 | occlusion low-frequency ratio (float) |
  | occlusion room ratio | 11 | occlusion room ratio (float) |

- The voice is then placed at the **virtual source position** returned by the manager (converted back to game axes)
  instead of the true position, for that start or that frame.
- When the query fails, no property is set and the true position is used.
- A voice without an EAX buffer interface is positioned but gets no properties.
- Still open: what the manager returns for stock `.eal` files, which define no geometry. Expected: failure (no effect),
  UNVERIFIED. Check with `eaxman.dll` installed by logging the voice position against the source position.

**3.3 A3Dsnd §7.1 and §7.3, hardware voice parameters (Refinements).**
- §7.1: start-up also stores a scale value of 1.0 (values ≤ 0 would be refused). It has no effect on any later
  computation or call.
- §7.3, distance roll-off:
  - the per-pass value is the source's distance-model scale;
  - it is clamp(`s_attn` / a, 0, 1) for one-shots and clamp(`s_auto_attn` / a, 0, 1) for loop sounds, with a = the
    sound's attenuation;
  - it is **0** when a = 0 and for the listener's own sounds; a negative quotient also gives 0.
- §7.3, render mode, every pass:
  - the source's render mode is read, bit 0x08 is set when the channel is positioned (not the listener's own sound and
    attenuation ≠ 0) and cleared otherwise, and the mode is written back;
  - a source that is reset (sound finished or replaced) gets render mode 0x0C and equaliser 1.0;
  - the meaning of bit 0x08 in A3D 2.0 is UNVERIFIED.
- §7.3, non-positioned channels (listener's own sound, or attenuation 0): after gain, roll-off (0), render mode
  (bit 0x08 clear), position and pitch have been set, the source's equaliser is set to 0.0. The channel is then mixed
  by the software path.
  - This answers part of the UNVERIFIED "how the hardware source of such a channel is silenced": no gain or stop
    call is made for it.
  - Whether the hardware source is audible in that state is still UNVERIFIED.
- Verify (with A3D hardware or an A3D 2.0 wrapper that logs calls): one positioned and one listener sound give the
  call pattern above; distance-model scale 0.5 for a one-shot with attenuation 2 and `s_attn` 1.

**3.4 A3Dsnd §7.4, `s_showtossed` and `s_usepvs` (Resolution).** Replace "`s_showtossed`, `s_usepvs` … are passed
to A3D or to the geometry code (UNVERIFIED meanings beyond the names)" for these two cvars by:
- `s_showtossed` (default "0") and `s_usepvs` (default "1"), both flags 0, are read when they change, but their value
  has **no effect** in 1.07f.
- The other cvars of that list keep the old wording.
- Verify: toggling either cvar during play changes no A3D call and nothing audible.

## Constants
| Name | Value |
|---|---|
| Decoded-chunk heap | one 3 MiB block per process (unchanged size), released at exit |
| EAX availability test | support & 3 = 3 (get and set), listener properties 0–15 |
| EAX listener property gates | 1 = all parameters (56-byte record), 11 = environment |
| EAX buffer properties set from the manager | 7, 8, 9, 10, 11 (4 bytes each) |
| Manager axes | (x, z, y) of the game position; geometry set 0; flags 0 |
| A3D render mode after reset / bit toggled per pass | 0x0C / 0x08 |
| A3D equaliser for non-positioned channels / after reset | 0.0 / 1.0 |
| A3D distance-model scale range | 0 … 1 (0 for attenuation 0 and the listener's own sounds) |
| `s_showtossed`, `s_usepvs` defaults | "0", "1" (no effect) |

## Test vectors / how to verify
1. Menu: a `ghoul` model shifted mostly off-screen is still drawn in every frame (item 2.1).
2. EAX: with a driver that reports set-only EAX 2.0 listener support, the "NO EAX2.0 Listener Properties were
   detected." line appears (item 3.1).
3. EAX + manager: a probe `eaxman.dll` whose source query returns obstruction −1000, ratios 0.25/0.5/0.75,
   occlusion −2000 and a virtual position 100 units above the source. The voice then gets buffer properties
   7 = −1000, 8 = 0.25, 9 = −2000, 10 = 0.5, 11 = 0.75, and the sound is heard from above (item 3.2). The position
   passed to the probe has the game's y and z exchanged.
4. A3D: a probe A3D 2.0 layer logs, for a positioned one-shot with attenuation 2 and `s_attn` 1:
   - distance-model scale 0.5;
   - render mode with bit 0x08 set;
   - no equaliser call.

   For the player's own footstep sound it logs distance-model scale 0, bit 0x08 clear and equaliser 0.0 (item 3.3).
5. `s_showtossed 1` / `s_usepvs 0` change nothing in the A3D call log (item 3.4).

## Open questions
- A3D 2.0 method identities behind the distance-model scale, equaliser and render-mode calls, and the meaning of
  render-mode bit 0x08 (needs the Aureal SDK header or hardware).
- What the Environmental Audio Manager returns for stock `.eal` files without geometry.
