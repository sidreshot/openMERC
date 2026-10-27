// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (package G9). PUBLIC INTERFACE for the game core (G18) and other packages.
//
// One game mode per level, chosen from cvar `deathmatch` at every level start (SPEC-790 1):
//   0 single player, 1 deathmatch (Team DM with dmflags 0x80), 2 Assassin, 3 Arsenal, 4 CTF, 5 Realistic,
//   6 Control, 7 Conquer ("CTB").
// Every mode-dependent decision goes through this interface: effective deathmatch flags, teams, scoring,
// spawn kits, death drops, end-of-level rules and map rotation, scoreboards, status layouts, the game log.
//
// Where the game core calls the hooks (SPEC-780 positions; see docs/impl/G9.md "Needs from G18"):
//   SpawnEntities:        GM_LevelInit() before any entity spawns; GM_LevelSetup() after all have spawned
//   G_RunFrame:           GM_RunFrame() after all entities ran (replaces CheckDMRules)
//   ExitLevel:            GM_LevelExit()
//   EndDMLevel:           GM_EndDMLevel() (map rotation with the mode's "same map" flag)
//   export 0x50:          GM_GameTypeName()
//   ClientUserinfoChanged GM_UserinfoChanged(ent, userinfo, spawnvariant) before the full player-info configstring
//   ClientBegin (DM):     GM_ClientBegin(ent) at step 3
//   ClientDisconnect:     GM_ClientDisconnect(ent) (first hook), GM_ClientDisconnected(ent) (second hook)
//   PutClientInServer:    GM_ClientSpawn(ent) at step 7 in deathmatch, after the new inventory object (kit, layout)
//   player_die:           GM_Obituary, GM_DeathDrop, GM_Score in that order; frag sounds only if
//                         GM_FragSounds() (not in Assassin)
//   spectator change:     GM_DropFlag(ent) before the score reset
//   ClientEndServerFrame: GM_ClientEndFrame(ent) at step 13 (before name colours are sent; this hook sends
//                         the icon tables itself, svc 33)
//   damage to a client:   GM_ClientDamaged(targ, attacker) (CTF: records hurting a flag carrier)
//   score / dying human / intermission: GM_Scoreboard(ent, killer); help: GM_Help(ent);
//   32-frame refresh:     GM_Scoreboard while the scoreboard is shown, GM_StatusLayout while help is shown
//   ClientThink move scale: GM_MoveScale(ent); weapon spread: GM_SpreadAddon(...)
//   item code:            GM_ItemFilter(...), GM_WeaponsStay(), GM_WeightLimit(), GM_AutoSwitch(), ...
//
// Implements: SPEC-790 (all), SPEC-720 2 / SPEC-780 / SPEC-782 corrections listed in SPEC-790
#pragma once

#include "../g_local.h"

enum {
	GM_SP = 0,
	GM_DM = 1,
	GM_ASSASSIN = 2,
	GM_ARSENAL = 3,
	GM_CTF = 4,
	GM_REALISTIC = 5,
	GM_CONTROL = 6,
	GM_CONQUER = 7
};

// dmflags bits (SPEC-790 3)
#define DF_NO_HEALTH 0x0001
#define DF_NO_ITEMS 0x0002
#define DF_WEAPONS_STAY 0x0004
#define DF_NO_FALLING 0x0008
#define DF_REALISTIC_DAMAGE 0x0010
#define DF_SAME_LEVEL 0x0020
#define DF_NO_RELOAD 0x0040
#define DF_TEAMPLAY 0x0080
#define DF_NO_FRIENDLY_FIRE 0x0100
#define DF_FORCE_RESPAWN 0x0200
#define DF_NO_ARMOR 0x0400
#define DF_INFINITE_AMMO 0x0800
#define DF_SPINNING_PICKUPS 0x1000
#define DF_BULLET_ONLY 0x2000
#define DF_FORCE_JOIN 0x4000
#define DF_NUMBERED_TEAMS 0x8000

// configstrings written by the modes (SPEC-790 2)
#define CS_GM_CONQUER_DMFLAGS 10
#define CS_GM_ASSASSIN 32
#define CS_GM_TEAMGAME 33
#define CS_GM_ASSASSIN2 34

// string-package ids: (package id << 8) | entry (SPEC-313; package ids from the stock .sp files)
#define SP_DM_GENERIC 0x00
#define SP_DM_ASSASSIN 0x08
#define SP_DM_ARSENAL 0x09
#define SP_DM_CTF 0x0A
#define SP_DM_CONTROL 0x0D
#define SPID(pkg, entry) (((pkg) << 8) | (entry))

//-----------------------------------------------------------------------------------------------------------
// Mode and flags
int GM_Mode(void);                 // current mode 0..7, -1 before the first level start
const char *GM_GameTypeName(void); // SPEC-001 export 0x50; "Unknown" before a mode exists
int GM_DMFlags(void);              // dmflags as the current mode sees them (forced on/off bits applied)
bool GM_Flag(int bit);             // GM_DMFlags() & bit
bool GM_Deathmatch(void);          // modes 1..7
bool GM_TeamGame(void);            // CTF, Control always; DM, Realistic, Conquer with the mode's 0x80
bool GM_TeamPlay(void);            // the mode's 0x80
bool GM_UsesTeamSpawns(void);      // = GM_TeamGame(): info_player_team1/2 (SPEC-780 9 as corrected)
bool GM_AssignsTeams(void);        // CTF, Control, Conquer
bool GM_ForceJoin(void);           // the mode's 0x4000 (CTF, Control, Conquer)
// Same-team rule (SPEC-790 4) for two client entities without character AI.
bool GM_SameTeam(edict_t *a, edict_t *b);
const char *GM_TeamName(edict_t *client); // "blue" for team 1, "red" for 2, else userinfo teamname
int GM_Team(edict_t *client);             // team number 0 none, 1 Blue, 2 Red
// SPEC-780 1: kill the player as a suicide when his model team changes in game (team play, not CTF/Control,
// Conquer only without numbered teams)
bool GM_ModelTeamChangeKills(void);

//-----------------------------------------------------------------------------------------------------------
// Level
void GM_LevelInit(void);   // B1.1: before the map's entities spawn
void GM_LevelSetup(void);  // B1.3: after all entities have spawned
void GM_RunFrame(void);    // B2: mode frame rule, then the end rules (deathmatch, no intermission)
void GM_LevelExit(void);   // B2: level-exit rule
void GM_EndDMLevel(void);  // B2: next map (same-map flag, sv_maplist, nextmap, target_changelevel), intermission
bool GM_TouchAllowed(void);  // flags and conquer zones accept touches (touch delay of B1)

//-----------------------------------------------------------------------------------------------------------
// Clients
void GM_UserinfoChanged(edict_t *ent, char *userinfo, bool spawnvariant = false); // team numbers (CTF/Control/Conquer)
void GM_ClientBegin(edict_t *ent);                     // entering a deathmatch game (icons reset, Assassin)
void GM_ClientSpawn(edict_t *ent);                     // every deathmatch spawn: inventory, kit, layout
void GM_ClientDisconnect(edict_t *ent);                // first disconnect hook
void GM_ClientDisconnected(edict_t *ent);              // second disconnect hook
void GM_ClientEndFrame(edict_t *ent);                  // end-of-frame rule; sends icon tables (svc 33)
void GM_ClientDamaged(edict_t *targ, edict_t *attacker);
void GM_DropFlag(edict_t *ent);                        // CTF: drop a carried flag (spectator change)

// Death
void GM_Obituary(edict_t *self, edict_t *inflictor, edict_t *attacker);
void GM_DeathDrop(edict_t *self);
void GM_Score(edict_t *self, edict_t *inflictor, edict_t *attacker);
bool GM_FragSounds(void);       // the common frag sounds of SPEC-780 B7 (not in Assassin)

// Score helpers (score = stats[10] = resp.score)
int GM_GetScore(edict_t *ent);
void GM_SetScore(edict_t *ent, int score);
void GM_AddScore(edict_t *ent, int delta);

//-----------------------------------------------------------------------------------------------------------
// Rules consulted by player, item, weapon and damage code
float GM_MoveScale(edict_t *ent);   // SPEC-790 B3/B5/B11 (0..1, before trunc(x*255))
// SPEC-782 B1 aim spread add-on in degrees; airbornelike = the player's body reports that state
float GM_SpreadAddon(edict_t *ent, int weapon, bool airbornelike);
int GM_WeightLimit(void);           // 0 = none (SP: encumbrance value, Realistic 4)
bool GM_AutoSwitch(void);           // automatic switch to a better picked-up weapon (not Realistic)
bool GM_WeaponsStay(void);          // effective 0x4
bool GM_NoDrops(void);              // Arsenal: players drop nothing
float GM_DroppedLifetime(void);     // 20 s in deathmatch, 0 = unlimited (single player)
// Item filter of SPEC-781 1 per mode. Returns 0 keep, 1 remove, or a replacement item index (> 1 is never a
// valid "remove"; item #38 in Realistic becomes #34 -> returns 34). kind/std as the item table.
int GM_ItemFilter(int itemindex, int kind, int standard);
int GM_MedkitLimit(void);           // carry limit of item #34 in deathmatch (2; 1 with 0x10; 1 in Realistic)
// Damage predicates (SPEC-790 B3)
bool GM_ZoneArmourCovered(int zone);
bool GM_ZoneIsLimb(int zone);
bool GM_BoltOnKnockable(const char *name);

// Realistic remembered item positions (SPEC-790 B1.2, used by the weapon return of SPEC-781 7)
void GM_RememberItem(int itemindex, const vec3_t origin);
int GM_RememberedItems(int itemindex, vec3_t *out, int max);

//-----------------------------------------------------------------------------------------------------------
// Layouts
void GM_Scoreboard(edict_t *ent, edict_t *killer);  // the mode's scoreboard (writes the game log once)
bool GM_StatusLayout(edict_t *ent);                // Assassin/Arsenal status; false when the mode has none
void GM_Help(edict_t *ent);                        // `help`: status layout for 30 s, or hide it
void GM_SetStats(edict_t *ent);                    // single-player meter (also done by GM_ClientEndFrame)

//-----------------------------------------------------------------------------------------------------------
// CTF / Control. The flag rules of SPEC-781 9/10 are implemented here: G18's item code spawns the flag entities
// and leaves their touches to G9 (GM_LevelSetup takes them over). G18 hooks:
//   - player-module item drop callback with item type 7 (CTF flag): GM_DropFlag(ent) instead of a plain pickup
//     (a flag item dropped through DropEquipItem is also adopted by GM_RunFrame on the next frame);
//   - SPEC-003 server callback 12 "owner carries the CTF flag": GM_CarriedFlag(ent) != 0;
//   - SPEC-780 16 carried-flag bolt-on on the player model: GM_CarriedFlag(ent) is the flag's team.
int GM_CarriedFlag(edict_t *ent);                  // team of the flag carried (0 none)
void GM_SetCarriedFlag(edict_t *ent, int team);
int GM_CTFCaptures(int team);
int GM_CTFFlagTeam(edict_t *e);                    // team of a flag entity (ctf_flag_blue/red, ctf_dropped_flag)
edict_t *GM_CTFBaseFlag(int team);                 // the base flag entity of a team (NULL if the map has none)
bool GM_CTFFlagAtBase(int team);                   // that base flag is at its base (visible, touchable)
int GM_ControlOwner(edict_t *flag);                // team owning a control point (#39), 0 none
edict_t *GM_AssassinTarget(edict_t *player);       // the player's current Assassin target (NULL none)
bool GM_Exhausted(edict_t *player);                // Realistic: exhausted by running (pm_flags 0x40)
// G9 installs G5's Bot_SetModeHooks (bot objectives) and G3's g3hooks entries at every level start.
void GM_ControlRegisterPoint(edict_t *flag);       // control flag spawned (count of points)
void GM_ControlSwitch(edict_t *flag, int oldteam, int newteam); // point changes owner
int GM_ControlScore(int team);

// Spawn function for the spawn table (`misc_ctf_base`), and the Conquer zone registration called by G18's
// SP_trigger_conquer after InitTrigger (moves the origin to the box centre, installs the zone touch)
void SP_misc_ctf_base(edict_t *ent);
void GM_ConquerZone(edict_t *ent);

// Single-player cheat `defaultweapons` (SPEC-790 B11)
void GM_DefaultWeapons(edict_t *ent);

//-----------------------------------------------------------------------------------------------------------
// Conquer zones for bots (SPEC-790 B10: a position counts as inside a zone when it lies within the zone's box)
bool GM_InConquerZone(const vec3_t p);

//-----------------------------------------------------------------------------------------------------------
// Services the modes need from the game core. Declared extern "C" with a default implementation in
// modes/gm_shim.cpp bound by /alternatename, so the game core can define the real ones without a link conflict.
extern "C" {
float G_StealthValue(void);          // SPEC-761 stealth value 0..1 (single-player meter, SPEC-790 B11)
void G_SetPlayerInfo(edict_t *ent);  // rewrite configstring 1415 + slot = name\team\model\N (SPEC-780 5)
}
