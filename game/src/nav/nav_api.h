/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-740 (navigation runtime, node editor), SPEC-700 record offset 292 (navigation cache)
 * Owner: package G5 (game/src/nav). Read by G2 (AI: character manager, decisions), G18 (level spawn and client
 * command hooks) and the world spawner (SPEC-761). Do not edit outside G5; ask G5 for changes.
 *
 * Public interface of the single-player navigation runtime that reads maps/<mapname>.nvb (SPEC-143 + the SPEC-740
 * amendments). Multiplayer bots do not use it (SPEC-741 uses .wrs waypoints, see game/src/bots/).
 *
 * Who calls what (SPEC-740 section numbers):
 *   G18  Nav_LevelReset()          SpawnEntities, "reset navigation" (SPEC-760 B1.3 step 3)      rule 1
 *   G18  Nav_WorldSpawn()          SP_worldspawn: clears the per-node "seen" bytes             Level-wide state
 *   G18  Nav_Shutdown()            game shutdown                                               rule 1
 *   G18  Nav_ClientCommand(ent)    first thing in ClientCommand; returns 1 when it handled the command   rule 12
 *   G2   Nav_PlayerFrame(sight)    character manager, every frame while a sight client exists  rule 3
 *   G2   Nav_InitCache(c)          when a character record is created (SPEC-700 defaults)
 *   G2   Nav_RefreshCache(c, ent)  each awake character, every frame                           rule 4
 *   G2   Nav_MovePoint(...)        "approach" / "flee" queries of the decisions (SPEC-710)     rule 6
 *   G2   Nav_FleeSpot(...)         per-node precomputed flee node                              rule 7
 *   G2   Nav_LookAroundNode(...)   search decision C6                                          rule 8
 *   G2   Nav_StepTest(...)         move feasibility                                            rule 9
 *   G2   Nav_InCone(...)           cone helper                                                 rule 11
 *   SPEC-761 world spawner  Nav_HiddenSpawnSpot(...)                                         rule 10
 *
 * Door predicate: rules 6 and 9 accept "a door-like entity (SPEC-710) usable now". The default implementation
 * (nav_runtime.cpp) follows the SPEC-710 C9 wording; G2 owns the precise predicate and may install its own with
 * Nav_SetDoorPredicate().
 *
 * All functions are safe to call with no navigation loaded (they return their fallback).
 */
#ifndef OM_NAV_API_H
#define OM_NAV_API_H

#include "../g_local.h"

/* Per-character navigation cache, 96 bytes, SPEC-700 character record offset 292 (SPEC-740 "Per-character
 * navigation cache"). Layout is fixed: the record is saved with the character. */
struct NavCache {
	float ref[3];          /* 0  reference position (refresh: origin + 16 z; queries: the "from" point) */
	float movepoint[3];    /* 12 result of the last move-point query */
	int current_node;      /* 24 first node listed by the current layer; 0 = none */
	int current_layer;     /* 28 current layer id; -1 = none */
	int chosen_node;       /* 32 node chosen by the last query; 0 = none */
	int goal_node;         /* 36 goal node of the last query */
	int direct;            /* 40 1 = the last query produced no navigation point (move point = from) */
	int history[3];        /* 44 the three previous current-node values, most recent first */
	int unused[2];         /* 56 */
	int layer_copy[4];     /* 64 four copies of the current layer id (SPEC-700: default -1) */
	float last_dir[3];     /* 80 last accepted move direction (unit vector) */
	float last_dir_time;   /* 92 level time at which it was accepted */
};
static_assert(sizeof(NavCache) == 96, "SPEC-700/740 navigation cache is 96 bytes");

enum { NAV_APPROACH = 0, NAV_FLEE = 1 };

/* level life cycle */
void Nav_LevelReset(void);
void Nav_WorldSpawn(void);
void Nav_Shutdown(void);
int Nav_Active(void);              /* 1 when data is loaded and ai_nonodes = 0 */
int Nav_NodeCount(void);           /* N (0 = no navigation) */
const float *Nav_NodeOrigin(int node);  /* raised origin (+14), NULL for an invalid index */
float Nav_NodeClearance(int node);      /* tail1, 0 for an invalid index */
int Nav_PlayerNode(void);
int Nav_PlayerLayer(void);
int Nav_NodeSeen(int node);

/* per frame */
void Nav_PlayerFrame(edict_t *sight_client);
void Nav_InitCache(NavCache *c);
void Nav_RefreshCache(NavCache *c, edict_t *ent);

/* Rule 6. from = the character's origin, goal = destination (approach) or the position to flee from (flee),
 * ignore = the character's target (traces hitting it do not count as blocked), step_height = SPEC-700 record
 * offset 460 (default 24). Writes c->movepoint, chosen/goal node and the direct flag; returns !c->direct. */
int Nav_MovePoint(NavCache *c, edict_t *ent, float step_height, const vec3_t from, const vec3_t goal, int mode,
                  edict_t *ignore);
/* Rule 7. team 0 uses tail2, other teams tail3 (tail2 when tail3 = 0). Returns 1 and the node origin when found. */
int Nav_FleeSpot(NavCache *c, int team, vec3_t out);
/* Rule 8. */
int Nav_LookAroundNode(const vec3_t p, int n, vec3_t out);
/* Rule 9. Returns 1 = pass. The entity is not moved. */
int Nav_StepTest(edict_t *ent, float step_height, const vec3_t move);
/* Rule 10. Returns 1 and the node origin (with the +14 load offset; the spawner adds its own 8). */
int Nav_HiddenSpawnSpot(vec3_t out, int allow_seen);
/* Rule 11. */
int Nav_InCone(const vec3_t a, float ra, const vec3_t b, float rb, const vec3_t p);

/* door predicate used by rules 6 and 9: non-zero when `door` is door-like and usable now by `user` */
typedef int (*nav_door_predicate_t)(edict_t *door, edict_t *user);
void Nav_SetDoorPredicate(nav_door_predicate_t fn);
int Nav_DoorUsableNow(edict_t *door, edict_t *user);

/* developer node editor (rule 12-14); returns 1 when the command was an editor command */
int Nav_ClientCommand(edict_t *ent);

#endif
