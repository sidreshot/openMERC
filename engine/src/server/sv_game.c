/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Interface between the server and the game module.
 *
 * OpenMerc: the Soldier of Fortune 1.07f game module host. Loads
 * gamex86.dll (the original or the OpenMerc game), hands it the 99-entry
 * import table and drives its 28-field export table.
 *
 * Implements: SPEC-001, SPEC-003 (#67 context flag), SPEC-010 (#60-#66),
 *             SPEC-313 (#25, #26, #55)
 *
 * =======================================================================
 */

#include "header/server.h"
#include "../common/header/sof/engine.h"
#include <limits.h>

#ifdef _WIN32
#define SV_GAME_LIBRARY "gamex86.dll"
#else
#define SV_GAME_LIBRARY "gamex86.so"
#endif

sof_game_export_t *ge;
static void *game_library;
static sof_game_import_t gi;

cvar_t *sv_hostdebug;

void
SV_HostDebug(int bit, const char *fmt, ...)
{
	va_list argptr;
	char msg[1024];

	if (!sv_hostdebug || !((int)sv_hostdebug->value & bit))
	{
		return;
	}

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Com_Printf("%s", msg);
}

/* ---------------------------------------------------------------------
 * host services for the Ghoul layer (sv_ghoul.cpp)
 * --------------------------------------------------------------------- */

void
SV_GhoulHost_Print(const char *text)
{
	Com_Printf("%s", text);
}

void
SV_GhoulHost_Fatal(const char *text)
{
	Sys_Error("%s", text);
}

int
SV_GhoulHost_LoadFile(const char *path, void **data)
{
	return FS_LoadFile(path, data);
}

void
SV_GhoulHost_FreeFile(void *data)
{
	FS_FreeFile(data);
}

int
SV_GhoulHost_Debug(void)
{
	return sv_hostdebug ? (int)sv_hostdebug->value : 0;
}

float
SV_GhoulHost_Cvar(const char *name)
{
	return Cvar_VariableValue(name);
}

double
SV_GhoulHost_Milliseconds(void)
{
	return (double)Sys_Milliseconds();
}

/* ---------------------------------------------------------------------
 * helpers
 * --------------------------------------------------------------------- */

static client_t *
PF_ClientForEdict(const edict_t *ent)
{
	int n;

	if (!ent || !ge)
	{
		return NULL;
	}

	n = NUM_FOR_EDICT(ent);

	if ((n < 1) || (n > svs.numclients))
	{
		return NULL;
	}

	return svs.clients + (n - 1);
}

/* ---------------------------------------------------------------------
 * #0-#8 names, models
 * --------------------------------------------------------------------- */

static int
PF_ModelIndex(const char *name)
{
	return SV_ModelIndex(name);
}

static int
PF_SoundIndex(const char *name)
{
	return SV_SoundIndex(name);
}

static int
PF_EffectIndex(const char *name)
{
	return SV_EffectIndex(name);
}

static int
PF_ImageIndex(const char *name)
{
	return SV_ImageIndex(name);
}

static void
PF_SoundRemove(const char *name)
{
	SV_RemoveSound(name);
}

/* #5: "a.b.c.d:port" checked against the address filter (SPEC-313 s3) */
static int
PF_AddressBanned(const char *address)
{
	byte ip[4];
	int i, v;
	const char *p = address;

	if (!address)
	{
		return 0;
	}

	for (i = 0; i < 4; i++)
	{
		v = 0;

		if ((*p < '0') || (*p > '9'))
		{
			return 0; /* not an IPv4 address (loopback, IPX) */
		}

		while ((*p >= '0') && (*p <= '9'))
		{
			v = v * 10 + (*p - '0');
			p++;
		}

		ip[i] = (byte)v;

		if (i < 3)
		{
			if (*p != '.')
			{
				return 0;
			}

			p++;
		}
	}

	return SoF_FilterRefused(ip) ? 1 : 0;
}

static void
PF_GhoulRegisterAll(void)
{
	if (Cvar_VariableValue("deathmatch"))
	{
		SV_Ghoul_RegisterAllFiles();
	}
}

static void
PF_SetModel(edict_t *ent, const char *name)
{
	int i;

	if (!name)
	{
		Com_Error(ERR_DROP, "PF_setmodel: NULL");
	}

	i = SV_ModelIndex(name);
	ent->s.modelindex = i;

	/* inline models also set the size and link */
	if (name[0] == '*')
	{
		cmodel_t *mod = CM_InlineModel(name);

		VectorCopy(mod->mins, ent->mins);
		VectorCopy(mod->maxs, ent->maxs);

		/* SPEC-202 2.4: 0x400 inline brush model; 0x800 when a face is an alpha surface
		   (black-box, dm/jpndm2: *1, *2 with texinfo flag 0x800 -> 0xC00, *3..*6 -> 0x400;
		   set here, UNVERIFIED whether the original sets them at setmodel or per frame) */
		ent->s.renderfx |= 0x400;

		if (SV_InlineModelFaceFlags(atoi(name + 1)) & 0x800)
		{
			ent->s.renderfx |= 0x800;
		}

		SV_LinkEdict(ent);
	}
}

static void
PF_SetRenderModel(edict_t *ent, const char *name)
{
	if (!ent || !name)
	{
		return;
	}

	ent->s.modelindex2 = SV_ModelIndex(name);
}

/* ---------------------------------------------------------------------
 * #9-#21 command arguments, prints
 * --------------------------------------------------------------------- */

static int
PF_Argc(void)
{
	return Cmd_Argc();
}

static char *
PF_Argv(int n)
{
	return Cmd_Argv(n);
}

static char *
PF_Args(void)
{
	return Cmd_Args();
}

static void
PF_bprintf(int level, const char *fmt, ...)
{
	char msg[2048];
	va_list argptr;

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	SV_BroadcastPrintf(level, "%s", msg);
}

static void
PF_dprintf(const char *fmt, ...)
{
	char msg[1024];
	va_list argptr;

	/* SPEC-AMEND-005 G6b.6: import 13 prints (console and log) only while
	   developer is odd */
	if (!Com_DeveloperBit(1))
	{
		return;
	}

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Com_Printf("%s", msg);
}

static void
PF_cprintf(edict_t *ent, int level, const char *fmt, ...)
{
	char msg[1024];
	va_list argptr;
	client_t *cl = NULL;

	if (ent)
	{
		cl = PF_ClientForEdict(ent);

		if (!cl || (cl->state < cs_connected))
		{
			Com_Error(ERR_DROP, "cprintf to a non-client");
		}
	}

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	if (cl)
	{
		if (level >= cl->messagelevel)
		{
			SV_ClientPrintf(cl, level, "%s", msg);
		}
	}
	else
	{
		Com_Printf("%s", msg);
	}
}

/* OpenMerc: in single player both game DLLs turn every console word they do
   not know ("notarget" in the retail one, which lacks it) into "say <word>",
   a chat line to nobody. While such a command runs the chat is dropped and
   the player gets Quake II's "Unknown command" instead (KNOWN_ISSUES). */
static const char *sv_sp_unknown;
static qboolean sv_sp_unknown_told;

/* #15: chat line tagged with the sender's slot; SPEC-780: level 0 = say,
   1 = team (the svc_nameprint team flag) */
static void
PF_clprintf(edict_t *to, edict_t *from, int level, const char *fmt, ...)
{
	char msg[1024];
	va_list argptr;
	client_t *cl = NULL;
	int sender;

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	if (sv_sp_unknown)
	{
		if (!sv_sp_unknown_told)
		{
			sv_sp_unknown_told = true;
			Com_Printf("Unknown command \"%s\"\n", sv_sp_unknown);
		}

		return;
	}

	if (!to)
	{
		Com_Printf("%s", msg);
		return;
	}

	cl = PF_ClientForEdict(to);

	if (!cl || (cl->state < cs_connected))
	{
		Com_Error(ERR_DROP, "clprintf to a non-client");
	}

	if (SV_ClientIsBot(cl))
	{
		return;
	}

	sender = from ? NUM_FOR_EDICT(from) - 1 : 0;

	MSG_WriteByte(&cl->netchan.message, sofsvc_nameprint);
	MSG_WriteByte(&cl->netchan.message, sender & 0xFF);
	MSG_WriteByte(&cl->netchan.message, level & 0xFF);
	MSG_WriteString(&cl->netchan.message, msg);
}

static void
PF_WelcomePrint(edict_t *ent)
{
	client_t *cl = PF_ClientForEdict(ent);

	if (!cl || SV_ClientIsBot(cl))
	{
		return;
	}

	MSG_WriteByte(&cl->netchan.message, sofsvc_welcomeprint);
}

static void
PF_centerprintf(edict_t *ent, const char *fmt, ...)
{
	char msg[1024];
	va_list argptr;
	client_t *cl = PF_ClientForEdict(ent);

	if (!cl || SV_ClientIsBot(cl))
	{
		return;
	}

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	MSG_WriteByte(&sv.multicast, sofsvc_centerprint);
	MSG_WriteString(&sv.multicast, msg);
	SV_Unicast(ent, true);
}

static void
PF_cinprintf(edict_t *ent, int x, int y, int textspeed, const char *text)
{
	client_t *cl = NULL;

	if (ent)
	{
		cl = PF_ClientForEdict(ent);

		if (!cl || (cl->state < cs_connected))
		{
			Com_Error(ERR_DROP, "cinprintf to a non-client");
		}

		if (SV_ClientIsBot(cl))
		{
			return;
		}
	}

	MSG_WriteByte(&sv.multicast, sofsvc_cinprintf);
	MSG_WriteShort(&sv.multicast, x);
	MSG_WriteShort(&sv.multicast, y);
	MSG_WriteByte(&sv.multicast, textspeed);
	MSG_WriteString(&sv.multicast, text ? text : "");

	if (ent)
	{
		SV_Unicast(ent, true);
	}
	else
	{
		SV_Multicast(NULL, SOF_MULTICAST_ALL_R, 0);
	}
}

static void
PF_CaptionPrint(int level, int id)
{
	(void)level;
	MSG_WriteByte(&sv.multicast, sofsvc_captionprint);
	MSG_WriteShort(&sv.multicast, id);
	SV_Multicast(NULL, SOF_MULTICAST_ALL_R, 0);
}

static void
PF_CaptionPrintEnt(edict_t *ent, int id)
{
	client_t *cl = PF_ClientForEdict(ent);

	if (!cl || SV_ClientIsBot(cl))
	{
		return;
	}

	MSG_WriteByte(&sv.multicast, sofsvc_captionprint);
	MSG_WriteShort(&sv.multicast, id);
	SV_Unicast(ent, true);
}

static void
PF_ForceConNotify(void)
{
	MSG_WriteByte(&sv.multicast, sofsvc_force_con_notify);
	SV_Multicast(NULL, SOF_MULTICAST_ALL_R, 0);
}

/* ---------------------------------------------------------------------
 * #22-#30 sounds, surfaces, multicast
 * --------------------------------------------------------------------- */

static void
PF_StartSoundChecks(float volume, float attenuation, float timeofs)
{
	if ((volume < 0.0f) || (volume > 1.0f))
	{
		Com_Error(ERR_FATAL, "SV_StartSound: volume = %f", volume);
	}

	if ((attenuation < 0.0f) || (attenuation > 4.0f))
	{
		Com_Error(ERR_FATAL, "SV_StartSound: attenuation = %f", attenuation);
	}

	if ((timeofs < 0.0f) || (timeofs > 0.255f))
	{
		Com_Error(ERR_FATAL, "SV_StartSound: timeofs = %f", timeofs);
	}
}

static qboolean SV_JumpGateDrops(const edict_t *entity, int channel, int soundindex);

static void
PF_StartSound(edict_t *entity, int channel, int soundindex, float volume,
		float attenuation, float timeofs, int local)
{
	if (!entity)
	{
		return;
	}

	PF_StartSoundChecks(volume, attenuation, timeofs);

	if (SV_JumpGateDrops(entity, channel, soundindex))
	{
		return;
	}

	SV_StartSound(NULL, entity, channel, soundindex, volume, attenuation, timeofs, local);
}

static void
PF_PositionedSound(const float *origin, edict_t *entity, int channel, int soundindex,
		float volume, float attenuation, float timeofs, int local)
{
	PF_StartSoundChecks(volume, attenuation, timeofs);
	SV_StartSound(origin, entity, channel, soundindex, volume, attenuation, timeofs, local);
}

static int
PF_DamageTexture(sof_surface_t *surface, int damage)
{
	return SV_DamageTexture(surface, damage);
}

static int
PF_GetMaterials(uint8_t *buf, int capacity)
{
	return SV_MaterialList(buf, capacity);
}

static void
PF_LoadingProgress(float fraction, int flag)
{
	(void)flag;

	if (dedicated && dedicated->value)
	{
		return;
	}

	Cvar_SetValue("menu_loading", fraction);
}

static void
PF_Multicast(const float *origin, int to)
{
	SV_Multicast(origin, to, 0);
}

static void
PF_MulticastExclude(const float *origin, int to, int excluded)
{
	SV_Multicast(origin, to, excluded);
}

static void
PF_Unicast(edict_t *ent, int reliable)
{
	SV_Unicast(ent, reliable);
}

/* ---------------------------------------------------------------------
 * #31-#46 message writing
 * --------------------------------------------------------------------- */

static void PF_WriteByte(int c) { MSG_WriteByte(&sv.multicast, c & 0xFF); }
static void PF_WriteShort(int c) { MSG_WriteShort(&sv.multicast, c); }
static void PF_WriteLong(int c) { MSG_WriteLong(&sv.multicast, c); }
static void PF_WriteFloat(float f) { MSG_WriteFloat(&sv.multicast, f); }
static void PF_WriteString(const char *s) { MSG_WriteString(&sv.multicast, s ? s : ""); }
static void PF_WritePos(const float *pos) { SV_WritePos(&sv.multicast, pos); }
static void PF_WriteDir(const float *dir) { SV_WriteDir(&sv.multicast, dir); }
static void PF_WriteAngle(float f) { SV_WriteAngle(&sv.multicast, f); }

/* the module message buffer of SPEC-003 (not the engine's sizebuf_t) */
static byte *
PF_SZGetSpace(sof_sizebuf_t *sb, int length)
{
	byte *data;

	if (sb->cursize + length > sb->maxsize)
	{
		if (!sb->allowoverflow)
		{
			Com_Error(ERR_FATAL, "SZ_GetSpace: overflow without allowoverflow set");
		}

		if (length > sb->maxsize)
		{
			Com_Error(ERR_FATAL, "SZ_GetSpace: %i is > full buffer size", length);
		}

		Com_Printf("SZ_GetSpace: overflow\n");
		sb->cursize = 0;
		sb->overflowed = 1;
	}

	data = sb->data + sb->cursize;
	sb->cursize += length;

	return data;
}

static void
PF_SZ_Init(sof_sizebuf_t *sb, void *data, int size)
{
	memset(sb, 0, sizeof(*sb));
	sb->data = data;
	sb->maxsize = size;
}

static void
PF_SZ_Clear(sof_sizebuf_t *sb)
{
	sb->cursize = 0;
	sb->overflowed = 0;
}

static void
PF_SZ_Write(sof_sizebuf_t *sb, const void *data, int length)
{
	if (length > 0)
	{
		memcpy(PF_SZGetSpace(sb, length), data, length);
	}
}

static void
PF_SZ_WriteByte(sof_sizebuf_t *sb, int c)
{
	byte *b = PF_SZGetSpace(sb, 1);

	b[0] = c & 0xFF;
}

static void
PF_SZ_WriteShort(sof_sizebuf_t *sb, int c)
{
	byte *b = PF_SZGetSpace(sb, 2);

	b[0] = c & 0xFF;
	b[1] = (c >> 8) & 0xFF;
}

static void
PF_SZ_WriteLong(sof_sizebuf_t *sb, int c)
{
	byte *b = PF_SZGetSpace(sb, 4);

	b[0] = c & 0xFF;
	b[1] = (c >> 8) & 0xFF;
	b[2] = (c >> 16) & 0xFF;
	b[3] = (c >> 24) & 0xFF;
}

static client_t *
PF_ClientNum(int clientnum)
{
	if ((clientnum < 0) || (clientnum >= svs.numclients))
	{
		return NULL;
	}

	return svs.clients + clientnum;
}

static void
PF_ReliableWriteByteToClient(int c, int clientnum)
{
	client_t *cl = PF_ClientNum(clientnum);

	if (cl && (cl->state >= cs_connected))
	{
		MSG_WriteByte(&cl->netchan.message, c & 0xFF);
	}
}

static void
PF_ReliableWriteDataToClient(const void *data, int length, int clientnum)
{
	client_t *cl = PF_ClientNum(clientnum);

	if (cl && (cl->state >= cs_connected) && (length > 0))
	{
		SZ_Write(&cl->netchan.message, (void *)data, length);
	}
}

static int
PF_DirToByte(const float *dir)
{
	return SV_DirToByte(dir);
}

/* #46: svc_playernamecols, count byte, then the run data (SPEC-201 s33) */
static void
PF_SendPlayernameColors(edict_t *ent, int length, int count, const void *data)
{
	client_t *cl = PF_ClientForEdict(ent);

	if (!cl || (cl->state < cs_connected) || SV_ClientIsBot(cl))
	{
		return;
	}

	MSG_WriteByte(&cl->netchan.message, sofsvc_playernamecols);
	MSG_WriteByte(&cl->netchan.message, count & 0xFF);

	if (length > 0)
	{
		SZ_Write(&cl->netchan.message, (void *)data, length);
	}
}

/* ---------------------------------------------------------------------
 * #47-#51 string packages
 * --------------------------------------------------------------------- */

static void
PF_SP_Register(const char *package)
{
	SV_SP_Register(package);
}

static void
PF_SP_Print(edict_t *ent, int id, ...)
{
	va_list argptr;

	va_start(argptr, id);
	SV_SP_Print(ent, id, argptr, false);
	va_end(argptr);
}

static void
PF_SP_Print_Obit(edict_t *ent, int id, ...)
{
	va_list argptr;

	va_start(argptr, id);
	SV_SP_Print(ent, id, argptr, true);
	va_end(argptr);
}

static int
PF_SP_SPrint(char *buf, int capacity, int id, ...)
{
	va_list argptr;
	int n;

	va_start(argptr, id);
	n = SV_SP_Format(buf, capacity, id, argptr);
	va_end(argptr);

	return n;
}

static const char *
PF_SP_GetStringText(int id)
{
	return SV_SP_GetText(id);
}

/* ---------------------------------------------------------------------
 * #52-#59 world queries
 * --------------------------------------------------------------------- */

static int
PF_Trace(const float *start, const float *mins, const float *maxs, const float *end,
		edict_t *passent, int contentmask, sof_trace_t *out)
{
	SV_TraceSoF(start, mins, maxs, end, passent, contentmask, out);
	return out->fraction == 1.0f;
}

static int
PF_TraceFilter(const float *start, const float *mins, const float *maxs,
		const float *end, edict_t *passent, int contentmask, sof_trace_t *out,
		sof_tracefilter_t filter)
{
	SV_TraceFiltered(start, mins, maxs, end, passent, contentmask, out, filter);
	return out->fraction == 1.0f;
}

static int
PF_PointContents(const float *p)
{
	return SV_PointContents(p);
}

static float
PF_RegionDistance(const float *p)
{
	return SV_RegionDistance(p);
}

static int
PF_inPVS(const float *p1, const float *p2)
{
	return SV_InPVS(p1, p2);
}

static int
PF_inPHS(const float *p1, const float *p2)
{
	return SV_InPHS(p1, p2);
}

static void
PF_SetAreaPortalState(int portalnum, int open)
{
	CM_SetAreaPortalState(portalnum, open);
}

static int
PF_AreasConnected(int area1, int area2)
{
	return CM_AreasConnected(area1, area2);
}

/* ---------------------------------------------------------------------
 * #60-#68 Ghoul services, player context, configstrings
 * --------------------------------------------------------------------- */

static void *
PF_GetGhoul(void)
{
	void *sys = SV_Ghoul_System();

	SV_ImportTrace_HookVtable(0, sys); /* test aid (sv_watchdog) */
	return sys;
}

static void *
PF_NewPlayerModelInfo(const char *gpm)
{
	void *pm = SV_Ghoul_NewPlayerModel(gpm);

	SV_ImportTrace_HookVtable(3, pm); /* test aid (sv_watchdog) */
	return pm;
}

/* test aid (sv_watchdog): the Ghoul instance and object method tables are
   traced once the game has made an instance (entity field 0x164) */
void
SV_ImportTrace_ScanEdicts(void)
{
	static qboolean done;
	int i;

	if (done || !ge || (Cvar_VariableValue("sv_watchdog") <= 0))
	{
		return;
	}

	for (i = 0; i < ge->num_edicts; i++)
	{
		sof_edict_t *e = (sof_edict_t *)((byte *)ge->edicts + i * ge->edict_size);

		if (e->inuse && e->ghoulInst)
		{
			SV_Ghoul_HookTraceInstance(e->ghoulInst);
			done = true;
			return;
		}
	}
}

static int
PF_LoadGhoulFile(const char *folder, const char *file, void **buffer)
{
	return SV_Ghoul_LoadFile(folder, file, buffer);
}

static int
PF_ReadLine(int *remaining, char **cursor, char *line)
{
	return SV_Ghoul_GetLine(remaining, cursor, line);
}

static void
PF_PreloadGSQ(const char *folder, const char *name)
{
	SV_Ghoul_PreloadList(folder, name);
}

static int
PF_RegisterGSQSequences(const char *folder, const char *name, void *object)
{
	return SV_Ghoul_RegisterList(folder, name, object);
}

static void
PF_TurnOffPartsFromGSQ(const char *folder, const char *name, void *object, void *instance)
{
	SV_Ghoul_PartsOff(folder, name, object, instance);
}

static void
PF_Configstring(int index, const char *val)
{
	if ((index < 0) || (index >= OM_MAX_CONFIGSTRINGS))
	{
		Com_Error(ERR_DROP, "configstring: bad index %i", index);
	}

	SV_SetConfigstring(index, val ? val : "");
}

/* ---------------------------------------------------------------------
 * #72-#99 errors, player module, random, world links, memory, savegame,
 * cvars, files, commands
 * --------------------------------------------------------------------- */

YQ2_ATTR_NORETURN_FUNCPTR static void
PF_error(const char *fmt, ...)
{
	char msg[1024];
	va_list argptr;

	va_start(argptr, fmt);
	vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Com_Error(ERR_DROP, "Game Error: %s", msg);
}

static void
PF_DedicatedPrint(const char *text)
{
	if (text)
	{
		Com_Printf("%s", text);
	}
}

static void *
PF_LoadPlayerDLL(void *common, void *client, void *server, int isclient)
{
	return SV_LoadPlayerModule(common, client, server, isclient);
}

static void
PF_UnloadPlayerDLL(int isclient)
{
	SV_UnloadPlayerModule(isclient);
}

static float
PF_flrand(float min, float max)
{
	return flrand(min, max);
}

static int
PF_irand(int min, int max)
{
	return irand(min, max);
}

static void
PF_LinkEntity(edict_t *ent)
{
	SV_LinkEdict(ent);
}

static void
PF_UnlinkEntity(edict_t *ent)
{
	SV_UnlinkEdict(ent);
}

static int
PF_BoxEdicts(const float *mins, const float *maxs, edict_t **list, int maxcount,
		int areatype)
{
	return SV_AreaEdicts(mins, maxs, list, maxcount, areatype);
}

/* Jump-sound gate for game modules that play player/jump.wav on every
   ground->air transition while jump is held (mode A: stair/lip grazes during
   split moves re-fire it every server frame, userbugs2 stairs_hold). The game
   calls gi.sound right after gi.Pmove in ClientThink, so remember whether the
   last Pmove raised JUMP_HELD (a real jump impulse) and drop a client's
   CHAN_VOICE jump.wav when it did not. sv_om_jumpgate 0 restores the plain path. */
static cvar_t *sv_om_jumpgate;
static int sv_jumpgate_time = -1;
static int sv_jumpgate_flags;
static qboolean sv_jumpgate_edge;

static void
PF_Pmove(sof_pmove_t *pm)
{
	int pre = pm->s.pm_flags;

	SV_Pmove(pm);

	sv_jumpgate_time = (int)sv.time;
	sv_jumpgate_flags = pm->s.pm_flags;
	sv_jumpgate_edge = !(pre & PMF_JUMP_HELD) && (pm->s.pm_flags & PMF_JUMP_HELD);
}

static qboolean
SV_JumpGateDrops(const edict_t *entity, int channel, int soundindex)
{
	const char *name;
	int num;

	if (!sv_om_jumpgate)
	{
		sv_om_jumpgate = Cvar_Get("sv_om_jumpgate", "1", 0);
	}


	if (!sv_om_jumpgate->value || ((channel & 7) != CHAN_VOICE) ||
		(soundindex <= 0) || (soundindex >= OM_MAX_SOUNDS))
	{
		return false;
	}

	num = NUM_FOR_EDICT(entity);

	if ((num < 1) || (num > maxclients->value) || !entity->client)
	{
		return false;
	}

	name = sv.configstrings[OM_CS_SOUND(soundindex)];

	if (Q_stricmp(name, "player/jump.wav") != 0)
	{
		return false;
	}

	if (sv_om_jumpgate->value >= 3)
	{
		Com_Printf("sv_om_jumpgate: jump.wav ent %d time %d/%u flags 0x%02x/0x%02x edge %d\n",
				num, sv_jumpgate_time, sv.time, sv_jumpgate_flags, entity->client->ps.pmove.pm_flags, sv_jumpgate_edge);
	}

	/* only a sound issued in the same frame, straight after this client's
	   Pmove (its flags still match), is judged; anything else passes */
	if ((sv_jumpgate_time != (int)sv.time) ||
		(entity->client->ps.pmove.pm_flags != sv_jumpgate_flags))
	{
		return false;
	}

	if (sv_jumpgate_edge)
	{
		return false;
	}

	if (sv_om_jumpgate->value >= 2)
	{
		Com_Printf("sv_om_jumpgate: dropped jump.wav ent %d (no JUMP_HELD edge, flags 0x%02x)\n",
				num, sv_jumpgate_flags);
	}

	return true;
}

static void *
PF_TagMalloc(int size, int tag)
{
	if (size < 0)
	{
		Com_Error(ERR_FATAL, "TagMalloc: size < 0: %i", size);
	}

	if ((tag > USHRT_MAX) || (tag < 0))
	{
		tag = 0;
	}

	return Z_TagMalloc(size, (unsigned short)tag); /* zero filled */
}

static void
PF_TagFree(void *block)
{
	Z_Free(block);
}

static void
PF_FreeTags(int tag)
{
	if ((tag > USHRT_MAX) || (tag <= 0))
	{
		return;
	}

	Z_FreeTags((unsigned short)tag);
}

static int
PF_WriteChunk(int id, const void *data, int length)
{
	return SV_WriteChunk(id, data, length);
}

static int
PF_ReadChunk(int id, void *buffer, int length, void **allocated)
{
	return SV_ReadChunk(id, buffer, length, allocated);
}

static cvar_t *
PF_Cvar(const char *name, const char *value, int flags, sof_cvarcallback_t cb)
{
	return Cvar_GetModule(name, value, flags, (cvarcallback_t)cb);
}

static cvar_t *
PF_CvarSet(const char *name, const char *value)
{
	return Cvar_Set(name, value);
}

static void
PF_CvarSetValue(const char *name, float value)
{
	Cvar_SetValue(name, value);
}

static cvar_t *
PF_CvarForceSet(const char *name, const char *value)
{
	return Cvar_ForceSetUnprotected(name, value);
}

static char *
PF_CvarInfo(int mask)
{
	return Cvar_BitInfo(mask);
}

static float
PF_CvarValue(const char *name)
{
	return Cvar_VariableValue(name);
}

static int
PF_FS_LoadFile(const char *path, void **buffer, int flag)
{
	return FS_SoFLoadFile(path, buffer, flag);
}

static void
PF_FS_FreeFile(void *buffer)
{
	FS_FreeFile(buffer);
}

static const char *
PF_FS_Gamedir(void)
{
	return FS_Gamedir();
}

static void
PF_FS_CreatePath(const char *path)
{
	FS_CreatePath(path);
}

static int
PF_FS_FileExists(const char *path)
{
	return FS_FileExists(path) ? 1 : 0;
}

static void
PF_AddCommandString(const char *text)
{
	Cbuf_AddText(text);
}

/* ---------------------------------------------------------------------
 * loading (SPEC-001 "Loading and handshake")
 * --------------------------------------------------------------------- */

static void
SV_UnloadGameLibrary(void)
{
	if (game_library)
	{
		Sys_FreeLibrary(game_library);
		game_library = NULL;
	}
}

/* SPEC-301 s8 checksum of the game module file loaded last (SPEC-AMEND-005 GS.15) */
static unsigned sv_gamechecksum;

unsigned
SV_GameModuleChecksum(void)
{
	return game_library ? sv_gamechecksum : 0;
}

/* true when the loaded game module is OpenMerc's own (marker export, DEVIATIONS "pure server") */
qboolean
SV_GameModuleIsOpenMerc(void)
{
	return game_library && Sys_GetProcAddress(game_library, "OpenMerc_Module") != NULL;
}

static sof_getgameapi_t
SV_LoadGameLibrary(void)
{
	char name[MAX_OSPATH], cwd[MAX_OSPATH];
	const char *path = NULL;
	void *entry;

	if (game_library)
	{
		Com_Error(ERR_FATAL, "game module loaded twice (SV_UnloadGameLibrary missing)");
	}

	/* <current directory>/final, <current directory>, then the search paths */
	if (!Sys_Realpath(".", cwd, sizeof(cwd)))
	{
		cwd[0] = 0;
	}

	if (cwd[0])
	{
		Com_sprintf(name, sizeof(name), "%s/final/%s", cwd, SV_GAME_LIBRARY);

		if (Sys_IsFile(name) && (entry = Sys_LoadLibrary(name, "GetGameAPI", &game_library)))
		{
			Com_DPrintf("LoadLibrary (%s)\n", name);
			sv_gamechecksum = SV_FileChecksum(name);
			return (sof_getgameapi_t)entry;
		}

		Com_sprintf(name, sizeof(name), "%s/%s", cwd, SV_GAME_LIBRARY);

		if (Sys_IsFile(name) && (entry = Sys_LoadLibrary(name, "GetGameAPI", &game_library)))
		{
			Com_DPrintf("LoadLibrary (%s)\n", name);
			sv_gamechecksum = SV_FileChecksum(name);
			return (sof_getgameapi_t)entry;
		}
	}

	while ((path = FS_NextPath(path)) != NULL)
	{
		Com_sprintf(name, sizeof(name), "%s/%s", path, SV_GAME_LIBRARY);

		if (!Sys_IsFile(name))
		{
			continue;
		}

		entry = Sys_LoadLibrary(name, "GetGameAPI", &game_library);

		if (entry)
		{
			Com_DPrintf("LoadLibrary (%s)\n", name);
			sv_gamechecksum = SV_FileChecksum(name);
			return (sof_getgameapi_t)entry;
		}

		/* found but without GetGameAPI: Sys_LoadLibrary unloaded it */
		Com_Error(ERR_DROP, "failed to load game DLL");
	}

	return NULL;
}

/*
 * Called when either the entire server is being killed, or
 * it is changing to a different game directory.
 */
void
SV_ShutdownGameProgs(void)
{
	SV_ShutdownGameAndGhoul(false, false);
}

/*
 * Server shutdown with the Ghoul server system (SPEC-010 "Lifetimes and resets"):
 * network clients dropped (method 15), the game's Shutdown (the game destroys its
 * own objects, method 6), then the reset (method 8, not when only reconnecting)
 * while the game module, whose callback objects instances may still reference,
 * is loaded; the modules are unloaded last. Resetting before the game's
 * Shutdown left the game destroying freed instances (hang or fail-fast at
 * killserver/quit, 2026-10-25). The relative order in the original is
 * UNVERIFIED (OPEN_QUESTIONS I1b).
 */
void
SV_ShutdownGameAndGhoul(qboolean ghoul, qboolean reconnecting)
{
	SV_Watchdog_Beat("game shutdown");

	if (ghoul)
	{
		SV_Ghoul_ServerDropAll();
	}

	if (ge)
	{
		SV_Watchdog_Mark("game Shutdown");
		ge->Shutdown();
		ge = NULL;
	}

	if (ghoul && !reconnecting)
	{
		SV_Watchdog_Mark("Ghoul server reset");
		SV_Ghoul_ServerReset();
	}

	SV_Watchdog_Mark("game module unload");
	SV_UnloadGameLibrary();
	SV_Watchdog_Mark("player module shutdown");
	SV_ShutdownPlayerModule();
	SV_Watchdog_Mark("after game shutdown");
}

void
SV_InitGameProgs(void)
{
	sof_getgameapi_t GetGameAPI;
	sof_game_import_t import;

	/* unload anything we have now */
	if (ge)
	{
		SV_ShutdownGameProgs();
	}

	Com_Printf("-------- game initialization -------\n");

	memset(&import, 0, sizeof(import));

	import.modelindex = PF_ModelIndex;
	import.soundindex = PF_SoundIndex;
	import.effectindex = PF_EffectIndex;
	import.imageindex = PF_ImageIndex;
	import.soundremove = PF_SoundRemove;
	import.addressbanned = PF_AddressBanned;
	import.ghoulregisterall = PF_GhoulRegisterAll;
	import.setmodel = PF_SetModel;
	import.setrendermodel = PF_SetRenderModel;
	import.argc = PF_Argc;
	import.argv = PF_Argv;
	import.args = PF_Args;
	import.bprintf = PF_bprintf;
	import.dprintf = PF_dprintf;
	import.cprintf = PF_cprintf;
	import.clprintf = PF_clprintf;
	import.welcomeprint = PF_WelcomePrint;
	import.centerprintf = PF_centerprintf;
	import.cinprintf = PF_cinprintf;
	import.captionprint = PF_CaptionPrint;
	import.captionprintent = PF_CaptionPrintEnt;
	import.forceconnotify = PF_ForceConNotify;
	import.sound = PF_StartSound;
	import.positioned_sound = PF_PositionedSound;
	import.unused24 = NULL;
	import.damagetexture = PF_DamageTexture;
	import.getmaterials = PF_GetMaterials;
	import.loadingprogress = PF_LoadingProgress;
	import.multicast = PF_Multicast;
	import.multicastexclude = PF_MulticastExclude;
	import.unicast = PF_Unicast;
	import.WriteChar = PF_WriteByte; /* identical to WriteByte in the original */
	import.WriteByte = PF_WriteByte;
	import.WriteShort = PF_WriteShort;
	import.WriteLong = PF_WriteLong;
	import.WriteFloat = PF_WriteFloat;
	import.WriteString = PF_WriteString;
	import.WritePosition = PF_WritePos;
	import.WriteDir = PF_WriteDir;
	import.WriteAngle = PF_WriteAngle;
	import.SZ_WriteByte = PF_SZ_WriteByte;
	import.SZ_WriteShort = PF_SZ_WriteShort;
	import.SZ_WriteLong = PF_SZ_WriteLong;
	import.ReliableWriteByteToClient = PF_ReliableWriteByteToClient;
	import.ReliableWriteDataToClient = PF_ReliableWriteDataToClient;
	import.DirToByte = PF_DirToByte;
	import.SendPlayernameColors = PF_SendPlayernameColors;
	import.SP_Register = PF_SP_Register;
	import.SP_Print = PF_SP_Print;
	import.SP_Print_Obit = PF_SP_Print_Obit;
	import.SP_SPrint = PF_SP_SPrint;
	import.SP_GetStringText = PF_SP_GetStringText;
	import.trace = PF_Trace;
	import.tracefilter = PF_TraceFilter;
	import.pointcontents = PF_PointContents;
	import.regiondistance = PF_RegionDistance;
	import.inPVS = PF_inPVS;
	import.inPHS = PF_inPHS;
	import.SetAreaPortalState = PF_SetAreaPortalState;
	import.AreasConnected = PF_AreasConnected;
	import.GetGhoul = PF_GetGhoul;
	import.NewPlayerModelInfo = PF_NewPlayerModelInfo;
	import.LoadGhoulFile = PF_LoadGhoulFile;
	import.ReadLine = PF_ReadLine;
	import.PreloadGSQ = PF_PreloadGSQ;
	import.RegisterGSQSequences = PF_RegisterGSQSequences;
	import.TurnOffPartsFromGSQ = PF_TurnOffPartsFromGSQ;
	import.playercontext = &sv_playercontext;
	import.configstring = PF_Configstring;
	import.SZ_Init = PF_SZ_Init;
	import.SZ_Clear = PF_SZ_Clear;
	import.SZ_Write = PF_SZ_Write;
	import.error = PF_error;
	import.dedicatedprint = PF_DedicatedPrint;
	import.LoadPlayerDLL = PF_LoadPlayerDLL;
	import.UnloadPlayerDLL = PF_UnloadPlayerDLL;
	import.flrand = PF_flrand;
	import.irand = PF_irand;
	import.linkentity = PF_LinkEntity;
	import.unlinkentity = PF_UnlinkEntity;
	import.BoxEdicts = PF_BoxEdicts;
	import.Pmove = PF_Pmove;
	import.TagMalloc = PF_TagMalloc;
	import.TagFree = PF_TagFree;
	import.FreeTags = PF_FreeTags;
	import.WriteChunk = PF_WriteChunk;
	import.ReadChunk = PF_ReadChunk;
	import.cvar = PF_Cvar;
	import.cvar_set = PF_CvarSet;
	import.cvar_setvalue = PF_CvarSetValue;
	import.cvar_forceset = PF_CvarForceSet;
	import.cvar_info = PF_CvarInfo;
	import.cvar_value = PF_CvarValue;
	import.FS_LoadFile = PF_FS_LoadFile;
	import.FS_FreeFile = PF_FS_FreeFile;
	import.FS_Gamedir = PF_FS_Gamedir;
	import.FS_CreatePath = PF_FS_CreatePath;
	import.FS_FileExists = PF_FS_FileExists;
	import.AddCommandString = PF_AddCommandString;

	/* the Ghoul services must exist before GetGameAPI (SPEC-001) */
	SV_Ghoul_SelectForGame();

	GetGameAPI = SV_LoadGameLibrary();

	if (!GetGameAPI)
	{
		Com_Error(ERR_DROP, "failed to load game DLL");
	}

	/* the game copies the table; keep ours anyway for diagnostics */
	gi = import;

	if (Cvar_VariableValue("sv_watchdog") > 0)
	{
		SV_ImportTrace_Install(&gi); /* test aid: import-call ring */
	}

	ge = GetGameAPI(&gi);

	if (!ge)
	{
		SV_UnloadGameLibrary();
		Com_Error(ERR_DROP, "failed to load game DLL");
	}

	if (ge->apiversion != SOF_GAME_API_VERSION)
	{
		int v = ge->apiversion;

		ge = NULL;
		SV_UnloadGameLibrary();
		Com_Error(ERR_DROP, "game is version %i, not %i", v, SOF_GAME_API_VERSION);
	}

	SV_HostDebug(SV_DBG_IMPORTS, "GetGameAPI: edict_size %i\n", ge->edict_size);

	ge->Init();

	SV_HostDebug(SV_DBG_IMPORTS, "Init: edicts %p, num %i, max %i\n",
			(void *)ge->edicts, ge->num_edicts, ge->max_edicts);

	if (ge->edict_size < (int)sizeof(edict_t))
	{
		Com_Error(ERR_FATAL, "game entity record %i bytes, need %i",
				ge->edict_size, (int)sizeof(edict_t));
	}

	/* the game's own name for its mode, for the server browser */
	if (ge->GetGameType)
	{
		extern void SV_GameSpy_SetGameTypeFunc(const char *(*func)(void));
		SV_GameSpy_SetGameTypeFunc(ge->GetGameType);
	}

	Com_Printf("------------------------------------\n\n");
}

/* ---------------------------------------------------------------------
 * export wrappers used by the rest of the server
 * --------------------------------------------------------------------- */

int
SV_GameCinematicState(void)
{
	if (!ge || !ge->GetCinematicState)
	{
		return 0;
	}

	return ge->GetCinematicState() & 0xFF;
}

/* SPEC-001 "Movement": ClientThink only while no cinematic runs */
void
SV_GameClientThink(edict_t *ent, sof_usercmd_t *cmd)
{
	int state = SV_GameCinematicState();

	SV_HostDebug(SV_DBG_THINK, "think frame %i cin %i msec %i buttons %x%s\n", sv.framenum, state, cmd->msec,
			cmd->buttons, state ? " (held back)" : "");

	if (!state)
	{
		extern int sv_omrec_local; /* sv_pmove.c, om_autorecord */

		sv_player = ent;
		sv_omrec_local = (NUM_FOR_EDICT(ent) == 1);

		if (ent->client && (ent->client->ps.pmove.pm_type == SOF_PM_DEAD) &&
			(cmd->buttons & SOF_BUTTON_SKIP) && !Cvar_VariableValue("deathmatch"))
		{
			Cvar_Set("sv_restartlevel", "1");
		}

		/* SPEC-602 s7: the player module runs on the server side around the
		   game's client think (the shared module of a listen server) */
		if (sv_playercontext)
		{
			int saved = *sv_playercontext;

			*sv_playercontext = 0;
			ge->ClientThink(ent, cmd);

			if (sv_playercontext)
			{
				*sv_playercontext = saved;
			}
		}
		else
		{
			ge->ClientThink(ent, cmd);
		}

		sv_omrec_local = 0;
		return;
	}

	if (cmd->buttons & SOF_BUTTON_SKIP)
	{
		if (state != 2)
		{
			ge->SetCinematicState(1);
		}

		Cvar_Set("sv_jumpcinematic", "2");
	}
}

/* game exports 0x48/0x4C (SPEC-003) and the client command (SPEC-602 s7) run
   with the player-module context flag at 0 */
void
SV_GameCallWithPlayerContext(void (*fn)(void))
{
	int saved;

	if (!fn)
	{
		return;
	}

	if (!sv_playercontext)
	{
		fn();
		return;
	}

	saved = *sv_playercontext;
	*sv_playercontext = 0;
	fn();

	if (sv_playercontext)
	{
		*sv_playercontext = saved;
	}
}

void
SV_GameClientCommand(edict_t *ent)
{
	int saved;
	const char *c0 = Cmd_Argv(0);

	/* single player only: a listen game with one slot (see sv_sp_unknown) */
	sv_sp_unknown = NULL;
	sv_sp_unknown_told = false;

	if ((maxclients->value <= 1) && !(dedicated && dedicated->value) &&
		Q_stricmp(c0, "say") && Q_stricmp(c0, "say_team"))
	{
		sv_sp_unknown = c0;
	}

	if (!sv_playercontext)
	{
		ge->ClientCommand(ent);
		sv_sp_unknown = NULL;
		return;
	}

	saved = *sv_playercontext;
	*sv_playercontext = 0;
	ge->ClientCommand(ent);
	sv_sp_unknown = NULL;

	if (sv_playercontext)
	{
		*sv_playercontext = saved;
	}
}

/* one game frame with the given counter; stores the returned game time.
   The level start runs counters 0 and 1 while loading; then every server
   frame runs counter = wire frame number, starting at 1 (as the original;
   its game time is then (frame + 2) x 0.1, net-verification.md) */
void
SV_GameRunFrame(int counter)
{
	if (sv_playercontext)
	{
		*sv_playercontext = 0;
	}

	sv.gameframe = counter;
	sv.gametime = ge->RunFrame(counter);

	if (sv_hostdebug && ((int)sv_hostdebug->value & SV_DBG_THINK))
	{
		static int last = -1;
		int state = SV_GameCinematicState();

		if (state != last)
		{
			SV_HostDebug(SV_DBG_THINK, "frame %i (counter %i): cinematic state %i -> %i\n", sv.framenum, counter,
					last, state);
			last = state;
		}
	}
}
