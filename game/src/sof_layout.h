// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - in-memory layouts shared with the original engine. Measured black-box with game/probe
// (docs/impl/G18.md, OPEN_QUESTIONS G18 18-23) and confirmed/completed by SPEC-AMEND-005 G18.18-G18.23.
//
// Implements: SPEC-202 (meaning of the fields), SPEC-760 1 (entity state at entity offset 0), SPEC-AMEND-005
//             G18.18-G18.23
#pragma once

#include "sof_game.h"

// entity state, 116 bytes
typedef struct entity_state_s {
	int number;          // 0x00
	vec3_t origin;       // 0x04
	vec3_t angles;       // 0x10
	float yawofs;        // 0x1C "yaw offset" (SPEC-202 2)
	int modelindex;      // 0x20
	int modelindex2;     // 0x24 secondary model, -1 = none (SPEC-760 1)
	int frame;           // 0x28
	int skinnum;         // 0x2C
	int effects;         // 0x30
	int renderfx;        // 0x34
	int solid;           // 0x38
	int sound;           // 0x3C looping sound
	int sounddata;       // 0x40 volume bits 3..7, attenuation class 0..2
	int event[3];        // 0x44 bits 0..6 id, bit 7 = another pair follows
	int eventparm[3];    // 0x50
	short fx[4][3];      // 0x5C effect slots: id, bolt, scale*16; with effects 0x0001 (rotate) the rotation block
	                     //      {999, angular velocity x3, Ghoul instance ids x4, unused x4} (AMEND-005 G18.18)
} entity_state_t;

// pmove state, 28 bytes (Quake II order; byte 0x1A is SoF's move scale)
typedef struct {
	int pm_type;
	short origin[3];     // 1/8 units
	short velocity[3];
	uint8_t pm_flags;
	uint8_t pm_time;
	short gravity;
	short delta_angles[3];
	uint8_t movescale;   // trunc(move scale * 255) (SPEC-780 2 field 15)
	uint8_t pad;
} pmove_state_t;

// player state, 200 bytes (client record offset 0)
typedef struct {
	pmove_state_t pmove;            // 0x00
	vec3_t viewangles;              // 0x1C
	vec3_t viewoffset;              // 0x28
	vec3_t kick_angles;             // 0x34
	vec3_t weaponkick;              // 0x40 (only [0] is sent)
	vec3_t remote_vieworigin;       // 0x4C 1/8 units
	vec3_t remote_viewangles;       // 0x58
	int remote_id;                  // 0x64 remote view entity, -1 none
	int remote_type;                // 0x68
	ghoul::IGhoulInst *weaponinst;  // 0x6C written by player.dll
	short weaponinstid;             // 0x70 overwritten by the engine every frame with the network id of 0x6C
	short weapontype;               // 0x72 view-weapon type = current weapon id (player.dll)
	short weaponcountA;             // 0x74 clip rounds (player.dll; low byte sent)
	short weaponcountB;             // 0x76 reserve (player.dll)
	uint8_t weaponseq;              // 0x78 weapon-state sequence (player.dll)
	uint8_t resync;                 // 0x79 prediction resync number
	uint8_t inhibit;                // 0x7A button inhibit mask
	uint8_t pad7b;
	void *bodyinst;                 // 0x7C the original game keeps the player's own body instance here; unused
	short remotemodel;              // 0x80 remote-view model id (never written by the game)
	short pad82;
	float blend[4];                 // 0x84
	float fov;                      // 0x94
	int rdflags;                    // 0x98
	short ambientset;               // 0x9C
	uint8_t musicvalue;             // 0x9E
	uint8_t pad9f;
	short unusedA0;                 // 0xA0 cleared by the game every frame, not sent
	short damagedir;                // 0xA2 damage-direction latch: copied to stats[15] and cleared every frame
	short stats[16];                // 0xA4
	uint8_t dmrank;                 // 0xC4
	uint8_t dmrankcount;            // 0xC5
	uint8_t spectated;              // 0xC6
	uint8_t cinematicfreeze;        // 0xC7
} player_state_t;

// user command, 24 bytes
struct usercmd_s {
	uint8_t msec;        // 0
	uint8_t buttons;     // 1
	uint8_t lightlevel;  // 2
	int8_t lean;         // 3 -1 right, 0, +1 left
	short angles[3];     // 4
	short forwardmove;   // 10
	short sidemove;      // 12
	short upmove;        // 14
	float firetime;      // 16
	float altfiretime;   // 20
};

#define MAXTOUCH 32

// player-movement block (game import #81)
typedef struct {
	pmove_state_t s;                 // 0x00
	usercmd_t cmd;                   // 0x1C
	qboolean snapinitial;            // 0x34
	int numtouch;                    // 0x38
	edict_t *touchents[MAXTOUCH];    // 0x3C
	vec3_t viewangles;               // 0xBC
	float viewheight;                // 0xC8
	vec3_t mins, maxs;               // 0xCC, 0xD8
	edict_t *groundentity;           // 0xE4
	int watertype;                   // 0xE8
	int waterlevel;                  // 0xEC
	trace_t (*trace)(const float *start, const float *mins, const float *maxs, const float *end); // 0xF0
	int (*pointcontents)(const float *point);                                  // 0xF4
	int (*isclient)(edict_t *ent);   // 0xF8 called with the ground entity when a jump starts; non-zero refuses it
	float staggertime;               // 0xFC accelerations x max(0, 1 - v) while v > 0 (AMEND-005 G18.21)
} pmove_t;                           // 256 bytes

// cvar record
struct cvar_s {
	char *name;          // 0x00
	char *string;        // 0x04
	char *latched_string; // 0x08
	int flags;           // 0x0C
	void (*callback)(cvar_t *); // 0x10 change callback or null (AMEND-005 G18.22)
	int modified;        // 0x14 Quake II meaning
	float value;         // 0x18
	cvar_t *next;        // 0x1C
};

// surface record (72 bytes, AMEND-005 G18.23); game modules read only the material byte (35) and pass the pointer
// back to import #25
struct csurface_s {
	float vecs[2][4];    // 0
	int flags;           // 32 high byte = material id
	int animframes;      // 36 1 + nexttexinfo chain length
	void *next;          // 40
	void *image, *origimage;           // 44, 48 renderer-owned
	void *texrec, *origtexrec;         // 52, 56 texture-table records
	int value;           // 60 texinfo value
	int damagecount;     // 64
	int damagestage;     // 68
};

#if defined(_M_IX86) || defined(__i386__) /* the SoF 32-bit x86 ABI (MSVC or GCC/Clang i386) */
static_assert(sizeof(entity_state_t) == 116, "entity state is 116 bytes");
static_assert(sizeof(pmove_state_t) == 28, "pmove state is 28 bytes");
static_assert(sizeof(player_state_t) == 200, "player state is 200 bytes");
static_assert(sizeof(usercmd_t) == 24, "user command is 24 bytes");
static_assert(offsetof(pmove_t, trace) == 0xF0, "pmove trace callback");
static_assert(offsetof(pmove_t, staggertime) == 0xFC, "pmove stagger time");
static_assert(offsetof(player_state_t, stats) == 0xA4, "player state stats");
static_assert(offsetof(player_state_t, cinematicfreeze) == 0xC7, "player state cinematic freeze");
static_assert(offsetof(cvar_s, value) == 0x18, "cvar value at 0x18 (SPEC-003)");
static_assert(sizeof(pmove_t) == 256, "player-movement block is 256 bytes");
static_assert(sizeof(csurface_s) == 72, "surface record is 72 bytes");
static_assert(offsetof(player_state_t, inhibit) == 0x7A, "player state inhibit byte");
static_assert(offsetof(player_state_t, damagedir) == 0xA2, "player state damage-direction latch");
#endif
