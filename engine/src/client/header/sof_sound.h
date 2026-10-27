/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1a (sound module host). Client-internal.
 *
 * The engine side of the Soldier of Fortune 1.07f sound module interface:
 * loads the OMsnd module (SND2: the only one; snd_dll is ignored), keeps
 * the five-pointer sound struct up to date and routes
 * the client's sound calls to the module.
 *
 * Implements: SPEC-004, SPEC-AMEND-001, SPEC-301 (Behaviour 6)
 */

#ifndef CL_SOF_SOUND_H
#define CL_SOF_SOUND_H

#define SNDH_CS_AMBIENTSET 12
#define SNDH_CS_MUSICSET 13
#define SNDH_CS_SOUNDS 291
#define SNDH_MAX_SOUNDS 356
#define SNDH_MAX_CONFIGSTRINGS 3555
/* OpenMerc sound extension (game_api.h OM_MAX_SOUNDS): indexes 356..511 are configstrings 3555..3710, and their
   precache pointers follow the 1.07f client block in the sound struct (cl + SNDH_CL_EXT, OMsnd CL_SoundPrecache) */
#define SNDH_ALL_SOUNDS 512
#define SNDH_CS_SOUNDS_EXT 3555
#define SNDH_ALL_CONFIGSTRINGS (SNDH_CS_SOUNDS_EXT + SNDH_ALL_SOUNDS - SNDH_MAX_SOUNDS)
#define SNDH_CS_SOUND(i) (((i) < SNDH_MAX_SOUNDS) ? (SNDH_CS_SOUNDS + (i)) : (SNDH_CS_SOUNDS_EXT + (i) - SNDH_MAX_SOUNDS))
#define SNDH_CL_EXT 0x3C000

/* level registration from the SoF configstrings (SPEC-004 "Engine call
   sites"): sounds, ambient set, music set */
void SNDH_RegisterLevel(void);
/* a SoF configstring changed (index in SoF numbering); cleared = removed */
void SNDH_ConfigStringChanged(int index, const char *value, qboolean cleared);
/* svc_sound (10), parsed from net_message */
void SNDH_ParseSound(void);
/* svc_sound_info (4): the flags byte was read, the values follow */
void SNDH_ParseSoundInfo(int flags);
/* per-frame player-state values: general ambient set, music mode */
void SNDH_SetPlayerAudio(int ambientset, int musicmode);
/* the client is shutting down (sound struct #3 +0x24) */
void SNDH_SetShuttingDown(qboolean down);
/* the loaded module's name, "" when none */
const char *SNDH_ModuleName(void);

#endif
