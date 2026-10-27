// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — generic map objects, part 2 (misc_generic_motorcycle ... wpn_c4, without the
// subway train, which is in prop_train.cpp).
// Implements: SPEC-770 §6B.1-§6B.8

#include "props.h"
#include "../effects/fx.h"

namespace props {

static const vec3_t kZero = {0, 0, 0};

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

// skin on the entry's skin material, applied after the setup
static void SkinAfter(edict_t *ent, const char *skin)
{
	const ModelEntry *e = D(ent).entry;
	if (e && ent->ghoulInst)
		gg::SetSkinInst(ent->ghoulInst, e->dir, e->file, e->skinmat, skin);
}

static void Pain_Parts(edict_t *self, edict_t *other, float, int, const vec3_t point)
{
	ShootableParts(self, other, point);
}

static void RandDir(vec3_t d, int r, bool up)
{
	d[0] = (float)irand(-r, r);
	d[1] = (float)irand(-r, r);
	d[2] = up ? (float)irand(5, 10) : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.1 plain props

static void SP_palette_jack(edict_t *e) { G(e, 62, -29, -10, -20, 29, 10, 20); }
static void SP_paper_blowing(edict_t *e) { G(e, 63, -8, -8, -2, 8, 8, 2); }
static void SP_parking_meter(edict_t *e)
{
	Force(e, 2);
	G(e, 64, -1, -4, -21, 1, 4, 21);
}
static void SP_pillow(edict_t *e)
{
	if (e->s.skinnum == 1)
		e->surfacetype = 27;
	G(e, 66, -18, -30, -2, 18, 30, 2, SkinBy(e, "pillow", "pillow2"));
}
static void SP_plant_potted(edict_t *e) { G(e, 67, -33, -42, -18, 23, 27, 43, SkinBy(e, "plant", "plant2", "plant3", "plant4")); }
static void SP_plant_tall(edict_t *e) { G(e, 68, -33, -42, -18, 23, 27, 43); }
static void SP_radio(edict_t *e) { G(e, 71, -4, -14, -10, 4, 14, 9); }
static void SP_shovel(edict_t *e)
{
	Force(e, 2);
	G(e, 74, -5, -3, -24, 5, 3, 24, SkinBy(e, "shovel", "shovel2"));
}
static void SP_shrub(edict_t *e)
{
	Force(e, 2);
	G(e, 75, -20, -20, -13, 20, 20, 13, SkinBy(e, "shrub", "shrub2"));
}
static void SP_street_name_sign(edict_t *e)
{
	Force(e, 2);
	G(e, 81, -12, -12, -36, 12, 12, 36, SkinBy(e, "sign", "sign2"));
}
static void SP_sword(edict_t *e) { G(e, 82, -23, -2, -2, 23, 2, 2); }
static void SP_sword_large(edict_t *e)
{
	G(e, 83, -29, -2, -2, 29, 2, 2);
	gg::ScaleEnt(e, 1.3f);
}
static void SP_table_round(edict_t *e)
{
	if (e->s.skinnum == 1) {
		e->surfacetype = 14;
		e->material = 17;
	} else {
		e->surfacetype = 28;
		e->material = 11;
	}
	G(e, 84, -27, -23, -16, 27, 23, 16, SkinBy(e, "table", "table2"));
	SetBox(e, -27, -23, -16, 27, 23, 16); // written again after the setup: not rotated
	gi.linkentity(e);
}
static void SP_urinal_cake(edict_t *e) { G(e, 107, -3, -3, -1, 3, 3, 1); }
static void SP_urn(edict_t *e) { G(e, 108, -16, -16, -14, 16, 16, 14); }
static void SP_web_moss_animated(edict_t *e)
{
	Force(e, 3);
	G(e, 110, -4, -15, -10, -1, 15, 10, SkinBy(e, "web1", "web2", "moss1", "moss2", "bloodflap1"));
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.2 weapon display props

static void SP_wpn_bat(edict_t *e) { G(e, 112, -35, -33, 55, -31, 3, 60); }
static void SP_wpn_desert_eagle(edict_t *e) { G(e, 113, -42, -12, 55, -30, 1, 58); }
static void SP_wpn_glock(edict_t *e) { G(e, 114, -40, -10, 56, -31, 1, 58); }
static void SP_wpn_ingram(edict_t *e) { G(e, 115, -44, -13, 55, -27, 2, 59); }
static void SP_wpn_katana(edict_t *e) { G(e, 116, -37, -42, 54, -30, 9, 61); }
static void SP_wpn_minimi(edict_t *e) { G(e, 117, -7, -2, -1, 29, 7, 3); }
static void SP_wpn_sig3000(edict_t *e) { G(e, 118, -60, -21, 55, -26, 8, 58); }
static void SP_wpn_spas12(edict_t *e) { G(e, 119, -60, -6, 55, -30, 1, 58); }

static void c4_use(edict_t *self, edict_t *, edict_t *)
{
	if (self->spawnflags & 4)
		gg::AllParts(self->ghoulInst, true);
}

static void SP_wpn_c4(edict_t *e)
{
	G(e, 120, -4, -2, -4, 3, 2, 4);
	e->movetype = MOVETYPE_NONE;
	if (e->spawnflags & 4)
		gg::AllParts(e->ghoulInst, false);
	e->use = c4_use;
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.3 tipping and shoved props

static void motorcycle_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	gg::SetSkinInst(self->ghoulInst, "objects/generic/motorcycle", "motorcycle", "motorcycle", "damaged");
}

static void SP_motorcycle(edict_t *e)
{
	Force(e, 2);
	G(e, 60, -44, -17, -25, 44, 20, 23);
	SkinAfter(e, "motorcycle");
	SetPain(e, motorcycle_pain);
}

// tumble (L, f): f0 = L pitch, f1 = L roll, f2 = factor, i0 = health to 1 while turning, i1 = class end action
static void cone_end(edict_t *self)
{
	SetBox(self, -7, -7, -7, 7, 7, 7);
	self->solid = SOLID_BBOX;
	gi.linkentity(self);
}

static void tumble_think(edict_t *self)
{
	PropData &d = D(self);
	if (fabsf(self->s.angles[PITCH]) < d.f0 && fabsf(self->s.angles[ROLL]) < d.f1) {
		self->avelocity[PITCH] *= d.f2;
		self->avelocity[ROLL] *= d.f2;
		if (d.i0)
			self->health = 1;
		self->nextthink = level.time + FRAMETIME;
		return;
	}
	float lim[3] = {d.f0, 0, d.f1};
	for (int a = 0; a < 3; a += 2) {
		if (self->s.angles[a] > lim[a])
			self->s.angles[a] = lim[a];
		if (self->s.angles[a] < -lim[a])
			self->s.angles[a] = -lim[a];
	}
	VectorClear(self->avelocity);
	if (d.i1 == 1)
		cone_end(self);
}

static void StartTumble(edict_t *self, edict_t *other, float vz, float lp, float lr, float f, bool health1, int endact)
{
	vec3_t d;
	KnockRates(self, other, d);
	VectorSet(self->velocity, 0, 0, vz);
	PropData &pd = D(self);
	pd.f0 = lp;
	pd.f1 = lr;
	pd.f2 = f;
	pd.i0 = health1;
	pd.i1 = endact;
	self->think = tumble_think;
	self->nextthink = level.time + FRAMETIME;
}

static void cone_pain(edict_t *self, edict_t *other, float, int, const vec3_t)
{
	SetPain(self, NULL);
	SkinAfter(self, "damaged");
	StartTumble(self, other, 250, 100, 100, 1.5f, true, 1);
}

static void SP_orange_cone(edict_t *e)
{
	G(e, 61, -6, -6, -13, 6, 6, 13);
	SkinAfter(e, "orangecone");
	SetPain(e, cone_pain);
}

static void ashtray_pain_later(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	PropData &d = D(self);
	if (d.i2)
		fx::PointTE(fx::TE_RICOCHET, self->s.origin);
	int dd = (int)(d.f3 * damage);
	vec3_t dir;
	const float *src = other ? other->s.origin : self->s.origin;
	VectorSubtract(self->s.origin, src, dir);
	VectorNormalize(dir);
	VectorScale(dir, frand(10, 15) * dd, self->velocity);
	self->avelocity[YAW] = 200 * dir[1];
}

static void ashtray_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t)
{
	PropData &d = D(self);
	if (d.i2)
		fx::PointTE(fx::TE_RICOCHET, self->s.origin);
	ThrowAway(self, other, (int)(d.f2 * damage));
	KnockOver(self, other, damage, 6);
	SetPain(self, ashtray_pain_later);
}

static void SP_trash_ashtray(edict_t *e)
{
	MarkSoundList(34);
	MarkSoundList(35);
	MarkSoundList(36);
	G(e, 98, -5, -4, -9, 5, 4, 9);
	D(e).i2 = 1; // ricochet sound
	D(e).f2 = 0.03f;
	D(e).f3 = 0.3f;
	SetPain(e, ashtray_pain);
}

void GarbageBreak(edict_t *self, edict_t *inflictor, edict_t *attacker); // prop_generic1.cpp

static void garbage_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int, vec3_t)
{
	GarbageBreak(self, inflictor, attacker);
}

static void trashbarrel_pain(edict_t *self, edict_t *other, float, int, const vec3_t)
{
	SetPain(self, NULL);
	StartTumble(self, other, 120, 85, 85, 1.9f, false, 0);
}

static void SP_trashcan_barrel(edict_t *e)
{
	G(e, 99, -15, -16, -17, 15, 16, 22);
	e->die = garbage_die;
	SetPain(e, trashbarrel_pain);
}

static void SP_waste_basket_wire(edict_t *e)
{
	G(e, 109, -6, -7, -7, 6, 7, 7, SkinBy(e, "waste_basket", "waste_basket2", "waste_basket3", "waste_basket4"));
	D(e).f2 = 0.6f;
	D(e).f3 = 0.6f;
	SetPain(e, ashtray_pain);
}

static void wetfloor_pain(edict_t *self, edict_t *other, float, int, const vec3_t)
{
	SetPain(self, NULL);
	StartTumble(self, other, 160, 128, 90, 1.5f, true, 0);
}

static void SP_wet_floor_sign(edict_t *e)
{
	G(e, 111, -9, -8, -8, 9, 8, 8);
	SetPain(e, wetfloor_pain);
}

static void sofa_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t point)
{
	self->health = 10000;
	vec3_t d;
	const float *src = other ? other->s.origin : self->s.origin;
	VectorSubtract(self->s.origin, src, d);
	VectorNormalize(d);
	VectorMA(self->velocity, 2.0f * damage, d, self->velocity);
	self->velocity[2] = 20;
	vec3_t p;
	for (int i = 0; i < 3; i++)
		p[i] = point[i] + irand(-10, 10) * 0.1f;
	fx::PointTE(fx::TE_NOP, p);
}

static void SP_sofa(edict_t *e)
{
	G(e, 77, -17, -36, -17, 17, 36, 17, SkinBy(e, "sofa", "sofa2", "sofa3"));
	e->friction = 1;
	SetPain(e, sofa_pain);
}

static void SP_sofa_chair(edict_t *e)
{
	Force(e, 2);
	G(e, 78, -17, -16, -17, 17, 16, 17, SkinBy(e, "chair", "chair2", "chair2a"));
	e->friction = 1;
	SetPain(e, sofa_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.4 bathroom fixtures

static void spray_start(edict_t *self)
{
	vec3_t f, b;
	AngleVectors(self->s.angles, f, NULL, NULL);
	VectorNegate(f, b);
	fx::ExtDir(b);
	fx::EffectAt(fx::Index("environ/k_waterspout3"), self->s.origin);
	self->think = G_FreeEdict;
	self->nextthink = level.time + 3;
}

static edict_t *Spray(edict_t *fixture, bool toilet)
{
	edict_t *s = G_Spawn();
	s->classname = (char *)"water_spray";
	s->solid = SOLID_NOT;
	s->movetype = MOVETYPE_NONE;
	VectorCopy(fixture->s.origin, s->s.origin);
	VectorCopy(fixture->s.angles, s->s.angles);
	LoopSound(s, "Ambient/Gen/WtrFX/SprayBig.wav");
	if (toilet) {
		fx::EffectAt(fx::Index("environ/toiletspout"), fixture->s.origin);
		s->think = G_FreeEdict;
		s->nextthink = level.time + 3;
	} else {
		s->think = spray_start;
		s->nextthink = level.time + FRAMETIME;
	}
	gi.linkentity(s);
	return s;
}

static void brokensink_touch(edict_t *self, edict_t *, cplane_t *, csurface_t *)
{
	PlaySound(self, CHAN_VOICE, "Impact/Break/Ceramic.wav", 0.6f, ATTN_NORM);
	fx::Debris(self->s.origin, kZero, 8, 1, 0, 0, 0, 0, 0);
	G_FreeEdict(self);
}

static void sink_die(edict_t *self, edict_t *, edict_t *, int, vec3_t)
{
	Spray(self, false);
	edict_t *b = G_Spawn();
	b->classname = self->classname;
	vec3_t f;
	AngleVectors(self->s.angles, f, NULL, NULL);
	VectorMA(self->s.origin, 5, f, b->s.origin);
	VectorCopy(self->s.angles, b->s.angles);
	b->material = self->material;
	b->surfacetype = self->surfacetype;
	b->health = 200;
	b->spawnflags = 0;
	b->touch = brokensink_touch;
	ClearData(b);
	SetBox(b, -10, -11, -5, 10, 11, 6);
	Setup(b, T_GENERIC, 76, "damaged");
	b->s.effects |= 0x200;
	G_FreeEdict(self);
}

static void sink_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	SetPain(self, NULL);
	PlaySound(self, CHAN_VOICE, "Impact/Break/Ceramic.wav", 0.6f, ATTN_NORM);
	fx::Debris(self->s.origin, kZero, 5, 1, 0, 0, 0, 0, 0);
	SkinAfter(self, self->s.skinnum == 2 ? "damaged" : "sinkclean2d");
}

static void SP_sink(edict_t *e)
{
	Force(e, 2);
	Sound("Ambient/Gen/WtrFX/SprayBig.wav");
	Sound("Impact/Break/Ceramic.wav");
	fx::Index("environ/k_waterspout3");
	G(e, 76, -11, -12, -6, 11, 12, 7);
	SkinAfter(e, e->s.skinnum == 2 ? "sink" : "sinkclean");
	e->s.effects |= 0x200;
	e->die = sink_die;
	SetPain(e, sink_pain);
}

static void idle_think(edict_t *self) { self->nextthink = level.time + irand(0, 4) * 0.1f; }

static void toilet_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	SkinAfter(self, "damaged");
	self->health = 1;
	self->think = idle_think;
	self->nextthink = level.time + 0.5f;
}

static void toilet_die(edict_t *self, edict_t *, edict_t *, int, vec3_t)
{
	Spray(self, true);
	TableBreak(self, self);
}

static void SP_toilet(edict_t *e)
{
	Sound("Ambient/Gen/WtrFX/SprayBig.wav");
	fx::Index("environ/toiletspout");
	G(e, 96, -14, -9, -16, 14, 9, 16);
	SkinAfter(e, e->s.skinnum == 1 ? "toiletclean" : "toilet");
	e->movetype = MOVETYPE_NONE;
	e->die = toilet_die;
	SetPain(e, toilet_pain);
}

static void urinal_use(edict_t *self, edict_t *, edict_t *)
{
	PlaySound(self, CHAN_BODY, "Ambient/Models/Urinal/Flush.wav", 1, ATTN_NORM);
}

// f0 = pain timer
static void urinal_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	PropData &d = D(self);
	if (level.time < d.f0)
		return;
	SkinAfter(self, "damaged");
	self->think = idle_think;
	self->nextthink = level.time + 0.5f;
	d.f0 = level.time + frand(6, 12);
}

static void urinal_break(edict_t *self) { TableBreak(self, self); }

static void urinal_die(edict_t *self, edict_t *, edict_t *, int, vec3_t)
{
	Spray(self, false);
	self->think = urinal_break;
	self->nextthink = level.time + FRAMETIME;
}

static void SP_urinal(edict_t *e)
{
	Force(e, 2);
	Sound("Ambient/Models/Urinal/Flush.wav");
	Sound("Ambient/Gen/WtrFX/SprayBig.wav");
	fx::Index("environ/k_waterspout3");
	G(e, 106, -6, -9, -11, 9, 9, 14);
	SkinAfter(e, e->s.skinnum == 1 ? "urinal2" : "urinal");
	e->s.renderfx |= 1 << 28;
	e->use = urinal_use;
	e->plUse = urinal_use;
	e->die = urinal_die;
	SetPain(e, urinal_pain);
	D(e).f0 = -1;
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.5 electronics and signals

static void phonebooth_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	fx::PointTE(fx::TE_PHONEHIT, self->s.origin);
}

static void SP_phone_booth(edict_t *e)
{
	Force(e, 2);
	G(e, 65, -10, -27, -19, 10, 27, 19);
	SetPain(e, phonebooth_pain);
}

static void SP_radar_dish(edict_t *e)
{
	G(e, 69, -64, -64, -15, 64, 64, 70);
	gg::PauseEnt(e);
	gg::IGhoulInst *dish = Bolt(e, T_GENERIC, 69, "DISH_BOLT", 70, "DISH_BOLT");
	float rate[3] = {0, 1.5f, 0};
	int inst[4] = {dish ? dish->GetInstanceId() : 0, 0, 0, 0};
	fx::RotationSet(e, rate, inst);
}

// security camera: i0 counter, i1 n, f0 speed, f1 half-angle, f2 next alarm test
static void camera_alarmtest(edict_t *self)
{
	edict_t *p = G_Find(NULL, FOFS(classname), "player");
	if (!p)
		return;
	trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, p->s.origin, self, self->clipmask);
	if (tr.fraction < 1 && tr.ent != p)
		return;
	if (!gg::InFrontCone(self, p->s.origin, D(self).f1))
		return;
	PlaySound(self, CHAN_VOICE, "Ambient/Models/Camera/Alarm.wav", 0.6f, ATTN_NORM);
	G_UseTargets(self, p);
}

static void camera_fixed_think(edict_t *self)
{
	camera_alarmtest(self);
	self->nextthink = level.time + 1;
}

static void camera_pan_think(edict_t *self)
{
	PropData &d = D(self);
	self->s.angles[YAW] += d.f0;
	if (--d.i0 <= 0) {
		d.f0 = -d.f0;
		d.i0 = d.i1;
	}
	if ((self->spawnflags & 8) && level.time >= d.f2) {
		camera_alarmtest(self);
		d.f2 = level.time + 1;
	}
	self->nextthink = level.time + FRAMETIME;
}

static void CameraSetup(edict_t *e)
{
	PropData &d = D(e);
	d.f0 = e->speed ? e->speed : 5;
	int count = e->count ? e->count : 90;
	d.i1 = (int)(2 * count / d.f0);
	d.i0 = d.i1 / 2;
	d.f1 = e->attenuation ? e->attenuation : 30;
	if (e->spawnflags & 4) {
		LoopSound(e, "Ambient/Models/Camera/MoveLP.wav");
		e->think = camera_pan_think;
		e->nextthink = level.time + FRAMETIME;
		d.f2 = level.time + 1;
	} else if (e->spawnflags & 8) {
		e->think = camera_fixed_think;
		e->nextthink = level.time + 1;
	}
}

// a panning camera gets its loop sound before the precaches (black-box: a panning first camera registers MoveLP
// before Alarm, ger2; a fixed one Alarm before MoveLP, dm/raven)
static void CameraPanSound(edict_t *e)
{
	if (e->spawnflags & 4)
		Sound("Ambient/Models/Camera/MoveLP.wav");
}

static void SP_security_camera(edict_t *e)
{
	Force(e, 2);
	CameraPanSound(e);
	Sound("Ambient/Models/Camera/Alarm.wav");
	Sound("Ambient/Models/Camera/MoveLP.wav");
	G(e, 73, -8, -2, -4, 8, 2, 24, NULL, "camera");
	gg::IGhoulInst *cam = Bolt(e, T_GENERIC, 73, "DUMMY01", 72, "DUMMY01");
	gg::PlayInst(cam, "camera", 1);
	gg::ScaleEnt(e, 2);
	CameraSetup(e);
}

static void SP_security_camera2(edict_t *e)
{
	Force(e, 2);
	CameraPanSound(e);
	Sound("Ambient/Models/Camera/Alarm.wav");
	Sound("Ambient/Models/Camera/MoveLP.wav");
	G(e, 72, -8, -2, -4, 8, 2, 4, NULL, "camera");
	gg::PlayEnt(e, "camera", true);
	gg::ScaleEnt(e, 2);
	CameraSetup(e);
}

// speaker: e0 owner, i0 spark counter
static void speaker_sparks(edict_t *self)
{
	PropData &d = D(self);
	if (d.i0 <= 0)
		return;
	d.i0--;
	vec3_t f, r, p;
	AngleVectors(self->s.angles, f, r, NULL);
	VectorMA(self->s.origin, 14, f, p);
	VectorMA(p, 4, r, p);
	static const vec3_t up = {0, 0, 1};
	fx::Sparks(fx::TE_SPARKS_WHITE, p, up);
	RadiusDamage(self, d.e0, 20, NULL, 10, MOD_EXPLOSIVE, 0);
	self->nextthink = level.time + 0.1f + 0.1f * irand(0, 29);
}

static void speaker_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t)
{
	PropData &d = D(self);
	self->movetype = MOVETYPE_DAN;
	self->gravity = 1;
	vec3_t dd;
	const float *src = inflictor ? inflictor->s.origin : self->s.origin;
	VectorSubtract(self->s.origin, src, dd);
	VectorNormalize(dd);
	VectorMA(self->velocity, 5.0f * damage, dd, self->velocity);
	self->velocity[2] = 0;
	float yr = (float)(irand(0, 99) + 100 + damage);
	self->avelocity[YAW] = irand(0, 1) ? yr : -yr;
	fx::Sparks(fx::TE_SPARKS_WHITE, self->s.origin, dd);
	fx::Explosion(fx::TE_EXPLOSION_SMALL, self->s.origin, 10, 1);
	vec3_t f;
	AngleVectors(self->s.angles, f, NULL, NULL);
	VectorMA(self->s.origin, 10, f, self->s.origin);
	gg::PlayEnt(self, "speaker_down", false);
	self->takedamage = DAMAGE_NO;
	self->die = NULL;
	d.e0 = attacker;
	self->owner = attacker;
	if (!(self->spawnflags & 4))
		self->mins[2] = -5;
	gi.linkentity(self);
	d.i0 = irand(0, 9);
	if (d.i0 < 4)
		d.i0 = 4;
	self->think = speaker_sparks;
	speaker_sparks(self);
}

static void SP_speaker(edict_t *e)
{
	Force(e, 2);
	MarkSoundList(91);
	G(e, 79, -5, -5, -12, 5, 5, 12);
	if (e->spawnflags & 4) {
		gg::HidePart(e->ghoulInst, "_ARM01");
		e->mins[2] = -5;
		gi.linkentity(e);
	}
	e->die = speaker_die;
}

// stop light: i0 cycle index
static void stoplight_cycle(edict_t *self)
{
	static const char *seq[3] = {"blue", "red", "yellow"};
	PropData &d = D(self);
	gg::PlayEnt(self, seq[d.i0], false);
	d.i0 = (d.i0 + 1) % 3;
	self->nextthink = level.time + 5;
}

static void stoplight_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	gg::PlayEnt(self, "stoplight", false);
	SkinAfter(self, "stoplight_d");
	self->think = NULL;
	self->nextthink = 0;
}

static void SP_stoplight(edict_t *e)
{
	Force(e, 2);
	G(e, 80, -6, -5, -44, 9, 4, 44, e->s.skinnum ? "stoplight_d" : NULL);
	if (!e->s.skinnum) {
		gg::PlayEnt(e, "yellow", false);
		e->think = stoplight_cycle;
		e->nextthink = level.time + 5;
		SetPain(e, stoplight_pain);
	}
}

// televisions: i0 = broken, i1 = hanging, i2 = dropped
static void TVSparks3(const vec3_t at)
{
	vec3_t d;
	RandDir(d, 10, false);
	fx::Sparks(fx::TE_SPARKS_ORANGE, at, d);
	RandDir(d, 10, false);
	fx::Sparks(fx::TE_SPARKS_ORANGE, at, d);
	RandDir(d, 20, true);
	fx::Sparks(fx::TE_SPARKS_ORANGE, at, d);
}

static void TVSparks2(const vec3_t at)
{
	vec3_t d;
	RandDir(d, 10, false);
	fx::Sparks(fx::TE_SPARKS_ORANGE, at, d);
	RandDir(d, 10, false);
	fx::Sparks(fx::TE_SPARKS_ORANGE, at, d);
}

static void TVBreakBurst(edict_t *self, const vec3_t q)
{
	TVSparks3(q);
	fx::Debris(q, kZero, 3, 1, 9, 0, 0, 0, 28);
	fx::Debris(q, kZero, 5, 1, 7, 0, 0, 0, 37);
	fx::Explosion(fx::TE_EXPLOSION_SMALL, q, 100, 0);
	PlaySound(self, CHAN_VOICE, "Impact/Glassbreak/Monitor.wav", 0.6f, ATTN_NORM);
	LoopSound(self, "Ambient/Gen/Electric/buzz.wav");
}

void TVDeath(edict_t *self, edict_t *attacker)
{
	TVSparks3(self->s.origin);
	fx::Debris(self->s.origin, kZero, 5, 1, 7, 0, 0, 0, 37);
	fx::Explosion(fx::TE_EXPLOSION_MEDIUM, self->s.origin, 50, 1);
	StandardBreak(self, attacker, 0, 0);
}

static void tv_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t) { TVDeath(self, attacker); }

static void TVPrecache(void)
{
	MarkSoundList(90);
	MarkSoundList(91);
	Sound("Impact/Glassbreak/Monitor.wav");
	Sound("Ambient/Gen/Electric/buzz.wav");
}

static void tvwall_brokenthink(edict_t *self) { self->nextthink = level.time + FRAMETIME; }

static void tvwall_break(edict_t *self)
{
	PropData &d = D(self);
	self->movetype = MOVETYPE_DAN;
	Toggle(self, 2);
	gg::IGhoulInst *in = self->ghoulInst;
	if (self->style == 0) {
		gg::HidePart(in, "FACE");
		gg::HidePart(in, "WORLD_OMNI");
	} else {
		gg::HidePart(in, "FACE2");
		gg::HidePart(in, "FACE2B");
		gg::HidePart(in, "FACE2C");
		gg::HidePart(in, "FACE2D");
		StopLoop(self);
	}
	SkinAfter(self, "tv_wall_d");
	TVBreakBurst(self, self->s.origin);
	d.i0 = 1;
	self->think = tvwall_brokenthink;
	self->nextthink = level.time + FRAMETIME;
}

static void tvwall_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	if (D(self).i0)
		TVSparks2(self->s.origin);
	else
		tvwall_break(self);
}

static void tvwall_use(edict_t *self, edict_t *, edict_t *) { tvwall_break(self); }

static void tvwall_idle(edict_t *self) { gg::PlayEnt(self, "tv_wall", true); }

static void SP_tv_wall(edict_t *e)
{
	Force(e, 2);
	TVPrecache();
	fx::Index("environ/normalsmoke");
	SetMaterialFlag(1);
	SetMaterialFlag(4);
	G(e, 105, -5, -9, -9, 6, 9, 7);
	SkinAfter(e, "tv_wall");
	gg::ScaleEnt(e, 1.5f);
	if (e->style == 0) {
		gg::HidePart(e->ghoulInst, "FACE2");
		gg::HidePart(e->ghoulInst, "FACE2B");
		gg::HidePart(e->ghoulInst, "FACE2C");
		gg::HidePart(e->ghoulInst, "FACE2D");
	} else {
		gg::HidePart(e->ghoulInst, "FACE");
		gg::HidePart(e->ghoulInst, "WORLD_OMNI");
		LoopSound(e, "ambient/models/Panel/Panel1.wav");
	}
	e->think = tvwall_idle;
	e->nextthink = level.time + irand(0, 4) * 0.1f;
	e->die = tv_die;
	e->use = tvwall_use;
	SetPain(e, tvwall_pain);
}

static void tvceil_brokenthink(edict_t *self)
{
	PropData &d = D(self);
	gg::IGhoulInst *scr = gg::TrackedPart(self, 1);
	if (!d.i1 && !d.i2) {
		vec3_t pos, dir;
		gg::BoltWorld(self, scr, "face", pos, dir);
		if (-dir[2] < 0.9f)
			gg::RotateInst(scr, 2, 0.3f);
		else
			d.i1 = 1;
	}
	self->nextthink = level.time + FRAMETIME;
}

static void tvceil_break(edict_t *self)
{
	PropData &d = D(self);
	gg::IGhoulInst *scr = gg::TrackedPart(self, 1);
	gg::HidePart(scr, "FACE");
	gg::HidePart(scr, "WORLD_LIGHT");
	vec3_t f, q;
	AngleVectors(self->s.angles, f, NULL, NULL);
	VectorMA(self->s.origin, 10, f, q);
	TVBreakBurst(self, q);
	d.i0 = 1;
	self->think = tvceil_brokenthink;
	self->nextthink = level.time + FRAMETIME;
}

static void tvceil_touch(edict_t *self, edict_t *, cplane_t *, csurface_t *)
{
	vec3_t d;
	RandDir(d, 10, false);
	fx::Sparks(fx::TE_SPARKS_ORANGE, self->s.origin, d);
	fx::Debris(self->s.origin, kZero, 3, 1, 9, 0, 0, 0, 32);
	fx::Debris(self->s.origin, kZero, 5, 1, 7, 0, 0, 0, 38);
	fx::Explosion(fx::TE_EXPLOSION_SMALL, self->s.origin, 200, 1);
	gg::SetInstRotation(gg::TrackedPart(self, 1), 2, 1.58f);
	self->touch = NULL;
	Force(self, 2);
}

static void tvceil_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	PropData &d = D(self);
	if (!d.i0) {
		tvceil_break(self);
		return;
	}
	TVSparks2(self->s.origin);
	if (d.i1) {
		gg::HidePart(self->ghoulInst, "support1");
		gg::HidePart(self->ghoulInst, "support2");
		Toggle(self, 2);
		self->movetype = MOVETYPE_DAN;
		fx::Debris(self->s.origin, kZero, 3, 1, 9, 0, 0, 0, 28);
		d.i1 = 0;
		d.i2 = 1;
		self->mins[2] += 9;
		gi.linkentity(self);
		self->touch = tvceil_touch;
	}
}

static void tvceil_use(edict_t *self, edict_t *, edict_t *) { tvceil_break(self); }

static void tvceil_idle(edict_t *self)
{
	gg::PlayInst(gg::TrackedPart(self, 1), "tv", 1);
	LoopSound(self, "Ambient/Models/Monitor/Static.wav");
}

static void SP_tv_ceiling(edict_t *e)
{
	Force(e, 2);
	TVPrecache();
	Sound("Ambient/Models/Monitor/Static.wav");
	fx::Index("environ/normalsmoke");
	SetMaterialFlag(1);
	SetMaterialFlag(4);
	G(e, 104, -18, -19, -22, 19, 19, 28);
	gg::IGhoulInst *scr = Bolt(e, T_GENERIC, 104, "DUMMY01", 103, "DUMMY01");
	gg::ScaleEnt(e, 1.5f);
	if (e->style == 1)
		gg::SetSkinInst(scr, "objects/generic/tv_ceiling", "tv", "face", "tv_br0");
	e->think = tvceil_idle;
	e->nextthink = level.time + 0.1f + 0.1f * irand(0, 4);
	e->die = tv_die;
	e->use = tvceil_use;
	SetPain(e, tvceil_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.6 tables

static bool Gone(edict_t *self, int i) { return !gg::PartShown(self, i); }

static void TiltedBox1(edict_t *self)
{
	SetBox(self, -41, -24, -7, 41, 24, 16);
	gi.linkentity(self);
}

static void table1_react(edict_t *self, int part, edict_t *, const vec3_t)
{
	PropData &d = D(self);
	int legs = !Gone(self, 1) + !Gone(self, 2) + !Gone(self, 3) + !Gone(self, 4);
	if (legs >= 3)
		return;
	vec3_t at;
	PartPos(self, part, at);
	PartDebris(self, at);
	float p0 = d.start_angles[PITCH], r0 = d.start_angles[ROLL];
	float &pitch = self->s.angles[PITCH], &roll = self->s.angles[ROLL];
	if (legs == 0 && Gone(self, 5)) {
		pitch = p0;
		roll = r0;
		SetBox(self, -41, -24, 10, 41, 24, 15);
		ClearFlags(self, 2);
		gi.linkentity(self);
		return;
	}
	bool L1 = !Gone(self, 1), L2 = !Gone(self, 2), L3 = !Gone(self, 3), L4 = !Gone(self, 4), B = !Gone(self, 5);
	switch (part) {
	case 1:
		if (!L2) {
			roll += 30;
			pitch = p0;
			TiltedBox1(self);
		} else if (!L3 && !B && roll == r0) {
			pitch -= 20;
			TiltedBox1(self);
		}
		break;
	case 2:
		if (!L1) {
			roll += 30;
			pitch = p0;
			TiltedBox1(self);
		} else if (!L4 && !B && roll == r0) {
			pitch += 20;
			TiltedBox1(self);
		}
		break;
	case 3:
		if (!L1 && !B) {
			if (roll == r0) {
				pitch -= 20;
				TiltedBox1(self);
			}
		} else if (L1 && !L4 && !B) {
			roll -= 30;
			pitch = p0;
			TiltedBox1(self);
		}
		break;
	case 4:
		if (B)
			break;
		if (L2) {
			if (!L3) {
				roll -= 30;
				pitch = p0;
				TiltedBox1(self);
			}
		} else if (roll == r0) {
			pitch += 20;
			TiltedBox1(self);
		}
		break;
	case 5:
		if (!L1 && !L3) {
			if (roll == r0) {
				pitch -= 20;
				TiltedBox1(self);
			}
		} else if (!L2 && !L4) {
			if (roll == r0) {
				pitch += 20;
				TiltedBox1(self);
			}
		} else if (!L3 && !L4) {
			roll -= 30;
			pitch = p0;
			TiltedBox1(self);
		}
		break;
	}
}

static void table2_react(edict_t *self, int part, edict_t *, const vec3_t)
{
	PropData &d = D(self);
	vec3_t at;
	PartPos(self, part, at);
	PartDebris(self, at);
	float &pitch = self->s.angles[PITCH], &roll = self->s.angles[ROLL];
	bool P1 = !Gone(self, 1), P2 = !Gone(self, 2), P3 = !Gone(self, 3), P4 = !Gone(self, 4);
	if (!P1 && !P2 && !P3 && !P4) {
		pitch = d.start_angles[PITCH];
		roll = d.start_angles[ROLL];
		SetBox(self, -64, -106, -1, 47, 32, 4);
		gi.linkentity(self);
		return;
	}
	gg::IGhoulInst *T = gg::TrackedPart(self, 5);
	switch (part) {
	case 1:
		if (!P2) {
			if (!P3 && P4)
				pitch += 18;
		} else if (!P3 && P4) {
			gg::RotateInst(T, 1, -0.18850f);
			pitch += 8;
		}
		break;
	case 2:
		if (P1 && !P3 && P4) {
			pitch -= 12;
			roll -= 12;
			SetBox(self, -64, -106, -16, 47, 32, 4);
			gi.linkentity(self);
		}
		break;
	case 3:
		if (!P1) {
			if (P2 && P4) {
				gg::RotateInst(T, 1, -0.18850f);
				pitch += 8;
			}
		} else if (!P2 && P4) {
			pitch -= 12;
			roll -= 12;
			SetBox(self, -64, -106, -16, 47, 32, 4);
			gi.linkentity(self);
		}
		break;
	case 4:
		if (!P2 && P1 && P3)
			roll += 18;
		break;
	}
}

static const PartRow kTable1Parts[] = {{"DUMMY01", 1, table1_react}, {"DUMMY02", 2, table1_react},
                                       {"DUMMY03", 3, table1_react}, {"DUMMY04", 4, table1_react},
                                       {"DUMMY05", 5, table1_react}};
static const PartRow kTable2Parts[] = {{"DUMMY08", 1, table2_react}, {"DUMMY09", 2, table2_react},
                                       {"DUMMY10", 3, table2_react}, {"DUMMY07", 4, table2_react}};

static void table_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || other == g_edicts || other->client || (other->svflags & SVF_MONSTER))
		return;
	LinkObject(self, other);
}

static void table1_use(edict_t *self, edict_t *other, edict_t *) { FallOverStart(self, other); }

static void SP_table1(edict_t *e)
{
	const char *skin = "table1";
	switch (e->s.skinnum) {
	case 1: skin = "table1A"; e->surfacetype = 28; e->material = 11; break;
	case 2: skin = "table1B"; e->surfacetype = 14; e->material = 17; break;
	case 3: skin = "table1C"; e->surfacetype = 28; e->material = 11; break;
	default: e->surfacetype = 14; e->material = 17; break;
	}
	e->touch = table_touch;
	G(e, 85, -41, -24, -18, 41, 24, 10, skin);
	SetBox(e, -41, -24, -18, 41, 24, 10); // set after the setup: not rotated
	gi.linkentity(e);
	VectorCopy(e->s.angles, D(e).start_angles);
	for (int i = 1; i <= 4; i++)
		Bolt(e, T_GENERIC, 85, va("DUMMY0%d", i), 87, "DUMMY01", skin);
	Bolt(e, T_GENERIC, 85, "DUMMY05", 86, "DUMMY05", skin);
	D(e).parts = kTable1Parts;
	D(e).nparts = 5;
	SetPain(e, Pain_Parts);
	if (e->spawnflags & 4)
		e->plUse = table1_use;
}

static void SP_table2(edict_t *e)
{
	if (st.scale == 0)
		st.scale = 0.8f;
	else if (st.scale > 0.2f)
		st.scale -= 0.2f;
	e->touch = table_touch;
	G(e, 88, -56, -98, -26, 39, 24, 4);
	VectorCopy(e->s.angles, D(e).start_angles);
	Bolt(e, T_GENERIC, 88, "DUMMY08", 89, "DUMMY08");
	Bolt(e, T_GENERIC, 88, "DUMMY09", 90, "DUMMY09");
	Bolt(e, T_GENERIC, 88, "DUMMY10", 91, "DUMMY10");
	gg::IGhoulInst *p4 = Bolt(e, T_GENERIC, 88, "DUMMY07", 89, "DUMMY08");
	gg::RotateInst(p4, 0, -(float)M_PI / 2);
	Bolt(e, T_GENERIC, 88, "DUMMY12", 92, "DUMMY12");
	D(e).parts = kTable2Parts;
	D(e).nparts = 4;
	SetPain(e, Pain_Parts);
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.7 gas tanks

static void gastank_explode(edict_t *self, edict_t *attacker)
{
	SetPain(self, NULL);
	self->die = NULL;
	self->use = NULL;
	Explode(self, true, true);
	StandardBreak(self, attacker, 0, 3);
}

static void gastank_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t) { gastank_explode(self, attacker); }
static void gastank_use(edict_t *self, edict_t *, edict_t *activator) { gastank_explode(self, activator); }

static void SP_tank_gas(edict_t *e)
{
	MarkSoundList(58);
	fx::Index("weapons/world/airexplode");
	fx::Index("environ/onfireburst");
	G(e, 94, -58, -23, -27, 58, 23, 19);
	e->die = gastank_die;
	e->use = gastank_use;
}

static void propane_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	SetPain(self, NULL);
	self->die = NULL;
	Explode(self, true, true);
	if (self->spawnflags)
		G_FreeEdict(self);
	else
		StandardBreak(self, attacker, 0, 0);
}

static void SP_tank_propane(edict_t *e)
{
	MarkSoundList(58);
	fx::Index("environ/onfireburst");
	G(e, 95, -3, -3, -25, 3, 3, 25);
	e->die = propane_die;
}

// ---------------------------------------------------------------------------------------------------------------
// 6B.8 shattering props

static void shatter_die(edict_t *self, edict_t *, edict_t *, int, vec3_t)
{
	self->die = NULL;
	self->takedamage = DAMAGE_NO;
	self->s.effects |= 0x400;
	self->think = G_FreeEdict;
	self->nextthink = level.time + 0.2f;
}

static void SP_tree_dead(edict_t *e)
{
	Force(e, 2);
	G(e, 100, -43, -43, -41, 40, 67, 110);
	e->die = shatter_die;
}
static void SP_tree_thin(edict_t *e)
{
	Force(e, 2);
	G(e, 101, -26, -15, -66, 25, 16, 96);
	e->die = shatter_die;
}
static void SP_truckcab(edict_t *e)
{
	Force(e, 2);
	G(e, 102, -91, -40, -49, 91, 40, 49);
	e->die = shatter_die;
}

static const SpawnEntry s_spawns[] = {
	{"misc_generic_motorcycle", SP_motorcycle},
	{"misc_generic_orange_cone", SP_orange_cone},
	{"misc_generic_palette_jack", SP_palette_jack},
	{"misc_generic_paper_blowing", SP_paper_blowing},
	{"misc_generic_parking_meter", SP_parking_meter},
	{"misc_generic_phone_booth", SP_phone_booth},
	{"misc_generic_pillow", SP_pillow},
	{"misc_generic_plant_potted", SP_plant_potted},
	{"misc_generic_plant_tall", SP_plant_tall},
	{"misc_generic_radar_dish", SP_radar_dish},
	{"misc_generic_radio", SP_radio},
	{"misc_generic_security_camera", SP_security_camera},
	{"misc_generic_security_camera2", SP_security_camera2},
	{"misc_generic_shovel", SP_shovel},
	{"misc_generic_shrub", SP_shrub},
	{"misc_generic_sink", SP_sink},
	{"misc_generic_sofa", SP_sofa},
	{"misc_generic_sofa_chair", SP_sofa_chair},
	{"misc_generic_speaker", SP_speaker},
	{"misc_generic_stoplight", SP_stoplight},
	{"misc_generic_street_name_sign", SP_street_name_sign},
	{"misc_generic_sword", SP_sword},
	{"misc_generic_sword_large", SP_sword_large},
	{"misc_generic_table1", SP_table1},
	{"misc_generic_table2", SP_table2},
	{"misc_generic_table_round", SP_table_round},
	{"misc_generic_tank_gas", SP_tank_gas},
	{"misc_generic_tank_propane", SP_tank_propane},
	{"misc_generic_toilet", SP_toilet},
	{"misc_generic_trash_ashtray", SP_trash_ashtray},
	{"misc_generic_trashcan_barrel", SP_trashcan_barrel},
	{"misc_generic_tree_dead", SP_tree_dead},
	{"misc_generic_tree_thin", SP_tree_thin},
	{"misc_generic_truckcab", SP_truckcab},
	{"misc_generic_tv_ceiling", SP_tv_ceiling},
	{"misc_generic_tv_wall", SP_tv_wall},
	{"misc_generic_urinal", SP_urinal},
	{"misc_generic_urinal_cake", SP_urinal_cake},
	{"misc_generic_urn", SP_urn},
	{"misc_generic_waste_basket_wire", SP_waste_basket_wire},
	{"misc_generic_web_moss_animated", SP_web_moss_animated},
	{"misc_generic_wet_floor_sign", SP_wet_floor_sign},
	{"misc_generic_wpn_bat", SP_wpn_bat},
	{"misc_generic_wpn_c4", SP_wpn_c4},
	{"misc_generic_wpn_desert_eagle", SP_wpn_desert_eagle},
	{"misc_generic_wpn_glock", SP_wpn_glock},
	{"misc_generic_wpn_ingram", SP_wpn_ingram},
	{"misc_generic_wpn_katana", SP_wpn_katana},
	{"misc_generic_wpn_minimi", SP_wpn_minimi},
	{"misc_generic_wpn_sig3000", SP_wpn_sig3000},
	{"misc_generic_wpn_spas12", SP_wpn_spas12},
	{NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
