# SPEC-110 — `.adp` sound (4-bit IMA ADPCM)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — every `.adp` in `pak0`, `pak1`, `zsond` (1177 files, 92.9 MB); for the survey in
§8 every `.wav` in `pak0`–`pak2`, `zsond` (1071 files). No game binary was consulted.
Implementation: `tools/formats/adp.py` (decoder, reference encoder, trailer detector, WAV writer)

## Purpose

`.adp` holds the long sounds: cinematic dialogue (`sound/cin/…`), enemy and NPC speech (`sound/enemy/…`,
`sound/speech/…`), a few heavy impacts and ambiences, and all music (`sound/music/<set>/l*.adp`, `r*.adp`,
referenced by `.dms`, SPEC-111). Short effects stay as plain RIFF `.wav` (§8).

The codec is the public **IMA/DVI ADPCM** algorithm (IMA "Recommended Practices for Enhancing Digital Audio
Compatibility in Multimedia Systems", rev. 3.00, 1992; summary with tables:
<https://wiki.multimedia.cx/index.php/IMA_ADPCM>) without any block structure: one continuous stream per file.
There is **no** RIFF/WAVE header.

## Layout

All integers little-endian.

| Offset | Size | Type | Field | Meaning / observed values |
|---|---|---|---|---|
| 0 | 4 | uint32 | `sample_rate` | Hz. **22050** in all 1177 files |
| 4 | n | uint8[n] | `payload` | ADPCM codes, two per byte, **high nibble first**; n = file size − 4 |

- Channel count: always **1** (mono). Stereo music is two files, see SPEC-111.
- Number of decoded samples = 2·n. There is no length field; a stream with an odd number of codes cannot be
  represented exactly (the stock converter drops the last code, §4).
- n is odd in 982 files and even in 195; no alignment or block size exists.

## Decoding

State: predictor `p` (int, 16-bit range) and step index `i` (0…88). At the start of every file **p = 0, i = 0**
(no state is stored in the file).

For each 4-bit code `c` in playback order (byte `b`: first `b >> 4`, then `b & 15`):

1. `step = STEP_TABLE[i]` (the 89-entry IMA table 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, … 29794, 32767).
2. `d = (step >> 3)`; `if c & 4: d += step`; `if c & 2: d += step >> 1`; `if c & 1: d += step >> 2`
   (integer shifts — the shift-and-add form; the "exact" form `(2·m+1)·step/8` does **not** reproduce the
   stock data, §5).
3. `p = p − d` if `c & 8` else `p + d`; then clamp `p` to [−32768, 32767].
4. `i = i + INDEX_TABLE[c & 7]` with `INDEX_TABLE = (−1, −1, −1, −1, 2, 4, 6, 8)`; clamp `i` to [0, 88].
5. Output sample = `p` (signed 16-bit PCM).

Test vectors (synthetic, decoder starting at p = 0, i = 0):

| payload bytes | decoded samples |
|---|---|
| `77 77 43 33` | 11, 41, 104, 240, 416, 581, 731, 867 |
| `FF FF` | −11, −41, −104, −240 |
| `00 00 00 00` | 0, 0, 0, 0, 0, 0, 0, 0 (step 7 >> 3 = 0; index stays 0) |

## Semantics

- Duration = 2·n / `sample_rate`. Total decoded audio: pak0 6778 s (of which music 6225 s = 59 stereo pieces ×
  2 channels), pak1 968 s, zsond 1081 s.
- Path convention: the game refers to these files with their full path inside the game tree (e.g. from `.dms`
  files and, presumably, from scripts); names under `sound/cin/` mix case between paks (`sound/Cin/Ger4/…` in
  pak1, `sound/cin/ger4/…` in pak0) — lookups must be case-insensitive, as for every other game path.
- `zsond.pak` overrides 430 `.adp` paths of pak0 (83) and pak1 (347), see §6.

## 4. Trailer artefact (RIFF chunk encoded as audio)

Most stock streams end with 16–39 samples that are **not audio**: the converter that produced the `.adp`
encoded everything from the start of the source WAV's `data` chunk payload to the **end of the source file**,
so the RIFF chunks that followed the `data` chunk (a Sound Forge 4.0 `LIST`/`INFO` chunk with `ICRD`
date, `IENG` engineer, `ISFT` "Sound Forge 4.0") were treated as int16 samples and encoded too.

Proof (bit-exact, §5): `sound/impact/wood/CrateExp.adp` and the stock `sound/impact/wood/CrateExp.wav` (pak0).
The WAV has 34752 samples followed by a 78-byte `LIST` chunk. IMA-encoding the 34752 samples **plus the 78
chunk bytes read as 39 int16 words** with §5's encoder from (0, 0) reproduces all 34790 codes of the `.adp`
exactly (the 34791st code was dropped because the stream must have an even number of codes).
`HeliExp.adp`/`HeliExp.wav` (a louder re-master of the same effect, so not bit-exact) gives the same trailer
start: sample 99870 = the WAV's sample count.

Decoded, the trailer is a burst of near-full-scale values lasting 0.7–1.8 ms (≈ 100× the RMS of the preceding
400 samples) — an audible click if played.

Detection (implemented in `adp.find_riff_trailer`): for each candidate start `s` in the last 48 samples, take
the decoder state before `s` and IMA-encode the words that begin a `LIST` chunk; accept the earliest `s` where
the re-encoded codes equal the stored codes:

| layout | words encoded at `s`, `s+1`, … |
|---|---|
| mono (all non-music files) | `"LI"`, `"ST"`, size_lo (any even value 4…4094), 0, `"IN"`, `"FO"` |
| left channel of music (`l*.adp`) | `"LI"`, size_lo, `"IN"` |
| right channel of music (`r*.adp`) | `"ST"`, 0, `"FO"` |

The stereo patterns show that music was encoded from interleaved stereo WAVs split word by word: even words →
`l*` file (left), odd words → `r*` file (right); the trailing `LIST` chunk was split the same way.

Results over the stock data (Raven-made files, i.e. pak0 + pak1, 747 files):

| group | files | trailer found | trailer length (samples) |
|---|---|---|---|
| speech/cin/enemy/impact/ambient (mono) | 629 | 626 | 38 (372 files) or 39 (254) — 39 words, minus one when the total would be odd |
| music (`l*`/`r*`) | 118 | 108 (54 pairs; l and r always at the same offset) | 18 (56) or 19 (52) |
| no trailer | 13 | — | `cin/irq2b/Script/HeliShoot`, `cin/kos3/script/HeliShoot`, `cin/sib2/script/WallBreak`; music pairs `dm3/*DM3`, `ger2/*Action`, `ger3/*Tension`, `nyc3/*Action`, `tsr2/*TSR2` |

The 430 zsond files have no trailer.

The decoder must still decode the whole stream (the trailer is part of the file); whether the original engine
plays it is UNKNOWN (Open question 1). `adp.py decode --trim` drops it for listening.

## 5. Reference encoder (verification)

Encoder state starts at p = 0, i = 0. For each input sample `x`: `diff = x − p`; `c = 8` and `diff = −diff` if
`diff < 0`, else `c = 0`; if `diff ≥ step`: `c |= 4`, `diff −= step`; if `diff ≥ step >> 1`: `c |= 2`,
`diff −= step >> 1`; if `diff ≥ step >> 2`: `c |= 1`; then update `p` and `i` exactly as the decoder does with
code `c` (so the encoder tracks the clamped reconstruction).

- On `CrateExp` (34790 codes) this encoder reproduces every stock code; the exact-multiplication variant
  differs in 6232 codes (first at code 17).
- Decoding `CrateExp.adp` and re-encoding the result also reproduces every code.

This fixes the step table, index table, nibble order, initial state, rounding and clamping.

## 6. `zsond.pak` (localisation)

`zmain.pak` + `zsond.pak` are a **Russian localisation** (their `strip/*.sp` files carry CP1251 Russian text in
the `TEXT_ENGLISH` fields; `zmain` also replaces the fonts). `zsond` replaces 430 `.adp` and 69 `.wav` speech
files with Russian recordings:

- Every replacement `.wav` has exactly the same frame count as the English file it replaces (69/69), but the
  content is uncorrelated (different language) — the dub was time-fitted sample-exactly.
- Every replacement `.adp` is exactly **8 bytes longer** than the file it replaces (430/430), starts with the
  same 4 payload bytes `77 77 43 33` (430/430) and has no `LIST` trailer.
- Decoded with the algorithm above, the zsond streams are speech-like (voiced harmonics visible in
  spectrograms) but **overdriven**: median RMS ≈ 16 000 vs 7 700 for the English files; 0.6–37 % (median
  6.7 %) of samples at the clamp limits, the positive limit ≈ 6× more often than the negative one (sample of 43
  files); slowly wandering DC (median level over the last 200 samples ≈ +8000). The PCM `.wav` replacements in the same pak are clean (RMS 7200, no clipping, no DC), so the
  distortion comes from how the `.adp` were produced, not from the recordings.
- Tried without success (none gives clean output on zsond while the stock decoder is clean on pak0/pak1):
  low-nibble-first order; skipping 4 or 8 bytes; initial step index 0…88; no-rounding and exact-multiply
  difference forms; two's-complement codes; all 24 bit permutations of the code; no clamping / 16-bit
  wrap-around; the OKI/Dialogic 12-bit table; Yamaha ADPCM; half-size step table; five alternative index tables.

Our reading: the localiser's encoder differed from Raven's, and the English game decodes these files with the
same decoder as everything else, i.e. the Russian voice plays distorted. This is a hypothesis (Open question 2).
OpenMerc decodes them with the same algorithm.

## 7. Validation

`python tools/formats/adp.py validate <dir with extracted paks> --out <wavdir>`:

- **1177 / 1177** files decoded without error; all at 22050 Hz.
- Raven files (747): no file clips at more than 0.11 % of samples (`impact/metal/Bridge2`, a loud impact);
  16 files touch the clamp at all. |DC| ≤ 4380 (largest: `cin/IRQ3a/Script/PowerDown`, a deliberately
  asymmetric low sweep; next `cin/sib3/One`, asymmetric voice). Silences decode to exact 0 / ±few.
- Visual check (waveform + spectrogram PNGs) of speech (`speech/tsr1/npc/EB9-1`, `Cin/Tut/HUDD1`,
  `enemy/so1/irq1a/Greet1`), effects (`PowerDown`, `CrateExp`) and music (`jpn3/lAction`): clean, speech shows
  clear pitch harmonics and formants, music shows rhythmic structure.
- Reference comparison: `CrateExp` bit-exact (§5); `HeliExp` correlation 0.88–0.91 per window against the
  louder stock WAV, same length.
- Stereo music: l/r files of all 59 pieces have equal length; zero-lag correlation median 0.32 (wide stereo mixes).
- Decoded output (trailer removed): `openmerc-data/decoded/adp/<pak>/…`, stereo music `openmerc-data/decoded/music/<set>/<piece>.wav` (via `dms.py render`).

## 8. `.wav` survey (stock RIFF WAVE files)

1071 files, all `WAVE_FORMAT_PCM` (tag 1), **all mono**:

| rate | bits | files | where |
|---|---|---|---|
| 22050 | 16 | 1015 | everywhere (enemy 402, ambient 340, weapons 129, impact 53, cin 36, menu 17, dm 15, player 12, misc 10, speech 1) |
| 11025 | 8 | 47 | `sound/player` 32, `sound/ambient` 12, `sound/impact` 3 |
| 11025 | 16 | 5 | `sound/ambient` |
| 22050 | 8 | 3 | `sound/ambient` |
| 44100 | 16 | 1 | `sound/enemy` (pak1) |

Chunk sequences: `fmt data LIST` (925), `fmt data LIST cue LIST` (136), `fmt data` (6), `fmt data smpl LIST cue
LIST` (2), `fmt data smpl` (1), `fmt data smpl LIST xtra` (1). The `cue ` chunk is the loop-start marker read by
the Quake II WAV loader (`client/snd_mem.c`, `GetWavinfo`: `cue ` → loop start, a following `LIST` with `mark`
→ loop length); it occurs almost only on ambient loop sounds (SPEC-112). One file
(`pak2 sound/player/ConquerLP.wav`) has a RIFF size 44 bytes smaller than the file. The Quake II loader handles
all of these formats; a reader must not assume 16-bit or 22050 Hz.

## Open questions

1. **Trailer playback** (UNKNOWN). Does the engine play the last 16–39 encoded samples (a click) or stop
   earlier? Hypothesis: it plays the whole stream (no length information exists to stop earlier). Black-box
   test: record the game's output while a single cinematic line plays in a quiet room and look for a ≈1 ms
   full-scale burst at the end; for music, record a loop transition of a piece with a trailer (e.g. `jpn3`
   Action) and of one without (`ger2` Action).
2. **zsond decoding** (UNKNOWN). Does the original game (any language build) play the zsond lines distorted
   as decoded here? Black-box test: run the game with `zmain.pak`/`zsond.pak` present, record a Russian line
   (e.g. `sound/cin/ger1/s48d1`) and compare its level/clipping with our decode. Also whether the stock engine
   loads `z*.pak` at all, and in which order relative to `pak0`–`pak3` (it must load zsond after pak0/pak1
   for the overrides to apply).
3. **Sample-rate field** (UNKNOWN). Always 22050 in stock data; whether the engine honours other values.
   Black-box test: patch the field of a copy to 11025 in a mod pak and listen for half-speed playback.
4. **Looping** of `.adp` music: there is no loop-point information in the file; looping behaviour is defined by
   SPEC-111 (also UNKNOWN in part).
