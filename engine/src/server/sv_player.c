/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: loader of the player module (player.dll) for game imports
 * #74/#75, with the reference counting shared by the server and the client
 * side, the context-flag variable of import #67 and the player-module file
 * checksum.
 *
 * Implements: SPEC-003 ("Loading"), SPEC-001 (#67, #74, #75)
 */

#include "header/server.h"

#ifdef _WIN32
#define SV_PLAYER_LIBRARY "player.dll"
#else
#define SV_PLAYER_LIBRARY "player.so"
#endif

/* game import #67: the game stores the player module's context-flag pointer here */
int *sv_playercontext;

static void *player_library;
static qboolean player_side[2];       /* 0 server, 1 client */
static void *player_server_api, *player_client_api;
static unsigned player_checksum;

/* SPEC-003 / SPEC-301 s8: seed 0xF8349720; per byte: rotate left 1, then add
   (odd positions 1, 3, 5, ...) or xor (even positions 2, 4, ...); also used for
   the game module file (SPEC-AMEND-005 GS.15) */
unsigned
SV_FileChecksum(const char *path)
{
	FILE *f;
	unsigned v = 0xF8349720u;
	int c;
	long n = 0;

	f = Q_fopen(path, "rb");

	if (!f)
	{
		return 0;
	}

	while ((c = fgetc(f)) != EOF)
	{
		v = (v << 1) | (v >> 31);

		if (!(n & 1))
		{
			v += (unsigned)c;
		}
		else
		{
			v ^= (unsigned)c;
		}

		n++;
	}

	fclose(f);
	return v;
}

static qboolean
SV_TryPlayerLibrary(const char *name)
{
	if (!Sys_IsFile(name))
	{
		return false;
	}

	Com_DPrintf("LoadLibrary (%s)\n", name);
	Sys_LoadLibrary(name, NULL, &player_library);

	if (!player_library)
	{
		return false;
	}

	player_checksum = SV_FileChecksum(name);
	Com_DPrintf("player module checksum %08X\n", player_checksum);
	return true;
}

static qboolean
SV_OpenPlayerLibrary(void)
{
	char name[MAX_OSPATH], cwd[MAX_OSPATH];
	const char *path = NULL;

	if (player_library)
	{
		return true;
	}

	if (Sys_Realpath(".", cwd, sizeof(cwd)))
	{
		Com_sprintf(name, sizeof(name), "%s/final/%s", cwd, SV_PLAYER_LIBRARY);

		if (SV_TryPlayerLibrary(name))
		{
			return true;
		}

		Com_sprintf(name, sizeof(name), "%s/%s", cwd, SV_PLAYER_LIBRARY);

		if (SV_TryPlayerLibrary(name))
		{
			return true;
		}
	}

	while ((path = FS_NextPath(path)) != NULL)
	{
		Com_sprintf(name, sizeof(name), "%s/%s", path, SV_PLAYER_LIBRARY);

		if (SV_TryPlayerLibrary(name))
		{
			return true;
		}
	}

	return false;
}

/* #74: is-client = 0 -> GetPlayerServerAPI(common, server), otherwise
   GetPlayerClientAPI(common, client); NULL on failure */
void *
SV_LoadPlayerModule(void *common, void *client, void *server, int isclient)
{
	sof_getplayerapi_t api;
	int side = isclient ? 1 : 0;
	void *result;

	if (!SV_OpenPlayerLibrary())
	{
		return NULL;
	}

	api = (sof_getplayerapi_t)Sys_GetProcAddress(player_library,
			side ? "GetPlayerClientAPI" : "GetPlayerServerAPI");

	if (!api)
	{
		if (!player_side[0] && !player_side[1])
		{
			Sys_FreeLibrary(player_library);
			player_library = NULL;
		}

		return NULL;
	}

	result = side ? api(common, client) : api(common, server);

	if (result)
	{
		player_side[side] = true;

		if (side)
		{
			player_client_api = result;
		}
		else
		{
			player_server_api = result;
		}
	}

	return result;
}

/* I1d: set by the engine client (cl_player.c) while it holds the client side */
void (*sv_player_clientdrop)(void);

/* #75: release one side; the library goes when neither side uses it */
void
SV_UnloadPlayerModule(int isclient)
{
	int side = isclient ? 1 : 0;

	player_side[side] = false;

	if (side)
	{
		player_client_api = NULL;
	}
	else
	{
		player_server_api = NULL;

		/* I1d: SPEC-003 "Loading" - unloading the server side also makes the client delete its prediction
		   inventory and drop its own reference (it reloads at its next connection) */
		if (player_side[1] && sv_player_clientdrop)
		{
			sv_player_clientdrop();
		}
	}

	if (!player_side[0] && !player_side[1] && player_library)
	{
		Sys_FreeLibrary(player_library);
		player_library = NULL;
	}
}

/* SPEC-003 checksum of the player module file when its server side is loaded, else 0 */
unsigned
SV_PlayerModuleChecksum(void)
{
	return (player_library && player_side[0]) ? player_checksum : 0;
}

/* true when the server-side player module is OpenMerc's own (marker export, DEVIATIONS "pure server") */
qboolean
SV_PlayerModuleIsOpenMerc(void)
{
	return player_library && player_side[0] &&
		   Sys_GetProcAddress(player_library, "OpenMerc_Module") != NULL;
}

/* the game is gone: forget whatever it did not release itself */
void
SV_ShutdownPlayerModule(void)
{
	sv_playercontext = NULL;

	if (player_side[0])
	{
		SV_UnloadPlayerModule(0);
	}
}
