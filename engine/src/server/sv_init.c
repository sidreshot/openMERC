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
 * Server startup.
 *
 * OpenMerc: SoF 1.07f name indexes and configstring layout, the level
 * start sequence of SPEC-001 (SpawnEntities, RunFrame(0), RunFrame(1)
 * while loading), baselines, the Ghoul lifetimes of SPEC-010 and the
 * spawn count rules observed on the original (net-verification.md).
 *
 * Implements: SPEC-001 (name indexes, level start), SPEC-203 (configstring
 *             layout, baselines, spawn count, map change), SPEC-010
 *             (lifetimes), SPEC-313 s3 (map list hook)
 *
 * =======================================================================
 */

#include "header/server.h"
#include "../common/header/sof/engine.h"

server_static_t svs; /* persistant server info */
server_t sv; /* local server */

/* ---------------------------------------------------------------------
 * name indexes (SPEC-001 "Name indexes")
 * --------------------------------------------------------------------- */

static void
SV_IndexOverflow(int start, int max, const char *cls)
{
	int i;

	Com_Printf("Items in Slot:\n");

	for (i = 1; i < max; i++)
	{
		Com_Printf("%i: %s\n", i, sv.configstrings[start + i]);
	}

	Com_Error(ERR_DROP, "*Index: %s overflow", cls);
}

int
SV_FindIndex(const char *name, int start, int max, const char *cls)
{
	int i, freeslot = 0;

	if (!name || !name[0])
	{
		return 0;
	}

	/* one leading slash is ignored */
	if ((name[0] == '/') || (name[0] == '\\'))
	{
		name++;

		if (!name[0])
		{
			return 0;
		}
	}

	for (i = 1; i < max; i++)
	{
		const char *cs = sv.configstrings[start + i];

		if (!cs[0])
		{
			if (!freeslot)
			{
				freeslot = i;
			}

			continue;
		}

		if (!Q_stricmp(cs, name))
		{
			return i;
		}
	}

	if (!freeslot)
	{
		SV_IndexOverflow(start, max, cls);
		return 0;
	}

	SV_SetConfigstring(start + freeslot, name);
	return freeslot;
}

int
SV_ModelIndex(const char *name)
{
	return SV_FindIndex(name, SOF_CS_MODELS, SOF_MAX_MODELS, "model");
}

/* sounds: the 1.07f slots 1..355, then the OpenMerc extension 356..511 (game_api.h OM_MAX_SOUNDS) */
int
SV_SoundIndex(const char *name)
{
	int i, freeslot = 0;

	if (!name || !name[0])
	{
		return 0;
	}

	if ((name[0] == '/') || (name[0] == '\\'))
	{
		name++;

		if (!name[0])
		{
			return 0;
		}
	}

	for (i = 1; i < OM_MAX_SOUNDS; i++)
	{
		const char *cs = sv.configstrings[OM_CS_SOUND(i)];

		if (!cs[0])
		{
			if (!freeslot)
			{
				freeslot = i;
			}

			continue;
		}

		if (!Q_stricmp(cs, name))
		{
			return i;
		}
	}

	if (!freeslot)
	{
		Com_Printf("Items in Slot:\n");

		for (i = 1; i < OM_MAX_SOUNDS; i++)
		{
			Com_Printf("%i: %s\n", i, sv.configstrings[OM_CS_SOUND(i)]);
		}

		Com_Error(ERR_DROP, "*Index: sound overflow");
		return 0;
	}

	if (freeslot == SOF_MAX_SOUNDS)
	{
		Com_DPrintf("SV_SoundIndex: past the 1.07f sound table (%s)\n", name);
	}

	SV_SetConfigstring(OM_CS_SOUND(freeslot), name);
	return freeslot;
}

int
SV_EffectIndex(const char *name)
{
	return SV_FindIndex(name, SOF_CS_EFFECTS, SOF_MAX_EFFECTS, "effect");
}

int
SV_ImageIndex(const char *name)
{
	return SV_FindIndex(name, SOF_CS_IMAGES, SOF_MAX_IMAGES, "image");
}

int
SV_GhoulFileIndex(const char *name)
{
	return SV_FindIndex(name, SOF_CS_GHOULFILES, SOF_MAX_GHOULFILES, "ghoulfile");
}

int
SV_StringPackageIndex(const char *name)
{
	return SV_FindIndex(name, SOF_CS_STRINGPACKAGES, SOF_MAX_STRINGPACKAGES, "sp");
}

/* game import #4: frees the slot and, while running, tells the clients */
void
SV_RemoveSound(const char *name)
{
	int i;

	if (!name || !name[0])
	{
		return;
	}

	if ((name[0] == '/') || (name[0] == '\\'))
	{
		name++;
	}

	for (i = 1; i < OM_MAX_SOUNDS; i++)
	{
		char *cs = sv.configstrings[OM_CS_SOUND(i)];

		if (cs[0] && !Q_stricmp(cs, name))
		{
			cs[0] = 0;

			if (sv.state != ss_loading)
			{
				MSG_WriteByte(&sv.multicast, sofsvc_removeconfigstring);
				MSG_WriteShort(&sv.multicast, OM_CS_SOUND(i));
				SV_Multicast(NULL, SOF_MULTICAST_ALL_R, 0);
			}

			return;
		}
	}
}

/* case-insensitive strstr within [s, end) */
static const char *
SV_FindNoCase(const char *s, const char *end, const char *what)
{
	size_t n = strlen(what);

	for (; s + n <= end; s++)
	{
		if (!Q_strncasecmp(s, what, n))
		{
			return s;
		}
	}

	return NULL;
}

/*
 * OpenMerc: a single-player map started with deathmatch set has no
 * info_player_deathmatch, and both game modules (the retail one too) stop with
 * "Couldn't find deathmatch spawn point". In that case every info_player_start
 * is also given to the game as an info_player_deathmatch (appended copies of
 * those entities), so the map can be played in deathmatch. Maps with their own
 * deathmatch spots are passed on unchanged. Returns NULL when nothing changes,
 * else a Z_Malloc'd string.
 */
static char *
SV_DeathmatchSpawnFallback(const char *ents)
{
	static const char start[] = "\"info_player_start\"";
	static const char dm[] = "\"info_player_deathmatch\"";
	const char *p, *end;
	char *out, *o;
	size_t len;
	int count = 0;

	if (!ents || !Cvar_VariableValue("deathmatch"))
	{
		return NULL;
	}

	len = strlen(ents);
	end = ents + len;

	if (SV_FindNoCase(ents, end, dm) || !SV_FindNoCase(ents, end, start))
	{
		return NULL;
	}

	/* each copy grows by the length difference of the class names */
	out = Z_Malloc((int)(2 * len + 2));
	memcpy(out, ents, len);
	o = out + len;

	for (p = ents; (p = strchr(p, '{')) != NULL; )
	{
		const char *close = strchr(p, '}');
		const char *cls;

		if (!close)
		{
			break;
		}

		cls = SV_FindNoCase(p, close, start);

		if (cls && ((size_t)(o - out) + (close - p) + sizeof(dm) + 2 < 2 * len + 2))
		{
			*o++ = '\n';
			memcpy(o, p, cls - p);
			o += cls - p;
			memcpy(o, dm, sizeof(dm) - 1);
			o += sizeof(dm) - 1;
			memcpy(o, cls + sizeof(start) - 1, close + 1 - (cls + sizeof(start) - 1));
			o += close + 1 - (cls + sizeof(start) - 1);
			count++;
		}

		p = close + 1;
	}

	*o = 0;

	if (!count)
	{
		Z_Free(out);
		return NULL;
	}

	Com_Printf("deathmatch: no deathmatch spawn points, %d single-player start(s) used\n", count);
	return out;
}

/* stores a configstring; past the loading state every client gets it */
void
SV_SetConfigstring(int index, const char *val)
{
	if ((index < 0) || (index >= OM_MAX_CONFIGSTRINGS))
	{
		Com_Error(ERR_DROP, "configstring: bad index %i", index);
	}

	if (!val)
	{
		val = "";
	}

	Q_strlcpy(sv.configstrings[index], val, SOF_CS_LENGTH);

	if (sv.state != ss_loading)
	{
		SZ_Clear(&sv.multicast);
		SV_WriteConfigstringMsg(&sv.multicast, index, sv.configstrings[index]);
		SV_Multicast(NULL, SOF_MULTICAST_ALL_R, 0);
	}
}

/* ---------------------------------------------------------------------
 * baselines (SPEC-203: model, sound, effects or a non-empty effect slot)
 * --------------------------------------------------------------------- */

qboolean
SV_BaselineWanted(const sof_entity_state_t *s)
{
	return s->modelindex || s->sound || s->effects ||
		s->effect_slots[0] || s->effect_slots[3] || s->effect_slots[6] || s->effect_slots[9];
}

static void
SV_CreateBaseline(void)
{
	edict_t *svent;
	int entnum;

	memset(sv.baselines, 0, sizeof(sv.baselines));

	for (entnum = 1; entnum < ge->num_edicts && entnum < SOF_MAX_EDICTS; entnum++)
	{
		svent = EDICT_NUM(entnum);

		if (!svent->inuse)
		{
			continue;
		}

		if (!SV_BaselineWanted(&svent->s))
		{
			continue;
		}

		svent->s.number = entnum;

		/* take current state as baseline (no old_origin in SoF: the
		   bytes Quake II used for it are effect slots, AMEND-005 G18.18) */
		sv.baselines[entnum] = svent->s;
	}
}

void
SV_ClearBaselines(void)
{
	memset(sv.baselines, 0, sizeof(sv.baselines));
}

/* ---------------------------------------------------------------------
 * spawning
 * --------------------------------------------------------------------- */

static qboolean
SV_AnyClientConnected(void)
{
	int i;

	for (i = 0; i < svs.numclients; i++)
	{
		if (svs.clients[i].state >= cs_connected)
		{
			return true;
		}
	}

	return false;
}

/*
 * Change the server to a new map, taking all connected
 * clients along with it.
 */
static void
SV_SpawnServer(const char *server, const char *spawnpoint, server_state_t serverstate,
		qboolean attractloop, qboolean loadgame)
{
	int i;
	unsigned checksum;
	qboolean clients;

	if (attractloop)
	{
		Cvar_Set("paused", "0");
	}

	Com_Printf("------- Server Initialization -------\n");
	Com_DPrintf("SpawnServer: %s\n", server);

	if (sv.demofile)
	{
		FS_FCloseFile(sv.demofile);
	}

	svs.spawncount++; /* any partially connected client will be restarted */
	sv.state = ss_dead;
	Com_SetServerState(sv.state);

	clients = SV_AnyClientConnected();

	/* wipe the entire per-level structure */
	SV_FreeMapExtras();
	memset(&sv, 0, sizeof(sv));
	svs.realtime = 0;
	sv.loadgame = loadgame;
	sv.attractloop = attractloop;

	SZ_Init(&sv.multicast, sv.multicast_buf, sizeof(sv.multicast_buf));

	Q_strlcpy(sv.name, server, sizeof(sv.name));
	Q_strlcpy(sv.configstrings[SOF_CS_NAME], server, SOF_CS_LENGTH);

	/* leave slots at start for clients only */
	for (i = 0; i < svs.numclients; i++)
	{
		/* needs to reconnect */
		if (svs.clients[i].state > cs_connected)
		{
			svs.clients[i].state = cs_connected;
		}

		svs.clients[i].lastframe = -1;
		svs.clients[i].entered = false;
	}

	sv.time = 1000;

	/* SPEC-010 lifetimes: resend the Ghoul registry to connected clients,
	   purge model files nobody references */
	SV_Ghoul_ServerSpawn(clients);

	if (serverstate != ss_game)
	{
		sv.models[1] = CM_LoadMap("", false, &checksum); /* no real map */
	}
	else
	{
		Com_sprintf(sv.configstrings[SOF_CS_MODELS + 1], SOF_CS_LENGTH, "maps/%s.bsp", server);
		sv.models[1] = CM_LoadMap(sv.configstrings[SOF_CS_MODELS + 1], false, &checksum);
		SV_LoadMapExtras(sv.configstrings[SOF_CS_MODELS + 1]);
	}

	/* clear physics interaction links */
	SV_ClearWorld();

	for (i = 1; i < CM_NumInlineModels(); i++)
	{
		Com_sprintf(sv.configstrings[SOF_CS_MODELS + 1 + i], SOF_CS_LENGTH, "*%i", i);
		sv.models[i + 1] = CM_InlineModel(sv.configstrings[SOF_CS_MODELS + 1 + i]);
	}

	/* the server's welcome message (SPEC-203 configstring 1493) */
	Q_strlcpy(sv.configstrings[SOF_CS_WELCOME],
			Cvar_VariableString("welcome_mess"), SOF_CS_LENGTH);

	/* the deathmatch map rotation (SPEC-313 s3) */
	if (Cvar_VariableValue("deathmatch"))
	{
		SoF_BuildMapList();
	}

	/* the server's string packages go with the old level (SPEC-313 s1) */
	SV_SP_Release();

	/* spawn the rest of the entities on the map */
	sv.state = ss_loading;
	Com_SetServerState(sv.state);

	/* load and spawn all other entities */
	{
		/* kept until the next map: the game may hold on to the string it was given, as to the map's own */
		static char *dments;

		if (dments)
		{
			Z_Free(dments);
		}

		dments = SV_DeathmatchSpawnFallback(CM_EntityString());
		ge->SpawnEntities(sv.name, dments ? dments : CM_EntityString(), spawnpoint);
	}

	/* SPEC-001: RunFrame(0) and RunFrame(1) while still loading */
	SV_GameRunFrame(0);
	SV_GameRunFrame(1);
	/* the first server frame is frame 1 and passes counter 1, then 2, 3, ...
	   (the original 1.07f: RunFrame 0, 1 while loading, then 1, 2, 3 with the
	   wire frame number - logged by the I1b tracing proxy on kos1, 2026-10-25;
	   game time on the wire = (frame + 2) x 0.1 as net-verification.md says).
	   Starting at frame 0 with counter frame + 2 had put the game time one
	   frame ahead of the original's: (frame + 3) x 0.1. */
	sv.framenum = 0;

	/* all precaches are complete */
	sv.state = serverstate;
	Com_SetServerState(sv.state);

	/* create a baseline for more efficient communications */
	SV_CreateBaseline();

	/* set serverinfo variable */
	Cvar_FullSet("mapname", sv.name, CVAR_SERVERINFO | CVAR_NOSET);

	SV_SetPureViolenceBit();

	Com_Printf("-------------------------------------\n");
}

/*
 * SPEC-AMEND-005 GS.15: at the end of every server spawn sv_violence is
 * force-set to its current value with bit 0x20 ("pure server") set when the
 * game module file has the checksum of the shipped 1.07f gamex86.dll
 * (64C0FC54) and the server-side player module file that of the shipped
 * player.dll (EACC2EEA), cleared otherwise; bits 0x01-0x10 (the server's
 * locks) are kept. DEVIATIONS "pure server" (2026-10-25): a game module and a
 * server-side player module that both carry the OpenMerc_Module marker export
 * (OpenMerc's own modules) count as the stock pair, otherwise a listen server
 * made only of OpenMerc code refused its own client on every PC whose parental
 * lock store is missing or invalid (cl_violence 63 vs sv_violence 31).
 */
#define SV_PURE_GAME_CHECKSUM 0x64C0FC54u
#define SV_PURE_PLAYER_CHECKSUM 0xEACC2EEAu

void
SV_SetPureViolenceBit(void)
{
	int bits = (int)Cvar_VariableValue("sv_violence") & 0x1f;

	if (((SV_GameModuleChecksum() == SV_PURE_GAME_CHECKSUM) &&
		 (SV_PlayerModuleChecksum() == SV_PURE_PLAYER_CHECKSUM)) ||
		(SV_GameModuleIsOpenMerc() && SV_PlayerModuleIsOpenMerc()))
	{
		bits |= 0x20;
	}

	Cvar_ForceSet("sv_violence", va("%i", bits));
}

/*
 * A brand new game has been started
 */
void
SV_InitGame(void)
{
	int i;

	if (svs.initialized)
	{
		/* cause any connected clients to reconnect */
		SV_Shutdown("Server restarted\n", true);
	}

#ifndef DEDICATED_ONLY
	else
	{
		/* make sure the client is down */
		CL_Drop();
		SCR_BeginLoadingPlaque();
	}
#endif

	/* get any latched variable changes (maxclients, etc) */
	Cvar_GetLatchedVars();

	svs.initialized = true;

	if (Cvar_VariableValue("deathmatch") && (maxclients->value <= 1))
	{
		Cvar_FullSet("maxclients", "8", CVAR_SERVERINFO | CVAR_LATCH);
	}
	else if (maxclients->value > SOF_MAX_CLIENTS)
	{
		Cvar_FullSet("maxclients", va("%i", SOF_MAX_CLIENTS), CVAR_SERVERINFO | CVAR_LATCH);
	}
	else if (maxclients->value < 1)
	{
		Cvar_FullSet("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH);
	}

	/* SPEC-200: a local server with one client (single player) uses the
	   16384-byte maximum datagram, every other server 1400; set before the
	   client channels are opened */
	net_maxmsglen = ((maxclients->value <= 1) && !(dedicated && dedicated->value)) ?
		MAX_MSGLEN_LOCAL : MAX_MSGLEN;

	/* net-verification.md: C-library rand() at a fresh game start, +1 per level */
	svs.spawncount = rand();
	SV_GameSpy_ForceHeartbeat();
	svs.numclients = (int)maxclients->value;
	svs.clients = Z_Malloc(sizeof(client_t) * svs.numclients);
	svs.num_client_entities = svs.numclients * UPDATE_BACKUP * 64;
	svs.client_entities = Z_Malloc(sizeof(sof_entity_state_t) * svs.num_client_entities);

	/* init network stuff */
	NET_Config((maxclients->value > 1) || (dedicated && dedicated->value));

	SV_InitTables();

	/* init game */
	SV_InitGameProgs();

	for (i = 0; i < svs.numclients; i++)
	{
		CLNUM_EDICT(i)->s.number = i + 1;
		memset(&svs.clients[i].lastcmd, 0, sizeof(svs.clients[i].lastcmd));
	}
}

/*
 * the full syntax is:
 *
 * map [*]<map>$<startspot>+<nextserver>
 *
 * command from the console or progs.
 */
void
SV_Map(qboolean attractloop, const char *levelstring, qboolean loadgame)
{
	char level[MAX_QPATH];
	char *ch;
	size_t l;
	char spawnpoint[MAX_QPATH];

	sv.loadgame = loadgame;
	sv.attractloop = attractloop;

	/* copied first: levelstring is usually Cmd_Argv(1), and starting the game
	   module runs commands that tokenize again (with developer 0 the map name
	   became a menu string such as "OPTIONS") */
	Q_strlcpy(level, levelstring, sizeof(level));

	if ((sv.state == ss_dead) && !sv.loadgame)
	{
		SV_InitGame(); /* the game is just starting */
	}

	/* if there is a + in the map, set nextserver to the remainder */
	ch = strstr(level, "+");

	if (ch)
	{
		*ch = 0;
		Cvar_Set("nextserver", va("gamemap \"%s\"", ch + 1));
	}
	else
	{
		Cvar_Set("nextserver", "");
	}

	/* if there is a $, use the remainder as a spawnpoint */
	ch = strstr(level, "$");

	if (ch)
	{
		*ch = 0;
		Q_strlcpy(spawnpoint, ch + 1, sizeof(spawnpoint));
	}
	else
	{
		spawnpoint[0] = 0;
	}

	/* skip the end-of-unit flag if necessary */
	l = strlen(level);

	if (level[0] == '*')
	{
		memmove(level, level + 1, l);
	}

#ifndef DEDICATED_ONLY
	SCR_BeginLoadingPlaque(); /* for local system */
#endif

	/* SPEC-203 s5: "changing", flush, load, then "reconnect" */
	SV_BroadcastCommand("changing\n");
	SV_SendClientMessages();
	SV_SpawnServer(level, spawnpoint, ss_game, attractloop, loadgame);

	/* commands after `map` (command line or script) wait until the local
	   client has entered (Quake II; SV_Begin_f inserts them again). The
	   original 1.07f does the same: `+map kos1 +exec x` runs x after the
	   client's begin, before it is active (black box, 2026-10-25) */
	Cbuf_CopyToDefer();

	{
		int i;

		/* the reconnect limit does not apply to a map change */
		for (i = 0; i < svs.numclients; i++)
		{
			svs.clients[i].lastconnect = -99999;
		}
	}

	SV_BroadcastCommand("reconnect\n");
}
