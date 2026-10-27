/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc client (I1d): the client side of the player module (player.dll, SPEC-003).
 *
 *  - Session: for every connection (also the local one of a single-player game and demo playback) the client
 *    loads the module's client side - GetPlayerClientAPI(common, client) through the loader shared with the
 *    server side (sv_player.c, one library, reference counted), checks the version, calls Init and creates its
 *    prediction inventory. At the disconnect the inventory is deleted, Shutdown called and the reference
 *    dropped; when the server side is unloaded first, the client side goes with it (SPEC-003 "Loading").
 *  - The shared context flag is written -1 at the start of every client frame and before the prediction
 *    inventory is (re)created (SPEC-003 "Context flag").
 *  - svc 28: the event block is read into the inventory (slot 69) and, when the client does not predict,
 *    applied at once (slot 70); svc 29: a new prediction inventory and the resync number; svc 30: the 640-byte
 *    full state (slot 77) (SPEC-003 "Network messages", SPEC-602 section 6). On a non-local connection export
 *    8 forgets the cached in-view models after every new inventory (observed, black-box trace of the 1.07f
 *    client through I7's tracing proxy).
 *  - `predicting` is set at the start of a connection from cl_predict, and only when the server is not
 *    `localhost` (SPEC-003 "Client prediction").
 *  - View weapon: the module's client imports 2/3 (predicted fire effects), 4 (note sound) and 6 (note effect).
 *    When the client does not predict, the engine itself plays the notes of the view-weapon instance the server
 *    replicates (player-state field 1): it registers handlers for `sound`, `effect`, `fire` and `altfire` on the
 *    client Ghoul instance and processes its events up to the game clock + 0.001 s once per client frame, after
 *    the packets of the frame were read. Observed on the 1.07f client in single player (I1d sessions o1, o_a,
 *    o_c: `sound` notes play on the player's own entity, channel 1, volume 0.6, no attenuation; `effect` notes
 *    start their effect and `fire` notes the predicted fire effects - e.g. the shell-casing sound - in the
 *    client frame of the shot's temp entity once the game clock has settled), while the player module's client
 *    imports are never called there (I7's proxy traces of the same kind of sessions).
 *
 * Implements: SPEC-003 (client side: loading, tables, context flag, svc 28-30), SPEC-602 (section 1, 6),
 *             SPEC-400 ("View weapon"), SPEC-310 section 13
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "header/cl_ghoul.h"
#include "header/cl_player.h"
#include "fx/cl_fx.h"
#include "../common/header/sof/engine.h"
#include "../common/header/sof/game_api.h"

/* the loader shared with the server side (sv_player.c) */
void *SV_LoadPlayerModule(void *common, void *client, void *server, int isclient);
void SV_UnloadPlayerModule(int isclient);
extern void (*sv_player_clientdrop)(void);
/* the Ghoul text services the game also gets (sv_ghoul.cpp, imports #61-#66) */
void *SV_Ghoul_NewPlayerModel(const char *file);
int SV_Ghoul_LoadFile(const char *folder, const char *file, void **buffer);
int SV_Ghoul_GetLine(int *remaining, char **cursor, char *line);
void SV_Ghoul_PreloadList(const char *folder, const char *name);
int SV_Ghoul_RegisterList(const char *folder, const char *name, void *object);
void SV_Ghoul_PartsOff(const char *folder, const char *name, void *object, void *instance);

#define CLP_STATE_BYTES 640
#define CLP_NOTE_VOLUME 0.6f

static cvar_t *om_clplayer;   /* OpenMerc test switch: 0 = no client side (as before I1d) */
static cvar_t *om_cpltrace;   /* OpenMerc test aid: print every note and module call of the client side */
static cvar_t *ghl_specular;

static sof_player_common_import_t clp_common;
static sof_player_client_import_t clp_client;

static struct
{
	qboolean session;            /* a connection is running with the client side loaded */
	qboolean local;              /* the server is `localhost` */
	sof_player_export_t *api;
	void *inv;                   /* prediction inventory */
	int resync;                  /* number of the last svc 29 */
	int viewhandle;              /* view-weapon instance the engine's note handlers serve */
	int viewtype;
	float time;                  /* client import 1: the client's game-time estimate */
} clp;

/* ---- client import table (SPEC-003) ---------------------------------------------------------------- */

static void *
CLP_GetGhoul(void)
{
	return CL_Ghoul_System();
}

static int
CLP_CurrentWeaponOf(void *inv)
{
	return inv ? CLP_InvCurrentWeapon(inv) : 0;
}

static void
CLP_FirePrimary(void *ctx, void *owner, void *inv)
{
	CL_Player_FireEffects(CLP_CurrentWeaponOf(inv), 0);
}

static void
CLP_FireAlternate(void *ctx, void *owner, void *inv)
{
	CL_Player_FireEffects(CLP_CurrentWeaponOf(inv), 1);
}

static void
CLP_KillSound(void)
{
	Com_Printf("WeaponKillSoundClient\n");
}

static int CLP_ReadByte(void) { return MSG_ReadByte(&net_message); }
static int CLP_ReadShort(void) { return MSG_ReadShort(&net_message); }
static int CLP_ReadLong(void) { return MSG_ReadLong(&net_message); }

/* the module's 20-byte message buffer (SPEC-003), not the engine's sizebuf_t */
static int
CLP_SZReadByte(sof_sizebuf_t *sb)
{
	if (sb->readcount + 1 > sb->cursize)
	{
		return -1;
	}

	return sb->data[sb->readcount++];
}

static int
CLP_SZReadShort(sof_sizebuf_t *sb)
{
	int c;

	if (sb->readcount + 2 > sb->cursize)
	{
		return -1;
	}

	c = (short)(sb->data[sb->readcount] | (sb->data[sb->readcount + 1] << 8));
	sb->readcount += 2;
	return c;
}

static int
CLP_SZReadLong(sof_sizebuf_t *sb)
{
	int c;

	if (sb->readcount + 4 > sb->cursize)
	{
		return -1;
	}

	c = sb->data[sb->readcount] | (sb->data[sb->readcount + 1] << 8) | (sb->data[sb->readcount + 2] << 16) |
		(sb->data[sb->readcount + 3] << 24);
	sb->readcount += 4;
	return c;
}

static void
CLP_SZReadData(sof_sizebuf_t *sb, void *dst, int len)
{
	int i;

	for (i = 0; i < len; i++)
	{
		int c = CLP_SZReadByte(sb);

		((byte *)dst)[i] = (byte)(c < 0 ? 0 : c);
	}
}

static byte *
CLP_SZGetSpace(sof_sizebuf_t *sb, int length)
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
CLP_SZWriteByte(sof_sizebuf_t *sb, int c)
{
	CLP_SZGetSpace(sb, 1)[0] = c & 0xFF;
}

static void
CLP_SZWriteShort(sof_sizebuf_t *sb, int c)
{
	byte *b = CLP_SZGetSpace(sb, 2);

	b[0] = c & 0xFF;
	b[1] = (c >> 8) & 0xFF;
}

static void
CLP_SZWriteLong(sof_sizebuf_t *sb, int c)
{
	byte *b = CLP_SZGetSpace(sb, 4);

	b[0] = c & 0xFF;
	b[1] = (c >> 8) & 0xFF;
	b[2] = (c >> 16) & 0xFF;
	b[3] = (c >> 24) & 0xFF;
}

static void
CLP_SZInit(sof_sizebuf_t *sb, void *data, int size)
{
	memset(sb, 0, sizeof(*sb));
	sb->data = data;
	sb->maxsize = size;
}

static void
CLP_SZClear(sof_sizebuf_t *sb)
{
	sb->cursize = 0;
	sb->overflowed = 0;
}

static void
CLP_SZWrite(sof_sizebuf_t *sb, const void *data, int length)
{
	if (length > 0)
	{
		memcpy(CLP_SZGetSpace(sb, length), data, length);
	}
}

/* ---- common import table ------------------------------------------------------------------------- */

static void *
CLP_CvarGet(const char *name, const char *value, int flags)
{
	return Cvar_GetModule(name, value, flags, NULL);
}

static void *
CLP_CvarSet(const char *name, const char *value)
{
	return Cvar_Set(name, value);
}

static void
CLP_BuildTables(void)
{
	memset(&clp_common, 0, sizeof(clp_common));
	clp_common.Cvar_Get = CLP_CvarGet;
	clp_common.Cvar_Set = CLP_CvarSet;
	clp_common.Cvar_SetValue = Cvar_SetValue;
	clp_common.Cmd_Argc = Cmd_Argc;
	clp_common.Cmd_Argv = Cmd_Argv;
	clp_common.Cmd_Args = Cmd_Args;
	clp_common.Com_Printf = Com_Printf;
	clp_common.Com_sprintf = Com_sprintf;
	clp_common.Com_DPrintf = Com_DPrintf;
	clp_common.FS_LoadFile = FS_SoFLoadFile;
	clp_common.FS_FreeFile = FS_FreeFile;
	clp_common.irand = irand;
	clp_common.ghl_specular = ghl_specular;
	clp_common.NewPlayerModelInfo = SV_Ghoul_NewPlayerModel;
	clp_common.LoadGhoulFile = SV_Ghoul_LoadFile;
	clp_common.ReadLine = SV_Ghoul_GetLine;
	clp_common.PreloadGSQ = SV_Ghoul_PreloadList;
	clp_common.RegisterGSQSequences = SV_Ghoul_RegisterList;
	clp_common.TurnOffPartsFromGSQ = SV_Ghoul_PartsOff;

	memset(&clp_client, 0, sizeof(clp_client));
	clp_client.GetGhoul = CLP_GetGhoul;
	clp_client.time = &clp.time;
	clp_client.FirePrimaryEffects = CLP_FirePrimary;
	clp_client.FireAlternateEffects = CLP_FireAlternate;
	clp_client.WeaponSound = CL_Player_NoteSound;
	clp_client.WeaponKillSound = CLP_KillSound;
	clp_client.WeaponEffect = CL_Player_NoteEffect;
	clp_client.ReadByte = CLP_ReadByte;
	clp_client.ReadShort = CLP_ReadShort;
	clp_client.ReadLong = CLP_ReadLong;
	clp_client.SZ_ReadByte = CLP_SZReadByte;
	clp_client.SZ_ReadShort = CLP_SZReadShort;
	clp_client.SZ_ReadLong = CLP_SZReadLong;
	clp_client.SZ_ReadData = CLP_SZReadData;
	clp_client.SZ_WriteByte = CLP_SZWriteByte;
	clp_client.SZ_WriteShort = CLP_SZWriteShort;
	clp_client.SZ_WriteLong = CLP_SZWriteLong;
	clp_client.SZ_Init = CLP_SZInit;
	clp_client.SZ_Clear = CLP_SZClear;
	clp_client.SZ_Write = CLP_SZWrite;
}

/* ---- view-weapon notes (SPEC-003 client imports 2/3/4/6, SPEC-400 "View weapon") -------------------- */

/* client import 4: note text `name [n|*]`: `<name>.wav`, `<name><random 1..n>.wav`, or `<name>.wav` at a random
   volume for `*`; on the player's own entity, channel 1 (weapon), volume 0.6, no attenuation (observed) */
void
CL_Player_NoteSound(const char *note)
{
	char name[MAX_QPATH], file[MAX_QPATH];
	const char *rest;
	struct sfx_s *sfx;
	float vol = CLP_NOTE_VOLUME;
	int i = 0, n = 0;

	if (!note)
	{
		return;
	}

	while (note[i] && (note[i] != ' ') && (i < (int)sizeof(name) - 1))
	{
		name[i] = note[i];
		i++;
	}

	name[i] = 0;
	rest = note + i;

	while (*rest == ' ')
	{
		rest++;
	}

	if (!name[0])
	{
		return;
	}

	if ((*rest >= '0') && (*rest <= '9'))
	{
		n = atoi(rest);
	}

	if (n > 0)
	{
		Com_sprintf(file, sizeof(file), "%s%d.wav", name, irand(1, n));
	}
	else
	{
		Com_sprintf(file, sizeof(file), "%s.wav", name);
	}

	if (*rest == '*')
	{
		/* random volume: range not specified (OPEN_QUESTIONS I1d); measured on the original: see I1d.md */
		vol = flrand(0.3f, CLP_NOTE_VOLUME);
	}

	if (om_cpltrace && om_cpltrace->value)
	{
		Com_Printf("clplayer: %.3f note sound \"%s\" -> %s vol %.3f\n", CL_Ghoul_GameClock(), note, file, vol);
	}

	sfx = S_RegisterSound(file);

	if (sfx)
	{
		S_StartSound(NULL, cl.playernum + 1, CHAN_WEAPON, sfx, vol, ATTN_NONE, 0);
	}
}

/* the view weapon is drawn this frame (SPEC-310 section 13) */
static qboolean
CLP_ViewWeaponShown(void)
{
	const sofps_t *x = CL_SoF_CurrentPS();

	return CL_Ghoul_Active() && cl_gun->value && !x->remote_type && x->gun_id &&
		(cl.frame.playerstate.fov <= 95);
}

/* client import 6: effect `name [bolt]` on the view model (SPEC-400 "View weapon" 1) */
void
CL_Player_NoteEffect(const char *note)
{
	if (om_cpltrace && om_cpltrace->value)
	{
		Com_Printf("clplayer: %.3f note effect \"%s\" (shown %d, fov %g)\n", CL_Ghoul_GameClock(), note ? note : "",
				CLP_ViewWeaponShown(), CL_SoF_GameFov());
	}

	/* the player-state fov, not the horplus-widened render fov: the note is ignored unless it is
	   95, which a 16:9 window never renders (no muzzle flash and no fire sound, ENG2) */
	CLFX_ViewWeaponNote(note, CLP_ViewWeaponShown(), CL_SoF_GameFov());
}

/* client imports 2/3: the predicted fire effects with code = weapon id - 1 (primary) or weapon id + 11
   (alternate) (SPEC-400 "View weapon" 2); weapon < 0: the weapon of the player state's view weapon */
void
CL_Player_FireEffects(int weapon, int alternate)
{
	if (weapon < 0)
	{
		weapon = CL_SoF_CurrentPS()->gun_type;
	}

	if (om_cpltrace && om_cpltrace->value)
	{
		Com_Printf("clplayer: %.3f %s weapon %d\n", CL_Ghoul_GameClock(), alternate ? "altfire" : "fire", weapon);
	}

	if (weapon > 0)
	{
		CLFX_PredictedFire(alternate ? weapon + 11 : weapon - 1);
	}
}

/* the engine's own handlers on the replicated view weapon, while the client does not predict */
static void
CLP_EngineViewNotes(void)
{
	const sofps_t *x = CL_SoF_CurrentPS();
	void *inst = NULL;

	if ((cls.state == ca_active) && x->gun_id)
	{
		inst = CLG_FindInstance(x->gun_id);
	}

	if (!inst)
	{
		if (clp.viewhandle && om_cpltrace && om_cpltrace->value)
		{
			Com_Printf("clplayer: %.3f view weapon %d gone (player state %d, weapon %d)\n", CL_Ghoul_GameClock(),
					clp.viewhandle, x->gun_id, x->gun_type);
		}

		CLP_ViewNotesAttach(NULL);
		clp.viewhandle = 0;
		return;
	}

	if (x->gun_id != clp.viewhandle)
	{
		if (om_cpltrace && om_cpltrace->value)
		{
			Com_Printf("clplayer: %.3f view weapon %d (weapon %d) -> %d (weapon %d)\n", CL_Ghoul_GameClock(),
					clp.viewhandle, clp.viewtype, x->gun_id, x->gun_type);
		}

		CLP_ViewNotesAttach(NULL);
		clp.viewhandle = x->gun_id;
		clp.viewtype = x->gun_type;
	}

	CLP_ViewNotesAttach(inst);
	/* up to the game clock + 0.001 s, the limit the module's own weapon think uses (SPEC-600 Behaviour 2);
	   derived from the original's delivery frames (pistol1 reload_b notes at 66.7 / 400 / 666.7 ms arrive 325
	   and 275 ms apart at 25-ms frames, which needs a limit just past the frame's clock; I1d.md o1) */
	CLP_ViewNotesProcess(inst, CL_Ghoul_GameClock() + 0.001f);
}

/* ---- session ------------------------------------------------------------------------------------------ */

static void
CLP_SetContext(void)
{
	clp.time = CL_Ghoul_GameClock();

	if (clp.api && clp.api->context)
	{
		*clp.api->context = -1;
	}
}

/* a fresh prediction inventory (all defaults); on a non-local connection the cached in-view models are
   forgotten as well (export 8) */
static void
CLP_NewInventory(void)
{
	if (!clp.api)
	{
		return;
	}

	CLP_SetContext();

	if (clp.inv)
	{
		clp.api->DeleteInventory(clp.inv);
		clp.inv = NULL;
	}

	clp.inv = clp.api->NewInventory();

	if (!clp.local && clp.api->ForgetViewWeapons)
	{
		clp.api->ForgetViewWeapons();
	}
}

static void
CLP_EndSession(void)
{
	sof_player_export_t *api = clp.api;

	CLP_ViewNotesAttach(NULL);
	clp.viewhandle = 0;

	if (api)
	{
		CLP_SetContext();

		if (clp.inv)
		{
			api->DeleteInventory(clp.inv);
		}

		if (api->Shutdown)
		{
			api->Shutdown();
		}

		clp.api = NULL;
		clp.inv = NULL;
		SV_UnloadPlayerModule(1);
	}

	clp.session = false;
}

/* sv_player.c: the server side goes away first; the client deletes its inventory and drops its reference */
static void
CLP_ServerSideUnloaded(void)
{
	if (clp.api)
	{
		CLP_EndSession();
	}
}

static void
CLP_StartSession(void)
{
	sof_player_export_t *api;

	clp.session = true;
	clp.local = !Q_stricmp(cls.servername, "localhost") || CL_Demo_Playing();
	clp.resync = 0;
	clp.viewhandle = 0;

	/* SPEC-003 "Client prediction": from cl_predict, and only when the server is not localhost; the client
	   does not predict yet (SPEC-602 predictor not hosted), so it reports 0 (a predicting client must send
	   fire times, the game waits for them) */
	Cvar_ForceSet("predicting", "0");

	if (!om_clplayer->value)
	{
		return;
	}

	CLP_BuildTables();
	api = SV_LoadPlayerModule(&clp_common, &clp_client, NULL, -1);

	if (!api)
	{
		Com_Error(ERR_FATAL, "Client failed to load player DLL");
	}

	if (api->version != SOF_PLAYER_API_VERSION)
	{
		SV_UnloadPlayerModule(1);
		Com_Error(ERR_FATAL, "player is version %i, not %i", api->version, SOF_PLAYER_API_VERSION);
	}

	clp.api = api;

	if (api->Init)
	{
		api->Init();
	}

	CLP_NewInventory();
}

/* ---- engine hooks ---------------------------------------------------------------------------------- */

void
CL_Player_Init(void)
{
	om_clplayer = Cvar_Get("om_clplayer", "1", 0);
	om_cpltrace = Cvar_Get("om_cpltrace", "0", 0);
	ghl_specular = Cvar_Get("ghl_specular", "1", CVAR_ARCHIVE);
	sv_player_clientdrop = CLP_ServerSideUnloaded;
}

void
CL_Player_Shutdown(void)
{
	CLP_EndSession();
	sv_player_clientdrop = NULL;
}

void
CL_Player_ConnectionStarting(void)
{
	if (!clp.session)
	{
		CLP_StartSession();
	}
}

void
CL_Player_Disconnect(void)
{
	CLP_EndSession();
}

void
CL_Player_FrameStart(void)
{
	/* demo playback (no connect packet) */
	if (!clp.session && (cls.state >= ca_connected))
	{
		CLP_StartSession();
	}

	if (!clp.api)
	{
		return;
	}

	CLP_SetContext();
}

void
CL_Player_Frame(void)
{
	if (!clp.api)
	{
		return;
	}

	CLP_SetContext();

	if (!CL_Player_Predicting())
	{
		CLP_EngineViewNotes();
	}
}

int
CL_Player_Predicting(void)
{
	return 0;
}

void *
CL_Player_PredictedViewInstance(void)
{
	return NULL;
}

void
CL_Player_CommandFireTimes(float *primary, float *alternate)
{
	*primary = 0;
	*alternate = 0;
}

/* ---- svc 28 / 29 / 30 ------------------------------------------------------------------------------ */

/* skips an event block without a client side (the old parser of cl_sof.c) */
static void
CLP_SkipEvents(void)
{
	int count, h, i;

	count = MSG_ReadByte(&net_message);

	for (i = 0; i < count; i++)
	{
		h = MSG_ReadByte(&net_message);

		if ((h & 0x0F) <= 4)
		{
			switch ((h >> 4) & 3)
			{
				case 0: MSG_ReadByte(&net_message); break;
				case 1: MSG_ReadShort(&net_message); break;
				case 2: MSG_ReadShort(&net_message); MSG_ReadByte(&net_message); break;
				default: MSG_ReadLong(&net_message); break;
			}
		}
	}
}

void
CL_Player_ParseEvents(void)
{
	if (!clp.inv)
	{
		CLP_SkipEvents();
		return;
	}

	CLP_SetContext();
	CLP_InvReadEvents(clp.inv);

	if (!CL_Player_Predicting())
	{
		CLP_InvApplyEvents(clp.inv);
		CLP_InvClearEvents(clp.inv);
	}
}

void
CL_Player_ParseRestart(void)
{
	int n = MSG_ReadByte(&net_message);

	if (om_cpltrace && om_cpltrace->value)
	{
		Com_Printf("clplayer: restart prediction %d\n", n);
	}

	clp.resync = n;
	CLP_NewInventory();
}

void
CL_Player_ParseFullState(void)
{
	sof_sizebuf_t sb;
	int start = net_message.readcount;

	if (clp.inv && (start + CLP_STATE_BYTES <= net_message.cursize))
	{
		memset(&sb, 0, sizeof(sb));
		sb.data = net_message.data;
		sb.maxsize = net_message.cursize;
		sb.cursize = net_message.cursize;
		sb.readcount = start;
		CLP_SetContext();
		CLP_InvReadFullState(clp.inv, &sb);
	}

	net_message.readcount = start + CLP_STATE_BYTES;
}
