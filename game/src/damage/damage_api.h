// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - package G3: damage model, gore and dismemberment, character bodies.
//
// PUBLIC INTERFACE of game/src/damage/ for the other game packages (G18 world/player, G2 AI, G4 roster,
// G7 props, G9 modes). Owner: G3. Other packages read this header; they do not edit it.
//
// Overview
//   1. Damage (SPEC-720): Dmg_Damage is THE damage function (12 values). G18's T_Damage / T_RadiusDamage /
//      CanDamage forward here (one hook call each). Area damage: Dmg_RadiusLinear / Dmg_RadiusMinMax / Dmg_RadiusCone.
//   2. Shot hit list (SPEC-720 "Shot hit list"): weapons trace through Dmg_ShotTrace (or install Dmg_ShotFilter as the
//      SPEC-001 #53 filter after Dmg_ShotBegin); a traced hit then passes DMGF_TRACED / DMGF_PROJECTILE to Dmg_Damage
//      and the body resolves the hit zone from that list.
//   3. Bodies (SPEC-722): every character (enemy, civilian, animal, vehicle) and every player owns a G3Body attached to
//      its entity (edict_t::g3body). G4 creates it (Body_Create) during the type set-up, then issues the set-up
//      requests in SPEC-722 order (SetRightHand, SetLeftHand, accessories, skins/parts, SetFace, SetVoice, Precache,
//      SetArmour). G2's AI object implements G3BodyOwner (callbacks/queries the body needs from the AI) and calls
//      the choice functions (ChooseMove/Stand/Attack/Jump/Dodge/Posture/Reload/Cock/Pain/Death), SetAnim, Think.
//   4. Gore (SPEC-721) runs inside G3Body::Damage; nothing to call from outside except Gore_DekkerSplit and the
//      violence locks (Gore_ReadLocks at game init).
//   5. Cross-package hooks (g3hooks): things G3 needs from packages that may not exist yet. Every hook has a safe
//      default; the owning package assigns its function at init.
//
// Character damage path (SPEC-720 Behaviour 9 == SPEC-700 §10): Dmg_Damage hands a hit on a character (entity with
// a body whose owner is set) to G3BodyOwner::Damage. The default implementation (Dmg_CharacterDamage, in G3) does
// all of SPEC-720 Behaviour 9 and calls back into the owner for the AI parts (OnBetrayed, OnHitAlert, Die, Pain).
// G2 may override G3BodyOwner::Damage completely; it must then call G3Body::Damage for the final damage.
//
// Implements: SPEC-720, SPEC-721, SPEC-722 (interface)
#pragma once

#include "../g_local.h"

class G3Body;
class G3BodyOwner;

// ---------------------------------------------------------------------------------------------------------------
// Damage flags (SPEC-720 table "Damage flags"; names as used in the specs)
// ---------------------------------------------------------------------------------------------------------------
enum : int {
	DMGF_AREA = 0x1,              // every area-damage variant
	DMGF_NO_ARMOR = 0x2,          // player inventory armour not used
	DMGF_NO_KNOCKBACK = 0x8,
	DMGF_NO_PROTECTION = 0x20,    // ignores god mode, spawn protection, blocked team damage
	DMGF_TEAM_BLOCKED = 0x40,     // set by Dmg_Damage when team damage is blocked
	DMGF_PUSH_ONLY = 0x80,        // area damage: 5 % damage, full push
	DMGF_TRACED = 0x100,          // bullet/knife/pellet/bite: zone from the shot hit list
	DMGF_PROJECTILE = 0x200,      // traced projectile; exit-wound wall spray
	DMGF_0x400 = 0x400,           // mines/spikes; no effect in the damage model
	DMGF_MICROWAVE = 0x800,       // MPG burst
	DMGF_FIRE = 0x1000,
	DMGF_FALL = 0x2000,
	DMGF_DROWN = 0x4000,
	DMGF_EXPLOSIVE = 0x8000,      // no zone armour/multipliers, gib below -50, severing
	DMGF_DANCE = 0x10000,         // automatic-weapon "dance" death (SPEC-722 death table)
	DMGF_SPREAD = 0x20000,        // spread over neighbouring zones; guts and severing allowed
	DMGF_TORSO_BLAST = 0x100000,  // zones from the torso by facing; no stump blood
	DMGF_MICROWAVE_RAY = 0x200000,// MPG primary; nerve death variant
	DMGF_SLASH = 0x400000,        // melee slash: back-stab, slash zone rules
	DMGF_0x800000 = 0x800000,     // no damage to armour configuration 3
	DMGF_THROWN = 0x1000000,      // thrown blade
	DMGF_0x2000000 = 0x2000000    // character melee/emitter attacks; no effect in the damage model
};

// Means-of-death bit added for unblocked team damage (SPEC-720 §2); the obituary ignores it.
#define DMG_MOD_FRIENDLY 0x8000000

// edict_t::g3flags
enum : int {
	G3F_SUBPART = 0x1,        // damage passes to edict_t::g3damageowner (SPEC-720 1.2); set by Dmg_SetDamageOwner
	G3F_NO_AREA = 0x2,        // not hit by area damage (SPEC-720 14.1)
	G3F_THROWN_KNIFE = 0x4,   // thrown-knife projectile (the original's projectile marker 254; SPEC-721 §10)
	G3F_PIECE = 0x8,          // severed body piece / gib piece
	G3F_KNIFE_PICKUP = 0x10   // knife pickup made from a stuck knife
};
// Set by Dmg_Damage when a thrown knife was deflected by armour (SPEC-720 11.3/11.4: "a thrown knife drops"); the
// thrown-knife code (G18, SPEC-782 §6) then leaves the knife lying 40 units in front of the target.
extern bool dmg_knifeDeflected;

// One hit, the 12 values of the damage call (SPEC-720 "The damage call").
struct G3Hit {
	edict_t *targ;
	edict_t *inflictor;
	edict_t *attacker;      // may be NULL
	vec3_t dir;             // normalised by Dmg_Damage
	vec3_t point;           // world impact point
	vec3_t origin;          // shot start (traced) / explosion centre (area)
	int damage;
	int knockback;
	int dflags;             // DMGF_*
	int mod;                // means of death (MOD_* of g_local.h), may carry DMG_MOD_FRIENDLY
	float armorPiercing;    // value 11: non-zero = armour used up at twice the rate
	float armorBypass;      // value 12: fraction 0..1 ignoring character zone armour
};

// ---------------------------------------------------------------------------------------------------------------
// 1. Damage (SPEC-720)
// ---------------------------------------------------------------------------------------------------------------
void Dmg_Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t dir, const vec3_t point,
                const vec3_t origin, int damage, int knockback, int dflags, int mod, float armorPiercing = 0.0f,
                float armorBypass = 0.0f);
void Dmg_DamageHit(G3Hit &hit);                       // same, from a filled record
// Behaviour 14: area damage. The gib counter of SPEC-721 rule 8 is reset at the start of each call.
// Linear: centre = inflictor origin, p = D (1 - d/R).
void Dmg_RadiusLinear(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod,
                      int dflags);
// Min/max: p = m + (M - m)(1 - d/R) (C4, claymores, grenades, rockets).
void Dmg_RadiusMinMax(edict_t *inflictor, edict_t *attacker, const vec3_t center, float maxdmg, float mindmg,
                      float radius, edict_t *ignore, int mod, int dflags);
// Cone (steam generators): only entities within the cone (cos >= mincos) of conedir.
void Dmg_RadiusCone(edict_t *inflictor, edict_t *attacker, float damage, float radius, edict_t *ignore, int mod,
                    const vec3_t conedir, float mincos);
bool Dmg_CanDamage(edict_t *targ, edict_t *inflictor);   // Behaviour 15
// Behaviour 9 (default character rules); returns 1 when the character was hurt. Called by G3BodyOwner::Damage.
int Dmg_CharacterDamage(G3Hit &hit);
// Last means of death (read by the obituary, SPEC-780).
extern int dmg_lastMod;
// Single-player toughness factor t of sk_toughness (SPEC-720 table).
float Dmg_ToughnessFactor(void);
int Dmg_Toughness(void);                  // sk_toughness clamped 0..4
// Effective dmflags of the current mode (SPEC-790 §3 table; g3hooks.dmflags overrides).
int Dmg_DMFlags(void);
bool Dmg_TeamPlay(void);                  // dmflags 0x80 (always in CTF/Control)
bool Dmg_RealisticDamage(void);           // dmflags 0x10
// "Entity is a sub-part of another": damage to `part` is passed to `owner` (SPEC-720 1.2).
void Dmg_SetDamageOwner(edict_t *part, edict_t *owner);

// ---------------------------------------------------------------------------------------------------------------
// 2. Shot hit list (SPEC-720)
// ---------------------------------------------------------------------------------------------------------------
#define DMG_SHOT_MAX_HITS 20
struct G3ShotHits {
	edict_t *ent;                                  // entity the records belong to (NULL = none)
	int count;
	ghoul::HitRecord rec[DMG_SHOT_MAX_HITS];       // sorted by distance (SPEC-011 method 43)
	vec3_t start;                                  // shot start in that entity's space
	vec3_t dir;                                    // unit direction in that entity's space
	vec3_t worldStart, worldDir;                   // the same in world space
};
extern G3ShotHits dmg_shot;
void Dmg_ShotClear(void);
// Set the ray for the filter (world space); then call gix.tracefilter(..., Dmg_ShotFilter).
void Dmg_ShotBegin(const vec3_t start, const vec3_t end);
int Dmg_ShotFilter(trace_t *tr);                  // SPEC-001 #53 filter: passes between limbs, snaps to the first record
// Convenience: Dmg_ShotBegin + engine trace with the filter.
trace_t Dmg_ShotTrace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, edict_t *pass,
                      int mask);
// Fill the hit list for `ent` along a world ray (used by the player path and thrown knives). Returns the count.
int Dmg_ShotFill(edict_t *ent, const vec3_t start, const vec3_t dir, float length);

// ---------------------------------------------------------------------------------------------------------------
// 3. Bodies (SPEC-722)
// ---------------------------------------------------------------------------------------------------------------
// Body families (SPEC-722 common part "Body families"; numbers are those of the original).
enum G3BodyFamily {
	BODY_HUMAN = 1, BODY_DOG = 2, BODY_MESO = 3, BODY_ECTO = 4, BODY_HUSKY = 5, BODY_ROTTWEILER = 6,
	BODY_ORGANIC = 7, BODY_FEMALE = 8, BODY_HELI = 9, BODY_TANK = 10, BODY_SNOWCAT = 11, BODY_COW = 12,
	BODY_MESO_ARMOURED = 13
};

// Animation record (SPEC-722 "Animation records"): gameplay attributes of one animation sequence.
struct G3Anim {
	const char *name;       // sequence `ghoul/<model dir>/<name>.ghl`
	int cls;                // 0 stationary, 1 locomotion, 2 attack, 3 jump, 4 fall, 5 hit reaction, 6 death
	float k;                // speed scale (exact effect not yet established by tests)
	float yawLeft;          // max yaw change left per think (deg)
	float yawRight;         // max yaw change right per think (deg, negative)
	uint32_t flags;         // ANIMF_*
	int posture;            // POSTURE_*
	float startOffset;      // seconds
	int group;              // AI behaviour group
};
enum : uint32_t {
	ANIMF_MARK = 0x00000800 /* set on a few rows, no effect */, ANIMF_AIM = 0x00001000, ANIMF_RHAND_OPEN = 0x00002000, ANIMF_LHAND_OPEN = 0x00004000,
	ANIMF_BACKWARDS = 0x00008000, ANIMF_STRAFE_LEFT = 0x00010000, ANIMF_STRAFE_RIGHT = 0x00020000,
	ANIMF_LEAN = 0x00040000, ANIMF_FINISH_CYCLE = 0x00400000, ANIMF_REHITTABLE = 0x00800000,
	ANIMF_MATCH_PHASE = 0x01000000, ANIMF_KEEP_START = 0x02000000, ANIMF_REVERSED = 0x04000000,
	ANIMF_LOOP = 0x08000000, ANIMF_GRENADE = 0x10000000, ANIMF_LEAK = 0x20000000
};
enum { ANIMCLS_STATIONARY = 0, ANIMCLS_MOVE = 1, ANIMCLS_ATTACK = 2, ANIMCLS_JUMP = 3, ANIMCLS_FALL = 4,
	ANIMCLS_HIT = 5, ANIMCLS_DEATH = 6 };
enum { POSTURE_STAND = 0, POSTURE_CROUCH = 1, POSTURE_PRONE = 2, POSTURE_SIT = 4, POSTURE_FACEDOWN = 5,
	POSTURE_ONBACK = 6, POSTURE_ONSIDE = 7 };
// Tables (g3_humananims.inc, g3_animalanims.inc); records are identified by pointer (table position).
extern const G3Anim g3_humanAnims[];
extern const int g3_numHumanAnims;
extern const G3Anim g3_dogAnims[];
extern const int g3_numDogAnims;
extern const G3Anim g3_cowAnims[];
extern const int g3_numCowAnims;
const G3Anim *Anim_Find(const G3Anim *table, int count, const char *name);  // first record with that name
const G3Anim *Anim_Human(const char *name);
int Anim_HumanIndex(const G3Anim *a);  // 0..g3_numHumanAnims-1 or -1

// Body event-flag word bits read by AI actions each think (SPEC-711 A4).
enum : uint32_t {
	BODYEV_LSTEP = 0x01, BODYEV_FIRE = 0x02, BODYEV_JUMP = 0x40, BODYEV_OOZE = 0x80, BODYEV_THUD = 0x100,
	BODYEV_DOGLEAP = 0x200, BODYEV_THROW = 0x400, BODYEV_EOS = 0x800, BODYEV_INAIR = 0x1000, BODYEV_RSTEP = 0x2000
};

// Expression states (SPEC-722 "Expression state" / "Face expressions").
enum { EXPR_NEUTRAL = 1, EXPR_AFRAID = 2, EXPR_ALT = 3, EXPR_PAIN = 4, EXPR_DEAD = 5, EXPR_TALK = 6,
	EXPR_TALK_ALT = 7, EXPR_TALK_AFRAID = 8 };

// Limb injury bits (SPEC-720 rule 11.9 / SPEC-722 "Limb injury").
enum { LIMB_RLEG = 1, LIMB_LLEG = 2, LIMB_RARM = 4, LIMB_LARM = 8 };

// What the body needs from the AI (implemented by G2's AI object; every method has a neutral default).
class G3BodyOwner {
public:
	virtual ~G3BodyOwner() {}
	// --- damage (SPEC-700 §10 / SPEC-720 B9). Default: Dmg_CharacterDamage. Return 1 when hurt.
	virtual int Damage(G3Hit &hit) { return Dmg_CharacterDamage(hit); }
	// Parts of Behaviour 9 the AI owns (called by Dmg_CharacterDamage):
	virtual void OnBetrayed(edict_t * /*attacker*/) {}                // player hit a teammate character
	virtual void OnHitAlert(edict_t * /*attacker*/) {}                // register attacker position (SPEC-701 hearing kind 2)
	virtual void OnHit(void) {}                                     // "the AI object records that it was hit"
	virtual bool IsBuddy(void) { return false; }                    // BUDDY_HIT (UNVERIFIED which)
	virtual bool IsHostage(void) { return false; }                  // protected hostage (HOSTAGE_HIT)
	// Death (SPEC-700 §10 Death) and pain; defaults play the body's choice directly on the model.
	virtual void Die(edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, int dflags);
	virtual void Pain(edict_t *attacker, const vec3_t point, int knockback, int damage);
	// --- queries used by the body's choices
	virtual edict_t *Target(void) { return NULL; }                  // current target / enemy
	virtual bool TargetHostileArmed(void) { return Target() != NULL; }
	virtual float FlashBlindness(void) { return 0; }                // >= 20: blinded animations
	virtual bool JustNoticedEnemy(void) { return false; }           // startle (reaction override 3)
	virtual void OnStartled(void) {}
	virtual void Velocity(vec3_t out) { VectorClear(out); }         // AI velocity estimate
	virtual float MoveValue(void) { return 0; }                     // footstep test "> 9"
	virtual float ReferenceHeight(void) { return 0; }               // floor height for footsteps
	virtual bool NoDisarm(void) { return false; }
	virtual bool ScriptedSilence(void) { return false; }
	virtual bool HasKiller(void) { return false; }
	virtual float TurnFactor(void) { return 1.0f; }
	virtual bool IsDesignated(void) { return false; }               // shared AI state "designated" character
	virtual bool FiresBlind(void) { return false; }
	virtual bool FeetFollowFloor(void) { return true; }             // ground-offset event condition
	virtual float RefireFactor(void) { return 1.0f; }               // F of the refire rules
	// --- notifications from the body
	virtual void OnSequenceEnd(void) {}                             // EOS/BOS (finished flag already set)
	virtual void OnGroundOffset(float /*z*/) {}                     // quake_ground bolt height
	virtual void OnLook(const vec3_t /*angles*/, float /*eyeHeight*/) {} // sbolt_mouth / GBOLT_HEAD
	virtual void OnHeel(int /*side*/, float /*height*/) {}          // 0 right, 1 left
	virtual void OnDeathAnimDone(void) {}                           // once, when the death animation completed
	virtual void OnDropWeaponNote(void) {}                          // note `dropweapon`
	virtual void OnDogBite(edict_t * /*victim*/) {}
	// --- added 2026-10-25 (SPEC-AMEND-005 G3.12): combat profile flag 1 (SPEC-700 rule 19, profile[7] bit 1)
	virtual bool CombatProfileFlag1(void) { return false; }
};

// Input of the movement/standing/attack choices (filled by the AI action; SPEC-722 "Movement and standing choice").
struct G3MoveInput {
	const G3Anim *forced;       // a sequence explicitly requested (wins if available)
	int posture;                // requested posture, -1 = keep
	vec3_t moveDir;             // world movement direction (unit or zero)
	float speed;                // movement speed (units/s)
	bool running;               // run instead of walk
	bool aiming;                // aims at the target while moving/standing
	bool strafing;              // strafe while aiming
	edict_t *target;            // NULL = none
	vec3_t aimPoint;            // point aimed at (for up/down aim)
	float turn;                 // remaining turn (deg, +left) for turn-in-place
	bool highJump;              // jump choice: high jump
	bool meleeRange;            // attack choice: target within melee range
};

// Body. Created by Body_Create; owned by the entity (ent->g3body); freed with Body_Destroy / when the entity is freed.
class G3Body {
public:
	explicit G3Body(edict_t *ent, int family);
	virtual ~G3Body();

	int family;
	edict_t *ent;
	G3BodyOwner *owner;          // the AI object (NULL for players and pieces)
	const char *modelDir;        // e.g. "enemy/meso"

	// --- common state (SPEC-722 "Body state visible to other parts of the game")
	const G3Anim *cur;
	bool finished;
	int pauseKind;
	int expr; float exprExpire; bool exprLock;
	uint32_t eventFlags;
	bool pendingRestart;
	float playStart;
	float deathTime, firstDeathTime;
	vec3_t deathPoint;
	float lastHitTime;
	int lastHitFlags;

	// playback (common part "Playback")
	bool HasAnim(const G3Anim *a);
	bool SetAnim(const G3Anim *a, bool force);           // set (no restart) / force (restart)
	bool AnimDone(void);
	void Pause(int kind, float time);
	void Resume(void);
	uint32_t TakeEventFlags(void) { uint32_t f = eventFlags; eventFlags = 0; return f; }
	// expression state
	void SetExpression(int state, float duration, bool lock = false);
	// turning and root motion
	void YawLimits(float fallback, float faceDist, bool targetIsClient, float *left, float *right);
	void MoveDelta(float stepFactor, vec3_t out);        // world-space displacement of this think
	vec3_t rootVel; float rootYawRate;

	// per think: events, voices, faces, weapon charge effects, corpse freeze (G2 calls it every think)
	virtual void Think(void);

	// choices; NULL = none
	virtual const G3Anim *ChooseMove(const G3MoveInput &in) { return NULL; }
	virtual const G3Anim *ChooseStand(const G3MoveInput &in) { return NULL; }
	virtual const G3Anim *ChooseAttack(const G3MoveInput &in) { return NULL; }
	virtual const G3Anim *ChooseJump(const G3MoveInput &in) { return NULL; }
	virtual const G3Anim *ChooseDodge(int dir /*0 left 1 right 2 back 3 fwd 4 dive*/) { return NULL; }
	virtual const G3Anim *ChoosePosture(int posture, float speed) { return NULL; }
	virtual const G3Anim *ChooseReload(const G3MoveInput &in) { return NULL; }
	virtual const G3Anim *ChooseCock(void) { return NULL; }
	virtual const G3Anim *ChoosePain(edict_t *attacker, const vec3_t point, int knockback, int damage) { return NULL; }
	virtual const G3Anim *ChooseDeath(edict_t *attacker, edict_t *inflictor, int damage, int dflags,
	                                  const G3Anim *forced) { return NULL; }
	virtual const G3Anim *ChooseIdle(void) { return NULL; }

	// damage: final damage of a hit (SPEC-720 Behaviour 10-12; gore SPEC-721). Called with all 12 values.
	virtual int Damage(G3Hit &hit) { return hit.damage; }
	virtual int ProtectionClass(void) { return 0; }      // armour configuration (3 = pain suppressed on 0 damage)
	virtual bool Ignored(void) { return false; }          // "ignored" state (SPEC-700 §11)

	// set-up requests (SPEC-722 "Character set-up"); defaults do nothing for non-human bodies
	virtual bool SetRightHand(int attackType) { return false; }
	virtual void SetLeftHand(int attackType) {}
	virtual void SetFace(const char *prefix) {}
	// muzzle effect on the flash bolt of the right-hand weapon (SPEC-782 2); false = no such bolt
	virtual bool MuzzleEffect(const char *effect) { return false; }
	virtual void SetVoice(const char *name, int count, int deathType, const char *customDir) {}
	virtual void Precache(void) {}
	virtual void SetArmour(int type) {}                  // armour configuration = "protection class" of SPEC-700
	                                                     // (spawnflag 32768: SetArmour(0) after the set-up)
	virtual void StripArmour(void) {}
	virtual int ArmourPercent(void) { return 0; }
	// bolt-ons and skins (common part "Bolt-ons and skins")
	ghoul::IGhoulInst *AddBoltOn(const char *dir, const char *model, const char *skin, const char *bolt,
	                             const char *toBolt, float scale = 1.0f);
	ghoul::IGhoulInst *AddRandomBoltOn(float p, const char *dir, const char *model, const char *skin,
	                                   const char *bolt, const char *toBolt, float scale = 1.0f);
	bool SetSkin(const char *material, const char *skin);
	bool ShowPart(const char *part, bool shown);

	// reactions the AI forwards (SPEC-722 voices/greetings)
	virtual void OnUsed(edict_t *user) {}
	virtual void OnTouched(edict_t *toucher) {}
	virtual void Wake(float volume) {}                     // wake voice
	virtual void TacticalVoice(const char *event, int variants) {}
	// weapon handling (human)
	virtual int RightHandType(void) { return 0; }
	virtual int LeftHandType(void) { return 0; }
	virtual bool ReadyToFire(void) { return false; }
	virtual bool Fire(edict_t *target, const vec3_t targetPoint, bool paced, int targetBolt) { return false; }
	virtual bool CanThrowNow(void) { return false; }
	virtual bool Throw(edict_t *target) { return false; }
	virtual void DropWeapons(void) {}
	virtual int LimbInjury(void) { return 0; }             // LIMB_* bits currently active
	// corpse (SPEC-722 "Corpses")
	bool readyToRemove;
	// savegame (SPEC-722 "Savegames", SPEC-721 ZGIA/AIGZ, SPEC-700 "Savegame chunks of the AI"): the body image
	// chunk and the gore chunk are written by Body_WriteData below; families add their fields here
	uint32_t saveId;                                      // id from the shared AI counter (0 = none yet)
	virtual void SaveFields(class G3SaveBuf &buf) {}      // family part of the image (writes, or reads back when
	                                                      // buf.reading; OpenMerc's own layout, g3_save.cpp)

	// --- internal (G3)
	ghoul::IGhoulInst *Inst(void) { return ent ? ent->ghoulInst : NULL; }
	virtual void OnNote(int note, const char *arg);       // model events
	virtual void OnBolt(int which, const ghoul::Matrix4 *m);
	float RootYawRate(const ghoul::Matrix4 *m);           // yaw rate (rad/s) of a `quake_origin` rate matrix
	virtual void RegisterEvents(void);
	bool gibbed;                 // fully gibbed
	class G3Gore *gore;          // gore zones (organic bodies)
	bool muted;                  // no further voice from this body
	bool voiceEnabled;
	ghoul::IGhoulInst *gutsInst; // guts bolt-on (plays g_exit_guts_anim2 after anim1)
	float frozenAt;              // corpse freeze time (0 = not frozen)
	float gutsStart;
	float fadeEnd;               // corpse fade-out end (0 = not fading)
	const G3Anim *painPrev;      // deathmatch player: record to resume after the pain reaction
	float painUntil;             //   ... non-zero while a player hit reaction plays (until its EOS)
	virtual void PrePlay(const G3Anim *a, bool changed) {}   // human additions before playback
	virtual void OnHeel(int side, float height) {}
	virtual void GeneralVoice(const char *event, int variants, float volume = 0.8f) {}
	virtual bool AllowsDrops(void) { return true; }
	virtual bool NoDrop(void) { return false; }
	virtual int HandBolt(ghoul::GhoulID bolt) { return -1; }     // 0 right hand, 1 left hand, -1 none
	virtual void ClearHand(int hand) {}                          // removes the hand's bolt-on and its type
	virtual void ForgetHand(int hand) {}                         // (internal) the hand's type only
	virtual void DropHandWeapon(int hand) {}
	ghoul::IGhoulInst *knives[8];
	int numKnives;
	bool IsStuckKnife(ghoul::IGhoulInst *k);
	void AddStuckKnife(ghoul::IGhoulInst *k);
	void ReleaseStuckKnives(ghoul::IGhoulInst *only);   // NULL = all
	void *operator new(size_t size);
	void operator delete(void *p);
	G3Body *listPrev, *listNext;   // list of all live bodies (Body_ReleaseAll)
};

G3Body *Body_Create(edict_t *ent, int family);   // unknown id: "ERROR: invalid body class code: <n>", meso body
void Body_Destroy(edict_t *ent);
inline G3Body *Body_Of(edict_t *ent) { return ent ? ent->g3body : NULL; }
// Player bodies (deathmatch): female when the player model's base model is `fem_play`.
G3Body *Body_CreateForPlayer(edict_t *player, const char *baseModel);
// Corpses (SPEC-722 "Corpses"): call Body_CorpseFrameBegin once per server frame, then Body_CorpseCheck for each dead
// character whose death action runs; returns true when the corpse should be removed now.
// Savegames (SPEC-700 "Savegame chunks of the AI", kind "bodies"; SPEC-722 "Savegames"; SPEC-721 savegame data).
// Called by the AI savegame code in the order of SPEC-700 (part 1 table 7, part 2 kind 7). Chunk ids and sizes
// are those of the original (`AIBM`/`AIBE`/`AIBF` 492, `AIBK` 500, `AIBH`/`AIBR` 144, `AIBC` 148, `HELI` 440,
// `TANK` 172, `SCAT` 192; organic bodies then `AIGZ` = int32 per zone); the image layout is OpenMerc's own
// (the original's is private and not portable). On load a body is re-created on the entity stored in its image,
// its zones are rebuilt from the model (the Ghoul instance and its callbacks come from the glue's chunks, read
// before), and the AI re-links its owner: `Body_ById(id)->owner = ai`.
void Body_AssignIds(uint32_t *counter);   // every body without an id gets (*counter)++
uint32_t Body_MaxId(void);                // largest id seen (written or read)
void Body_WriteTable(void);               // AICT + AICC {id, family} in ascending id order
void Body_ReadTable(void);                // remembers the ids/families read for Body_ReadData
void Body_WriteData(void);                // per body in ascending id order: image chunk [+ AIGZ]
void Body_ReadData(void);                 // per remembered body: image chunk (re-creates the body) [+ AIGZ]
G3Body *Body_ById(uint32_t id);
uint32_t Body_SaveChunkId(int family, int *size);  // image chunk id (and size) of a family, 0 = none
// Lifetime: bodies (and their gore records) are module-heap objects, independent of the engine's tagged memory.
// Body_Destroy(ent) releases one; Body_ReleaseAll() releases every live body (level spawn "reset bodies" step via
// Gore_LevelStart, ShutdownGame, savegame load). Pointers other packages keep (AI objects) are invalid afterwards.
void Body_ReleaseAll(void);
int Body_Count(void);                     // live bodies (tests, leak checks)
void Body_CorpseFrameBegin(void);
bool Body_CorpseCheck(edict_t *corpse, bool isArmourTarget);
// Player body queue copy (SPEC-722 "Corpses"): gives the queued corpse a body of the dead player's family.
void Body_CopyToCorpse(edict_t *player, edict_t *corpse);
// Scripted requests on human bodies (SPEC-720 11.1, 12): next hit kills (variant 2 adds SPREAD); the armoured boss
// fires its killtarget on its first hit; the tumble flag selects the tumbling death (SPEC-722 death rule 2).
void Body_ScriptedKill(edict_t *ent, int variant);
void Body_SetFirstHitTrigger(edict_t *ent);
void Body_SetTumble(edict_t *ent, bool on);
// Helicopter body (SPEC-722 "Helicopter body", SPEC-720 §12; used by G4's helicopter AI)
void Body_HeliSmoke(edict_t *ent, float healthFraction);         // -1 removes the smoke
void Body_HeliCanopyBlood(edict_t *ent, int variant);            // 1 front, 2 back, 3 both, other: none
void Body_HeliBarrel(edict_t *ent, ghoul::IGhoulInst *barrel, bool firing);
int Body_HeliCrewDead(edict_t *ent);                              // bit 1 gunner, bit 2 pilot
void Body_HeliAllowCrewKill(edict_t *ent, bool allow);
// Tank and snowcat bodies (SPEC-722, SPEC-731 Part B §2)
void Body_VehicleMoving(edict_t *ent, bool on);
void Body_VehicleAlternate(edict_t *ent, bool alternate);         // snowcat spawnflag 1 look
bool Body_VehicleDisabled(edict_t *ent);
void Body_VehicleSetDisabled(edict_t *ent, bool disabled);
int Body_VehicleMGHealth(edict_t *ent);
void Body_VehicleAimParams(edict_t *ent, float out[5]);           // turn rate, speed, turret, gun pitch, MG rates

// ---------------------------------------------------------------------------------------------------------------
// 4. Gore (SPEC-721)
// ---------------------------------------------------------------------------------------------------------------
struct G3Locks { bool blood, gorezones, sever, deaths, textures; };
extern G3Locks g3locks;
void Gore_ReadLocks(void);                         // at game init (SPEC-721 §13)
void Gore_LevelStart(void);                        // registers the gore bolt-on models (SPEC-791 §5)
void Gore_DekkerSplit(edict_t *target);            // func_Dekker_split (SPEC-721 §9)
void Gore_ResetGibCounter(void);                   // start of each radius-damage event

// ---------------------------------------------------------------------------------------------------------------
// 5. Cross-package hooks (assign at game init; NULL = G3's built-in default)
// ---------------------------------------------------------------------------------------------------------------
struct G3Hooks {
	int (*dmflags)(void);                                   // G9: effective dmflags (default: SPEC-790 §3 table)
	bool (*sameTeam)(edict_t *a, edict_t *b);               // G9: team rule (default OnSameTeam)
	void (*musicDanger)(edict_t *player);                   // G7: music "danger" state 3
	void (*musicIntensity)(float delta);                    // G7: general voices raise intensity 0.05
	void (*dmPlayerHit)(edict_t *targ, edict_t *attacker);  // G9: deathmatch "who hit whom"
	void (*statistic)(int which);                           // G2/G18: 0 gibs, 1 head shots, 2 throat, 3 groin
	void (*limbAI)(edict_t *piece);                         // G2: create the class-13 limb AI for a severed piece
	void (*cameraShake)(const vec3_t at, float a, float b, float c); // G18: func_camerashake parameters
	void (*explosionNoise)(const vec3_t at, float radius, float strength); // G2: AI explosion noise alert
	void (*setOnFire)(edict_t *targ, edict_t *attacker, float seconds);   // G18: ignite
	edict_t *(*dropWeapon)(edict_t *from, int weapon, const vec3_t org, const vec3_t vel); // G18: weapon pickup
	edict_t *(*dropArmour)(edict_t *corpse, int count);     // G18: armour pickup under a corpse
	void (*playerWeaponLose)(edict_t *player);              // G18: `weaponlose` player command
	bool (*cinematic)(void);                                // G6b: a cinematic is running
	void (*fireAttack)(edict_t *shooter, int attackType, const vec3_t start, const vec3_t dir, edict_t *target);
	                                                        // G18: server-side attack definition (SPEC-782)
	ghoul::IGhoulObject *(*modelObject)(const char *dir, const char *model, const char *skin);
	                                                        // G9: SPEC-791 model records (default: G3's registry)
	void (*playerSkinRefresh)(edict_t *player);             // G18: armour changed between none and some (SPEC-720 13)
};
extern G3Hooks g3hooks;
