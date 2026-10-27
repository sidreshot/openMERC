// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - private state of the game modes (G9). Not for other packages.
// Implements: SPEC-790
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_modes.h"
#include "../ghoulglue/gg_ghoul.h"

// string-package entries used by the modes (index within the package, stock 1.07f packages)
enum {
	DMG_TEXT_DROPPED_WEAPON = 0,
	DMG_TEXT_OBT_MOD_WATER = 1,
	DMG_TEXT_OBT_MOD_SLIME = 2,
	DMG_TEXT_OBT_MOD_CRUSH = 3,
	DMG_TEXT_OBT_MOD_FALLING = 4,
	DMG_TEXT_OBT_MOD_SUICIDE = 5,
	DMG_TEXT_OBT_MOD_EXPLOSIVE = 6,
	DMG_TEXT_OBT_MOD_FIRE = 7,
	DMG_TEXT_OBT_MOD_LAVA = 8,
	DMG_TEXT_OBT_MOD_EXIT = 9,
	DMG_TEXT_OBT_MOD_BARBWIRE = 10,
	DMG_TEXT_OBT_MOD_DOGBITE = 11,
	DMG_TEXT_OBT_MOD_PHOS_GRENADE = 12,
	DMG_TEXT_OBT_MOD_ROCKET_SPLASH_F = 13,
	DMG_TEXT_OBT_MOD_ROCKET_SPLASH_M = 14,
	DMG_TEXT_OBT_MOD_C4_F = 15,
	DMG_TEXT_OBT_MOD_C4_M = 16,
	DMG_TEXT_OBT_MOD_CLAYMORE = 17,
	DMG_TEXT_OBT_MOD_NEURAL_GRENADE_F = 18,
	DMG_TEXT_OBT_MOD_NEURAL_GRENADE_M = 19,
	DMG_TEXT_OBT_F = 20,
	DMG_TEXT_OBT_M = 21,
	DMG_TEXT_OBT_MOD_KNIFE_SLASH = 22,
	DMG_TEXT_OBT_MOD_KNIFE_THROWN = 23,
	DMG_TEXT_OBT_MOD_PISTOL1 = 24,
	DMG_TEXT_OBT_MOD_PISTOL2 = 25,
	DMG_TEXT_OBT_MOD_MPISTOL = 26,
	DMG_TEXT_OBT_MOD_ASSAULTRIFLE = 27,
	DMG_TEXT_OBT_MOD_SNIPERRIFLE = 28,
	DMG_TEXT_OBT_MOD_AUTOSHOTGUN = 29,
	DMG_TEXT_OBT_MOD_SHOTGUN = 30,
	DMG_TEXT_OBT_MOD_MACHINEGUN = 31,
	DMG_TEXT_OBT_MOD_PHOS_GRENADE_2 = 32,
	DMG_TEXT_OBT_MOD_ROCKET = 33,
	DMG_TEXT_OBT_MOD_ROCKET_SPLASH = 34,
	DMG_TEXT_OBT_MOD_MPG = 35,
	DMG_TEXT_OBT_MOD_FLAMEGUN = 36,
	DMG_TEXT_OBT_MOD_FLAMEGUN_NAPALM = 37,
	DMG_TEXT_OBT_MOD_C4 = 38,
	DMG_TEXT_OBT_MOD_CLAYMORE_2 = 39,
	DMG_TEXT_OBT_MOD_NEURAL_GRENADE = 40,
	DMG_TEXT_OBT_MOD_TELEFRAG = 41,
	DMG_TEXT_OBT_DIED = 42,
	DMG_LAYOUT_RESET = 43,
	DMG_LAYOUT_SCOREBAORD_CLIENT = 44,
	DMG_LAYOUT_SCOREBOARD_TEAM = 45,
	DMG_TEXT_OBT_MOD_GRENADE_M = 46,
	DMG_TEXT_OBT_MOD_GRENADE_F = 47,
	DMG_TEXT_OBT_MOD_GRENADE_2 = 48,
	DMG_LAYOUT_SCOREBAORD_SPECTATOR = 55,
	DMG_TEXT_OBT_MOD_CONC_GRENADE = 56,
	DMG_LAYOUT_CTF_CLIENT = 57,
	DMG_FRAG_LIMIT_HIT = 59,
	DMG_TIME_LIMIT_HIT = 60,
	DMG_NO_CHANGE = 71,
	DMG_CONQUER_LAYOUT_SCOREBOARD_TEAM = 72,

	ASN_TEXT_LOST_FRAG = 0,
	ASN_LAYOUT_TARGET = 1,
	ASN_LAYOUT_NO_TARGET = 2,
	ASN_LAYOUT_NUM_ENEMIES = 3,

	ARS_TEXT_BIG_WINNER = 0,
	ARS_TEXT_LAST_WEAPON = 1,
	ARS_LAYOUT_STATUS = 2,

	CTF_FRAG_CARRIER_RED = 2,
	CTF_FRAG_HURT_CARRIER_RED = 3,
	CTF_PROTECT_FLAG_RED = 4,
	CTF_END_GAME_BONUS_RED = 5,
	CTF_TEAM_SCORE_LAYOUT = 12,
	CTF_END_GAME = 13,
	CTF_FRAG_CARRIER_BLUE = 16,
	CTF_FRAG_HURT_CARRIER_BLUE = 17,
	CTF_PROTECT_FLAG_BLUE = 18,
	CTF_END_GAME_BONUS_BLUE = 19,

	CTL_TEAM_SCORE_LAYOUT = 12,
	CTL_END_GAME = 13
};

#define GM_MAXZONES 64
#define GM_MAXITEMPOS 256

// per-client state of the modes (indexed by client slot)
struct gm_client_t {
	// Assassin
	edict_t *target;
	edict_t *pending;
	// icon (player-name colour) tables, viewer row, index = other client's slot
	uint8_t icons[MAX_CLIENTS];
	uint8_t sent[MAX_CLIENTS];
	bool sentvalid[MAX_CLIENTS];
	bool spectating;            // was a spectator at the last spawn
	// team number assigned by the mode (re-applied at spawn: the game core restores the respawn data it copied
	// before the spawn-variant user-info processing, which would lose a team assigned there)
	int team;
	// CTF
	int flagteam;               // team of the flag carried (0 none)
	float hurtcarrier;          // last time this client hurt an enemy flag carrier
	// Realistic
	int fatigue;
	bool exhausted;
	// Conquer
	bool inzone;
	bool inzone_prev;
	float zoneentered;
	// Arsenal
	char arsenal_inv[1024];
	bool arsenal_have;
};

struct gm_zone_t {
	edict_t *ent;
	bool occupied;
	int lastsecond;
};

struct gm_itempos_t {
	int item;
	vec3_t origin;
};

struct gm_level_t {
	int mode;                   // -1 before the first level
	float touchdelay;           // flags and zones accept touches after this level time
	bool logwritten;
	// CTF
	int captures[3];
	// Control
	int ctrl_points;
	int ctrl_owned[3];
	int ctrl_score[3];
	float ctrl_next;
	// Conquer
	gm_zone_t zones[GM_MAXZONES];
	int nzones;
	char cs33[16], cs10[16];
	// Arsenal
	int arsenal_set[6];
	bool arsenal_announced;
	bool arsenal_newround;
	// Realistic
	gm_itempos_t itempos[GM_MAXITEMPOS];
	int nitempos;
};

extern gm_client_t gm_cl[MAX_CLIENTS];
extern gm_level_t gm;

// helpers shared by the mode files
static inline void gm_strncpyz(char *dst, const char *src, int size)
{
	if (size <= 0)
		return;
	if (!src)
		src = "";
	int i = 0;
	for (; i < size - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
}
gm_client_t *GM_Cl(edict_t *ent);          // NULL for non-clients
int GM_Slot(edict_t *ent);                 // 0-based client slot, -1 for non-clients
edict_t *GM_ClientEnt(int slot);
bool GM_IsClient(edict_t *ent);            // entity 1..maxclients with a client record
bool GM_InGame(edict_t *ent);              // in use, client, not a spectator
bool GM_IsSpectator(edict_t *ent);
bool GM_IsBot(edict_t *ent);
int GM_MaxClients(void);
int GM_EntNum(edict_t *ent);
void GM_Print(edict_t *to, int pkg, int entry, ...);   // string-package print (NULL = everyone)
int GM_Rand(void);                                     // C-library style random number 0..0x7fff
void GM_LocalSound(edict_t *ent, const char *name, float vol, float att, int chan = CHAN_VOICE);
void GM_SoundAll(const char *name, float vol, float att);

// per-mode pieces
void GM_AssassinBegin(edict_t *ent);
void GM_AssassinChoose(edict_t *ent);
void GM_AssassinSpawn(edict_t *ent, bool wasspectating);
void GM_AssassinEndFrame(edict_t *ent);
void GM_AssassinScore(edict_t *self, edict_t *attacker);
void GM_AssassinDisconnect(edict_t *ent);
void GM_StatusRefresh(edict_t *ent);
void GM_InstallHooks(void);
void GM_UpdateIcon(edict_t *viewer, edict_t *other);
void GM_ResetIcons(edict_t *ent);
void GM_SendIcons(edict_t *ent);

void GM_ArsenalDrawSet(void);
void GM_ArsenalKit(edict_t *ent, bool fresh);
void GM_ArsenalScore(edict_t *self, edict_t *attacker);
void GM_ArsenalFrame(void);

void GM_CTFLevelSetup(void);
void GM_FlagsSetup(void);
void GM_FlagsFrame(void);
void GM_CTFReturnFlag(int team);
void GM_CTFDropCarriedFlag(edict_t *ent);
void GM_CTFScore(edict_t *self, edict_t *attacker);
void GM_CTFLevelExit(void);
void GM_ControlLevelSetup(void);
bool GM_ControlEndRule(void);
void GM_ControlLevelExit(void);
void GM_ConquerLevelInit(void);
void GM_ConquerFrame(void);
void GM_ConquerEndFrame(edict_t *ent);
void GM_ConquerConfigstrings(void);
void GM_ConquerLevelExit(void);

void GM_RealisticEndFrame(edict_t *ent);

void GM_DefaultKit(edict_t *ent);
void GM_RealisticKit(edict_t *ent);
void GM_NewInventory(edict_t *ent);

void GM_WriteGameLog(void);
void GM_DefaultScoreboard(edict_t *ent, edict_t *killer);
void GM_TeamColumnsScoreboard(edict_t *ent);
void GM_PlayerInfo(edict_t *ent);          // rewrite configstring 1415 + slot
