// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: character roster, helicopters and vehicles, special characters.
//
// Public interface of game/src/roster/ (owner: G4). Other packages read this header; they do not edit it.
//
//   Spawning (G18)          Roster_CallSpawn(ent) is called by ED_CallSpawn (one /* G4 */ hook): for every m_*
//                           classname of SPEC-006 it runs the per-classname set-up of SPEC-730 / 731 / 732 and
//                           returns true. Roster_LevelStart() is called once per level spawn after the Ghoul
//                           glue was reset (one /* G4 */ hook in SpawnEntities).
//   AI framework (G2)       Characters are built on ai_api.h: om_ai::CharacterSetup, CreateAI, the type set-up
//                           below, InitProfile, first_think = the start-up set (StartCharacter + AddDecisionSet).
//                           Helicopters (type 8), tank and snowcat (9, 10) and severed limbs (13) are OmAI
//                           subclasses registered with om_ai::RegisterAIFactory; they override Think and Damage.
//                           Roster_TypeSetup() is the "type's own set-up" of SPEC-700 rule 2.2 for an entity that
//                           already has its AI object (spawners, savegames).
//   Bodies (G3)             Bodies are created with Body_Create during the type set-up; the SPEC-722 requests go to
//                           the body. G3's Dmg_Damage hands hits on helicopters/vehicles to their AI object's Damage
//                           override (SPEC-731 Part A §12.1, Part B §13). g3hooks.limbAI is set to Roster_SetupLimb's
//                           one-argument form at level start.
//   Scripts (G6b host)      Roster_HeliCommand() / Roster_VehicleCommand() implement om_script::Host::heli_command /
//                           vehicle_command (SPEC-750 §12.8, SPEC-AMEND-003 §6): they return the action identifier,
//                           -1 when the command creates no action, -2 when the entity has no such AI.
//                           Roster_VehicleActionDone() implements Host::vehicle_action_done.
//   Savegames (G2)          Roster_WriteVehicleChunks() / Roster_ReadVehicleChunks(): AIHA (+ HEWP after AIAI) and
//                           AITA (SPEC-731 Part A §6, Part B §4).
//
// Implements: SPEC-730, SPEC-731, SPEC-732
#pragma once

struct edict_s;
typedef struct edict_s edict_t;
typedef float vec3_t[3];

// --- spawning -------------------------------------------------------------------------------------------------
bool Roster_IsClassname(const char *classname);
bool Roster_CallSpawn(edict_t *ent);
void Roster_LevelStart(void);

// --- per-type data --------------------------------------------------------------------------------------------
enum {
	ROSTER_FAMILY_NONE = 0,
	ROSTER_FAMILY_ECTO = 1,      // civilian/officer body (SPEC-722 family 4)
	ROSTER_FAMILY_FEMALE = 2,    // female body (family 8)
	ROSTER_FAMILY_MESO = 3,      // male soldier body (family 3)
	ROSTER_FAMILY_ARMOURED = 4,  // armoured male soldier (family 13)
	ROSTER_FAMILY_DOG = 5,
	ROSTER_FAMILY_BULL = 6,
	ROSTER_FAMILY_HELI = 7,
	ROSTER_FAMILY_VEHICLE = 8,
	ROSTER_FAMILY_LIMB = 9,
	ROSTER_FAMILY_CORPSE = 10
};

// SPEC-710 decision sets given at spawn (SPEC-730 table 3) = om_ai::DecisionSet values
enum {
	ROSTER_SET_A = 0,   // bystander: 2, 6
	ROSTER_SET_B = 1,   // fighter: 2, 3, 4, 6, 5
	ROSTER_SET_C = 2,   // fighter with rank 3
	ROSTER_SET_F = 3,   // animal: 2
	ROSTER_SET_G = 4,   // Dekker: 11 (10), 12 (8), 13 (5), 14 (2), 5; rank 3; omniscience sense
	ROSTER_SET_H = 5,   // dead on arrival: nothing
	ROSTER_SET_VEHICLE = 6, // idle decision (kind 1)
	ROSTER_SET_NONE = 7
};

struct RosterTypeInfo {
	int type;            // SPEC-700 type code
	int family;          // ROSTER_FAMILY_*
	int startset;        // ROSTER_SET_*
	float sizescale;     // 1.0, 1.1 (skin boss, Dekker), bull 1.5; dogs random unless spawnflag 8
	const char *dir;     // model directory, e.g. "enemy/meso"
	const char *set;     // sequence-set name (the first of two for the three random-set classnames)
};
// false for codes without a roster set-up
bool Roster_TypeInfo(int type, RosterTypeInfo *out);
int Roster_TypeCodeForClassname(const char *classname); // 0 when not a roster classname
// SPEC-700 rule 2.2 type set-up for an entity whose AI object (ent->ai) of `type` was just created
bool Roster_TypeSetup(edict_t *ent, int type);

// --- special characters created by other packages ---------------------------------------------------------------
// severed piece `piece` (its Ghoul instance already set) cut from `source` (character or player; may be NULL)
void Roster_SetupLimb(edict_t *piece, edict_t *source);
// body-queue corpse `corpse` copied from the dying player `player` (after G3's Body_CopyToCorpse)
void Roster_SetupPlayerCorpse(edict_t *corpse, edict_t *player);

// --- scripts (SPEC-750 §12.8) -----------------------------------------------------------------------------------
bool Roster_HasHeliAI(edict_t *ent);
bool Roster_HasVehicleAI(edict_t *ent);
int Roster_HeliCommand(edict_t *ent, int command, edict_t *target, const vec3_t vec, float scalar);
int Roster_VehicleCommand(edict_t *ent, int command, edict_t *target, const vec3_t vec, float scalar);
bool Roster_VehicleActionDone(edict_t *ent, int action_id);

// --- savegames --------------------------------------------------------------------------------------------------
typedef void (*roster_chunk_write_t)(void *ctx, const char *id, const void *data, int size);
typedef bool (*roster_chunk_read_t)(void *ctx, const char *id, void *data, int size);
// part 0 = chunks before AIAI (AIHA, AITA), 1 = after the component lists (HEWP per waypoint)
void Roster_WriteVehicleChunks(edict_t *ent, int part, roster_chunk_write_t w, void *ctx);
bool Roster_ReadVehicleChunks(edict_t *ent, int part, roster_chunk_read_t r, void *ctx);
