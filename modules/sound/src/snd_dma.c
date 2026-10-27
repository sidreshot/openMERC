/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc: Quake II 3.19 client/snd_dma.c (main control of the sound system), turned into the S_* exports
 * of a SoF sound module and changed to the default backend of SPEC-040: SoF name rules and limits
 * (SPEC-004), the near-radius term of the spatialization (1.1), menus at full volume, `.adp` sounds
 * non-positional (1.3), player-model sounds through the entity's player configstring (1.4), the S_Update
 * order of 1.5 with music and ambient sets, the sound-processing gain, the engine's client data through the
 * sound struct.
 *
 * OMsnd 1.0 (docs/impl/SND2.md): no mixer and no DMA ring. Every channel is an OpenAL Soft voice
 * (snd_voice.c) that S_Update re-spatializes every frame; `paintedtime` is a 22050 Hz clock driven by the
 * wall clock, so the start-time rules (s_beginofs drift, time offsets) and the music fades keep their numbers.
 * Autosound loops keep their voice while re-added (S_AddLoop). A full channel table steals the free voice,
 * else the quietest one (Quake II: the one that ends first), with Quake II's override rules.
 *
 * Implements: SPEC-004, SPEC-AMEND-001 A, SPEC-040 1, 1.1, 1.3, 1.4, 1.5, 1.6, 4,
 *             SPEC-AMEND-006 I5s-2.1, I5s-2.3, I5s-2.4, I5s-2.5
 */
// snd_dma.c -- main control for any streaming sound output device

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snd_local.h"

static void S_Play(void);
static void S_SoundList(void);
static void S_SoundInfo_f(void);
static void S_ForceSong_f(void);
static void S_EaxEnvironment_f(void);
static void S_EaxParams_f(void);
void S_StopAllSounds(void);

// =======================================================================
// Internal sound data & structures
// =======================================================================

// only begin attenuating sound volumes when outside the FULLVOLUME range
#define SOUND_FULLVOLUME 80

#define ATTN_STATIC 3

static int s_registration_sequence;

channel_t channels[MAX_CHANNELS];

qboolean s_started;


vec3_t listener_origin;
vec3_t listener_forward;
vec3_t listener_right;
vec3_t listener_up;

static qboolean s_registering;

int paintedtime;  // the sound clock (SND_CLOCK_RATE)
int s_framecount;
static unsigned clock_base_ms;

// SoF Defsnd: 712 sounds (SPEC-004 "Sounds and names")
#define MAX_SFX 712
static sfx_t known_sfx[MAX_SFX];
static char *truenames[MAX_SFX];  // S_AliasName targets (the SoF sfx_t has no truename field)
static int num_sfx;

#define MAX_PLAYSOUNDS 128
static playsound_t s_playsounds[MAX_PLAYSOUNDS];
static playsound_t s_freeplays;
playsound_t s_pendingplays;

static int s_beginofs;

float s_procgain = 1.0f;
int s_proctype;

cvar_t *s_volume;
cvar_t *s_testsound;
cvar_t *s_show;
cvar_t *s_nosound, *s_disable, *s_musicenabled, *s_musicvolume, *paused;
cvar_t *om_wavefile, *om_debug, *om_focusmute, *om_reverb;
cvar_t *om_rate, *om_hrtf, *om_limiter, *om_refresh, *om_gain, *om_underwaterlp;
cvar_t *om_device, *om_voicevolume;
static cvar_t *om_stall, *om_stallmin, *om_stallmax;
static cvar_t *s_initsound;

static qboolean cmds_added;
static qboolean app_active = true;

/*
OMsnd 1.0 focus rule: losing the focus never silences a visible game. s_om_focusmute 1 mutes only while the
window is out of focus AND minimised or hidden, so a borderless fullscreen window that lost the focus to a
second monitor, an overlay or a sound-card control panel keeps playing (configs written by OMsnd 0.x carry
s_om_focusmute "1", which used to mute on every focus change).
*/
static qboolean S_FocusMuted(void)
{
    return !app_active && om_focusmute->value && Eng_WindowHidden();
}

// ====================================================================
// helpers
// ====================================================================

qboolean Snd_ClientActive(void)
{
    return Eng_HaveStruct() && CL_State() == CA_ACTIVE && CL_Prepped();
}

const char *S_TrueName(const sfx_t *s)
{
    int i = (int)(s - known_sfx);
    if (i >= 0 && i < MAX_SFX && truenames[i])
        return truenames[i];
    return s->name;
}

/* s_om_debug 8: a voice stopped before its sound ended (cut-off phrases); 16: per-second counts of loop restarts
 * and of large gain steps between frames (stutter / tremble) */
static int dbg_loop_restarts, dbg_gain_steps, dbg_spat_flips, dbg_cuts, dbg_last_ms;

static void log_cut(channel_t *ch, const char *why)
{
    int pos, total;
    if (!ch->sfx)
        return;
    if (!ch->autosound)
        dbg_cuts++;
    if (!((int)om_debug->value & 8) || ch->autosound || Vo_Finished(ch))
        return;
    pos = Vo_PlayedMs(ch, &total);
    if (total - pos < 30)
        return; /* ended anyway */
    Snd_Printf("OMsnd: [p=%d] cut (%s) %s ent %d ch %d%s at %d of %d ms\n", paintedtime, why, ch->sfx->name,
               ch->entnum, ch->entchannel, ch->sfx->cache && ch->sfx->cache->adpcm ? " adp" : "", pos, total);
}

#define DotProduct(a, b) ((a)[0] * (b)[0] + (a)[1] * (b)[1] + (a)[2] * (b)[2])

static float VectorNormalizeLength(vec3_t v)
{
    float length = (float)sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length) {
        float ilength = 1 / length;
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    }
    return length;
}

/* SoF name rules (SPEC-004): one leading `/` or `\` removed, lower case, `\` -> `/`; fatal errors */
static void normalize_name(const char *in, char *out)
{
    size_t len, i;
    if (!in)
        eng.Com_Error(ERR_FATAL, "S_FindName: NULL\n");
    if (*in == '/' || *in == '\\')
        in++;
    if (!in[0])
        eng.Com_Error(ERR_FATAL, "S_FindName: empty name\n");
    len = strlen(in);
    if (len >= MAX_QPATH)
        eng.Com_Error(ERR_FATAL, "Sound name too long: %s", in);
    for (i = 0; i <= len; i++) {
        char c = in[i];
        if (c == '\\')
            c = '/';
        else if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[i] = c;
    }
}

// ====================================================================
// User-setable variables
// ====================================================================

static void S_SoundInfo_f(void)
{
    if (!s_started) {
        Snd_Printf("sound system not started\n");
        return;
    }

    Snd_Printf("OMsnd %s: %s\n", OMSND_VERSION, Vo_Describe());
    Snd_Printf("clock %d, frame %d, processing type %d (gain %.2f), focus %s%s\n", paintedtime, s_framecount,
               s_proctype, s_procgain, app_active ? "yes" : "no",
               S_FocusMuted() ? " (muted: s_om_focusmute, window minimised)" : "");
    Mus_Info();
    Amb_Info();
    if (om_reverb->value)
        EFX_Info();
}
/*
================
S_Init
================
*/
void S_Init(void)
{
    if (!Eng_Resolve()) {
        Snd_DebugString("OMsnd: engine exports missing, sound disabled\n");
        return;
    }
    Snd_Printf("\n------- sound initialization -------\n");

    s_initsound = eng.Cvar_Get("s_initsound", "1", 0, NULL);
    if (!s_initsound->value)
        Snd_Printf("not initializing.\n");
    else {
        s_volume = eng.Cvar_Get("s_volume", "0.7", CVAR_ARCHIVE, NULL);
        s_nosound = eng.Cvar_Get("s_nosound", "0", CVAR_ARCHIVE, NULL);
        s_disable = eng.Cvar_Get("s_disable", "0", 0, NULL);
        s_musicenabled = eng.Cvar_Get("s_musicenabled", "1", CVAR_ARCHIVE, NULL);
        s_musicvolume = eng.Cvar_Get("s_musicvolume", "0.7", CVAR_ARCHIVE, NULL);
        s_show = eng.Cvar_Get("s_show", "0", 0, NULL);
        s_testsound = eng.Cvar_Get("s_testsound", "0", 0, NULL);
        // legacy mixer cvars, kept so configs and menus that set them still work; no effect since OMsnd 1.0
        eng.Cvar_Get("s_khz", "22", CVAR_ARCHIVE, NULL);         // no-op: OpenAL runs at s_om_rate
        eng.Cvar_Get("s_loadas8bit", "0", CVAR_ARCHIVE, NULL);   // no-op: sounds keep their own width
        eng.Cvar_Get("s_mixahead", "0.2", CVAR_ARCHIVE, NULL);   // no-op: no software mix-ahead
        eng.Cvar_Get("s_primary", "0", CVAR_ARCHIVE, NULL);      // no-op: DirectSound primary buffer
        eng.Cvar_Get("cpu_mmx", "0", CVAR_NOSET, NULL);
        eng.Cvar_Get("cpu_amd3d", "0", CVAR_NOSET, NULL);
        eng.Cvar_Get("use_mmx", "1", 0, NULL);
        eng.Cvar_Get("use_amd3d", "1", 0, NULL);
        paused = eng.Cvar_Get("paused", "0", 0, NULL);
        // OpenMerc additions
        om_wavefile = eng.Cvar_Get("s_om_wavefile", "", 0, NULL);
        om_debug = eng.Cvar_Get("s_om_debug", "0", 0, NULL);
        om_stall = eng.Cvar_Get("s_om_stall", "0", 0, NULL); // test: percent of frames stalled (a slow game frame)
        om_stallmin = eng.Cvar_Get("s_om_stallmin", "30", 0, NULL); // test: stall length range (ms)
        om_stallmax = eng.Cvar_Get("s_om_stallmax", "80", 0, NULL);
        // 0: keep playing when the window loses focus (borderless fullscreen loses it to overlays and
        // second screens); 1: Defsnd's silence while inactive
        om_focusmute = eng.Cvar_Get("s_om_focusmute", "0", CVAR_ARCHIVE, NULL);
        om_reverb = eng.Cvar_Get("s_om_reverb", "1", CVAR_ARCHIVE, NULL);
        om_rate = eng.Cvar_Get("s_om_rate", "48000", CVAR_ARCHIVE, NULL);       // device rate (Hz)
        om_hrtf = eng.Cvar_Get("s_om_hrtf", "0", CVAR_ARCHIVE, NULL);           // 1 on, 0 off, -1 driver default
        om_limiter = eng.Cvar_Get("s_om_limiter", "1", CVAR_ARCHIVE, NULL);     // soft output limiter
        om_refresh = eng.Cvar_Get("s_om_refresh", "100", CVAR_ARCHIVE, NULL);   // device updates per second
        om_gain = eng.Cvar_Get("s_om_gain", "1", CVAR_ARCHIVE, NULL);           // master gain after s_volume
        om_underwaterlp = eng.Cvar_Get("s_om_underwaterlp", "1", CVAR_ARCHIVE, NULL);
        // output device by its OpenAL name ("default": the system default output); applied live
        om_device = eng.Cvar_Get("s_om_device", "default", CVAR_ARCHIVE, NULL);
        // speech volume (cinematic, enemy and briefing `.adp` lines); a new profile starts at s_volume
        om_voicevolume = eng.Cvar_Get("s_om_voicevolume", s_volume->string, CVAR_ARCHIVE, NULL);

        eng.Cmd_AddCommand("play", S_Play);
        eng.Cmd_AddCommand("stopsound", S_StopAllSounds);
        eng.Cmd_AddCommand("soundlist", S_SoundList);
        eng.Cmd_AddCommand("soundinfo", S_SoundInfo_f);
        eng.Cmd_AddCommand("forcesong", S_ForceSong_f);
        eng.Cmd_AddCommand("snd_eaxenvironment", S_EaxEnvironment_f);
        eng.Cmd_AddCommand("snd_eaxparams", S_EaxParams_f);
        cmds_added = true;

        if (!Vo_Init(om_wavefile->string)) {
            Snd_Printf("OMsnd: no sound output, sound disabled\n");
            Snd_Printf("------------------------------------\n");
            return;
        }
        EFX_Init(Vo_Device());

        s_started = 1;
        num_sfx = 0;

        paintedtime = 0;
        s_framecount = 0;
        clock_base_ms = Snd_Milliseconds();
        s_beginofs = 0;
        s_procgain = 1.0f;  // reset only by S_Init or S_SetSoundProcType(0) (SPEC-040 1.2)
        s_proctype = 0;
        app_active = true;

        Snd_Printf("sound sampling rate: %i\n", om_rate->value >= 8000 ? (int)om_rate->value : 48000);

        S_StopAllSounds();
        Amb_Init();
        Mus_Init();
    }

    Snd_Printf("------------------------------------\n");
}

// =======================================================================
// Shutdown sound engine
// =======================================================================

static void free_sfx_record(int i)
{
    S_FreeCache(&known_sfx[i]);
    free(truenames[i]);
    truenames[i] = NULL;
    memset(&known_sfx[i], 0, sizeof(sfx_t));
}

void S_Shutdown(void)
{
    int i;

    if (cmds_added) {
        eng.Cmd_RemoveCommand("play");
        eng.Cmd_RemoveCommand("stopsound");
        eng.Cmd_RemoveCommand("soundlist");
        eng.Cmd_RemoveCommand("soundinfo");
        eng.Cmd_RemoveCommand("forcesong");
        eng.Cmd_RemoveCommand("snd_eaxenvironment");
        eng.Cmd_RemoveCommand("snd_eaxparams");
        cmds_added = false;
    }

    if (!s_started)
        return;

    S_StopAllSounds();
    Mus_Shutdown();
    Amb_Shutdown();

    // free all sounds (their OpenAL buffers go before the device)
    for (i = 0; i < num_sfx; i++)
        if (known_sfx[i].name[0] || truenames[i])
            free_sfx_record(i);
    num_sfx = 0;

    Vo_Shutdown();  // and the EFX objects
    s_started = 0;
}

// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_FindName

==================
*/
static sfx_t *FindName(const char *name, qboolean create)
{
    int i;
    sfx_t *sfx;
    char n[MAX_QPATH];

    normalize_name(name, n);

    // see if already loaded
    for (i = 0; i < num_sfx; i++)
        if (!strcmp(known_sfx[i].name, n))
            return &known_sfx[i];

    if (!create)
        return NULL;

    // find a free sfx
    for (i = 0; i < num_sfx; i++)
        if (!known_sfx[i].name[0])
            break;

    if (i == num_sfx) {
        if (num_sfx == MAX_SFX)
            eng.Com_Error(ERR_FATAL, "S_FindName: out of sfx_t");
        num_sfx++;
    }

    sfx = &known_sfx[i];
    free_sfx_record(i);
    strcpy(sfx->name, n);
    sfx->registration_sequence = s_registration_sequence;

    return sfx;
}

sfx_t *S_FindName(const char *name, int create)
{
    if (!s_started)
        return NULL;
    return FindName(name, create != 0);
}

/*
==================
S_AliasName

==================
*/
static sfx_t *S_AliasName(const char *aliasname, const char *truename)
{
    sfx_t *sfx;
    int i;
    char n[MAX_QPATH];

    normalize_name(aliasname, n);

    // find a free sfx
    for (i = 0; i < num_sfx; i++)
        if (!known_sfx[i].name[0])
            break;

    if (i == num_sfx) {
        if (num_sfx == MAX_SFX)
            eng.Com_Error(ERR_FATAL, "S_FindName: out of sfx_t");
        num_sfx++;
    }

    sfx = &known_sfx[i];
    free_sfx_record(i);
    strcpy(sfx->name, n);
    sfx->registration_sequence = s_registration_sequence;
    truenames[i] = _strdup(truename);

    return sfx;
}

/*
=====================
S_BeginRegistration

=====================
*/
void S_BeginRegistration(void)
{
    s_registration_sequence++;
    s_registering = true;
}

/*
==================
S_RegisterSound

==================
*/
sfx_t *S_RegisterSound(const char *name)
{
    sfx_t *sfx;

    if (!s_started)
        return NULL;

    sfx = FindName(name, true);
    sfx->registration_sequence = s_registration_sequence;

    if (!s_registering)
        S_LoadSound(sfx);

    return sfx;
}

static void stop_users(const sfx_t *s)
{
    int i;
    playsound_t *ps, *next;
    for (i = 0; i < MAX_CHANNELS; i++)
        if (channels[i].sfx == s) {
            log_cut(&channels[i], "freed");
            Vo_Stop(&channels[i]);
            memset(&channels[i], 0, sizeof(channel_t));
        }
    for (ps = s_pendingplays.next; ps != &s_pendingplays; ps = next) {
        next = ps->next;
        if (ps->sfx == s) {
            ps->prev->next = ps->next;
            ps->next->prev = ps->prev;
            ps->next = s_freeplays.next;
            s_freeplays.next->prev = ps;
            ps->prev = &s_freeplays;
            s_freeplays.next = ps;
        }
    }
}

/*
=====================
S_EndRegistration

=====================
*/
void S_EndRegistration(void)
{
    int i;
    sfx_t *sfx;

    if (!s_started)
        return;

    // free any sounds not from this registration sequence
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            continue;
        if (sfx->registration_sequence != s_registration_sequence) {  // don't need this sound
            stop_users(sfx);
            Amb_ForgetSfx(sfx);
            free_sfx_record(i);
        }
    }

    // load everything in
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            continue;
        S_LoadSound(sfx);
    }

    s_registering = false;

    // SoF: ambient waves are bound by name lookup only (SPEC-040 1.5)
    Amb_EndRegistration();
    if (Eng_HaveStruct())
        EFX_SetMapEnvironment(CL_ConfigString(CS_MAPMODEL));
    if ((int)om_debug->value & 1) {
        int loaded = 0, total = 0;
        for (i = 0; i < num_sfx; i++)
            if (known_sfx[i].name[0]) {
                total++;
                loaded += known_sfx[i].cache != NULL;
            }
        Snd_Printf("OMsnd: registration %d: %d sounds, %d loaded\n", s_registration_sequence, total, loaded);
    }
}

void S_FreeSound(sfx_t *s)
{
    int i;
    if (!s_started || !s || s < known_sfx || s >= known_sfx + MAX_SFX)
        return;
    i = (int)(s - known_sfx);
    if (!s->name[0])
        return;
    stop_users(s);
    Amb_ForgetSfx(s);
    free_sfx_record(i);
}

void S_Touch(void)
{
}

//=============================================================================

/*
=================
S_PickChannel
=================
*/
channel_t *S_PickChannel(int entnum, int entchannel, qboolean local)
{
    int ch_idx;
    int first_to_die;
    float score, best;
    channel_t *ch;

    if (entchannel < 0)
        eng.Com_Error(ERR_DROP, "S_PickChannel: entchannel<0");

    // Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    best = 1e30f;
    for (ch_idx = 0; ch_idx < MAX_CHANNELS; ch_idx++) {
        if (entchannel != 0  // channel 0 never overrides
            && channels[ch_idx].entnum == entnum && channels[ch_idx].entchannel == entchannel &&
            channels[ch_idx].sfx) {
            // always override sound from same entity
            first_to_die = ch_idx;
            break;
        }

        // don't let world sounds override the listener's own sounds (Quake II: player sounds; OMsnd 1.0: every
        // local sound, see S_IsLocalSound, so a weapon sound cannot lose its voice to an ambient loop)
        if (channels[ch_idx].sfx && channels[ch_idx].local && !local)
            continue;

        // OMsnd 1.0: a free voice first, else the quietest (Quake II: the one that ends first; with
        // 64 voices that point is reached only in heavy fights, where the quietest is the least missed)
        score = channels[ch_idx].sfx ? channels[ch_idx].gain : -1.0f;
        if (score < best) {
            best = score;
            first_to_die = ch_idx;
        }
    }

    if (first_to_die == -1)
        return NULL;

    ch = &channels[first_to_die];
    if (ch->sfx) {
        if ((int)om_debug->value & 4)
            Snd_Printf("OMsnd: [p=%d] %s %s (ent %d ch %d) for ent %d ch %d\n", paintedtime,
                       ch->entnum == entnum && ch->entchannel == entchannel ? "override" : "steal", ch->sfx->name,
                       ch->entnum, ch->entchannel, entnum, entchannel);
        log_cut(ch, ch->entnum == entnum && ch->entchannel == entchannel ? "override" : "stolen");
        Vo_Stop(ch);
    }
    memset(ch, 0, sizeof(*ch));

    return ch;
}

/*
=================
S_SpatializeOrigin

Used for spatializing channels and autosounds. SoF: near radius N for ambient point sets (SPEC-040 1.1).
OMsnd 1.0: the volume is SoF's distance term; the stereo split becomes a direction in listener space for
OpenAL, and the near-radius blend b becomes the spread of the voice.
=================
*/
void S_SpatializeOrigin(const vec3_t origin, float master_vol, float dist_mult, float near_radius, spat_t *out)
{
    vec_t dist, a, b, vol;
    vec3_t source_vec;

    memset(out, 0, sizeof(*out));
    if (CL_State() != CA_ACTIVE) {
        out->vol = 255;
        return;
    }

    // calculate stereo seperation and distance attenuation
    source_vec[0] = origin[0] - listener_origin[0];
    source_vec[1] = origin[1] - listener_origin[1];
    source_vec[2] = origin[2] - listener_origin[2];

    dist = VectorNormalizeLength(source_vec);
    dist -= SOUND_FULLVOLUME;
    if (near_radius <= 0) {
        a = (dist < 0 ? 0 : dist) * dist_mult;  // different attenuation levels
        b = 0;
    } else {
        a = (dist - near_radius < 0 ? 0 : dist - near_radius) * dist_mult;
        if (dist < near_radius / 2)
            b = 1;
        else if (dist < near_radius)
            b = (near_radius - dist) / (near_radius / 2);
        else
            b = 0;
    }

    // add in distance effect
    vol = (float)(int)(master_vol * (1.0f - a));
    out->vol = vol < 0 ? 0 : vol;

    if (!dist_mult || b == 1)  // no attenuation = no spatialization
        return;
    out->positional = true;
    out->spread = b;
    // listener space, OpenAL axes: x right, y up, z towards the back
    out->dir[0] = DotProduct(source_vec, listener_right);
    out->dir[1] = DotProduct(source_vec, listener_up);
    out->dir[2] = -DotProduct(source_vec, listener_forward);
    if (!out->dir[0] && !out->dir[1] && !out->dir[2])
        out->dir[2] = -1;  // at the listener: straight ahead
}

static const char *va_vec(const vec3_t v)
{
    static char buf[64];
    snprintf(buf, sizeof(buf), "%.0f %.0f %.0f", v[0], v[1], v[2]);
    return buf;
}

/*
=================
S_IsLocalSound

OMsnd 1.0: the listener's own sounds are decided once, when the sound starts, and stay head-relative at their
full volume for their whole length:
  - the view entity (player number + 1; Quake II/SoF: "anything coming from the view entity will always be
    full volume");
  - attenuation 0 (ATTN_NONE): no distance term and no direction (SPEC-040 1.1);
  - the world entity (0) without an origin: it has no position to take;
  - an explicit origin within LOCAL_RADIUS of the listener: the player's own bounding box (a weapon sound
    started at the player's origin or muzzle); the original pans such a sound by the few units between the
    origin and the eye, which only makes it quieter (0.5 per side).
Such a sound is never stopped by distance and only another local sound can take its voice.
=================
*/
#define LOCAL_RADIUS 48.0f

qboolean S_IsLocalSound(int entnum, qboolean fixed_origin, const vec3_t origin, float attenuation)
{
    vec3_t d;
    if (entnum == CL_PlayerNum() + 1)
        return true;
    if (attenuation == 0) // ATTN_NONE
        return true;
    if (!fixed_origin)
        return entnum <= 0;
    d[0] = origin[0] - listener_origin[0];
    d[1] = origin[1] - listener_origin[1];
    d[2] = origin[2] - listener_origin[2];
    return DotProduct(d, d) < LOCAL_RADIUS * LOCAL_RADIUS;
}

/*
=================
S_Spatialize
=================
*/
void S_Spatialize(channel_t *ch)
{
    vec3_t origin;

    // the listener's own sounds: full volume, no direction. Checked before the "not in game" rule of
    // S_SpatializeOrigin (Quake II order): measured on Defsnd, a local sound in the menus plays at 153
    // (gain 0.4177 = 153 * 179 / 65536), not 255 (docs/impl/I5s-2.md; SPEC-AMEND-006 I5s-2.3)
    if (ch->local) {
        memset(&ch->sp, 0, sizeof(ch->sp));
        ch->sp.vol = (float)ch->master_vol;
        return;
    }

    if (ch->fixed_origin) {
        origin[0] = ch->origin[0];
        origin[1] = ch->origin[1];
        origin[2] = ch->origin[2];
    } else if (eng.CL_GetEntitySoundOrigin && ch->entnum >= 0 && ch->entnum < MAX_EDICTS)
        eng.CL_GetEntitySoundOrigin(ch->entnum, origin);
    else {
        origin[0] = listener_origin[0];
        origin[1] = listener_origin[1];
        origin[2] = listener_origin[2];
    }

    S_SpatializeOrigin(origin, (float)ch->master_vol, ch->dist_mult, ch->near_radius, &ch->sp);
}

/*
=================
S_AllocPlaysound
=================
*/
static playsound_t *S_AllocPlaysound(void)
{
    playsound_t *ps;

    ps = s_freeplays.next;
    if (ps == &s_freeplays)
        return NULL;  // no free playsounds

    // unlink from freelist
    ps->prev->next = ps->next;
    ps->next->prev = ps->prev;

    return ps;
}

/*
=================
S_FreePlaysound
=================
*/
static void S_FreePlaysound(playsound_t *ps)
{
    // unlink from channel
    ps->prev->next = ps->next;
    ps->next->prev = ps->prev;

    // add to free list
    ps->next = s_freeplays.next;
    s_freeplays.next->prev = ps;
    ps->prev = &s_freeplays;
    s_freeplays.next = ps;
}

/*
===============
S_IssuePlaysound

Take the next playsound and begin it on the channel
This is never called directly by S_Play*, but only
by the update loop.
===============
*/
void S_IssuePlaysound(playsound_t *ps)
{
    channel_t *ch;
    sfxcache_t *sc;
    qboolean local;

    if (s_show->value)
        Snd_Printf("Issue %i\n", ps->begin);
    // pick a channel to play on
    local = S_IsLocalSound(ps->entnum, ps->fixed_origin, ps->origin, ps->attenuation);
    ch = S_PickChannel(ps->entnum, ps->entchannel, local);
    if (!ch) {
        if ((int)om_debug->value & 4)
            Snd_Printf("OMsnd: [p=%d] no voice for %s (ent %d ch %d)\n", paintedtime, ps->sfx->name, ps->entnum,
                       ps->entchannel);
        S_FreePlaysound(ps);
        return;
    }
    ch->local = local;

    // spatialize
    if (ps->attenuation == ATTN_STATIC)
        ch->dist_mult = ps->attenuation * 0.001f;
    else
        ch->dist_mult = ps->attenuation * 0.0005f;
    ch->master_vol = (int)ps->volume;
    ch->entnum = ps->entnum;
    ch->entchannel = ps->entchannel;
    ch->sfx = ps->sfx;
    ch->origin[0] = ps->origin[0];
    ch->origin[1] = ps->origin[1];
    ch->origin[2] = ps->origin[2];
    ch->fixed_origin = ps->fixed_origin;

    sc = S_LoadSound(ch->sfx);
    if (!sc) {
        memset(ch, 0, sizeof(*ch));
        S_FreePlaysound(ps);
        return;
    }

    // SoF `.adp` (SPEC-040 1.3 as corrected by SPEC-AMEND-006 I5s-2.1): the mixer re-spatializes the channel
    // without its distance before its first painted samples, so it plays non-positionally at its full volume
    if (sc->adpcm)
        ch->dist_mult = 0;
    S_Spatialize(ch);

    Vo_Start(ch, sc, 0);
    if (((int)om_debug->value & 4) && ch->sfx)
        Snd_Printf("OMsnd: [p=%d] voice %d: %s vol %.0f%s gain %.3f\n", paintedtime, (int)(ch - channels),
                   ch->sfx->name, ch->sp.vol, ch->local ? " local" : ch->sp.positional ? " 3d" : "", ch->gain);

    // free the playsound
    S_FreePlaysound(ps);
}

/* an autosound loop for this frame: the voice that plays this sound for this key keeps playing, else a new
 * one starts at the loop's phase (Quake II: pos mod length of a voice allocated every frame) */
void S_AddLoop(sfx_t *sfx, sfxcache_t *sc, const spat_t *sp, int pos, int key)
{
    int i;
    channel_t *ch;
    if (sp->vol <= 0 || sc->length <= 0)
        return;  // not audible; an untouched voice stops after Amb_AddSounds
    for (i = 0, ch = channels; i < MAX_CHANNELS; i++, ch++)
        if (ch->autosound && ch->sfx == sfx && ch->loopkey == key && ch->touched != s_framecount)
            break;
    if (i == MAX_CHANNELS) {
        ch = S_PickChannel(0, 0, false);
        if (!ch)
            return;
        ch->autosound = true;
        ch->sfx = sfx;
        ch->loopkey = key;
        ch->sp = *sp;
        ch->touched = s_framecount;
        Vo_Start(ch, sc, pos < 0 ? 0 : pos % sc->length);
        return;
    }
    ch->sp = *sp;
    ch->touched = s_framecount;
    Vo_Apply(ch);
}
/* SPEC-040 1.4 */
static sfx_t *S_RegisterSexedSound(int entnum, const char *base)
{
    int n;
    const char *p;
    sfx_t *sfx;
    char model[MAX_QPATH];
    char sexedFilename[MAX_QPATH];
    char maleFilename[MAX_QPATH];
    char lookup[MAX_QPATH];

    // determine what model the client is using
    model[0] = 0;
    n = CS_PLAYERSKINS + CL_EntityCurrentNumber(entnum) - 1;
    if (n >= 0 && n < 3555) {
        const char *cs = CL_ConfigString(n);
        if (cs[0]) {
            p = strchr(cs, '\\');
            if (p) {
                char *slash;
                p += 1;
                _snprintf_s(model, sizeof(model), _TRUNCATE, "%s", p);
                slash = strchr(model, '/');
                if (slash)
                    *slash = 0;
            }
        }
    }
    // if we can't figure it out, they're male
    if (!model[0])
        strcpy(model, "male");

    // see if we already know of the model specific sound
    _snprintf_s(sexedFilename, sizeof(sexedFilename), _TRUNCATE, "#players/%s/%s", model, base + 1);
    sfx = FindName(sexedFilename, false);

    if (!sfx) {
        // no, so see if it exists
        normalize_name(sexedFilename, lookup);
        if (Snd_FileExists(&sexedFilename[1])) {
            // yes, register it
            sfx = S_RegisterSound(sexedFilename);
        } else {
            // no, revert to the male sound in the pak0.pak
            _snprintf_s(maleFilename, sizeof(maleFilename), _TRUNCATE, "player/%s/%s", "male", base + 1);
            sfx = S_AliasName(sexedFilename, maleFilename);
        }
        if ((int)om_debug->value & 4)
            Snd_Printf("OMsnd: %s on entity %d -> %s (%s)\n", base, entnum, lookup, S_TrueName(sfx));
    }

    return sfx;
}

// =======================================================================
// Start a sound effect
// =======================================================================

static void start_sound(const float *origin, int entnum, int entchannel, sfx_t *sfx, int vol, float attenuation,
                        float timeofs)
{
    sfxcache_t *sc;
    playsound_t *ps, *sort;
    int start;
    int servertime = Eng_HaveStruct() ? CL_Time() : 0;

    if (!s_started)
        return;

    if (!sfx)
        return;

    if (sfx->name[0] == '*')
        sfx = S_RegisterSexedSound(entnum, sfx->name);

    // make sure the sound is loaded
    sc = S_LoadSound(sfx);
    if (!sc)
        return;  // couldn't load the sound's data

    if ((int)om_debug->value & 4)
        Snd_Printf("OMsnd: [p=%d cl=%d] start %s ent %d ch %d vol %d attn %.2f ofs %.3f at %s, listener %.0f %.0f %.0f "
                   "(player ent %d)\n", paintedtime, servertime, sfx->name, entnum, entchannel, vol, attenuation, timeofs,
                   origin ? va_vec(origin) : "(entity)", listener_origin[0], listener_origin[1], listener_origin[2],
                   CL_PlayerNum() + 1);

    // make the playsound_t
    ps = S_AllocPlaysound();
    if (!ps)
        return;

    if (origin) {
        ps->origin[0] = origin[0];
        ps->origin[1] = origin[1];
        ps->origin[2] = origin[2];
        ps->fixed_origin = true;
    } else
        ps->fixed_origin = false;

    ps->entnum = entnum;
    ps->entchannel = entchannel;
    ps->attenuation = attenuation;
    ps->volume = (float)vol;
    ps->sfx = sfx;

    // drift s_beginofs (Quake II uses the frame's server time; the sound struct gives the client time)
    start = (int)(servertime * 0.001 * SND_CLOCK_RATE + s_beginofs);
    if (start < paintedtime) {
        start = paintedtime;
        s_beginofs = (int)(start - (servertime * 0.001 * SND_CLOCK_RATE));
    } else if (start > paintedtime + 0.3 * SND_CLOCK_RATE) {
        start = (int)(paintedtime + 0.1 * SND_CLOCK_RATE);
        s_beginofs = (int)(start - (servertime * 0.001 * SND_CLOCK_RATE));
    } else {
        s_beginofs -= 10;
    }

    if (!timeofs)
        ps->begin = paintedtime;
    else
        ps->begin = (unsigned)(start + timeofs * SND_CLOCK_RATE);

    // sort into the pending sound list
    for (sort = s_pendingplays.next; sort != &s_pendingplays && sort->begin < ps->begin; sort = sort->next)
        ;

    ps->next = sort;
    ps->prev = sort->prev;

    ps->next->prev = ps;
    ps->prev->next = ps;

    // OMsnd 1.0: a sound due now starts now instead of at the next S_Update (one frame less latency)
    if ((int)ps->begin <= paintedtime && s_pendingplays.next == ps)
        S_IssuePlaysound(ps);
}

/*
====================
S_StartSound

Validates the parms and ques the sound up
if pos is NULL, the sound will be dynamically sourced from the entity
Entchannel 0 will never override a playing sound
====================
*/
void S_StartSound(const float *origin, int entnum, int entchannel, sfx_t *sfx, float fvol, float attenuation,
                  float timeofs)
{
    start_sound(origin, entnum, entchannel, sfx, (int)(fvol * 255), attenuation, timeofs);
}

void S_StartSoundVol(const float *origin, int entnum, int entchannel, sfx_t *sfx, int vol, float attenuation,
                     float timeofs)
{
    start_sound(origin, entnum, entchannel, sfx, vol, attenuation, timeofs);
}

/*
==================
S_StartLocalSound
==================
*/
void S_StartLocalSound(const char *sound)
{
    sfx_t *sfx;

    if (!s_started)
        return;

    sfx = S_RegisterSound(sound);
    if (!sfx) {
        Snd_Printf("S_StartLocalSound: can't cache %s\n", sound);
        return;
    }
    // SoF: volume 0.6 (SPEC-004, SPEC-040 4)
    S_StartSound(NULL, CL_PlayerNum() + 1, 0, sfx, 0.6f, 1, 0);
}

/*
==================
S_StopAllSounds
==================
*/
void S_StopAllSounds(void)
{
    int i;

    if (!s_started)
        return;

    // clear all the playsounds
    memset(s_playsounds, 0, sizeof(s_playsounds));
    s_freeplays.next = s_freeplays.prev = &s_freeplays;
    s_pendingplays.next = s_pendingplays.prev = &s_pendingplays;

    for (i = 0; i < MAX_PLAYSOUNDS; i++) {
        s_playsounds[i].prev = &s_freeplays;
        s_playsounds[i].next = s_freeplays.next;
        s_playsounds[i].prev->next = &s_playsounds[i];
        s_playsounds[i].next->prev = &s_playsounds[i];
    }

    // clear all the channels
    for (i = 0; i < MAX_CHANNELS; i++)
        if (channels[i].sfx) {
            log_cut(&channels[i], "stopall");
            Vo_Stop(&channels[i]);
        }
    memset(channels, 0, sizeof(channels));
}

//=============================================================================

/* the sound clock follows the wall clock (SND_CLOCK_RATE samples per second) */
static void advance_clock(void)
{
    unsigned ms = Snd_Milliseconds() - clock_base_ms;
    paintedtime = (int)((unsigned long long)ms * SND_CLOCK_RATE / 1000);
}

/*
============
S_Update

Called once each time through the main loop. SoF order (SPEC-040 1.5): silence check, re-spatialization,
music state, ambient sets and entity loop sounds; then (OMsnd 1.0) the voices are updated in one batch.
============
*/
void S_Update(const float *origin, const float *forward, const float *right, const float *up)
{
    int i;
    int total;
    channel_t *ch;
    playsound_t *ps;
    qboolean adp_hold, silent;

    if (!s_started)
        return;

    // test tool: s_om_stall N stalls N percent of the frames for s_om_stallmin..s_om_stallmax ms (a slow game
    // frame), with its own generator so that the game's random numbers do not change
    if (om_stall->value > 0) {
        static unsigned seed = 12345;
        int lo = (int)om_stallmin->value, hi = (int)om_stallmax->value;
        if (lo < 1)
            lo = 1;
        if (hi < lo)
            hi = lo;
        seed = seed * 1103515245u + 12345u;
        if ((int)((seed >> 16) % 100) < (int)om_stall->value) {
            seed = seed * 1103515245u + 12345u;
            Snd_Sleep((unsigned)(lo + (int)((seed >> 16) % (unsigned)(hi - lo + 1))));
        }
    }

    advance_clock();
    s_framecount++;
    Vo_CheckDevice();
    Vo_ApplySettings();

    // Defsnd stops mixing here (and releases its DirectSound buffers while the window is inactive); OMsnd
    // mutes the listener, and for the focus only as S_FocusMuted says
    silent = s_nosound->value || s_disable->value || (Eng_HaveStruct() && CL_ShuttingDown()) || S_FocusMuted();
    Vo_SetMasterMute(silent);
    if ((int)om_debug->value & 8) {
        // the states that silence the whole mix or hold the music and .adp voices, when they change
        static int last_silent = -1, last_active = -1;
        int active = Snd_ClientActive();
        if (silent != last_silent || active != last_active)
            Snd_Printf("OMsnd: [p=%d] mix %s, client %s (state %d)\n", paintedtime, silent ? "muted" : "on",
                       active ? "active" : "not active", Eng_HaveStruct() ? CL_State() : -1);
        last_silent = silent;
        last_active = active;
    }

    Vo_BeginFrame();

    for (i = 0; i < 3; i++) {
        listener_origin[i] = origin ? origin[i] : 0;
        listener_forward[i] = forward ? forward[i] : 0;
        listener_right[i] = right ? right[i] : 0;
        listener_up[i] = up ? up[i] : 0;
    }

    // SoF: `.adp` sounds advance only while not paused and in game with the level prepped (SPEC-040 1.3)
    adp_hold = paused->value != 0 || !Snd_ClientActive();

    // update spatialization for dynamic sounds; free the voices that ended
    ch = channels;
    for (i = 0; i < MAX_CHANNELS; i++, ch++) {
        if (!ch->sfx || ch->autosound)
            continue;
        if (!ch->adp_held && Vo_Finished(ch)) {
            memset(ch, 0, sizeof(*ch));
            continue;
        }
        // respatialize; OMsnd 1.0: a voice out of range keeps playing at gain 0 (Quake II frees it) so that a
        // sound whose entity comes back into range, or whose entity origin was not known yet when it started,
        // is heard for the rest of its length; it is the first one stolen when the voices run out
        {
            float g0 = ch->gain;
            qboolean p0 = ch->sp.positional;
            S_Spatialize(ch);
            Vo_Apply(ch);
            if (p0 != ch->sp.positional)
                dbg_spat_flips++;
            if ((g0 > 0.01f || ch->gain > 0.01f) && (ch->gain > g0 * 1.41f || ch->gain < g0 * 0.71f))
                dbg_gain_steps++;
        }
    }

    // start the sounds that are due
    while ((ps = s_pendingplays.next) != &s_pendingplays && (int)ps->begin <= paintedtime)
        S_IssuePlaysound(ps);

    // SoF: music state, then ambient sets and entity loop sounds
    Mus_Update();
    Amb_AddSounds();

    // loops not re-added this frame stop; `.adp` voices hold while paused
    ch = channels;
    for (i = 0; i < MAX_CHANNELS; i++, ch++) {
        if (!ch->sfx)
            continue;
        if (ch->autosound && ch->touched != s_framecount) {
            dbg_loop_restarts++;
            Vo_Stop(ch);
            memset(ch, 0, sizeof(*ch));
            continue;
        }
        if (ch->sfx->cache && ch->sfx->cache->adpcm)
            Vo_Pause(ch, adp_hold);
    }

    if (((int)om_debug->value & 16) && Snd_Milliseconds() - (unsigned)dbg_last_ms >= 1000) {
        int used = 0;
        for (i = 0; i < MAX_CHANNELS; i++)
            used += channels[i].sfx != NULL;
        Snd_Printf("OMsnd: [p=%d] 1s: voices %d, loop stops %d, gain steps %d, 3d flips %d, cuts %d, music dry %d\n",
                   paintedtime, used, dbg_loop_restarts, dbg_gain_steps, dbg_spat_flips, dbg_cuts, Mus_TakeDryCount());
        dbg_loop_restarts = dbg_gain_steps = dbg_spat_flips = dbg_cuts = 0;
        dbg_last_ms = (int)Snd_Milliseconds();
    }

    Vo_SetUnderwater(s_proctype == 1);
    Vo_TestTone(s_testsound->value != 0);

    //
    // debugging output
    //
    if (s_show->value) {
        total = 0;
        ch = channels;
        for (i = 0; i < MAX_CHANNELS; i++, ch++)
            if (ch->sfx && ch->sp.vol > 0) {
                Snd_Printf("%3i %5.3f %s%s %s\n", (int)ch->sp.vol, ch->gain, ch->sp.positional ? "3d" : "--",
                           ch->autosound ? "L" : " ", ch->sfx->name);
                total++;
            }

        Snd_Printf("----(%i)---- painted: %i\n", total, paintedtime);
    }

    Vo_EndFrame();
}
// =======================================================================
// remaining exports (SPEC-004 "Exports")
// =======================================================================

void S_SetSoundStruct(void *const *ptrs)
{
    Eng_SetSoundStruct(ptrs);
}

void S_Activate(int active)
{
    app_active = active != 0;
    if (!s_started)
        return;
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] S_Activate(%d)%s\n", paintedtime, active,
                   S_FocusMuted() ? " (muted: s_om_focusmute, window minimised)" : "");
    Vo_SetMasterMute(S_FocusMuted());
}

void S_RegisterAmbientSet(const char *path)
{
    if (!s_started)
        return;
    Amb_Register(path);
}

void S_SetGeneralAmbientSet(unsigned char index)
{
    if (!s_started)
        return;
    Amb_SetGeneral(index);
}

void S_RegisterMusicSet(const char *name)
{
    // SPEC-040 1.6: the original crashes here after s_initsound 0; OMsnd does nothing
    if (!s_started)
        return;
    Mus_Register(name);
}

void S_SetMusicIntensity(unsigned char value)
{
    if (!s_started)
        return;
    Mus_SetIntensity(value);
}

void S_SetMusicDesignerInfo(unsigned char value)
{
    if (!s_started)
        return;
    Mus_SetDesignerInfo(value);
}

void S_SetSoundProcType(unsigned char type)
{
    if (!s_started)
        return;
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] S_SetSoundProcType(%d)\n", paintedtime, type);
    if (type > 2) {
        eng.Com_Error(ERR_DROP, "Invalid sound procedure value...");
        return;
    }
    if (type == 0) {
        s_procgain = 1.0f;
        s_proctype = 0;
    } else if (type == 1) {
        s_procgain = 0.3f;
        s_proctype = 1;
    }
    // 2: keep the previous gain
    if (type < 2)
        EFX_SetProcType(type);
}

/* EAXSnd's optional exports (SPEC-004); SoF.exe 1.07f resolves but never calls them */
void SNDEAX_SetEnvironment(int environment)
{
    if (!s_started)
        return;
    EFX_SetEnvironment(environment);
}

void SNDEAX_SetEnvironmentLevel(void)
{
}

/*
===============================================================================

console functions

===============================================================================
*/

static void S_Play(void)
{
    int i;
    char name[256];

    i = 1;
    while (i < eng.Cmd_Argc()) {
        const char *arg = eng.Cmd_Argv(i);
        if (!strrchr(arg, '.'))
            _snprintf_s(name, sizeof(name), _TRUNCATE, "%s.wav", arg);
        else
            _snprintf_s(name, sizeof(name), _TRUNCATE, "%s", arg);
        // SPEC-004: each argument is played as a local sound
        S_StartLocalSound(name);
        i++;
    }
}

static void S_SoundList(void)
{
    int i;
    sfx_t *sfx;
    sfxcache_t *sc;
    int size, total;

    total = 0;
    for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++) {
        if (!sfx->registration_sequence)
            continue;
        sc = sfx->cache;
        if (sc) {
            size = sc->bytes;
            total += size;
            if (sc->loopstart >= 0)
                Snd_Printf("L");
            else
                Snd_Printf(" ");
            Snd_Printf("(%2db %5d Hz) %6i : %s\n", sc->width * 8, sc->speed, size, sfx->name);
        } else {
            if (sfx->name[0] == '*')
                Snd_Printf("  placeholder : %s\n", sfx->name);
            else
                Snd_Printf("  not loaded  : %s\n", sfx->name);
        }
    }
    Snd_Printf("Total resident: %i\n", total);
}

/* SPEC-040 5.3 `forcesong`: s_musicenabled's value (only) becomes 1, the set is freed and the named one loaded */
static void S_ForceSong_f(void)
{
    s_musicenabled->value = 1.0f;
    if (eng.Cmd_Argc() < 2)
        Mus_Force(NULL, false);
    else
        Mus_Force(eng.Cmd_Argv(1), true);
}

static void S_EaxEnvironment_f(void)
{
    if (eng.Cmd_Argc() < 2) {
        EFX_Info();
        Snd_Printf("usage: snd_eaxenvironment <0..%d> (OMsnd: needs s_om_reverb 1)\n", EAX_ENVIRONMENT_COUNT - 1);
        return;
    }
    EFX_SetEnvironment(atoi(eng.Cmd_Argv(1)));
}

static void S_EaxParams_f(void)
{
    EFX_Info();
}
