/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 (internal state of the bot system), SPEC-AMEND-005 G5.1-G5.8
 * Owner: package G5 (game/src/bots). Private to bot_*.cpp.
 */
#ifndef OM_BOT_LOCAL_H
#define OM_BOT_LOCAL_H

#include <string>
#include <vector>

#include "bot_api.h"

/* string package 12 `BOT` (SPEC-741 Messages) */
#define BOTMSG(i) (0x0C00 | (i))
enum {
	BM_VERSION = 0, BM_HEADER = 1, BM_SPAWNPOINT = 2, BM_PLACEMENT = 3, BM_DISCONNECTED = 4, BM_NOFILE = 5,
	BM_LOADED = 6, BM_EDITMODE = 7, BM_CANTCREATE = 8, BM_WRITTEN = 9, BM_NOCONTROL = 23, BM_LIMIT = 24,
	BM_NOSLOT = 25
};

#define BOT_UNSET "??????"
/* SPEC-AMEND-005 G5.1: a bot carries server flag 0x200 ("client entity without a network connection"; the engine
 * drops sends addressed to it) and no entity flag; "the bot flag" of SPEC-741 3 is game-private (Bot::inuse,
 * gclient_t::pers.isbot). Removal clears the whole server-flags field. */
#define BOT_SVFLAG 0x200
#define BOT_WP_MAX 500         /* waypoint capacity (index 0 is an unused dummy) */
#define BOT_PATH_MAX 255
#define BOT_LINKS 8
#define WP_LINKFLAG 0x8000
#define BOT_TRACE_MASK 0x2010003 /* SPEC-741 14.3 */

/* waypoint kinds (SPEC-741 Amendments to SPEC-143) */
enum { WPK_STANDARD = 0, WPK_CROUCH = 1, WPK_JUMP = 2, WPK_CONTROL = 3, WPK_ELEVATOR = 4, WPK_VISIBLE = 9,
       WPK_LADDER_BOTTOM = 10, WPK_LADDER_TOP = 11 };

//------------------------------------------------------------------------------------------------
// personalities (bot_personality.cpp)

struct BotPersonality {
	char name[64];
	bool valid;
	char userinfo[513];
	int accuracy, fov, view_distance, turning_speed, intelligence, canchat, chatfreq;
	std::vector<std::string> sect[7]; /* file sections [SECT=1]..[SECT=6] */
	int uses;                         /* use count this level */
};

extern std::vector<BotPersonality> g_bot_pers;
void BotPers_Load(void);
void BotPers_Free(void);
int BotPers_Find(const char *name);    /* case-insensitive; no match -> 0 (the first personality) */
int BotPers_Choose(bool count_use);    /* SPEC-741 4 "Choice for a new bot" without a requested name */
void BotPers_NewLevel(void);           /* use counts and the random round restart per level */

//------------------------------------------------------------------------------------------------
// waypoints (bot_waypoints.cpp)

struct BotWaypoint {
	vec3_t origin;
	int kind;
	int tag;                 /* another waypoint id or 0 */
	float next_d2;           /* stored, unused */
	int pref;                /* weapon preference (run time) */
	int spawn;               /* waypoint was created for / belongs to a spawn point (0 = no) */
	int nlinks;
	int link[BOT_LINKS];     /* target id | WP_LINKFLAG */
	float cost[BOT_LINKS];   /* squared distance */
	int path;                /* path index or -1 */
};

struct BotPath {
	int start, end;          /* first and last waypoint id */
};

/* one juncture entry of a path's list (SPEC-AMEND-005 G5.7): waypoint `node` of the path joins waypoint `onode` of path
 * `opath` (opath = -1: dead end). A real juncture has an entry in both paths' lists. Each list is ordered by `node`;
 * entries at the same node keep their creation order. */
struct BotJuncture {
	int node;
	int opath, onode;
};

struct BotSpawnRef {
	int cls;                 /* 0 deathmatch, 1 team1, 2 team2 */
	int ordinal;             /* spawn order within its class */
	edict_t *ent;
	int wp;
};

struct BotWaypointSystem {
	bool initialised;
	bool ready;
	bool roam;               /* bot_waypoints 0 */
	bool editmode;
	bool written;            /* written once per level (no name, not edit mode) */
	int version;
	int count;               /* waypoints 1..count */
	BotWaypoint wp[BOT_WP_MAX + 1];
	int npaths;
	BotPath paths[BOT_PATH_MAX];
	std::vector<BotJuncture> pj[BOT_PATH_MAX]; /* juncture list of each path */
	std::vector<BotSpawnRef> spawns;
	int blue_base, red_base; /* CTF base waypoints */
	std::vector<int> control_points;
	std::vector<edict_t *> control_flags;
	int conquer_goal;
	int edit_cursor;         /* last placed waypoint */
	int draw_from, draw_to;  /* bot_wp_drawonly */
	int draw_rr;
};

extern BotWaypointSystem g_wps;
void BotWp_Reset(void);
void BotWp_Init(void);                       /* SPEC-741 1.2, at worldspawn + 10 s */
bool BotWp_Load(const char *mapbase);
void BotWp_BuildPaths(void);
void BotWp_MarkLadders(void);
void BotWp_BuildSpawnTable(void);
void BotWp_TagItems(void);
void BotWp_AutoLinks(void);
bool BotWp_Write(const char *name);
int BotWp_Nearest(const vec3_t p, float maxdist, bool needvisible, edict_t *ignore, bool skip_spawn);
int BotWp_LinkTarget(int link) ;
bool BotWp_Links(int a, int b);              /* a has a link to b */
int BotWp_SpawnWaypoint(edict_t *spot);
int BotWp_EditorCommand(edict_t *ent, const char *cmd);
void BotWp_EditFrame(void);
bool BotWp_Visible(const vec3_t a, const vec3_t b, edict_t *ignore);
/* routes */
struct BotRouteStep {
	int node;
	int path;
};
bool BotWp_Route(int start, int dest, std::vector<BotRouteStep> &out);

//------------------------------------------------------------------------------------------------
// per-bot state

struct BotNoise {
	float time;              /* level time of the latest noise */
	float expiry;            /* time + 1 s; the record is valid while now < expiry (SPEC-AMEND-005 G5.5) */
	float loudness;
	int kind;
};

struct BotChatLine {
	bool pending;
	int index;               /* chat index 1..6 */
	char text[257];
	float say_time;
	int team;
};

struct BotRouteCache {
	int dest;
	float time;
	std::vector<BotRouteStep> steps;
	int pos;
};

struct BotDanger {
	edict_t *ent;
	int kind;
};

struct Bot {
	bool inuse;
	int clientnum;           /* edict number 1..maxclients */
	int slot;                /* bot_name<slot>, -1 = fill/none */
	char slotvalue[64];      /* value of the slot when the bot was added */
	int pers;
	char userinfo[MAX_INFO_STRING];
	BotShared shared;
	int team;

	/* think */
	float idle_chat_time;
	int last_weapon;
	float weapon_hold_until;
	float door_pause_until;
	float no_ammo_until;
	edict_t *enemy;
	vec3_t last_fight_pos;   /* where the bot stood when it last fought */
	vec3_t enemy_last_pos;
	float view_yaw, view_pitch;
	float yaw_goal;
	int aim_err;
	float aim_err_timer;
	float next_fire;
	int strafe;              /* 1 right, -1 left, 0 none */
	float strafe_switch;
	float strafe_pause_until;
	bool retreating;
	/* last-sighting chase */
	int chase_stage;         /* 0 none, 1 back to last fight spot, 2 to the enemy's last position */
	float chase_until;
	/* items */
	edict_t *item;
	float item_giveup;
	float item_retry;
	/* waypoints */
	int cur_wp;
	int last_wp;             /* refreshed every think (SPEC-AMEND-005 G5.7 "Start") */
	int prev_wp;             /* the waypoint reached last (ladder rule) */
	bool left_jump_wp;       /* the waypoint reached last was kind 2 (SPEC-AMEND-005 G5.6) */
	int roam_s, roam_ds;     /* remembered roaming pair (waypoint, walk direction), initially (0, +1) (G5.8) */
	float wp_deadline;
	int walk_dir;
	int dest_wp;
	vec3_t dest_origin;
	BotRouteCache route;
	float jump_until;
	float crouch_wp;
	float hold_switch;       /* Conquer hold facing alternation */
	int hold_side;
	/* explosives */
	float next_grenade;
	bool run_tactic;
	float switch_lock_until;  /* item lock: 2 s after the run tactic ends (SPEC-AMEND-005 G5.4) */
	bool switch_in_progress;  /* the last grenade request was "switching" */
	/* command */
	float fwd, side, up;
	int buttons;
	float up_suppress_until;
	float fire_pri, fire_alt; /* this frame's fire request: 1.0 in the requested mode, else 0 (SPEC-AMEND-005 G5.3) */
	/* modes */
	int ctf_role;            /* 0 attacker, 1 defender */
	float ctf_next_eval;
	int ctf_objective;
	int control_goal;
	/* chat */
	BotChatLine chat;
	edict_t *chat_target;
	bool dead_reset;
};

extern Bot g_bots[MAX_CLIENTS + 1];  /* indexed by edict number */
extern BotNoise g_noise[MAX_CLIENTS + 1];
extern std::vector<BotDanger> g_danger;
extern bool g_bot_active;            /* worldspawn ran under deathmatch rules */
extern float g_bot_wp_init_time;

Bot *BotFor(const edict_t *ent);
void Bot_Think(Bot *b, edict_t *ent, usercmd_t *cmd);
void Bot_RemoveClient(Bot *b, bool resetslot);
float BotMax(void);
bool BotRetreats(Bot *b, edict_t *ent);
bool BotAudible(Bot *b, edict_t *ent, edict_t *other);
bool BotVisible(edict_t *self, edict_t *other);
const BotPersonality &BotP(const Bot *b);
void BotChat_Frame(void);
void BotChat_Trigger(Bot *b, edict_t *x, int index);
void BotChat_Idle(Bot *b, edict_t *ent);
void BotChat_Announce(Bot *b, edict_t *ent, int objective);
PlayerInventory *BotInv(edict_t *ent);
int BotCurWeapon(edict_t *ent);
bool BotCarriesFlag(edict_t *ent);

/* cvars */
extern cvar_t *bot_waypoints, *bot_wp_edit, *bot_wp_info, *bot_listfile, *bot_random, *bot_fillserv,
	*bot_configexec, *bot_chat, *bot_wppref, *bot_hunt, *bot_notarget, *bot_runtact;

static inline void BotStrCpy(char *dst, const char *src, size_t cap)
{
	if (!cap)
		return;
	strncpy(dst, src ? src : "", cap - 1);
	dst[cap - 1] = 0;
}
static inline void BotStrCat(char *dst, const char *src, size_t cap)
{
	size_t n = strlen(dst);
	if (n + 1 < cap)
		BotStrCpy(dst + n, src, cap - n);
}

/* map name without directories and extension (dm/jpndm2 -> jpndm2, SPEC-741 Files) */
static inline void BotMapBase(char *out, size_t cap)
{
	const char *s = level.mapname, *slash = strrchr(s, '/');
	if (slash)
		s = slash + 1;
	BotStrCpy(out, s, cap);
	char *dot = strrchr(out, '.');
	if (dot)
		*dot = 0;
}

/* printing helpers */
void BotPrintId(int id, ...);   /* to every client (SP_Print with no entity) */
void BotDevMsg(const char *fmt, ...);
void BotConsoleId(int id, ...); /* formatted with the string package, to the console */

#endif
