// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game-side Ghoul glue (package G9). PUBLIC INTERFACE for every game package.
//
// The game never shows a model on an entity by talking to the engine Ghoul system directly; it goes through
// this layer (SPEC-791):
//   - one *model record* (GGModel) per distinct (directory, list, skin, base) key, one engine model object each;
//   - an *object instance* (GGInst) per model instance shown on an entity (the entity's ghoulInst field points
//     to the engine instance, the engine instance's user value is the entity);
//   - *bolt-ons* (GGBolt): child model instances hung on a bolt of an object instance or of another bolt-on;
//   - name helpers (skins, parts, sequences, bolts), placement helpers, the aim controller;
//   - *attached-object records* (GGAttached) of map objects with up to six tracked bolt-on parts and the
//     fall-over state of SPEC-770 5.8;
//   - savegame chunks GGOB/GGON/STRG, GGOI/GHLI, GGBI/GHLI, EDOI/EDBN/EDBL and the Ghoul id remap.
//
// Users: G18 (items, player models, effects), G2/G3/G4 (character bodies, gore pieces, vehicles), G7 (props).
// Conventions: names are compared case-insensitively; "time" is level time in seconds (level.time); a GhoulID
// of 0 means none; every function accepts NULL arguments and then does nothing / returns NULL or 0.
// All memory is owned by the glue (malloc/free); GG_FreeAll() releases everything (level spawn / level load).
//
// Hooks the game core must call (see docs/impl/G9.md "Needs from G18"):
//   GG_FreeAll()        whenever all entities are freed (SpawnEntities, ReadLevel) - before entities spawn
//   GG_LevelSpawn()     in SpawnEntities right after GG_FreeAll (registers the 13 gore models, SPEC-791 B5)
//   GG_EntityFreed(e)   from G_FreeEdict (frees the attached-object record and the entity's instances)
//   GG_Shutdown()       from ShutdownGame
//
// Implements: SPEC-791 (all), SPEC-752 5 (Ghoul id remap)
#pragma once

#include "../g_local.h"

typedef ghoul::GhoulID GhoulID;

struct GGModel;
struct GGInst;
struct GGBolt;
struct GGAttached;

//===========================================================================================================
// Model-table entry (SPEC-770 2): 40 bytes in the 32-bit build. Props (G7) may use this type for their tables;
// the helpers below only read dir, file and skinmaterial.
struct gg_entry_t {
	const char *dir;          // 0  model directory under ghoul/, e.g. "objects/generic/bench"
	const char *file;         // 4  model file (also the name of its default sequence)
	int surfacetype;          // 8
	int material;             // 12 debris material
	int health;               // 16
	int solid;                // 20
	const char *skinmaterial; // 24 material whose skin may be replaced (NULL = none)
	int debriscount;          // 28 (low byte used)
	float debrissize;         // 32
	void *parttable;          // 36 filled at spawn by classes with shootable parts
};

//===========================================================================================================
// 1. Model records (SPEC-791 B1-B4)

// Find or create the record of (dir, list, skin, base). Creating registers the base sequence
// ghoul/<dir>/<base>.ghl (base non-empty), the sequence list <list>.gsq through import #65, finishes the
// registration with all skins (allskins) or none, prepares all-skins records at once and computes the
// availability bytes. findonly: never create. skin/base NULL = "".
GGModel *GG_FindModel(const char *dir, const char *list, bool allskins = true, const char *skin = NULL,
                      const char *base = NULL, bool findonly = false);
GGModel *GG_ModelOfObject(ghoul::IGhoulObject *obj);        // record whose engine object this is
GGModel *GG_ModelOfInst(ghoul::IGhoulInst *inst);           // instance -> object (method 34) -> record
GGInst *GG_ObjInstOf(ghoul::IGhoulInst *inst);              // the object instance registered for inst
GGInst *GG_ObjInstOfEntity(edict_t *ent);                   // GG_ObjInstOf(ent->ghoulInst)

ghoul::IGhoulObject *GG_Object(GGModel *m);
const char *GG_ModelDir(GGModel *m);
const char *GG_ModelList(GGModel *m);
bool GG_Prepared(GGModel *m);

// Skins (B2). Register (material, skin) on a record: 0 if the material does not exist; on a prepared record only
// looks the skin up (object method 20); otherwise remembers the pair (savegames) and registers it (method 19).
GhoulID GG_RegisterSkin(GGModel *m, const char *material, const char *skin);
// Only while not prepared: registers the skin on the materials in id order (remembering each pair) and stops at the
// first that provides it, whose id is returned (0 = none did; SPEC-AMEND-006 G9.16).
GhoulID GG_RegisterSkinAnyMaterial(GGModel *m, const char *skin);
// Set a skin by names on an instance (dir/file only used in messages; console warnings as shipped).
void GG_SetSkinInst(ghoul::IGhoulInst *inst, const char *dir, const char *file, const char *material,
                    const char *skin);
// The same on an entity's instance and, with alsoparts, on every tracked part of its attached-object record.
void GG_SetSkin(edict_t *ent, const char *dir, const char *file, const char *material, const char *skin,
                bool alsoparts = false);

// Prepare once (object method 2 with specular = ghl_specular > 0 && ghl_light_method > 0) (B3).
void GG_Prepare(GGModel *m);
// Sequence id of ghoul/<dir>/<name>.ghl; 0 if missing or the record is not prepared yet (developer message).
GhoulID GG_FindSequence(GGModel *m, const char *name);
// Availability cache (B4): true when the model has human animation record i (SPEC-722 table, 0..538).
bool GG_HasAnimation(GGModel *m, int humanAnimIndex);
int GG_HumanAnimCount(void);                       // = g3_numHumanAnims
const char *GG_HumanAnimName(int humanAnimIndex);  // animation record name, NULL outside the table

//===========================================================================================================
// 2. Object instances (B6)

// New engine instance of the record's object for ent (may be NULL): ent->ghoulInst := instance, the instance's
// user value := ent, parts listed in ghoul/<dir>/<list>_poff.gsq are hidden (import #66).
GGInst *GG_NewInst(GGModel *m, edict_t *ent);
// Copy of src's state without children (instance method 36 with 0), no parts-off list (severed body pieces).
GGInst *GG_CloneInst(GGModel *m, edict_t *ent, ghoul::IGhoulInst *src);
// Hand the instance (and all bolt-ons, recursively) to another entity; the old entity's ghoulInst is cleared.
void GG_MoveInst(GGInst *oi, edict_t *newent);
// Remember seq as current and call instance method 17 (nothing when seq is 0).
void GG_Play(GGInst *oi, GhoulID seq, float time, float offset = 0, bool restart = true,
             int mode = ghoul::kPlayLoop, bool matchphase = false, bool reverse = false);
// Destroy every object instance owned by ent (with bolt-ons), then clear ent->ghoulInst.
void GG_RemoveEntity(edict_t *ent);
// Destroy one object instance: its bolt-ons, the owner's ghoulInst field, the engine instance (method 53).
void GG_DestroyInst(GGInst *oi);

ghoul::IGhoulInst *GG_EngineInst(GGInst *oi);
GGModel *GG_InstModel(GGInst *oi);
edict_t *GG_InstOwner(GGInst *oi);
GhoulID GG_InstSequence(GGInst *oi);

//===========================================================================================================
// 3. Bolt-ons (B7)

// Hang a new instance of child on parent's bolt parentbolt, aligned with the child's own bolt childbolt.
// scale outside [0.99999, 1.00001] scales the child's transform; skin: set on the child's material named like
// the child's list name, else on the first material with that skin. NULL when any id/record is missing.
GGBolt *GG_AddBolt(GGInst *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt,
                   const char *skin = NULL, float scale = 1.0f);
GGBolt *GG_AddBoltToBolt(GGBolt *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt,
                         const char *skin = NULL, float scale = 1.0f);
// Checked variants (character bodies): print "Unable to add bolt on!" (developer) when child, parentbolt,
// childbolt (or seq for the playing variant) is 0; the playing variant loops seq from now.
GGBolt *GG_AddBoltChecked(GGInst *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt,
                          const char *skin = NULL, float scale = 1.0f);
GGBolt *GG_AddBoltPlaying(GGInst *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt, GhoulID seq,
                          const char *skin = NULL, float scale = 1.0f);
void GG_PlayBolt(GGBolt *b, GhoulID seq, float time, float offset = 0, bool restart = true,
                 int mode = ghoul::kPlayLoop, bool matchphase = false, bool reverse = false);
GGBolt *GG_FindBoltOn(GGInst *oi, GhoulID parentbolt);              // first direct bolt-on on that bolt
GGBolt *GG_FindBoltByInst(GGInst *oi, ghoul::IGhoulInst *inst);     // direct bolt-on with that instance
void GG_RemoveBolt(GGInst *oi, GGBolt *b);                          // one direct bolt-on
void GG_RemoveBoltsOn(GGInst *oi, GhoulID parentbolt);              // all direct bolt-ons on that bolt
void GG_RemoveAllBolts(GGInst *oi);                                 // all direct bolt-ons
void GG_PauseAllBolts(GGInst *oi);                                  // method 19 on direct bolt-ons, now
edict_t *GG_BoltOwner(GGBolt *b);                                   // entity owning the whole tree
GGInst *GG_BoltRoot(GGBolt *b);                                     // the owning object instance
ghoul::IGhoulInst *GG_BoltEngineInst(GGBolt *b);
GGModel *GG_BoltModel(GGBolt *b);
GhoulID GG_BoltSequence(GGBolt *b);
GhoulID GG_BoltParentBolt(GGBolt *b);
GhoulID GG_BoltChildBolt(GGBolt *b);
float GG_BoltScale(GGBolt *b);
// Iterate the direct bolt-ons of an object instance / the children of a bolt-on (NULL ends).
GGBolt *GG_FirstBolt(GGInst *oi);
GGBolt *GG_FirstChildBolt(GGBolt *b);
GGBolt *GG_NextBolt(GGBolt *b);

//===========================================================================================================
// 4. Attached-object records (map objects, SPEC-791 Interface 4; fall state of SPEC-770 5.8)

#define GG_MAX_TRACKED_PARTS 6

struct GGAttached {
	edict_t *owner;
	edict_t *link;            // chain of props assembled from several entities
	unsigned shownmask;       // bit i-1 = tracked part i shown
	int partcount;            // counted parts 0..6
	int listsize;             // entries in parts[] (a seventh part is listed but not counted)
	int listcap;
	GGBolt **parts;           // tracked parts 1..listsize (index 0 = part 1); NULL = empty slot
	// fall-over state (SPEC-770 5.8, EDOI bytes 32..95)
	edict_t *fallsubject;     // never set by the stock props (SPEC-791 open question)
	int falldir;              // 0 none, 1 forward, 2 backward, 3 left, 4 right
	float fallstart;
	float pivotheight;
	float radius;
	float angleofs;
	vec3_t startangles;
	vec3_t pivot;
	vec3_t falldirection;
	void (*restorethink)(edict_t *self);
};

GGAttached *GG_AttachedCreate(edict_t *ent);   // creates (or returns the existing) record
GGAttached *GG_Attached(edict_t *ent);         // NULL when none
void GG_AttachedFree(edict_t *ent);
void GG_EntityFreed(edict_t *ent);             // hook for G_FreeEdict: record + instances of ent

//===========================================================================================================
// 5. Simple model on an entity (B8): the standard way map objects, items and effects get a model.
// Nothing when the entity already has an instance or renderfx bit 0x400000. With skin and material both
// non-empty the record is created for that skin. Sets renderfx := 0x400000 and loops the default sequence.
bool GG_SetSimpleModel(edict_t *ent, const char *dir, const char *file, const char *skin = NULL,
                       const char *material = NULL);

//===========================================================================================================
// 6. Name helpers (B9)
void GG_HidePart(edict_t *ent, const char *part);   // developer message when the part is missing
void GG_ShowPart(edict_t *ent, const char *part);
void GG_HidePartInst(ghoul::IGhoulInst *inst, const char *part);
void GG_ShowPartInst(ghoul::IGhoulInst *inst, const char *part);
void GG_ShowOnlyPart(ghoul::IGhoulInst *inst, const char *part);
void GG_SetAllParts(ghoul::IGhoulInst *inst, bool shown);
void GG_HideAllRecursive(ghoul::IGhoulInst *inst);
// Bolt a model-table entry onto an instance's object instance (static pose). NULL on failure (messages).
GGBolt *GG_BoltEntry(ghoul::IGhoulInst *inst, const gg_entry_t *parent, const char *parentbolt,
                     const gg_entry_t *child, const char *childbolt, const char *skin = NULL);
// The same on ent->ghoulInst, tracked on ent's attached-object record (count/shown bit while below 6).
GGBolt *GG_AddTrackedPart(edict_t *ent, const gg_entry_t *parent, const char *parentbolt,
                          const gg_entry_t *child, const char *childbolt, const char *skin = NULL);
// Bolt onto a bolt-on; tracked on the attached-object record of the tree's owner.
GGBolt *GG_BoltEntryOnBolt(GGBolt *parent, const gg_entry_t *parententry, const char *parentbolt,
                           const gg_entry_t *child, const char *childbolt, const char *skin = NULL);
GGBolt *GG_FindBoltNamed(edict_t *ent, const gg_entry_t *entry, const char *bolt);
ghoul::IGhoulInst *GG_TrackedPart(edict_t *ent, int i);       // 1-based; NULL outside / empty slot
GGBolt *GG_TrackedPartBolt(edict_t *ent, int i);
void GG_PlayByName(edict_t *ent, const char *name, bool loop); // loop: mode 0, else mode 1
// code 0 once-and-hold, 1 loop, 2 static pose, other: nothing. Returns the sequence id (0 = not found).
GhoulID GG_PlayByNameInst(ghoul::IGhoulInst *inst, const char *name, int code);
void GG_ScaleModel(edict_t *ent, float factor);
void GG_ScaleInst(ghoul::IGhoulInst *inst, float factor);

//===========================================================================================================
// 7. Placement helpers (B10)
void GG_PlaceRelative(edict_t *source, edict_t *target, float forward, float right, float up);
bool GG_InFrontCone(edict_t *ent, const vec3_t point, float halfangledeg); // prints "Threshold %f  Dot %f"

//===========================================================================================================
// 8. Aim controller (B11). Ranges and steps in radians; +-4000 = unlimited.
struct GGAim {
	edict_t *ent;
	ghoul::IGhoulInst *inst;       // the instance to turn (a bolt-on, or the entity's own instance)
	ghoul::IGhoulInst *parent;     // its parent instance (bolted parts)
	GhoulID bolt;                  // the bolt it hangs on (part's own bolt)
	GhoulID parentbolt;            // the parent's bolt
	vec3_t target;
	float pitchmin, pitchmax, yawmin, yawmax;
	float maxstep;
	bool yawon, pitchon, throughparents;
	float yaw, pitch;              // resulting angles kept for the caller (bolted parts)
};
void GG_AimInit(GGAim *a);          // everything 0 except throughparents = 1
int GG_AimEntity(GGAim *a);         // 1 free, 2 step-limited, 3 range-limited / nothing to do
int GG_AimPart(GGAim *a);

//===========================================================================================================
// 9. Level lifecycle (B5)
void GG_FreeAll(void);      // destroy all object instances, records and the engine objects (system method 6)
void GG_LevelSpawn(void);   // the 13 Enemy/bolt gore records
void GG_Shutdown(void);

//===========================================================================================================
// 10. Savegames (B13; chunk order SPEC-700 "Savegame chunks of the AI", SPEC-752 1.2)
// Kinds of the AI tables: 0 models, 1 bolt-ons, 2 object instances (table order of SPEC-700 part 1).
enum { GG_KIND_MODEL = 0, GG_KIND_BOLT = 1, GG_KIND_INST = 2 };
// Give every glue object without an id the next value of the shared AI id counter (*counter, post-increment).
void GG_AssignIds(uint32_t *counter);
uint32_t GG_MaxId(void);                       // largest id seen (load: counter := max + 1)
void GG_WriteTable(int kind);                  // AICT + AICC {id, 0} in ascending id order
void GG_ReadTable(int kind);                   // creates empty objects for the ids read
void GG_WriteData(int kind);                   // part 2: GGOB [+GGON+STRG] / GGOI+GHLI / GGBI+GHLI
void GG_ReadData(int kind);
void GG_WriteAttached(void);                   // OINM + per record EDOI, EDBN [+ EDBL]
void GG_ReadAttached(void);
void GG_RemapEntityIds(void);                  // SPEC-752 5, after the whole level section is read
// Note-callback table handed to the instance serializer (instance methods 37/38). Packages that attach note
// callbacks register them once at startup, always in the same order (the table is part of the save format).
void GG_RegisterCallback(ghoul::IGhoulCallback *cb);
// Function index encoding of the restore think (EDOI byte 92): the game core supplies the encoder/decoder.
void GG_SetFunctionCodec(int (*encode)(void (*fn)(edict_t *)), void (*(*decode)(int))(edict_t *));

//===========================================================================================================
// 11. Test entity `ghoul_model` (B14) - spawn function for the spawn table (SPEC-006)
void SP_ghoul_model(edict_t *ent);

//===========================================================================================================
// 12. Statistics for tests (number of records, object instances, bolt-ons, attached records)
void GG_Stats(int *models, int *insts, int *bolts, int *attached);
