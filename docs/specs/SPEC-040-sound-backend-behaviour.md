# SPEC-040: Sound backend behaviour (mixing, ambient sets, dynamic music, EAX and A3D differences)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: Defsnd.dll, EAXSnd.dll, A3Dsnd.dll (and one SoF.exe call sequence), version 1.07f
Depends on: SPEC-004, SPEC-AMEND-001, SPEC-110 (.adp), SPEC-111 (.dms), SPEC-112 (.ams), SPEC-113 (.eal),
SPEC-002 (renderer export slot 0xd8), SPEC-201/SPEC-203 (configstrings)
Answers: OPEN_QUESTIONS.md items 1, 3, 4, 5, 6, 7; SPEC-110 open questions 1 and 3; SPEC-111 open questions 1–6
(as far as the backends are concerned); SPEC-112 open questions 1–5; SPEC-113 open question 2 (bytes 84/88).

## Purpose

SPEC-004 defines the interface of the three interchangeable sound libraries. This spec defines what they do behind
that interface, at the level needed to reproduce what a player hears: how sounds are positioned and mixed, how
ambient sets and dynamic music are played, and where the EAX and A3D libraries differ from the default library.

The default library (Defsnd) is the reference. It is the Quake II 3.19 client sound system (`snd_dma.c`,
`snd_mix.c`, `snd_mem.c`, DirectSound part of `snd_win.c`) with the SoF additions listed here. Where this spec says
"as Quake II", the Quake II 3.19 GPL behaviour applies unchanged. A3Dsnd contains the same code as Defsnd plus an
Aureal A3D path (section 7). EAXSnd is a separate implementation on DirectSound3D hardware voices (section 6).

## Interface / layout

### Terms

| Term | Meaning |
|---|---|
| output rate R | samples per second of the mixer output: 22050 Hz when `s_khz` is exactly 22, otherwise 11025 Hz (the value 44 also gives 11025 — same as Quake II 3.19) |
| sample time | count of output sample frames mixed since the sound system started (Quake II `paintedtime`) |
| client time | the client time in milliseconds (sound struct #1, SPEC-004) |
| volume (0–255) | the integer volume scale used by the mixer; S_StartSound volume v (0..1) becomes trunc(v · 255) |
| listener entity | player number + 1 (SPEC-004) |
| frame | one call of S_Update |

### How the backends read `.ams` records (SPEC-112)

| SPEC-112 field | Defsnd / A3Dsnd use | EAXSnd use |
|---|---|---|
| `flag_a`, `flag_b` | stored, never used | stored, never used |
| `min_interval`, `max_interval` | seconds; the gap to the next random one-shot is min + U·(max − min), U = (random integer 0…999)/1000 | same |
| `loop` | wave index of the loop; −1 = none | same |
| `count` | only the low byte is used | only the low byte is used |
| `waves` | random one-shot waves (groups 0 and 1) | same |
| `distance` | radius of a point set (group 1) | same |
| `volume_min`, `volume_max` | see sections 3 and 6.6 | see 6.6 |
| group 2 (triggered) | parsed, never used by the backend (mover sounds are played by the game module) | parsed, never used |

Header counts: Defsnd/A3Dsnd hold at most 1024 wave names and 256 sets per group; a group whose count is exactly
255 is skipped while parsing (UNVERIFIED that any file can rely on this; stock counts are 72/70/27). EAXSnd takes
each group count modulo 255.

### How the backends read `.dms` files (SPEC-111)

| SPEC-111 field | Defsnd / A3Dsnd use | EAXSnd use |
|---|---|---|
| `version` | must be 11 | must be 11 |
| `piece_count` | signed byte | signed byte |
| bytes 2–3 | byte 2 is a count of a post-processing pass over bytes after the last record; it has no effect on playback. Byte 3 is ignored | ignored |
| `header_unknown` (bytes 4–8) | ignored | ignored |
| `left`, `right` | `.adp` paths (full game paths) | same |
| `type` | class = type − 1: type 1 → calm class, type 2 → middle class, type 3 → action class, type 0 → designer piece | same |
| `trigger` | designer number (any type; stock data uses it on type 0 only) | same |
| `flag` | designer pieces only, see 5.2 | loop flag for every piece, see 6.8 |
| `volume` | read as a 32-bit little-endian integer from the volume byte and the three bytes after it (stock: those are 0) | same |
| record byte +11 | number of extra bytes to skip before the next record (signed; 0xFF counts as 0). Stock value 0 | same |

So a writer must keep the three bytes after `volume` at zero and byte +11 at zero or 0xFF; the other reserved bytes
and header bytes 3–8 may hold anything.

## Behaviour

### 1. Output, channels and timing (Defsnd, A3Dsnd without A3D hardware)

1. Output goes to a DirectSound buffer exactly as in Quake II `snd_win.c` (DirectSound only, no wave-out fallback):
   2 channels, 16 bits, rate R, secondary buffer (or primary with `s_primary 1`).
2. Every frame the mixer mixes ahead to soundtime + `s_mixahead` · R (Quake II `S_Update_`), in blocks of at most 2048
   output frames. When the sample time passes 2^30 the buffers wrap and all sounds stop (as Quake II).
3. 32 mixing channels and 128 pending start requests; channel choice, start-time drift (`s_beginofs`) and `timeofs`
   are as Quake II `S_PickChannel`, `S_StartSound`, `S_IssuePlaysound`.
4. Attenuation: distance multiplier m = attenuation · 0.001 when the attenuation is exactly 3.0, otherwise
   attenuation · 0.0005.
5. Per frame, before mixing (S_Update):
   1. if `s_nosound`, `s_disable` or the client-shutting-down flag is set, the output buffer is cleared and nothing
      else happens (no channel, ambient or music state advances);
   2. a changed `s_volume` rebuilds the 8-bit scale table;
   3. every channel is re-spatialized (autosound channels are cleared, as Quake II); channels whose left and right
      volumes are both 0 are freed;
   4. the music state is updated (section 5);
   5. ambient sets and entity loop sounds are added (sections 2 and 3);
   6. mixing: music first, then every channel (section 1.2).

#### 1.1 Spatialization

Inputs: source position P, listener position L and right vector r, master volume V (0–255), distance multiplier m,
and a near radius N (0 for everything except ambient point sets, section 3.4).

- If the client is not in the active state (connection state 8), left = right = 255.
- Sounds on the listener entity get left = right = V (no attenuation, no panning).
- Otherwise: d = |P − L| − 80; dir = (P − L) normalized; dot = r · dir.
  - Fall-off: a = m · max(0, d) when N = 0, else a = m · max(0, d − N).
  - Near blend b: b = 0 when N = 0; else b = 1 when d < N/2, b = (N − d)/(N/2) when N/2 ≤ d < N, else 0.
  - If the output is mono, or m = 0, or b = 1: lscale = rscale = 1. Otherwise
    rscale = 0.5 (1 + dot)(1 − b) + b and lscale = 0.5 (1 − dot)(1 − b) + b.
  - right = trunc(V · rscale · (1 − a)), left = trunc(V · lscale · (1 − a)); negative results become 0.

With N = 0 this is exactly Quake II `S_SpatializeOrigin` (80 = full-volume distance). Audible range by
attenuation value: 0 → unlimited, 1 → 2080 units, 2 → 1080 units, 3 → 413.3 units.

#### 1.2 Mixing formulas

The mix buffer holds 32-bit integers per output channel; after all channels are added each value is divided by 256
(arithmetic shift) and clipped to 16 bits (Quake II). With g = the sound-processing gain (1.0 normally, 0.3 after
S_SetSoundProcType(1)):

| Source | Contribution per output sample | Notes |
|---|---|---|
| 16-bit PCM | (s · trunc(vol · trunc(`s_volume` · 256) · g)) shifted right by 8 (per side, vol = left or right) | the only path that uses g |
| 8-bit PCM | Quake II scale table: c · trunc(⌊vol/8⌋ · 2048 · `s_volume`) for the signed 8-bit sample c, vol clamped to 255 | g is NOT applied |
| ADPCM (`.adp`) | s · trunc(left · `s_volume`) to BOTH output channels | the left volume only; g is NOT applied |
| music | s · trunc(level · `s_musicvolume`) per channel | `s_volume` and g are NOT applied |

So the underwater muffle (S_SetSoundProcType 1) lowers only 16-bit PCM effects to 30 %; 8-bit effects, all `.adp`
sounds (speech, cinematic dialogue) and music keep their volume. (Answers OPEN_QUESTIONS 5: music is not affected.)
The gain is reset to 1.0 only by S_Init or S_SetSoundProcType(0); it survives level changes.

#### 1.3 Sound data

- PCM `.wav`: loaded, resampled and looped exactly as Quake II `S_LoadSound`/`ResampleSfx`/`GetWavinfo` (including
  the `cue ` loop start and `LIST`/`mark` loop length, the error "Sound %s has a bad loop length" (drop), and
  "%s is a stereo sample" for stereo files, which then play as silence). A file that is not RIFF/WAVE prints
  "Missing RIFF/WAVE chunks" and then also "%s is a stereo sample".
- `.adp` is recognised by the last four characters of the resolved path being exactly `.adp` (case-sensitive; an
  upper-case `.ADP` is parsed as a WAV and fails as above). The sound is kept compressed: length = 2·(file size) − 46
  samples = 2n − 38 for an n-byte payload, rate = the header value (SPEC-110), no loop point. It is decoded while
  mixing (SPEC-110 decoder, state reset at the start of every play), resampled only by the exact ratios
  rate/R = 0.25 or 0.5 (each sample repeated 4 or 2 times) or ≥ 1 (every k-th sample, k = trunc(rate/R)); other
  ratios below 1 produce silence.
- `.adp` playback advances only while the cvar `paused` is 0 and the client is active with the level prepped; while
  paused the sound holds its position and resumes afterwards. PCM sounds keep playing during a pause.
- **Distance on `.adp` sounds:** when an `.adp` sound starts, its distance multiplier is set to 0 after the first
  spatialization. From the next frame on it plays at full master volume in both ears wherever the source is
  (non-positional). The first mixed block may still be attenuated and panned.
- Answer to OPEN_QUESTIONS 7a / SPEC-110 question 1: the last 38 decoded samples of every `.adp` sound effect and
  the last 19 of every music channel (section 5.1) are never played. Stock trailers are 38/39 samples (mono) and
  18/19 samples (music), so the converter click is not heard: at most one trailer sample of the 39-sample mono
  trailers is played, and stock files without a trailer lose up to 38 (19) real samples (≤ 1.7 ms).
- Answer to OPEN_QUESTIONS 7b: yes. A PCM sound with a `cue ` loop start loops for ever when started as a one-shot
  (Quake II rule), until the channel is freed: it becomes inaudible (left and right both 0 at a re-spatialization,
  e.g. out of range), another sound takes the same entity and non-zero channel, the channel is stolen by channel
  selection, S_StopAllSounds or S_FreeSound. EAXSnd never loops one-shots (6.4).
- SPEC-110 question 3: the header rate is honoured within the ratio rule above (stock data is 22050 Hz only).

#### 1.4 Player-model sounds (`*` names)

For S_StartSound(…, entity e, …, sfx "*name"): the number n is the current entity-state number of client entity e
(normally e). The text is configstring 1414 + n (so 1415 + slot for player slots 0–31; there is no range check: other
entity numbers read whatever configstring lies at that index, e.g. 1447… are CS_DMICONS). The model is the text after
the first `\` up to the next `/` (or to the end when there is no `/`); if the configstring is empty or the result is
empty, the model is `male`. Then:

1. if a sound named `#players/<model>/<name>` (formatted into 64 characters) is already registered, it is used;
2. else, if the file `players/<model>/<name>` exists, it is registered and loaded;
3. else `player/male/<name>` (i.e. `sound/player/male/<name>`) is used, registered on demand.

Answer to OPEN_QUESTIONS 6: any entity number works; only numbers 1…32 address player slots. Note that the SoF
player-info text is `name\model\skin\…` (SPEC-203, no `/`), so step 2 looks for `players/<model\skin\…>/<name>`,
which normally does not exist, and the `player/male` fallback is the usual result (UNVERIFIED in game: start a sound
with a `*` name on a player using a non-male model and check which file is opened, e.g. with `fs_debug`/a file
monitor). EAXSnd does not resolve `*` names at all (6.9).

#### 1.5 Registration

As SPEC-004. In addition: the ambient waves of the loaded `.ams` are bound to sounds at S_EndRegistration by name
lookup only (no creation). An ambient wave is therefore audible only if the game registered (precached) the same
sound name in that level; otherwise it is silently skipped.

#### 1.6 `s_initsound 0` (answers OPEN_QUESTIONS 1)

With `s_initsound 0`, Defsnd and A3Dsnd create no cvars. The engine's sound registration (map load, and also every
`snd_restart`, which always re-registers) calls S_RegisterMusicSet, which reads the never-created `s_musicenabled`
cvar and crashes with an access violation. So the SPEC-004 test vector is wrong for the original: the crash happens
at the first registration after `s_initsound 0` takes effect, inside or outside a level (UNVERIFIED outside a level:
`s_initsound 0; snd_restart` at the main menu should also crash). EAXSnd prints "not initializing." but starts
anyway. A reimplementation should not crash (recommended: S_RegisterMusicSet does nothing when sound is off).

### 2. Entity loop sounds (Defsnd, A3Dsnd)

Every frame, for each entity of the current frame that is not an ambient point source (state flag 0x100 clear):

- skipped while the cvar `paused` is non-zero;
- the loop sound index s (state field "sound") must be 1…355 and its sound loaded;
- a looping channel is (re)allocated for this frame at the entity's sound origin with master volume
  (loop parameter byte AND 0xF8), distance multiplier from bits 0–2 of the parameter: 0.003, 0.003, 0.003, 0.0005,
  0.00025 for values 0–4. Values 5–7 are not valid; the original reads unrelated bytes there, which behave as
  "audible only within 80 units" (5 and 7) and "no fall-off" (6);
- the loop position is (sample time + k · 143117) mod length, where k is the entity's index in the frame's entity
  list. Several entities with the same loop sound are therefore not phase-aligned and are mixed separately (no
  Quake II style merging).

Loop channels are cleared every frame and allocated again (Quake II autosound rule), so they compete with one-shots
for the 32 channels.

Entity loop sounds are added in the same per-frame pass as the ambient sets and share its preconditions (client
active, level prepped, an ambient file loaded successfully at least once since S_Init — a later failed load does not
clear this). Without any loadable `.ams` file Defsnd and A3Dsnd play no entity loop sounds at all.

### 3. Ambient sets (Defsnd)

Ambient processing runs only while the client is active, the level is prepped and an ambient file was loaded.

#### 3.1 General set selection

- The current general set index is the low byte of the player-state field (SPEC-004 struct #1 +0x9A8), compared every
  frame as a 16-bit value with the current index. S_SetGeneralAmbientSet(i) sets the current index too, but the next
  frame restores the player-state value if they differ. (If the field's high byte were non-zero, the change would be
  re-detected every frame; stock servers send small values — UNVERIFIED.)
- On every change: the crossfade start time = client time, the previous index = the old index, the current index =
  the new one, and the random one-shot timer is rescheduled (3.3) without playing.
- Index 0 means "no general ambience" (the stock set 0 is `Empty`).

#### 3.2 General loop and crossfade (answers OPEN_QUESTIONS 4, loop volume and fade-in)

- Loop volume (0–255) of a general set: Vloop = trunc(vmin + 0.4 · ⌊(vmax − vmin)/2⌋), played non-positionally
  (both ears, no distance) from position (sample time mod length).
- For 2000 ms of client time after a change, both loops play: the previous set's loop at trunc(Vloop_prev · (1 − f))
  and the current set's at trunc(Vloop_cur · f), f = (client time − start) · 0.0005. After 2000 ms only the current
  set's loop plays at Vloop.
- A level start counts as a change from index 0, so the general loop fades in over the first 2 s.
- Index 0 plays no loop. A non-zero general set without a loop (−1) is not handled by the original (it reads outside
  its sound table); stock data has a loop in every general set except `Empty` (index 0). A reimplementation should
  play nothing for such a set.

#### 3.3 General random one-shots

- A one-shot is due when client time > the scheduled time (strictly greater). The scheduled time starts at 0 on
  S_RegisterAmbientSet, so the first change at level start only schedules.
- When due and the current set (index ≤ the general count) has random waves: wave = one of them chosen uniformly
  (random integer mod count); if its sound is loaded, it is started non-positionally (attenuation 0, volume
  vmin + random integer in [0, vmax − vmin], on the 0–255 scale). The next time = client time + min · 1000 +
  (random 0…999) · (max − min) ms.
- Answer to OPEN_QUESTIONS 4 (interval 0): min = max = 0 schedules the next one-shot at the current client time, so
  `CityRoughNeighborhood` starts a new random one-shot every frame in which the client time advanced (each takes a
  free channel or steals the one closest to its end).

#### 3.4 Point sets (group 1; entities with flag 0x100)

For each such entity in the frame, with set index = state field "sound" (0…255), D = the set's `distance` and
dist = |entity origin − listener|:

- nothing happens when dist > D (hard cut at the radius);
- f = 1 − dist/D;
- loop (if any): volume trunc((vmin + ⌊(vmax − vmin)/2⌋) · f), positioned at the entity origin with distance
  multiplier 0.003 and near radius N = D (1.1). Because dist ≤ D the near term removes all extra fall-off; panning is
  off within D/2 + 80 units and blends in up to the radius. Answer to OPEN_QUESTIONS 4 (fall-off curve of
  `distance`): linear from full volume at the source to silence at D, then cut;
- random one-shots: one timer per entity number (reset to 0 by S_Init); when client time > timer and the set has
  random waves: a uniformly chosen wave, if loaded, starts non-positionally at volume trunc((vmin + random integer in
  [0, vmax − vmin]) · f); the timer = trunc(client time + min · 1000 + (random 0…999) · (max − min)). The first
  one-shot plays as soon as the listener comes within D. The timers are indexed by entity number and are reset only
  by S_Init, not at a level change: a timer left by the previous level delays that entity number's first one-shot
  until the client time passes it (UNVERIFIED in game; matters only if the client time restarts with each level).

#### 3.5 Underwater

S_SetSoundProcType(1) also enables an "underwater loop" in the ambient code (full volume, non-positional), but no
code that assigns a sound to it was found in 1.07f; it never plays (UNVERIFIED in game: no extra loop is heard when
diving).

#### 3.6 Answers to SPEC-112 open questions

1. `flag_a`/`flag_b` are ignored by all three backends.
2. Interval: seconds, uniform in [min, max) in 1 ms steps (general sets: global timer; point sets: per entity).
3. `distance`: hard radius with linear fade (3.4).
4. Volume range: loops use a fixed value (Defsnd/A3Dsnd: general vmin + 0.4·half range, point vmin + half range;
   EAXSnd 6.6); random one-shots draw uniformly in [vmin, vmax] per playback.
5. Loop and one-shots use separate channels; general sets crossfade over 2000 ms (Defsnd/A3Dsnd only).
7. The backend receives set indices, never names; unknown names are the game module's concern.

### 4. Local sounds and channels

- S_StartLocalSound plays on the listener entity, channel 0, volume 0.6 (153 on the 0–255 scale), attenuation 1:
  both ears at 153, never attenuated.
- Ambient one-shots (3.3, 3.4) are started with attenuation 0 and channel 0 on an entity number equal to the wave's
  index in the `.ams` wave table; with attenuation 0 they are never attenuated or panned, so only their volume
  matters, but they take part in channel selection like other sounds on that entity number.

### 5. Dynamic music (Defsnd)

#### 5.1 Loading

- S_RegisterMusicSet(name) (and `forcesong`) first frees the current set. It loads only while `s_musicenabled` is
  non-zero and the name is neither empty nor exactly `(null)` (case-sensitive), from `sound/music/<name>.dms`.
  Messages: "%s isn't a valid music set name" (missing), "Invalid version for Dynamic Music Set %s (%d should be
  %d)". Pieces are read as in the `.dms` table.
- A piece whose left file cannot be loaded prints "Couldn't load %s" and never plays; same for the right file. The
  right channel is read with the left file's length. Playable length = 2n − 19 samples per channel (n = left payload
  bytes); rate = the left file's header rate.
- Then the music state is reset (5.2). Turning `s_musicenabled` on later does not load anything until the next
  registration or `forcesong`.

#### 5.2 Tracks and levels

The player has five tracks; four are used:

| Track | Plays pieces of | Picked on reset / at piece end |
|---|---|---|
| calm | type 1 (`*Tension`) | uniformly random piece of the class |
| middle | type 2 (none in stock data) | uniformly random piece of the class; faded in only by designer value 0xFF after S_SetMusicIntensity(1) (5.3) |
| action | type 3 (`*Action`) | uniformly random piece of the class |
| designer | type 0, chosen by trigger number | by trigger (5.3) |

Each track has a level 0–255, a position and its own decoder state. A fade is (start time t0, end time t1, start
level Ls, target Lt) in sample time; starting a fade sets t0 = now, t1 = now + duration, Ls = the current level.
Once per frame, for each track with t1 > t0: if now > t1, level = trunc(Lt · Vp/255), else
level = trunc(trunc((1 − f) · Ls + f · Lt) · Vp/255), f = (now − t0)/(t1 − t0), Vp = the track's piece volume.
(Ls already contains the Vp factor, so a fade starting from a full track begins with a step down to Vp²/255.)

Mixing (every block, only while the client is active, the level prepped and `s_musicenabled` ≠ 0): a track with
level 0 is skipped and does not advance (it resumes where it stopped when faded in again). Otherwise rate/R · count
source samples of both channels are decoded and mixed (1.2; same ratio rule as `.adp` sounds). At the end of a piece
the position and decoder restart, and:

- designer track, piece `flag` 0: the track stops (silent) and stays silent until a new designer number or mode
  starts other music (answers OPEN_QUESTIONS 3, "play-once piece ends"; the calm/action tracks are not faded back in);
- designer track, `flag` 1, designer number Dn ≠ 0 (always the case while a designer piece is current): the same
  piece loops seamlessly;
- calm/middle/action tracks with Dn = 0: a new uniformly random piece of the class (the same piece when the class has
  one); with Dn ≠ 0: the same piece again;
- (degenerate: a `flag` 1 designer piece reaching its end after Dn was cleared continues only if a pseudo-random draw
  r (0…32767) is smaller than the number k of type-0 pieces, with the (r + 1)-th type-0 piece in file order; normally
  it stops. Only reachable through the fades of 5.3 when the track is still audible.)

At a piece boundary the rest of that mixing block (≤ 2048 frames) gets no music from the track and the same number
of samples at the start of the next piece is skipped.

#### 5.3 Music state machine

State: designer state S ∈ {none, playing, silenced}, designer number Dn, last music mode p, intensity selector
I ∈ {0 calm, 1 middle, 2 action, 3 unused fifth track}. Durations are in output samples (at R = 22050: 150 = 6.8 ms,
10000 = 0.45 s, 15000 = 0.68 s, 100000 = 4.5 s; at 11025 Hz twice as long).

I is set by: mode 2 (I = 0), mode 3 (I = 2), S_SetMusicIntensity(v) (I = v), and S_RegisterMusicSet / `forcesong`
(I = 3). It is used only by designer value 0xFF.

Reset (load, `forcesong`): S = silenced, Dn = 0, p = 1, all track levels 0, new random pieces, I = 3. The music is
active only if the set has at least one piece.

Each frame the mode m = player-state music byte (SPEC-004 struct #1 +0x9AA). Nothing happens unless m ≠ p; then
(afterwards p = m):

| m | Condition | Fades / changes |
|---|---|---|
| 2 (calm) | S = none, p = 3 | calm → 255 over 100000, action → 0 over 100000; I = calm |
| 2 | S = none, p ≠ 3 | calm → 255 over 100000; I = calm |
| 2 | S ≠ none | S = none, Dn = 0, calm → 255 over 10000, designer → 0 over 15000; I = calm |
| 3 (action) | S = none, p = 2 | action → 255 over 10000, calm → 0 over 15000; I = action |
| 3 | S = none, p ≠ 2 | no fade; I = action |
| 3 | S ≠ none | S = none, Dn = 0, action → 255 over 10000, designer → 0 over 15000; I = action |
| 1 (fade out) | — | Dn = 0; action, calm, middle, designer → 0 over 150; S = silenced |
| 0 or ≥ 4 | — | action, calm, middle → 0 over 15000; if a piece with trigger m − 3 exists (first match in file order): the designer track restarts that piece from its beginning, S = playing, Dn = m − 3, designer → 255 over 10000. Otherwise nothing else (S unchanged) |

So mode 0 = no music (there is no trigger −3; answers OPEN_QUESTIONS 3). Designer piece n is mode n + 3.

S_SetMusicDesignerInfo(v) acts at once, independent of m:

| v | Effect |
|---|---|
| 0xFF | if S = none: nothing. Else S = none, Dn = 0, the track selected by I → 255 over 10000, designer → 0 over 15000 (I = 3: the unused track, i.e. silence; I ≥ 4: the original reads past its 4-entry track table and crashes) |
| 0 | Dn = 0; action, calm, middle, designer → 0 over 150; S = silenced |
| other | if v = Dn: nothing. Else action, calm, middle → 0 over 15000; if a piece with trigger v exists: designer restarts it, S = playing, Dn = v, designer → 255 over 10000 |

What follows 0xFF (OPEN_QUESTIONS 3): the track chosen by I fades back in — the calm or action music of the last
mode 2/3 change, or the one chosen by S_SetMusicIntensity. If neither happened since the level's music set was
registered (I = 3), nothing fades in and the music is silent until the next mode change. A later mode change still
applies the mode table.

S_SetMusicIntensity(v) (Defsnd, A3Dsnd): sets I = v and nothing else (no immediate audible effect). SPEC-004's "never
used" is wrong: it decides what designer value 0xFF returns to. Values ≥ 4 followed by 0xFF while S ≠ none crash the
original; a reimplementation should treat them as 3.

`forcesong <name>` (OPEN_QUESTIONS 3): sets the value of `s_musicenabled` to 1 (the value only; the cvar string and
its modified flag are not changed), frees the set, loads `sound/music/<name>.dms`, and resets. Because the reset sets
p = 1 and S = silenced, the next frame applies the current mode m through the "S ≠ none" rows: m = 2 fades the new
set's calm piece in over 10000, m = 3 fades its action piece in over 10000, m ≥ 4 starts designer piece m − 3,
m = 0 or 1 stays silent. The same happens at every level load (S_RegisterMusicSet). `forcesong` without an argument
frees the set and leaves the music silent.

### 6. EAXSnd

EAXSnd keeps the interface (SPEC-004) but works differently. Every rule of sections 1–5 that is not repeated here
does not apply to EAXSnd.

#### 6.1 Device and voices

- DirectSound is created through COM, first as Creative's EAX DirectSound object, else plain DirectSound
  ("CLSID_EAXDirectSound not registered.\nAttempting to use CLSID_DirectSound.", "CLSID_DirectSound not
  registered."), exclusive cooperative level, a 3-D primary buffer and the listener. Without the EAX 2.0 listener
  property set it prints "NO EAX2.0 Listener Properties were detected." and continues without reverb; without
  voice management "Unable to use voice managment (DSPROPERTY_VMANAGER_MODE_AUTO)." (sic).
- Other messages: "Sound Overflow! Unable to create sound %s." (more than 1380 sounds; SPEC-004 quotes it
  slightly differently), "%s is not a registered sound." (bad sound handle), "Unable to register ambient sound set
  %s", "%s version %d not supported.\nUse only version %d." (`.ams` and `.dms`), "%s left music file not found",
  "%s right music file not found", "Unable to create music buffer for song ID %d.", "%s file not found" (sound or
  `.dms`).
- The number of voices N = the device's count of 3-D hardware buffers ("%d available 3D hardware voices").
  All sounds are played as 3-D buffers on these voices.
- `s_initsound 0` is ignored (prints "not initializing." and initializes).
- S_Activate does nothing.

#### 6.2 Sounds

- Up to 1380 sounds. Names: one leading `/` or `\` removed, `\` → `/`, no lower-casing (lookup is
  case-insensitive), names of 65 characters or more and empty names give NULL.
- S_BeginRegistration destroys every loaded sound and empties the name table; names registered during
  registration are loaded at S_EndRegistration; names created outside registration are loaded at once. A sound that
  fails to load prints "%s file not found" or "Unable to create sound %s" and has no data (plays nothing).
- WAV: RIFF/WAVE with `fmt ` and `data` chunks only; `cue `/`LIST` loop points are ignored. Each sound can play up to
  32 instances at once (SPEC-113 `dsrc` byte 84) with a voice priority (byte 88, default 0).
- `.adp` (case-insensitive extension): one instance only (starting it again restarts it), decoded into a
  8192-sample streaming buffer as it plays; length 2n − 38 samples like Defsnd.
- Per-sound 3-D defaults are the SPEC-113 `dsrc` values: minimum distance 10 m = 10/0.0381 = 262.47 units, maximum
  distance 1.0e9, cones 360°, EAX buffer defaults. With the Environmental Audio Manager (6.5) a source preset is
  looked up by the name "<last directory>-<file name without extension>" (e.g. `FireFX-FlareLP` for
  `Ambient/Gen/FireFX/FlareLP.wav`) and replaces these values; it prints "%s min dist = %f".

#### 6.3 Positions and distance model

- The listener is placed at the view origin with the forward/up orientation (world units). The DirectSound3D
  distance factor is the `.eal` `lisa` value or 0.0381 m per unit, rolloff and Doppler 1.0 (or the `.eal` values).
- Sound attenuation follows DirectSound3D's model (full volume up to the minimum distance, then
  gain = min/(min + rolloff·(d − min))). The SoF attenuation value is used only to decide: attenuation < 0.01 →
  non-positional with volume forced to 1.0; attenuation exactly 0 → voice priority + 2. ATTN_IDLE/STATIC have no
  further effect.
- Non-positional sounds (entity 0, attenuation < 0.01, every `.adp` sound started without an explicit origin, local
  sounds) are placed 1 m (26.25 units) in front of the listener.
- Explicit origins are fixed; sounds on an entity follow its sound origin every frame.
- The entity channel is ignored: a new sound never replaces an older one on the same entity and channel.
- Voice choice: a free voice, else one that has stopped, else the voice with the lowest priority is stopped and
  reused.

#### 6.4 Volume

DirectSound volume (hundredths of dB) = trunc(2000 · log10(min(`s_volume`, 1) · v)), or −10000 when either factor
≤ 0 (i.e. linear amplitude v · `s_volume`). Volume is clamped to 0..1. One-shots never loop. S_StartLocalSound
plays at volume 0.6 non-positionally. Changing `s_volume` updates all buffers at the next frame.

#### 6.5 Environment (reverb)

- If `eaxman.dll` exports EaxManagerCreate and creation succeeds ("Environmental Audio Manager found."), at each
  S_BeginRegistration the file named by configstring 36 with its extension replaced by `.eal` (or appended) is
  loaded. A missing file does nothing (no message). A file that fails to load prints "Unable to load Environmental
  Audio Library %s.", releases the manager for the rest of the session and falls back to presets. Success prints
  "Using Environmental Audio Library %s." and applies the file's listener attributes.
- With a loaded `.eal`, every frame the manager is asked for the environment at the listener position (passed as
  x, z, y); when it changes, its listener properties are applied (stock files define no areas, SPEC-113).
- Without the manager: the map file name (after the last `/` or `\` of configstring 36) selects a preset
  environment, compared case-insensitively: `tsr1.bsp` 12, `trn1.bsp` 19, `arm1.bsp` 4, `kos1.bsp` 8. The preset
  value is not reset for other maps (it keeps the last matched value, initially 0 = generic).
- S_SetSoundProcType(0) sets that preset value (even when an `.eal` is loaded), (1) sets environment 22
  (underwater), any other value is the drop error "Invalid sound procedure value...". SNDEAX_SetEnvironment(e) sets
  environment e. There is no volume muffle.
- With the manager loaded, each voice's obstruction/occlusion values are taken from the manager for the source
  position when it starts and each frame (UNVERIFIED: exact property set; stock `.eal` files contain no geometry).

#### 6.6 Ambient sets

Same file, same selection field. Differences from section 3:

- General set: the index from the player state is applied every frame (no crossfade; the old loop stops at the next
  frame). Random one-shots are due when client time ≥ the scheduled time (the first one plays immediately); volume
  (vmin + (random mod (vmax − vmin))) · 0.6/256; non-positional. The loop plays at (vmin + ⌊(vmax − vmin)/2⌋) ·
  0.6/256, non-positional.
- Point sets (dist ≤ D): random one-shots are positional at the entity (attenuation 1, volume
  (1 − dist/D) · (vmin + (random mod (vmax − vmin))) · 0.6/256, per-entity timer); the loop is positional at the
  entity with volume 1 − (dist/D) · (vmin + ⌊(vmax − vmin)/2⌋) · 0.6/256 (sic: full volume at the source, slightly
  quieter at the radius, then cut).
- A set with random waves and vmin = vmax makes EAXSnd divide by zero (crash). Stock data has none; a writer must
  keep vmax > vmin in such sets.
- 0.6/256 maps the 0–255 scale to EAX volume (255 → 0.598).

#### 6.7 Loop sounds

- Entity loop sounds play at the entity-state origin with a fixed volume 0.6; the loop parameter byte (volume and
  distance) is ignored.
- Loop voices are keyed by (sound, entity number); a loop not re-added in a frame is stopped at the end of that
  frame. UNVERIFIED corner: the "not re-added" marking only happens while an ambient set is loaded and the level
  prepped; without an `.ams` file stale loops would keep playing.
- While `paused` is non-zero, `.adp` voices are paused and resumed later; PCM voices continue.

#### 6.8 Music

- Each piece has its own stereo streaming buffer; length 2n − 38 samples per channel. The piece `flag` means loop
  for every piece: a piece with flag 0 (all stock Tension/Action pieces, `Intro`, `TSR2`, …) goes silent at its end;
  flag 1 loops seamlessly.
- Piece volume (hundredths of dB): base = 2000 · log10(volume/255 · `s_musicvolume`) (or −10000);
  buffer volume = trunc((base + 10000) · level) − 10000, with a per-piece level 0..1 that changes by a per-piece
  rate once per frame (clamped at 1; a piece with rate ≤ 0 and level ≤ 0 stops). Fades are therefore frame-rate
  dependent: rate 0.005 = 200 frames.
- After loading: one random piece per class is chosen and the calm piece starts at once (rate 1.0 = full after one
  frame).
- Every frame (not only on change), by mode m: 1 → calm/action and designer fade out (rate −0.005);
  2 or 3 → the current intensity piece goes to full (rate 1.0), designer fades out (−0.005), and if the intensity
  class differs from calm (2) / action (3) the old class fades out at 0.005 per frame while the new class's piece
  restarts from the beginning at full volume; 0 or ≥ 4 → if m − 3 differs from the designer number, the intensity
  piece fades out and the piece with trigger m − 3 (if any) starts at once from the beginning at full volume (the old
  designer piece drops at rate −1.0).
- S_SetMusicDesignerInfo: 0 → intensity and designer fade out (−0.005); 0xFF → intensity piece to full (rate 1.0),
  designer fades out; other v → as mode v + 3. S_SetMusicIntensity(v) switches the intensity class to v the same
  way (old fades at 0.005/frame, new restarts at full). Because the mode rules are re-applied every frame, the next
  frame overrides these calls (modes 1–3), restarts the mode's own designer piece (modes ≥ 4 with an existing piece)
  or fades the intensity music out (mode 0 and modes ≥ 4 without a piece); a designer piece started by
  S_SetMusicDesignerInfo survives only in mode 0 or a designer mode without a piece (UNVERIFIED in game).
- Music stops while the client is not active/prepped and resumes after. `s_musicvolume` changes apply at once;
  setting `s_musicenabled` 0 sets the music volume to 0 (silence), 1 restores `s_musicvolume`.

#### 6.9 Missing features

No `play`, `soundlist`, `soundinfo`, `forcesong`; no `*` player sounds (the name is looked up as a file
`sound/*…`, fails, and plays nothing); no `timeofs`; no 8-bit scale/gain distinctions; no underwater muffle.

### 7. A3Dsnd

#### 7.1 Start-up

S_Init tries Aureal A3D first ("Initializing Aureal A3D..."; success: developer message "Aureal A3D initialized"):
16 hardware 3-D sources, a 2-D stream for the software mix, units 1.0 per metre. Creating A3D also registers the
Aureal A3D COM classes in the Windows registry (Aureal SDK behaviour). On failure ("  Could not find A3D hardware",
"  Failed call to A3D::Init", "  Failed call to A3D::CoInit", "  Failed call to A3D::Create Renderer", then the
developer message "Aureal A3D initialization failed.  Dropping to Direct Sound" or "Non-Aureal A3D.dll found.  This is
an unsupported version of A3D. Dropping to Direct Sound") it uses DirectSound exactly like Defsnd. `enable_a3d` /
`disable_a3d` switch at run time ("A3D already enabled", "A3D is not currently running.", "Shutting down A3D...").
Because the engine loads A3Dsnd only when A3D 2.0 is detected (SPEC-004), the A3D path is the normal case.

#### 7.2 Same as Defsnd

Everything in sections 1–5 applies, including the software mixer for channels not on a hardware source, with the
following differences.

1. Channels 0…H−1 (H = number of hardware sources created, ≤ 16) are A3D hardware voices. A one-shot may use one only
   when it is not `.adp`, at least one of `s_reflect`/`s_geom_reverb` is on, and it is attenuated
   (attenuation ≠ 0) or (`s_a3d_weapons` ≠ 0 and its name contains `weapon`); otherwise it is placed on a software
   channel. A loop sound for the same entity reuses its channel (and hardware source) from the previous frame.
2. Sounds on the listener entity are forced to master volume 255 (the requested volume is ignored) with no
   distance.
3. Loop sounds that share sample data: every autosound channel's master volume is divided by the number of active
   channels playing the same data (one pass per frame).
4. Music mode 2 with S = none and p ≠ 3 does nothing (Defsnd fades the calm piece in over 100000 samples).
5. General ambient loops and the underwater loop pass negative entity numbers (set index, −1024) so that loops keep
   their hardware channels.

#### 7.3 Hardware voice parameters

For a channel on a hardware source, each mixing pass: gain = clamp(master volume · `s_volume`/255, 0, 1); a distance
roll-off value = clamp(`s_attn`/m, 0, 1) for one-shots and clamp(`s_auto_attn`/m, 0, 1) for loop sounds
(m = distance multiplier, 1); position = the source origin; pitch = sample rate / R; minimum/maximum distance
`s_min_distance`/`s_max_distance`; listener distance model scale `s_distance`, roll-off `s_rolloff`, Doppler
`s_doppler`. The listener's own sounds and unattenuated sounds are not positioned by A3D and are also mixed by the
software path (UNVERIFIED how the hardware source of such a channel is silenced).
The 2-D stream gain is `s_2dvolume`; when `cl_camera_under_surface` is set and `menus_active` is 0 the 2-D gain is
halved and every source gets the equalizer value `s_watereq` (else 1.0). UNVERIFIED: the exact A3D API calls behind
these parameters (Aureal A3D 2.0 SDK); no A3D hardware was available.

#### 7.4 Geometry (occlusion and reflections)

While `s_geometry` ≠ 0, `menus_active` is 0 and the client is not shutting down, A3Dsnd calls renderer export slot
0xd8 (SPEC-002) once per frame with (the A3D interface, the A3D geometry interface, one A3D material, pointer to a
state word). The state word is 2 on the first call after A3D starts, 1 after any of `s_polykeep`, `s_polysize`,
`s_polyreflectsize`, `s_numpolys`, `s_bloat`, `s_occfactor`, `s_reflect`, `s_occlude`, `s_geom_reverb` changed,
otherwise 0; A3Dsnd resets it to 0 after the call. The call is skipped when the A3D units-per-metre value is ≤ 0. The
material has transmittance `s_occfactor` and reflectance 0.75 (UNVERIFIED order of the two values). `s_reflect`,
`s_occlude`, `s_geom_reverb` toggle the corresponding A3D features (`s_reflect` is forced to 0 when the hardware
lacks reflections); `s_geom_reverb_scale`, `s_refgain`, `s_refdelay`, `s_numpolys`, `s_showtossed`, `s_usepvs`,
`s_leafnum` are passed to A3D or to the geometry code (UNVERIFIED meanings beyond the names).

## Constants

| Name | Value |
|---|---|
| full-volume distance | 80 units |
| distance multiplier | attenuation · 0.0005; attenuation 3.0 → · 0.001 |
| loop distance table (bits 0–2) | 0.003, 0.003, 0.003, 0.0005, 0.00025 |
| point-set distance multiplier | 0.003 (cancelled inside the radius) |
| entity loop phase step | 143117 samples per entity index |
| ambient crossfade | 2000 ms client time, f = Δt · 0.0005 |
| general loop volume | vmin + 0.4 · ⌊(vmax − vmin)/2⌋ |
| point loop volume | (vmin + ⌊(vmax − vmin)/2⌋) · (1 − dist/D) |
| random gap | min · 1000 + (0…999) · (max − min) ms |
| underwater gain | 0.3 (16-bit PCM only) |
| `.adp` playable length | 2n − 38 samples (effects, EAX music too); 2n − 19 (Defsnd/A3D music) |
| music fade durations | 150, 10000, 15000, 100000 output samples |
| local sound volume | 0.6 |
| mix block | 2048 frames |
| channels / start requests | 32 / 128 (Defsnd, A3Dsnd) |
| A3D hardware sources | 16 |
| EAX volume mapping | 2000 · log10(v) hundredths of dB, floor −10000 |
| EAX ambient scale | 0.6/256 per volume step |
| EAX music fade step | 0.005 per frame (1.0 = instant, −1.0 = instant stop) |
| EAX sound instances / `.adp` instances | 32 / 1 |
| EAX default minimum distance | 10/0.0381 = 262.47 units |
| EAX local sound position | 1/0.0381 = 26.25 units in front of the listener |
| EAX map presets | tsr1 12, trn1 19, arm1 4, kos1 8; underwater 22 |

## Test vectors / how to verify

1. Underwater: in a map with water, play an 8-bit effect (`sound/player/…`, 11025 Hz 8-bit), a 16-bit effect and a
   cinematic `.adp` line while S_SetSoundProcType(1) is active (dive): only the 16-bit effect drops to about 30 %;
   music level is unchanged (record and compare RMS).
2. `.adp` distance: start an enemy speech `.adp` far away (≥ 2100 units) with attenuation 1: after the first
   ~0.1 s it is heard at full volume in both ears. A `.wav` with the same parameters is inaudible.
3. Trailer: decode `cin/…` lines with SPEC-110 and drop the last 38 samples; a loopback recording of the game must
   end at that sample (no click).
4. Point set: walk away from an `environ_soundgen` with `ComputerMonitorTerminal` (D = 300): the loop falls off
   linearly and stops abruptly at 300 units; within about 230 units (D/2 + 80) it is not panned.
5. General ambience at level start fades in over 2 s; switching `func_ambientset` areas crossfades over 2 s
   (Defsnd), switches instantly (EAXSnd).
6. `CityRoughNeighborhood` (interval 0): a new random one-shot every client frame (Defsnd).
7. Music (Defsnd, jpn3): idle → calm piece fades in (≈0.45 s after level load); first combat → action in over
   0.45 s, calm out over 0.68 s; back to calm over 4.5 s. Send designer number n (svc_sound_info 0x02): the piece with
   trigger n fades in over 0.45 s; a flag-0 piece plays once and the music stays silent, a flag-1 piece loops; then
   0xFF: the calm/action music of the last mode change returns over 0.45 s. Sending 0xFF before any mode change since
   the level loaded gives silence.
8. `forcesong ger3` while the music mode is 2: `ger3` Tension fades in; `forcesong` with no argument: silence.
9. DM map: the deathmatch track (type 0, flag 1) loops with all three backends while its designer mode/number stays
   active; `TSR1` `Intro` (flag 0) plays once.
10. `s_initsound 0` then `snd_restart` (Defsnd): access violation (section 1.6); EAXSnd: sound still works.
11. A looped `.wav` with a `cue ` chunk started once with S_StartSound loops until the player moves out of range.
12. EAXSnd in a non-preset map after visiting tsr1: S_SetSoundProcType(0) sets environment 12 (stale preset).
13. Sample rate (SPEC-110 q3): a copy of an `.adp` with header 11025 plays at the right pitch (ratio 0.5); with
    16000 it is silent in Defsnd (ratio 0.73).

## Open questions

1. A3D: exact A3D 2.0 API semantics of the hardware parameters and geometry material values (needs A3D hardware or
   the Aureal SDK).
2. EAXSnd with the Environmental Audio Manager: exact obstruction/occlusion handling and the meaning of the manager's
   environment query (stock `.eal` files have no geometry, so stock play is not affected).
3. Whether stock servers ever send a general ambient value ≥ 256 or use S_SetGeneralAmbientSet (svc_sound_info 0x04)
   with a value different from the player-state field (it would be undone the next frame, section 3.1).
4. In-game confirmation of the `*` sound fallback with SoF's player-info format (1.4).

## Proposed amendments to SPEC-004

- `s_khz`: 22 → 22050 Hz, any other value (also 44) → 11025 Hz.
- "t = 1 sets an overall gain of 0.3, applied to every channel" → applied to 16-bit PCM channels only (1.2).
- "Fades take 10000–15000 output samples" → 150/10000/15000/100000 (section 5.3).
- EAXSnd: `s_initsound 0` is ignored; it does use the entity ring for loop sounds (fixed volume 0.6, 6.7);
  S_SetMusicIntensity and the music byte act as in 6.8.
- Test vector "`s_initsound 0` then `snd_restart`: no errors" is wrong for Defsnd/A3Dsnd (1.6).
- S_SetMusicIntensity (Defsnd/A3Dsnd) selects the music that designer value 0xFF returns to (5.3).
- EAXSnd overflow message: "Sound Overflow! Unable to create sound %s." (6.1).
- S_SetGeneralAmbientSet: overridden by the player-state field at the next frame (3.1, 6.6).
- `.adp` playable length and decoding: 2n − 38 samples (effects), 2n − 19 (Defsnd/A3Dsnd music); non-positional
  after the first block (1.3).

## Answers to SPEC-111 open questions

1. Header bytes 4–8 are ignored by all backends.
2. Type 1/3 are the calm/action pieces of the music modes 2/3; type 2 is a third class that can never fade in.
3. `trigger` = designer number: mode n + 3 or svc_sound_info designer value n.
4. `flag` = loop. Defsnd/A3Dsnd consult it for designer pieces only (flag 1 loops, flag 0 plays once; calm/action
   pieces always continue). EAXSnd consults it for every piece, so stock Tension/Action pieces (flag 0) play once
   there (6.8).
5. `volume` is a linear factor /255 on the track level (Defsnd) or a linear amplitude (EAXSnd, via dB).
6. Reserved bytes: see the `.dms` table (three bytes after `volume` must be 0; byte +11 is a skip count).

## Answer to SPEC-113 open question 2

`dsrc`/`srca` byte 84 (stock 32) is the maximum number of simultaneous instances of the sound; byte 88 (stock 0) is
its voice priority. Bytes 52–83 are DirectSound3D cone angles, cone outside volume, cone orientation, minimum and
maximum distance, applied to each instance (EAXSnd).
