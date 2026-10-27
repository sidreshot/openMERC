# SPEC-004: Sound module interface (Defsnd.dll / EAXSnd.dll / A3Dsnd.dll)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, Defsnd.dll, EAXSnd.dll, A3Dsnd.dll, version 1.07f
Depends on: SPEC-005 (engine module map), SPEC-201/SPEC-203 (svc ids, configstrings CS_AMBIENTSET 12,
CS_MUSICSET 13, CS_SOUNDS 291, CS_PLAYERSKINS 1415); ambient-set `.ams` and dynamic-music `.dms` file formats
are separate specs

## Purpose

SoF keeps the whole client sound system (mixing, 3-D positioning, ambient sets, dynamic music) in one of three
replaceable DLLs. The engine loads exactly one of them at a time and calls it through exported `S_*`
functions. The DLLs call back into the engine through functions that SoF.exe exports by name. This spec
describes:

- the selection and loading of the DLL;
- every export and every engine import;
- the shared "sound struct";
- the console variables and commands;
- the differences between the three backends that an interchangeable reimplementation must respect.

The mixing model is the Quake II 3.19 sound system (`snd_dma.c`, `snd_mix.c`, `snd_mem.c`) unless stated
otherwise.

## Interface / layout

### Selecting and loading the library (engine side)

The engine archives the cvar `snd_dll` (flag 1); its default is empty. On client initialization and on
`snd_restart` the engine does the following:

1. If a sound library is already loaded, it calls its `S_Shutdown` and unloads it. If unloading fails, it
   raises the fatal error "Sound Lib FreeLibrary failed".
2. It picks the library by the value of `snd_dll` (compared case-insensitively):
   - `a3dsnd.dll`: prints "Attempting A3D 2.0 support". If no A3D 2.0 device is detected, it prints
     "A3D NOT supported" and sets `snd_dll` to `defsnd.dll`.
   - `eaxsnd.dll`: prints "Attempting EAX 2.0 support". If EAX 2.0 is not detected, it prints
     "EAX NOT supported" and sets `defsnd.dll`.
   - empty: if A3D 2.0 is detected, it prints "Found A3D 2.0 support" and sets `a3dsnd.dll`. Otherwise, if
     EAX 2.0 is detected, it prints "Found EAX support" and sets `eaxsnd.dll`. Otherwise it sets `defsnd.dll`.
   - Then, if the value is now `defsnd.dll` (chosen directly or by any fallback above), it prints
     "Setting Default Sound support".
3. It prints `------- Loading <snd_dll> -------` and loads the file named by `snd_dll` (no directory: the
   normal DLL search path).
4. If the load fails and `snd_dll` is empty, it raises the fatal error "Couldn't load default sound dll!".
   Otherwise it sets `snd_dll` to `defsnd.dll` and retries. A second failure is the fatal error
   `LoadLibrary("<name>") failed`.
5. It resolves 20 **mandatory** exports, in this order: S_Init, S_Shutdown, S_Touch, S_FreeSound,
   S_StartSound, S_StartLocalSound, S_StopAllSounds, S_Update, S_Activate, S_BeginRegistration,
   S_RegisterSound, S_EndRegistration, S_FindName, S_RegisterMusicSet, S_RegisterAmbientSet,
   S_SetMusicIntensity, S_SetMusicDesignerInfo, S_SetGeneralAmbientSet, S_SetSoundProcType, S_SetSoundStruct.
   A missing export is the fatal error `GetProcAddress <name> failed on <dll>`.
6. It calls `S_SetSoundStruct` with the sound struct (below). This happens before `S_Init`.
7. It resolves three **optional** exports: `S_A3D_ExportRenderGeom`, `SNDEAX_SetEnvironment` and
   `SNDEAX_SetEnvironmentLevel`. If `S_A3D_ExportRenderGeom` exists and a renderer is running, the engine calls
   it at once with the renderer's export table. It calls it again after every renderer (re)load. The two
   `SNDEAX_*` functions are resolved but never called by SoF.exe 1.07f.
8. The caller then calls `S_Init`.

Detection, as observed (exact capability tests UNVERIFIED):

- **A3D 2.0:** an A3D COM object can be created and reports the A3D 2.0 capability bit.
- **EAX 2.0:** a DirectSound device exists that can create a hardware 3-D secondary buffer (22050 Hz, 16-bit,
  mono) whose property-set interface supports both get and set of the EAX 2.0 listener properties.

### Exports

All exports use the C calling convention. In the "Libraries" column, D = Defsnd, E = EAXSnd, A = A3Dsnd.

| Export | Libraries | Signature | Behaviour summary |
|---|---|---|---|
| S_Init | D E A | `void (void)` | create cvars and commands, open the output device, start the music and ambient state |
| S_Shutdown | D E A | `void (void)` | close the device, remove commands, free all sounds |
| S_BeginRegistration | D E A | `void (void)` | start a new registration sequence (level load) |
| S_RegisterSound | D E A | `sfx_t *(const char *name)` | find or create a sound. Returns NULL if sound is not running. |
| S_EndRegistration | D E A | `void (void)` | free sounds not registered in this sequence and load the rest |
| S_FindName | D E A | `sfx_t *(const char *name, int create)` | look up a sound by name (never called by SoF.exe) |
| S_FreeSound | D E A | `void (sfx_t *)` | free one sound and stop channels using it |
| S_Touch | D E A | `void (void)` | mark all cached sound data as in use (memory touch) |
| S_StartSound | D E A | `void (const vec3_t origin_or_NULL, int entnum, int entchannel, sfx_t *sfx, float volume, float attenuation, float timeofs)` | start a sound (see Behaviour) |
| S_StartLocalSound | D E A | `void (const char *name)` | play a non-positional sound for the local player |
| S_StopAllSounds | D E A | `void (void)` | stop every sound and clear the output buffer |
| S_Update | D E A | `void (const vec3_t origin, const vec3_t forward, const vec3_t right, const vec3_t up)` | per-frame listener update and mixing |
| S_Activate | D E A | `void (int active)` | the application gained (1) or lost (0) focus |
| S_SetSoundStruct | D E A | `void (void *const ptrs[5])` | copy the 5-pointer sound struct |
| S_RegisterAmbientSet | D E A | `void (const char *path)` | load the ambient-set file |
| S_SetGeneralAmbientSet | D E A | `void (unsigned char index)` | select the general ambient set |
| S_RegisterMusicSet | D E A | `void (const char *name)` | load `sound/music/<name>.dms` |
| S_SetMusicIntensity | D E A | `void (unsigned char value)` | music intensity (only EAX acts on it) |
| S_SetMusicDesignerInfo | D E A | `void (unsigned char value)` | start or stop a designer music track |
| S_SetSoundProcType | D E A | `void (unsigned char type)` | environmental sound processing mode (0 normal, 1 underwater) |
| SNDEAX_SetEnvironment | E | `void (int environment)` | set the EAX 2.0 listener environment |
| SNDEAX_SetEnvironmentLevel | E | `void (void)` | does nothing. The same code as EAX's S_Activate. |
| S_A3D_ExportRenderGeom | A | `void (void *refexport)` | store a pointer to the renderer export table (used for geometry) |

Export counts: Defsnd 20, EAXSnd 22, A3Dsnd 21.

**sfx_t** is opaque to the engine. The engine only stores the pointers and passes them back. In every backend
the sound name is a NUL-terminated string at offset 0 (64 bytes), and a record is 72 bytes. Defsnd and A3Dsnd
store a registration sequence number at offset 64 and a cache pointer at offset 68. EAXSnd stores a slot index
at offset 64.

### Engine functions imported by name from SoF.exe

| Name | Used by | Use |
|---|---|---|
| CL_GetEntitySoundOrigin | D E A | `void (int entnum, vec3_t out)`: current sound origin of an entity |
| Cmd_AddCommand, Cmd_RemoveCommand, Cmd_Argv | D E A | console commands |
| Cmd_Argc | D A | |
| Com_Printf, Com_Error, Com_sprintf | D E A | |
| Com_DPrintf | D A | |
| Cvar_Get | D E A | |
| FS_LoadFile, FS_FreeFile | D E A | sound, ambient and music files |
| FS_FOpenFile, FS_FCloseFile | D A | existence test for model-specific player sounds |
| Z_Malloc, Z_Free, Z_Touch | D A | sound cache memory |
| irand | D A | random ambient sounds |
| VectorLength | D E A | |
| VectorNormalize | D A | |

The engine also exports Cvar_Set, Cvar_SetValue, Sys_Error and flrand. No sound library uses them.

### The sound struct (`S_SetSoundStruct`)

The argument points to five pointers. The libraries copy them and read engine client data through them every
frame:

| # | Points to | Fields read by the libraries (byte offset in the pointed-to object) |
|---|---|---|
| 0 | the client's ring of 1024 parsed entity states (116-byte records) | +0 entity number; +4 origin (3 floats); +0x30 flags (bit 0x100 = the entity is an ambient-point source); +0x3C looping sound index, or the ambient point-set index when bit 0x100 is set; +0x40 loop parameters: bits 0–2 select the distance factor (0.003, 0.003, 0.003, 0.0005, 0.00025 for values 0–4) and bits 3–7 are the volume (value & 0xF8, out of 255) |
| 1 | the client state | +0x0D byte: level loaded and sounds prepped; +0x9A8 byte: general ambient set index from the current player state; +0x9AA byte: music mode from the current player state; +0x9D4 int: number of entities in the current frame; +0x9D8 int: index of the frame's first entity in the ring (masked with 1023); +0x1A3C int: client time in milliseconds; +0x1F3C int: player number (the listener entity is player number + 1); +0x1F44: configstrings, 64 bytes each (configstring n is at +0x1F44 + 64·n); +0x3A004: the 356 sound-precache pointers (sfx_t*) |
| 2 | the array of 1024 client entities (396-byte records) | +0x74 entity number of the current state (for player-model-specific sounds) |
| 3 | the client static state | +0 connection state (8 = active, in game); +0x24 byte: client shutting down (non-zero forces silence) |
| 4 | the main window handle variable (HWND*) | DirectSound cooperative level |

Configstrings used by the libraries: 36 (map model name, used by EAXSnd), CS_PLAYERSKINS = 1415 (player n uses
1415 + n − 1; text `name\model/skin`).

## Behaviour

### Engine call sites

- **Client initialization:** load the library, then S_Init. **`snd_restart`:** S_Shutdown, reload the library
  (selection repeated), S_Init, then re-register all sounds. **Client shutdown:** S_Shutdown, then unload.
- **Level registration** (after the configstrings arrive): S_BeginRegistration; the engine's built-in sounds;
  S_RegisterSound for each sound configstring (CS_SOUNDS = 291, 356 entries); S_RegisterAmbientSet with
  configstring 12 (CS_AMBIENTSET: a file path, default `sound/sound.ams`); S_RegisterMusicSet with
  configstring 13 (CS_MUSICSET: the worldspawn `musicSet` key, a bare name); S_EndRegistration. The value of
  configstring 12 goes to the library unchanged, so it must be a full game path.
- A sound configstring changed later (svc_configstring 15) registers that sound at once. Clearing it
  (svc 35, range 291..646) calls S_FreeSound.
- **svc_sound (10):**
  - flags byte;
  - sound index byte (+256 if flag 0x20);
  - volume byte/255 if flag 0x01, else 1.0;
  - attenuation byte/64 if flag 0x02, else 1.0;
  - time offset byte × 0.001 s if flag 0x10, else 0;
  - if flag 0x08, a short holding entity × 8 + channel (entity ≤ 1024, channel 0..7);
  - if flag 0x04, a position.

  Then S_StartSound(position or NULL, entity, channel, precache[index], volume, attenuation, offset).
- **svc_sound_info (4):** a flags byte, then one byte for each set flag in this order: 0x01 →
  S_SetMusicIntensity, 0x02 → S_SetMusicDesignerInfo, 0x04 → S_SetGeneralAmbientSet,
  0x08 → S_SetSoundProcType. Any other bit is the error "Invalid soundinfo type...".
- **Every client frame:** S_Update(view origin, forward, right, up).
- **Window focus change:** S_Activate(1) when activated, S_Activate(0) when deactivated or minimized.
- Menus and client effects call S_StartLocalSound and S_StartSound. Disconnects and level changes call
  S_StopAllSounds.

### Default behaviour (Defsnd; A3Dsnd matches unless noted)

**Startup and shutdown**

- S_Init prints `------- sound initialization -------`.
  - If `s_initsound` is 0, it prints "not initializing." and sound stays off. Every other call is then
    harmless: S_RegisterSound returns NULL and S_StartSound does nothing.
  - Otherwise it creates the cvars and commands. It opens DirectSound (`dsound.dll` is loaded at run time) and
    prints "sound sampling rate: <Hz>".
  - Output uses DirectSound with a secondary buffer, or the primary buffer when `s_primary` is 1.
- S_Activate(0) releases the DirectSound buffers and sets the normal cooperative level. S_Activate(1)
  recreates them.

**Sounds and names**

- Up to **712** sounds; 32 mixing channels; 128 pending start requests.
- Sound names:
  - a leading `/` or `\` is removed, the name is lower-cased and `\` becomes `/`;
  - names of 64 characters or more are a fatal error "Sound name too long: <name>";
  - an empty name is fatal ("S_FindName: empty name") and so is a NULL name ("S_FindName: NULL");
  - running out of slots is fatal ("S_FindName: out of sfx_t").
- File lookup:
  - A name starting with `#` is a path from the game root without the `#`.
  - A name ending in `.adp` is ADPCM-compressed audio (layout UNVERIFIED).
  - Otherwise the file is `sound/<name>` and must be a RIFF/WAVE in PCM format ("Microsoft PCM format only").
    A `cue ` chunk sets the loop start, as in Quake II.
  - A file that cannot be loaded prints the developer message "Couldn't load <path>" and plays as silence.

**Registration**

- S_RegisterSound loads the data at once outside registration, or at S_EndRegistration during registration.
- S_EndRegistration frees every sound not referenced in the current sequence, then loads all remaining ones.

**Starting sounds**

- S_StartSound behaves as in Quake II:
  - volume is 0..1;
  - attenuation 0 means "no fall-off", 1 normal, 2 idle, 3 static (Quake II meanings; the distance
    constants are UNVERIFIED);
  - `timeofs` delays the start by that many seconds;
  - channel 0 never replaces another sound, and a non-zero channel replaces the sound already playing on the
    same entity and channel;
  - a NULL origin makes the sound follow the entity (through CL_GetEntitySoundOrigin), except for the
    listener's own entity, which plays at full volume in both ears.
- Player-model sounds: a sfx whose name starts with `*` is resolved through the entity's player configstring
  model to `players/<model>/<name without *>` (`#` path). If that file does not exist, the fallback is
  `player/male/<name without *>`.
- S_StartLocalSound(name) = S_StartSound(NULL, player number + 1, channel 0, sound, 0.6, 1.0, 0).

**S_Update**

- Output is silence when `s_nosound` or `s_disable` is set or the client is shutting down.
- A changed `s_volume` rebuilds the volume tables.
- Each frame it re-spatializes the channels and adds:
  - entity loop sounds (from the entity ring);
  - ambient sounds;
  - music.

  It then mixes `s_mixahead` seconds ahead.
- With `s_show` 1 it prints one line per audible channel and a summary of the painted count.
- While the cvar `paused` is non-zero, entity loop sounds are not added.

**Sound processing type**

- S_SetSoundProcType(t): t > 2 is the error (drop) "Invalid sound procedure value...".
- t = 0 sets an overall gain of 1.0. t = 1 sets an overall gain of 0.3, applied to every channel while
  mixing (the underwater muffle). t = 2 keeps the previous gain.

**Ambient sounds** (requires the client to be active and the level prepped)

- S_RegisterAmbientSet(path) loads a version-11 ambient file.
  - A missing file prints "Unable to locate <path> - all ambient sounds will be disabled".
  - A version mismatch prints "Invalid version (<v>) for Ambient Sound Set <path> (Should be 11)".
  - The file format belongs to a separate spec.
- The general ambient set is the one named by the player-state field (struct #1 +0x9A8), or the one set by
  S_SetGeneralAmbientSet(i).
- Changing the set crossfades from the old set to the new one over 2000 ms. Each set has a looping sound plus
  random one-shot sounds at randomized intervals.
- Entities flagged as ambient-point sources play their point set's loop when the listener is within the set's
  radius, plus random one-shots.

**Music**

- S_RegisterMusicSet(name) only acts while `s_musicenabled` is non-zero. Names that are empty or `(null)` are
  ignored.
  - A missing file prints "<name> isn't a valid music set name".
  - A version other than 11 prints "Invalid version for Dynamic Music Set <name> (<v> should be 11)".
  - Loading resets the music state.
- The music mode comes from the player-state byte (struct #1 +0x9AA) each frame:
  - 1 = fade out;
  - 2 = "calm" track (label UNVERIFIED);
  - 3 = "action" track (label UNVERIFIED);
  - n ≥ 4 = designer track n − 3.

  Fades take 10000–15000 output samples.
- S_SetMusicDesignerInfo(v): 0xFF stops the designer track, 0 fades all tracks quickly, any other v starts
  designer track v.
- S_SetMusicIntensity: Defsnd and A3Dsnd store the value and never use it. The call has no audible effect.
- Console command `forcesong <name>` sets `s_musicenabled` 1 and loads music set `<name>`.

### EAXSnd differences (must be tolerated by a replacement engine)

- It plays through DirectSound3D hardware voices with EAX 2.0 and prints "<n> available 3D hardware voices".
- If `eaxman.dll` is present, it uses it. It then loads a per-map environment file `<map>.eal`, named after
  configstring 36 with the extension replaced.
- Without such a file, the map's file name picks a preset EAX environment:
  - `tsr1.bsp` → 12
  - `trn1.bsp` → 19
  - `arm1.bsp` → 4
  - `kos1.bsp` → 8
- Up to **1380** sounds. Name problems are not fatal: a NULL, empty or over-64-character name returns NULL,
  and running out of slots prints "Sound Overflow: Unable to create <name>" and returns NULL.
- S_StartSound ignores `timeofs` and does not resolve `*` player-model sounds. It clamps volume to 0..1. An
  attenuation below 0.01 plays the sound non-positionally.
- S_StartLocalSound plays with entity 0, volume 0.6 and attenuation 1.0.
- S_SetSoundProcType: 0 restores the level's EAX environment; 1 selects EAX environment 22 (underwater);
  anything else is the drop error "Invalid sound procedure value...".
- S_SetMusicIntensity is used: it crossfades to intensity track `value` at rate 0.005.
- The music-mode byte also drives music: 2 → intensity 0, 3 → intensity 2.
- S_Activate does nothing.
- It creates only the cvars `s_initsound`, `s_volume`, `s_musicvolume` (default **1.0**), `s_nosound`,
  `s_disable`, `s_musicenabled` and `paused`. Its only commands are `snd_eaxparams`, `snd_eaxenvironment` and
  `stopsound`. There is no `play`, `soundlist`, `soundinfo` or `forcesong`.
- It does not use the entity ring (struct #0) for looping sounds in the same way. UNVERIFIED.

### A3Dsnd differences

- Same behaviour as Defsnd plus an A3D 2.0 3-D path.
- It keeps the renderer export table it receives through S_A3D_ExportRenderGeom, for geometry-based
  occlusion and reflections.
- Extra cvars and commands: see Constants.

## Constants

Cvars (name, default, archive flag 1 = saved):

| Cvar | Defsnd | A3Dsnd | EAXSnd | Meaning |
|---|---|---|---|---|
| s_initsound | "1" | "1" | "1" | 0 = do not start sound |
| s_volume | "0.7" (A) | "0.7" (A) | "0.7" (A) | effects volume |
| s_khz | "22" (A) | "22" (A) | — | output rate in kHz (11/22/44) |
| s_loadas8bit | "0" (A) | "0" (A) | — | |
| s_nosound | "0" (A) | "0" (A) | "0" (A) | silence |
| s_disable | "0" | "0" | "0" | silence |
| s_musicenabled | "1" (A) | "1" (A) | "1" (A) | |
| s_musicvolume | "0.7" (A) | "0.7" (A) | "1.0" (A) | |
| s_mixahead | "0.2" (A) | "0.2" (A) | — | seconds |
| s_show | "0" | "0" | — | debug |
| s_testsound | "0" | "0" | — | |
| s_primary | "0" (A) | "0" (A) | — | write to the primary buffer |
| cpu_mmx, cpu_amd3d | "0" | "0" | — | read-only CPU flags |
| use_mmx, use_amd3d | "1" | "1" | — | |
| paused | "0" | "0" | "0" | |

A3Dsnd adds the cvars s_rolloff "1.0", s_doppler "1.0", s_distance "50", s_min_distance "16",
s_max_distance "1000", s_auto_attn "0.0003", s_attn "0.0007", s_2dvolume "0.8", s_watereq "0.1",
s_a3d_weapons "1", s_geometry "1", s_leafnum "0", s_refgain "0.60", s_refdelay "4.5", s_polykeep "10000000",
s_polysize "50000", s_polyreflectsize "10000000", s_numpolys "400", s_usepvs "1", s_showtossed "0",
s_bloat "2.0", s_occlude "1", s_reflect "1", s_geom_reverb "1", s_geom_reverb_scale "0.5" and
s_occfactor "0.2". It also reads cl_camera_under_surface, menus_active and cl_cinematicfreeze.

Commands:

- Defsnd and A3Dsnd: `play <sound…>` (adds `.wav` if there is no extension and plays each argument as a local
  sound), `stopsound`, `soundlist`, `soundinfo`, `forcesong <name>`.
- A3Dsnd also: `enable_a3d`, `disable_a3d`.
- EAXSnd: `snd_eaxparams`, `snd_eaxenvironment`, `stopsound`.
- Engine: `snd_restart`; cvar `snd_dll`.

Limits: 712 sounds (D/A), 1380 (E); 356 sound configstrings starting at 291; 1024 entities; ambient and music
file version 11; entity channel 0..7.

## Test vectors / how to verify

- Set `snd_dll ""` and run on hardware without A3D or EAX: the console shows "Setting Default Sound support"
  then "------- Loading defsnd.dll -------", and `snd_dll` is saved as `defsnd.dll`.
- Set `snd_dll "eaxsnd.dll"` on a machine without EAX: "Attempting EAX 2.0 support", "EAX NOT supported",
  "Setting Default Sound support", then "------- Loading defsnd.dll -------".
- `play misc/menu1` with Defsnd plays `sound/misc/menu1.wav`. With EAXSnd it is an unknown command
  (UNVERIFIED: assumes the engine registers no `play` of its own; see SPEC-005).
- Send svc_sound_info with flags 0x08 and value 1 (Defsnd): the volume of all effects drops to about 30 %.
  Value 3 drops the client with "Invalid sound procedure value...".
- `s_initsound 0` then `snd_restart`: "not initializing." and no sound, without errors.

## Open questions

- Exact A3D and EAX detection criteria. A3D use of the renderer table.
- ADPCM (.adp) data layout and decoder.
- EAXSnd looping-sound and ambient handling compared with Defsnd (EAXSnd has its own ambient classes).
- What triggers S_Touch (the engine calls it together with a renderer memory touch).
- Whether any 1.07f code path calls SNDEAX_SetEnvironment (none found in SoF.exe or gamex86).
