/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc sound module: optional environmental reverb in the manner of EAXSnd (SPEC-040 6.5). EAX 2.0
 * environments (the public 26-preset list) and the per-map `.eal` default environment are mapped to an
 * OpenAL EFX (EAX) reverb on one auxiliary effect slot that every game voice sends to (not the music and
 * not the menu sounds). Defsnd has no reverb (SPEC-040 1); OMsnd 1.0 turns it on by default
 * (`s_om_reverb 1`, 0 gives Defsnd's dry sound). EAX volume and distance models are not done.
 * Processing type 1 (underwater) also puts a low-pass filter on the game voices (`s_om_underwaterlp`).
 *
 * Environment selection (SPEC-004 "EAXSnd differences", SPEC-113):
 *   1. `maps/<map>.eal` next to the map: its `denv` block (EAXLISTENERPROPERTIES) converted with
 *      the usual millibel -> gain mapping;
 *   2. otherwise the EAXSnd map table (tsr1 12, trn1 19, arm1 4, kos1 8);
 *   3. otherwise Generic.
 * Processing type 1 (underwater) selects EAX environment 22; type 0 restores the level's.
 *
 * Preset values come from OpenAL Soft's <AL/efx-presets.h> (Creative's published EAX presets).
 *
 * Implements: SPEC-113, SPEC-004 (SNDEAX_SetEnvironment, sound processing type)
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <AL/efx-presets.h>

#include "snd_local.h"

static int proctype;

static LPALGENEFFECTS p_alGenEffects;
static LPALDELETEEFFECTS p_alDeleteEffects;
static LPALEFFECTI p_alEffecti;
static LPALEFFECTF p_alEffectf;
static LPALEFFECTFV p_alEffectfv;
static LPALGENAUXILIARYEFFECTSLOTS p_alGenAuxiliaryEffectSlots;
static LPALDELETEAUXILIARYEFFECTSLOTS p_alDeleteAuxiliaryEffectSlots;
static LPALAUXILIARYEFFECTSLOTI p_alAuxiliaryEffectSloti;
static LPALAUXILIARYEFFECTSLOTF p_alAuxiliaryEffectSlotf;
static LPALGENFILTERS p_alGenFilters;
static LPALDELETEFILTERS p_alDeleteFilters;
static LPALFILTERI p_alFilteri;
static LPALFILTERF p_alFilterf;
static ALuint uw_filter;

static qboolean efx_ok, efx_eaxreverb;
static ALuint efx_slot, efx_effect;
static EFXEAXREVERBPROPERTIES level_props, current_props;
static char level_desc[96] = "Generic (default)";
static char current_desc[96] = "Generic (default)";

static const EFXEAXREVERBPROPERTIES eax_presets[EAX_ENVIRONMENT_COUNT] = {
    EFX_REVERB_PRESET_GENERIC,     EFX_REVERB_PRESET_PADDEDCELL,      EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,    EFX_REVERB_PRESET_LIVINGROOM,      EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,  EFX_REVERB_PRESET_CONCERTHALL,     EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,       EFX_REVERB_PRESET_HANGAR,          EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,     EFX_REVERB_PRESET_STONECORRIDOR,   EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,      EFX_REVERB_PRESET_CITY,            EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,      EFX_REVERB_PRESET_PLAIN,           EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,   EFX_REVERB_PRESET_UNDERWATER,      EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY,       EFX_REVERB_PRESET_PSYCHOTIC,
};

static const char *eax_names[EAX_ENVIRONMENT_COUNT] = {
    "Generic", "PaddedCell", "Room", "Bathroom", "LivingRoom", "StoneRoom", "Auditorium",
    "ConcertHall", "Cave", "Arena", "Hangar", "CarpetedHallway", "Hallway", "StoneCorridor",
    "Alley", "Forest", "City", "Mountains", "Quarry", "Plain", "ParkingLot", "SewerPipe",
    "Underwater", "Drugged", "Dizzy", "Psychotic",
};

/* EAXSnd's built-in environments for maps without an .eal (SPEC-004) */
static const struct {
    const char *map;
    int env;
} map_table[] = {{"tsr1.bsp", 12}, {"trn1.bsp", 19}, {"arm1.bsp", 4}, {"kos1.bsp", 8}};

static void apply(const EFXEAXREVERBPROPERTIES *r, const char *desc)
{
    current_props = *r;
    _snprintf_s(current_desc, sizeof(current_desc), _TRUNCATE, "%s", desc);
    if (!efx_ok)
        return;
    if (efx_eaxreverb) {
        p_alEffectf(efx_effect, AL_EAXREVERB_DENSITY, r->flDensity);
        p_alEffectf(efx_effect, AL_EAXREVERB_DIFFUSION, r->flDiffusion);
        p_alEffectf(efx_effect, AL_EAXREVERB_GAIN, r->flGain);
        p_alEffectf(efx_effect, AL_EAXREVERB_GAINHF, r->flGainHF);
        p_alEffectf(efx_effect, AL_EAXREVERB_GAINLF, r->flGainLF);
        p_alEffectf(efx_effect, AL_EAXREVERB_DECAY_TIME, r->flDecayTime);
        p_alEffectf(efx_effect, AL_EAXREVERB_DECAY_HFRATIO, r->flDecayHFRatio);
        p_alEffectf(efx_effect, AL_EAXREVERB_DECAY_LFRATIO, r->flDecayLFRatio);
        p_alEffectf(efx_effect, AL_EAXREVERB_REFLECTIONS_GAIN, r->flReflectionsGain);
        p_alEffectf(efx_effect, AL_EAXREVERB_REFLECTIONS_DELAY, r->flReflectionsDelay);
        p_alEffectfv(efx_effect, AL_EAXREVERB_REFLECTIONS_PAN, r->flReflectionsPan);
        p_alEffectf(efx_effect, AL_EAXREVERB_LATE_REVERB_GAIN, r->flLateReverbGain);
        p_alEffectf(efx_effect, AL_EAXREVERB_LATE_REVERB_DELAY, r->flLateReverbDelay);
        p_alEffectfv(efx_effect, AL_EAXREVERB_LATE_REVERB_PAN, r->flLateReverbPan);
        p_alEffectf(efx_effect, AL_EAXREVERB_ECHO_TIME, r->flEchoTime);
        p_alEffectf(efx_effect, AL_EAXREVERB_ECHO_DEPTH, r->flEchoDepth);
        p_alEffectf(efx_effect, AL_EAXREVERB_MODULATION_TIME, r->flModulationTime);
        p_alEffectf(efx_effect, AL_EAXREVERB_MODULATION_DEPTH, r->flModulationDepth);
        p_alEffectf(efx_effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, r->flAirAbsorptionGainHF);
        p_alEffectf(efx_effect, AL_EAXREVERB_HFREFERENCE, r->flHFReference);
        p_alEffectf(efx_effect, AL_EAXREVERB_LFREFERENCE, r->flLFReference);
        p_alEffectf(efx_effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, r->flRoomRolloffFactor);
        p_alEffecti(efx_effect, AL_EAXREVERB_DECAY_HFLIMIT, r->iDecayHFLimit);
    } else {
        p_alEffectf(efx_effect, AL_REVERB_DENSITY, r->flDensity);
        p_alEffectf(efx_effect, AL_REVERB_DIFFUSION, r->flDiffusion);
        p_alEffectf(efx_effect, AL_REVERB_GAIN, r->flGain);
        p_alEffectf(efx_effect, AL_REVERB_GAINHF, r->flGainHF);
        p_alEffectf(efx_effect, AL_REVERB_DECAY_TIME, r->flDecayTime);
        p_alEffectf(efx_effect, AL_REVERB_DECAY_HFRATIO, r->flDecayHFRatio);
        p_alEffectf(efx_effect, AL_REVERB_REFLECTIONS_GAIN, r->flReflectionsGain);
        p_alEffectf(efx_effect, AL_REVERB_REFLECTIONS_DELAY, r->flReflectionsDelay);
        p_alEffectf(efx_effect, AL_REVERB_LATE_REVERB_GAIN, r->flLateReverbGain);
        p_alEffectf(efx_effect, AL_REVERB_LATE_REVERB_DELAY, r->flLateReverbDelay);
        p_alEffectf(efx_effect, AL_REVERB_AIR_ABSORPTION_GAINHF, r->flAirAbsorptionGainHF);
        p_alEffectf(efx_effect, AL_REVERB_ROOM_ROLLOFF_FACTOR, r->flRoomRolloffFactor);
        p_alEffecti(efx_effect, AL_REVERB_DECAY_HFLIMIT, r->iDecayHFLimit);
    }
    /* re-attaching the effect makes the slot pick up the new parameters */
    p_alAuxiliaryEffectSloti(efx_slot, AL_EFFECTSLOT_EFFECT, (ALint)efx_effect);
}

void EFX_Init(void *alcdevice)
{
    ALCdevice *dev = (ALCdevice *)alcdevice;
    efx_ok = false;
    level_props = eax_presets[0];
    current_props = eax_presets[0];
    if (!alcIsExtensionPresent(dev, "ALC_EXT_EFX")) {
        Snd_Printf("OMsnd: ALC_EXT_EFX not available, no environmental reverb\n");
        return;
    }
#define LOAD(var, type, name) var = (type)alGetProcAddress(name)
    LOAD(p_alGenEffects, LPALGENEFFECTS, "alGenEffects");
    LOAD(p_alDeleteEffects, LPALDELETEEFFECTS, "alDeleteEffects");
    LOAD(p_alEffecti, LPALEFFECTI, "alEffecti");
    LOAD(p_alEffectf, LPALEFFECTF, "alEffectf");
    LOAD(p_alEffectfv, LPALEFFECTFV, "alEffectfv");
    LOAD(p_alGenAuxiliaryEffectSlots, LPALGENAUXILIARYEFFECTSLOTS, "alGenAuxiliaryEffectSlots");
    LOAD(p_alDeleteAuxiliaryEffectSlots, LPALDELETEAUXILIARYEFFECTSLOTS, "alDeleteAuxiliaryEffectSlots");
    LOAD(p_alAuxiliaryEffectSloti, LPALAUXILIARYEFFECTSLOTI, "alAuxiliaryEffectSloti");
    LOAD(p_alAuxiliaryEffectSlotf, LPALAUXILIARYEFFECTSLOTF, "alAuxiliaryEffectSlotf");
    LOAD(p_alGenFilters, LPALGENFILTERS, "alGenFilters");
    LOAD(p_alDeleteFilters, LPALDELETEFILTERS, "alDeleteFilters");
    LOAD(p_alFilteri, LPALFILTERI, "alFilteri");
    LOAD(p_alFilterf, LPALFILTERF, "alFilterf");
#undef LOAD
    if (!p_alGenEffects || !p_alDeleteEffects || !p_alEffecti || !p_alEffectf || !p_alEffectfv ||
        !p_alGenAuxiliaryEffectSlots || !p_alDeleteAuxiliaryEffectSlots || !p_alAuxiliaryEffectSloti ||
        !p_alAuxiliaryEffectSlotf) {
        Snd_Printf("OMsnd: EFX entry points missing, no environmental reverb\n");
        return;
    }
    alGetError();
    p_alGenAuxiliaryEffectSlots(1, &efx_slot);
    p_alGenEffects(1, &efx_effect);
    if (alGetError() != AL_NO_ERROR) {
        Snd_Printf("OMsnd: cannot create an EFX effect slot\n");
        return;
    }
    p_alEffecti(efx_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    efx_eaxreverb = alGetError() == AL_NO_ERROR;
    if (!efx_eaxreverb) {
        p_alEffecti(efx_effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        if (alGetError() != AL_NO_ERROR) {
            Snd_Printf("OMsnd: no reverb effect available\n");
            p_alDeleteEffects(1, &efx_effect);
            p_alDeleteAuxiliaryEffectSlots(1, &efx_slot);
            return;
        }
    }
    p_alAuxiliaryEffectSlotf(efx_slot, AL_EFFECTSLOT_GAIN, 1.0f);
    /* underwater: muffled direct sound (high frequencies at 0.25, the level stays SoF's 0.3 gain) */
    uw_filter = 0;
    if (p_alGenFilters && p_alFilteri && p_alFilterf) {
        p_alGenFilters(1, &uw_filter);
        p_alFilteri(uw_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        p_alFilterf(uw_filter, AL_LOWPASS_GAIN, 1.0f);
        p_alFilterf(uw_filter, AL_LOWPASS_GAINHF, 0.25f);
        if (alGetError() != AL_NO_ERROR)
            uw_filter = 0;
    }
    /* SPEC-113 `lisa`: 0.0381 m per world unit */
    alListenerf(AL_METERS_PER_UNIT, 0.0381f);
    alGetError();
    efx_ok = true;
    apply(&level_props, "Generic (default)");
    Snd_Printf("OMsnd: EFX %s reverb ready\n", efx_eaxreverb ? "EAX" : "standard");
}

void EFX_Shutdown(void)
{
    if (!efx_ok)
        return;
    p_alAuxiliaryEffectSloti(efx_slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
    if (uw_filter && p_alDeleteFilters)
        p_alDeleteFilters(1, &uw_filter);
    uw_filter = 0;
    p_alDeleteEffects(1, &efx_effect);
    p_alDeleteAuxiliaryEffectSlots(1, &efx_slot);
    efx_ok = false;
}

unsigned EFX_Slot(void)
{
    if (!efx_ok || !om_reverb || !om_reverb->value)
        return 0;
    return efx_slot;
}

unsigned EFX_UnderwaterFilter(void)
{
    if (!efx_ok || !om_underwaterlp || !om_underwaterlp->value)
        return 0;
    return uw_filter;
}

static float mb_gain(int mb)
{
    return powf(10.0f, (float)mb / 2000.0f);
}

static int rd32(const byte *p)
{
    return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24));
}

static float rdf(const byte *p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

/* EAX 2.0 listener block (EAXLISTENERPROPERTIES, 56 bytes, SPEC-113 `denv`) -> EFX EAX reverb */
static void eax2_to_efx(const byte *p, EFXEAXREVERBPROPERTIES *r)
{
    float size = rdf(p + 40);
    float density = size * size * size / 16.0f;
    memset(r, 0, sizeof(*r));
    r->flDensity = density > 1.0f ? 1.0f : density < 0.0f ? 0.0f : density;
    r->flDiffusion = rdf(p + 44);
    r->flGain = mb_gain(rd32(p + 0));
    r->flGainHF = mb_gain(rd32(p + 4));
    r->flGainLF = 1.0f;
    r->flRoomRolloffFactor = rdf(p + 8);
    r->flDecayTime = rdf(p + 12);
    r->flDecayHFRatio = rdf(p + 16);
    r->flDecayLFRatio = 1.0f;
    r->flReflectionsGain = mb_gain(rd32(p + 20));
    r->flReflectionsDelay = rdf(p + 24);
    r->flLateReverbGain = mb_gain(rd32(p + 28));
    r->flLateReverbDelay = rdf(p + 32);
    r->flEchoTime = 0.25f;
    r->flEchoDepth = 0.0f;
    r->flModulationTime = 0.25f;
    r->flModulationDepth = 0.0f;
    r->flAirAbsorptionGainHF = powf(10.0f, rdf(p + 48) / 2000.0f);
    r->flHFReference = 5000.0f;
    r->flLFReference = 250.0f;
    r->iDecayHFLimit = (rd32(p + 52) & 0x20) ? 1 : 0;
}

/* `denv` chunk of a RIFF form "eal " (SPEC-113); returns the 56-byte block or NULL */
static const byte *eal_denv(const byte *buf, int len)
{
    const byte *p = buf + 12, *end = buf + len;
    if (len < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "eal ", 4))
        return NULL;
    while (p + 8 <= end) {
        int sz = rd32(p + 4);
        if (sz < 0 || p + 8 + sz > end)
            return NULL;
        if (!memcmp(p, "denv", 4) && sz >= 56)
            return p + 8;
        p += 8 + ((sz + 1) & ~1);
    }
    return NULL;
}

void EFX_SetMapEnvironment(const char *mapmodel)
{
    char eal[260];
    const char *base, *dot;
    void *buf;
    int len;
    size_t i;

    level_props = eax_presets[0];
    _snprintf_s(level_desc, sizeof(level_desc), _TRUNCATE, "Generic (default)");
    if (mapmodel && mapmodel[0]) {
        dot = strrchr(mapmodel, '.');
        base = strrchr(mapmodel, '/');
        base = base ? base + 1 : mapmodel;
        _snprintf_s(eal, sizeof(eal), _TRUNCATE, "%.*s.eal",
                    (int)(dot && dot > base ? (size_t)(dot - mapmodel) : strlen(mapmodel)), mapmodel);
        len = Snd_LoadFile(eal, &buf);
        if (len > 0 && buf) {
            const byte *denv = eal_denv((const byte *)buf, len);
            if (denv) {
                int envindex = rd32(denv + 36);
                eax2_to_efx(denv, &level_props);
                _snprintf_s(level_desc, sizeof(level_desc), _TRUNCATE, "%s (EAX environment %d %s)", eal,
                            envindex, envindex >= 0 && envindex < EAX_ENVIRONMENT_COUNT ? eax_names[envindex] : "?");
            }
            Snd_FreeFile(buf);
        }
        if (!strncmp(level_desc, "Generic (default)", 17)) {
            for (i = 0; i < sizeof(map_table) / sizeof(map_table[0]); i++)
                if (!_stricmp(base, map_table[i].map)) {
                    level_props = eax_presets[map_table[i].env];
                    _snprintf_s(level_desc, sizeof(level_desc), _TRUNCATE, "map table: EAX environment %d %s",
                                map_table[i].env, eax_names[map_table[i].env]);
                }
        }
    }
    if (proctype != 1)
        apply(&level_props, level_desc);
    Snd_Printf("OMsnd: environment %s\n", level_desc);
}

void EFX_SetEnvironment(int env)
{
    if (env < 0 || env >= EAX_ENVIRONMENT_COUNT) {
        Snd_Printf("EAX environment %d out of range 0..%d\n", env, EAX_ENVIRONMENT_COUNT - 1);
        return;
    }
    level_props = eax_presets[env];
    _snprintf_s(level_desc, sizeof(level_desc), _TRUNCATE, "EAX environment %d %s", env, eax_names[env]);
    if (proctype != 1)
        apply(&level_props, level_desc);
}

void EFX_SetProcType(int type)
{
    proctype = type;
    if (type == 1)
        apply(&eax_presets[EAX_ENVIRONMENT_UNDERWATER], "EAX environment 22 Underwater (processing type 1)");
    else
        apply(&level_props, level_desc);
}

void EFX_Info(void)
{
    const EFXEAXREVERBPROPERTIES *r = &current_props;
    Snd_Printf("reverb: %s%s\n", efx_ok ? "" : "(unavailable) ", current_desc);
    Snd_Printf("  gain %.3f hf %.3f decay %.2fs hfratio %.2f refl %.3f/%.3fs late %.3f/%.3fs density %.3f\n",
               r->flGain, r->flGainHF, r->flDecayTime, r->flDecayHFRatio, r->flReflectionsGain,
               r->flReflectionsDelay, r->flLateReverbGain, r->flLateReverbDelay, r->flDensity);
}
