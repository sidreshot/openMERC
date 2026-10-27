// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — map objects ("props", SPEC-770): public interface.
//
// Used by:
//  - G18's spawn dispatch (g_spawn.cpp): G7_CallSpawn() spawns every prop and server-effect classname of SPEC-770 /
//    SPEC-771 and returns true; G7_LevelSpawned() runs at "ambient sound init" of the level spawn (SPEC-760 B1.3
//    step 5).
//  - G4 (vehicle characters): the model tables 10-12 use the same 40-byte entry format (SPEC-770 §2); Entry() and the
//    standard setup / break helpers below can be called for any entity.
//  - G6b (savegames): PropData::seqref is the "sequence reference" table * 65536 + index (SPEC-752 §2.1); the
//    attached-object record of SPEC-791 (tracked parts, shown bits, link chain) is G9's GGAttached.
// Per-entity prop state lives in a table indexed by entity number (PropData), so edict_t is not changed; the setup
// clears it whenever a new prop is made in a record.
//
// Implements: SPEC-770 §2-§5 (shared framework), SPEC-770 Appendix A (model tables)
#pragma once

#include "../g_local.h"
#include "prop_ghoul.h"

// G18 hooks (declared here; g_spawn.cpp declares them locally at the call)
bool G7_CallSpawn(edict_t *ent);
void G7_LevelSpawned(void);

namespace props {

// ---------------------------------------------------------------------------------------------------------------
// §2 model tables

struct ModelEntry {
	const char *dir;      // under ghoul/
	const char *file;     // ghoul/<dir>/<file>.ghb, default sequence ghoul/<dir>/<file>.ghl
	int surf;             // default surface type
	int mat;              // default debris material
	int health;
	int solid;
	const char *skinmat;  // material whose skin may be replaced (NULL = none)
	int debris_n;         // only the low byte is used
	float debris_s;       // truncated
};

enum {
	T_BOSNIA = 0, T_CASTLE = 1, T_GENERIC = 2, T_IRAQ = 3, T_ITEMS = 4, T_LIGHTS = 5, T_NEWYORK = 6,
	T_SIBERIA = 7, T_TOKYO = 8, T_UGANDA = 9, T_HELI = 10, T_TANK = 11, T_SNOWCAT = 12
};
const ModelEntry *Entry(int table, int index); // NULL for tables not in this spec (4, 10-12)
gg::ModelRef Ref(const ModelEntry *e);

// ---------------------------------------------------------------------------------------------------------------
// per-entity state

struct PropData;
typedef void (*prop_pain_t)(edict_t *self, edict_t *other, float kick, int damage, const vec3_t point);
// reaction of a shootable part (§5.7): part index 1..6, the attacker and the hit point
typedef void (*part_react_t)(edict_t *self, int part, edict_t *attacker, const vec3_t point);

struct PartRow {
	const char *bolt;     // bolt of the main model the part hangs on
	int index;            // tracked part index 1..6
	part_react_t react;   // NULL = default (5-chunk debris + grey puff); PART_NONE = no reaction
};
void PartReactNone(edict_t *self, int part, edict_t *attacker, const vec3_t point);

struct PropData {
	const ModelEntry *entry;
	int table, index;
	int seqref;               // table * 65536 + index (SPEC-752)
	const PartRow *parts;     // part table (§5.7), NULL = none
	int nparts;
	vec3_t start_angles;
	prop_pain_t pain;         // pain behaviour with the hit point
	// fall over (§5.8)
	int fall_dir;
	float fall_turned, fall_speed;
	vec3_t fall_pivot, fall_axis;
	void (*fall_think)(edict_t *);
	void (*fall_use)(edict_t *, edict_t *, edict_t *);
	// class scratch (meaning per class)
	float t0, t1, t2;
	int i0, i1, i2, i3;
	float f0, f1, f2, f3;
	vec3_t v0, v1;
	edict_t *e0;
	edict_t *list[10];        // fruit list / linked chain
	int nlist;
	void (*next)(edict_t *);  // state of multi-step behaviours
	gg::IGhoulInst *inst0, *inst1;
	int flags;                // 1 = has an attached-object record (set by the setup)
	vec3_t box_mins, box_maxs; // class box before the right-angle rotation
	void (*push_saved_think)(edict_t *);
	float push_saved_next;
};
PropData &D(edict_t *ent);
void ClearData(edict_t *ent);

// ---------------------------------------------------------------------------------------------------------------
// §3 standard setup

// spawnflag forcing helpers ("forces +n", "clears n", "toggles n")
inline void Force(edict_t *e, int bits) { e->spawnflags |= bits; }
inline void ClearFlags(edict_t *e, int bits) { e->spawnflags &= ~bits; }
inline void Toggle(edict_t *e, int bits) { e->spawnflags ^= bits; }

// sets box, then: model (table entry, skin, only part), defaults, box rotation, clip mask, scale, damage/physics
// flags, solid-4 bounds, colour tint, link, mass 800
void Setup(edict_t *ent, int table, int index, const char *skin = NULL, const char *onlypart = NULL);
// the same steps 4-10 for classes that build their model themselves (no mass change)
void SetupNoTable(edict_t *ent, int surf, int mat, int health, int solid);
// skin applied to the entry's skin material after a setup made without a skin: the model record then keeps all
// skins of its directory (black-box: in game these classes show every skin of the directory)
void SkinAfterSetup(edict_t *ent, const char *skin);
void SetBox(edict_t *ent, float x0, float y0, float z0, float x1, float y1, float z1);
void RotateBox(edict_t *ent); // §3 step 5 (right-angle box rotation)
void ApplyDefaults(edict_t *ent, const ModelEntry *e);

// ---------------------------------------------------------------------------------------------------------------
// §4 breaking

void StandardBreak(edict_t *self, edict_t *attacker, int n = 0, int s = 0);
void TableBreak(edict_t *self, edict_t *attacker);
void Die_StandardBreak(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);
void Die_TableBreak(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point);

// ---------------------------------------------------------------------------------------------------------------
// §5 shared reactions

void Touch_PlayerPush(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
void KnockRates(edict_t *self, edict_t *attacker, vec3_t d); // sets pitch/roll rates, returns d
void KnockOver(edict_t *self, edict_t *attacker, int damage, float k);
void Think_Tumble(edict_t *self);
void ThrowAway(edict_t *self, edict_t *attacker, int damage);
void ThrowAwaySpin(edict_t *self, edict_t *attacker, int damage, int axis);
void Explode(edict_t *self, bool ignite, bool flash);
void Ignite(edict_t *self, float radius);
gg::IGhoulInst *Bolt(edict_t *ent, int table, int parent, const char *parentbolt, int child, const char *childbolt,
                     const char *skin = NULL);
void RemovePart(edict_t *self, int i, bool debris);
void PartDebris(edict_t *self, const vec3_t at); // 5-chunk debris + grey puff at a position
bool PartPos(edict_t *self, int i, vec3_t out);  // world position of tracked part i's bolt
// pain of a prop with a part table: hidden part + reaction, then linked objects thrown off; returns the part hit
int ShootableParts(edict_t *self, edict_t *attacker, const vec3_t point);
void ThrowOffLinked(edict_t *self);
void LinkObject(edict_t *self, edict_t *other); // table chains (§6B.6)
void FallOverStart(edict_t *self, edict_t *player);
void Riders(edict_t *self);

// pain behaviour with the hit point (SPEC-720 §5.3); NULL clears it
void SetPain(edict_t *ent, prop_pain_t fn);

// ---------------------------------------------------------------------------------------------------------------
// small helpers

float frand(float a, float b);       // engine flrand
int irand(int a, int b);             // engine irand
int Sound(const char *name);          // precache, returns index
void PlaySound(edict_t *ent, int chan, const char *name, float vol, float att);
void LoopSound(edict_t *ent, const char *name, int data = 0xF8);
void StopLoop(edict_t *ent);
void MarkSoundList(int k);            // "precaches sound list k" (SPEC-401 client sound list)
void SetMaterialFlag(int position);   // sets one character of configstring 16
void Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t dir, const vec3_t point,
            const vec3_t origin, int damage, int knockback, int dflags, int mod);
void RadiusDamage(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod,
                  int dflags = 0);
bool IsPlayer(const edict_t *e);
bool IsCharacter(const edict_t *e);
void Forward(edict_t *e, vec3_t f, vec3_t r = NULL, vec3_t u = NULL);

// ---------------------------------------------------------------------------------------------------------------
// spawn registration

struct SpawnEntry {
	const char *classname;
	void (*spawn)(edict_t *ent);
};
struct SpawnTable {
	SpawnTable(const SpawnEntry *t);
	const SpawnEntry *entries;
	SpawnTable *next;
};
void LevelInit(void);   // first spawn of a level: reset caches
void RegisterNoteCallbacks(void); // savegame callback table order (G9), once
void ResetAllData(void);
void FlushSoundList(void);
void ResetSoundList(void);
void LevelFinish(void); // ambient sound init time: flush the sound list

} // namespace props
