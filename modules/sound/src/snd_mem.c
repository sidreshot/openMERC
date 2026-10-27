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
 * OpenMerc: Quake II 3.19 client/snd_mem.c (sound caching, WAV loading), changed for the SoF default sound
 * backend: `.adp` sounds decoded at their own rate (SPEC-040 1.3), bounds-checked chunk walk, alias names from
 * the side table of snd_dma.c, and (OMsnd 1.0) every sound uploaded to an OpenAL buffer at its own rate.
 *
 * Implements: SPEC-040 1.3 (sound data), SPEC-004 ("Sounds and names", file lookup), SPEC-110
 */
// snd_mem.c: sound caching

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snd_local.h"

const char *S_TrueName(const sfx_t *s);

typedef struct {
    int rate;
    int width;
    int channels;
    int loopstart;
    int samples;
    int dataofs;  // chunk starts this many bytes from file start
} wavinfo_t;

/*
===============================================================================

WAV loading

===============================================================================
*/

static const byte *data_p;
static const byte *iff_end;
static const byte *last_chunk;
static const byte *iff_data;
static int iff_chunk_len;

static short GetLittleShort(void)
{
    short val;
    if (data_p + 2 > iff_end) {
        data_p += 2;
        return 0;
    }
    val = (short)(data_p[0] | (data_p[1] << 8));
    data_p += 2;
    return val;
}

static int GetLittleLong(void)
{
    int val;
    if (data_p + 4 > iff_end) {
        data_p += 4;
        return 0;
    }
    val = (int)(data_p[0] | (data_p[1] << 8) | (data_p[2] << 16) | ((unsigned)data_p[3] << 24));
    data_p += 4;
    return val;
}

static void FindNextChunk(const char *name)
{
    while (1) {
        data_p = last_chunk;

        if (data_p + 8 > iff_end) {  // didn't find the chunk
            data_p = NULL;
            return;
        }

        data_p += 4;
        iff_chunk_len = GetLittleLong();
        if (iff_chunk_len < 0) {
            data_p = NULL;
            return;
        }
        data_p -= 8;
        last_chunk = data_p + 8 + ((iff_chunk_len + 1) & ~1);
        if (last_chunk < data_p) {  // OpenMerc: wrapped pointer
            data_p = NULL;
            return;
        }
        if (!strncmp((const char *)data_p, name, 4))
            return;
    }
}

static void FindChunk(const char *name)
{
    last_chunk = iff_data;
    FindNextChunk(name);
}

/*
============
GetWavinfo
============
*/
static wavinfo_t GetWavinfo(const char *name, const byte *wav, int wavlength)
{
    wavinfo_t info;
    int i;
    int format;
    int samples;

    memset(&info, 0, sizeof(info));

    if (!wav)
        return info;

    iff_data = wav;
    iff_end = wav + wavlength;

    // find "RIFF" chunk
    FindChunk("RIFF");
    if (!(data_p && data_p + 12 <= iff_end && !strncmp((const char *)data_p + 8, "WAVE", 4))) {
        Snd_Printf("Missing RIFF/WAVE chunks\n");
        return info;
    }

    // get "fmt " chunk
    iff_data = data_p + 12;

    FindChunk("fmt ");
    if (!data_p) {
        Snd_Printf("Missing fmt chunk\n");
        return info;
    }
    data_p += 8;
    format = GetLittleShort();
    if (format != 1) {
        Snd_Printf("Microsoft PCM format only\n");
        return info;
    }

    info.channels = GetLittleShort();
    info.rate = GetLittleLong();
    data_p += 4 + 2;
    info.width = GetLittleShort() / 8;

    // get cue chunk
    FindChunk("cue ");
    if (data_p) {
        data_p += 32;
        info.loopstart = GetLittleLong();

        // if the next chunk is a LIST chunk, look for a cue length marker
        FindNextChunk("LIST");
        if (data_p) {
            if (data_p + 32 <= iff_end && !strncmp((const char *)data_p + 28, "mark", 4)) {
                // this is not a proper parse, but it works with cooledit...
                data_p += 24;
                i = GetLittleLong();  // samples in loop
                info.samples = info.loopstart + i;
            }
        }
    } else
        info.loopstart = -1;

    // find data chunk
    FindChunk("data");
    if (!data_p) {
        Snd_Printf("Missing data chunk\n");
        return info;
    }

    data_p += 4;
    samples = info.width > 0 ? GetLittleLong() / info.width : 0;

    if (info.samples) {
        if (samples < info.samples)
            eng.Com_Error(ERR_DROP, "Sound %s has a bad loop length", name);
    } else
        info.samples = samples;

    info.dataofs = (int)(data_p - wav);

    return info;
}

/* clock samples (SND_CLOCK_RATE) for a number of samples at a rate */
static int clock_length(int frames, int rate)
{
    return rate > 0 ? (int)((long long)frames * SND_CLOCK_RATE / rate) : 0;
}

/*
==============
S_LoadSound

OMsnd 1.0: the samples go to an OpenAL buffer at their own rate and width; OpenAL resamples them at the
device rate (Quake II resampled them once to the mixer rate by nearest sample, and `s_loadas8bit` halved
16-bit sounds to 8 bits; both are gone).
==============
*/
sfxcache_t *S_LoadSound(sfx_t *s)
{
    char namebuffer[MAX_QPATH];
    byte *data;
    wavinfo_t info;
    sfxcache_t *sc;
    int size, have, bytes;
    const char *name;
    size_t n;

    if (s->name[0] == '*')
        return NULL;

    // see if still in memory
    sc = s->cache;
    if (sc)
        return sc;

    // load it in
    name = S_TrueName(s);
    if (name[0] == '#')
        _snprintf_s(namebuffer, sizeof(namebuffer), _TRUNCATE, "%s", &name[1]);
    else
        _snprintf_s(namebuffer, sizeof(namebuffer), _TRUNCATE, "sound/%s", name);

    size = Snd_LoadFile(namebuffer, (void **)&data);
    if (!data) {
        Snd_DPrintf("Couldn't load %s\n", namebuffer);
        return NULL;
    }

    // SoF: `.adp` by the last four characters of the resolved path, case-sensitive (SPEC-040 1.3)
    n = strlen(namebuffer);
    if (n >= 4 && !strcmp(namebuffer + n - 4, ".adp")) {
        const byte *payload;
        int nbytes, rate, srclen;
        short *pcm;
        if (!ADP_Parse(data, size, &rate, &payload, &nbytes) || rate <= 0) {
            Snd_FreeFile(data);
            return NULL;
        }
        srclen = 2 * size - 46;  // playable length: the last 38 decoded samples are never played
        if (srclen < 1)
            srclen = 1;
        pcm = (short *)calloc((size_t)srclen + 1, sizeof(short));
        sc = (sfxcache_t *)calloc(1, sizeof(sfxcache_t));
        if (!pcm || !sc) {
            free(pcm);
            free(sc);
            Snd_FreeFile(data);
            return NULL;
        }
        ADP_DecodeTo(payload, nbytes, pcm, srclen);
        Snd_FreeFile(data);
        /* played at the header rate: the original's ratio rule (SPEC-040 1.3) maps the rate onto the mixer rate
         * by an integer ratio, which plays 8000 or 32000 Hz files off-pitch; OpenAL resamples exactly */
        sc->adpcm = true;
        sc->voice = !_strnicmp(namebuffer, "sound/cin/", 10) || !_strnicmp(namebuffer, "sound/speech/", 13) ||
                    !_strnicmp(namebuffer, "sound/enemy/", 12);
        sc->frames = srclen;
        sc->speed = rate;
        sc->width = 2;
        sc->loopstart = -1;
        sc->bytes = srclen * 2;
        sc->length = clock_length(srclen, rate);
        sc->buffer = Vo_MakeBuffer(pcm, srclen, 2, rate, -1);
        free(pcm);
        if (!sc->buffer) {
            free(sc);
            return NULL;
        }
        s->cache = sc;
        return sc;
    }

    info = GetWavinfo(s->name, data, size);
    if (info.channels != 1) {
        Snd_Printf("%s is a stereo sample\n", s->name);
        Snd_FreeFile(data);
        return NULL;
    }
    if (info.rate <= 0 || info.width < 1 || info.width > 2 || info.samples <= 0) {  // OpenMerc: unusable header
        Snd_FreeFile(data);
        return NULL;
    }

    sc = (sfxcache_t *)calloc(1, sizeof(sfxcache_t));
    bytes = info.samples * info.width;
    if (sc)
        sc->data = calloc(1, (size_t)bytes + 2);
    if (!sc || !sc->data) {
        free(sc);
        Snd_FreeFile(data);
        return NULL;
    }
    // OpenMerc: a data chunk shorter than its header claims reads as silence
    have = info.dataofs < size ? size - info.dataofs : 0;
    if (have > bytes)
        have = bytes;
    memcpy(sc->data, data + info.dataofs, (size_t)have);
    if (info.width == 1)
        memset((byte *)sc->data + have, 0x80, (size_t)(bytes - have)); // unsigned 8-bit silence
    Snd_FreeFile(data);

    sc->frames = info.samples;
    sc->speed = info.rate;
    sc->width = info.width;
    sc->stereo = 0;
    sc->bytes = bytes;
    sc->loopstart = info.loopstart >= 0 && info.loopstart < info.samples ? info.loopstart : -1;
    sc->length = clock_length(info.samples, info.rate);
    sc->buffer = Vo_MakeBuffer(sc->data, sc->frames, sc->width, sc->speed, sc->loopstart);
    if (!sc->buffer) {
        free(sc->data);
        free(sc);
        return NULL;
    }
    if (sc->loopstart <= 0) {  // only a cue loop needs the samples again (autosound copy without loop points)
        free(sc->data);
        sc->data = NULL;
    }
    s->cache = sc;
    return sc;
}

void S_FreeCache(sfx_t *s)
{
    sfxcache_t *sc = s->cache;
    if (sc) {
        Vo_DeleteBuffer(sc->buffer);
        Vo_DeleteBuffer(sc->buffer_whole);
        free(sc->data);
        free(sc);
    }
    s->cache = NULL;
}