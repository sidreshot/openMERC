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
 * Main header file of the server.
 *
 * OpenMerc: the server of Soldier of Fortune 1.07f. It hosts game modules
 * with the SoF interface (the original gamex86.dll or the OpenMerc game),
 * speaks network protocol 33 and drives the Ghoul model system's network
 * replication. Derived from the Quake II / Yamagi Quake II server.
 *
 * Implements: SPEC-001, SPEC-003, SPEC-005, SPEC-010, SPEC-011, SPEC-013,
 *             SPEC-200, SPEC-201, SPEC-202, SPEC-203, SPEC-313
 *
 * =======================================================================
 */

#ifndef SV_SERVER_H
#define SV_SERVER_H

#include "../../common/header/common.h"
#include "../../common/header/sof/game_api.h"

/* the server's view of a game entity: the SPEC-760 prefix */
typedef sof_edict_t edict_t;

#define SV_PROTOCOL_VERSION 33

#define LATENCY_COUNTS 16
#define RATE_MESSAGES 10

#define MAX_CHALLENGES 1024
#define MAX_SAVE_TOKEN_CHARS 128

/* SPEC-200: network games never send more than 1400 bytes */
/* SPEC-200 "Packet size limit": 1400 in network games, 16384 in single player
   (net_maxmsglen, set by SV_InitGame); buffers are sized for the larger */
#define SV_MAX_DATAGRAM MAX_MSGLEN_LOCAL
#define SV_DATAGRAM_LIMIT net_maxmsglen
#define SV_OUTPUTBUF_LENGTH (MAX_MSGLEN - 16) /* rcon/status replies (out of band) */
/* SPEC-203: signon batches stop at half the maximum datagram (700 / 8192) */
#define SV_SIGNON_BATCH (net_maxmsglen / 2)

#define EDICT_NUM(n) ((edict_t *)((byte *)ge->edicts + ge->edict_size * (n)))
#define NUM_FOR_EDICT(e) ((int)(((byte *)(e) - (byte *)ge->edicts) / ge->edict_size))
#define CL_EDICT(cl) EDICT_NUM(1 + ((cl) - svs.clients))
#define CLNUM_EDICT(i) EDICT_NUM((i) + 1)

/* server -> client message ids (SPEC-201) */
enum
{
	sofsvc_bad, sofsvc_temp_entity, sofsvc_layout, sofsvc_unused, sofsvc_sound_info,
	sofsvc_effect, sofsvc_equip, sofsvc_nop, sofsvc_disconnect, sofsvc_reconnect,
	sofsvc_sound, sofsvc_print, sofsvc_nameprint, sofsvc_stufftext, sofsvc_serverdata,
	sofsvc_configstring, sofsvc_spawnbaseline, sofsvc_centerprint, sofsvc_captionprint,
	sofsvc_download, sofsvc_playerinfo, sofsvc_packetentities, sofsvc_deltapacketentities,
	sofsvc_frame, sofsvc_culledevent, sofsvc_damagetexture, sofsvc_ghoulreliable,
	sofsvc_ghoulunreliable, sofsvc_ric, sofsvc_restart_predn, sofsvc_rebuild_pred_inv,
	sofsvc_countdown, sofsvc_cinprintf, sofsvc_playernamecols, sofsvc_sp_print,
	sofsvc_removeconfigstring, sofsvc_sp_print_data_1, sofsvc_sp_print_data_2,
	sofsvc_welcomeprint, sofsvc_sp_print_obit, sofsvc_force_con_notify
};

/* string-package ids the engine sends (SPEC-201 s8, package 4 "general") */
#define SV_STR_KICKED 0x040A
#define SV_STR_TIMEDOUT 0x040B
#define SV_STR_OVERFLOWED 0x040C
#define SV_STR_DISCONNECTED 0x040D
#define SV_STR_BADREAD 0x040E
#define SV_STR_BADCOMMAND 0x040F
#define SV_STR_SERVERQUIT 0x0410
#define SV_STR_INSERTCD 0x0411

typedef enum
{
	ss_dead,            /* no map loaded */
	ss_loading,         /* spawning level edicts */
	ss_game,            /* actively running */
	ss_cinematic,
	ss_demo,
	ss_pic
} server_state_t;

typedef struct
{
	server_state_t state;       /* precache commands are only valid during load */

	qboolean attractloop;       /* running cinematics and demos for the local system only */
	qboolean loadgame;          /* client begins should reuse existing entity */

	unsigned time;              /* always sv.framenum * 100 msec */
	int framenum;               /* frame number on the wire */
	int gameframe;              /* counter passed to the game's RunFrame */
	float gametime;             /* the game's level time, returned by RunFrame */

	char name[MAX_QPATH];       /* map name, or cinematic name */
	struct cmodel_s *models[SOF_MAX_MODELS];

	char configstrings[OM_MAX_CONFIGSTRINGS][SOF_CS_LENGTH]; /* SoF layout + sound extension */
	sof_entity_state_t baselines[SOF_MAX_EDICTS];

	/* the multicast buffer is used to send a message to a set of clients
	   it is only used to marshall data until SV_Multicast is called */
	sizebuf_t multicast;
	byte multicast_buf[MAX_MSGLEN];

	/* texture-information records of the map (SPEC-313 s4) */
	sof_surface_t *surfaces;
	int numsurfaces;
	int *surfacedamage;         /* accumulated damage per record */
	int *surfacestage;          /* damage stage per record */

	/* demo server information */
	fileHandle_t demofile;
	qboolean timedemo;          /* don't time sync */
} server_t;

typedef enum
{
	cs_free,        /* can be reused for a new connection */
	cs_zombie,      /* client has been disconnected, but don't reuse
					   connection for a couple seconds */
	cs_connected,   /* has been assigned to a client_t, but not in game yet */
	cs_spawned      /* client is fully in game */
} client_state_t;

typedef struct
{
	int areabytes;
	byte areabits[32];                  /* portalarea visibility bits (SPEC-202: at most 32) */
	sof_player_state_t ps;
	int gunhandle;                      /* Ghoul handle of ps.gun for this client */
	int num_entities;
	int first_entity;                   /* into the circular svs.client_entities[] */
	int senttime;                       /* for ping calculations */
} client_frame_t;

#define SV_MAX_CULLED 32

typedef struct
{
	int event, parm;
	int distance;
	vec3_t dir;
} sv_culledevent_t;

typedef struct client_s
{
	client_state_t state;

	char userinfo[MAX_INFO_STRING];     /* name, etc */

	int lastframe;                      /* for delta compression */
	sof_usercmd_t lastcmd;              /* for filling in big drops */

	int commandMsec;                    /* every seconds this is reset, if user */
										/* commands exhaust it, assume time cheating */

	int frame_latency[LATENCY_COUNTS];
	int ping;

	int message_size[RATE_MESSAGES];    /* used to rate drop packets */
	int rate;
	int surpressCount;                  /* number of messages rate supressed */

	char name[32];                      /* extracted from userinfo, high bits masked */
	int messagelevel;                   /* for filtering printed messages */
	float cdvalue;                      /* SPEC-200: cd value / 1.35 * 4 */

	/* The datagram is written to by sound calls, prints,
	   temp ents, etc. It can be harmlessly overflowed. */
	sizebuf_t datagram;
	byte datagram_buf[SV_MAX_DATAGRAM];

	client_frame_t frames[UPDATE_BACKUP];     /* updates can be delta'd from here */

	/* events of entities not sent this frame (SPEC-202 s3.1) */
	sv_culledevent_t culled[SV_MAX_CULLED];
	int numculled;

	byte *download;                     /* file being downloaded */
	int downloadsize;                   /* total bytes (can't use EOF because of paks) */
	int downloadcount;                  /* bytes sent */

	int lastmessage;                    /* sv.framenum when packet was last received */
	int lastconnect;

	int challenge;                      /* challenge of this user, randomly generated */
	qboolean entered;                   /* ClientBegin ran for this connection */
	qboolean ghoulknown;                /* the Ghoul system was told about this client */

	netchan_t netchan;
} client_t;

typedef struct
{
	netadr_t adr;
	int challenge;
	int time;
} challenge_t;

typedef struct
{
	qboolean initialized;               /* sv_init has completed */
	int realtime;                       /* always increasing, no clamping, etc */

	char mapcmd[MAX_SAVE_TOKEN_CHARS];  /* ie: *intro.cin+base */

	int spawncount;                     /* SPEC-203 <sc>; used to check late spawns */

	client_t *clients;                  /* [maxclients->value]; */
	int numclients;                     /* maxclients when the game started */
	int num_client_entities;            /* maxclients->value*UPDATE_BACKUP*MAX_PACKET_ENTITIES */
	int next_client_entities;           /* next client_entity to use */
	sof_entity_state_t *client_entities; /* [num_client_entities] */

	challenge_t challenges[MAX_CHALLENGES];    /* to prevent invalid IPs from connecting */

	/* serverrecord values */
	FILE *demofile;
	sizebuf_t demo_multicast;
	byte demo_multicast_buf[MAX_MSGLEN];
} server_static_t;

extern netadr_t net_from;
extern sizebuf_t net_message;

extern server_static_t svs;                 /* persistant server info */
extern server_t sv;                         /* local server */

extern cvar_t *sv_paused;
extern cvar_t *maxclients;
extern cvar_t *sv_noreload;
extern cvar_t *sv_enforcetime;
extern cvar_t *sv_reconnect_limit;
extern cvar_t *sv_max_ghoul_mp;
extern cvar_t *sv_ghoul_stub;
extern cvar_t *sv_hostdebug;
extern cvar_t *hostname;
extern cvar_t *public_server;
extern cvar_t *allow_download;
extern cvar_t *allow_download_maps;
extern cvar_t *allow_download_sounds;
extern cvar_t *allow_download_stringpackage;
extern cvar_t *rcon_password;
extern cvar_t *timeout;
extern cvar_t *zombietime;

extern client_t *sv_client;
extern edict_t *sv_player;

/* ---- sv_main.c ---- */
void SV_FinalMessage(const char *message, qboolean reconnect);
void SV_DropClient(client_t *drop);
void SV_DropClientWithReason(client_t *drop, int stringid);
void SV_UserinfoChanged(client_t *cl, qboolean ingame);
char *SV_StatusString(void);
int SV_ClientScore(client_t *cl);
const char *SV_GameTypeName(void);
void Master_Heartbeat(void);
void SV_GameSpy_ForceHeartbeat(void);

/* ---- sv_init.c ---- */
int SV_FindIndex(const char *name, int start, int max, const char *cls);
int SV_ModelIndex(const char *name);
int SV_SoundIndex(const char *name);
int SV_EffectIndex(const char *name);
int SV_ImageIndex(const char *name);
int SV_GhoulFileIndex(const char *name);
int SV_InlineModelFaceFlags(int n);
int SV_StringPackageIndex(const char *name);
void SV_RemoveSound(const char *name);
void SV_SetConfigstring(int index, const char *value);
void SV_InitGame(void);
void SV_Map(qboolean attractloop, const char *levelstring, qboolean loadgame);
void SV_ClearBaselines(void);

/* ---- sv_game.c ---- */
extern sof_game_export_t *ge;
void SV_InitGameProgs(void);
void SV_ShutdownGameProgs(void);
void SV_ShutdownGameAndGhoul(qboolean ghoul, qboolean reconnecting);
unsigned SV_FileChecksum(const char *path);      /* sv_player.c, SPEC-301 s8 */
unsigned SV_GameModuleChecksum(void);              /* sv_game.c */
unsigned SV_PlayerModuleChecksum(void);            /* sv_player.c */
qboolean SV_GameModuleIsOpenMerc(void);            /* sv_game.c, marker export */
qboolean SV_PlayerModuleIsOpenMerc(void);          /* sv_player.c, marker export */
void SV_SetPureViolenceBit(void);                 /* sv_init.c, GS.15 */

/* sv_watchdog.c: stall reports (test aid, cvar sv_watchdog) */
void SV_Watchdog_Init(void);
void SV_Watchdog_Beat(const char *where);
void SV_Watchdog_Mark(const char *where);
void SV_Watchdog_Disarm(void);

/* sv_imptrace.c: ring of the last game-import calls (test aid) */
void SV_ImportTrace_Install(void *table);
void SV_ImportTrace_Dump(void *file); /* FILE * */
void SV_ImportTrace_HookVtable(int iface, void *object);
void SV_ImportTrace_ScanEdicts(void);
void SV_World_DumpLastFilter(void *file); /* FILE * */
void SV_Ghoul_HookTraceInstance(void *inst);
int SV_GameCinematicState(void);
void SV_GameClientCommand(edict_t *ent); /* context flag 0 around it, SPEC-602 s7 */
void SV_GameClientThink(edict_t *ent, sof_usercmd_t *cmd);
void SV_GameRunFrame(int counter);
void SV_GameCallWithPlayerContext(void (*fn)(void));

/* ---- sv_player.c (player module loader, SPEC-003) ---- */
void *SV_LoadPlayerModule(void *common, void *client, void *server, int isclient);
void SV_UnloadPlayerModule(int isclient);
void SV_ShutdownPlayerModule(void);
extern int *sv_playercontext;           /* game import #67 target */

/* ---- sv_ghoul.cpp (Ghoul services, SPEC-010/011/013) ---- */
void SV_Ghoul_Init(void);
void SV_Ghoul_SelectForGame(void);
void SV_Ghoul_Command(int argc, const char *const *argv);
void SV_Ghoul_Shutdown(void);
void *SV_Ghoul_System(void);            /* ghoul::IGhoulSystem * of the server role */
void *SV_Ghoul_NewPlayerModel(const char *file);
int SV_Ghoul_LoadFile(const char *folder, const char *file, void **buffer);
int SV_Ghoul_GetLine(int *remaining, char **cursor, char *line);
void SV_Ghoul_PreloadList(const char *folder, const char *name);
int SV_Ghoul_RegisterList(const char *folder, const char *name, void *object);
void SV_Ghoul_PartsOff(const char *folder, const char *name, void *object, void *instance);
void SV_Ghoul_ServerSpawn(qboolean clientsconnected);
void SV_Ghoul_ServerDropAll(void);     /* server method 15 */
void SV_Ghoul_ServerReset(void);       /* server method 8 */
void SV_Ghoul_ClientConnected(int clientnum);
void SV_Ghoul_ClientDropped(int clientnum);
qboolean SV_Ghoul_ReliablePending(int clientnum);
int SV_Ghoul_WriteReliable(int clientnum, byte *out, int budget, qboolean *done);
void SV_Ghoul_BeginFrame(int clientnum, int frame);
int SV_Ghoul_Offer(void *inst, float priority);
int SV_Ghoul_WriteUnreliable(int clientnum, int frame, float time, byte *out, int budget);
void SV_Ghoul_CommitFrame(int clientnum, int frame);
void SV_Ghoul_Acknowledge(int clientnum, int frame);
void SV_Ghoul_TouchFile(const char *path);
void SV_Ghoul_RegisterAllFiles(void);   /* game import #6 */
void SV_Ghoul_StatsPrint(void);         /* gserver STATS */

/* ---- sv_world.c ---- */
void SV_ClearWorld(void);
void SV_UnlinkEdict(edict_t *ent);
void SV_LinkEdict(edict_t *ent);
int SV_AreaEdicts(const vec3_t mins, const vec3_t maxs, edict_t **list,
		int maxcount, int areatype);
int SV_PointContents(const vec3_t p);
void SV_TraceSoF(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, const edict_t *passedict, int contentmask, sof_trace_t *out);
void SV_TraceFiltered(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, const edict_t *passedict, int contentmask, sof_trace_t *out,
		sof_tracefilter_t filter);
float SV_RegionDistance(const vec3_t p);
qboolean SV_InPVS(const vec3_t p1, const vec3_t p2);
qboolean SV_InPHS(const vec3_t p1, const vec3_t p2);
void SV_LoadMapExtras(const char *mapfile);   /* map check string, texinfo records */
void SV_FreeMapExtras(void);
int SV_SurfaceIndex(const sof_surface_t *s);
sof_surface_t *SV_SurfaceFromQ2(const csurface_t *s);
int SV_DamageTexture(sof_surface_t *surface, int damage);
int SV_MaterialList(byte *buf, int capacity);
void SV_TraceTest_f(void);

/* ---- sv_pmove.c ---- */
void SV_Pmove(sof_pmove_t *pm);
extern int sv_pmove_snaps;

/* ---- sv_msg.c (SoF primitives) ---- */
void SV_WriteCoord(sizebuf_t *sb, float f);
void SV_WritePos(sizebuf_t *sb, const vec3_t pos);
void SV_WriteAngle(sizebuf_t *sb, float f);
void SV_WriteAngle16(sizebuf_t *sb, float f);
int SV_DirToByte(const vec3_t dir);
void SV_WriteDir(sizebuf_t *sb, const vec3_t dir);
void SV_WriteConfigstringMsg(sizebuf_t *sb, int index, const char *s);
int SV_ConfigstringMsgSize(const char *s);
int SV_ConfigstringDictCode(const char *s);
int SV_MoveChecksum(const byte *region, int len, unsigned sequence);
void SV_InitTables(void);

/* ---- sv_entities.c ---- */
void SV_BuildClientFrame(client_t *client);
void SV_WriteFrameToClient(client_t *client, sizebuf_t *msg);
void SV_WriteDeltaEntity(const sof_entity_state_t *from, const sof_entity_state_t *to,
		sizebuf_t *msg, qboolean force, qboolean newentity);
int SV_DeltaEntitySize(const sof_entity_state_t *from, const sof_entity_state_t *to);
qboolean SV_BaselineWanted(const sof_entity_state_t *s);
void SV_RecordDemoMessage(void);
void SV_ClearEntityEvents(void);

/* ---- sv_send.c ---- */
typedef enum {RD_NONE, RD_CLIENT, RD_PACKET} redirect_t;
extern char sv_outputbuf[SV_OUTPUTBUF_LENGTH];
void SV_FlushRedirect(int sv_redirected, char *outputbuf);
void SV_SendClientMessages(void);
void SV_SendPrepClientMessages(void);
void SV_Multicast(const vec3_t origin, int to, int excludeent);
void SV_Unicast(const edict_t *ent, qboolean reliable);
void SV_StartSound(const vec3_t origin, const edict_t *entity, int channel,
		int soundindex, float volume, float attenuation, float timeofs, int local);
void SV_ClientPrintf(client_t *cl, int level, const char *fmt, ...);
void SV_BroadcastPrintf(int level, const char *fmt, ...);
void SV_BroadcastCommand(const char *fmt, ...);
qboolean SV_ClientIsBot(const client_t *cl);

/* ---- sv_strings.c (string-package prints, SPEC-131/313 s1 server side) ---- */
void SV_SP_Init(void);
void SV_SP_Shutdown(void);
void SV_SP_Register(const char *package);
void SV_SP_Release(void);
const char *SV_SP_GetText(int id);
int SV_SP_PackArgs(int id, va_list args, byte *out, int capacity);
int SV_SP_Format(char *buf, int capacity, int id, va_list args);
void SV_SP_Print(const edict_t *ent, int id, va_list args, qboolean obituary);
void SV_SP_PrintClient(client_t *cl, int id);

/* ---- sv_user.c ---- */
void SV_Nextserver(void);
void SV_ExecuteClientMessage(client_t *cl);
void SV_ExecuteUserCommand(char *s);

/* ---- sv_cmd.c ---- */
void SV_InitOperatorCommands(void);

/* ---- sv_save.c ---- */
void SV_InitSaveHost(void);
/* OpenMerc timed autosave: after every game frame that ran (not paused),
   and once at the end of every server frame */
void SV_AutoSaveGameFrame(void);
void SV_AutoSaveFrame(void);
int SV_WriteChunk(int id, const void *data, int length);
int SV_ReadChunk(int id, void *buffer, int length, void **allocated);

/* ---- sv_conless.c ---- */
void SV_ConnectionlessPacket(void);
qboolean SV_IsLANAddress(netadr_t adr);

/* host debugging (cvar sv_hostdebug bits) */
#define SV_DBG_IMPORTS 1
#define SV_DBG_GHOUL 2
#define SV_DBG_PMOVE 4
#define SV_DBG_NET 8
#define SV_DBG_THINK 64 /* ClientThink calls, begin, cinematic state (I1b timeline) */
void SV_HostDebug(int bit, const char *fmt, ...);

#endif
