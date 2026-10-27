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

void
SV_InitSaveHost(void)
{
	sg_host_t host;

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
