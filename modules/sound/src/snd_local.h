/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module (OMsnd.dll): shared declarations.
 *
 * OMsnd keeps the game-facing behaviour of the default backend (Defsnd) of SPEC-040: the Quake II 3.19
 * channel rules (snd_dma.c, GPL) with the SoF additions (ambient sets, dynamic music, .adp, underwater gain,
 * `*` player sounds), the SoF attenuation and volume scale. Since 1.0 (docs/impl/SND2.md) there is no
 * software mixer: every channel is an OpenAL Soft source (snd_voice.c) with the SoF gain computed here and
 * the direction in listener space, music tracks are streamed sources, and the EAX environment is an EFX
 * reverb every game voice sends to.
 *
 * Implements: SPEC-004 (sound module interface), SPEC-AMEND-001 A (engine functions), SPEC-040 (backend
 *             behaviour), SPEC-110 (.adp), SPEC-111 (.dms), SPEC-112 (.ams), SPEC-113 (.eal, EAX option)
 */
#ifndef OM_SND_LOCAL_H
#define OM_SND_LOCAL_H

#include <stddef.h>
#include "../../../engine/src/common/header/om_platform.h" /* OpenMerc: MSVC names on other platforms */

typedef float vec_t;
typedef vec_t vec3_t[3];
typedef unsigned char byte;
typedef int qboolean;
#ifndef true
#define true 1
#define false 0
#endif

#define OMSND_VERSION "1.0"

/* ---- engine objects --------------------------------------------------------------------------------- */

/* cvar object returned by the engine's Cvar_Get (SPEC-002 "cvar object", 32 bytes) */
typedef struct cvar_s {
    char *name;
    char *string;
    char *latched_string;
    int flags;
    void (*changed)(struct cvar_s *);
    int modified;
    float value;
    struct cvar_s *next;
} cvar_t;

#define CVAR_ARCHIVE 1
#define CVAR_NOSET 8
#define ERR_FATAL 0
#define ERR_DROP 1

#define MAX_QPATH 64

/* Sound record handed to the engine as an opaque pointer (SPEC-004 "sfx_t"): name at offset 0 (64 bytes),
 * registration sequence at 64, cache pointer at 68; 72 bytes. Alias targets (`*` sounds) are kept in a
 * side table (snd_dma.c). */
typedef struct sfxcache_s sfxcache_t;
typedef struct sfx_s {
    char name[MAX_QPATH];
    int registration_sequence;
    sfxcache_t *cache;
} sfx_t;

/* Loaded sound data: one OpenAL buffer at the file's own rate (OMsnd 1.0 does not resample to a mixer rate) */
struct sfxcache_s {
    int length;     /* duration in clock samples (SND_CLOCK_RATE) */
    int frames;     /* samples in the buffer */
    int loopstart;  /* buffer sample of the cue loop start, or -1 */
    int speed;      /* the file's rate */
    int width;      /* 1 or 2 bytes (PCM); 2 for .adp */
    int stereo;
    qboolean adpcm; /* SPEC-040 1.3: `.adp` rules (non-positional, held while paused) */
    qboolean voice; /* OpenMerc: speech (`.adp` under sound/cin, sound/speech, sound/enemy), s_om_voicevolume */
    int bytes;      /* size of the sample data (soundlist) */
    unsigned buffer;        /* OpenAL buffer (with the cue loop points when loopstart >= 0) */
    unsigned buffer_whole;  /* the same samples without loop points, for autosound loops (made on demand) */
    void *data;             /* kept only while buffer_whole may still be needed (loopstart > 0) */
};
/* Functions SoF.exe exports by name for the sound libraries (SPEC-004, SPEC-AMEND-001 A). Resolved at
 * S_Init with GetProcAddress on the executable module (so any executable name works). */
typedef struct {
    void (*Com_Printf)(const char *fmt, ...);
    void (*Com_DPrintf)(const char *fmt, ...);
    void (*Com_Error)(int code, const char *fmt, ...);
    /* four arguments: the change callback (cvar record +0x10) is NULL, as in every stock sound module
     * (SPEC-AMEND-006 I5s-2.4) */
    cvar_t *(*Cvar_Get)(const char *name, const char *value, int flags, void (*changed)(cvar_t *));
    void (*Cmd_AddCommand)(const char *name, void (*fn)(void));
    void (*Cmd_RemoveCommand)(const char *name);
    int (*Cmd_Argc)(void);
    char *(*Cmd_Argv)(int i);
    int (*FS_LoadFile)(const char *name, void **buf, int unused);
    void (*FS_FreeFile)(void *buf);
    int (*FS_FOpenFile)(const char *name, int *handle);
    void (*FS_FCloseFile)(int handle);
    int (*irand)(int min, int max);
    void (*CL_GetEntitySoundOrigin)(int entnum, float *out);
} engine_t;

extern engine_t eng;

/* ---- snd_engine.c: imports and the shared client data (SPEC-004 "The sound struct") ------------------ */

qboolean Eng_Resolve(void); /* false when a mandatory import is missing */
void Eng_SetSoundStruct(void *const *ptrs);
qboolean Eng_HaveStruct(void);
qboolean Eng_WindowHidden(void);

void Snd_Printf(const char *fmt, ...);
void Snd_DebugString(const char *s); /* debugger output (Windows) or stderr */
void Snd_DPrintf(const char *fmt, ...);
unsigned Snd_Milliseconds(void);
void Snd_Sleep(unsigned ms);
int Snd_Rand(int min, int max); /* engine irand when present, else own generator */
int Snd_LoadFile(const char *path, void **buf);
void Snd_FreeFile(void *buf);
qboolean Snd_FileExists(const char *path);

#define MAX_PARSE_ENTITIES 1024
#define MAX_EDICTS 1024
#define CS_AMBIENTSET 12
#define CS_MUSICSET 13
#define CS_MAPMODEL 36
#define CS_SOUNDS 291
#define MAX_SOUNDS 356        /* the 1.07f sound table (cl +0x3A004) */
#define OM_MAX_SOUNDS 512     /* OpenMerc extension: indexes 356..511 at cl +OM_CL_SOUNDS_EXT (engine sof_sound.h) */
#define OM_CL_SOUNDS_EXT 0x3C000
#define CS_PLAYERSKINS 1415
#define CA_ACTIVE 8

int CL_State(void);                     /* cls +0 */
qboolean CL_ShuttingDown(void);         /* cls +0x24 */
qboolean CL_Prepped(void);              /* cl +0x0D */
int CL_GeneralAmbientField(void);       /* cl +0x9A8, read as a 16-bit value (SPEC-040 3.1) */
int CL_MusicMode(void);                 /* cl +0x9AA */
int CL_FrameNumEntities(void);          /* cl +0x9D4 */
int CL_FrameFirstEntity(void);          /* cl +0x9D8 */
int CL_Time(void);                      /* cl +0x1A3C */
int CL_PlayerNum(void);                 /* cl +0x1F3C */
const char *CL_ConfigString(int n);     /* cl +0x1F44 + 64 n */
sfx_t *CL_SoundPrecache(int i);         /* cl +0x3A004, from index 356 cl +0x3C000 */
int CL_EntityCurrentNumber(int entnum); /* cl_entities[entnum] +0x74 */

/* one parsed entity state of the current frame (ring record, 116 bytes) */
typedef struct {
    int number;
    vec3_t origin;
    int flags;
    int sound;
    int loopparm;
} frame_entity_t;
qboolean CL_FrameEntity(int i, frame_entity_t *out);

#define ENTFLAG_AMBIENT_POINT 0x100

/* ---- channels and voices (snd_dma.c / snd_voice.c) --------------------------------------------------- */

/* The sound clock. SoF's timing rules (pending start times, music fades, loop phases) are written in output
 * samples of the 22050 Hz mixer (SPEC-040 "Terms"). OMsnd 1.0 has no mixer; `paintedtime` is that clock,
 * driven by the wall clock in S_Update, and every rule keeps its numbers. */
#define SND_CLOCK_RATE 22050

typedef struct playsound_s {
    struct playsound_s *prev, *next;
    sfx_t *sfx;
    float volume;
    float attenuation;
    int entnum;
    int entchannel;
    qboolean fixed_origin;
    vec3_t origin;
    unsigned begin;
} playsound_t;

/* result of the SoF spatialization (SPEC-040 1.1) in the form a 3-D voice needs: the volume on SoF's
 * 0-255 scale after distance attenuation, the direction in listener space (x right, y up, z back, OpenAL
 * convention) and how far the near-radius term pulls the sound towards "all around" (0 = point, 1 = none) */
typedef struct {
    float vol;
    float spread;        /* b of SPEC-040 1.1 */
    qboolean positional; /* false: no direction (dist_mult 0, own entity, menus, b = 1) */
    vec3_t dir;
} spat_t;

typedef struct {
    sfx_t *sfx;
    int entnum;
    int entchannel;
    vec3_t origin;
    vec_t dist_mult;
    int master_vol;       /* 0-255 */
    qboolean fixed_origin;
    qboolean autosound;   /* loop sound, kept while Amb_AddSounds re-adds it every frame */
    int loopkey;          /* autosound identity (set index, entity) */
    int touched;          /* frame number of the last re-add (autosound) */
    vec_t near_radius;    /* SPEC-040 1.1 N: ambient point-set loops only */
    spat_t sp;            /* last spatialization */
    float gain;           /* last OpenAL gain (voice stealing priority) */
    qboolean adp_held;    /* `.adp` paused while the game is paused or not in game */
    qboolean local;       /* the listener's own sound (S_IsLocalSound): head-relative at its full volume,
                             never culled by distance, never stolen by a world sound */
} channel_t;

#define MAX_CHANNELS 64

extern channel_t channels[MAX_CHANNELS];
extern int paintedtime;  /* sound clock, SND_CLOCK_RATE samples */
extern int s_framecount;
extern vec3_t listener_origin, listener_forward, listener_right, listener_up;
extern playsound_t s_pendingplays;
extern float s_procgain;                /* SPEC-040 1.2 g: 1.0, or 0.3 after S_SetSoundProcType(1) */
extern int s_proctype;
extern qboolean s_started;

extern cvar_t *s_volume, *s_nosound, *s_show, *s_testsound;
extern cvar_t *s_musicenabled, *s_musicvolume, *s_disable, *paused, *om_debug, *om_reverb;
extern cvar_t *om_rate, *om_hrtf, *om_limiter, *om_refresh, *om_gain, *om_underwaterlp;
extern cvar_t *om_device, *om_voicevolume;

qboolean Snd_ClientActive(void); /* connection state active and level prepped */
sfx_t *S_FindName(const char *name, int create);
sfxcache_t *S_LoadSound(sfx_t *s);
void S_IssuePlaysound(playsound_t *ps);
channel_t *S_PickChannel(int entnum, int entchannel, qboolean local);
void S_Spatialize(channel_t *ch);
/* the listener's own sound: head-relative, full volume, not culled (snd_dma.c) */
qboolean S_IsLocalSound(int entnum, qboolean fixed_origin, const vec3_t origin, float attenuation);
void S_SpatializeOrigin(const vec3_t origin, float master_vol, float dist_mult, float near_radius, spat_t *out);
/* starts a sound with a 0-255 master volume (the ambient code works on that scale, SPEC-040 3.3) */
void S_StartSoundVol(const float *origin, int entnum, int entchannel, sfx_t *sfx, int vol, float attenuation,
                     float timeofs);
/* an autosound loop for this frame (SPEC-040 2, 3): kept playing while re-added with the same key */
void S_AddLoop(sfx_t *sfx, sfxcache_t *sc, const spat_t *sp, int pos, int key);
void S_FreeCache(sfx_t *s);

/* ---- snd_voice.c: OpenAL Soft device, voices, gain staging --------------------------------------------- */

qboolean Vo_Init(const char *wavefile);
void Vo_Shutdown(void);
const char *Vo_Describe(void);
void *Vo_Device(void);
void Vo_BeginFrame(void);                /* defers OpenAL updates until Vo_EndFrame */
void Vo_EndFrame(void);
qboolean Vo_CheckDevice(void);           /* reopens a lost device; false while there is none */
void Vo_ApplySettings(void);             /* s_om_device / s_om_hrtf / s_om_limiter / s_om_rate changes, live */
unsigned Vo_MakeBuffer(const void *data, int frames, int width, int rate, int loopstart);
void Vo_DeleteBuffer(unsigned buf);
void Vo_Start(channel_t *ch, sfxcache_t *sc, int clockpos); /* clockpos: start offset in clock samples */
void Vo_Stop(channel_t *ch);
qboolean Vo_Finished(channel_t *ch);
void Vo_Apply(channel_t *ch);            /* gain, direction, sends from ch->sp */
void Vo_Pause(channel_t *ch, qboolean pause);
int Vo_PlayedMs(channel_t *ch, int *total_ms); /* diagnostics: position and length in ms */
void Vo_SetMasterMute(qboolean mute);
void Vo_TestTone(qboolean on);
void Vo_SetUnderwater(qboolean on);
/* ---- snd_adp.c (SPEC-110) ----------------------------------------------------------------------------- */

qboolean ADP_Parse(const byte *data, int len, int *rate, const byte **payload, int *nbytes);
void ADP_DecodeTo(const byte *payload, int nbytes, short *out, int nsamples); /* first nsamples, zero-padded */
int ADP_Ratio(int rate, int outrate);       /* SPEC-040 1.3 ratio rule: k > 0, -2, -4 or 0 */
int ADP_OutLength(int srclen, int ratio, int rate, int outrate);
int ADP_SourceIndex(int outpos, int ratio); /* source sample for an output position */

/* ---- snd_ambient.c (SPEC-040 2-4, SPEC-112) -------------------------------------------------------------- */

void Amb_Init(void);
void Amb_Register(const char *path);
void Amb_EndRegistration(void);
void Amb_ForgetSfx(const sfx_t *s);       /* a sound record is being freed */
void Amb_SetGeneral(int index);
void Amb_AddSounds(void);                /* ambient sets and entity loop sounds, once per frame */
void Amb_Shutdown(void);
void Amb_Info(void);

/* ---- snd_music.c (SPEC-040 5, SPEC-111) ------------------------------------------------------------------- */

void Mus_Init(void);
void Mus_Register(const char *name);
void Mus_Force(const char *name, qboolean have_name);
void Mus_SetIntensity(int value);
void Mus_SetDesignerInfo(int value);
void Mus_Update(void);                   /* state machine and fades, once per frame */
void Mus_Stop(void);                     /* silence the music sources at once (stopsound) */
void Mus_Shutdown(void);
void Mus_Info(void);
int Mus_TakeDryCount(void);              /* s_om_debug 16: music sources that ran out of queued samples */

/* ---- snd_efx.c (EAX environments as an EFX reverb slot every game voice sends to, s_om_reverb) ---------- */

void EFX_Init(void *alcdevice);
void EFX_Shutdown(void);
unsigned EFX_Slot(void);                 /* 0 when reverb is off */
unsigned EFX_UnderwaterFilter(void);     /* low-pass filter for processing type 1, 0 if none */
void EFX_SetMapEnvironment(const char *mapmodel);
void EFX_SetEnvironment(int env);        /* EAX 2.0 environment index 0..25 */
void EFX_SetProcType(int type);
void EFX_Info(void);

#define EAX_ENVIRONMENT_COUNT 26
#define EAX_ENVIRONMENT_UNDERWATER 22

#endif
