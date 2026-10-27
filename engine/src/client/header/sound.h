/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * The client's sound calls. They are implemented by snd_host.c on top of
 * the OMsnd module (modules/sound, SPEC-004); the engine has no mixer of
 * its own (SND2: Yamagi's OpenAL/SDL/OGG sound system was removed).
 */

#ifndef CL_HEADER_SOUND_H
#define CL_HEADER_SOUND_H

struct sfx_s;

void S_Init(void);
void S_Shutdown(void);

/* the window gained or lost the focus; the module decides what that does
   (OMsnd: s_om_focusmute, default 0 = keep playing) */
void S_Activate(qboolean active);

/* if origin is NULL, the sound will be dynamically sourced from the entity */
void S_StartSound(vec3_t origin, int entnum, int entchannel,
		struct sfx_s *sfx, float fvol, float attenuation,
		float timeofs);

void S_StartLocalSound(const char *sound);
/* streamed cinematic audio: SoF has none, accepted and dropped */
void S_RawSamples(int samples, int rate, int width, int channels,
		const byte *data, float volume);
void S_StopAllSounds(void);
void S_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up);
void S_BeginRegistration(void);
struct sfx_s *S_RegisterSound(const char *name);
void S_EndRegistration(void);

/* exported by the executable to the module: an entity's sound origin */
void CL_GetEntitySoundOrigin(int entnum, vec3_t out);

#endif