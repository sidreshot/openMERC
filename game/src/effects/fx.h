// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — server-side effect emission (public interface).
//
// What the game sends to clients to make an effect happen (SPEC-771 §C): temp entities (svc_temp_entity, SPEC-201
// §1) with the shared rules (lock_blood suppression, the game-wide debris and weapon-impact rate limits), impact
// descriptors built from traces, named effects (svc_effect, SPEC-201 §5) with the pending extension values, and the
// entity-state effects (events, effect slots, rotation record, fade; SPEC-202, drawn as SPEC-400/401).
//
// Other packages should emit these messages through this header so the rate limits are shared game-wide:
//   G18 (bullets, projectiles: WeaponImpact, Debris), G3 (gore: BloodSplat, Debris chunk 32, BloodEffect...),
//   G4 (vehicle bodies: Sparks, Explosion), G2 (AI debug lines are not provided here).
// All functions are safe to call with NULL entities where documented; positions are world coordinates.
// Multicast values are those of SPEC-001 (MULTICAST_PHS etc.).
//
// Implements: SPEC-771 C (C.1-C.4), SPEC-761 3.12 (one shake tick), SPEC-201 1/5 (message forms)
#pragma once

#include "../g_local.h"

namespace fx {

// temp-entity numbers sent by the game (SPEC-201 §1 / SPEC-771 C.2)
enum {
	TE_SPARKS_WHITE = 0,
	TE_NOP = 1,
	TE_BLOODSPLAT = 3,
	TE_IMPACT = 6,
	TE_IMPACT_STAGE = 7,
	TE_ARC = 8,
	TE_FLYBY = 9,
	TE_SPARKS_ORANGE = 10,
	TE_GROUNDFLASH = 13,
	TE_SPARKS = 14,
	TE_SCORCH = 15,
	TE_LIGHTNING = 16,
	TE_DECAL17 = 17,
	TE_DEBRIS = 18,
	TE_EXPLOSION_DEBRIS = 19,
	TE_FLASHGRENADE = 20,
	TE_SMOKEBURST = 26,
	TE_PIPELEFT = 28,
	TE_LITSPARKS = 31,
	TE_LARGE_EXPLOSION_SND = 44,
	TE_PUFF = 45,
	TE_EXPLOSION_SMALL = 46,
	TE_PAPER = 48,
	TE_SPARKSHOWER = 49,
	TE_SODACANS = 50,
	TE_DUST = 52,
	TE_PUDDLE = 53,
	TE_RICOCHET = 54,
	TE_EXPLOSION_MEDIUM = 55,
	TE_EXPLOSION_LARGE = 56,
	TE_PHONEHIT = 58,
	TE_LIDSPIN = 59
};

// ---------------------------------------------------------------------------------------------------------------
// C.1 shared rules

// lock_blood parental option (SPEC-721): blood effects suppressed
bool BloodLocked(void);

// Impact descriptor (SPEC-201 §1 "impact", SPEC-771 C.1)
struct Impact {
	int kind;          // 0 world surface, 1 world point, 2 brush entity, 3 other entity
	vec3_t point;      // kind 2: offset from the entity origin rotated by -yaw
	vec3_t normal;     // kind 0/2
	int entnum;        // kind 2
	int material;      // kind 2/3
};
// from a trace result; `tr.fraction` must be < 1 (a hit)
void ImpactFromTrace(const trace_t &tr, Impact &out);
// "impact along a direction d at P": trace P + 5d -> P - 35d (solid|window, ignoring `ignore`)
void ImpactAlong(const vec3_t P, const vec3_t d, edict_t *ignore, Impact &out);
void WriteImpact(const Impact &imp);

// ---------------------------------------------------------------------------------------------------------------
// C.2 temp entities (each PHS at its first position unless stated)

void Sparks(int te, const vec3_t p, const vec3_t dir);              // TE 0 / 10 / 14 (and 31, 49: same payload)
void PointTE(int te, const vec3_t p, int multicast = MULTICAST_PHS); // TE 1, 20, 54, 58, 59 (payload = pos only)
void Arc(const vec3_t p, int length, const vec3_t dir);              // TE 8
void GroundFlash(const vec3_t p, edict_t *ignore);                   // TE 13: impact below P
// floor decal kinds 1 scorch (TE 15), 2 (TE 17), 3 pipeleft (TE 28), 4 puddle (TE 53); kind 0 = blood pool effect
void FloorDecal(int kind, const vec3_t p, int lifetime = 0);
void Lightning(void);                                                // TE 16, reliable to all
// TE 18 with the game-wide repeat filter (same surface byte within 0.1 s dropped) and the flesh lock;
// returns whether the message was sent
bool Debris(const vec3_t p, const vec3_t dir, int count, int size, int chunk, int ex, int ey, int ez, int surface);
void ExplosionDebris(edict_t *ent);                                  // TE 19 along the entity's forward
void SmokeBurst(const vec3_t p, int speed);                          // TE 26
void Puff(const vec3_t p, int r, int g, int b, int a);               // TE 45
void Explosion(int te, const vec3_t p, int sizebyte, int soundbyte); // TE 46 / 55 / 56
void Paper(const vec3_t p, int count);                               // TE 48
void SodaCans(const vec3_t p, const vec3_t dir, int speed, int count); // TE 50
void Dust(const vec3_t p, int count);                                // TE 52 (PVS)
void LargeExplosion(const vec3_t p, int magnitude);                  // TE 44
// TE 3 blood splat: trace from P along `dir` for 128 units (solid|window, ignoring `ignore`); sent when it hits
// the world or a brush entity at fraction <= 0.99 and blood is not locked; returns whether sent
bool BloodSplat(const vec3_t P, const vec3_t dir, edict_t *ignore, float reach = 128);
// TE 6 / 7 weapon impact from a shot trace: not on sky, not on characters/players under lock_blood, rate-limited
// for surface materials 32, 37, 38, 39, 40, 41 (one per 0.2 s game-wide); `stagechanged` selects TE 7
bool WeaponImpact(const trace_t &tr, const vec3_t shotdir, int power, int style, int debris, bool stagechanged);

// ---------------------------------------------------------------------------------------------------------------
// C.3 named effects (svc_effect)

int Index(const char *name);                // effect configstring index (registers on first use)
// pending extension values: travel with the next named effect whose index is non-zero
void ExtScale(float scale);
void ExtPos2(const vec3_t p);
void ExtDir(const vec3_t d);
void EffectAt(int effect, const vec3_t pos, int multicast = MULTICAST_PHS, edict_t *exclude = NULL);
void EffectAtName(const char *name, const vec3_t pos, int multicast = MULTICAST_PHS);
void EffectEnt(int effect, edict_t *ent, int multicast = MULTICAST_PHS, edict_t *exclude = NULL);
// bolt 0 = random bolt; reversed = flags 0x40
void EffectBolt(int effect, edict_t *ent, int bolt, bool reversed = false, int multicast = MULTICAST_PHS,
                edict_t *exclude = NULL);
void EffectInstBolt(int effect, edict_t *ent, int inst, int bolt, bool reversed = false,
                    int multicast = MULTICAST_PHS, edict_t *exclude = NULL);
// blood pool (floor decal kind 0): `gore/bloodpool` at a position, unless blood is locked
void BloodPool(const vec3_t p);

// ---------------------------------------------------------------------------------------------------------------
// C.4 entity-state effects

// events: first free of the three slots, "another pair follows" bits; a fourth overwrites slot 3
void AddEvent(edict_t *ent, int id, int parm = 0);
void ClearEvents(edict_t *ent);

// effect slots: id, bolt, scale (sent as trunc(scale*16)); bolt 0 = entity origin / any (remove, has)
bool SlotAdd(edict_t *ent, int effect, int bolt = 0, float scale = 0);
bool SlotAddName(edict_t *ent, const char *name, int bolt = 0, float scale = 0);
void SlotRemove(edict_t *ent, int effect, int bolt = 0);
void SlotRescale(edict_t *ent, int effect, int bolt, float scale);
bool SlotHas(edict_t *ent, int effect, int bolt = 0);

// rotation record (effects bit 0x0001): v0 = 999, v1..v3 = trunc(512 * rate), v4..v7 = instance ids
// "set" clears all twelve values first
void RotationSet(edict_t *ent, const float rate[3], const int inst[4]);
void RotationClear(edict_t *ent);
bool RotationActive(edict_t *ent);
// raw access to v0..v11 (the twelve 16-bit slot values in the order of SPEC-400)
short RotationValue(edict_t *ent, int i);
void RotationSetValue(edict_t *ent, int i, int value);

// fade (effects bit 0x8000)
void FadeOut(edict_t *ent, float seconds);
// returns true when the fade finished (the caller removes the entity)
bool FadeCheck(edict_t *ent);
void FadeCancel(edict_t *ent);

// ---------------------------------------------------------------------------------------------------------------
// shared world helpers used by effects and props

// one tick of the func_camerashake rule (SPEC-761 3.12): every player whose centre is within `radius` gets entity
// event 16 + level with parameter `parm`
void ShakeTick(const vec3_t origin, float maxintensity, float radius, int parm);

// character noise alert (AI spec). The AI package sets the hook; without it nothing happens.
typedef void (*ai_noise_fn)(edict_t *source, const vec3_t where, float loudness, float radius);
extern ai_noise_fn g_ai_noise;
void AINoise(edict_t *source, const vec3_t where, float loudness, float radius);

// character attacks (SPEC-782 §4, §11) used by environ_invisible_attack and the turret props: the weapons code (G18)
// sets this hook; `extra` = throw speed of thrown attacks, `nodamage` = effects only. Without it nothing is fired.
typedef void (*attack_fn)(edict_t *owner, int attack, const vec3_t start, const vec3_t dir, float extra,
                          bool nodamage);
extern attack_fn g_fire_attack;
// fires through g_fire_attack, else through G3's g3hooks.fireAttack (no throw speed / no-damage option there)
void FireAttack(edict_t *owner, int attack, const vec3_t start, const vec3_t dir, float extra, bool nodamage);
// scripted kill setting of a character body (SPEC-720 §11.1, variant 2 = next hit fatal): set by G3
typedef void (*scripted_kill_fn)(edict_t *character, int variant);
extern scripted_kill_fn g_arm_scripted_kill;

// reset the game-wide filters and pending values at level start (called by the G7 level hooks)
void LevelReset(void);

} // namespace fx
