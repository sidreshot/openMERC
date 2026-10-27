/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1b (game module host). Other packages (G18 game/, I7 player/):
 * include, do not edit; ask I1b for changes. I1d added the player module's
 * common and client import tables (marked I1d).
 *
 * The Soldier of Fortune 1.07f interface between the engine and the game
 * module (gamex86.dll) and the player module (player.dll), as the engine
 * sees it: the 99-entry import table, the 28-field export table, the
 * engine-visible entity prefix, the entity state, the player state, the
 * user command, the player-movement block, the trace result, the module
 * message buffer and the player-module tables.
 *
 * All names carry a sof_ prefix so that this header can be used next to
 * the Quake II structures of shared.h. In the 32-bit Windows build every
 * layout is the original one (static asserts below); other targets compile
 * the same source with native pointer sizes.
 *
 * Layouts that no spec gives (entity state beyond SPEC-760 s1, player state,
 * user command, player-movement block) were measured black-box: the
 * original game module was run in the OpenMerc host and the values it
 * writes were compared with what the original server sends for the same
 * level (docs/impl/I1b.md "Measured layouts"). Fields marked "UNVERIFIED"
 * are inferred, not measured.
 *
 * Implements: SPEC-001, SPEC-003, SPEC-313 (trace result), SPEC-760 s1,
 *             SPEC-202 s2/s4/s6.1 (memory side of the transmitted states)
 */

#ifndef OM_SOF_GAME_API_H
#define OM_SOF_GAME_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef float sof_vec3_t[3];

struct sof_edict_s;
struct sof_gclient_s;
struct cvar_s;

/* ---------------------------------------------------------------------
 * constants (SPEC-001, SPEC-203)
 * --------------------------------------------------------------------- */

#define SOF_GAME_API_VERSION 3
#define SOF_PLAYER_API_VERSION 1
#define SOF_TAG_GAME 765
#define SOF_TAG_LEVEL 766

#define SOF_MAX_EDICTS 1024
#define SOF_MAX_CLIENTS 32
#define SOF_MAX_STATS 16
#define SOF_MAX_ENT_CLUSTERS 32
#define SOF_MAXTOUCH 32

/* configstrings (SPEC-203) */
#define SOF_CS_NAME 0
#define SOF_CS_CDTRACK 1
#define SOF_CS_SKY 2
#define SOF_CS_MAPCHECKSUM 6
#define SOF_CS_AMBSET 12
#define SOF_CS_MUSICSET 13
#define SOF_CS_MAXCLIENTS 31
#define SOF_CS_MODELS 35
#define SOF_CS_SOUNDS 291
#define SOF_CS_EFFECTS 647
#define SOF_CS_IMAGES 903
#define SOF_CS_LIGHTS 1159
#define SOF_CS_PLAYERSKINS 1415
#define SOF_CS_DMICONS 1447
#define SOF_CS_STRINGPACKAGES 1463
#define SOF_CS_WELCOME 1493
#define SOF_CS_GHOULFILES 1497
#define SOF_MAX_CONFIGSTRINGS 3555
#define SOF_CS_LENGTH 64

#define SOF_MAX_MODELS 256
#define SOF_MAX_SOUNDS 356
#define SOF_MAX_EFFECTS 256
#define SOF_MAX_IMAGES 256
#define SOF_MAX_STRINGPACKAGES 30
#define SOF_MAX_GHOULFILES 2048

/* OpenMerc: sound indexes past the SoF 1.07f table. A single-player map run with deathmatch 1 indexes the six
   player death-voice sets on top of its own sounds and needs more than 355 (tsr1). The wire index is 9 bits
   (svc_sound flag 0x20, entity U_SOUND256), so 512 sounds fit the protocol. Indexes 356..511 are kept in
   configstrings appended after the 1.07f layout (3555..3710), so no 1.07f configstring index moves and the
   retail game module of mode A keeps working. 1.07f network compatibility is not a goal (DEVIATIONS.md). */
#define OM_MAX_SOUNDS 512
#define OM_CS_SOUNDS_EXT SOF_MAX_CONFIGSTRINGS
#define OM_SOUNDS_EXT (OM_MAX_SOUNDS - SOF_MAX_SOUNDS)
#define OM_MAX_CONFIGSTRINGS (OM_CS_SOUNDS_EXT + OM_SOUNDS_EXT)
#define OM_CS_SOUND(i) (((i) < SOF_MAX_SOUNDS) ? (SOF_CS_SOUNDS + (i)) : (OM_CS_SOUNDS_EXT + (i) - SOF_MAX_SOUNDS))

/* multicast destinations (SPEC-001 #28/#29) */
#define SOF_MULTICAST_ALL 0
#define SOF_MULTICAST_PHS 1
#define SOF_MULTICAST_PVS 2
#define SOF_MULTICAST_ALL_R 3
#define SOF_MULTICAST_PHS_R 4
#define SOF_MULTICAST_PVS_R 5

/* server flags (SPEC-760 s3) */
#define SOF_SVF_NOCLIENT 0x01
#define SOF_SVF_DEADMONSTER 0x02
#define SOF_SVF_MONSTER 0x04
#define SOF_SVF_ROTATE_ROLL 0x08
#define SOF_SVF_ROTATE_PITCH 0x10
#define SOF_SVF_ROTATE_YAW 0x20
#define SOF_SVF_SAFEZONE 0x40
#define SOF_SVF_LINKNONSOLID 0x80

/* solid values (SPEC-760 s2) */
#define SOF_SOLID_NOT 0
#define SOF_SOLID_TRIGGER 1
#define SOF_SOLID_BBOX 2
#define SOF_SOLID_BSP 3
#define SOF_SOLID_BSP2 4
#define SOF_SOLID_CORPSE 5

/* player-movement types (SPEC-202 s4 field 8) */
#define SOF_PM_NORMAL 0
#define SOF_PM_NOCLIP 1
#define SOF_PM_SPECTATOR 2
#define SOF_PM_DEAD 3
#define SOF_PM_GIB 4
#define SOF_PM_FREEZE 5
#define SOF_PM_REMOTE 6
#define SOF_PM_INTERMISSION_SPECTATOR 7

/* user command buttons (SPEC-202 s6.1) */
#define SOF_BUTTON_ATTACK 0x01
#define SOF_BUTTON_USE 0x02
#define SOF_BUTTON_ALTATTACK 0x04
#define SOF_BUTTON_WEAPONEXTRA1 0x08
#define SOF_BUTTON_WEAPONEXTRA2 0x10
#define SOF_BUTTON_RUN 0x20
#define SOF_BUTTON_SKIP 0x40
#define SOF_BUTTON_ANY 0x80

/* ---------------------------------------------------------------------
 * module message buffer (SPEC-003: 20 bytes, also used by game imports
 * #40-#42 and #69-#71)
 * --------------------------------------------------------------------- */

typedef struct sof_sizebuf_s
{
	uint8_t allowoverflow;   /* 0 */
	uint8_t overflowed;      /* 1 */
	uint8_t pad[2];
	uint8_t *data;           /* 4 */
	int maxsize;             /* 8 */
	int cursize;             /* 12 */
	int readcount;           /* 16 */
} sof_sizebuf_t;

/* ---------------------------------------------------------------------
 * trace result (SPEC-313, 56 bytes)
 * --------------------------------------------------------------------- */

typedef struct sof_cplane_s
{
	sof_vec3_t normal;
	float dist;
	uint8_t type;
	uint8_t signbits;
	uint8_t pad[2];
} sof_cplane_t;

/* texture-information record a trace points at (SPEC-313 "hit surface"):
   the BSP texinfo without its "next" link */
typedef struct sof_surface_s
{
	float vecs[2][4];        /* 0 */
	int flags;               /* 32: high byte = material (SPEC-101) */
	int value;               /* 36 */
	char texture[32];        /* 40 */
} sof_surface_t;

typedef struct sof_trace_s
{
	uint8_t allsolid;        /* 0 */
	uint8_t startsolid;      /* 1 */
	uint8_t pad[2];
	float fraction;          /* 4 */
	sof_vec3_t endpos;       /* 8 */
	sof_cplane_t plane;      /* 20 */
	sof_surface_t *surface;  /* 40 */
	int contents;            /* 44 */
	struct sof_edict_s *ent; /* 48 */
	int leafnum;             /* 52: last leaf with faces passed, -1 = none */
} sof_trace_t;

/* filter of the filtered trace (import #53): non-zero accepts the hit. The
   engine also passes the start point of the trace (the pointer the game gave
   to #53) and the end point of the entity sweep (the world hit point when the
   world was hit, else the end); the original game module reads both.
   Measured black-box with a tracing proxy module under the original engine
   (docs/impl/I1b.md, 2026-10-25); SPEC-313 s7 lists only the first. */
typedef int (*sof_tracefilter_t)(sof_trace_t *tr, const float *start, const float *sweepend);

/* ---------------------------------------------------------------------
 * entity state (116 bytes; SPEC-760 s1, SPEC-202 s2)
 * --------------------------------------------------------------------- */

typedef struct sof_entity_state_s
{
	int number;              /* 0x00 */
	sof_vec3_t origin;       /* 0x04 */
	sof_vec3_t angles;       /* 0x10 */
	float angle_diff;        /* 0x1C yaw offset (SPEC-202 "yaw offset") */
	int modelindex;          /* 0x20 */
	int modelindex2;         /* 0x24 secondary model, -1 = none */
	int frame;               /* 0x28 */
	int skinnum;             /* 0x2C */
	uint32_t effects;        /* 0x30 */
	int renderfx;            /* 0x34 */
	int solid;               /* 0x38 packed box (SPEC-202 s2.3) */
	int sound;               /* 0x3C looping sound / ambient set */
	int sound_data;          /* 0x40 volume bits 3..7 | attenuation class bits 0..2 */
	int event[3];            /* 0x44 bits 0..6 id, bit 7 = another pair follows (AMEND-005 G18.18) */
	int eventparm[3];        /* 0x50 */
	uint16_t effect_slots[12]; /* 0x5C 4 x (effect, bolt, scale x 16); with effects 0x0001 the
	                              rotation block {999, angular velocity x3, Ghoul ids x4, unused x4} */
} sof_entity_state_t;

/* ---------------------------------------------------------------------
 * player state (200 bytes, SPEC-202 s4; the first part is the movement
 * state handed to the player movement)
 * --------------------------------------------------------------------- */

typedef struct sof_pmove_state_s
{
	int pm_type;             /* 0x00 */
	int16_t origin[3];       /* 0x04 1/8 units */
	int16_t velocity[3];     /* 0x0A 1/8 units/s */
	uint8_t pm_flags;        /* 0x10 */
	uint8_t pm_time;         /* 0x11 */
	int16_t gravity;         /* 0x12 */
	int16_t delta_angles[3]; /* 0x14 */
	uint8_t move_scale;      /* 0x1A "extra pmove byte": movement speed scale x 255 */
	uint8_t pad1b;
} sof_pmove_state_t;

typedef struct sof_player_state_s
{
	sof_pmove_state_t pmove; /* 0x00 */
	sof_vec3_t viewangles;   /* 0x1C */
	sof_vec3_t viewoffset;   /* 0x28 */
	sof_vec3_t kick_angles;  /* 0x34 */
	sof_vec3_t weapon_kick;  /* 0x40 only [0] is sent */
	sof_vec3_t remote_origin; /* 0x4C float, 1/8 units */
	sof_vec3_t remote_angles; /* 0x58 */
	int remote_entity;       /* 0x64 -1 = none */
	int remote_type;         /* 0x68 */
	void *gun;               /* 0x6C view-weapon Ghoul instance (the engine sends its handle) */
	int16_t unknown70;       /* 0x70 */
	uint8_t gun_type;        /* 0x72 view-weapon type = current weapon id (measured) */
	uint8_t pad73;
	uint8_t gun_count_a;     /* 0x74 */
	uint8_t pad75;
	int16_t gun_count_b;     /* 0x76 */
	uint8_t weapon_seq;      /* 0x78 */
	uint8_t resync;          /* 0x79 */
	uint8_t inhibit;         /* 0x7A */
	uint8_t pad7b;
	void *unknown7c;         /* 0x7C not sent; a pointer in the open game (sof_layout.h bodyinst) */
	int unknown80;           /* 0x80 not sent (remote-view model?) */
	float blend[4];          /* 0x84 */
	float fov;               /* 0x94 */
	int rdflags;             /* 0x98 */
	int16_t ambient;         /* 0x9C */
	uint8_t music;           /* 0x9E */
	uint8_t pad9f;
	int unknowna0;           /* 0xA0 not sent */
	int16_t stats[SOF_MAX_STATS]; /* 0xA4 */
	uint8_t dm_rank;         /* 0xC4 */
	uint8_t dm_ranked;       /* 0xC5 */
	uint8_t spectated;       /* 0xC6 */
	uint8_t cinematic_freeze; /* 0xC7 */
} sof_player_state_t;

/* the client record the game keeps per player: player state, then the ping
   in milliseconds written by the engine (SPEC-780 s1) */
typedef struct sof_gclient_s
{
	sof_player_state_t ps;   /* 0 */
	int ping;                /* 200 */
} sof_gclient_t;

/* ---------------------------------------------------------------------
 * user command (24 bytes, ClientThink argument; SPEC-202 s6.1)
 * --------------------------------------------------------------------- */

typedef struct sof_usercmd_s
{
	uint8_t msec;            /* 0 */
	uint8_t buttons;         /* 1 */
	uint8_t lightlevel;      /* 2 */
	int8_t lean;             /* 3 -1 right, 0, +1 left */
	int16_t angles[3];       /* 4 */
	int16_t forwardmove;     /* 10 */
	int16_t sidemove;        /* 12 */
	int16_t upmove;          /* 14 */
	float firetime;          /* 16 */
	float altfiretime;       /* 20 */
} sof_usercmd_t;

/* ---------------------------------------------------------------------
 * player-movement block (import #81). The order follows Quake II's
 * pmove_t with the SoF state and command; the SoF additions after the
 * callbacks are measured (I1b.md "Measured layouts").
 * --------------------------------------------------------------------- */

typedef struct sof_pmove_s
{
	sof_pmove_state_t s;     /* 0x00 in/out */
	sof_usercmd_t cmd;       /* 0x1C in */
	int snapinitial;         /* 0x34 in */
	int numtouch;            /* 0x38 out */
	struct sof_edict_s *touchents[SOF_MAXTOUCH]; /* 0x3C out */
	sof_vec3_t viewangles;   /* 0xBC out */
	float viewheight;        /* 0xC8 out */
	sof_vec3_t mins, maxs;   /* 0xCC out */
	struct sof_edict_s *groundentity; /* 0xE4 out */
	int watertype;           /* 0xE8 out */
	int waterlevel;          /* 0xEC out */
	/* callbacks (in): MSVC returns the 56-byte trace through a hidden
	   first argument, as for the Quake II game source */
#if defined(__i386__) && !defined(_MSC_VER)
	/* GCC/Clang i386: the callee pops the hidden return pointer itself, so the call has to be declared by
	   value or the caller's stack is 4 bytes off after every trace */
	sof_trace_t (*trace)(const float *start, const float *mins, const float *maxs,
			const float *end);                      /* 0xF0 */
#else
	void (*trace)(sof_trace_t *out, const float *start, const float *mins,
			const float *maxs, const float *end);   /* 0xF0 */
#endif
	int (*pointcontents)(const float *point);  /* 0xF4 */
	uint8_t sof_extra[64];   /* 0xF8 SoF additions (see I1b.md) */
} sof_pmove_t;

/* ---------------------------------------------------------------------
 * entity record: engine-visible prefix (SPEC-760 s1, 0x19C bytes of the
 * 1124-byte record of the original game)
 * --------------------------------------------------------------------- */

typedef struct sof_link_s
{
	struct sof_link_s *prev, *next;
} sof_link_t;

typedef struct sof_edict_s
{
	sof_entity_state_t s;    /* 0x000 */
	sof_gclient_t *client;   /* 0x074 */
	int inuse;               /* 0x078 */
	int linkcount;           /* 0x07C */
	sof_link_t area;         /* 0x080 */
	int num_clusters;        /* 0x088 -1 = use headnode */
	int clusternums[SOF_MAX_ENT_CLUSTERS]; /* 0x08C */
	int headnode;            /* 0x10C */
	int areanum, areanum2;   /* 0x110 */
	int svflags;             /* 0x118 */
	sof_vec3_t mins, maxs;   /* 0x11C */
	sof_vec3_t absmin, absmax, size; /* 0x134 */
	int solid;               /* 0x158 */
	int clipmask;            /* 0x15C */
	struct sof_edict_s *owner; /* 0x160 */
	void *ghoulInst;         /* 0x164 Ghoul model instance (SPEC-011) */
	sof_vec3_t engine168;    /* 0x168 1000.0 at allocation (engine-side, UNVERIFIED) */
	sof_vec3_t engine174;    /* 0x174 -1000.0 at allocation */
	sof_vec3_t engine180;    /* 0x180 10000.0 at allocation */
	int engine18c;           /* 0x18C -1 at allocation */
	uint8_t engine190[12];   /* 0x190 */
	/* 0x19C: game-private fields */
} sof_edict_t;

/* ---------------------------------------------------------------------
 * import table (engine -> game, SPEC-001): 99 entries, 396 bytes
 * --------------------------------------------------------------------- */

typedef void (*sof_cvarcallback_t)(struct cvar_s *cv);

typedef struct sof_game_import_s
{
	int (*modelindex)(const char *name);                                    /* 0 */
	int (*soundindex)(const char *name);                                    /* 1 */
	int (*effectindex)(const char *name);                                   /* 2 */
	int (*imageindex)(const char *name);                                    /* 3 */
	void (*soundremove)(const char *name);                                  /* 4 */
	int (*addressbanned)(const char *address);                              /* 5 */
	void (*ghoulregisterall)(void);                                         /* 6 */
	void (*setmodel)(sof_edict_t *ent, const char *name);                   /* 7 */
	void (*setrendermodel)(sof_edict_t *ent, const char *name);             /* 8 */
	int (*argc)(void);                                                      /* 9 */
	char *(*argv)(int n);                                                   /* 10 */
	char *(*args)(void);                                                    /* 11 */
	void (*bprintf)(int printlevel, const char *fmt, ...);                  /* 12 */
	void (*dprintf)(const char *fmt, ...);                                  /* 13 */
	void (*cprintf)(sof_edict_t *ent, int printlevel, const char *fmt, ...); /* 14 */
	void (*clprintf)(sof_edict_t *to, sof_edict_t *from, int printlevel,
			const char *fmt, ...);                                          /* 15 */
	void (*welcomeprint)(sof_edict_t *ent);                                 /* 16 */
	void (*centerprintf)(sof_edict_t *ent, const char *fmt, ...);           /* 17 */
	void (*cinprintf)(sof_edict_t *ent, int x, int y, int textspeed,
			const char *text);                                              /* 18 */
	void (*captionprint)(int printlevel, int id);                           /* 19 */
	void (*captionprintent)(sof_edict_t *ent, int id);                      /* 20 */
	void (*forceconnotify)(void);                                           /* 21 */
	void (*sound)(sof_edict_t *ent, int channel, int soundindex, float volume,
			float attenuation, float timeofs, int local);                   /* 22 */
	void (*positioned_sound)(const float *origin, sof_edict_t *ent, int channel,
			int soundindex, float volume, float attenuation, float timeofs,
			int local);                                                     /* 23 */
	void *unused24;                                                         /* 24 */
	int (*damagetexture)(sof_surface_t *surface, int damage);               /* 25 */
	int (*getmaterials)(uint8_t *buf, int capacity);                        /* 26 */
	void (*loadingprogress)(float fraction, int flag);                      /* 27 */
	void (*multicast)(const float *origin, int to);                         /* 28 */
	void (*multicastexclude)(const float *origin, int to, int excluded);    /* 29 */
	void (*unicast)(sof_edict_t *ent, int reliable);                        /* 30 */
	void (*WriteChar)(int c);                                               /* 31 */
	void (*WriteByte)(int c);                                               /* 32 */
	void (*WriteShort)(int c);                                              /* 33 */
	void (*WriteLong)(int c);                                               /* 34 */
	void (*WriteFloat)(float f);                                            /* 35 */
	void (*WriteString)(const char *s);                                     /* 36 */
	void (*WritePosition)(const float *pos);                                /* 37 */
	void (*WriteDir)(const float *dir);                                     /* 38 */
	void (*WriteAngle)(float f);                                            /* 39 */
	void (*SZ_WriteByte)(sof_sizebuf_t *sb, int c);                         /* 40 */
	void (*SZ_WriteShort)(sof_sizebuf_t *sb, int c);                        /* 41 */
	void (*SZ_WriteLong)(sof_sizebuf_t *sb, int c);                         /* 42 */
	void (*ReliableWriteByteToClient)(int c, int clientnum);                /* 43 */
	void (*ReliableWriteDataToClient)(const void *data, int length,
			int clientnum);                                                 /* 44 */
	int (*DirToByte)(const float *dir);                                     /* 45 */
	void (*SendPlayernameColors)(sof_edict_t *ent, int length, int count,
			const void *data);                                              /* 46 */
	void (*SP_Register)(const char *package);                               /* 47 */
	void (*SP_Print)(sof_edict_t *ent, int id, ...);                        /* 48 */
	void (*SP_Print_Obit)(sof_edict_t *ent, int id, ...);                   /* 49 */
	int (*SP_SPrint)(char *buf, int capacity, int id, ...);                 /* 50 */
	const char *(*SP_GetStringText)(int id);                                /* 51 */
	int (*trace)(const float *start, const float *mins, const float *maxs,
			const float *end, sof_edict_t *passent, int contentmask,
			sof_trace_t *out);                                              /* 52 */
	int (*tracefilter)(const float *start, const float *mins, const float *maxs,
			const float *end, sof_edict_t *passent, int contentmask,
			sof_trace_t *out, sof_tracefilter_t filter);                    /* 53 */
	int (*pointcontents)(const float *point);                               /* 54 */
	float (*regiondistance)(const float *point);                            /* 55 */
	int (*inPVS)(const float *p1, const float *p2);                         /* 56 */
	int (*inPHS)(const float *p1, const float *p2);                         /* 57 */
	void (*SetAreaPortalState)(int portalnum, int open);                    /* 58 */
	int (*AreasConnected)(int area1, int area2);                            /* 59 */
	void *(*GetGhoul)(void);                                                /* 60 IGhoulSystem * */
	void *(*NewPlayerModelInfo)(const char *gpm);                           /* 61 IGhoulPlayerModel * */
	int (*LoadGhoulFile)(const char *folder, const char *file, void **buffer); /* 62 */
	int (*ReadLine)(int *remaining, char **cursor, char *line);             /* 63 */
	void (*PreloadGSQ)(const char *folder, const char *name);               /* 64 */
	int (*RegisterGSQSequences)(const char *folder, const char *name,
			void *object);                                                  /* 65 */
	void (*TurnOffPartsFromGSQ)(const char *folder, const char *name,
			void *object, void *instance);                                  /* 66 */
	int **playercontext;                                                    /* 67 data */
	void (*configstring)(int index, const char *value);                     /* 68 */
	void (*SZ_Init)(sof_sizebuf_t *sb, void *data, int size);               /* 69 */
	void (*SZ_Clear)(sof_sizebuf_t *sb);                                    /* 70 */
	void (*SZ_Write)(sof_sizebuf_t *sb, const void *data, int length);      /* 71 */
	void (*error)(const char *fmt, ...);                                    /* 72 */
	void (*dedicatedprint)(const char *text);                               /* 73 */
	void *(*LoadPlayerDLL)(void *common, void *client, void *server,
			int isclient);                                                  /* 74 */
	void (*UnloadPlayerDLL)(int isclient);                                  /* 75 */
	float (*flrand)(float min, float max);                                  /* 76 */
	int (*irand)(int min, int max);                                         /* 77 */
	void (*linkentity)(sof_edict_t *ent);                                   /* 78 */
	void (*unlinkentity)(sof_edict_t *ent);                                 /* 79 */
	int (*BoxEdicts)(const float *mins, const float *maxs, sof_edict_t **list,
			int maxcount, int areatype);                                    /* 80 */
	void (*Pmove)(sof_pmove_t *pm);                                         /* 81 */
	void *(*TagMalloc)(int size, int tag);                                  /* 82 */
	void (*TagFree)(void *block);                                           /* 83 */
	void (*FreeTags)(int tag);                                              /* 84 */
	int (*WriteChunk)(int id, const void *data, int length);                /* 85 */
	int (*ReadChunk)(int id, void *buffer, int length, void **allocated);   /* 86 */
	struct cvar_s *(*cvar)(const char *name, const char *value, int flags,
			sof_cvarcallback_t cb);                                         /* 87 */
	struct cvar_s *(*cvar_set)(const char *name, const char *value);        /* 88 */
	void (*cvar_setvalue)(const char *name, float value);                   /* 89 */
	struct cvar_s *(*cvar_forceset)(const char *name, const char *value);   /* 90 */
	char *(*cvar_info)(int flagmask);                                       /* 91 */
	float (*cvar_value)(const char *name);                                  /* 92 */
	int (*FS_LoadFile)(const char *path, void **buffer, int flag);          /* 93 */
	void (*FS_FreeFile)(void *buffer);                                      /* 94 */
	const char *(*FS_Gamedir)(void);                                        /* 95 */
	void (*FS_CreatePath)(const char *path);                                /* 96 */
	int (*FS_FileExists)(const char *path);                                 /* 97 */
	void (*AddCommandString)(const char *text);                             /* 98 */
} sof_game_import_t;

/* ---------------------------------------------------------------------
 * export table (game -> engine, SPEC-001): 28 fields, 112 bytes
 * --------------------------------------------------------------------- */

typedef struct sof_game_export_s
{
	int apiversion;                                                          /* 0x00 */
	void (*Init)(void);                                                      /* 0x04 */
	void (*Shutdown)(void);                                                  /* 0x08 */
	void (*SpawnEntities)(const char *mapname, const char *entstring,
			const char *spawnpoint);                                         /* 0x0C */
	void (*WriteGame)(int autosave);                                         /* 0x10 */
	int (*ReadGame)(int unused);                                             /* 0x14 */
	void (*WriteLevel)(void);                                                /* 0x18 */
	void (*ReadLevel)(void);                                                 /* 0x1C */
	int (*ClientPreConnect)(sof_edict_t *ent, char *userinfo);               /* 0x20 */
	int (*ClientConnect)(sof_edict_t *ent, char *userinfo);                  /* 0x24 */
	void (*ClientBegin)(sof_edict_t *ent);                                   /* 0x28 */
	void (*ClientUserinfoChanged)(sof_edict_t *ent, char *userinfo, int ingame); /* 0x2C */
	void (*ClientDisconnect)(sof_edict_t *ent);                              /* 0x30 */
	void (*ClientCommand)(sof_edict_t *ent);                                 /* 0x34 */
	void (*ClientThink)(sof_edict_t *ent, sof_usercmd_t *cmd);               /* 0x38 */
	void (*ClientDropped)(sof_edict_t *ent);                                 /* 0x3C */
	int (*SaveAllowed)(void);                                                /* 0x40 */
	void (*SavesLeft)(void);                                                 /* 0x44 */
	void (*GameStats)(void);                                                 /* 0x48 */
	void (*UpdateInven)(void);                                               /* 0x4C */
	const char *(*GetGameType)(void);                                        /* 0x50 */
	int (*GetCinematicState)(void);                                          /* 0x54 low byte */
	void (*SetCinematicState)(int state);                                    /* 0x58 */
	float (*RunFrame)(int framenum);                                         /* 0x5C */
	sof_edict_t *edicts;                                                     /* 0x60 */
	int edict_size;                                                          /* 0x64 */
	int num_edicts;                                                          /* 0x68 */
	int max_edicts;                                                          /* 0x6C */
} sof_game_export_t;

typedef sof_game_export_t *(*sof_getgameapi_t)(sof_game_import_t *import);

/* ---------------------------------------------------------------------
 * player module (SPEC-003)
 * --------------------------------------------------------------------- */

/* export table, 12 entries, 48 bytes (both sides get the same table) */
typedef struct sof_player_export_s
{
	int version;                                                             /* 0x00 */
	int *context;                                                            /* 0x04 shared context flag */
	int (*Init)(void);                                                       /* 0x08 */
	void (*Shutdown)(void);                                                  /* 0x0C */
	void **weapons;                                                          /* 0x10 15 weapon descriptions */
	int *firedPrimary;                                                       /* 0x14 */
	int *firedAlternate;                                                     /* 0x18 */
	void (*PrecacheViewWeapons)(unsigned mask);                              /* 0x1C */
	void (*ForgetViewWeapons)(void);                                         /* 0x20 */
	void *(*NewInventory)(void);                                             /* 0x24 */
	void (*DeleteInventory)(void *inv);                                      /* 0x28 */
	void (*RestoreInventory)(void *inv);                                     /* 0x2C */
} sof_player_export_t;

typedef sof_player_export_t *(*sof_getplayerapi_t)(void *common, void *side);

/* I1d: the tables the engine client hands to GetPlayerClientAPI (SPEC-003). Common table: 19 pointers,
   76 bytes; the client fills every entry (the game fills its own copy for the server side). */
typedef struct sof_player_common_import_s
{
	void *(*Cvar_Get)(const char *name, const char *value, int flags);       /* 0x00 */
	void *(*Cvar_Set)(const char *name, const char *value);                  /* 0x04 */
	void (*Cvar_SetValue)(const char *name, float value);                    /* 0x08 */
	int (*Cmd_Argc)(void);                                                   /* 0x0C */
	char *(*Cmd_Argv)(int i);                                                /* 0x10 */
	char *(*Cmd_Args)(void);                                                 /* 0x14 */
	void (*Com_Printf)(const char *fmt, ...);                                /* 0x18 */
	void (*Com_sprintf)(char *dst, int size, const char *fmt, ...);          /* 0x1C */
	void (*Com_DPrintf)(const char *fmt, ...);                               /* 0x20 */
	int (*FS_LoadFile)(const char *path, void **buffer, int unused);         /* 0x24 */
	void (*FS_FreeFile)(void *buffer);                                       /* 0x28 */
	int (*irand)(int min, int max);                                          /* 0x2C */
	void *ghl_specular;                                                      /* 0x30 cvar record */
	void *(*NewPlayerModelInfo)(const char *gpm);                            /* 0x34 */
	int (*LoadGhoulFile)(const char *folder, const char *file, void **buffer); /* 0x38 */
	int (*ReadLine)(int *remaining, char **cursor, char *line);              /* 0x3C */
	void (*PreloadGSQ)(const char *folder, const char *name);                /* 0x40 */
	int (*RegisterGSQSequences)(const char *folder, const char *name,
			void *object);                                                   /* 0x44 */
	void (*TurnOffPartsFromGSQ)(const char *folder, const char *name,
			void *object, void *instance);                                   /* 0x48 */
} sof_player_common_import_t;

/* I1d: client import table, 20 pointers, 80 bytes (SPEC-003). Imports 2/3 receive (owner context, owner,
   inventory) like the server's fire imports (SPEC-AMEND-006 I7.13). */
typedef struct sof_player_client_import_s
{
	void *(*GetGhoul)(void);                                                 /* 0x00 IGhoulSystem * */
	float *time;                                                             /* 0x04 client time, seconds */
	void (*FirePrimaryEffects)(void *ctx, void *owner, void *inv);           /* 0x08 */
	void (*FireAlternateEffects)(void *ctx, void *owner, void *inv);         /* 0x0C */
	void (*WeaponSound)(const char *note);                                   /* 0x10 */
	void (*WeaponKillSound)(void);                                           /* 0x14 */
	void (*WeaponEffect)(const char *note);                                  /* 0x18 */
	int (*ReadByte)(void);                                                   /* 0x1C */
	int (*ReadShort)(void);                                                  /* 0x20 */
	int (*ReadLong)(void);                                                   /* 0x24 */
	int (*SZ_ReadByte)(sof_sizebuf_t *sb);                                   /* 0x28 */
	int (*SZ_ReadShort)(sof_sizebuf_t *sb);                                  /* 0x2C */
	int (*SZ_ReadLong)(sof_sizebuf_t *sb);                                   /* 0x30 */
	void (*SZ_ReadData)(sof_sizebuf_t *sb, void *dst, int len);              /* 0x34 */
	void (*SZ_WriteByte)(sof_sizebuf_t *sb, int c);                          /* 0x38 */
	void (*SZ_WriteShort)(sof_sizebuf_t *sb, int c);                         /* 0x3C */
	void (*SZ_WriteLong)(sof_sizebuf_t *sb, int c);                          /* 0x40 */
	void (*SZ_Init)(sof_sizebuf_t *sb, void *data, int size);                /* 0x44 */
	void (*SZ_Clear)(sof_sizebuf_t *sb);                                     /* 0x48 */
	void (*SZ_Write)(sof_sizebuf_t *sb, const void *data, int length);       /* 0x4C */
} sof_player_client_import_t;

#if (defined(_WIN32) && !defined(_WIN64)) || defined(__i386__) /* 32-bit x86 layout */
#ifdef __cplusplus
#define OM_SOF_STATIC_ASSERT static_assert
#else
#define OM_SOF_STATIC_ASSERT _Static_assert
#endif
OM_SOF_STATIC_ASSERT(sizeof(sof_sizebuf_t) == 20, "SPEC-003 sizebuf is 20 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_trace_t) == 56, "SPEC-313 trace result is 56 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_surface_t) == 72, "texinfo record is 72 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_entity_state_t) == 116, "entity state is 116 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_pmove_state_t) == 28, "movement state is 28 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_player_state_t) == 200, "player state is 200 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_usercmd_t) == 24, "user command is 24 bytes");
OM_SOF_STATIC_ASSERT(offsetof(sof_edict_t, client) == 0x74, "SPEC-760 entity prefix");
OM_SOF_STATIC_ASSERT(offsetof(sof_edict_t, svflags) == 0x118, "SPEC-760 entity prefix");
OM_SOF_STATIC_ASSERT(offsetof(sof_edict_t, solid) == 0x158, "SPEC-760 entity prefix");
OM_SOF_STATIC_ASSERT(offsetof(sof_edict_t, ghoulInst) == 0x164, "SPEC-760 entity prefix");
OM_SOF_STATIC_ASSERT(sizeof(sof_edict_t) == 0x19C, "SPEC-760 entity prefix");
OM_SOF_STATIC_ASSERT(sizeof(sof_game_import_t) == 396, "SPEC-001 import table is 396 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_game_export_t) == 112, "SPEC-001 export table is 112 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_player_export_t) == 48, "SPEC-003 export table is 48 bytes");
OM_SOF_STATIC_ASSERT(sizeof(sof_player_common_import_t) == 76, "SPEC-003 common table is 76 bytes"); /* I1d */
OM_SOF_STATIC_ASSERT(sizeof(sof_player_client_import_t) == 80, "SPEC-003 client table is 80 bytes"); /* I1d */
OM_SOF_STATIC_ASSERT(offsetof(sof_entity_state_t, modelindex) == 0x20, "SPEC-760 entity state");
OM_SOF_STATIC_ASSERT(offsetof(sof_entity_state_t, event) == 0x44, "SPEC-760 entity state");
OM_SOF_STATIC_ASSERT(offsetof(sof_entity_state_t, sound_data) == 0x40, "AMEND-005 G18.18 entity state");
OM_SOF_STATIC_ASSERT(offsetof(sof_entity_state_t, eventparm) == 0x50, "AMEND-005 G18.18 entity state");
OM_SOF_STATIC_ASSERT(offsetof(sof_entity_state_t, effect_slots) == 0x5C, "AMEND-005 G18.18 entity state");
OM_SOF_STATIC_ASSERT(sizeof(((sof_entity_state_t *)0)->event) == 12, "AMEND-005 G18.18 events are int[3]");
OM_SOF_STATIC_ASSERT(offsetof(sof_player_state_t, stats) == 0xA4, "player state stats");
#endif

#ifdef __cplusplus
}
#endif

#endif /* OM_SOF_GAME_API_H */
