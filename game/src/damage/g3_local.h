// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - internal declarations shared by the files of game/src/damage/.
// Not for other packages (use damage_api.h).
// Implements: SPEC-720, SPEC-721, SPEC-722
#pragma once

#include "damage_api.h"

#include <initializer_list>
#include <new>
#include <string.h>
#include <math.h>

namespace ghoul {
class IGhoulInst;
class IGhoulObject;
}
using ghoul::GhoulID;
using ghoul::IGhoulInst;
using ghoul::IGhoulObject;

// ---------------------------------------------------------------------------------------------------------------
// random numbers (game imports #76/#77) and cvars
// ---------------------------------------------------------------------------------------------------------------
float G3_flrand(float lo, float hi);
int G3_irand(int lo, int hi);
float G3_Cvar(const char *name);            // value of a cvar (0 when missing)
bool G3_Deathmatch(void);
int G3_DeathmatchMode(void);                 // deathmatch cvar value 0..7
bool G3_GoreTest(void);                      // ai_goretest != 0
bool G3_CreatePak(void);                     // fs_createpak != 0
bool G3_Cinematic(void);
const char *G3_MapName(void);
float G3_Time(void);                         // level time

// ---------------------------------------------------------------------------------------------------------------
// Ghoul helpers (g3_ghoul.cpp). Names are model node names; lookups are by name through the model object.
// ---------------------------------------------------------------------------------------------------------------
IGhoulObject *G3G_Object(IGhoulInst *inst);
GhoulID G3G_Part(IGhoulInst *inst, const char *name);            // mesh or bolt (null part) id, 0 = none
GhoulID G3G_Material(IGhoulInst *inst, const char *name);
GhoulID G3G_Skin(IGhoulInst *inst, GhoulID material, const char *name);
GhoulID G3G_Sequence(IGhoulInst *inst, const char *modelDir, const char *seq);
bool G3G_PartShown(IGhoulInst *inst, GhoulID part);
void G3G_ShowPart(IGhoulInst *inst, GhoulID part, bool shown);
bool G3G_SetSkinByName(IGhoulInst *inst, const char *material, const char *skin, GhoulID part = 0);
// Registry of models used as bolt-ons (directory Enemy/bolt etc.): object + name
IGhoulObject *G3G_ModelObject(const char *dir, const char *model, const char *skin = NULL);
const char *G3G_ModelName(IGhoulObject *obj);                     // file name of a registered model ("" unknown)
// Attach a new instance of dir/model to `parent` at parent bolt `bolt` by the child's bolt `toBolt` (0 = origin).
IGhoulInst *G3G_AttachNew(IGhoulInst *parent, GhoulID bolt, IGhoulObject *obj, const char *toBolt, float scale,
                          const char *seq = NULL, bool loop = true);
IGhoulInst *G3G_AttachByName(IGhoulInst *parent, const char *bolt, const char *dir, const char *model,
                             const char *toBolt, float scale = 1.0f, const char *skin = NULL);
// Children
int G3G_NumChildren(IGhoulInst *inst);
IGhoulInst *G3G_Child(IGhoulInst *inst, int i, GhoulID *bolt);
IGhoulInst *G3G_ChildOnBolt(IGhoulInst *inst, GhoulID bolt, int nth = 0);
void G3G_RemoveChild(IGhoulInst *parent, IGhoulInst *child);       // detach + destroy (recursively)
void G3G_RemoveChildrenOnBolt(IGhoulInst *parent, GhoulID bolt);
void G3G_RemoveAllChildren(IGhoulInst *parent);
IGhoulInst *G3G_MoveChild(IGhoulInst *from, IGhoulInst *child, IGhoulInst *to, GhoulID bolt, edict_t *newOwner);
IGhoulInst *G3G_CloneFor(IGhoulInst *src, edict_t *ent);       // copy without children for another entity
IGhoulInst *G3G_NewInstFor(IGhoulObject *obj, edict_t *ent);   // new instance on an entity
bool G3G_IsDescendant(IGhoulInst *root, IGhoulInst *inst);      // still attached below root
const char *G3G_ModelDirOfInst(IGhoulInst *inst);               // model directory of the instance's record
// Transforms: entity space <-> world (SPEC-011 entity matrix: rows forward, -right, up, origin; pitch/roll negated)
void G3_EntityAxes(const edict_t *ent, vec3_t fwd, vec3_t left, vec3_t up);
void G3_EntityToWorld(const edict_t *ent, const vec3_t local, vec3_t world);
void G3_WorldToEntity(const edict_t *ent, const vec3_t world, vec3_t local);
void G3_WorldDirToEntity(const edict_t *ent, const vec3_t wdir, vec3_t ldir);
void G3_EntityDirToWorld(const edict_t *ent, const vec3_t ldir, vec3_t wdir);
// Bolt matrix (mode 5 = pose incl. instance transform, through parents) of `inst`, and the bolt's world position
bool G3G_BoltMatrix(IGhoulInst *inst, GhoulID bolt, ghoul::Matrix4 *m, int mode = 5, bool parents = true);
bool G3G_BoltWorld(edict_t *ent, IGhoulInst *inst, GhoulID bolt, vec3_t pos, vec3_t fwd = NULL);
bool G3G_BoltWorldByName(edict_t *ent, const char *bolt, vec3_t pos);

// ---------------------------------------------------------------------------------------------------------------
// Effects (g3_fx.cpp): SPEC-201 messages with the lock rules of SPEC-721 §13 / SPEC-771 C.1
// ---------------------------------------------------------------------------------------------------------------
void G3FX_At(const char *effect, const vec3_t pos, const vec3_t dir = NULL, float scale = 0);  // PHS at pos
void G3FX_OnEnt(const char *effect, edict_t *ent);                                           // follows entity
void G3FX_OnBolt(const char *effect, edict_t *ent, GhoulID bolt, IGhoulInst *inst = NULL);    // on a bolt
// blood effects (lock_blood rules)
void G3FX_StumpBlood(edict_t *ent, GhoulID bolt, IGhoulInst *inst = NULL);
void G3FX_BloodCloud(edict_t *ent, GhoulID bolt, int size, IGhoulInst *inst = NULL);
void G3FX_BloodSplat(const vec3_t point, const vec3_t dir, edict_t *ignore);   // TE 3 behind a hit
void G3FX_BloodPool(const vec3_t pos);
void G3FX_ExitSpray(const vec3_t pos, const vec3_t dir);
void G3FX_Debris(const vec3_t pos, const vec3_t dir, int count, int size, int chunkType, const vec3_t extents,
                 int material);
void G3FX_Sparks(int te, const vec3_t pos, const vec3_t dir);                 // TE 0/10/14
void G3FX_LargeExplosion(int te, const vec3_t pos, int size, int mode);       // TE 44/46/55/56
// effect slots (SPEC-771 C.4)
void G3FX_SlotAdd(edict_t *ent, const char *effect, GhoulID bolt, float scale);
void G3FX_SlotRemove(edict_t *ent, const char *effect, GhoulID bolt);
bool G3FX_SlotRescale(edict_t *ent, const char *effect, GhoulID bolt, float scale);
bool G3FX_SlotHas(edict_t *ent, const char *effect, GhoulID bolt);
// sounds
void G3_Sound(edict_t *ent, int chan, const char *name, float vol, float attn);
void G3_SoundAt(const vec3_t pos, int chan, const char *name, float vol, float attn);
int G3_SoundIndex(const char *name);
int G3_EffectIndex(const char *name);

// ---------------------------------------------------------------------------------------------------------------
// Gore (g3_gore.cpp)
// ---------------------------------------------------------------------------------------------------------------
enum { NO_ZONE = -1 };
enum {
	GZ_HEAD_FRONT, GZ_HEAD_BACK, GZ_NECK, GZ_SHLDR_RIGHT_FRONT, GZ_SHLDR_RIGHT_BACK, GZ_SHLDR_LEFT_FRONT,
	GZ_SHLDR_LEFT_BACK, GZ_CHEST_FRONT, GZ_CHEST_BACK, GZ_ARM_UPPER_RIGHT, GZ_ARM_UPPER_LEFT, GZ_ARM_LOWER_RIGHT,
	GZ_ARM_LOWER_LEFT, GZ_GUT_FRONT, GZ_GUT_BACK, GZ_GROIN, GZ_LEG_UPPER_RIGHT_FRONT, GZ_LEG_UPPER_RIGHT_BACK,
	GZ_LEG_UPPER_LEFT_FRONT, GZ_LEG_UPPER_LEFT_BACK, GZ_LEG_LOWER_RIGHT, GZ_LEG_LOWER_LEFT, GZ_FOOT_RIGHT,
	GZ_FOOT_LEFT, GZ_GUT_FRONT_EXTRA, GZ_GUT_BACK_EXTRA, GZ_NUM_HUMAN
};
extern const char *const g3_humanZoneNames[GZ_NUM_HUMAN];

// Sever-group segments (SPEC-721 zone tables / SPEC-722 death rule 11)
enum G3Segment {
	SEG_END = 0, SEG_TORSO, SEG_NECK, SEG_SHOULDER, SEG_HEAD, SEG_RUARM, SEG_LUARM, SEG_RFARM, SEG_LFARM,
	SEG_RTHIGH, SEG_LTHIGH, SEG_RCALF, SEG_LCALF, SEG_RFOOT, SEG_LFOOT, SEG_GROIN, SEG_ANIMAL
};

#define G3_MAX_ZONES 26
#define G3_MAX_SEVER 48

struct G3SeverItem {
	GhoulID id;
	unsigned char kind;    // 0 mesh, 1 bolt, 2 "+head & both upper arms", 3 "+lower leg zone" (zone in id)
};

struct G3Zone {
	GhoulID woundMesh;     // gz_* mesh, 0 = none
	GhoulID owner;         // body mesh the zone belongs to
	GhoulID goreBolt;
	GhoulID bodyCap, pieceCap;          // cap meshes
	GhoulID bodyCapBolt, pieceCapBolt;  // cap bolts
	const char *capModel;  // cap bolt-on model (Enemy/bolt), NULL = g_cap1
	const char *capModelBolt;
	int segment;           // G3Segment
	int nextZone;          // next zone of piece, -1
	int link;              // wound link, -1
	int damage;            // accumulated damage (negative = armour)
	int level;             // 0..3
	float scale;           // damage scale (SPEC-720)
	int nsever;
	G3SeverItem sever[G3_MAX_SEVER];
	bool headCapNone;      // body-side cap bolt "no_gore_for_me_please"
};

class G3Gore {
public:
	G3Gore(G3Body *body, int numZones);
	G3Body *body;
	int numZones;
	G3Zone zones[G3_MAX_ZONES];
	GhoulID skins[3][4];          // wound skins [level-1][variant]
	GhoulID gzMaterial, cMaterial, cBloodSkin;
	bool gutsOut;
	int killZone;                 // first hit zone of the first killing hit (blood pool, death rule 11), -1
	vec3_t killBoltPos;           // tracked bolt position (world)
	int lastZone;                 // last wounded zone (death choice)
	bool voiceDisabled;
	// rule 1.5: the character's own `g_half_head` record (head skin, `gz_half_head`, face "<prefix>_D")
	IGhoulObject *halfHeadObj;
	char halfHeadH[48], halfHeadF[48];
	void RegisterHalfHead(const char *facePrefix);   // NULL = derived from the face skin shown now
	void RegisterCapModels(void);                    // the zones' cap bolt-on records (find or create)
	// setup (SPEC-721 §1)
	void Setup(void);
	void SetupHuman(int fam);
	void SetupDog(int fam);
	void SetupCow(void);
	// wounding (SPEC-721 §2, §5, §12)
	bool WoundZone(int z, int d, int H, int mod, int dflags, edict_t *attacker, const vec3_t dir);
	void ExitWound(int entry, int exitz, int d, int H, int dflags, const vec3_t hitPos, const vec3_t dir);
	void KillingCloud(int z, int d, int H);          // rule 2.7 cloud (G3.1), after the zone's wound/sever
	void NoteKillingHit(int firstZone, int d, int H); // blood-pool tracking and killing zone (G3.1)
	int LevelOf(int A) const { return A <= 0 ? 0 : (A <= 100 ? 1 : (A <= 200 ? 2 : 3)); }
	void ApplyWoundSkin(int z, int level, int variant);
	int SkinVariant(int z);
	// severing (SPEC-721 §3, §4, §6)
	bool CanSever(int z, int d, int H, int dflags);
	edict_t *Sever(int z, int exitz, bool gibPiece, const vec3_t gibDir, bool keep);
	void HeadExplosion(int z, const vec3_t hitPos);
	// gibbing (SPEC-721 §8)
	void Gib(const G3Hit &hit);
	edict_t *GibChest(const vec3_t dir, bool keep);
	// bolt-ons (SPEC-721 §10, §11)
	int KnockOff(IGhoulInst *boltOn, int damage, const G3Hit &hit, bool anyHitOnly);
	void DropBoltOn(IGhoulInst *boltOn, const G3Hit &hit, bool weapon);
	void StickKnife(int z, const G3Hit &hit);
	void ReleaseKnives(IGhoulInst *from);
	void FuelTankExplode(IGhoulInst *tank, const G3Hit &hit);
	// misc
	bool OwnerShown(int z);
	int ZoneByOwnerPart(GhoulID part, const float *point, bool thrownKnifeDM);
	void TrackKillBolt(void);
	void BloodPool(void);
	void SaveWrite(void);
	void SaveRead(const int *values, int count);
	// armour (SPEC-720 "Character zone armour")
	int armourType;
	void SetArmour(int type, int points);
	void ClearArmour(void);
	int RemainingArmour(void);
};
extern int g3_gibCounter;          // G of SPEC-721 rule 8
extern int g3_gibPieces;           // pieces of the current gib
int Gore_ArmourPoints(G3Body *body);
G3Gore *G3_NewGore(G3Body *b, int zones);
ghoul::IGhoulCallback *G3_NoteCallback(int note);
ghoul::IGhoulCallback *G3_BoltCallback(int which);

// ---------------------------------------------------------------------------------------------------------------
// Bodies (g3_body.cpp, g3_human.cpp, g3_animals.cpp, g3_vehicles.cpp)
// ---------------------------------------------------------------------------------------------------------------
G3Body *G3_NewHumanBody(edict_t *ent, int family);
G3Body *G3_NewDogBody(edict_t *ent, int family);
G3Body *G3_NewCowBody(edict_t *ent);
G3Body *G3_NewHeliBody(edict_t *ent);
G3Body *G3_NewTankBody(edict_t *ent, bool snowcat);
G3Body *G3_NewBaseBody(edict_t *ent, int family);
bool G3_IsHumanFamily(int family);
// weapon identification (SPEC-722 common part): weapon number / attack type of a bolted model name
int Body_WeaponNumber(const char *model);
int Body_AttackTypeOfModel(const char *model);
float Body_RefireDelay(int attackType, int charRef);
void G3_AddEvent(edict_t *ent, int ev, int parm);
void G3_ResetHint(void);
int Dmg_AbsorbArmour(class PlayerInventory *inv, int damage, float piercing, float fraction); // SPEC-003 method 31
void Dmg_EnsureShot(edict_t *ent, const G3Hit &h);  // fill the shot hit list for a traced hit (safety net)                      // hint zone of SPEC-720 10.4 (level start, tests)
// Body image of the savegame (g3_save.cpp): one buffer that writes fields or reads them back in the same order
class G3SaveBuf {
public:
	unsigned char data[512];
	int pos, size;
	bool reading, overflow;
	explicit G3SaveBuf(bool rd, int sz) : pos(0), size(sz), reading(rd), overflow(false) { memset(data, 0, sizeof data); }
	void Raw(void *p, int n);
	void I(int &v, int bytes);          // signed integer of 1, 2 or 4 bytes
	void U(uint32_t &v) { int t = (int)v; I(t, 4); v = (uint32_t)t; }
	void F(float &v);
	void B(bool &v);
	void V(vec3_t v);
	void Str(char *s, int cap);         // length byte + characters
	void Anim(const G3Anim *&a);        // record of the human, dog or cow table (or none)
};

// classname helpers
bool G3_ClassIs(const edict_t *ent, const char *cls);
edict_t *G3_SightClient(void);
void G3_Printf(const char *fmt, ...);        // console print (always shown)
void G3_DPrintf(const char *fmt, ...);       // developer print
const char *G3_vs(const vec3_t v);           // "(x y z)" with two decimals (diagnostics)
