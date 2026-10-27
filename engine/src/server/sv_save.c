/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc server: the savegame hooks of the server and the game-module
 * host. The .sof container and the save/load commands are the engine
 * core's (common/sof_save.c, SPEC-303); the server supplies the running
 * level's data through sg_host_t and gives the game module the chunk
 * services of imports #85/#86.
 *
 * Implements: SPEC-001 (#85, #86, "Save", "Load"), SPEC-303 (server side)
 */

#include "header/server.h"
#include "../common/header/sof/savegame.h"
#include "../common/header/sof_pmove.h"

#ifndef DEDICATED_ONLY
/* client/menu/menu.c (I6m): the 128x128 picture taken when the menus opened */
qboolean M_SaveThumbnail(byte *rgba65536);
#endif

/* game import #85: true when the whole chunk was written */
int
SV_WriteChunk(int id, const void *data, int length)
{
	if (!SG_IsOpen())
	{
		return 0;
	}

	SG_WriteChunk((unsigned)id, data, length);
	return 1;
}

/* game import #86: into the buffer (length must match when non-zero), or
   into new memory handed over through `allocated', or read and dropped */
int
SV_ReadChunk(int id, void *buffer, int length, void **allocated)
{
	int len = 0;
	void *data;

	if (buffer && (length > 0))
	{
		return SG_ReadChunk((unsigned)id, buffer, length);
	}

	data = SG_ReadChunkAlloc((unsigned)id, &len);

	if (buffer && data)
	{
		memcpy(buffer, data, len);
	}

	if (allocated)
	{
		*allocated = data;
	}
	else if (data)
	{
		Z_Free(data);
	}

	return len;
}

static qboolean
SVH_InGame(void)
{
	return ge && (sv.state == ss_game) && !sv.attractloop;
}

static int
SVH_PlayerHealth(void)
{
	/* the first client's health stat (SPEC-780 s3 stats[1]), as Quake II reads
	   it for "Can't savegame while dead!" (SPEC-303 s1) */
	if (ge && svs.clients && (svs.clients[0].state >= cs_connected))
	{
		const edict_t *ent = EDICT_NUM(1);

		if (ent->client)
		{
			return ent->client->ps.stats[1];
		}
	}

	return 100;
}

static qboolean
SVH_InCinematic(void)
{
	return SV_GameCinematicState() != 0;
}

static qboolean
SVH_AllowSave(void)
{
	return (ge && ge->SaveAllowed) ? ge->SaveAllowed() : true;
}

static const char *
SVH_MapCommand(void)
{
	return svs.mapcmd[0] ? svs.mapcmd : sv.name;
}

static const char *
SVH_ConfigString(int index)
{
	if ((index < 0) || (index >= OM_MAX_CONFIGSTRINGS))
	{
		return "";
	}

	return sv.configstrings[index];
}

static int
SVH_FrameNumber(void)
{
	return sv.framenum;
}

static void
SVH_WriteGame(qboolean autosave)
{
	if (ge)
	{
		ge->WriteGame(autosave);
	}
}

static void
SVH_WriteLevel(void)
{
	if (ge)
	{
		ge->WriteLevel();
	}
}

static qboolean
SVH_StartLoaded(const char *mapcommand)
{
	qboolean autosave;
	char map[MAX_QPATH];

	Q_strlcpy(map, mapcommand, sizeof(map));

	/* SPEC-303 load step 4: a new server is started as `map` would (the running
	   one goes with "Server restarted" and its clients reconnect - the original
	   prints that, then the client's "Server disconnected, reconnecting"), the
	   game reads its game chunks, then the map is spawned in "loading a save"
	   mode (Quake II order). Spawning first and reading afterwards kept the old
	   client slot: the local client's reconnect was refused, "Server is full". */
	SV_InitGame();

	if (!ge)
	{
		return false;
	}

	/* ReadGame returns the autosave flag; a normal save has a level part */
	autosave = ge->ReadGame(0) ? true : false;

	SV_Map(false, map, true);
	return !autosave;
}

static void
SVH_SetFrameNumber(int frame)
{
	sv.framenum = frame;
}

static void
SVH_SetConfigString(int index, const char *value)
{
	if ((index >= 0) && (index < OM_MAX_CONFIGSTRINGS))
	{
		Q_strlcpy(sv.configstrings[index], value ? value : "", SOF_CS_LENGTH);
	}
}

static void
SVH_ReadLevel(void)
{
	if (ge)
	{
		ge->ReadLevel();
	}
}

/* ---------------------------------------------------------------------
 * OpenMerc timed autosave (docs/impl/AUTOSAVE.md). om_autosave_interval
 * minutes (0 off, at most 5) of game time - frames the game module ran, so
 * paused time, menus and the console do not count - after the level start,
 * a load or any save, the next quiet moment is written to autotime1 /
 * autotime2. Single player only. A quiet moment: alive and walking
 * (SOF_PM_NORMAL) on the ground, no cinematic, the client drawing the game
 * (no menu, console or loading plaque), standing on walkable solid ground
 * and not stuck, and all of that plus no damage taken, no fire button and no
 * EmergencySnapPosition for the last 3 s. Both modes: the save path is the engine's, the game module
 * only writes its chunks as for "save". The manual-save limit of the harder
 * skills is not consulted and not spent.
 * --------------------------------------------------------------------- */

#define SVA_QUIETFRAMES 30   /* 3 s */
#define SVA_FAILFRAMES 600   /* 60 s after a failed write */
#define SVA_SHOTWAIT 500     /* ms to wait for the thumbnail */

static cvar_t *om_autosave_interval;
static cvar_t *om_autosave_debug;

static struct
{
	int spawncount;
	int savecount;
	int frames;  /* game frames since the level start, a load or a save */
	int quiet;   /* game frames without damage taken or a fire button */
	int health;
	int backoff; /* game frames before the next try */
	int due;     /* game frames the save has waited for a quiet moment */
	int shotstart;
	int snaps;   /* sv_pmove_snaps seen */
	qboolean footing; /* last game frame: standing on solid, walkable ground, not stuck */
} sva;

static edict_t *
SVA_Player(void)
{
	edict_t *ent;

	if (!ge || (sv.state != ss_game) || sv.attractloop || !svs.clients ||
		(maxclients->value > 1) || Cvar_VariableValue("deathmatch") ||
		Cvar_VariableValue("coop") || (svs.clients[0].state != cs_spawned))
	{
		return NULL;
	}

	ent = EDICT_NUM(1);
	return ent->client ? ent : NULL;
}

static void
SVA_Reset(void)
{
	memset(&sva, 0, sizeof(sva));
	sva.spawncount = svs.spawncount;
	sva.savecount = SG_SaveCount();
	sva.health = -1;
	sva.snaps = sv_pmove_snaps;
	SG_CancelTimedShot();
}

/* minutes as milliseconds of game time, 0 = off; clamped to 0..5 */
static int
SVA_Interval(void)
{
	float v = om_autosave_interval->value;

	if ((v < 0) || (v > 5))
	{
		Cvar_SetValue("om_autosave_interval", (v < 0) ? 0 : 5);
		v = om_autosave_interval->value;
	}

	return (int)(v * 60000.0f);
}

void
SV_AutoSaveGameFrame(void)
{
	edict_t *ent;
	int h;

	if ((sva.spawncount != svs.spawncount) || (sva.savecount != SG_SaveCount()))
	{
		SVA_Reset();
	}

	if (!(ent = SVA_Player()))
	{
		return;
	}

	sva.frames++;

	if (sva.backoff > 0)
	{
		sva.backoff--;
	}

	h = ent->client->ps.stats[1];

	/* footing: the player box is free (not stuck in solid) and 4 units below
	   it is walkable solid ground (normal z >= 0.7) - a save taken while
	   stuck or standing on an edge restores the player there */
	{
		sof_trace_t tr;
		vec3_t end;

		VectorCopy(ent->s.origin, end);
		end[2] -= 4;
		SV_TraceSoF(ent->s.origin, ent->mins, ent->maxs, end, ent, SOFPM_MASK_PLAYERSOLID, &tr);
		sva.footing = !tr.startsolid && !tr.allsolid && (tr.fraction < 1.0f) && (tr.plane.normal[2] >= 0.7f);
	}

	/* 3 s of a steady state before a save: no damage taken, no fire button,
	   and alive, walking on the ground outside a cinematic the whole time.
	   Mode A: a save taken on the first frame after tsr1's intro cinematic
	   (on the ground again) crashed the original game module on load. */
	if (((sva.health >= 0) && (h < sva.health)) || (h < 1) ||
		(svs.clients[0].lastcmd.buttons & (SOF_BUTTON_ATTACK | SOF_BUTTON_ALTATTACK)) ||
		(ent->client->ps.pmove.pm_type != SOF_PM_NORMAL) ||
		!(ent->client->ps.pmove.pm_flags & SOFPMF_ON_GROUND) || SVH_InCinematic() ||
		Cvar_VariableValue("sv_jumpcinematic") || !sva.footing || (sva.snaps != sv_pmove_snaps))
	{
		sva.quiet = 0;
	}
	else
	{
		sva.quiet++;
	}

	sva.snaps = sv_pmove_snaps;
	sva.health = h;
}

void
SV_AutoSaveFrame(void)
{
	edict_t *ent;
	char slot[MAX_QPATH];
	int interval, state, writems, shotms;

	interval = SVA_Interval();

	if ((sva.spawncount != svs.spawncount) || (sva.savecount != SG_SaveCount()))
	{
		SVA_Reset();
	}

	if (!interval || !(ent = SVA_Player()) || ((float)sva.frames * 100.0f < (float)interval))
	{
		SG_CancelTimedShot();
		return;
	}

	{
		const char *wait = NULL;

		if (ent->client->ps.stats[1] < 1)
			wait = "dead";
		else if (ent->client->ps.pmove.pm_type != SOF_PM_NORMAL)
			wait = "not walking (pm_type)";
		else if (!(ent->client->ps.pmove.pm_flags & SOFPMF_ON_GROUND))
			wait = "in the air";
		else if (!sva.footing)
			wait = "not on solid ground (edge, slope or stuck)";
		else if (SVH_InCinematic() || Cvar_VariableValue("sv_jumpcinematic"))
			wait = "cinematic";
		else if (sv_paused->value)
			wait = "paused";
		else if (!SG_ClientInGame())
			wait = "menu, console or loading";
		else if (sva.quiet < SVA_QUIETFRAMES)
			wait = "settling (damage, fire, air, cinematic or a stuck-in-solid snap in the last 3 s)";
		else if (sva.backoff > 0)
			wait = "retry after a failed write";

		if (wait)
		{
			/* om_autosave_debug: the reason, every 10 s of waiting */
			if (om_autosave_debug->value && !(sva.due % 100))
			{
				Com_Printf("om_autosave: due, waiting: %s (health %i)\n", wait, ent->client->ps.stats[1]);
			}

			sva.due++;
			SG_CancelTimedShot();
			return;
		}
	}

	/* the thumbnail comes from the client's next drawn frame */
	state = SG_TimedShotState();

	if (state == 0)
	{
		SG_RequestTimedShot();
		sva.shotstart = Sys_Milliseconds();
		return;
	}

	if ((state == 1) && (Sys_Milliseconds() - sva.shotstart < SVA_SHOTWAIT))
	{
		return;
	}

	/* no ge->SaveAllowed: that is the manual-save limit of the harder skills
	   (sk_saving), it counts one save down and prints "N saves left". An
	   autosave does not spend the player's saves; om_autosave_interval 0
	   keeps the limit strict. */
	if (!SG_TimedAutoSave(slot, sizeof(slot), &writems, &shotms))
	{
		sva.backoff = SVA_FAILFRAMES;
		return;
	}

	Com_Printf("%s\n", SG_AutosaveLabel());
	{
		const short *o = ent->client->ps.pmove.origin;
		const char *f = "om_autosave: %s on %s at (%.1f %.1f %.1f), write %i ms, thumbnail %i ms, "
			"waited %.1f s for a quiet moment\n";

		if (om_autosave_debug->value || (writems > 50))
		{
			Com_Printf(f, slot, sv.name, o[0] * 0.125f, o[1] * 0.125f, o[2] * 0.125f, writems, shotms,
					sva.due * 0.1f);
		}
		else
		{
			Com_DPrintf(f, slot, sv.name, o[0] * 0.125f, o[1] * 0.125f, o[2] * 0.125f, writems, shotms,
					sva.due * 0.1f);
		}
	}

	SVA_Reset();
}

void
SV_InitSaveHost(void)
{
	sg_host_t host;

	/* 3 minutes: about a third of a typical SoF level section, so a death
	   costs little replay, while a write (thumbnail + chunks) stays rare */
	om_autosave_interval = Cvar_Get("om_autosave_interval", "3", CVAR_ARCHIVE);
	om_autosave_debug = Cvar_Get("om_autosave_debug", "0", 0);

	memset(&host, 0, sizeof(host));
	host.InGame = SVH_InGame;
	host.PlayerHealth = SVH_PlayerHealth;
	host.InCinematic = SVH_InCinematic;
	host.AllowSave = SVH_AllowSave;
	host.MapCommand = SVH_MapCommand;
	host.ConfigString = SVH_ConfigString;
	host.FrameNumber = SVH_FrameNumber;
	host.WriteGame = SVH_WriteGame;
	host.WriteLevel = SVH_WriteLevel;
	host.StartLoaded = SVH_StartLoaded;
	host.SetFrameNumber = SVH_SetFrameNumber;
	host.SetConfigString = SVH_SetConfigString;
	host.ReadLevel = SVH_ReadLevel;
#ifndef DEDICATED_ONLY
	host.Screenshot = M_SaveThumbnail;
#endif

	SG_SetHost(&host);
}
