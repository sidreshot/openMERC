# SND2: modern sound (OMsnd 1.0)

Branch `sound-modern` (rebased on main b3d2a44). Clean-room: written from docs/specs (SPEC-004, SPEC-040,
SPEC-AMEND-006), GPL Quake II / Yamagi source, the public OpenAL Soft documentation and black-box measurements.

## Design

- **Output.** The DMA mixer (snd_mix.c, snd_out.c) is replaced by OpenAL Soft. Each channel has its own
  source (snd_voice.c): 64 voices, `AL_NONE` distance model, and SoF's gain computed in code (16-bit sounds:
  vol/256 * s_volume * procgain; `.adp` sounds: trunc(vol * s_volume)/256). Direction is given relative to the
  listener and the spread uses `AL_SOURCE_RADIUS`. Device rate is 48 kHz by default (`s_om_rate`), with
  optional HRTF (`s_om_hrtf`) and the OpenAL Soft output limiter (`s_om_limiter`). The levels are calibrated
  against Quake II: OpenAL gives 0.5015 per side both for a centred spatialized source and for a
  non-spatialized one. snd_adp.c and snd_mem.c are still the loaders.
- **Voice choice.** A free voice is taken first, otherwise the quietest. The same entity and channel
  (channel other than 0) overrides the sound already playing there. A voice that goes out of range keeps
  playing at gain 0 instead of being freed, so it is heard again if it comes back into range.
- **The listener's own sounds (S_IsLocalSound).** This is decided once, when the sound starts. A sound is
  local when it comes from the view entity (player number + 1), has attenuation 0 (ATTN_NONE), comes from
  the world entity with no origin, or has an origin within 48 units of the listener. Local sounds play
  head-relative at full volume, are never culled by distance, and can only lose their voice to another
  local sound.
- **Music and ambient.** Music streams on its own direct-channel sources with crossfades; the SPEC-040
  state machine is unchanged. Ambient sets and entity loops use `S_AddLoop` with keys. The EFX reverb send
  and the underwater low-pass are kept.
- **Focus.** The engine always calls `S_Activate`. `s_om_focusmute` defaults to 0. When it is 1, which
  configs written by OMsnd 0.x carry, the game is muted only while the window is out of focus and also
  minimised or hidden. A borderless fullscreen window that loses focus keeps playing.
- **Device loss.** `Vo_CheckDevice` reopens a lost device.
- **Diagnostics.** `s_om_wavefile` writes the output to a WAV file. `s_om_debug` 1/2/4 logs; with 4 every
  start is printed with its origin, the listener and the player entity, plus the voice it got
  (`local` / `3d`). With `developer 1` the engine reports any svc_sound it drops.

## Removed

- The engine's own sound system: `engine/src/client/sound` (OpenAL, SDL, OGG, wave), its OGG/AL stubs and
  every call site (console, pause, cinematics, CD track, disconnect), `sound_started`, `snd_is_underwater`,
  `PM_UpdateUnderwaterSfx`, `CL_GetEntitySoundVelocity`, `CL_GetViewVelocity`, the OpenAL options in
  CMake and the Makefile, and `s_muteonfocusloss`. `header/sound.h` now holds the S_* calls.
- Module selection: the A3D/EAX DirectSound probes, `defsnd.dll` as the default, and the
  Defsnd/A3Dsnd/EAXSnd fallback chain. `snd_host.c` loads only `OMsnd.dll`. `snd_dll` is still registered
  so retail configs parse, but it is ignored (with a message). A retail `config.cfg` sets
  `snd_dll "defsnd.dll"`.
- `s_khz`, `s_loadas8bit`, `s_primary` and `s_mixahead` are no-ops (commented in snd_dma.c).

## Engine fix found while testing

An svc_sound whose sound configstring arrived between level registration and the first frame was dropped,
because `SNDH_ConfigStringChanged` only registers once `cl.sound_prepped` is set. On tsr1 this silenced the
cinematic dialogue (`cin/tsr1/c3d1.adp`, `c3d2.adp`, `c4d1.adp`). The sound is now registered on first use.

## Tests

The harness is `modules/sound/test/sndhost.c`, a fake engine that drives a script.
`modules/sound/test/hostlevels.py` measures each window between actions: peaks, RMS, onset and full-scale
samples. It also checks the `expect=local|heard|silent` tags in the script.

| Test | Result |
|---|---|
| Calibration (wave writer, reverb off, Fire.wav peak 30935, vol 255) | player 30815/30815 (Quake II 30814); centred at 80 units 15408/15408; hard left/right 25867/0; 500 ahead 12145; 1000 ahead 8278; attn 0 at 300 units 30816; proctype 1 + underwater LP 8330 |
| `test/local.txt`, the listener's own sounds, 8 checks | 8 of 8 pass. View entity (playernum 3, so ent 4): 30815/30815, also with an explicit origin 1000 away. ATTN_NONE at 2000: 30816/30816. Entity 0 without origin: 30816/30816. Origin 30 below the listener: 30816/30816. Ent 1 at 3000 when it is not the player: silent. Player shot after 70 world sounds and before 70 more: 30816/30816, its voice never stolen (log), 77 world steals. An entity sound that started out of range is heard once the entity comes into range (peak 9989). |
| 22 shotguns at once | 140 full-scale samples, longest run 11 (OpenAL limiter ceiling, no wrap-around clipping) |
| Content (reverb on) | footsteps about 19k peak; positional dog bark L 16617 / R 4407; NPC .adp about 20k; ambient set RMS about 1840; entity loop RMS about 1475; music ger1 calm RMS 1282-1474, action about 3200, fade out per spec |
| Latency, real device (USB Advanced Audio Device, WASAPI loopback per process) | test tone 50.8 ms; 8 shotguns 49.9-58.5 ms (mostly about 51); requirement under 80 ms |
| In game (D:\Games\OpenMerc-snd-test, tsr1, borderless fullscreen, s_om_debug 4) | the module receives world, impact, ricochet, footstep, NPC and dialogue sounds. Firing produced ricochets and an enemy death but **no weapon fire sound reached S_StartSound at all** (neither svc_sound nor a client call), so the silent player weapon is upstream of the sound module (game/player DLL to engine). |

## Left

- Player weapon fire sounds never reach the sound system in game. This is a game-side or engine-services
  path, not OMsnd. With `s_om_debug 4` any sound that does arrive shows up as `local`.
- The 8-bit gain rule ((vol & ~7)/256) is not implemented; 8-bit sounds use vol/256.
- A capture comparing `s_om_limiter 0` with the limiter on; a capture on the Audigy 5/Rx (the default
  device during testing was the USB device).

## AUD: stutter and cut-off phrases (owner report, mode A)

Harness `tests/AUD/xaudio.ps1` (X1 runs of trn1, tsr1, kos1, nyc1, wave writer, `s_om_debug 24`, `wavgaps.py`).
The frame rate has to be set explicitly: `vid_maxfps 0` on the hidden desktop runs at about 60 fps.

| fps | result (all four maps, about 100 s each) |
|---|---|
| 300 | drops 0, gaps 0, clicks 0, wobble 0; dialogue (.adp) cuts 0; override cuts tsr1 2, others 0 |
| 165 | the same; override cuts tsr1 1, kos1 7 |
| 60 | the same; override cuts tsr1 3, kos1 3 |

Every cut was a same entity and channel override of a short impact sound (`impact/gore/*`, `impact/player/armor.wav`),
which is the Quake II/SoF rule. There were no steals of a playing voice and no dialogue cuts. Per-frame position updates
at 300 fps did not add gain steps (nyc1 26-28 at every fps, from moving sources).
The device output clock latency (`ALC_DEVICE_CLOCK_LATENCY_SOFT`) is constant (33.0 ms WASAPI, 22.3 ms wave writer).
It cannot show an underrun, so that diagnostic was not kept.

### Real device under CPU load (2026-10-27)

Harness `tests/AUD/xload.ps1`: one X1 run on a real WASAPI output (the monitor's HDMI output), the game process
recorded by `proccap.exe` (process loopback), optional busy-loop processes (`-Burn`, Normal or BelowNormal only,
at most half of the logical processors; a High-priority load took the test PC off the network once), and
`s_om_stall` for slow game frames. 8-core Ryzen 7 9800X3D, about 110 s per run, five other game instances of other
work running on the same PC.

| run | drops / gaps / clicks | packet discontinuities | .adp cuts | music ran dry |
|---|---|---|---|---|
| tsr1 A, no load | 4 / 0 / 0 | 0 | 0 | 0 (1 false count before the fix below) |
| tsr1 A, 8 busy loops (all cores, Normal) | 4 / 0 / 0 | 0 | 0 | 0 (1 false count before the fix below) |
| tsr1 A, 4 busy loops | 4 / 0 / 0 | 0 | 0 | 0 |
| tsr1 A, 4 busy loops + 20 % of frames stalled 30-80 ms | 6 / 0 / 0 | 0 | 0 | 0 |
| kos1 A, the same | 6 / 1 / 0 | 0 | 0 | 0 |
| tsr1 B, the same | 6 / 0 / 0 | 0 | 0 | 0 |
| tsr1 A, the same with `s_om_refresh 50` | 6 / 1 / 0 | 0 | 0 | 0 |
| tsr1 A, 4 busy loops + 3 % of frames stalled 200-700 ms | 0 / 0 / 1 | 0 | 0 | 0 |
| tsr1 A, 2 busy loops + 1 % of frames stalled 1-1.6 s, 1 s music queue | 23 / 2 / 1 | 0 | 0 | 42 |
| the same, 2 s music queue | 4 / 0 / 0 | 0 | 0 | 0 |

There was no output underrun in any run. OpenAL Soft mixes on its own thread at time-critical priority with the
multimedia scheduler, so a game thread that is slow or starved does not starve the device. Sound effects are
static buffers that play to their end on their own. The drops at the very end of every recording are the harness
killing the game. The other drops and gaps (the start of tsr1 and kos1 with stalls) are `misc/type.wav`, which the
game plays once per typed character of the mission text: slow frames make the typing irregular, as they would in
the original. The click is several sounds started together after a 700 ms stall. Every cut was a same-entity and
same-channel override of a short impact sound, never a dialogue line.

The only fault that load produced is the music stream running dry during a game hitch longer than its queue. The
queue now holds 2 s (16 x 125 ms buffers). The gain of a track and a change of piece still apply at once, so the
deeper queue adds no delay to what is heard. A designer piece that ends without a loop is no longer counted as
running dry. `s_om_debug 8` also logs when the mix is muted and when the client becomes active or not.

Still open: dialogue freed by the game (`S_FreeSound`, SPEC-004) was not seen in these runs. If the owner hears
a cut again, `s_om_debug 8` shows every cut with its reason, and `s_om_refresh 50` A/Bs the mixing period.

