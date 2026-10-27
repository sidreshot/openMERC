/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module: dynamic music (`sound/music/<set>.dms`, stereo pieces as l/r `.adp` pairs) as the
 * default backend plays it (SPEC-040 5).
 *
 * Five tracks (calm = type 1, middle = type 2, action = type 3, an unused fifth, designer = type 0 by
 * trigger number), each with a level 0-255, a position and a fade (t0, t1, Ls, Lt) in output samples;
 * level = trunc(trunc((1 - f) Ls + f Lt) * Vp / 255) with Vp the piece volume. Tracks are mixed in every
 * block while in game with `s_musicenabled`; a track at level 0 does not advance. OMsnd 1.0: every track is
 * an OpenAL source playing the pieces as stereo buffers at their own rate, queued gaplessly one after the
 * other, at gain trunc(level * s_musicvolume) / 256 (not scaled by s_volume) and paused while its level is 0.
 * The state machine follows
 * the player-state music byte (applied when it changes), S_SetMusicDesignerInfo and S_SetMusicIntensity
 * exactly as the tables of SPEC-040 5.3. Fade lengths 150, 10000, 15000, 100000 output samples.
 *
 * Implements: SPEC-040 5 (5.1-5.3, forcesong), SPEC-111 (with SPEC-040's reading of the fields), SPEC-110
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AL/al.h>
#include <AL/alext.h>

#include "snd_local.h"

#define DMS_VERSION 11

enum { T_CALM, T_MIDDLE, T_ACTION, T_UNUSED, T_DESIGNER, NTRACKS };
enum { S_NONE, S_PLAYING, S_SILENCED };

#define FADE_QUICK 150
#define FADE_IN 10000
#define FADE_OUT 15000
#define FADE_LONG 100000

typedef struct {
    char left[MAX_QPATH * 2], right[MAX_QPATH * 2];
    int type, trigger, flag, volume;
    short *l, *r;
    int srclen;       /* playable source samples per channel: 2n - 19 */
    int rate;
    qboolean ok;      /* both files loaded */
} piece_t;

typedef struct {
    int piece;        /* -1: none */
    qboolean stopped; /* designer piece with flag 0 reached its end */
    int level;
    int pos;          /* samples of the piece already queued to the track's source */
    int gen;          /* bumped when the track must restart at once (new designer piece) */
    int t0, t1, ls, lt;
} track_t;

static struct {
    qboolean loaded;
    char name[MAX_QPATH];
    int count;
    piece_t *p;
} mset;

static track_t tracks[NTRACKS];
static int mstate, designer, lastmode, intensity;

static const char *track_names[NTRACKS] = {"calm", "middle", "action", "unused", "designer"};
static const char *state_names[3] = {"none", "playing", "silenced"};

/* ---- loading -------------------------------------------------------------------------------------------- */

/* ---- output: one streamed OpenAL source per track ---------------------------------------------------------- */

/* the queue holds MUS_BUFFERS x MUS_CHUNK_MS = 2 s ahead at any piece rate, so a slow game frame or a hitch of up
   to about two seconds (a model loaded during play, a save) does not run the stream dry; the gain of a track and a
   change of piece (out_flush) still apply at once, so the deeper queue adds no delay to what is heard (tests/AUD/xload.ps1:
   1 s queue, 1-1.6 s stalls: 42 dry runs in 80 s; 2 s queue: see docs/impl/SND2.md) */
#define MUS_BUFFERS 16
#define MUS_CHUNK_MS 125
#define MUS_MAXRATE 48000
#define MUS_CHUNK_MAX (MUS_MAXRATE * MUS_CHUNK_MS / 1000)

static struct {
    ALuint src;
    ALuint bufs[MUS_BUFFERS];
    ALuint freebuf[MUS_BUFFERS];
    int nfree;
    int gen;
    qboolean playing;
} mout[NTRACKS];
static qboolean mout_ready;
static int gen_counter;
static int dry_count; /* a playing music source found stopped: its queue ran out before the next refill */
static short mus_tmp[MUS_CHUNK_MAX * 2];

static void out_init(void)
{
    int t, i;
    if (mout_ready)
        return;
    memset(mout, 0, sizeof(mout));
    alGetError();
    for (t = 0; t < NTRACKS; t++) {
        alGenSources(1, &mout[t].src);
        alGenBuffers(MUS_BUFFERS, mout[t].bufs);
        if (alGetError() != AL_NO_ERROR) {
            mout[t].src = 0;
            continue;
        }
        alSourcei(mout[t].src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcef(mout[t].src, AL_ROLLOFF_FACTOR, 0.0f);
        alSource3f(mout[t].src, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcei(mout[t].src, AL_DIRECT_CHANNELS_SOFT, AL_TRUE); /* left and right as they are */
        alSourcef(mout[t].src, AL_GAIN, 0.0f);
        for (i = 0; i < MUS_BUFFERS; i++)
            mout[t].freebuf[i] = mout[t].bufs[i];
        mout[t].nfree = MUS_BUFFERS;
    }
    mout_ready = true;
}

static void out_flush(int t)
{
    int i;
    if (!mout[t].src)
        return;
    alSourceStop(mout[t].src);
    alSourcei(mout[t].src, AL_BUFFER, 0); /* unqueues everything of a stopped source */
    for (i = 0; i < MUS_BUFFERS; i++)
        mout[t].freebuf[i] = mout[t].bufs[i];
    mout[t].nfree = MUS_BUFFERS;
    mout[t].playing = false;
}

void Mus_Stop(void)
{
    int t;
    if (!mout_ready)
        return;
    for (t = 0; t < NTRACKS; t++)
        out_flush(t);
}

static void out_shutdown(void)
{
    int t;
    if (!mout_ready)
        return;
    Mus_Stop();
    for (t = 0; t < NTRACKS; t++)
        if (mout[t].src) {
            alDeleteSources(1, &mout[t].src);
            alDeleteBuffers(MUS_BUFFERS, mout[t].bufs);
        }
    alGetError();
    memset(mout, 0, sizeof(mout));
    mout_ready = false;
}

static void free_set(void)
{
    int i;
    Mus_Stop();
    for (i = 0; i < mset.count; i++) {
        free(mset.p[i].l);
        free(mset.p[i].r);
    }
    free(mset.p);
    memset(&mset, 0, sizeof(mset));
    memset(tracks, 0, sizeof(tracks));
    for (i = 0; i < NTRACKS; i++)
        tracks[i].piece = -1;
}

static qboolean cstr(const byte **p, const byte *end, char *out, size_t outsz)
{
    const byte *s = *p;
    size_t n = 0;
    while (s < end && *s) {
        if (n + 1 < outsz)
            out[n++] = (char)*s;
        s++;
    }
    if (s >= end)
        return false;
    out[n] = 0;
    *p = s + 1;
    return true;
}

/* SPEC-040 5.1: the right channel is read with the left file's length; playable length 2n - 19 */
static void load_piece(piece_t *pc)
{
    void *lb = NULL, *rb = NULL;
    int llen, rlen, lrate = 0, rrate = 0, ln = 0, rn = 0;
    const byte *lp = NULL, *rp = NULL;

    llen = Snd_LoadFile(pc->left, &lb);
    if (llen < 0 || !lb || !ADP_Parse(lb, llen, &lrate, &lp, &ln)) {
        Snd_Printf("Couldn't load %s\n", pc->left);
        if (lb)
            Snd_FreeFile(lb);
        return;
    }
    rlen = Snd_LoadFile(pc->right, &rb);
    if (rlen < 0 || !rb || !ADP_Parse(rb, rlen, &rrate, &rp, &rn)) {
        Snd_Printf("Couldn't load %s\n", pc->right);
        Snd_FreeFile(lb);
        if (rb)
            Snd_FreeFile(rb);
        return;
    }
    pc->srclen = 2 * ln - 19;
    if (pc->srclen < 0)
        pc->srclen = 0;
    pc->l = (short *)malloc((size_t)pc->srclen * sizeof(short) + 2);
    pc->r = (short *)malloc((size_t)pc->srclen * sizeof(short) + 2);
    if (pc->l && pc->r) {
        ADP_DecodeTo(lp, ln, pc->l, pc->srclen);
        ADP_DecodeTo(rp, rn, pc->r, pc->srclen);
        /* played at the left file's rate (the original maps it onto the mixer rate by the ratio rule) */
        pc->rate = lrate;
        pc->ok = pc->srclen > 0 && lrate > 0;
    }
    Snd_FreeFile(lb);
    Snd_FreeFile(rb);
}

static int random_piece(int cls)
{
    int i, n = 0, k;
    for (i = 0; i < mset.count; i++)
        n += mset.p[i].type == cls + 1;
    if (!n)
        return -1;
    k = Snd_Rand(0, n - 1);
    for (i = 0; i < mset.count; i++)
        if (mset.p[i].type == cls + 1 && !k--)
            return i;
    return -1;
}

static void reset_state(void)
{
    int i;
    memset(tracks, 0, sizeof(tracks));
    for (i = 0; i < NTRACKS; i++)
        tracks[i].piece = -1;
    tracks[T_CALM].piece = random_piece(T_CALM);
    tracks[T_MIDDLE].piece = random_piece(T_MIDDLE);
    tracks[T_ACTION].piece = random_piece(T_ACTION);
    mstate = S_SILENCED;
    designer = 0;
    lastmode = 1;
    intensity = 3;
}

static void load_set(const char *name)
{
    char path[MAX_QPATH * 2];
    void *buf;
    const byte *p, *end;
    int len, i, count;

    _snprintf_s(path, sizeof(path), _TRUNCATE, "sound/music/%s.dms", name);
    len = Snd_LoadFile(path, &buf);
    if (len < 0 || !buf) {
        Snd_Printf("%s isn't a valid music set name\n", name);
        return;
    }
    p = (const byte *)buf;
    end = p + len;
    if (len < 1 || p[0] != DMS_VERSION) {
        Snd_Printf("Invalid version for Dynamic Music Set %s (%d should be %d)\n", name, len ? p[0] : -1, DMS_VERSION);
        Snd_FreeFile(buf);
        return;
    }
    count = len > 1 ? (signed char)p[1] : 0;  // signed byte
    if (count < 0)
        count = 0;
    mset.p = (piece_t *)calloc((size_t)count + 1, sizeof(piece_t));
    if (!mset.p) {
        Snd_FreeFile(buf);
        return;
    }
    /* bytes 2-8: a post-processing count and unused values, no effect on playback */
    p += 9;
    for (i = 0; i < count; i++) {
        piece_t *pc = &mset.p[i];
        int skip;
        if (p > end || !cstr(&p, end, pc->left, sizeof(pc->left)) || !cstr(&p, end, pc->right, sizeof(pc->right)) ||
            p + 12 > end)
            break;
        pc->type = p[0];
        pc->trigger = p[1];
        pc->flag = p[2];
        pc->volume = (int)(p[3] | (p[4] << 8) | (p[5] << 16) | ((unsigned)p[6] << 24));
        skip = (signed char)p[11];  // extra bytes before the next record; 0xFF counts as 0
        p += 12 + (skip > 0 ? skip : 0);
    }
    mset.count = i;
    Snd_FreeFile(buf);
    _snprintf_s(mset.name, sizeof(mset.name), _TRUNCATE, "%s", name);
    for (i = 0; i < mset.count; i++)
        load_piece(&mset.p[i]);
    mset.loaded = true;
    reset_state();
    if ((int)om_debug->value & 1) {
        Snd_Printf("OMsnd: [p=%d] music set %s: %d pieces\n", paintedtime, name, mset.count);
        for (i = 0; i < mset.count; i++)
            Snd_Printf("OMsnd:   %d: %s type %d trigger %d flag %d volume %d length %d%s\n", i, mset.p[i].left,
                       mset.p[i].type, mset.p[i].trigger, mset.p[i].flag, mset.p[i].volume, mset.p[i].srclen,
                       mset.p[i].ok ? "" : " (not playable)");
    }
}

void Mus_Init(void)
{
    free_set();
    mstate = S_SILENCED;
    designer = 0;
    lastmode = 1;
    intensity = 3;
}

void Mus_Shutdown(void)
{
    free_set();
    out_shutdown();
}

void Mus_Register(const char *name)
{
    free_set();
    if (!s_musicenabled->value || !name || !name[0] || !strcmp(name, "(null)"))
        return;
    load_set(name);
}

void Mus_Force(const char *name, qboolean have_name)
{
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] forcesong %s\n", paintedtime, have_name ? name : "(none)");
    free_set();
    if (have_name && name && name[0] && strcmp(name, "(null)"))
        load_set(name);
}

/* ---- state machine (SPEC-040 5.3) ------------------------------------------------------------------------- */

static qboolean music_active(void)
{
    return mset.loaded && mset.count > 0;
}

static void fade(int t, int target, int duration)
{
    track_t *tr = &tracks[t];
    tr->t0 = paintedtime;
    tr->t1 = paintedtime + duration;
    tr->ls = tr->level;
    tr->lt = target;
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] music %s -> %d over %d (from %d)\n", paintedtime, track_names[t], target,
                   duration, tr->level);
}

static int find_trigger(int trigger)
{
    int i;
    for (i = 0; i < mset.count; i++)
        if (mset.p[i].trigger == trigger)
            return i;
    return -1;
}

static void start_designer(int v)
{
    int idx = find_trigger(v);
    if (idx < 0)
        return;
    tracks[T_DESIGNER].piece = idx;
    tracks[T_DESIGNER].pos = 0;
    tracks[T_DESIGNER].stopped = false;
    tracks[T_DESIGNER].gen = ++gen_counter; /* the new piece starts now, not after the queued samples */
    mstate = S_PLAYING;
    designer = v;
    fade(T_DESIGNER, 255, FADE_IN);
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] music designer piece %d (%s, flag %d)\n", paintedtime, idx, mset.p[idx].left,
                   mset.p[idx].flag);
}

static void silence_all(int duration)
{
    designer = 0;
    fade(T_ACTION, 0, duration);
    fade(T_CALM, 0, duration);
    fade(T_MIDDLE, 0, duration);
    fade(T_DESIGNER, 0, duration);
    mstate = S_SILENCED;
}

static void apply_mode(int m)
{
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d cl=%d] music mode %d -> %d (state %s, designer %d)\n", paintedtime,
                   Eng_HaveStruct() ? CL_Time() : 0, lastmode, m, state_names[mstate], designer);
    if (m == 2) {
        if (mstate == S_NONE && lastmode == 3) {
            fade(T_CALM, 255, FADE_LONG);
            fade(T_ACTION, 0, FADE_LONG);
        } else if (mstate == S_NONE) {
            fade(T_CALM, 255, FADE_LONG);
        } else {
            mstate = S_NONE;
            designer = 0;
            fade(T_CALM, 255, FADE_IN);
            fade(T_DESIGNER, 0, FADE_OUT);
        }
        intensity = 0;
    } else if (m == 3) {
        if (mstate == S_NONE && lastmode == 2) {
            fade(T_ACTION, 255, FADE_IN);
            fade(T_CALM, 0, FADE_OUT);
        } else if (mstate == S_NONE) {
            /* no fade */
        } else {
            mstate = S_NONE;
            designer = 0;
            fade(T_ACTION, 255, FADE_IN);
            fade(T_DESIGNER, 0, FADE_OUT);
        }
        intensity = 2;
    } else if (m == 1) {
        silence_all(FADE_QUICK);
    } else {
        /* 0 or >= 4: designer piece m - 3 (mode 0 has none) */
        fade(T_ACTION, 0, FADE_OUT);
        fade(T_CALM, 0, FADE_OUT);
        fade(T_MIDDLE, 0, FADE_OUT);
        start_designer(m - 3);
    }
}

void Mus_SetIntensity(int value)
{
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] S_SetMusicIntensity(%d)\n", paintedtime, value);
    intensity = value;
}

void Mus_SetDesignerInfo(int value)
{
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] S_SetMusicDesignerInfo(%d) (state %s, designer %d, intensity %d)\n", paintedtime,
                   value, state_names[mstate], designer, intensity);
    if (!music_active())
        return;
    if (value == 0xFF) {
        int t;
        if (mstate == S_NONE)
            return;
        mstate = S_NONE;
        designer = 0;
        /* intensity 3 selects the unused track (silence); >= 4 would crash the original: taken as 3 */
        t = intensity >= 0 && intensity <= 3 ? intensity : T_UNUSED;
        fade(t, 255, FADE_IN);
        fade(T_DESIGNER, 0, FADE_OUT);
    } else if (value == 0) {
        silence_all(FADE_QUICK);
    } else {
        if (value == designer)
            return;
        fade(T_ACTION, 0, FADE_OUT);
        fade(T_CALM, 0, FADE_OUT);
        fade(T_MIDDLE, 0, FADE_OUT);
        start_designer(value);
    }
}

static void stream_track(int t);

void Mus_Update(void)
{
    int t, m;

    if (!music_active())
        return;
    if (Snd_ClientActive()) {
        m = CL_MusicMode();
        if (m != lastmode) {
            apply_mode(m);
            lastmode = m;
        }
    }
    out_init();
    /* levels (SPEC-040 5.2) */
    for (t = 0; t < NTRACKS; t++) {
        track_t *tr = &tracks[t];
        int vp = tr->piece >= 0 ? mset.p[tr->piece].volume : 0;
        if (tr->t1 <= tr->t0)
            continue;
        if (paintedtime > tr->t1)
            tr->level = (int)((float)tr->lt * vp / 255);
        else {
            float f = (float)(paintedtime - tr->t0) / (float)(tr->t1 - tr->t0);
            tr->level = (int)((float)(int)((1 - f) * tr->ls + f * tr->lt) * vp / 255);
        }
    }
    for (t = 0; t < NTRACKS; t++)
        stream_track(t);
}

/* ---- playback ------------------------------------------------------------------------------------------------ */

/* end of a piece: SPEC-040 5.2 */
static void piece_end(int t)
{
    track_t *tr = &tracks[t];
    const piece_t *pc = &mset.p[tr->piece];
    if (t == T_DESIGNER) {
        if (!pc->flag) {
            tr->stopped = true;
        } else if (!designer) {
            /* degenerate case: continue with the (r + 1)-th type-0 piece if r < k, else stop */
            int i, k = 0, r = Snd_Rand(0, 32767);
            for (i = 0; i < mset.count; i++)
                k += mset.p[i].type == 0;
            if (r < k) {
                for (i = 0; i < mset.count; i++)
                    if (mset.p[i].type == 0 && !r--) {
                        tr->piece = i;
                        break;
                    }
            } else
                tr->stopped = true;
        }
        /* flag 1 with a designer number: the same piece loops */
    } else if (!designer) {
        int n = random_piece(t);
        if (n >= 0)
            tr->piece = n;
    }
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] music %s piece end -> %s%s\n", paintedtime, track_names[t],
                   tr->stopped ? "stopped" : mset.p[tr->piece].left, "");
}

/* queue the next samples of a track; a piece that ends continues with the next one in the same queue */
static void stream_track(int t)
{
    track_t *tr = &tracks[t];
    ALuint src = mout[t].src;
    ALint n = 0, state = AL_STOPPED;
    qboolean audible;
    int guard = 0;
    float gain;

    if (!src)
        return;
    if (mout[t].gen != tr->gen) {
        out_flush(t);
        mout[t].gen = tr->gen;
    }
    if (mout[t].playing) {
        alGetSourcei(src, AL_SOURCE_STATE, &state);
        /* a stopped track (a designer piece without a loop) ends when its queue does: not a dry run */
        if (state == AL_STOPPED && !tr->stopped) {
            dry_count++;
            if ((int)om_debug->value & 8)
                Snd_Printf("OMsnd: [p=%d] music %s ran dry\n", paintedtime, track_names[t]);
        }
    }
    alGetSourcei(src, AL_BUFFERS_PROCESSED, &n);
    while (n-- > 0) {
        ALuint b;
        alSourceUnqueueBuffers(src, 1, &b);
        if (mout[t].nfree < MUS_BUFFERS)
            mout[t].freebuf[mout[t].nfree++] = b;
    }

    audible = s_musicenabled->value && Snd_ClientActive() && tr->level > 0 && tr->piece >= 0;
    gain = audible ? (float)(int)(tr->level * s_musicvolume->value) / 256.0f : 0.0f;
    alSourcef(src, AL_GAIN, gain > 0 ? gain : 0.0f);
    if (!audible) {
        /* a silent track does not advance (SPEC-040 5.2) */
        if (mout[t].playing) {
            alSourcePause(src);
            mout[t].playing = false;
        }
        return;
    }

    while (mout[t].nfree > 0 && !tr->stopped && guard++ < 2 * MUS_BUFFERS) {
        const piece_t *pc = &mset.p[tr->piece];
        int k, cnt, chunk;
        ALuint b;
        if (!pc->ok)
            break;  // nothing to play; the original's track stays on it too
        chunk = pc->rate * MUS_CHUNK_MS / 1000;
        if (chunk > MUS_CHUNK_MAX)
            chunk = MUS_CHUNK_MAX;  // a faster piece queues less time ahead, never more than the buffer
        if (chunk < 256)
            chunk = 256;
        cnt = pc->srclen - tr->pos;
        if (cnt > chunk)
            cnt = chunk;
        if (cnt > 0) {
            for (k = 0; k < cnt; k++) {
                mus_tmp[2 * k] = pc->l[tr->pos + k];
                mus_tmp[2 * k + 1] = pc->r[tr->pos + k];
            }
            b = mout[t].freebuf[--mout[t].nfree];
            alBufferData(b, AL_FORMAT_STEREO16, mus_tmp, cnt * 4, pc->rate);
            alSourceQueueBuffers(src, 1, &b);
            tr->pos += cnt;
        }
        if (tr->pos >= pc->srclen) {
            piece_end(t);
            tr->pos = 0;
        }
    }

    alGetSourcei(src, AL_SOURCE_STATE, &state);
    alGetSourcei(src, AL_BUFFERS_QUEUED, &n);
    if (state != AL_PLAYING && n > 0)
        alSourcePlay(src);  // first start, resume after a silence, or after running dry
    mout[t].playing = n > 0;
}
int Mus_TakeDryCount(void)
{
    int n = dry_count;
    dry_count = 0;
    return n;
}

void Mus_Info(void)
{
    int t;
    if (!music_active()) {
        Snd_Printf("music: no set loaded\n");
        return;
    }
    Snd_Printf("music: set %s, state %s, designer %d, mode %d, intensity %d\n", mset.name, state_names[mstate],
               designer, lastmode, intensity);
    for (t = 0; t < NTRACKS; t++)
        if (tracks[t].piece >= 0)
            Snd_Printf("  %-8s %s level %d pos %d/%d%s\n", track_names[t], mset.p[tracks[t].piece].left,
                       tracks[t].level, tracks[t].pos, mset.p[tracks[t].piece].srclen,
                       tracks[t].stopped ? " (stopped)" : "");
}
