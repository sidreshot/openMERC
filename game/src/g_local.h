/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
 * OpenMerc: rewritten for the Soldier of Fortune 1.07f game module (G18). Keeps the Quake II structure
 * (edict_t, gclient_t, level/game locals, spawn temp, moveinfo) with the SoF entity prefix, key table,
 * properties, flags and the game-side interfaces to the engine, the Ghoul system and player.dll.
 * Implements: SPEC-001, SPEC-006, SPEC-760, SPEC-761, SPEC-780, SPEC-781, SPEC-782
 */
// g_local.h -- local definitions for game module
#pragma once

#include "q_shared.h"
#include "ghoul.h"
#include "player_api.h"

#define GAMEVERSION "base"

// server -> client message ids used by the game (SPEC-201)
#define svc_temp_entity 1
#define svc_effect 5
#define svc_sound 10
#define svc_stufftext 13
#define svc_restart_predn 29
#define svc_rebuild_pred_inv 30
#define svc_countdown 31
#define svc_playernamecols 33

//==================================================================

#define DAMAGE_TIME 0.5
#define FALL_TIME 0.3

// spawnflags consumed at spawn (SPEC-760 B1.3)
#define SPAWNFLAG_NOT_EASY 0x00000100
#define SPAWNFLAG_NOT_MEDIUM 0x00000200
#define SPAWNFLAG_NOT_HARD 0x00000400
#define SPAWNFLAG_NOT_DEATHMATCH 0x00000800

// edict->flags (SPEC-760 3)
#define FL_FLY 0x00000001
#define FL_SWIM 0x00000002
#define FL_IMMUNE_LASER 0x00000004
#define FL_INWATER 0x00000008
#define FL_GODMODE 0x00000010
#define FL_NOTARGET 0x00000020
#define FL_PARTIALGROUND 0x00000100
#define FL_WATERJUMP 0x00000200
#define FL_TEAMSLAVE 0x00000400
#define FL_NO_KNOCKBACK 0x00000800
#define FL_IGNORESIGHT 0x00001000 // SPEC-782 3: owner ignores the sight client
#define FL_LEANPROXY 0x00008000
#define FL_SPAWNED 0x00010000
#define FL_THROWNEQUIP 0x00400000
#define FL_ITEMLIST 0x00800000
#define FL_RESPAWN 0x80000000

// edict->svflags (SPEC-760 3)
#define SVF_NOCLIENT 0x00000001
#define SVF_DEADMONSTER 0x00000002
#define SVF_MONSTER 0x00000004
#define SVF_ROTATE_ROLL 0x00000008
#define SVF_ROTATE_PITCH 0x00000010
#define SVF_ROTATE_YAW 0x00000020
#define SVF_SAFEZONE 0x00000040
#define SVF_PLAYERPROXY 0x00000080

// solid values (SPEC-760 2)
#define SOLID_NOT 0
#define SOLID_TRIGGER 1
#define SOLID_BBOX 2
#define SOLID_BSP 3
#define SOLID_GROUND 4
#define SOLID_CORPSE 5

// entity state effects / renderfx bits used by the game (SPEC-760 3, SPEC-202 2.4)
#define EF_SOUNDISSET 0x00000100
#define EF_BUTTONIDLE 0x00000008
#define EF_BUTTONPRESSED 0x00000010
#define EF_ANIM_ALL 0x00000020
#define EF_ANIM_ALLFAST 0x00000040
#define EF_BUTTON1000 0x00001000
#define EF_AUTOROTATE 0x00002000
#define EF_RESPAWNED 0x00004000
#define RF_VIEWERMODEL 0x00000002
#define RF_LASERBEAM 0x00000020
#define RF_NOLERP 0x00000040
#define RF_INLINEMODEL 0x00000400
#define RF_BRUSHMODEL 0x00000800
#define RF_GHOUL 0x00400000

#define RDF_UNDERWATER 0x1
#define RDF_GOGGLES 0x4

#define FRAMETIME 0.1f

#define MELEE_DISTANCE 80
#define BODY_QUEUE_SIZE 8

// cvar flags (SPEC-302)
#define CVAR_ARCHIVE 0x0001
#define CVAR_USERINFO 0x0002
#define CVAR_SERVERINFO 0x0004
#define CVAR_NOSET 0x0008
#define CVAR_LATCH 0x0010
#define CVAR_INTEGER 0x0020
#define CVAR_WEAPON 0x0100
#define CVAR_ITEM 0x0200
#define CVAR_AMMO 0x0400
#define CVAR_MISC 0x0800

// print levels
#define PRINT_LOW 0
#define PRINT_MEDIUM 1
#define PRINT_HIGH 2
#define PRINT_CHAT 3

// sound channels
#define CHAN_AUTO 0
#define CHAN_WEAPON 1
#define CHAN_VOICE 2
#define CHAN_ITEM 3
#define CHAN_BODY 4
#define CHAN_NO_PHS_ADD 8
#define CHAN_RELIABLE 16
#define ATTN_NONE 0
#define ATTN_NORM 1
#define ATTN_IDLE 2
#define ATTN_STATIC 3

// pm_type (SPEC-202 4)
#define PM_NORMAL 0
#define PM_NOCLIP 1
#define PM_SPECTATOR 2
#define PM_DEAD 3
#define PM_GIB 4
#define PM_FREEZE 5
#define PM_REMOTE 6
#define PM_INTERMISSION_SPEC 7

#define PMF_DUCKED 0x01
#define PMF_JUMP_HELD 0x02
#define PMF_ON_GROUND 0x04
#define PMF_TIME_WATERJUMP 0x08
#define PMF_TIME_LAND 0x10
#define PMF_TIME_TELEPORT 0x20
#define PMF_EXHAUSTED 0x40
#define PMF_NO_PREDICTION 0x80

// user command buttons (SPEC-202 6.1)
#define BUTTON_ATTACK 0x01
#define BUTTON_USE 0x02
#define BUTTON_ALTATTACK 0x04
#define BUTTON_WEAPON1 0x08
#define BUTTON_WEAPON2 0x10
#define BUTTON_RUN 0x20
#define BUTTON_SKIP 0x40
#define BUTTON_ANY 0x80

// HUD stats indexes (SPEC-780 3)
#define STAT_ITEMTYPE 0
#define STAT_HEALTH 1
#define STAT_CLIP 2
#define STAT_AMMO 3
#define STAT_CLIPSIZE 4
#define STAT_ARMOR 5
#define STAT_WEAPON 6
#define STAT_ITEMCOUNT 7
#define STAT_FLAGS 8
#define STAT_LAYOUTS 9
#define STAT_FRAGS 10
#define STAT_METER 11
#define STAT_FORCEHUD 12
#define STAT_DAMAGEDIR 15

typedef enum { DAMAGE_NO, DAMAGE_YES, DAMAGE_AIM } damage_t;

#define DEAD_NO 0
#define DEAD_DYING 1
#define DEAD_DEAD 2
#define DEAD_RESPAWNABLE 3

#define RANGE_MELEE 0
#define RANGE_NEAR 1
#define RANGE_MID 2
#define RANGE_FAR 3

#define GIB_ORGANIC 0
#define GIB_METALLIC 1

// game.serverflags
#define SFL_CROSS_TRIGGER_MASK 0x000000ff

// movetypes (SPEC-760 2)
typedef enum {
	MOVETYPE_NONE,
	MOVETYPE_NOCLIP,
	MOVETYPE_PUSH,
	MOVETYPE_STOP,
	MOVETYPE_WALK,
	MOVETYPE_STEP,
	MOVETYPE_FLY,
	MOVETYPE_TOSS,
	MOVETYPE_FLYMISSILE,
	MOVETYPE_BOUNCE,
	MOVETYPE_DAN,       // 10 tumbling object
	MOVETYPE_DANTOUCH   // 11 as 10, touch runs while not solid
} movetype_t;

// damage flags (SPEC-780 / SPEC-782)
#define DAMAGE_RADIUS 0x00000001
#define DAMAGE_NO_ARMOR 0x00000002
#define DAMAGE_ENERGY 0x00000004
#define DAMAGE_NO_KNOCKBACK 0x00000008
#define DAMAGE_BULLET 0x00000010
#define DAMAGE_NO_PROTECTION 0x00000020
#define DAMAGE_EXPLOSIVE_HALF 0x00000080
#define DAMAGE_FIRE 0x00001000
#define DAMAGE_FALL 0x00002000
#define DAMAGE_DROWN 0x00004000

// means of death (SPEC-780 constants)
#define MOD_UNKNOWN 0
#define MOD_KNIFE_SLASH 1
#define MOD_KNIFE_THROWN 2
#define MOD_PISTOL1 3
#define MOD_PISTOL2 4
#define MOD_MPISTOL 5
#define MOD_ASSAULTRIFLE 6
#define MOD_SNIPERRIFLE 7
#define MOD_SLUG 8
#define MOD_SHOTGUN 9
#define MOD_MACHINEGUN 10
#define MOD_PHOS_GRENADE 11
#define MOD_ROCKET 12
#define MOD_ROCKET_SPLASH 13
#define MOD_MPG 14
#define MOD_FLAMEGUN 15
#define MOD_FLAMEGUN_NAPALM 16
#define MOD_C4 17
#define MOD_CLAYMORE 18
#define MOD_NEURAL_GRENADE 19
#define MOD_WATER 20
#define MOD_SLIME 21
#define MOD_CRUSH 22
#define MOD_TELEFRAG 23
#define MOD_FALLING 24
#define MOD_SUICIDE 25
#define MOD_EXPLOSIVE 26
#define MOD_FIRE 27
#define MOD_LAVA 28
#define MOD_EXIT 29
#define MOD_BARBWIRE 30
#define MOD_DOGBITE 31
#define MOD_TRIGGER_HURT 32
#define MOD_SPLASH 33
#define MOD_TARGET_LASER 34
#define MOD_THROWINGSTAR 35
#define MOD_GRENADE 36
#define MOD_CONC_GRENADE 37
#define MOD_FRIENDLY_FIRE 0x08000000

// item spawnflags (SPEC-781)
#define ITEM_TRIGGER_SPAWN 0x00000001
#define ITEM_NO_FALL 0x00000002
#define ITEM_HIDDEN 0x00000008
#define ITEM_SP_RESPAWN 0x00000080
#define DROPPED_ITEM 0x00010000
#define ITEM_AMMO_TAKEN 0x00100000

//==================================================================

typedef struct edict_s edict_t;
typedef struct gclient_s gclient_t;

// mission statistics counters (SPEC-AMEND-007 G18.41, GAME 0x618 + 4 * index)
enum {
	MSTAT_ENEMIES, MSTAT_FRIENDLIES, MSTAT_THROATSHOTS, MSTAT_GROINSHOTS, MSTAT_HEADSHOTS, MSTAT_GIBS, MSTAT_SAVES,
	MSTAT_CASH, MSTAT_COUNT
};

typedef struct {
	char helpmessage1[512];
	char helpmessage2[512];
	int helpchanged;
	gclient_t *clients;
	char spawnpoint[512];
	int maxclients;
	int maxentities;
	int serverflags;
	qboolean autosaved;
	int cinematic;          // SPEC-001 export 0x54 state
	// difficulty record (SPEC-760 B9.2)
	int saves_allowed;
	int cheats;
	int skill_spawning;     // S
	int skill_toughness;    // T
	int skill_encumbrance;  // E
	float tparm[9];
	float sparm;
	float spawn_alertgain, spawn_alertfade, spawn_maxalert, spawn_maxinterval, spawn_maxnear, spawn_interval;
	int encumbrance;
	float difficulty_score;
	int lock_deaths;
	int mstat_total[8];      // mission totals (GAME 0x618), cleared only with the game record
	float mstat_time;       // mission played time (GAME 0x638)
} game_locals_t;

#define MAX_ITEM_ENTS 1024

typedef struct {
	int framenum;
	float time;
	char level_name[MAX_QPATH];
	char mapname[MAX_QPATH];
	char nextmap[MAX_QPATH];
	char ainame[MAX_QPATH];

	float intermissiontime;
	char *changemap;
	int exitintermission;
	vec3_t intermission_origin;
	vec3_t intermission_angle;

	edict_t *sight_client;
	edict_t *sight_entity;
	int sight_entity_framenum;
	edict_t *sound_entity;
	int sound_entity_framenum;
	edict_t *sound2_entity;
	int sound2_entity_framenum;

	int total_secrets, found_secrets, total_goals, found_goals, total_monsters, killed_monsters;
	edict_t *current_entity;
	int body_que;

	// SoF level state
	char materialflags[26];  // configstring 16 (SPEC-760 B1.5)
	int forcehud;
	int weaponsavailable;
	int weaponmode;          // noweapons / setweapons
	int maxdeadhostages;
	int deadhostages;
	int countdown_expired;
	int missionstatus;
	float missionstatus_time;
	int alarm;
	int startmusic;
	int objectives_changed;
	int objectives_jingle;
	float fadealpha;         // single-player death fade
	int precache_all;        // fs_createpak / spawn-only precache mode
	int num_controlflags;
	int num_dmspawns;
	float alert;             // world spawner alert meter
	float alert_cooldown;
	float last_alert_sound, last_alert_warning, last_world_spawn;
	int trigger_sounds;
	edict_t *trigger_sound_list[16];
	char spawn_classes[3][MAX_QPATH]; // worldspawn spawn1..3
	/* G7 */
	int g7_levelinit; // props/effects caches reset for this level (zeroed by every level spawn)
	/* end G7 */
	/* G10 */
	int g10_savesinit;  // saves_left was set for this level (zeroed by every level spawn)
	int g10_savesleft;  // manual saves left in this level (from game.saves_allowed; saved with the level record)
	/* end G10 */
	int mstat[8];       // this level's mission-statistics counters (MSTAT_*)
	float mstat_start;  // level time the counters started
} level_locals_t;

typedef struct {
	char *sky;
	float skyrotate;
	vec3_t skyaxis;
	char *nextmap;
	int lip;
	int distance;
	int height;
	char *noise;
	float pausetime;
	char *item;
	char *gravity;
	char *terrainset;
	char *musicset;
	vec3_t color;
	vec3_t endpoint;
	float minyaw, maxyaw, minpitch, maxpitch;
	char *script;
	int killedvalue, survivalvalue;
	int material;
	float scale;
	int merctype, mercnum;
	char *waitaction[6];
	char *moveaction;
	int screeneffect;
	float spawnfrequency;
	char *parm[10];
	int surfacetype;
	int noweapons, setweapons;
	int maxdeadhostages;
	char *ambientset;
	int startmusic;
	int forcehud;
	int weaponsavailable;
	char *ainame;
	int distcull;
} spawn_temp_t;

typedef struct {
	vec3_t start_origin;
	vec3_t start_angles;
	vec3_t end_origin;
	vec3_t end_angles;
	int sound_start;
	int sound_middle;
	int sound_end;
	float accel;
	float speed;
	float decel;
	float distance;
	float wait;
	int state;
	vec3_t dir;
	float current_speed;
	float move_speed;
	float next_speed;
	float remaining_distance;
	float decel_distance;
	void (*endfunc)(edict_t *);
} moveinfo_t;

extern game_locals_t game;
extern level_locals_t level;
extern game_export_t globals;
extern spawn_temp_t st;

// engine import table with Quake II style helpers (the raw SPEC-001 table is gix)
extern game_import_t gix;
struct gi_wrap {
	int modelindex(const char *n) { return gix.modelindex(n); }
	int soundindex(const char *n) { return gix.soundindex(n); }
	int effectindex(const char *n) { return gix.effectindex(n); }
	int imageindex(const char *n) { return gix.imageindex(n); }
	void setmodel(edict_t *e, const char *n) { gix.setmodel(e, n); }
	int argc() { return gix.argc(); }
	char *argv(int n) { return gix.argv(n); }
	char *args() { return gix.args(); }
	void bprintf(int level, const char *fmt, ...);
	void dprintf(const char *fmt, ...);
	void cprintf(edict_t *e, int level, const char *fmt, ...);
	void centerprintf(edict_t *e, const char *fmt, ...);
	void error(const char *fmt, ...);
	void sound(edict_t *e, int chan, int idx, float vol, float att, float ofs, int local = 0);
	void positioned_sound(const float *org, edict_t *e, int chan, int idx, float vol, float att, float ofs,
	                      int local = 0);
	void configstring(int n, const char *s) { gix.configstring(n, s); }
	trace_t trace(const float *start, const float *mins, const float *maxs, const float *end, edict_t *pass,
	              int mask);
	int pointcontents(const float *p) { return gix.pointcontents(p); }
	int inPVS(const float *a, const float *b) { return gix.inPVS(a, b); }
	int inPHS(const float *a, const float *b) { return gix.inPVS(a, b); } // #57 is unused by the original
	void SetAreaPortalState(int n, qboolean open) { gix.SetAreaPortalState(n, open); }
	void linkentity(edict_t *e) { gix.linkentity(e); }
	void unlinkentity(edict_t *e) { gix.unlinkentity(e); }
	int BoxEdicts(const float *mins, const float *maxs, edict_t **list, int max, int area)
	{
		return gix.BoxEdicts(mins, maxs, list, max, area);
	}
	void Pmove(pmove_t *pm) { gix.Pmove(pm); }
	void multicast(const float *org, int to) { gix.multicast(org, to); }
	void unicast(edict_t *e, qboolean rel) { gix.unicast(e, rel); }
	void WriteChar(int c) { gix.WriteChar(c); }
	void WriteByte(int c) { gix.WriteByte(c); }
	void WriteShort(int c) { gix.WriteShort(c); }
	void WriteLong(int c) { gix.WriteLong(c); }
	void WriteFloat(float f) { gix.WriteFloat(f); }
	void WriteString(const char *s) { gix.WriteString(s); }
	void WritePosition(const float *p) { gix.WritePosition(p); }
	void WriteDir(const float *d) { gix.WriteDir(d); }
	void WriteAngle(float f) { gix.WriteAngle(f); }
	void *TagMalloc(int size, int tag) { return gix.TagMalloc(size, tag); }
	void TagFree(void *p) { gix.TagFree(p); }
	void FreeTags(int tag) { gix.FreeTags(tag); }
	cvar_t *cvar(const char *n, const char *v, int flags) { return gix.cvar(n, v, flags, NULL); }
	cvar_t *cvar_set(const char *n, const char *v) { return gix.cvar_set(n, v); }
	cvar_t *cvar_forceset(const char *n, const char *v) { return gix.cvar_forceset(n, v); }
	void AddCommandString(const char *t) { gix.AddCommandString(t); }
};
extern gi_wrap gi;

extern int meansOfDeath;
extern edict_t *g_edicts;

#define FOFS(x) (int)offsetof(edict_t, x)
#define STOFS(x) (int)offsetof(spawn_temp_t, x)
#define LLOFS(x) (int)offsetof(level_locals_t, x)
#define CLOFS(x) (int)offsetof(gclient_t, x)

#define random() ((rand() & 0x7fff) / ((float)0x7fff))
#define crandom() (2.0 * (random() - 0.5))

extern cvar_t *maxentities;
extern cvar_t *deathmatch;
extern cvar_t *dmflags;
extern cvar_t *skill;
extern cvar_t *fraglimit;
extern cvar_t *timelimit;
extern cvar_t *password;
extern cvar_t *spectator_password;
extern cvar_t *dedicated;
extern cvar_t *sv_gravity, *sv_gravityx, *sv_gravityy, *sv_gravityz;
extern cvar_t *sv_maxvelocity;
extern cvar_t *sv_rollspeed;
extern cvar_t *sv_rollangle;
extern cvar_t *run_pitch;
extern cvar_t *run_roll;
extern cvar_t *bob_up;
extern cvar_t *bob_pitch;
extern cvar_t *bob_roll;
extern cvar_t *sv_cheats;
extern cvar_t *maxclients;
extern cvar_t *maxspectators;
extern cvar_t *flood_msgs;
extern cvar_t *flood_persecond;
extern cvar_t *flood_waitdelay;
extern cvar_t *flood_killdelay;
extern cvar_t *sv_maplist;
extern cvar_t *g_movescale;
extern cvar_t *sv_suicidepenalty;
extern cvar_t *ghl_specular, *ghl_light_method, *ghl_mip, *ghl_no_server_update;
extern cvar_t *ghl_precache_verts, *ghl_precache_texture;
extern cvar_t *freezeworld;
extern cvar_t *ctf_control_delay_time, *ctf_flag_respawn_time;
extern cvar_t *ai_spawnfrequency;
extern cvar_t *ei_show;
extern cvar_t *pickupinfo;
extern cvar_t *gl_pictip;
extern cvar_t *fs_createpak;
extern cvar_t *thirdpersoncam, *followenemy, *camera_viewdist;
extern cvar_t *sv_pmodlistfile, *sv_altpmodlistfile;

extern edict_t *world; // = g_edicts (a variable, not a macro: packages may use "world" as an identifier)

// fields for spawning from the entity string (SPEC-760 4)
#define FFL_SPAWNTEMP 1
#define FFL_NOSPAWN 2

typedef enum {
	F_INT,
	F_SHORT,
	F_FLOAT,
	F_LSTRING,
	F_GSTRING,
	F_VECTOR,
	F_ANGLEHACK,
	F_EDICT,
	F_ITEM,
	F_CLIENT,
	F_FUNCTION,
	F_IGNORE
} fieldtype_t;

typedef struct {
	const char *name;
	int ofs;
	fieldtype_t type;
	int flags;
} field_t;

extern field_t fields[];

//==================================================================
// item table (SPEC-781)
enum { IK_AMMO = 0, IK_WEAPON = 1, IK_ARMOR = 2, IK_EQUIP = 3, IK_UNUSED = 4, IK_HEALTH = 5 };

typedef struct gitem_s {
	const char *classname;
	int kind;
	int value;          // ammo type, weapon id, item type
	int defcount;
	float respawn;
	int standard;       // survives dmflags 0x2000
	const char *msg_pickup, *msg_secondary, *msg_refuse;
	const char *dir, *model, *skin;
	float scale;
	float boxbottom;
	const char *fadeseq;
	const char *name;
} gitem_t;

extern gitem_t itemlist[];
#define NUM_ITEMS 39
#define ITEM_INDEX(x) ((int)((x) - itemlist) + 1)

//==================================================================
// Ghoul glue (g_ghoul.cpp)
struct GhoulModel {
	char path[MAX_QPATH * 2];
	ghoul::IGhoulObject *obj;
};
ghoul::IGhoulSystem *GhoulSys();
ghoul::IGhoulObject *GhoulModelObject(const char *dir, const char *model, const char *skin, bool allskins);
ghoul::IGhoulObject *GhoulModelObjectSkinned(const char *dir, const char *model, const char *skin);
ghoul::IGhoulInst *GhoulSetModel(edict_t *ent, const char *dir, const char *model, const char *skin, float scale,
                                 const char *seq = NULL, int mode = 0);
ghoul::IGhoulInst *GhoulSetModelSkinned(edict_t *ent, const char *dir, const char *model, const char *skin, float scale,
                                 const char *seq = NULL, int mode = 0); // one-skin record (single ammo)
void GhoulPlay(edict_t *ent, const char *seq, int mode, float offset = 0, bool restart = true);
float GhoulSeqDuration(edict_t *ent, const char *seq, int mode);
void GhoulRemove(edict_t *ent);
void GhoulResetLevel(void);
void GhoulEntityEvents(edict_t *ent); // SPEC-700 rule 7.4, called per entity by G_RunFrame
void GhoulUpdateTransform(edict_t *ent);
bool GhoulBoltPos(edict_t *ent, const char *bolt, vec3_t out);
bool GhoulInstBoltPos(ghoul::IGhoulInst *inst, const vec3_t origin, const vec3_t angles, const char *bolt,
                      vec3_t out);
void GhoulSetTint(edict_t *ent, float r, float g, float b, float a);
void GhoulPartsOff(edict_t *ent, const char *part);
int GhoulRayHit(edict_t *ent, const vec3_t start, const vec3_t dir, float maxdist, float *dist, vec3_t point);

//==================================================================
// player module (g_player.cpp)
extern PlayerExport *pe;
void PlayerModuleLoad(void);
void PlayerModuleUnload(void);
void PlayerModuleLevelStart(void);
void PlayerModuleForgetViewModels(void);
// helper entities (SPEC-780 §15, SPEC-AMEND-005 G2.8): append `helper` at the end of `owner`'s circular helper list;
// take an entity out of the list it is in; the entity a helper stands for (first one without server flag 0x80)
void G_HelperAppend(edict_t *owner, edict_t *helper);
void G_HelperUnlink(edict_t *e);
edict_t *G_HelperResolve(edict_t *e);
void G_MarkSurfaceSounds(void); // g_material.cpp
void G_DebrisReset(void);
void G_DebrisSurface(int surfacetype);
void G_InstallG3Hooks(void);    // g_combat.cpp: the G3 hooks owned by G18
void ClientLeaveQuiet(edict_t *ent); // p_client.cpp: leave without the broadcast (bot removal, G5)
// deathmatch player models (g_pmodel.cpp, SPEC-780 16)
void PModel_LevelStart(void);
void PModel_Shutdown(void);
void PModel_Setup(edict_t *ent);
bool PModel_Resolve(const char *skin, char *model, int mlen, char *team, int tlen);
void PModel_Animate(edict_t *ent);
void PModel_ThrowUsed(edict_t *ent);
void PModel_Died(edict_t *ent);
void PModel_ArmourChanged(edict_t *ent, bool armoured);
PlayerInventory *PlayerNewInventory(void);   // gets a neutral owner context while PlayerSpawning(ent) is set
void PlayerDeleteInventory(PlayerInventory *inv);
void PlayerSpawning(edict_t *ent);            // PutClientInServer: client being spawned, NULL when done
void PlayerSetIdleContext(edict_t *ent);      // owner context with no buttons (spawn, scripts)
WeaponDesc *PlayerWeaponDesc(int weapon);
void PlayerThinkInventory(edict_t *ent, int attack);
int InvCurWeapon(PlayerInventory *inv);
ghoul::IGhoulInst *InvViewInst(PlayerInventory *inv);
int InvSpread(PlayerInventory *inv);
void PredictionRestart(edict_t *ent);

//==================================================================
// g_cmds.cpp
void Cmd_Help_f(edict_t *ent);
void Cmd_Score_f(edict_t *ent);

// g_items.cpp
void PrecacheItem(gitem_t *it);
void InitItems(void);
gitem_t *FindItemByClassname(const char *classname);
gitem_t *GetItemByIndex(int index);
gitem_t *FindItemForWeapon(int weapon);
gitem_t *FindItemForItemType(int itemtype);
qboolean SpawnItemCheck(edict_t *ent, gitem_t *item);
void SpawnItem(edict_t *ent, gitem_t *item);
void ItemsLevelStart(void);
void ItemsFinalize(void);
void ItemsFrame(void);
void ItemRemoveFromList(edict_t *ent);
edict_t *DropWeaponItem(edict_t *ent, int weapon, int rounds);
edict_t *DropEquipItem(edict_t *ent, int itemtype, int count);
edict_t *SpawnKnifePickup(const vec3_t origin, const vec3_t angles, float life);
void RevealHiddenItems(const char *targetname);
void Cmd_ListPickups_f(edict_t *ent);

// g_utils.cpp
qboolean KillBox(edict_t *ent);
void G_ProjectSource(vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result);
edict_t *G_Find(edict_t *from, int fieldofs, const char *match, int len = 0);
edict_t *findradius(edict_t *from, const vec3_t org, float rad, int area = 1);
int G_RadiusList(const vec3_t org, float rad, edict_t **list, int areas);
edict_t *G_PickTarget(const char *targetname);
void G_UseTargets(edict_t *ent, edict_t *activator);
void G_SetMovedir(vec3_t angles, vec3_t movedir);
void G_InitEdict(edict_t *e);
edict_t *G_Spawn(void);
void G_FreeEdict(edict_t *e);
void G_FreeEdictNow(edict_t *e);
void G_TouchTriggers(edict_t *ent);
void G_TouchSolids(edict_t *ent);
char *G_CopyString(const char *in);
float *tv(float x, float y, float z);
char *vtos(const vec3_t v);
float vectoyaw(const vec3_t vec);
void vectoangles(const vec3_t vec, vec3_t angles);
void G_UseKey(edict_t *player);
void G_MaterialFlags(int material, int surfacetype);
void G_SetMaterialFlag(int pos, char c); // AMEND-005 G18.27 writers (bodies, props, remote cameras)
qboolean G_IsClient(edict_t *e);
edict_t *G_FirstPlayer(void);

// g_combat.cpp
qboolean OnSameTeam(edict_t *ent1, edict_t *ent2);
qboolean CanDamage(edict_t *targ, edict_t *inflictor);
void T_Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t dir, const vec3_t point,
              const vec3_t normal, int damage, int knockback, int dflags, int mod);
void T_RadiusDamage(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod,
                    int dflags = 0);
void T_RadiusDamageMinMax(edict_t *inflictor, edict_t *attacker, const vec3_t center, float maxdmg, float mindmg,
                          float radius, edict_t *ignore, int mod, int dflags);
void Killed(edict_t *targ, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);

// g_misc.cpp
void ThrowHead(edict_t *self, const char *gibname, int damage, int type);
void ThrowClientHead(edict_t *self, int damage);
void ThrowGib(edict_t *self, const char *gibname, int damage, int type);
void BecomeExplosion1(edict_t *self);

// g_phys.cpp
void G_RunEntity(edict_t *ent);
void SV_AddGravity(edict_t *ent);

// g_main.cpp
void SaveClientData(void);
void FetchClientEntData(edict_t *ent);
void EndDMLevel(void);

// g_chase.cpp
void UpdateChaseCam(edict_t *ent);
void ChaseNext(edict_t *ent);
void ChasePrev(edict_t *ent);
void GetChaseTarget(edict_t *ent);

// p_view.cpp
void ClientEndServerFrame(edict_t *ent);

// p_hud.cpp
void MoveClientToIntermission(edict_t *client);
void G_SetStats(edict_t *ent);
void G_SetSpectatorStats(edict_t *ent);
void G_CheckChaseStats(edict_t *ent);
void ValidateSelectedItem(edict_t *ent);
void DeathmatchScoreboardMessage(edict_t *client, edict_t *killer);
void BeginIntermission(edict_t *targ);

// p_client.cpp
void respawn(edict_t *ent);
void PutClientInServer(edict_t *ent);
void InitClientPersistant(gclient_t *client);
void InitClientResp(gclient_t *client);
void InitBodyQue(void);
void ClientBeginServerFrame(edict_t *ent);
void player_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);
void PlayerNoise(edict_t *who, const vec3_t where, int type);
qboolean ClientConnect(edict_t *ent, char *userinfo);
qboolean ClientPreConnect(edict_t *ent, char *userinfo);
void ClientUserinfoChanged(edict_t *ent, char *userinfo, qboolean ingame);
void ClientDisconnect(edict_t *ent);
void ClientDropped(edict_t *ent);
void ClientBegin(edict_t *ent);
void ClientCommand(edict_t *ent);
void ClientThink(edict_t *ent, usercmd_t *cmd);
void SpawnLoadout(edict_t *ent);

// p_trail.cpp
void PlayerTrail_Init(void);
void PlayerTrail_Add(vec3_t spot);
void PlayerTrail_New(vec3_t spot);
edict_t *PlayerTrail_PickFirst(edict_t *self);
edict_t *PlayerTrail_PickNext(edict_t *self);
edict_t *PlayerTrail_LastSpot(void);

// g_weapon.cpp (SPEC-782)
void WeaponsLevelPrecache(void);
void WeaponPrecacheAttack(int attack);
void WeaponFire(edict_t *ent, PlayerInventory *inv, int alt);
int WeaponUseFlashpack(edict_t *ent, ItemSlot *slot);
int WeaponUseC4(edict_t *ent, ItemSlot *slot);
int WeaponUseClaymore(edict_t *ent, ItemSlot *slot);
int WeaponUseMedkit(edict_t *ent, ItemSlot *slot);
int WeaponUseGrenade(edict_t *ent, ItemSlot *slot);
int WeaponGoggles(edict_t *ent, ItemSlot *slot);
void WhiteOut(const vec3_t point, float radius, float cap, float fade);
void SetOnFire(edict_t *targ, edict_t *attacker);

// g_effects.cpp
void FX_Effect(int effect, const vec3_t origin, int multicast_to, edict_t *exclude = NULL);
void FX_EffectEnt(int effect, edict_t *ent, int multicast_to);
void FX_EffectBolt(int effect, edict_t *owner, int bolt, int multicast_to);
void FX_EffectDir(int effect, const vec3_t origin, const vec3_t dir, int multicast_to, edict_t *exclude = NULL);
void FX_EffectBeam(int effect, const vec3_t start, const vec3_t end, int multicast_to, edict_t *exclude = NULL);
void FX_SetSlot(edict_t *ent, int slot, const char *effect, int bolt = 0, float scale = 0);
void FX_ClearSlots(edict_t *ent);
void WriteUnit1000(const vec3_t v);
void WriteUnit2048(const vec3_t v);
void WriteImpactWorld(const vec3_t point, const vec3_t normal);

// g_sofents.cpp (SPEC-761 shared helpers)
void G_CameraShake(edict_t *player, float intensity);
void G_ExplosionStimulus(const vec3_t center, float radius, float strength);
void G_ReportNoise(edict_t *source, const vec3_t where, float radius, int kind);
void G_WorldSpawnerFrame(void);
void G_AlertNoise(float amount);
void G_SoundSetLookup(edict_t *ent);

// g_strings.cpp
int SP_Id(const char *package, const char *entry);
void SP_PrintTo(edict_t *ent, const char *package, const char *entry);
void PlayerMarkWeapon(int weapon);

// g_placeholder.cpp
void SP_Placeholder(edict_t *ent);
void G_PlaceholderReset(void);

// g_func.cpp
void G_MoverSoundsFrame(void);
void G_MoverSoundsReset(void);
void G_AddEvent(edict_t *ent, int ev, int parm);
void SV_CheckGround(edict_t *ent);
void G_TriggerSoundNoise(edict_t *source, const vec3_t where);

// g_save.cpp
void WriteGame(qboolean autosave);
qboolean ReadGame(int unused);
void WriteLevel(void);
void ReadLevel(void);
qboolean SaveAllowed(void);
void SavesLeft(void);
void GameStats(void);
void UpdateInven(void);

// mission statistics (g_stats.cpp, SPEC-AMEND-007 G18.41)
void Stats_LevelReset(void);
void Stats_Count(int which, int n);
void Stats_DamageHook(int which);
void Stats_Accumulate(void);
void Stats_Publish(void);
void Stats_UpdateInvFinal(edict_t *ent);

//============================================================================

// client_t->anim_priority
#define ANIM_BASIC 0
#define ANIM_WAVE 1
#define ANIM_JUMP 2
#define ANIM_PAIN 3
#define ANIM_ATTACK 4
#define ANIM_DEATH 5
#define ANIM_REVERSE 6

// client data that stays across multiple level loads
typedef struct {
	char userinfo[MAX_INFO_STRING];
	char netname[16];
	char bestweap[8];
	qboolean connected;
	int health;
	int max_health;
	int savedFlags;
	int weapon;        // remembered weapon id
	float fov;
	qboolean spectator;
	qboolean predicting;
	qboolean isbot;
} client_persistant_t;

// client data that stays across deathmatch respawns
typedef struct {
	client_persistant_t coop_respawn;
	int enterframe;
	int score;
	vec3_t cmd_angles;
	qboolean spectator;
	int team;
	float last_kill_cmd;
} client_respawn_t;

// this structure is cleared on each PutClientInServer, except for 'client->pers'
struct gclient_s {
	player_state_t ps;   // 0: communicated by server to clients
	int ping;            // 200: written by the engine

	client_persistant_t pers;
	client_respawn_t resp;
	pmove_state_t old_pmove;

	PlayerInventory *inv;      // player.dll inventory object
	int resync;                // prediction resync counter
	char modelname[MAX_QPATH];
	char teamname[MAX_QPATH];
	char skinname[MAX_QPATH];
	int female;
	bool skin_star, team_star; // userinfo skin/teamname were replaced: `*` before them in the player configstring
	// SPEC-AMEND-006 G18.29 step 3: the name, `skin` and `teamname` the last model set-up was called with
	bool pm_set_valid;
	char pm_set_name[16], pm_set_skin[MAX_QPATH], pm_set_team[MAX_QPATH];

	qboolean showscores;
	qboolean showhelp;
	float showhelp_time;

	int buttons;
	int oldbuttons;
	int latched_buttons;
	qboolean attack_rearmed;
	float firetime;            // pending primary fire time (SPEC-602 3)
	float altfiretime;
	int lightlevel;

	float killer_yaw;
	int damage_dir;            // stats[15] for this frame

	vec3_t kick_angles;        // weapon kick (ps.weaponkick mirror)
	vec3_t kick_origin;
	float v_dmg_roll, v_dmg_pitch, v_dmg_time;
	float kick_pitch, kick_roll, kick_endtime; // SPEC-780 12 view kick
	float fall_time, fall_value;
	float damage_alpha;
	float bonus_alpha;
	vec3_t damage_blend;
	float flash_alpha, flash_fade;
	float gold_alpha;
	float script_alpha;
	vec3_t script_blend;
	float fade_alpha, fade_rate; // func_fade
	vec3_t fade_color;
	vec3_t v_angle;
	float bobtime;
	float bobmove;
	int bobcycle;
	float bobfracsin;
	float xyspeed;
	vec3_t oldviewangles;
	vec3_t oldvelocity;

	float next_drown_time;
	int old_waterlevel;
	int breather_sound;
	int drown_damage;

	int machinegun_shots;

	int anim_end;
	int anim_priority;
	qboolean anim_duck;
	qboolean anim_run;

	float pickup_msg_time;
	float flood_locktill;
	float flood_when[10];
	int flood_whenhead;
	float respawn_time;
	float spawnprotect_time;
	float respawn_effect_time;

	edict_t *chase_target;
	qboolean update_chase;

	int lean;
	float viewheight;
	float legslow;            // leg-injury slowdown 0..0.5
	float stagger_time;       // knock-back stagger end
	float zapped_time;
	float mpg_timer;
	int goggles;
	float sniper_offset[2];
	int forcesuicide;
	int teamchange_pending;
	int wave_pending;
	int nameschanged;

	edict_t *camera;          // active func_remote_camera
	int camera_nest;
	float camera_savedfov;
	int remote_type;
	float thirdperson_yaw;
	vec3_t thirdperson_origin;
	int thirdperson;

	int pmodel;                // deathmatch player model index (g_pmodel.cpp, SPEC-780 16)
	char pm_anim[48];          // third-person sequence playing
	float anim_until;          // end of a once-played sequence of priority anim_priority
	bool throw_pending;        // a grenade/C4/flash pack/claymore was used: play the throw (SPEC-780 16)
	bool pm_crouched;          // posture of the third-person model (AMEND-005 G18.26 posture change)
	float pm_posture_until;    // end of the going-down / standing-up sequence
	int pm_lean;               // lean shown by the third-person model (SPEC-780 16: into / hold / out of)
	bool pm_lean_out;          // the out-of-lean sequence is playing
	float pm_lean_until;       // end of the into / out-of sequence
	int pm_handweapon;         // weapon whose model is in the right hand (-1 = none yet)
	bool armor_shown;          // the model shows the armoured parts/skin
	bool pm_rebuild;           // armour changed: rebuild the model at the end of the frame
	int pm_flagteam;           // team of the CTF flag bolt-on shown (0 none)
	struct GGBolt *pm_flagbolt;
	char pm_flagseq[24];

	edict_t *helper_lean, *helper_crouch, *helper_gun;
	bool lean_on;              // the view offset's lean trace ran this frame (SPEC-780 §12); its end point:
	vec3_t lean_end;
	float burn_time, phos_time;
	edict_t *burn_attacker;
	int mission_msg;
	int bot_gunsmoke;

	// later packages append their fields below, each inside a /* <package> */ block
	/* G3 */
	int g3_blood;              // damage taken this frame (SPEC-720 B4), read by the view blends
	int g3_armorsaved;         // armour absorbed this frame
	int g3_knockback;          // knockback this frame
	vec3_t g3_damagefrom;      // last damage point this frame
	vec3_t g3_push;            // separate push accumulator (SPEC-720 B8.2)
	/* end G3 */
};

struct edict_s {
	// engine-visible prefix (SPEC-760 1)
	entity_state_t s;              // 0x000
	struct gclient_s *client;      // 0x074
	qboolean inuse;                // 0x078
	int linkcount;                 // 0x07C
	void *area_prev, *area_next;   // 0x080 area link
	int num_clusters;              // 0x088
	int clusternums[32];           // 0x08C
	int headnode;                  // 0x10C
	int areanum, areanum2;         // 0x110
	int svflags;                   // 0x118
	vec3_t mins, maxs;             // 0x11C
	vec3_t absmin, absmax, size;   // 0x134
	int solid;                     // 0x158
	int clipmask;                  // 0x15C
	edict_t *owner;                // 0x160
	ghoul::IGhoulInst *ghoulInst;  // 0x164
	vec3_t engine_a;               // 0x168 1000.0 each
	vec3_t engine_b;               // 0x174 -1000.0 each
	vec3_t engine_c;               // 0x180 10000.0 each
	int engine_d;                  // 0x18C -1
	int engine_e[3];               // 0x190

	// game-private from here (0x19C)
	int movetype;
	int flags;

	char *model;
	float freetime;

	char *message;
	int sp_message;
	char *classname;
	int spawnflags;

	float timestamp;

	float angle;
	char *target;
	char *targetname;
	char *scripttarget;
	char *killtarget;
	char *killfacing;
	char *team;
	char *pathtarget;
	char *deathtarget;
	char *combattarget;
	char *soundname;
	char *spawn1, *spawn2, *spawn3;
	edict_t *target_ent;

	float speed, accel, decel;
	vec3_t movedir;
	vec3_t pos1, pos2;

	vec3_t velocity;
	vec3_t avelocity;
	int mass;
	float air_finished;
	float gravity;

	edict_t *goalentity;
	edict_t *movetarget;
	float yaw_speed;
	float ideal_yaw;

	float nextthink;
	void (*prethink)(edict_t *ent);
	void (*think)(edict_t *self);
	void (*blocked)(edict_t *self, edict_t *other);
	void (*touch)(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
	void (*use)(edict_t *self, edict_t *other, edict_t *activator);
	void (*plUse)(edict_t *self, edict_t *other, edict_t *activator); // use-key handler
	void (*pain)(edict_t *self, edict_t *other, float kick, int damage, vec3_t point); // SPEC-720 5.3 (hit point)
	void (*die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);

	float touch_debounce_time;
	float pain_debounce_time;
	float damage_debounce_time;
	float fly_sound_debounce_time;
	float last_move_time;
	float lastusetime;

	int health;
	int max_health;
	int gib_health;
	int deadflag;
	qboolean show_hostile;

	char *map;

	int viewheight;
	int takedamage;
	int dmg;
	int radius_dmg;
	float dmg_radius;
	int sounds;
	int count;

	edict_t *chain;
	edict_t *enemy;
	edict_t *oldenemy;
	edict_t *activator;
	edict_t *groundentity;
	int groundentity_linkcount;
	edict_t *teamchain;
	edict_t *teammaster;

	edict_t *mynoise;
	edict_t *mynoise2;

	int noise_index;
	int noise_index2;
	float volume;
	float attenuation;

	float wait;
	float delay;
	float random;

	float teleport_time;

	int watertype;
	int waterlevel;

	vec3_t move_origin;
	vec3_t move_angles;

	int light_level;
	int style;

	gitem_t *item;

	moveinfo_t moveinfo;

	// SoF properties (SPEC-760 1)
	float stopspeed;
	float friction;
	float airfriction;
	float buoyancy;
	float elasticity;
	edict_t *attachedto;
	float alertradius;
	int personality;
	int material;
	int surfacetype;
	int portalsheld;          // door holds its area portals open
	int doorflip;             // smart door direction
	int moversounds;          // mover sound set looked up
	float lastworked;         // portcullis / keep_using
	int armed;

	// items
	int itemcount;            // resolved count / rounds
	int itemstate;            // fade state
	float itemrespawn;        // pickup statistics etc.
	float refuse_time;
	int itemlistindex;
	vec3_t item_origin;
	vec3_t item_angles;

	// projectiles / equipment
	float dietime;
	int bounces;
	float fuse;
	edict_t *lasthit;
	float mpg_timer;
	float burn_time;
	edict_t *burn_attacker;
	int attack;
	float throwspeed;

	char *ghoulmodel;         // the model path set on ghoulInst
	int ghoulflags;
	int gotsky;
	// SPEC-780 §15 / SPEC-AMEND-005 G2.8: the helper link — an owner's helpers form a circular list owner → helper 1
	// → … → helper n → owner (G_HelperAppend / G_HelperUnlink / G_HelperResolve, g_utils.cpp)
	edict_t *helperlink;

	// later packages append their fields below, each inside a /* <package> */ block
	/* G3 */
	class G3Body *g3body;      // character/player body (SPEC-722), game/src/damage/damage_api.h
	int g3flags;               // G3F_* (damage/damage_api.h)
	edict_t *g3damageowner;    // sub-part: damage passes to this entity (SPEC-720 1.2)
	/* end G3 */
	/* G6b */
	int sg_script;             // script_runner: its script (handle + 1, 0 = none); SPEC-752 4.4 script link
	vec3_t sg_movedest;        // destination of the most recent timed script MOVE (SPEC-750 11, AMEND-003 2e)
	/* end G6b */
	/* G2 */
	class OmAI *ai;            // character AI object (SPEC-700), game/src/ai/ai_api.h (om_ai::AI)
	const struct G3Anim *ai_waitaction[6]; // path_corner / point_combat animation keys (SPEC-760 B8.1)
	const struct G3Anim *ai_moveaction;
	/* end G2 */
	/* G4 */
	struct RgChar *roster;     // roster record (SPEC-730..732), game/src/roster/roster_api.h
	/* end G4 */
};

// helpers
static inline int G_EdictNum(const edict_t *e) { return (int)(e - g_edicts); }
#define EDICT_NUM(n) (&g_edicts[(n)])
#define NUM_FOR_EDICT(e) G_EdictNum(e)

extern int sm_meat_index;
extern int snd_talk;

// sound helpers (g_effects.cpp)
void G_Sound(edict_t *ent, int chan, const char *name, float vol, float att, int local = 0);
void G_PosSound(const vec3_t org, edict_t *ent, int chan, const char *name, float vol, float att);
