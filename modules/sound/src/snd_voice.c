/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module: the output (OMsnd 1.0, docs/impl/SND2.md).
 *
 * Every channel of snd_dma.c owns one OpenAL Soft source ("voice"). OpenAL does the mixing at the device
 * rate (s_om_rate, 48000 Hz by default) in floating point, with its band-limited resampler, and the output
 * limiter keeps the sum from clipping (s_om_limiter). OpenAL's own distance attenuation is off
 * (AL_NONE): the gain of a voice is SoF's (SPEC-040 1.1/1.2: master volume x distance term x s_volume x
 * processing gain), so loudness follows the spec; OpenAL only places the voice in the direction snd_dma.c
 * computed in listener space, which also makes HRTF possible (s_om_hrtf).
 *
 * Gain calibration (measured with the wave writer, docs/impl/SND2.md "Levels"): the Quake II mixer gives a
 * centred positional sound half its volume on each side and a non-positional one its full volume on both;
 * OpenAL Soft's stereo panner gives a centred mono source POS_CENTER per side and a non-spatialized one
 * NONPOS_LEVEL per side. The two scales below map them back onto SoF's levels.
 *
 * Implements: SPEC-040 1 (output), 1.1 (direction), 1.2 (gain), SPEC-004 (S_Init "sound sampling rate")
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* dladdr */
#endif
#include <dlfcn.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include "snd_local.h"

/* per-side output of a unit mono source, measured with the wave writer on OpenAL Soft 1.25.2 (stereo output,
 * default panning; modules/sound/test/sndhost.c + hostlevels.py, docs/impl/SND2.md "Levels"): shotgun Fire.wav
 * (peak 30935) at volume 255 gave 10928 per side straight ahead with the old 0.7071 scale and 15454 per side
 * non-spatialized with no scale, i.e. 0.5015 for both. Hard right gives 0.84 on the right side and ~0.05 on the left
 * (Quake II: 1.0 / 0.0); OpenAL keeps its constant-power panner there. */
#define POS_CENTER 0.5015f   /* spatialized, straight ahead */
#define NONPOS_LEVEL 0.5015f /* AL_SOURCE_SPATIALIZE_SOFT off, at the listener */

static ALCdevice *dev;
static ALCcontext *ctx;
static ALuint voices[MAX_CHANNELS];
static ALuint voice_slot[MAX_CHANNELS];   /* aux send currently set */
static ALuint voice_filter[MAX_CHANNELS]; /* direct filter currently set */
static ALuint tone_src, tone_buf;
static char describe[512];
static char resampler_name[64] = "default";
static ALint resampler = -1;
static int dev_rate, dev_hrtf, dev_limiter;
static qboolean have_spatialize, have_loop_points, have_radius, have_defer;
static LPALDEFERUPDATESSOFT p_defer;
static LPALPROCESSUPDATESSOFT p_process;
static LPALCREOPENDEVICESOFT p_reopen;
static ALCint attrs[32];
static unsigned last_check;
static int reopens;
static qboolean muted;
static qboolean underwater;
static qboolean capturing; /* s_om_wavefile: the wave writer is the only device */

/* NULL for the system default output (s_om_device "", "default", or a wave capture) */
static const char *wanted_device(void)
{
    const char *s = om_device ? om_device->string : NULL;
    if (capturing || !s || !s[0] || !_stricmp(s, "default"))
        return NULL;
    return s;
}

static void make_describe(void);

static void setup_wave_capture(const char *file, int freq)
{
    char path[MAX_PATH], ini[MAX_PATH + 16];
    FILE *f;
    size_t i;

    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s", file);
    for (i = 0; path[i]; i++)
        if (path[i] == '\\')
            path[i] = '/';
    _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%s.alsoft.ini", path);
    f = fopen(ini, "w");
    if (!f) {
        Snd_Printf("OMsnd: cannot write %s, capture disabled\n", ini);
        return;
    }
    fprintf(f, "[general]\ndrivers=wave\nfrequency=%d\nchannels=stereo\nsample-type=int16\ndither=false\n"
               "[wave]\nfile=%s\n", freq, path);
    fclose(f);
#ifdef _WIN32
    _putenv_s("ALSOFT_CONF", ini);
    _putenv_s("ALSOFT_DRIVERS", "wave");
    SetEnvironmentVariableA("ALSOFT_CONF", ini);
    SetEnvironmentVariableA("ALSOFT_DRIVERS", "wave");
#else
    setenv("ALSOFT_CONF", ini, 1);
    setenv("ALSOFT_DRIVERS", "wave", 1);
#endif
    Snd_Printf("OMsnd: writing the output to %s (%d Hz)\n", path, freq);
}

static void pick_resampler(void)
{
    LPALGETSTRINGISOFT getstringi;
    ALint n, i;
    if (!alIsExtensionPresent("AL_SOFT_source_resampler"))
        return;
    getstringi = (LPALGETSTRINGISOFT)alGetProcAddress("alGetStringiSOFT");
    n = alGetInteger(AL_NUM_RESAMPLERS_SOFT);
    for (i = 0; getstringi && i < n; i++) {
        const ALchar *name = getstringi(AL_RESAMPLER_NAME_SOFT, i);
        if (name && (!_stricmp(name, "BSinc24") || !_stricmp(name, "24-point Sinc")))
            resampler = i;
    }
    if (resampler >= 0 && getstringi)
        _snprintf_s(resampler_name, sizeof(resampler_name), _TRUNCATE, "%s",
                    getstringi(AL_RESAMPLER_NAME_SOFT, resampler));
}

static void setup_voice(ALuint s)
{
    alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcef(s, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcef(s, AL_REFERENCE_DISTANCE, 1.0f);
    alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(s, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    alSourcef(s, AL_GAIN, 0.0f);
    alSourcef(s, AL_MAX_GAIN, 4.0f);
    if (resampler >= 0)
        alSourcei(s, AL_SOURCE_RESAMPLER_SOFT, resampler);
}

static void build_attrs(void)
{
    int n = 0;
    attrs[n++] = ALC_FREQUENCY;
    attrs[n++] = om_rate->value >= 8000 && om_rate->value <= 192000 ? (int)om_rate->value : 48000;
    attrs[n++] = ALC_REFRESH;
    attrs[n++] = om_refresh->value >= 20 && om_refresh->value <= 1000 ? (int)om_refresh->value : 100;
    attrs[n++] = ALC_MONO_SOURCES;
    attrs[n++] = MAX_CHANNELS + 16;
    attrs[n++] = ALC_STEREO_SOURCES;
    attrs[n++] = 16;
    attrs[n++] = ALC_OUTPUT_LIMITER_SOFT;
    attrs[n++] = om_limiter->value ? ALC_TRUE : ALC_FALSE;
    if (dev && alcIsExtensionPresent(dev, "ALC_SOFT_HRTF")) {
        attrs[n++] = ALC_HRTF_SOFT;
        attrs[n++] = om_hrtf->value > 0 ? ALC_TRUE : om_hrtf->value < 0 ? ALC_DONT_CARE_SOFT : ALC_FALSE;
    }
    if (dev && alcIsExtensionPresent(dev, "ALC_EXT_EFX")) {
        attrs[n++] = ALC_MAX_AUXILIARY_SENDS;
        attrs[n++] = 1;
    }
    attrs[n] = 0;
}

static void read_device_state(void)
{
    ALCint v = 0;
    alcGetIntegerv(dev, ALC_FREQUENCY, 1, &v);
    dev_rate = v;
    v = 0;
    if (alcIsExtensionPresent(dev, "ALC_SOFT_HRTF"))
        alcGetIntegerv(dev, ALC_HRTF_SOFT, 1, &v);
    dev_hrtf = v;
    v = 0;
    alcGetIntegerv(dev, ALC_OUTPUT_LIMITER_SOFT, 1, &v);
    dev_limiter = v;
}

qboolean Vo_Init(const char *wavefile)
{
    char modpath[MAX_PATH] = "?";
#ifdef _WIN32
    HMODULE al;
#endif
    int i;

    capturing = wavefile && wavefile[0];
    if (capturing)
        setup_wave_capture(wavefile, om_rate->value >= 8000 ? (int)om_rate->value : 48000);
    dev = wanted_device() ? alcOpenDevice(wanted_device()) : NULL;
    if (!dev && wanted_device())
        Snd_Printf("OMsnd: cannot open \"%s\", using the default output\n", wanted_device());
    if (!dev)
        dev = alcOpenDevice(NULL);
    if (!dev) {
        Snd_Printf("OMsnd: alcOpenDevice failed\n");
        return false;
    }
    build_attrs();
    ctx = alcCreateContext(dev, attrs);
    if (!ctx || !alcMakeContextCurrent(ctx)) {
        Snd_Printf("OMsnd: cannot create an OpenAL context\n");
        if (ctx)
            alcDestroyContext(ctx);
        alcCloseDevice(dev);
        dev = NULL;
        ctx = NULL;
        return false;
    }
    alGetError();
    alDistanceModel(AL_NONE);
    alDopplerFactor(0.0f);
    alListenerf(AL_GAIN, om_gain->value >= 0 ? om_gain->value : 1.0f);
    muted = false;
    have_spatialize = alIsExtensionPresent("AL_SOFT_source_spatialize");
    have_loop_points = alIsExtensionPresent("AL_SOFT_loop_points");
    have_radius = alIsExtensionPresent("AL_EXT_SOURCE_RADIUS");
    p_defer = (LPALDEFERUPDATESSOFT)alGetProcAddress("alDeferUpdatesSOFT");
    p_process = (LPALPROCESSUPDATESSOFT)alGetProcAddress("alProcessUpdatesSOFT");
    have_defer = alIsExtensionPresent("AL_SOFT_deferred_updates") && p_defer && p_process;
    p_reopen = alcIsExtensionPresent(dev, "ALC_SOFT_reopen_device")
                   ? (LPALCREOPENDEVICESOFT)alcGetProcAddress(dev, "alcReopenDeviceSOFT")
                   : NULL;
    pick_resampler();

    alGenSources(MAX_CHANNELS, voices);
    if (alGetError() != AL_NO_ERROR) {
        Snd_Printf("OMsnd: cannot create %d voices\n", MAX_CHANNELS);
        memset(voices, 0, sizeof(voices));
        Vo_Shutdown();
        return false;
    }
    for (i = 0; i < MAX_CHANNELS; i++) {
        setup_voice(voices[i]);
        voice_slot[i] = 0;
        voice_filter[i] = 0;
    }
    alGenSources(1, &tone_src);
    setup_voice(tone_src);
    if (have_spatialize)
        alSourcei(tone_src, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
    alGetError();
    read_device_state();
    last_check = Snd_Milliseconds();
    reopens = 0;

#ifdef _WIN32
    al = GetModuleHandleA("OpenAL32.dll");
    if (al)
        GetModuleFileNameA(al, modpath, sizeof(modpath));
#else
    {
        Dl_info info;
        if (dladdr((void *)alcOpenDevice, &info) && info.dli_fname)
            _snprintf_s(modpath, sizeof(modpath), _TRUNCATE, "%s", info.dli_fname);
    }
#endif
    make_describe();
    Snd_Printf("OMsnd %s: %s, %s\n", OMSND_VERSION, describe, modpath);
    /* the settings Vo_Init just used are not changes */
    om_device->modified = om_hrtf->modified = om_limiter->modified = om_rate->modified = om_refresh->modified = false;
    return true;
}

static void make_describe(void)
{
    const ALCchar *devname = alcGetString(dev, ALC_ALL_DEVICES_SPECIFIER);
    if (!devname)
        devname = alcGetString(dev, ALC_DEVICE_SPECIFIER);
    _snprintf_s(describe, sizeof(describe), _TRUNCATE,
                "OpenAL %s (%s), device \"%s\", %d Hz, %d voices, resampler %s, HRTF %s, limiter %s",
                alGetString(AL_VERSION), alGetString(AL_RENDERER), devname ? devname : "?", dev_rate, MAX_CHANNELS,
                resampler_name, dev_hrtf ? "on" : "off", dev_limiter ? "on" : "off");
}

/* OpenMerc MENU2: the audio page changes the output device, HRTF, the limiter and the rate without a
 * snd_restart. A new device goes through alcReopenDeviceSOFT (the voices, buffers and effects stay); the
 * context attributes alone through alcResetDeviceSOFT. */
void Vo_ApplySettings(void)
{
    qboolean newdev, newattrs;
    LPALCRESETDEVICESOFT p_reset;
    ALCboolean ok = ALC_FALSE;

    if (!dev)
        return;
    newdev = om_device->modified;
    newattrs = om_hrtf->modified || om_limiter->modified || om_rate->modified || om_refresh->modified;
    om_device->modified = om_hrtf->modified = om_limiter->modified = om_rate->modified = om_refresh->modified = false;
    if (!newdev && !newattrs)
        return;
    if (newdev && capturing) {
        Snd_Printf("OMsnd: s_om_device is ignored while s_om_wavefile captures the output\n");
        newdev = false;
        if (!newattrs)
            return;
    }
    build_attrs();
    if (newdev) {
        if (!p_reopen) {
            Snd_Printf("OMsnd: this OpenAL cannot switch devices live, use snd_restart\n");
            return;
        }
        ok = p_reopen(dev, wanted_device(), attrs);
        if (!ok && wanted_device()) {
            Snd_Printf("OMsnd: cannot open \"%s\", using the default output\n", wanted_device());
            ok = p_reopen(dev, NULL, attrs);
        }
    } else {
        p_reset = alcIsExtensionPresent(dev, "ALC_SOFT_HRTF")
                      ? (LPALCRESETDEVICESOFT)alcGetProcAddress(dev, "alcResetDeviceSOFT")
                      : NULL;
        if (p_reset)
            ok = p_reset(dev, attrs);
        else if (p_reopen)
            ok = p_reopen(dev, wanted_device(), attrs);
    }
    if (!ok) {
        Snd_Printf("OMsnd: the new output settings did not apply, use snd_restart\n");
        return;
    }
    read_device_state();
    make_describe();
    Snd_Printf("OMsnd: output now %s\n", describe);
}

void Vo_Shutdown(void)
{
    if (ctx) {
        int i;
        for (i = 0; i < MAX_CHANNELS; i++)
            if (voices[i]) {
                alSourceStop(voices[i]);
                alSourcei(voices[i], AL_BUFFER, 0);
            }
        alDeleteSources(MAX_CHANNELS, voices);
        memset(voices, 0, sizeof(voices));
        if (tone_src) {
            alSourceStop(tone_src);
            alSourcei(tone_src, AL_BUFFER, 0);
            alDeleteSources(1, &tone_src);
        }
        if (tone_buf)
            alDeleteBuffers(1, &tone_buf);
        tone_src = tone_buf = 0;
        alGetError();
        EFX_Shutdown(); /* its slot and filter can go once no source uses them */
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
    }
    if (dev)
        alcCloseDevice(dev);
    ctx = NULL;
    dev = NULL;
}

const char *Vo_Describe(void)
{
    static char buf[600];
    int i, playing = 0;
    for (i = 0; i < MAX_CHANNELS; i++) {
        ALint st = AL_STOPPED;
        if (voices[i])
            alGetSourcei(voices[i], AL_SOURCE_STATE, &st);
        playing += st == AL_PLAYING;
    }
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%s; %d voices playing, %d device reopens", describe, playing, reopens);
    return buf;
}

void *Vo_Device(void)
{
    return dev;
}

void Vo_BeginFrame(void)
{
    if (have_defer)
        p_defer();
}

void Vo_EndFrame(void)
{
    if (have_defer)
        p_process();
    alGetError();
}

/* A device that went away (USB headset unplugged, driver reset, exclusive-mode grab) leaves OpenAL Soft's
 * device "disconnected": reopen it on the default output once a second; the voices keep their state. */
qboolean Vo_CheckDevice(void)
{
    ALCint connected = 1;
    unsigned now = Snd_Milliseconds();
    if (!dev)
        return false;
    if (now - last_check < 1000)
        return true;
    last_check = now;
    alcGetIntegerv(dev, ALC_CONNECTED, 1, &connected);
    if (connected)
        return true;
    if (p_reopen) {
        build_attrs();
        if (p_reopen(dev, NULL, attrs)) {
            reopens++;
            read_device_state();
            Snd_Printf("OMsnd: sound device lost, reopened on the default output (%d Hz)\n", dev_rate);
            return true;
        }
    }
    return false;
}

unsigned Vo_MakeBuffer(const void *data, int frames, int width, int rate, int loopstart)
{
    ALuint b = 0;
    if (frames <= 0 || rate <= 0)
        return 0;
    alGetError();
    alGenBuffers(1, &b);
    if (alGetError() != AL_NO_ERROR)
        return 0;
    alBufferData(b, width == 1 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16, data, frames * width, rate);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &b);
        return 0;
    }
    if (loopstart > 0 && loopstart < frames && have_loop_points) {
        ALint lp[2];
        lp[0] = loopstart;
        lp[1] = frames;
        alBufferiv(b, AL_LOOP_POINTS_SOFT, lp);
        alGetError();
    }
    return b;
}

void Vo_DeleteBuffer(unsigned buf)
{
    ALuint b = buf;
    if (b && ctx) {
        alDeleteBuffers(1, &b);
        alGetError();
    }
}

static ALuint voice_of(const channel_t *ch)
{
    int i = (int)(ch - channels);
    return i >= 0 && i < MAX_CHANNELS ? voices[i] : 0;
}

void Vo_Start(channel_t *ch, sfxcache_t *sc, int clockpos)
{
    ALuint s = voice_of(ch);
    ALuint buf = sc->buffer;
    qboolean loop = false;
    if (!s)
        return;
    alSourceStop(s);
    alSourcei(s, AL_BUFFER, 0);
    if (ch->autosound) {
        /* autosound loops always go back to the start (Quake II), not to the cue point */
        loop = true;
        if (sc->loopstart > 0) {
            if (!sc->buffer_whole && sc->data)
                sc->buffer_whole = Vo_MakeBuffer(sc->data, sc->frames, sc->width, sc->speed, -1);
            if (sc->buffer_whole)
                buf = sc->buffer_whole;
        }
    } else if (sc->loopstart >= 0 && sc->loopstart < sc->frames)
        loop = true; /* plays once, then repeats from the cue point (buffer loop points) */
    if (!buf) {
        ch->sfx = NULL; /* nothing to play: free the channel */
        return;
    }
    alSourcei(s, AL_BUFFER, (ALint)buf);
    alSourcei(s, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
    if (clockpos > 0 && sc->length > 0) {
        long long ofs = (long long)(clockpos % sc->length) * sc->frames / sc->length;
        alSourcei(s, AL_SAMPLE_OFFSET, (ALint)ofs);
    }
    ch->adp_held = false;
    Vo_Apply(ch);
    alSourcePlay(s);
}

void Vo_Stop(channel_t *ch)
{
    ALuint s = voice_of(ch);
    if (!s)
        return;
    alSourceStop(s);
    alSourcei(s, AL_BUFFER, 0);
}

qboolean Vo_Finished(channel_t *ch)
{
    ALint st = AL_STOPPED;
    ALuint s = voice_of(ch);
    if (!s)
        return true;
    alGetSourcei(s, AL_SOURCE_STATE, &st);
    return st == AL_STOPPED; /* AL_INITIAL: started in this deferred frame */
}

/* OMsnd diagnostics (s_om_debug 8): how far a voice got, in ms of its sound, and the sound's length */
int Vo_PlayedMs(channel_t *ch, int *total_ms)
{
    ALint ofs = 0;
    ALuint s = voice_of(ch);
    sfxcache_t *sc = ch->sfx ? ch->sfx->cache : NULL;
    *total_ms = sc && sc->speed > 0 ? (int)((long long)sc->frames * 1000 / sc->speed) : 0;
    if (!s || !sc || sc->speed <= 0)
        return 0;
    alGetSourcei(s, AL_SAMPLE_OFFSET, &ofs);
    return (int)((long long)ofs * 1000 / sc->speed);
}

void Vo_Pause(channel_t *ch, qboolean pause)
{
    ALuint s = voice_of(ch);
    if (!s || ch->adp_held == pause)
        return;
    ch->adp_held = pause;
    if (pause)
        alSourcePause(s);
    else
        alSourcePlay(s);
}

void Vo_Apply(channel_t *ch)
{
    ALuint s = voice_of(ch);
    int idx = (int)(ch - channels);
    sfxcache_t *sc = ch->sfx ? ch->sfx->cache : NULL;
    float gain;
    ALuint slot, filter;
    if (!s || !sc)
        return;

    if (sc->adpcm)
        /* `.adp`: trunc(volume * s_volume) on the 0-255 scale, no processing gain (SPEC-040 1.2) */
        gain = (float)(int)(ch->sp.vol * (sc->voice ? om_voicevolume : s_volume)->value) / 256.0f;
    else {
        gain = ch->sp.vol / 256.0f * s_volume->value;
        if (sc->width == 2)
            gain *= s_procgain; /* SPEC-040 1.2: the processing gain applies to 16-bit samples only */
    }

    if (ch->sp.positional) {
        /* Quake II: 0.5 per side straight ahead, 1.0 at the near-radius centre (spread 1) */
        float centre = 0.5f / POS_CENTER;
        float inner = 1.0f / NONPOS_LEVEL;
        gain *= centre + (inner - centre) * ch->sp.spread;
        if (have_spatialize)
            alSourcei(s, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);
        alSource3f(s, AL_POSITION, ch->sp.dir[0], ch->sp.dir[1], ch->sp.dir[2]);
        if (have_radius)
            alSourcef(s, AL_SOURCE_RADIUS, ch->sp.spread);
    } else {
        gain *= 1.0f / NONPOS_LEVEL;
        if (have_spatialize)
            alSourcei(s, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
        alSource3f(s, AL_POSITION, 0.0f, 0.0f, 0.0f);
        if (have_radius)
            alSourcef(s, AL_SOURCE_RADIUS, 0.0f);
    }
    if (gain < 0)
        gain = 0;
    if (gain > 4)
        gain = 4; /* s_volume above 1; the output limiter keeps the sum from clipping */
    ch->gain = gain;
    alSourcef(s, AL_GAIN, gain);

    /* the environment reverb and the underwater filter are for the game world, not the menus */
    slot = Snd_ClientActive() ? EFX_Slot() : 0;
    if (slot != voice_slot[idx]) {
        alSource3i(s, AL_AUXILIARY_SEND_FILTER, (ALint)(slot ? slot : AL_EFFECTSLOT_NULL), 0, AL_FILTER_NULL);
        voice_slot[idx] = slot;
    }
    filter = underwater && Snd_ClientActive() ? EFX_UnderwaterFilter() : 0;
    if (filter != voice_filter[idx]) {
        alSourcei(s, AL_DIRECT_FILTER, (ALint)(filter ? filter : AL_FILTER_NULL));
        voice_filter[idx] = filter;
    }
}

void Vo_SetMasterMute(qboolean mute)
{
    float g = om_gain->value >= 0 ? om_gain->value : 1.0f;
    if (!ctx)
        return;
    if (mute != muted || om_gain->modified) {
        alListenerf(AL_GAIN, mute ? 0.0f : g);
        muted = mute;
        om_gain->modified = false;
    }
}

void Vo_SetUnderwater(qboolean on)
{
    underwater = on;
}

/* `s_testsound 1`: the Quake II test tone, sin(0.1 n) at 22050 Hz (351 Hz), amplitude 20000 */
void Vo_TestTone(qboolean on)
{
    ALint st = AL_STOPPED;
    if (!tone_src)
        return;
    alGetSourcei(tone_src, AL_SOURCE_STATE, &st);
    if (on && st != AL_PLAYING) {
        if (!tone_buf) {
            /* 22050 samples hold 350.9 periods; use 6283 samples (= 100.0 periods of 62.83) for a clean loop */
            static short tone[6283];
            int i;
            for (i = 0; i < 6283; i++)
                tone[i] = (short)(sin(i * 0.1) * 20000);
            tone_buf = Vo_MakeBuffer(tone, 6283, 2, SND_CLOCK_RATE, -1);
            alSourcei(tone_src, AL_BUFFER, (ALint)tone_buf);
            alSourcei(tone_src, AL_LOOPING, AL_TRUE);
        }
        alSourcef(tone_src, AL_GAIN, 1.0f);
        alSourcePlay(tone_src);
    } else if (!on && st == AL_PLAYING)
        alSourceStop(tone_src);
}
