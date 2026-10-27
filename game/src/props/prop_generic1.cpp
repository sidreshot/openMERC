// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — generic map objects, part 1 (misc_generic_access_card ... light_flare,
// without the automatic guns, which are in prop_turret.cpp).
// Implements: SPEC-770 §6A.1-§6A.6

#include "props.h"
#include "../effects/fx.h"
#include "../damage/damage_api.h"

namespace props {

static const vec3_t kZero = {0, 0, 0};

// skin by the entity's skin number: s[k-1] for k = 1..n, else def
static const char *SkinBy(edict_t *e, const char *def, const char *s1 = NULL, const char *s2 = NULL,
                          const char *s3 = NULL, const char *s4 = NULL)
{
	const char *s[4] = {s1, s2, s3, s4};
	int k = e->s.skinnum;
	if (k >= 1 && k <= 4 && s[k - 1])
		return s[k - 1];
	return def;
}

static void G(edict_t *ent, int idx, float x0, float y0, float z0, float x1, float y1, float z1,
              const char *skin = NULL, const char *only = NULL)
{
	SetBox(ent, x0, y0, z0, x1, y1, z1);
	Setup(ent, T_GENERIC, idx, skin, only);
}

static void Pain_Nothing(edict_t *, edict_t *, float, int, const vec3_t) {}
static void Pain_Parts(edict_t *self, edict_t *other, float, int, const vec3_t point)
{
	ShootableParts(self, other, point);
}

static void SetSkinAll(edict_t *ent, const char *material, const char *skin)
{
	const ModelEntry *e = D(ent).entry;
	gg::SetSkinInst(ent->ghoulInst, e ? e->dir : "", e ? e->file : "", material, skin);
	for (int i = 1; i <= gg::PartCount(ent); i++)
		if (gg::IGhoulInst *p = gg::TrackedPart(ent, i))
			gg::SetSkinInst(p, "", "", material, skin);
}

// ---------------------------------------------------------------------------------------------------------------
// 6A.1

static void SP_access_card(edict_t *ent)
{
	G(ent, 0, -2, -3, -1, 5, 3, 1);
	ent->spawnflags |= 3; // set after the setup: no effect on damage/physics
}

static void banner_idle(edict_t *self)
{
	if (self->spawnflags & 4) {
		gg::PauseEnt(self);
		return;
	}
	LoopSound(self, "Ambient/Models/Banner/BanFlap.wav");
	gg::PlayEnt(self, "banner", true);
}

static gg::NoteCallback s_banner_eos(banner_idle);
gg::NoteCallback *NoteBanner(void) { return &s_banner_eos; }

static void banner_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	if (!self->ghoulInst)
		return;
	gg::PlayEnt(self, "pain", false);
	gg::OnNote(self, NULL, "EOS", &s_banner_eos);
}

static void SP_banner(edict_t *ent)
{
	const char *skin = "banner1";
	switch (ent->s.skinnum) {
	case 1: skin = "banner2"; break;
	case 2: skin = g3locks.textures ? "banner5" : "banner3"; break;
	case 3: skin = "banner4"; break;
	}
	ClearFlags(ent, 1);
	Force(ent, 2);
	Sound("Ambient/Models/Banner/BanFlap.wav");
	ent->health = 999999;
	G(ent, 1, -4, -51, -70, 4, 51, 70, skin);
	SetPain(ent, banner_pain);
	if (ent->spawnflags & 4)
		gg::PauseEnt(ent);
	else {
		ent->think = banner_idle;
		ent->nextthink = level.time + irand(0, 4) * 0.1f;
	}
}

static void barbwire_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || !other->takedamage)
		return;
	vec3_t dir;
	VectorSubtract(other->s.origin, self->s.origin, dir);
	Damage(other, self, self, dir, other->s.origin, self->s.origin, 1, 1, 0x100, MOD_BARBWIRE);
}

static void SP_barbwire_coil(edict_t *ent)
{
	Toggle(ent, 1);
	Force(ent, 2);
	ent->touch = barbwire_touch;
	G(ent, 2, -15, -59, -17, 15, 59, 17);
}

static void burningbarrel_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || !other->takedamage)
		return;
	if (other->s.origin[2] + other->mins[2] <= self->s.origin[2])
		return;
	static const vec3_t up = {0, 0, 1};
	vec3_t p = {other->s.origin[0], other->s.origin[1], other->absmin[2]};
	Damage(other, self, self, up, p, self->s.origin, 1, 0, DAMAGE_NO_ARMOR | DAMAGE_FIRE, MOD_FIRE);
}

static void SP_barrel_burning(edict_t *ent)
{
	Toggle(ent, 1);
	if (ent->spawnflags & 4)
		ent->touch = burningbarrel_touch;
	G(ent, 3, -11, -10, -15, 11, 10, 15);
	if (ent->spawnflags & 4) {
		fx::SlotAdd(ent, fx::Index("environ/firesmall1"), gg::PartId(ent->ghoulInst, "DUMMY01"), 0);
		LoopSound(ent, "Ambient/Gen/FireFX/FireSmall.wav");
	}
}

// chemical barrel
static void chembarrel_explode(edict_t *self, edict_t *attacker)
{
	self->die = NULL;
	self->use = NULL;
	if (self->spawnflags & 4) {
		fx::GroundFlash(self->s.origin, self);
		fx::EffectAt(fx::Index("weapons/world/airexplode"), self->s.origin);
		fx::ExplosionDebris(self);
		RadiusDamage(self, self, 350, self, 128, MOD_UNKNOWN, 0);
		fx::AINoise(self, self->s.origin, 150, 200);
		fx::ShakeTick(self->s.origin, 700, 1000, 30);
		G_AlertNoise(0.5f);
	}
	StandardBreak(self, attacker, 0, 0);
}

static void chembarrel_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t) { chembarrel_explode(self, attacker); }
static void chembarrel_use(edict_t *self, edict_t *other, edict_t *) { chembarrel_explode(self, other); }

static void chembarrel_tumble(edict_t *self)
{
	if (self->health <= 0)
		return;
	if (fabsf(self->s.angles[PITCH]) < 90 && fabsf(self->s.angles[ROLL]) < 90) {
		self->avelocity[PITCH] *= 1.6f;
		self->avelocity[ROLL] *= 1.6f;
		self->nextthink = level.time + FRAMETIME;
		return;
	}
	for (int a = 0; a < 3; a += 2) {
		if (self->s.angles[a] > 90)
			self->s.angles[a] = 90;
		if (self->s.angles[a] < -90)
			self->s.angles[a] = -90;
	}
	VectorClear(self->avelocity);
	if (self->health < 25) {
		self->nextthink = level.time + 0.3f;
		return;
	}
	// settle
	if (!(self->spawnflags & 4) && abs((int)self->velocity[0]) < 50 && abs((int)self->velocity[1]) < 50 &&
	    abs((int)self->velocity[2]) < 50)
		fx::FloorDecal(2, self->s.origin, 10000);
	self->gravity = 1;
	self->movetype = MOVETYPE_STEP;
	SetPain(self, NULL);
	self->mins[2] += 6;
	gi.linkentity(self);
}

static void chembarrel_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	KnockOver(self, other, damage, 4);
	self->think = chembarrel_tumble;
}

static void SP_barrel_chemical(edict_t *ent)
{
	const char *skin = (ent->spawnflags & 4) ? "barrel2" : (ent->s.skinnum == 1 ? "barrel3" : "barrel");
	if ((ent->spawnflags & 4) && !ent->health)
		ent->health = 1;
	MarkSoundList(58);
	Sound("Ambient/Gen/FireFX/FireSmall.wav");
	SetMaterialFlag(19);
	SetMaterialFlag(20);
	G(ent, 4, -9, -9, -15, 10, 8, 15, skin);
	ent->die = chembarrel_die;
	ent->use = chembarrel_use;
	SetPain(ent, chembarrel_pain);
}

static void SP_barrel_wine(edict_t *ent) { G(ent, 5, -15, -10, -10, 15, 10, 10); }

// ---------------------------------------------------------------------------------------------------------------
// 6A.2

static const PartRow kBenchParts[] = {{"DUMMY02", 1, NULL}};

static void bench_pain(edict_t *self, edict_t *other, float, int, const vec3_t p)
{
	vec3_t f, r, u;
	AngleVectors(self->s.angles, f, r, u);
	if (u[2] > 0.9f) {
		vec3_t rel, end;
		VectorSubtract(p, self->s.origin, rel);
		gg::IGhoulInst *top = gg::TrackedPart(self, 1);
		// "free" = the trace reaches its end; a start inside another entity's box does not block (black-box,
		// g7shot: a bench standing inside a table's box falls when shot from the front)
		if (DotProduct(f, rel) <= 0) {
			VectorMA(self->s.origin, 100, f, end);
			trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
			if (tr.fraction >= 1) {
				vec3_t m;
				VectorScale(f, 20, m);
				m[2] -= 7;
				self->s.angles[PITCH] = 70;
				VectorAdd(self->s.origin, m, self->s.origin);
				VectorSubtract(self->mins, m, self->mins);
				VectorSubtract(self->maxs, m, self->maxs);
				static const vec3_t ofs = {-1.5f, -2, 0};
				gg::SetInstOffsetRotation(top, ofs, 2, 1.03653f);
				gi.linkentity(self);
			}
		} else {
			VectorMA(self->s.origin, -100, f, end);
			trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
			if (tr.fraction >= 1) {
				self->s.angles[PITCH] = -90;
				VectorMA(self->s.origin, -20, f, self->s.origin);
				vec3_t m = {20 * f[0], 20 * f[1], 20 * f[2] + 6};
				VectorAdd(self->mins, m, self->mins);
				VectorAdd(self->maxs, m, self->maxs);
				gg::SetInstRotation(top, 1, -0.3927f);
				gi.linkentity(self);
			}
		}
	}
	ShootableParts(self, other, p);
}

static void SP_bench(edict_t *ent)
{
	const char *skin = ent->s.skinnum == 1 ? "bench2" : "bench";
	G(ent, 6, -10, -30, -17, 10, 30, 17, skin);
	VectorCopy(ent->s.angles, D(ent).start_angles);
	Bolt(ent, T_GENERIC, 6, "DUMMY02", 7, "DUMMY01", skin);
	D(ent).parts = kBenchParts;
	D(ent).nparts = 1;
	SetPain(ent, bench_pain);
}

static void blinds_pause(edict_t *self) { gg::PauseEnt(self); }

static void blinds_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	if (self->ghoulInst)
		self->ghoulInst->Resume(level.time);
	self->think = blinds_pause;
	self->nextthink = level.time + 0.5f;
	gg::SetSkinInst(self->ghoulInst, "objects/generic/blinds", "blinds", "blinds", "blindsdmg");
}

static void SP_blinds(edict_t *ent)
{
	Toggle(ent, 1);
	Force(ent, 2);
	G(ent, 8, -1, -55, -65, 0, 55, 65, "blinds");
	if (ent->spawnflags & 4)
		gg::PauseEnt(ent);
	SetPain(ent, blinds_pain);
}

static const PartRow kShelfParts[] = {
	{"DUMMY01", 1, PartReactNone}, {"DUMMY02", 2, PartReactNone}, {"DUMMY03", 3, PartReactNone}};

static void SP_bookshelf(edict_t *ent)
{
	G(ent, 9, -40, -16, 0, 40, 16, 64, ent->s.skinnum == 1 ? "middle" : "case");
	Bolt(ent, T_GENERIC, 9, "DUMMY01", 10, "DUMMY01");
	Bolt(ent, T_GENERIC, 9, "DUMMY02", 10, "DUMMY04");
	Bolt(ent, T_GENERIC, 9, "DUMMY03", 10, "DUMMY01");
	D(ent).parts = kShelfParts;
	D(ent).nparts = 3;
	SetPain(ent, Pain_Parts);
}

static void SP_broom(edict_t *ent)
{
	G(ent, 12, -2, -16, -25, 2, 16, 25, ent->s.skinnum == 1 ? "broom2" : "broom");
	SetPain(ent, Pain_Nothing);
}

// bunk: S1 = part 1, L1 = 2, L2 = 3, S2 = 4
static void bunk_box(edict_t *self, float z0, float z1, bool solid2)
{
	SetBox(self, -40, -20, z0, 40, 20, z1);
	if (solid2)
		self->solid = SOLID_BBOX;
}

static void bunk_react(edict_t *self, int part, edict_t *, const vec3_t)
{
	PropData &d = D(self);
	auto has = [&](int i) { return gg::PartShown(self, i); };
	bool S1 = has(1), L1 = has(2), L2 = has(3), S2 = has(4);
	float sp = d.start_angles[PITCH];
	vec3_t at;
	PartPos(self, part, at);
	gg::IGhoulInst *iS1 = gg::TrackedPart(self, 1), *iS2 = gg::TrackedPart(self, 4), *iL1 = gg::TrackedPart(self, 2);
	auto upright = [&]() { bunk_box(self, 35, 40, true); };
	if (part == 1) {
		if (!L1 && !L2) {
			if (S2) {
				gg::RotateInst(iS2, 2, 0.9425f);
				self->s.angles[PITCH] = sp - 90;
				upright();
				gg::BoltWorld(self, iS2, "DUMMY03", at);
			}
		} else if (!L1 && L2) {
			if (!S2) {
				self->s.angles[PITCH] = sp;
				bunk_box(self, -20, 5, false);
			} else {
				self->s.angles[PITCH] -= 6;
				bunk_box(self, -24, 29, false);
			}
		} else if (L1 && L2) {
			if (!S2) {
				RemovePart(self, 2, true);
				self->s.angles[PITCH] = sp;
				bunk_box(self, -20, 5, false);
			} else {
				self->s.angles[PITCH] -= 6;
				bunk_box(self, -24, 29, false);
				gg::RotateInst(iL1, 2, -0.34558f);
				gg::BoltWorld(self, iL1, "DUMMY01", at);
			}
		} else { // L1 only
			if (!S2) {
				self->s.angles[PITCH] = sp;
				bunk_box(self, 15, 25, false);
			} else {
				self->s.angles[PITCH] -= 30;
				bunk_box(self, -10, 29, false);
			}
		}
	} else if (part == 2 || part == 3) {
		bool other = part == 2 ? L2 : L1;
		if (!other) {
			if (S1 && S2) {
				gg::RotateInst(iS1, 2, 0.9425f);
				gg::RotateInst(iS2, 2, -0.9425f);
			} else if (S1) {
				self->s.angles[PITCH] = sp + 90;
				upright();
			} else if (S2) {
				self->s.angles[PITCH] = sp - 90;
				upright();
			}
		}
	} else if (part == 4) {
		if (!S1) {
			if (!L1 && L2) {
				self->s.angles[PITCH] = sp;
				bunk_box(self, -20, 5, false);
			}
		} else if (!L1 && !L2) {
			gg::RotateInst(iS1, 2, -0.9425f);
			self->s.angles[PITCH] = sp + 90;
			upright();
			gg::BoltWorld(self, iS1, "DUMMY03", at);
		} else if (!L1 && L2) {
			self->s.angles[PITCH] += 6;
			bunk_box(self, -24, 29, false);
		} else if (L1 && L2) {
			RemovePart(self, 2, true);
			self->s.angles[PITCH] += 6;
			bunk_box(self, -24, 29, false);
		} else { // L1 only
			self->s.angles[PITCH] += 30;
			bunk_box(self, -10, 29, false);
		}
	}
	gi.linkentity(self);
	PartDebris(self, at);
}

static const PartRow kBunkParts[] = {
	{"DUMMY05", 1, bunk_react}, {"DUMMY01", 2, bunk_react}, {"DUMMY03", 3, bunk_react}, {"DUMMY03", 4, bunk_react}};

static void SP_bunk(edict_t *ent)
{
	G(ent, 13, -40, -20, -29, 40, 20, 29, ent->s.skinnum == 1 ? "bunk2" : "bunk");
	VectorCopy(ent->s.angles, D(ent).start_angles);
	Bolt(ent, T_GENERIC, 13, "DUMMY05", 15, "DUMMY05");
	Bolt(ent, T_GENERIC, 13, "DUMMY01", 14, "DUMMY02");
	Bolt(ent, T_GENERIC, 13, "DUMMY03", 14, "DUMMY02");
	gg::IGhoulInst *s2 = Bolt(ent, T_GENERIC, 13, "DUMMY03", 15, "DUMMY05");
	gg::HidePart(ent->ghoulInst, "SIDE02");
	gg::RotateInst(s2, 0, (float)M_PI);
	D(ent).parts = kBunkParts;
	D(ent).nparts = 4;
	SetPain(ent, Pain_Parts);
}

// chair with legs
static void chairlegs_react(edict_t *self, int part, edict_t *, const vec3_t)
{
	PropData &d = D(self);
	auto has = [&](int i) { return gg::PartShown(self, i); };
	int n = has(1) + has(2) + has(3) + has(4);
	bool debris = true;
	if (n == 2) {
		if (has(1) && has(2)) {
			self->s.angles[PITCH] -= 40;
			SetBox(self, -10, -9, -6, 10, 9, 18);
		} else if (has(3) && has(4)) {
			self->s.angles[PITCH] += 40;
			SetBox(self, -10, -9, -6, 10, 9, 18);
		} else if (has(2) && has(3)) {
			self->s.angles[ROLL] += 40;
			SetBox(self, -10, -9, -6, 10, 9, 18);
		} else if (has(1) && has(4)) {
			self->s.angles[ROLL] -= 40;
			SetBox(self, -10, -9, -6, 10, 9, 18);
		} else { // 1 and 3, or 2 and 4
			for (int i = 1; i <= 4; i++)
				if (has(i))
					RemovePart(self, i, true);
			VectorCopy(d.start_angles, self->s.angles);
			SetBox(self, -10, -9, 0, 10, 9, 18);
		}
	} else if (n == 0) {
		VectorCopy(d.start_angles, self->s.angles);
		SetBox(self, -10, -9, 0, 10, 9, 18);
		debris = false;
	}
	gi.linkentity(self);
	if (debris)
		fx::Debris(self->s.origin, kZero, 7, 1, self->material, 0, 0, 0, self->surfacetype);
	(void)part;
}

static const PartRow kChairLegParts[] = {
	{"DUMMY01", 1, chairlegs_react}, {"DUMMY02", 2, chairlegs_react}, {"DUMMY03", 3, chairlegs_react},
	{"DUMMY04", 4, chairlegs_react}};

static void SP_chair_legs(edict_t *ent)
{
	const char *skin = "legs";
	if (ent->s.skinnum == 1) {
		skin = "legsb";
		ent->surfacetype = 14;
		ent->material = 17;
	} else if (ent->s.skinnum == 2) {
		skin = "legsplastic";
		ent->surfacetype = 0;
		ent->material = 31;
	} else {
		ent->surfacetype = 28;
		ent->material = 8;
	}
	ent->mass = 50;
	G(ent, 23, -10, -9, -12, 10, 9, 18, skin);
	VectorCopy(ent->s.angles, D(ent).start_angles);
	for (int i = 1; i <= 4; i++)
		Bolt(ent, T_GENERIC, 23, va("DUMMY0%d", i), 24, "DUMMY01", skin);
	D(ent).parts = kChairLegParts;
	D(ent).nparts = 4;
	SetPain(ent, Pain_Parts);
}

static void SP_chair_legs2(edict_t *ent) { G(ent, 25, -12, -10, -20, 12, 10, 20); }

// office chair: spin record on the seat; i0 = spinning
static void officechair_think(edict_t *self)
{
	int v = fx::RotationValue(self, 1);
	int av = abs(v);
	if (av > 2000)
		PlaySound(self, CHAN_BODY, "Impact/Chair/Squeak1.wav", 0.8f, ATTN_NORM);
	else if (av > 1000 && irand(0, 6) <= 2)
		PlaySound(self, CHAN_BODY, "Impact/Chair/Squeak1.wav", 0.8f, ATTN_NORM);
	v = (int)(v * 0.75f);
	if (abs(v) < 153.6f) {
		fx::RotationClear(self);
		return;
	}
	fx::RotationSetValue(self, 1, v);
	self->nextthink = level.time + 0.5f;
}

static void officechair_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t p)
{
	float w = damage * 0.1f;
	vec3_t d, r, c;
	const float *src = other ? other->s.origin : self->s.origin;
	VectorSubtract(src, self->s.origin, d);
	VectorNormalize(d);
	VectorSubtract(p, self->s.origin, r);
	CrossProduct(d, r, c);
	if (c[2] == 0)
		return;
	float sgn = c[2] < 0 ? 1.0f : -1.0f;
	if (!fx::RotationActive(self)) {
		gg::IGhoulInst *seat = gg::TrackedPart(self, 1);
		float rate[3] = {sgn * (w < 30 ? w : 30), 0, 0};
		int inst[4] = {seat ? seat->GetInstanceId() : 0, 0, 0, 0};
		fx::RotationSet(self, rate, inst);
		self->think = officechair_think;
		self->nextthink = level.time + 0.5f;
	} else
		fx::RotationSetValue(self, 1, fx::RotationValue(self, 1) + (int)(sgn * w * 512));
}

static void SP_chair_office(edict_t *ent)
{
	const char *skin = "office";
	if (ent->s.skinnum == 1) {
		skin = "office2";
		ent->material = 9;
	}
	Sound("Impact/Chair/Squeak1.wav");
	G(ent, 27, -13, -13, -17.5f, 13, 13, 24, skin);
	VectorCopy(ent->s.angles, D(ent).start_angles);
	Bolt(ent, T_GENERIC, 27, "DUMMY02", 26, "DUMMY01", skin);
	SetPain(ent, officechair_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// 6A.3

static void SP_car_american(edict_t *ent)
{
	const char *skin = SkinBy(ent, "car1", "car2", "car3", "car4");
	Force(ent, 2);
	G(ent, 16, -69, -30, -25, 69, 30, 25, skin);
	SetBox(ent, -69, -30, -25, 69, 30, 25);
	gi.linkentity(ent);
	int k = ent->s.skinnum;
	gg::IGhoulInst *in = ent->ghoulInst;
	static const char *taxi[] = {"_TAXILIGHT01", "TAXI_OMNI01", "TAXI_OMNI02"};
	static const char *police[] = {"MIDDLE_SECTION", "REDSIREN", "BLUESIREN", "REDCONE", "BLUECONE", "WORLD_1",
	                               "WORLD_2"};
	if (k == 1 || k == 2) {
		for (const char *p : police)
			gg::HidePart(in, p);
		if (k == 2)
			for (const char *p : taxi)
				gg::HidePart(in, p);
	} else
		for (const char *p : taxi)
			gg::HidePart(in, p);
	gg::PlayEnt(ent, "off", false);
	if (ent->spawnflags & 4)
		gg::PlayEnt(ent, "on", false);
	if (ent->spawnflags & 8) {
		LoopSound(ent, "Ambient/Models/Alarm/Police.wav", 0xFA);
		gg::PlayEnt(ent, "car", true);
	}
}

static void appear_use(edict_t *self, edict_t *, edict_t *)
{
	self->solid = SOLID_BBOX;
	if (self->ghoulInst)
		self->ghoulInst->SetOnOff(true, level.time);
	gi.linkentity(self);
}

static void HideUntilUsed(edict_t *ent)
{
	ent->solid = SOLID_NOT;
	if (ent->ghoulInst)
		ent->ghoulInst->SetOnOff(false, level.time);
	ent->use = appear_use;
	gi.linkentity(ent);
}

static void SP_car_european(edict_t *ent)
{
	Force(ent, 2);
	G(ent, 17, -91, -33, -28, 91, 33, 30, SkinBy(ent, "car", "car2", "car3"));
	if (ent->spawnflags & 4)
		HideUntilUsed(ent);
}

static void SP_car_honda(edict_t *ent)
{
	Force(ent, 2);
	G(ent, 18, -55, -25, -23, 55, 25, 30);
}

// Rolls-Royce: i0 = played, i1 = alarm running
static void rolls_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || !other->client)
		return;
	LoopSound(self, "Ambient/Models/Alarm/CarAlarm.wav");
	D(self).i1 = 1;
	self->touch = NULL;
}

static void rolls_use(edict_t *self, edict_t *, edict_t *)
{
	gg::PlayEnt(self, "car", false);
	D(self).i0 = 1;
}

static void rolls_smokeoff(edict_t *self) { fx::SlotRemove(self, fx::Index("environ/helismoke")); }

static void rolls_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	SetPain(self, NULL);
	PropData &d = D(self);
	if (!d.i0)
		gg::PlayEnt(self, "car", false);
	Explode(self, false, false);
	if (d.i1) {
		StopLoop(self);
		d.i1 = 0;
		PlaySound(self, CHAN_BODY, "Ambient/Models/Alarm/CarDisarm.wav", 0.8f, ATTN_NORM);
	}
	self->touch = NULL;
	fx::SlotAdd(self, fx::Index("environ/helismoke"), gg::PartId(self->ghoulInst, "dummy01"), 0);
	self->think = rolls_smokeoff;
	self->nextthink = level.time + 5;
}

static void SP_car_rolls(edict_t *ent)
{
	Force(ent, 2);
	MarkSoundList(58);
	Sound("Ambient/Models/Alarm/CarDisarm.wav");
	Sound("Ambient/Models/Alarm/CarAlarm.wav");
	fx::Index("environ/helismoke");
	ent->touch = rolls_touch;
	G(ent, 19, -84, -29, -24, 84, 29, 46);
	gg::PauseEnt(ent);
	ent->use = rolls_use;
	ent->plUse = rolls_use;
	SetPain(ent, rolls_pain);
}

static void SP_car_sports(edict_t *ent) { G(ent, 20, -82, -35, -19, 82, 35, 19, SkinBy(ent, "car", "car2")); }

static void SP_cart(edict_t *ent)
{
	const char *skin = SkinBy(ent, "cart", "cart2", "cartwood");
	if (ent->s.skinnum == 2) {
		ent->surfacetype = 14;
		ent->material = 17;
	}
	ent->mass = 25;
	G(ent, 21, -10, -20, -11, 10, 20, 11, skin);
}

static void register_smokeoff(edict_t *self) { fx::SlotRemove(self, fx::Index("environ/normalsmoke")); }

static void register_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	SetPain(self, NULL);
	for (int i = 0; i < 2; i++) {
		vec3_t d = {(float)irand(-10, 10), (float)irand(-10, 10), 0};
		VectorNormalize(d);
		fx::Sparks(fx::TE_SPARKS, self->s.origin, d);
	}
	if (self->s.skinnum == 0)
		gg::SetSkinInst(self->ghoulInst, "objects/generic/cash_register", "register", "register", "registerd");
	fx::SlotAdd(self, fx::Index("environ/normalsmoke"), 0, 0);
	self->think = register_smokeoff;
	self->nextthink = level.time + 2;
}

static void SP_cash_register(edict_t *ent)
{
	fx::Index("environ/normalsmoke");
	// model with all its skins, then the skin (black-box: the original's object lists register, registerd; arm1, jpn1)
	G(ent, 22, -8, -8, -9, 8, 8, 9);
	SkinAfterSetup(ent, ent->s.skinnum == 1 ? "registerd" : "register");
	SetPain(ent, register_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// 6A.4 chunks

static void SP_chunks_brick(edict_t *ent)
{
	const char *part = ent->style == 1 ? "CHUNK_2" : ent->style == 2 ? "CHUNK_3" : "CHUNK_1";
	G(ent, 28, -6, -3, -2, 6, 3, 2, SkinBy(ent, "red", "grey_dk", "brwn_dk", "brwn_lt", "grey_lt"), part);
}

static void SP_chunks_glass(edict_t *ent)
{
	const char *part = ent->style == 1 ? "GLASS02" : ent->style == 2 ? "GLASS03" : "GLASS01";
	G(ent, 29, 0, -1, -1, 1, 1, 1, NULL, part);
}

static void SP_chunks_metal(edict_t *ent)
{
	static const char *parts[] = {"METAL1", "METAL2", "METAL3", "METAL4"};
	int s = (ent->style >= 1 && ent->style <= 3) ? ent->style : 0;
	G(ent, 30, -4, -2, -4, 4, 2, 4, SkinBy(ent, "dark", "rusty", "shiny", "light"), parts[s]);
}

static void SP_chunks_rock(edict_t *ent)
{
	static const char *parts[] = {"ROCK1", "ROCK2", "ROCK3", "ROCK4"};
	int s = (ent->style >= 1 && ent->style <= 3) ? ent->style : 0;
	G(ent, 31, 0, -1, -1, 1, 1, 1, SkinBy(ent, "brwn_dk", "brwn_lt", "grey_dk", "grey_lt"), parts[s]);
}

static void SP_chunks_wall(edict_t *ent)
{
	static const char *skins[] = {"black", "brown", "lbrown", "dbrown", "grey", "dgrey", "lgrey", "green",
	                              "orange_cone", "red", "white"};
	static const int surf[] = {17, 24, 17, 19, 19, 17, 18, 18, 22, 23, 4};
	int k = (ent->s.skinnum >= 1 && ent->s.skinnum <= 10) ? ent->s.skinnum : 0;
	ent->surfacetype = surf[k];
	static const char *parts[] = {"_LITTLECHUNK", "_SMALLCHUNK", "_MEDIUMCHUNK", "_LARGECHUNK"};
	int s = (ent->style >= 1 && ent->style <= 3) ? ent->style : 0;
	G(ent, 32, -4, -2, -4, 4, 2, 4, skins[k], parts[s]);
}

static void SP_chunks_wood(edict_t *ent)
{
	static const char *parts[] = {"WOOD1", "WOOD2", "WOOD3", "WOOD4"};
	int s = (ent->style >= 1 && ent->style <= 3) ? ent->style : 0;
	// skin 0 or > 3 passes an invalid string in 1.07f (warning, default skin): treated as "no skin"
	G(ent, 33, 0, -1, -1, 1, 1, 1, SkinBy(ent, NULL, "grey_dk", "brwn_lt", "grey_lt"), parts[s]);
}

// ---------------------------------------------------------------------------------------------------------------
// 6A.5

static void keyboard_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	ThrowAway(self, other, (int)(0.8f * damage));
}

static void SP_comp_keyboard(edict_t *ent)
{
	G(ent, 34, -4, -11, -2, 4, 11, 2);
	SetPain(ent, keyboard_pain);
}

// broken screen: f0 = smoke timer T, i0 = 1 monitor / 2 monitor2
static void screen_think(edict_t *self)
{
	PropData &d = D(self);
	int smoke = fx::Index("environ/normalsmoke");
	if (level.time < d.f0 && fx::SlotHas(self, smoke)) {
		vec3_t f, r, p;
		AngleVectors(self->s.angles, f, r, NULL);
		VectorMA(self->s.origin, 8, f, p);
		VectorMA(p, -6, r, p);
		p[2] += irand(-6, 5);
		fx::Arc(p, 18, r);
	} else if (level.time >= d.f0) {
		fx::SlotRemove(self, smoke);
		StopLoop(self);
	}
	self->nextthink = level.time + FRAMETIME;
}

static void screen_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	SetPain(self, NULL);
	PropData &d = D(self);
	if (d.i0 == 1)
		SetSkinAll(self, "monitor", "damaged");
	else
		SetSkinAll(self, "monitor2", "monitor2_d");
	vec3_t f, p;
	AngleVectors(self->s.angles, f, NULL, NULL);
	VectorMA(self->s.origin, d.i0 == 1 ? 10.0f : 14.0f, f, p);
	fx::Explosion(fx::TE_EXPLOSION_SMALL, p, 100, 0);
	fx::SlotAdd(self, fx::Index("environ/normalsmoke"), 0, 0);
	PlaySound(self, CHAN_VOICE, "Impact/Glassbreak/Monitor.wav", 0.6f, ATTN_NORM);
	LoopSound(self, "Ambient/Gen/Electric/buzz.wav");
	d.f0 = level.time + frand(6, 12);
	self->think = screen_think;
	screen_think(self);
}

void TVDeath(edict_t *self, edict_t *attacker); // prop_generic2.cpp

static void monitor_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	fx::Debris(self->s.origin, kZero, 7, 1, self->material, 0, 0, 0, self->surfacetype);
	TVDeath(self, attacker);
}

static void ScreenPrecache(void)
{
	MarkSoundList(91);
	Sound("Impact/Glassbreak/Monitor.wav");
	Sound("Ambient/Gen/Electric/buzz.wav");
	fx::Index("environ/normalsmoke");
}

static void SP_comp_monitor(edict_t *ent)
{
	ScreenPrecache();
	// model with all its skins (original object: damaged, monitor; 11 maps), then the skin
	G(ent, 35, -9, -12, -12, 9, 12, 12);
	SkinAfterSetup(ent, "monitor");
	D(ent).i0 = 1;
	ent->die = monitor_die;
	SetPain(ent, screen_pain);
}

static void SP_comp_monitor2(edict_t *ent)
{
	ScreenPrecache();
	const char *skin = ent->s.skinnum ? "monitor2_d" : NULL;
	G(ent, 37, -9, -12, -12, 9, 12, 12, skin);
	Bolt(ent, T_GENERIC, 37, "DUMMY01", 36, "DUMMY01", skin);
	D(ent).i0 = 2;
	SetPain(ent, screen_pain);
}

static void SP_crate_fish(edict_t *ent) { G(ent, 38, -10, -7, -1, 10, 7, 8); }

// desk phone: f0 = smoke timer
static void phone_smoke(edict_t *self)
{
	if (level.time >= D(self).f0) {
		fx::SlotRemove(self, fx::Index("environ/normalsmoke"));
		StopLoop(self);
		return;
	}
	self->nextthink = level.time + 0.2f;
}

static void phone_pain_later(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	fx::PointTE(fx::TE_PHONEHIT, self->s.origin);
	ThrowAway(self, other, (int)(0.9f * damage));
}

static void phone_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	ThrowAway(self, other, (int)(0.9f * damage));
	fx::Explosion(fx::TE_EXPLOSION_SMALL, self->s.origin, 10, 0);
	fx::SlotAdd(self, fx::Index("environ/normalsmoke"), 0, 0);
	vec3_t u;
	AngleVectors(self->s.angles, NULL, NULL, u);
	fx::Sparks(fx::TE_SPARKS, self->s.origin, u);
	fx::PointTE(fx::TE_PHONEHIT, self->s.origin);
	D(self).f0 = level.time + frand(0.5f, 1.5f);
	self->think = phone_smoke;
	self->nextthink = level.time + FRAMETIME;
	SetPain(self, phone_pain_later);
}

static void SP_desk_phone(edict_t *ent)
{
	MarkSoundList(91);
	MarkSoundList(92);
	fx::Index("environ/normalsmoke");
	G(ent, 39, -8, -10, -5, 8, 10, 5, ent->s.skinnum == 1 ? "phone2" : "phone");
	SetPain(ent, phone_pain);
}

// big fan: f0 = current speed s, f1 = full speed, f2 = change per step, i0 = direction (+1 accel, -1 decel, 0 idle)
static void fan_record(edict_t *self, float s)
{
	float rate[3] = {0, s, 0};
	int inst[4] = {self->ghoulInst ? self->ghoulInst->GetInstanceId() : 0, 0, 0, 0};
	fx::RotationSet(self, rate, inst);
}

// big fan state machine (SPEC-AMEND-006 G7.10): i0 = state (0 off, 1 accelerating, 2 on, -1 decelerating),
// f0 = current speed c, f1 = full speed, f2 = speed change per 0.1 s
static void fan_think(edict_t *self)
{
	PropData &d = D(self);
	bool again = false;
	if (d.i0 == 1) {
		if (d.f1 - d.f0 < d.f2) {
			d.f0 = d.f1;
			d.i0 = 2;
		} else {
			d.f0 += d.f2;
			again = true;
		}
	} else if (d.i0 == -1) {
		if (d.f0 < d.f2) {
			d.f0 = 0;
			d.i0 = 0;
		} else {
			d.f0 -= d.f2;
			again = true;
		}
	}
	fan_record(self, d.f0);
	if (again)
		self->nextthink = level.time + FRAMETIME;
}

static void fan_use(edict_t *self, edict_t *, edict_t *)
{
	PropData &d = D(self);
	if (d.f1 > 31)
		d.f1 = 31;
	if (d.i0 == 0)
		d.i0 = 1; // off: accelerate, no record at the use
	else {
		// on, accelerating or decelerating: one full-speed record, then decelerate from the current speed
		fan_record(self, d.f1);
		d.i0 = -1;
	}
	self->think = fan_think;
	self->nextthink = level.time + FRAMETIME;
}

static void SP_fan_big(edict_t *ent)
{
	Force(ent, 2);
	G(ent, 40, -64, -40, -63, 63, 0, 63);
	PropData &d = D(ent);
	d.f2 = ent->volume ? ent->volume : 0.05f;
	d.f1 = ent->speed;
	d.i0 = 0;
	if (!(ent->spawnflags & 4)) {
		if (!d.f1)
			d.f1 = 1.5f;
		d.f0 = d.f1;
		d.i0 = 2;
		fan_record(ent, d.f0); // written before the clamp below (original order)
	}
	ent->use = fan_use;
}

static void SP_faucet(edict_t *ent)
{
	Force(ent, 2);
	G(ent, 41, -6, -5, -5, 11, 9, 5);
}

// fire extinguisher: i0 = leaking, i1 = burst pending, e0 = attacker of the burst
static void exting_break(edict_t *self) { TableBreak(self, D(self).e0); }

static void exting_burst(edict_t *self, edict_t *attacker)
{
	PropData &d = D(self);
	if (d.i1)
		return;
	d.i1 = 1;
	d.e0 = attacker;
	float h = (float)self->health;
	RadiusDamage(self, attacker, h, self, 0.5f * h + 60, MOD_EXPLOSIVE, 0);
	fx::ShakeTick(self->s.origin, 60, 900, 30);
	StopLoop(self);
	self->s.angles[YAW] += 180;
	self->think = exting_break;
	self->nextthink = level.time + FRAMETIME;
}

static void exting_plain_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	StandardBreak(self, attacker, 0, 0);
}

static void exting_leak(edict_t *self)
{
	PropData &d = D(self);
	if (!d.i0 || d.i1)
		return;
	self->health -= 1;
	if (self->health < 1) {
		d.i0 = 0;
		self->health = 1;
		self->takedamage = DAMAGE_YES;
		SetPain(self, NULL);
		self->die = exting_plain_die;
		StopLoop(self);
		return;
	}
	self->nextthink = level.time + FRAMETIME;
}

static void exting_pain(edict_t *self, edict_t *other, float, int, const vec3_t)
{
	if (D(self).i0)
		exting_burst(self, other);
}

static void exting_start(edict_t *self)
{
	LoopSound(self, "Impact/Exting/ExtingLeak.wav");
	D(self).i0 = 1;
	self->think = exting_leak;
	self->nextthink = level.time + FRAMETIME;
}

static void exting_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	PropData &d = D(self);
	if (d.i0) {
		exting_burst(self, attacker);
		return;
	}
	if (d.i1)
		return;
	gg::SetSkinInst(self->ghoulInst, "objects/generic/fire_extinguisher", "exting", "exting", "damaged");
	self->health = 50;
	fx::EffectEnt(fx::Index("environ/extinguisher1"), self);
	exting_start(self);
}

static void exting_use(edict_t *self, edict_t *, edict_t *)
{
	if (D(self).i0 || D(self).i1)
		return;
	self->health = 100 + irand(0, 9);
	exting_start(self);
}

static void SP_fire_extinguisher(edict_t *ent)
{
	Force(ent, 2);
	Sound("Impact/Exting/ExtingLeak.wav");
	fx::Index("environ/extinguisher1");
	// model with all its skins (original object: damaged, exting; 5 maps incl. dm/raven), then the skin
	G(ent, 42, -2, -6, -12, 4, 8, 12);
	SkinAfterSetup(ent, "exting");
	ent->die = exting_die;
	ent->use = exting_use;
	ent->plUse = exting_use;
	SetPain(ent, exting_pain);
}

static void hydrant_die(edict_t *self, edict_t *, edict_t *, int, vec3_t)
{
	self->die = NULL;
	self->takedamage = DAMAGE_NO;
	fx::Debris(self->s.origin, kZero, 5, 1, 0, 0, 0, 0, 0);
	fx::EffectAt(fx::Index("environ/toiletspout"), self->s.origin);
	self->think = G_FreeEdict;
	self->nextthink = level.time + 1;
}

static void SP_fire_hydrant(edict_t *ent)
{
	Force(ent, 2);
	Sound("Ambient/Gen/WtrFX/SprayBig.wav");
	G(ent, 43, -8, -8, -10, 8, 8, 14);
	ent->die = hydrant_die;
}

// swimming fish: v0 = A, v1 = B, i0 = goal (0 = B, 1 = A), i1 = used (sinking)
static void fish_think(edict_t *self)
{
	PropData &d = D(self);
	if (d.i1) {
		if (self->s.origin[2] + self->mins[2] > self->volume + 3) {
			self->s.origin[2] -= 1;
			gi.linkentity(self);
		}
	} else {
		const float *goal = d.i0 ? d.v0 : d.v1;
		vec3_t dd;
		VectorSubtract(goal, self->s.origin, dd);
		if (VectorLength(dd) < 10) {
			VectorNegate(self->velocity, self->velocity);
			self->s.angles[YAW] += 180;
			d.i0 = !d.i0;
		}
	}
	self->nextthink = level.time + FRAMETIME;
}

static void fish_use(edict_t *self, edict_t *, edict_t *)
{
	VectorScale(self->velocity, 2, self->velocity);
	D(self).i1 = 1;
}

static void SP_fish(edict_t *ent)
{
	PropData &d = D(ent);
	VectorCopy(ent->s.origin, d.v0);
	VectorCopy(ent->s.angles, d.v1);
	bool swim = d.v1[0] || d.v1[1] || d.v1[2];
	vec3_t dir = {0, 0, 0};
	if (swim) {
		VectorSubtract(d.v1, d.v0, dir);
		VectorNormalize(dir);
		vectoangles(dir, ent->s.angles);
	}
	G(ent, 44, -17, -6, -7, 17, 6, 7, ent->s.skinnum == 1 ? "yellow" : "white");
	ent->movetype = MOVETYPE_FLY;
	ent->flags |= FL_NO_KNOCKBACK;
	gg::PlayEnt(ent, "fish", true);
	ent->use = fish_use;
	if (swim) {
		VectorScale(dir, 20, ent->velocity);
		ent->think = fish_think;
		ent->nextthink = level.time + FRAMETIME;
	}
}

static void SP_fish_dead(edict_t *ent)
{
	G(ent, 45, -17, -6, -1, 17, 6, 7, ent->s.skinnum == 1 ? "yellow" : "white");
	ent->s.angles[ROLL] += 90;
}

// ---------------------------------------------------------------------------------------------------------------
// 6A.6

static void flagpole_start(edict_t *self)
{
	if (self->ghoulInst)
		gg::PlayInstAt(self->ghoulInst, "flag_pole", ghoul::kPlayLoop, level.time, 0);
}

static void SP_flag_pole(edict_t *ent)
{
	Force(ent, 2);
	G(ent, 46, -4, -65, -417, 4, 38, 36, SkinBy(ent, "flag_pole", "flag_pole2", "flag_pole3"));
	gg::PauseEnt(ent);
	ent->think = flagpole_start;
	ent->nextthink = level.time + irand(0, 4) * 0.1f;
}

static void SP_forklift(edict_t *ent)
{
	G(ent, 47, -73, -54, -53, 91, 4, 52, SkinBy(ent, "forklift", "forklift2", "forklift3"));
	gg::PauseEnt(ent);
	if (ent->spawnflags & 4)
		HideUntilUsed(ent);
}

void GarbageBreak(edict_t *self, edict_t *inflictor, edict_t *attacker)
{
	fx::Paper(self->s.origin, 0);
	vec3_t d = {0, 0, 1};
	edict_t *src = (attacker && attacker->inuse) ? attacker : (inflictor && inflictor->inuse) ? inflictor : NULL;
	if (src) {
		VectorSubtract(src->s.origin, self->s.origin, d);
		VectorNormalize(d);
	}
	fx::SodaCans(self->s.origin, d, 100, 0);
	TableBreak(self, attacker);
}

static void garbage_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int, vec3_t)
{
	GarbageBreak(self, inflictor, attacker);
}

static void garbage_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	fx::Paper(self->s.origin, 3);
	ThrowAwaySpin(self, other, damage, PITCH);
}

static void SP_garbage_bag(edict_t *ent)
{
	SetMaterialFlag(21);
	SetMaterialFlag(22);
	G(ent, 48, -9, -10, -9, 9, 10, 10);
	ent->die = garbage_die;
	SetPain(ent, garbage_pain);
}

static void SP_heap(edict_t *ent)
{
	Force(ent, 2);
	ent->surfacetype = ent->s.skinnum == 1 ? 27 : ent->s.skinnum == 2 ? 13 : 3;
	G(ent, 57, -20, -15, -1, 16, 17, 2, SkinBy(ent, "heap", "heap2", "heap3"));
}

static void SP_light_beam(edict_t *ent)
{
	Force(ent, 3);
	G(ent, 58, -13, -312, -30, 50, 141, 5);
}

static void SP_light_flare(edict_t *ent)
{
	Force(ent, 3);
	G(ent, 59, -5, -5, -5, 5, 5, 5);
}

static const SpawnEntry s_spawns[] = {
	{"misc_generic_access_card", SP_access_card},
	{"misc_generic_banner", SP_banner},
	{"misc_generic_barbwire_coil", SP_barbwire_coil},
	{"misc_generic_barrel_burning", SP_barrel_burning},
	{"misc_generic_barrel_chemical", SP_barrel_chemical},
	{"misc_generic_barrel_wine", SP_barrel_wine},
	{"misc_generic_bench", SP_bench},
	{"misc_generic_blinds", SP_blinds},
	{"misc_generic_bookshelf", SP_bookshelf},
	{"misc_generic_broom", SP_broom},
	{"misc_generic_bunk", SP_bunk},
	{"misc_generic_chair_legs", SP_chair_legs},
	{"misc_generic_chair_legs2", SP_chair_legs2},
	{"misc_generic_chair_office", SP_chair_office},
	{"misc_generic_car_american", SP_car_american},
	{"misc_generic_car_european", SP_car_european},
	{"misc_generic_car_honda", SP_car_honda},
	{"misc_generic_car_rolls", SP_car_rolls},
	{"misc_generic_car_sports", SP_car_sports},
	{"misc_generic_cart", SP_cart},
	{"misc_generic_cash_register", SP_cash_register},
	{"misc_generic_chunks_brick", SP_chunks_brick},
	{"misc_generic_chunks_glass", SP_chunks_glass},
	{"misc_generic_chunks_metal", SP_chunks_metal},
	{"misc_generic_chunks_rock", SP_chunks_rock},
	{"misc_generic_chunks_wall", SP_chunks_wall},
	{"misc_generic_chunks_wood", SP_chunks_wood},
	{"misc_generic_comp_keyboard", SP_comp_keyboard},
	{"misc_generic_comp_monitor", SP_comp_monitor},
	{"misc_generic_comp_monitor2", SP_comp_monitor2},
	{"misc_generic_crate_fish", SP_crate_fish},
	{"misc_generic_desk_phone", SP_desk_phone},
	{"misc_generic_fan_big", SP_fan_big},
	{"misc_generic_faucet", SP_faucet},
	{"misc_generic_fire_extinguisher", SP_fire_extinguisher},
	{"misc_generic_fire_hydrant", SP_fire_hydrant},
	{"misc_generic_fish", SP_fish},
	{"misc_generic_fish_dead", SP_fish_dead},
	{"misc_generic_flag_pole", SP_flag_pole},
	{"misc_generic_forklift", SP_forklift},
	{"misc_generic_garbage_bag", SP_garbage_bag},
	{"misc_generic_heap", SP_heap},
	{"misc_generic_light_beam", SP_light_beam},
	{"misc_generic_light_flare", SP_light_flare},
	{NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
