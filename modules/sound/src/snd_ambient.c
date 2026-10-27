/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module: ambient sound sets (`.ams`, SPEC-112) and entity loop sounds, as the default
 * backend plays them (SPEC-040 sections 2-4).
 *
 * - General sets (group 0): the index is the low byte of the player-state field (compared as 16 bits).
 *   Each change to a non-zero set with a loop starts a 2000 ms crossfade of the two sets' loops (client time;
 *   a change to set 0 or to a set without a loop stops the old loop at once, SPEC-AMEND-006 I5s-2.2) and
 *   every change reschedules the random one-shot timer. Loop volume vmin + 0.4 * floor((vmax - vmin) / 2),
 *   non-positional, position sample time mod length. A one-shot is due when client time > the scheduled time; it starts
 *   non-positionally (attenuation 0, channel 0, entity = wave index) at vmin + U[0, vmax - vmin]; next
 *   time = client time + min * 1000 + U[0, 999] * (max - min) ms.
 * - Point sets (group 1, entities with state flag 0x100, set index in the sound field): hard cut at the
 *   set's distance D, f = 1 - dist / D; loop at trunc((vmin + floor((vmax - vmin) / 2)) * f), spatialized
 *   with distance multiplier 0.003 and near radius D; one-shots on a timer per entity number (reset only by
 *   S_Init) at trunc((vmin + U) * f), non-positional.
 * - Entity loop sounds (other entities, sound field 1..355): volume = loop parameter & 0xF8, distance
 *   multiplier by bits 0-2, position (sample time + k * 143117) mod length for the k-th entity of the
 *   frame, no merging; skipped while paused. They need an ambient file loaded at least once since S_Init.
 * All loops are autosound channels. Quake II clears them every frame and allocates them again; OMsnd 1.0 keeps a
 * loop's voice while it is re-added with the same sound and key (set index, entity) and stops it the first frame it
 * is not (S_AddLoop): the same sound without restarting a voice every frame.
 *
 * Implements: SPEC-040 2, 3, 4, SPEC-AMEND-006 I5s-2.2, SPEC-112, SPEC-004 ("Ambient sounds")
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snd_local.h"

#define AMS_VERSION 11
#define MAX_AMS_WAVES 1024
#define MAX_GROUP_SETS 256
#define CROSSFADE_MS 2000
#define LOOP_PHASE_STEP 143117
#define POINT_DIST_MULT 0.003f

typedef struct {
    char name[64];
    float tmin, tmax;
    int loop;          /* wave index or -1 */
    int count;         /* low byte of the stored count (SPEC-040 "How the backends read .ams") */
    unsigned short waves[255];
    float distance;
    int vmin, vmax;
} amset_t;

static struct {
    qboolean valid;
    char path[MAX_QPATH];
    int nwaves;
    char (*wavename)[64];
    sfx_t **wavesfx;   /* bound at S_EndRegistration by name lookup only */
    int count[3];
    amset_t *sets[3];
} ams;

static qboolean ams_ever;          /* an ambient file loaded at least once since S_Init (SPEC-040 2) */
static int gen_cur, gen_prev;      /* general set indices */
static int gen_start;              /* crossfade start, client ms */
static int gen_next;               /* next general one-shot, client ms */
static int point_timer[MAX_EDICTS];

/* distance multipliers of entity loop sounds by parameter bits 0-2 (SPEC-040 2; 5-7 are not valid: the
 * original reads unrelated bytes that behave as "audible only within 80 units" (5, 7) and "no fall-off" (6)) */
static const float loop_dist_mult[8] = {0.003f, 0.003f, 0.003f, 0.0005f, 0.00025f, 1.0e9f, 0.0f, 1.0e9f};

/* ---- file --------------------------------------------------------------------------------------------- */

static void free_ams(void)
{
    int g;
    free(ams.wavename);
    free(ams.wavesfx);
    for (g = 0; g < 3; g++)
        free(ams.sets[g]);
    memset(&ams, 0, sizeof(ams));
}

static unsigned rd32(const byte *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static float rdf(const byte *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

static qboolean cstr(const byte **p, const byte *end, char *out, size_t outsz)
{
    const byte *s = *p;
    size_t n = 0;
    while (s < end && *s) {
        if (out && n + 1 < outsz)
            out[n++] = (char)*s;
        s++;
    }
    if (s >= end)
        return false;
    if (out)
        out[n] = 0;
    *p = s + 1;
    return true;
}

static qboolean parse_ams(const byte *buf, int len)
{
    const byte *p, *end = buf + len;
    unsigned nw, cnt[3];
    unsigned i;
    int g;

    if (len < 17)
        return false;
    nw = rd32(buf + 1);
    cnt[0] = rd32(buf + 5);
    cnt[1] = rd32(buf + 9);
    cnt[2] = rd32(buf + 13);
    p = buf + 17;
    ams.nwaves = nw > MAX_AMS_WAVES ? MAX_AMS_WAVES : (int)nw;
    ams.wavename = calloc((size_t)ams.nwaves + 1, sizeof(*ams.wavename));
    ams.wavesfx = calloc((size_t)ams.nwaves + 1, sizeof(*ams.wavesfx));
    if (!ams.wavename || !ams.wavesfx)
        return false;
    for (i = 0; i < nw; i++)
        if (!cstr(&p, end, i < (unsigned)ams.nwaves ? ams.wavename[i] : NULL, 64))
            return false;
    for (g = 0; g < 3; g++) {
        unsigned n = cnt[g];
        /* SPEC-040: a group whose count is exactly 255 is skipped while parsing (UNVERIFIED, no stock case) */
        if (n == 255)
            n = 0;
        ams.count[g] = n > MAX_GROUP_SETS ? MAX_GROUP_SETS : (int)n;
        ams.sets[g] = calloc((size_t)ams.count[g] + 1, sizeof(amset_t));
        if (!ams.sets[g])
            return false;
        for (i = 0; i < n; i++) {
            amset_t tmp, *s = i < (unsigned)ams.count[g] ? &ams.sets[g][i] : &tmp;
            int k, c;
            if (!cstr(&p, end, s->name, sizeof(s->name)) || p + 14 > end)
                return false;
            /* p[0], p[1]: flag_a, flag_b - stored, never used */
            s->tmin = rdf(p + 2);
            s->tmax = rdf(p + 6);
            s->loop = (short)(p[10] | (p[11] << 8));
            c = p[12]; /* only the low byte of the count */
            p += 14;
            if (p + 2 * c + 6 > end)
                return false;
            s->count = c;
            for (k = 0; k < c; k++)
                s->waves[k] = (unsigned short)(p[2 * k] | (p[2 * k + 1] << 8));
            p += 2 * c;
            s->distance = rdf(p);
            s->vmin = p[4];
            s->vmax = p[5];
            p += 6;
        }
    }
    return true;
}

void Amb_Init(void)
{
    free_ams();
    ams_ever = false;
    gen_cur = gen_prev = 0;
    gen_start = -100000;
    gen_next = 0;
    memset(point_timer, 0, sizeof(point_timer));
}

void Amb_Shutdown(void)
{
    free_ams();
}

void Amb_Register(const char *path)
{
    void *buf;
    int len;

    free_ams();
    if (!path)
        path = "";
    len = Snd_LoadFile(path, &buf);
    if (len < 0 || !buf) {
        Snd_Printf("Unable to locate %s - all ambient sounds will be disabled\n", path);
        return;
    }
    if (len < 1 || ((const byte *)buf)[0] != AMS_VERSION) {
        Snd_Printf("Invalid version (%d) for Ambient Sound Set %s (Should be %d)\n", len ? ((const byte *)buf)[0] : -1,
                   path, AMS_VERSION);
        Snd_FreeFile(buf);
        return;
    }
    if (!parse_ams((const byte *)buf, len)) {
        Snd_Printf("OMsnd: %s is damaged - all ambient sounds will be disabled\n", path);
        Snd_FreeFile(buf);
        free_ams();
        return;
    }
    Snd_FreeFile(buf);
    _snprintf_s(ams.path, sizeof(ams.path), _TRUNCATE, "%s", path);
    ams.valid = true;
    ams_ever = true;
    /* a level start counts as a change from index 0 (SPEC-040 3.2); the one-shot timer starts at 0 */
    gen_cur = gen_prev = 0;
    gen_start = -100000;
    gen_next = 0;
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: ambient sets %s: %d waves, %d general, %d local, %d triggered\n", path, ams.nwaves,
                   ams.count[0], ams.count[1], ams.count[2]);
}

void Amb_EndRegistration(void)
{
    int i, bound = 0;
    if (!ams.valid)
        return;
    for (i = 0; i < ams.nwaves; i++) {
        size_t n = strlen(ams.wavename[i]);
        ams.wavesfx[i] = (n && n < MAX_QPATH - 1) ? S_FindName(ams.wavename[i], 0) : NULL;
        bound += ams.wavesfx[i] != NULL;
    }
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: ambient waves bound to registered sounds: %d of %d\n", bound, ams.nwaves);
}

void Amb_ForgetSfx(const sfx_t *s)
{
    int i;
    if (!ams.wavesfx)
        return;
    for (i = 0; i < ams.nwaves; i++)
        if (ams.wavesfx[i] == s)
            ams.wavesfx[i] = NULL;
}

/* ---- helpers ------------------------------------------------------------------------------------------ */

static sfx_t *wave(int w, sfxcache_t **sc)
{
    sfx_t *s;
    if (w < 0 || w >= ams.nwaves || !(s = ams.wavesfx[w]))
        return NULL;
    *sc = S_LoadSound(s);
    return *sc ? s : NULL;
}

/* loop keys: general sets by index, point sets and entity loops by entity number */
#define KEY_GENERAL(i) (-1000 - (i))
#define KEY_POINT(n) (0x10000 + (n))
#define KEY_ENTITY(n) (n)

static void add_flat_loop(sfx_t *sfx, sfxcache_t *sc, int vol, int pos, int key)
{
    spat_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.vol = (float)vol;
    sp.positional = false;
    S_AddLoop(sfx, sc, &sp, pos, key);
}

static int next_time(int now, const amset_t *s)
{
    return (int)(now + s->tmin * 1000.0f + Snd_Rand(0, 999) * (s->tmax - s->tmin));
}

/* ---- general set (SPEC-040 3.1-3.3) -------------------------------------------------------------------- */

static void general_change(int value, int now)
{
    gen_start = now;
    gen_prev = gen_cur;
    gen_cur = value & 0xFF;
    if (gen_cur < ams.count[0])
        gen_next = next_time(now, &ams.sets[0][gen_cur]);
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d cl=%d] general ambient set %d (%s) -> %d (%s)\n", paintedtime, now, gen_prev,
                   gen_prev < ams.count[0] ? ams.sets[0][gen_prev].name : "?", gen_cur,
                   gen_cur < ams.count[0] ? ams.sets[0][gen_cur].name : "?");
}

void Amb_SetGeneral(int index)
{
    /* takes effect at once; the next frame restores the player-state value if it differs (SPEC-040 3.1) */
    if ((int)om_debug->value & 1)
        Snd_Printf("OMsnd: [p=%d] S_SetGeneralAmbientSet(%d)\n", paintedtime, index);
    if (ams.valid && Eng_HaveStruct() && index != gen_cur)
        general_change(index, CL_Time());
}

/* a general set with a loop: index not 0 and a loop wave (the stored -1 has the low byte 255, which the
 * amendment calls "loop index 255") */
static qboolean general_has_loop(int index)
{
    return index > 0 && index < ams.count[0] && (ams.sets[0][index].loop & 0xFF) != 0xFF;
}

static void general_loop(int index, float scale)
{
    const amset_t *s;
    sfx_t *sfx;
    sfxcache_t *sc;
    int v;
    if (!general_has_loop(index))
        return;  // index 0 = no general ambience
    s = &ams.sets[0][index];
    if (!(sfx = wave(s->loop, &sc)))
        return;
    v = (int)(s->vmin + 0.4f * ((s->vmax - s->vmin) / 2));
    v = (int)(v * scale);
    if (v <= 0)
        return;
    add_flat_loop(sfx, sc, v, paintedtime, KEY_GENERAL(index));
}

static void update_general(int now)
{
    int value = CL_GeneralAmbientField();
    if (value != gen_cur)
        general_change(value, now);

    /* SPEC-AMEND-006 I5s-2.2 (measured: docs/impl/I5s-2.md finding 22): loops only while the current set is not 0
     * and has a loop, so a change to 0 or to a set without a loop stops the previous loop at once; within the
     * 2000 ms window the previous loop plays only when the previous index is not 0 */
    if (general_has_loop(gen_cur)) {
        if (now - gen_start < CROSSFADE_MS) {
            float f = (now - gen_start) * 0.0005f;
            if (f < 0)
                f = 0;
            general_loop(gen_prev, 1.0f - f);
            general_loop(gen_cur, f);
        } else
            general_loop(gen_cur, 1.0f);
    }

    if (now > gen_next && gen_cur < ams.count[0]) {
        const amset_t *s = &ams.sets[0][gen_cur];
        if (s->count > 0) {
            sfx_t *sfx;
            sfxcache_t *sc;
            int w = s->waves[Snd_Rand(0, s->count - 1)];
            int vol = s->vmin + Snd_Rand(0, s->vmax - s->vmin);
            if ((sfx = wave(w, &sc)) != NULL) {
                if ((int)om_debug->value & 2)
                    Snd_Printf("OMsnd: [p=%d cl=%d] general one-shot %s vol %d\n", paintedtime, now, sfx->name, vol);
                S_StartSoundVol(NULL, w, 0, sfx, vol, 0, 0);
            }
            gen_next = next_time(now, s);
        }
    }
}

/* ---- point sets and entity loops (SPEC-040 2, 3.4) ----------------------------------------------------- */

static void point_set(const frame_entity_t *e, int now)
{
    const amset_t *s;
    float dx, dy, dz, dist, f;
    sfx_t *sfx;
    sfxcache_t *sc;

    if (e->sound < 0 || e->sound >= ams.count[1])
        return;
    s = &ams.sets[1][e->sound];
    dx = e->origin[0] - listener_origin[0];
    dy = e->origin[1] - listener_origin[1];
    dz = e->origin[2] - listener_origin[2];
    dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!(s->distance > 0) || dist > s->distance)
        return;  // hard cut at the radius
    f = 1.0f - dist / s->distance;

    if (s->loop >= 0 && (sfx = wave(s->loop, &sc)) != NULL) {
        spat_t sp;
        float v = (float)(int)((s->vmin + (s->vmax - s->vmin) / 2) * f);
        S_SpatializeOrigin(e->origin, v, POINT_DIST_MULT, s->distance, &sp);
        S_AddLoop(sfx, sc, &sp, paintedtime, KEY_POINT(e->number));
    }

    if (e->number >= 0 && e->number < MAX_EDICTS && now > point_timer[e->number] && s->count > 0) {
        int w = s->waves[Snd_Rand(0, s->count - 1)];
        int vol = (int)((s->vmin + Snd_Rand(0, s->vmax - s->vmin)) * f);
        if ((sfx = wave(w, &sc)) != NULL) {
            if ((int)om_debug->value & 2)
                Snd_Printf("OMsnd: [p=%d cl=%d] point one-shot ent %d %s vol %d\n", paintedtime, now, e->number,
                           sfx->name, vol);
            S_StartSoundVol(NULL, w, 0, sfx, vol, 0, 0);
        }
        point_timer[e->number] = next_time(now, s);
    }
}

static void entity_loop(const frame_entity_t *e, int k)
{
    sfx_t *sfx;
    sfxcache_t *sc;
    vec3_t origin;
    spat_t sp;

    if (e->sound < 1 || e->sound >= OM_MAX_SOUNDS)
        return;
    sfx = CL_SoundPrecache(e->sound);
    if (!sfx || !(sc = S_LoadSound(sfx)))
        return;
    if (eng.CL_GetEntitySoundOrigin && e->number >= 0 && e->number < MAX_EDICTS)
        eng.CL_GetEntitySoundOrigin(e->number, origin);
    else {
        origin[0] = e->origin[0];
        origin[1] = e->origin[1];
        origin[2] = e->origin[2];
    }
    S_SpatializeOrigin(origin, (float)(e->loopparm & 0xF8), loop_dist_mult[e->loopparm & 7], 0, &sp);
    S_AddLoop(sfx, sc, &sp, paintedtime + k * LOOP_PHASE_STEP, KEY_ENTITY(e->number));
}

void Amb_AddSounds(void)
{
    int i, n, now;
    static unsigned short seen[MAX_EDICTS];

    if (!Snd_ClientActive() || !ams_ever)
        return;
    now = CL_Time();
    if (ams.valid)
        update_general(now);

    n = CL_FrameNumEntities();
    if (n < 0 || n > MAX_PARSE_ENTITIES)
        n = 0;
    for (i = 0; i < n; i++) {
        frame_entity_t e;
        if (!CL_FrameEntity(i, &e))
            continue;
        if ((int)om_debug->value & 2 && e.number >= 0 && e.number < MAX_EDICTS &&
            seen[e.number] != (unsigned short)(((e.flags & ENTFLAG_AMBIENT_POINT) ? 0x8000 : 0) | (e.sound + 1))) {
            seen[e.number] = (unsigned short)(((e.flags & ENTFLAG_AMBIENT_POINT) ? 0x8000 : 0) | (e.sound + 1));
            if (e.flags & ENTFLAG_AMBIENT_POINT)
                Snd_Printf("OMsnd: [p=%d cl=%d] point set ent %d set %d (%s) at %.0f %.0f %.0f\n", paintedtime,
                           now, e.number, e.sound,
                           ams.valid && e.sound >= 0 && e.sound < ams.count[1] ? ams.sets[1][e.sound].name : "-",
                           e.origin[0], e.origin[1], e.origin[2]);
            else if (e.sound > 0)
                Snd_Printf("OMsnd: [p=%d cl=%d] loop ent %d sound %d parm 0x%02x\n", paintedtime, now, e.number,
                           e.sound, e.loopparm);
        }
        if (e.flags & ENTFLAG_AMBIENT_POINT) {
            if (ams.valid)
                point_set(&e, now);
        } else if (!paused->value)
            entity_loop(&e, i);
    }
}

void Amb_Info(void)
{
    if (!ams.valid) {
        Snd_Printf("ambient: no set file%s\n", ams_ever ? " (one was loaded before)" : "");
        return;
    }
    Snd_Printf("ambient: %s, general set %d (%s), previous %d, next one-shot at %d\n", ams.path, gen_cur,
               gen_cur < ams.count[0] ? ams.sets[0][gen_cur].name : "?", gen_prev, gen_next);
}
