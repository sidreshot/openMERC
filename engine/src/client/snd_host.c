/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Sound module host. Soldier of Fortune keeps the whole client sound
 * system in a library with the SPEC-004 interface; this file loads it (the
 * 20 mandatory and 3 optional exports), hands it the five-pointer sound
 * struct with the client data in the layout the modules read (SPEC-004
 * "The sound struct"), and implements the client's S_* calls on top of it.
 *
 * OpenMerc SND2: the module is always OMsnd.dll (modules/sound, OpenAL
 * Soft). The original selection (snd_dll, the A3D/EAX DirectSound probes,
 * the Defsnd/A3Dsnd/EAXSnd fallback chain) is removed: those libraries
 * mix through DirectSound 3D, which current Windows emulates in software,
 * and Defsnd is what a retail config.cfg (snd_dll "defsnd.dll") silently
 * selected. snd_dll is still registered so old configs and scripts parse,
 * but it is ignored. Yamagi's own sound system (OpenAL/SDL, OGG music) is
 * removed with it.
 *
 * Implements: SPEC-004 (selection and loading, exports, sound struct,
 *             engine call sites), SPEC-AMEND-001, SPEC-301 (Behaviour 6)
 */

#include "header/client.h"
#include "header/sof_sound.h"
#include "input/header/input.h" /* IN_Update */

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
#define SNDH_MODULE "OMsnd.dll"
#else
#define SNDH_MODULE "OMsnd.so"
#endif

/* ---------------------------------------------------------------------
 * module interface (SPEC-004 "Exports")
 * --------------------------------------------------------------------- */

typedef struct sndexport_s
{
	void (*Init)(void);
	void (*Shutdown)(void);
	void (*Touch)(void);
	void (*FreeSound)(void *sfx);
	void (*StartSound)(const float *origin, int entnum, int entchannel,
			void *sfx, float volume, float attenuation, float timeofs);
	void (*StartLocalSound)(const char *name);
	void (*StopAllSounds)(void);
	void (*Update)(const float *origin, const float *forward,
			const float *right, const float *up);
	void (*Activate)(int active);
	void (*BeginRegistration)(void);
	void *(*RegisterSound)(const char *name);
	void (*EndRegistration)(void);
	void *(*FindName)(const char *name, int create);
	void (*RegisterMusicSet)(const char *name);
	void (*RegisterAmbientSet)(const char *path);
	void (*SetMusicIntensity)(unsigned char value);
	void (*SetMusicDesignerInfo)(unsigned char value);
	void (*SetGeneralAmbientSet)(unsigned char index);
	void (*SetSoundProcType)(unsigned char type);
	void (*SetSoundStruct)(void *const *ptrs);
	/* optional */
	void (*A3D_ExportRenderGeom)(void *refexport);
	void (*EAX_SetEnvironment)(int environment);
	void (*EAX_SetEnvironmentLevel)(void);
} sndexport_t;

static const char *snd_mandatory[20] = {
	"S_Init", "S_Shutdown", "S_Touch", "S_FreeSound", "S_StartSound",
	"S_StartLocalSound", "S_StopAllSounds", "S_Update", "S_Activate",
	"S_BeginRegistration", "S_RegisterSound", "S_EndRegistration",
	"S_FindName", "S_RegisterMusicSet", "S_RegisterAmbientSet",
	"S_SetMusicIntensity", "S_SetMusicDesignerInfo", "S_SetGeneralAmbientSet",
	"S_SetSoundProcType", "S_SetSoundStruct"
};

static sndexport_t snd;
static void *snd_library;
static char snd_libname[MAX_QPATH];
static cvar_t *snd_dll;

/* ---------------------------------------------------------------------
 * the sound struct (SPEC-004): client data in the original layout
 * --------------------------------------------------------------------- */

#define SS_ENTSTATE_SIZE 116
#define SS_CLENTITY_SIZE 396
#define SS_CL_SIZE (SNDH_CL_EXT + sizeof(void *) * (SNDH_ALL_SOUNDS - SNDH_MAX_SOUNDS)) /* 1.07f block + extension of precache pointers */

static byte ss_entring[MAX_PARSE_ENTITIES * SS_ENTSTATE_SIZE];
static byte *ss_cl;               /* client state, SS_CL_SIZE bytes */
static byte ss_entities[1024 * SS_CLENTITY_SIZE];
static byte ss_cls[64];
static void *ss_hwnd;             /* HWND */
static void *ss_ptrs[5];

static int ss_ambientset, ss_musicmode;

#define SS_INT(base, ofs) (*(int *)((base) + (ofs)))

static void
SNDH_FindWindow(void)
{
#ifdef _WIN32
	/* the top-level window of this process */
	HWND w = NULL;
	DWORD pid = GetCurrentProcessId();

	while ((w = FindWindowExA(NULL, w, NULL, NULL)) != NULL)
	{
		DWORD wpid = 0;

		GetWindowThreadProcessId(w, &wpid);

		if ((wpid == pid) && IsWindowVisible(w))
		{
			break;
		}
	}

	ss_hwnd = w;
#endif
}

static void
SNDH_InitStruct(void)
{
	if (!ss_cl)
	{
		ss_cl = Z_Malloc(SS_CL_SIZE);
	}

	ss_ptrs[0] = ss_entring;
	ss_ptrs[1] = ss_cl;
	ss_ptrs[2] = ss_entities;
	ss_ptrs[3] = ss_cls;
	ss_ptrs[4] = &ss_hwnd;
}

static void
SNDH_CopyConfigStrings(void)
{
	int i;

	for (i = 0; i < SNDH_MAX_CONFIGSTRINGS; i++)
	{
		Q_strlcpy((char *)ss_cl + 0x1F44 + 64 * i, CL_SoF_ConfigString(i), 64);
	}
}

/* client connection state as the modules read it (8 = active) */
static int
SNDH_ConnState(void)
{
	switch (cls.state)
	{
		case ca_active:
			return 8;
		case ca_connected:
			return 4;
		case ca_connecting:
			return 3;
		case ca_disconnected:
			return 1;
		default:
			return 0;
	}
}

/* fills the frame-dependent fields before every module update */
static void
SNDH_UpdateStruct(void)
{
	int i, first, num;

	if (!ss_cl)
	{
		return;
	}

	SS_INT(ss_cls, 0) = SNDH_ConnState();

	if (CL_SoF_Active())
	{
		CL_SoF_FrameAudio(&ss_ambientset, &ss_musicmode);
	}

	ss_cl[0x0D] = (cl.sound_prepped && CL_SoF_Active()) ? 1 : 0;
	ss_cl[0x9A8] = (byte)ss_ambientset;
	ss_cl[0x9AA] = (byte)ss_musicmode;

	first = cl.frame.parse_entities;
	num = cl.frame.num_entities;

	if (!cl.frame.valid)
	{
		num = 0;
	}

	SS_INT(ss_cl, 0x9D4) = num;
	SS_INT(ss_cl, 0x9D8) = first & (MAX_PARSE_ENTITIES - 1);
	SS_INT(ss_cl, 0x1A3C) = cl.time;
	SS_INT(ss_cl, 0x1F3C) = cl.playernum;

	for (i = 0; i < num; i++)
	{
		int idx = (first + i) & (MAX_PARSE_ENTITIES - 1);
		const entity_state_t *s = &cl_parse_entities[idx];
		byte *e = ss_entring + idx * SS_ENTSTATE_SIZE;

		SS_INT(e, 0) = s->number;
		memcpy(e + 4, s->origin, sizeof(vec3_t));
		SS_INT(e, 0x30) = (int)s->effects;
		SS_INT(e, 0x3C) = s->sound;
		SS_INT(e, 0x40) = s->modelindex4 & 0xff; /* SoF loop parameters */

		if ((s->number >= 0) && (s->number < 1024))
		{
			SS_INT(ss_entities + s->number * SS_CLENTITY_SIZE, 0x74) =
				cl_entities[s->number].current.number;
		}
	}
}

/* exported by the executable: current sound origin of an entity */
void
CL_GetEntitySoundOrigin(int entnum, vec3_t out)
{
	centity_t *ent;
	int i;

	if ((entnum < 0) || (entnum >= MAX_EDICTS))
	{
		VectorClear(out);
		return;
	}

	ent = &cl_entities[entnum];

	for (i = 0; i < 3; i++)
	{
		out[i] = ent->prev.origin[i] +
			cl.lerpfrac * (ent->current.origin[i] - ent->prev.origin[i]);
	}
}

/* ---------------------------------------------------------------------
 * loading (SPEC-004 "Selecting and loading the library"; SND2: OMsnd only)
 * --------------------------------------------------------------------- */

static void *
SNDH_LoadLibrary(const char *name)
{
#ifdef _WIN32
	return (void *)LoadLibraryA(name);
#else
	/* next to the executable (as ref_*.so), then the current directory */
	char path[MAX_OSPATH];
	void *h = NULL;

	Com_sprintf(path, sizeof(path), "%s%s", Sys_GetBinaryDir(), name);

	if (Sys_IsFile(path))
	{
		Sys_LoadLibrary(path, NULL, &h);
	}

	if (!h)
	{
		Com_sprintf(path, sizeof(path), "./%s", name);
		Sys_LoadLibrary(path, NULL, &h);
	}

	return h;
#endif
}

static void *
SNDH_Proc(const char *name)
{
#ifdef _WIN32
	return (void *)GetProcAddress((HMODULE)snd_library, name);
#else
	return Sys_GetProcAddress(snd_library, name);
#endif
}

static void
SNDH_Unload(void)
{
	if (!snd_library)
	{
		return;
	}

	if (snd.Shutdown)
	{
		snd.Shutdown();
	}

#ifdef _WIN32
	if (!FreeLibrary((HMODULE)snd_library))
	{
		snd_library = NULL;
		memset(&snd, 0, sizeof(snd));
		Com_Error(ERR_FATAL, "Sound Lib FreeLibrary failed");
	}
#else
	Sys_FreeLibrary(snd_library);
#endif

	snd_library = NULL;
	snd_libname[0] = 0;
	memset(&snd, 0, sizeof(snd));
}

/* loads OMsnd; fatal errors as specified */
static void
SNDH_Load(void)
{
	void **slot;
	int i;

	SNDH_Unload();
	SNDH_FindWindow();

	if (snd_dll->string[0] && Q_stricmp(snd_dll->string, SNDH_MODULE) &&
		Q_stricmp(snd_dll->string, "OMsnd"))
	{
		Com_Printf("snd_dll \"%s\" ignored: OpenMerc always uses %s\n",
				snd_dll->string, SNDH_MODULE);
	}

	Com_Printf("------- Loading %s -------\n", SNDH_MODULE);

	snd_library = SNDH_LoadLibrary(SNDH_MODULE);

	if (!snd_library)
	{
		Com_Error(ERR_FATAL, "LoadLibrary(\"%s\") failed", SNDH_MODULE);
	}

	Q_strlcpy(snd_libname, SNDH_MODULE, sizeof(snd_libname));

	slot = (void **)&snd;

	for (i = 0; i < 20; i++)
	{
		slot[i] = SNDH_Proc(snd_mandatory[i]);

		if (!slot[i])
		{
			char name[MAX_QPATH];

			Q_strlcpy(name, snd_libname, sizeof(name));
#ifdef _WIN32
			FreeLibrary((HMODULE)snd_library);
#endif
			snd_library = NULL;
			memset(&snd, 0, sizeof(snd));
			Com_Error(ERR_FATAL, "GetProcAddress %s failed on %s", snd_mandatory[i], name);
		}
	}

	SNDH_InitStruct();
	snd.SetSoundStruct(ss_ptrs);

	snd.A3D_ExportRenderGeom = SNDH_Proc("S_A3D_ExportRenderGeom");
	snd.EAX_SetEnvironment = SNDH_Proc("SNDEAX_SetEnvironment");
	snd.EAX_SetEnvironmentLevel = SNDH_Proc("SNDEAX_SetEnvironmentLevel");

	/* S_A3D_ExportRenderGeom needs the SoF renderer export table
	   (SPEC-002); the OpenMerc renderer has none yet, so it is not
	   called (only A3Dsnd exports it, and A3D is never selected) */
}

const char *
SNDH_ModuleName(void)
{
	return snd_libname;
}

/* ---------------------------------------------------------------------
 * the client's sound calls
 * --------------------------------------------------------------------- */

void
S_Init(void)
{
	/* registered so retail configs and command lines parse; ignored */
	snd_dll = Cvar_Get("snd_dll", "", CVAR_ARCHIVE);
	Cvar_Get("s_volume", "0.7", CVAR_ARCHIVE);

	SNDH_InitStruct();
	SNDH_CopyConfigStrings();
	SNDH_Load();

	snd.Init();
}

void
S_Shutdown(void)
{
	SNDH_Unload();
}

void
S_Activate(qboolean active)
{
	if (snd.Activate)
	{
		SNDH_FindWindow();
		snd.Activate(active ? 1 : 0);
	}
}

void
S_StartSound(vec3_t origin, int entnum, int entchannel, struct sfx_s *sfx,
		float fvol, float attenuation, float timeofs)
{
	if (snd.StartSound && sfx)
	{
		snd.StartSound(origin, entnum, entchannel, sfx, fvol, attenuation, timeofs);
	}
}

void
S_StartLocalSound(const char *sound)
{
	if (snd.StartLocalSound)
	{
		snd.StartLocalSound(sound);
	}
}

void
S_RawSamples(int samples, int rate, int width, int channels, const byte *data,
		float volume)
{
	/* SoF has no streamed cinematics audio */
}

void
S_StopAllSounds(void)
{
	if (snd.StopAllSounds)
	{
		snd.StopAllSounds();
	}
}

void
S_Update(vec3_t origin, vec3_t forward, vec3_t right, vec3_t up)
{
	if (!snd.Update)
	{
		return;
	}

	SNDH_UpdateStruct();
	snd.Update(origin, forward, right, up);
}

void
S_BeginRegistration(void)
{
	if (snd.BeginRegistration)
	{
		snd.BeginRegistration();
	}
}

struct sfx_s *
S_RegisterSound(const char *name)
{
	if (!snd.RegisterSound || !name || !name[0])
	{
		return NULL;
	}

	return (struct sfx_s *)snd.RegisterSound(name);
}

void
S_EndRegistration(void)
{
	if (snd.EndRegistration)
	{
		snd.EndRegistration();
	}
}

/* ---------------------------------------------------------------------
 * SoF protocol hooks (cl_sof.c)
 * --------------------------------------------------------------------- */

/* the precache slot of sound index i (0..SNDH_ALL_SOUNDS-1): 1.07f table at cl+0x3A004, extension at cl+SNDH_CL_EXT */
static void **
SNDH_PrecacheSlot(int i)
{
	if (i < SNDH_MAX_SOUNDS)
	{
		return (void **)(ss_cl + 0x3A004) + i;
	}

	return (void **)(ss_cl + SNDH_CL_EXT) + (i - SNDH_MAX_SOUNDS);
}

/* the sound index of SoF configstring index, -1 when it is not a sound */
static int
SNDH_SoundOfConfigString(int index)
{
	if ((index > SNDH_CS_SOUNDS) && (index < SNDH_CS_SOUNDS + SNDH_MAX_SOUNDS))
	{
		return index - SNDH_CS_SOUNDS;
	}

	if ((index >= SNDH_CS_SOUNDS_EXT) && (index < SNDH_ALL_CONFIGSTRINGS))
	{
		return index - SNDH_CS_SOUNDS_EXT + SNDH_MAX_SOUNDS;
	}

	return -1;
}

void
SNDH_RegisterLevel(void)
{
	void **pre;
	int i;

	if (!snd.BeginRegistration || !ss_cl)
	{
		return;
	}

	SNDH_CopyConfigStrings();

	snd.BeginRegistration();

	for (i = 0; i < SNDH_ALL_SOUNDS; i++)
	{
		const char *name = CL_SoF_ConfigString(SNDH_CS_SOUND(i));

		pre = SNDH_PrecacheSlot(i);
		*pre = name[0] ? snd.RegisterSound(name) : NULL;
		IN_Update();
	}

	/* the value of configstring 12 is a full game path */
	snd.RegisterAmbientSet(CL_SoF_ConfigString(SNDH_CS_AMBIENTSET));
	snd.RegisterMusicSet(CL_SoF_ConfigString(SNDH_CS_MUSICSET));

	snd.EndRegistration();
}

void
SNDH_ConfigStringChanged(int index, const char *value, qboolean cleared)
{
	int n;

	if (!ss_cl || (index < 0) || (index >= SNDH_ALL_CONFIGSTRINGS))
	{
		return;
	}

	/* the module reads the 1.07f configstrings from the struct; the extension only through the precache */
	if (index < SNDH_MAX_CONFIGSTRINGS)
	{
		Q_strlcpy((char *)ss_cl + 0x1F44 + 64 * index, value ? value : "", 64);
	}

	n = SNDH_SoundOfConfigString(index);

	if ((n > 0) && snd.RegisterSound && cl.sound_prepped)
	{
		void **pre = SNDH_PrecacheSlot(n);

		if (cleared)
		{
			if (*pre)
			{
				snd.FreeSound(*pre);
			}

			*pre = NULL;
		}
		else if (value && value[0])
		{
			*pre = snd.RegisterSound(value);
		}
	}
}

/* svc_sound (SPEC-004 "Engine call sites") */
void
SNDH_ParseSound(void)
{
	int flags, index, ent, channel;
	float volume, attenuation, ofs;
	vec3_t pos;
	float *origin = NULL;

	flags = MSG_ReadByte(&net_message);
	index = MSG_ReadByte(&net_message);

	if (flags & 0x20)
	{
		index += 256;
	}

	volume = (flags & 0x01) ? MSG_ReadByte(&net_message) / 255.0f : 1.0f;
	attenuation = (flags & 0x02) ? MSG_ReadByte(&net_message) / 64.0f : 1.0f;
	ofs = (flags & 0x10) ? MSG_ReadByte(&net_message) * 0.001f : 0.0f;

	if (flags & 0x08)
	{
		int c = MSG_ReadShort(&net_message);

		ent = c >> 3;
		channel = c & 7;

		if (ent > 1024)
		{
			Com_Error(ERR_DROP, "CL_ParseStartSoundPacket: ent = %i", ent);
		}
	}
	else
	{
		ent = 0;
		channel = 0;
	}

	if (flags & 0x04)
	{
		MSG_ReadPos(&net_message, pos);
		origin = pos;
	}

	if (!ss_cl || (index < 0) || (index >= SNDH_ALL_SOUNDS) || !snd.StartSound)
	{
		Com_DPrintf("svc_sound %d dropped (no module or index out of range)\n", index);
		return;
	}

	/* SND2: a configstring that arrived between the level registration and the first frame (cl.sound_prepped,
	   SNDH_ConfigStringChanged) was never registered; register it now instead of dropping the sound */
	if (!*SNDH_PrecacheSlot(index) && CL_SoF_ConfigString(SNDH_CS_SOUND(index))[0])
	{
		*SNDH_PrecacheSlot(index) = snd.RegisterSound(CL_SoF_ConfigString(SNDH_CS_SOUND(index)));
	}

	if (!*SNDH_PrecacheSlot(index))
	{
		/* say so: a sound that is never registered is silent */
		Com_DPrintf("svc_sound %d \"%s\" dropped: not registered (ent %d ch %d)\n", index,
				CL_SoF_ConfigString(SNDH_CS_SOUND(index)), ent, channel);
		return;
	}

	snd.StartSound(origin, ent, channel, *SNDH_PrecacheSlot(index), volume,
			attenuation, ofs);
}

/* svc_sound_info: one byte per set flag (SPEC-004) */
void
SNDH_ParseSoundInfo(int flags)
{
	if (flags & ~0x0F)
	{
		Com_Error(ERR_DROP, "Invalid soundinfo type...");
		return;
	}

	if (flags & 0x01)
	{
		int v = MSG_ReadByte(&net_message);

		if (snd.SetMusicIntensity)
		{
			snd.SetMusicIntensity((unsigned char)v);
		}
	}

	if (flags & 0x02)
	{
		int v = MSG_ReadByte(&net_message);

		if (snd.SetMusicDesignerInfo)
		{
			snd.SetMusicDesignerInfo((unsigned char)v);
		}
	}

	if (flags & 0x04)
	{
		int v = MSG_ReadByte(&net_message);

		if (snd.SetGeneralAmbientSet)
		{
			snd.SetGeneralAmbientSet((unsigned char)v);
		}
	}

	if (flags & 0x08)
	{
		int v = MSG_ReadByte(&net_message);

		if (snd.SetSoundProcType)
		{
			snd.SetSoundProcType((unsigned char)v);
		}
	}
}

void
SNDH_SetPlayerAudio(int ambientset, int musicmode)
{
	ss_ambientset = ambientset;
	ss_musicmode = musicmode;
}

void
SNDH_SetShuttingDown(qboolean down)
{
	ss_cls[0x24] = down ? 1 : 0;
}
