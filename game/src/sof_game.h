// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the engine <-> game interface of Soldier of Fortune 1.07f.
//
// Game-side copy of the SPEC-001 structures (import table 99 entries / 396 bytes, export table 28
// fields / 112 bytes), the engine-visible entity prefix (SPEC-760 1), the trace result (SPEC-313) and
// the message buffer (SPEC-003). The in-memory layouts of the entity state, player state, user command
// and player-movement block are not specified (OPEN_QUESTIONS G18-1..4); they live in sof_layout.h.
//
// Implements: SPEC-001, SPEC-313 (trace result), SPEC-760 (entity prefix), SPEC-003 (sizebuf)
#pragma once

#include <stddef.h>
#include <stdint.h>

#define GAME_API_VERSION 3

typedef float vec3_t[3];
typedef int qboolean;

struct edict_s;
struct gclient_s;
typedef struct edict_s edict_t;
typedef struct gclient_s gclient_t;
struct cvar_s;
typedef struct cvar_s cvar_t;

namespace ghoul {
class IGhoulSystem;
class IGhoulObject;
class IGhoulInst;
class IGhoulPlayerModel;
}

// SPEC-003: message buffer, 20 bytes
typedef struct sizebuf_s {
	uint8_t allowoverflow;
	uint8_t overflowed;
	uint8_t pad[2];
	uint8_t *data;
	int maxsize;
	int cursize;
	int readcount;
} sizebuf_t;

// SPEC-313: plane inside the trace result, 20 bytes
typedef struct cplane_s {
	vec3_t normal;
	float dist;
	uint8_t type;
	uint8_t signbits;
	uint8_t pad[2];
} cplane_t;

// SPEC-313: trace result, 56 bytes
struct csurface_s;
typedef struct csurface_s csurface_t;
typedef struct trace_s {
	uint8_t allsolid;      // 0
	uint8_t startsolid;    // 1
	uint8_t pad[2];
	float fraction;        // 4
	vec3_t endpos;         // 8
	cplane_t plane;        // 20
	csurface_t *surface;   // 40 texture-information record
	int contents;          // 44
	edict_t *ent;          // 48
	int leafnum;           // 52
} trace_t;

// SPEC-001 #53 filter: accept (non-zero) or reject a hit
typedef int (*tracefilter_t)(trace_t *tr);

typedef void (*cvarcallback_t)(cvar_t *cv);

// ---------------------------------------------------------------------------------------------
// Import table (engine -> game), SPEC-001
// ---------------------------------------------------------------------------------------------
typedef struct game_import_s {
	int (*modelindex)(const char *name);                                         // 0
	int (*soundindex)(const char *name);                                         // 1
	int (*effectindex)(const char *name);                                        // 2
	int (*imageindex)(const char *name);                                         // 3
	void (*soundremove)(const char *name);                                       // 4
	int (*addressbanned)(const char *address);                                   // 5
	void (*ghoulregisterall)(void);                                              // 6
	void (*setmodel)(edict_t *ent, const char *name);                            // 7
	void (*setrendermodel)(edict_t *ent, const char *name);                      // 8 (unused)
	int (*argc)(void);                                                           // 9
	char *(*argv)(int n);                                                        // 10
	char *(*args)(void);                                                         // 11
	void (*bprintf)(int printlevel, const char *fmt, ...);                       // 12
	void (*dprintf)(const char *fmt, ...);                                       // 13
	void (*cprintf)(edict_t *ent, int printlevel, const char *fmt, ...);         // 14
	void (*clprintf)(edict_t *to, edict_t *from, int printlevel, const char *fmt, ...); // 15
	void (*welcomeprint)(edict_t *ent);                                          // 16
	void (*centerprintf)(edict_t *ent, const char *fmt, ...);                    // 17
	void (*cinprintf)(edict_t *ent, int x, int y, int textspeed, const char *text); // 18 (unused)
	void (*captionprint)(int printlevel, int id);                                // 19 (unused)
	void (*captionprintent)(edict_t *ent, int id);                               // 20 (unused)
	void (*forceconnotify)(void);                                                // 21
	void (*sound)(edict_t *ent, int channel, int soundindex, float volume, float attenuation,
	              float timeofs, int local);                                     // 22
	void (*positioned_sound)(const float *origin, edict_t *ent, int channel, int soundindex,
	                         float volume, float attenuation, float timeofs, int local); // 23
	void *unused24;                                                              // 24
	int (*damagetexture)(csurface_t *surface, int damage);                       // 25
	int (*getmaterials)(uint8_t *buf, int capacity);                             // 26
	void (*loadingprogress)(float fraction, int flag);                           // 27
	void (*multicast)(const float *origin, int to);                              // 28
	void (*multicastexclude)(const float *origin, int to, int excluded);         // 29
	void (*unicast)(edict_t *ent, qboolean reliable);                            // 30
	void (*WriteChar)(int c);                                                    // 31
	void (*WriteByte)(int c);                                                    // 32
	void (*WriteShort)(int c);                                                   // 33
	void (*WriteLong)(int c);                                                    // 34
	void (*WriteFloat)(float f);                                                 // 35
	void (*WriteString)(const char *s);                                          // 36
	void (*WritePosition)(const float *pos);                                     // 37
	void (*WriteDir)(const float *dir);                                          // 38
	void (*WriteAngle)(float f);                                                 // 39
	void (*SZ_WriteByte)(sizebuf_t *sb, int c);                                  // 40
	void (*SZ_WriteShort)(sizebuf_t *sb, int c);                                 // 41
	void (*SZ_WriteLong)(sizebuf_t *sb, int c);                                  // 42
	void (*ReliableWriteByteToClient)(int c, int clientnum);                     // 43
	void (*ReliableWriteDataToClient)(const void *data, int length, int clientnum); // 44
	int (*DirToByte)(const float *dir);                                          // 45 (unused)
	void (*SendPlayernameColors)(edict_t *ent, int length, int count, const void *data); // 46
	void (*SP_Register)(const char *package);                                    // 47
	void (*SP_Print)(edict_t *ent, int id, ...);                                 // 48
	void (*SP_Print_Obit)(edict_t *ent, int id, ...);                            // 49
	int (*SP_SPrint)(char *buf, int capacity, int id, ...);                      // 50
	const char *(*SP_GetStringText)(int id);                                     // 51
	int (*trace)(const float *start, const float *mins, const float *maxs, const float *end,
	             edict_t *passent, int contentmask, trace_t *out);               // 52
	int (*tracefilter)(const float *start, const float *mins, const float *maxs, const float *end,
	                   edict_t *passent, int contentmask, trace_t *out, tracefilter_t filter); // 53
	int (*pointcontents)(const float *point);                                    // 54
	float (*regiondistance)(const float *point);                                 // 55
	int (*inPVS)(const float *p1, const float *p2);                              // 56
	int (*inPHS)(const float *p1, const float *p2);                              // 57 (unused)
	void (*SetAreaPortalState)(int portalnum, qboolean open);                    // 58
	int (*AreasConnected)(int area1, int area2);                                 // 59 (unused)
	ghoul::IGhoulSystem *(*GetGhoul)(void);                                      // 60
	ghoul::IGhoulPlayerModel *(*NewPlayerModelInfo)(const char *gpm);             // 61
	int (*LoadGhoulFile)(const char *folder, const char *file, void **buffer);  // 62
	int (*ReadLine)(int *remaining, char **cursor, char *line);                  // 63
	void (*PreloadGSQ)(const char *folder, const char *name);                    // 64
	int (*RegisterGSQSequences)(const char *folder, const char *name, ghoul::IGhoulObject *obj); // 65
	void (*TurnOffPartsFromGSQ)(const char *folder, const char *name, ghoul::IGhoulObject *obj,
	                            ghoul::IGhoulInst *inst);                        // 66
	void **playercontext;                                                        // 67 data: engine pointer variable
	void (*configstring)(int index, const char *value);                          // 68
	void (*SZ_Init)(sizebuf_t *sb, void *data, int size);                       // 69
	void (*SZ_Clear)(sizebuf_t *sb);                                             // 70
	void (*SZ_Write)(sizebuf_t *sb, const void *data, int length);               // 71
	void (*error)(const char *fmt, ...);                                         // 72
	void (*dedicatedprint)(const char *text);                                    // 73
	void *(*LoadPlayerDLL)(void *common, void *client, void *server, int isclient); // 74
	void (*UnloadPlayerDLL)(int isclient);                                       // 75
	float (*flrand)(float min, float max);                                       // 76
	int (*irand)(int min, int max);                                              // 77
	void (*linkentity)(edict_t *ent);                                            // 78
	void (*unlinkentity)(edict_t *ent);                                          // 79
	int (*BoxEdicts)(const float *mins, const float *maxs, edict_t **list, int maxcount, int areatype); // 80
	void (*Pmove)(void *pm);                                                     // 81
	void *(*TagMalloc)(int size, int tag);                                       // 82
	void (*TagFree)(void *block);                                                // 83
	void (*FreeTags)(int tag);                                                   // 84
	int (*WriteChunk)(int id, const void *data, int length);                     // 85
	int (*ReadChunk)(int id, void *buffer, int length, void **allocated);        // 86
	cvar_t *(*cvar)(const char *name, const char *value, int flags, cvarcallback_t cb); // 87
	cvar_t *(*cvar_set)(const char *name, const char *value);                    // 88
	void (*cvar_setvalue)(const char *name, float value);                        // 89
	cvar_t *(*cvar_forceset)(const char *name, const char *value);               // 90
	char *(*cvar_info)(int flagmask);                                            // 91
	float (*cvar_value)(const char *name);                                       // 92
	int (*FS_LoadFile)(const char *path, void **buffer, int flag);               // 93
	void (*FS_FreeFile)(void *buffer);                                           // 94
	char *(*FS_Gamedir)(void);                                                   // 95
	void (*FS_CreatePath)(const char *path);                                     // 96
	int (*FS_FileExists)(const char *path);                                      // 97
	void (*AddCommandString)(const char *text);                                  // 98
} game_import_t;

// ---------------------------------------------------------------------------------------------
// Export table (game -> engine), SPEC-001
// ---------------------------------------------------------------------------------------------
typedef struct usercmd_s usercmd_t;

typedef struct game_export_s {
	int apiversion;                                                              // 0x00
	void (*Init)(void);                                                          // 0x04
	void (*Shutdown)(void);                                                      // 0x08
	void (*SpawnEntities)(const char *mapname, const char *entstring, const char *spawnpoint); // 0x0C
	void (*WriteGame)(qboolean autosave);                                        // 0x10
	qboolean (*ReadGame)(int unused);                                            // 0x14
	void (*WriteLevel)(void);                                                    // 0x18
	void (*ReadLevel)(void);                                                     // 0x1C
	qboolean (*ClientPreConnect)(edict_t *ent, char *userinfo);                  // 0x20
	qboolean (*ClientConnect)(edict_t *ent, char *userinfo);                     // 0x24
	void (*ClientBegin)(edict_t *ent);                                           // 0x28
	void (*ClientUserinfoChanged)(edict_t *ent, char *userinfo, qboolean ingame); // 0x2C
	void (*ClientDisconnect)(edict_t *ent);                                      // 0x30
	void (*ClientCommand)(edict_t *ent);                                         // 0x34
	void (*ClientThink)(edict_t *ent, usercmd_t *cmd);                           // 0x38
	void (*ClientDropped)(edict_t *ent);                                         // 0x3C
	qboolean (*SaveAllowed)(void);                                               // 0x40
	void (*SavesLeft)(void);                                                     // 0x44
	void (*GameStats)(void);                                                     // 0x48
	void (*UpdateInven)(void);                                                   // 0x4C
	const char *(*GetGameType)(void);                                            // 0x50
	int (*GetCinematicState)(void);                                              // 0x54
	void (*SetCinematicState)(int state);                                        // 0x58
	float (*RunFrame)(int framenum);                                             // 0x5C
	edict_t *edicts;                                                             // 0x60
	int edict_size;                                                              // 0x64
	int num_edicts;                                                              // 0x68
	int max_edicts;                                                              // 0x6C
} game_export_t;

#if defined(_M_IX86) || defined(__i386__) /* the SoF 32-bit x86 ABI (MSVC or GCC/Clang i386) */
static_assert(sizeof(game_import_t) == 396, "SPEC-001 import table is 396 bytes");
static_assert(sizeof(game_export_t) == 112, "SPEC-001 export table is 112 bytes");
static_assert(sizeof(trace_t) == 56, "SPEC-313 trace result is 56 bytes");
static_assert(sizeof(sizebuf_t) == 20, "SPEC-003 sizebuf is 20 bytes");
#endif

// SPEC-001 constants
#define TAG_GAME 765
#define TAG_LEVEL 766

#define MULTICAST_ALL 0
#define MULTICAST_PHS 1
#define MULTICAST_PVS 2
#define MULTICAST_ALL_R 3
#define MULTICAST_PHS_R 4
#define MULTICAST_PVS_R 5

// configstrings (SPEC-001 / SPEC-203)
#define CS_NAME 0
#define CS_CDTRACK 1
#define CS_SKY 2
#define CS_SKYAXIS 3
#define CS_SKYROTATE 4
#define CS_MAPCHECKSUM 6
#define CS_CTF_BLUE 8
#define CS_CTF_RED 9
#define CS_AMBSET 12
#define CS_MUSICSET 13
#define CS_TERRAINSET 14
#define CS_SCREENEFFECT 15
#define CS_MATERIALS 16
#define CS_MAXCLIENTS 31
#define CS_MODELS 35
#define CS_SOUNDS 291
#define CS_EFFECTS 647
#define CS_IMAGES 903
#define CS_LIGHTS 1159
#define CS_PLAYERSKINS 1415
#define CS_DMICONS 1447
#define CS_STRINGPACKAGES 1463
#define CS_WELCOME 1493
#define CS_GHOULFILES 1497
#define CS_CONTROLPOINTS 3545
#define MAX_CONFIGSTRINGS 3555
#define MAX_MODELS 256
#define MAX_SOUNDS 356
#define MAX_LIGHTSTYLES 256
#define MAX_CLIENTS 32
#define MAX_EDICTS 1024
