/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 (multiplayer bots: hooks between the game module and the bot system)
 * Owner: package G5 (game/src/bots). Called from G18 (player/client, items, projectiles, game loop), G3 (damage),
 * G9 (game modes). Do not edit outside G5; ask G5 for changes.
 *
 * The bot system only runs under deathmatch-family rules (cvar `deathmatch` != 0); every hook below returns at once
 * otherwise, so callers need no checks of their own. Where a hook must be called (SPEC sections in brackets):
 *
 *   G_RunFrame, once per server frame, before the entity loop     Bot_RunFrame()             [741 2, 5]
 *   SP_worldspawn, deathmatch and sv_globalrankings == 0           Bot_WorldSpawn()           [741 1.1; 760 B1.4]
 *   the waypoint loader entity's think (at + 10 s)                 Bot_WaypointLoaderThink    [741 1.2; AMEND-005 G5.2]
 *   level exit (ExitLevel) and ShutdownGame                        Bot_LevelExit()            [741 1.4]
 *   ClientCommand, before every other command                      Bot_ClientCommand(ent)     [741 editor; 780 dispatch]
 *   ClientPreConnect (deathmatch)                                   Bot_SlotTaken(ent)         [741 2.6; 780 1]
 *   ClientDisconnect (humans)                                       Bot_SlotFreed(ent)         [741 2.6; 780 3]
 *   PutClientInServer: a bot copies Bot_Userinfo(ent) first        Bot_IsBot(ent)             [780 4.2]
 *   spawn-point selection, after the spot is chosen                 Bot_SpawnSpot(ent, spot)   [741 3, 14.5]
 *   the moment a client is killed (obituary)                        Bot_Killed(victim, attacker, mod) [741 12]
 *   damage to a client by an attacker                               Bot_Damaged(targ, attacker) [741 6]
 *   sound import wrapper (#22/#23), non-local sounds                Bot_SoundNoise(ent, volume, lastarg) [741 6; 760 B9.1]
 *   footstep events 3-6                                             Bot_FootstepNoise(ent)     [741 6; 780 11.7]
 *   PlayerNoise                                                     Bot_PlayerNoise(ent, loudness) [741 6]
 *   grenade / C4 / flash pack thrown or placed, and when it goes off Bot_Explosive(ent, kind, add) [741 10; 782]
 *   a client's `say` / `say_team` text (for bot replies)            Bot_HeardChat(speaker, text) [741 12]
 *
 * Sound rule (SPEC-741 6, SPEC-760 B9.1): a sound whose last start argument ("local") is 1 started by a bot is not
 * played at all: the sound wrapper asks Bot_DropSound(ent, local) and skips the engine call when it returns 1.
 *
 * Realistic mode (SPEC-741 11, SPEC-790 5): the "tired" values are maintained outside the bot code; the game-mode
 * code writes them through Bot_State(ent).
 */
#ifndef OM_BOT_API_H
#define OM_BOT_API_H

#include "../g_local.h"

enum { BOT_EXPLOSIVE_GRENADE = 0, BOT_EXPLOSIVE_C4 = 1, BOT_EXPLOSIVE_FLASHPACK = 2 };

/* values other packages maintain for a bot (Realistic mode fatigue, SPEC-741 11 / SPEC-790 5) */
struct BotShared {
	float tired_seen;   /* level time at which the per-bot tired flag was last seen set */
	int fatigue;        /* per-bot counter (walks for 3 s after exceeding 50 while the bot had no enemy) */
	float fatigue_over; /* level time at which the counter last exceeded 50 without an enemy */
	int running;        /* "running" state read by the fatigue rule */
};

/* Game-mode facts the bot objectives need (SPEC-741 15). G9 installs them with Bot_SetModeHooks(); any NULL entry
 * keeps the bot system's fallback (noted per entry). */
struct BotModeHooks {
	int (*client_team)(edict_t *player);           /* 1 blue / 2 red / 0 none; fallback client->resp.team */
	int (*flag_team)(edict_t *flag);               /* team of a CTF flag item (#36); fallback 0 = unknown */
	int (*flag_at_base)(edict_t *flag);            /* fallback: 1 */
	edict_t *(*flag_carrier)(int team);            /* client carrying the flag of `team`; fallback: inventory scan */
	edict_t *(*assassin_target)(edict_t *player);  /* fallback NULL = no restriction */
	int (*assassin_hunts)(edict_t *hunter, edict_t *victim); /* fallback 0 */
	int (*control_owner)(edict_t *controlflag);    /* team owning a control point (#39), 0 none; fallback 0 */
	int (*tired)(edict_t *player);                 /* Realistic "tired" flag; fallback 0 */
};
void Bot_SetModeHooks(const BotModeHooks *hooks);

void Bot_WorldSpawn(void);
/* think of the waypoint loader entity spawned with the bot system's level start: the waypoints are loaded inside an
 * entity think of frame 10.0, after that frame's bot manager ran (SPEC-AMEND-005 G5.2); frees the entity */
void Bot_WaypointLoaderThink(edict_t *self);
void Bot_RunFrame(void);
void Bot_LevelExit(void);
int Bot_ClientCommand(edict_t *ent);
void Bot_SlotTaken(edict_t *ent);
void Bot_SlotFreed(edict_t *ent);
int Bot_IsBot(const edict_t *ent);
const char *Bot_Userinfo(const edict_t *ent);
void Bot_SpawnSpot(edict_t *ent, edict_t *spot);
void Bot_Killed(edict_t *victim, edict_t *attacker, int mod);
void Bot_Damaged(edict_t *targ, edict_t *attacker);
void Bot_SoundNoise(edict_t *ent, float volume, int lastarg);
int Bot_DropSound(edict_t *ent, int lastarg);
void Bot_FootstepNoise(edict_t *ent);
void Bot_PlayerNoise(edict_t *ent, float loudness);
void Bot_Explosive(edict_t *ent, int kind, int add);
void Bot_HeardChat(edict_t *speaker, const char *text);
BotShared *Bot_State(edict_t *ent); /* NULL for a non-bot */

#endif
