// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — level prop families: Kosovo/Bosnia, castle, Iraq; driving vehicles.
// Implements: SPEC-770 §7A

#include "props.h"
#include "../effects/fx.h"

namespace props {

static const vec3_t kZero = {0, 0, 0};

static void T(edict_t *ent, int table, int idx, float x0, float y0, float z0, float x1, float y1, float z1,
              const char *skin = NULL, const char *only = NULL)
{
	SetBox(ent, x0, y0, z0, x1, y1, z1);
	Setup(ent, table, idx, skin, only);
}

static void Pain_Parts(edict_t *self, edict_t *other, float, int, const vec3_t point)
{
	ShootableParts(self, other, point);
}

static bool Shown(edict_t *self, int i) { return gg::PartShown(self, i); }

// ---------------------------------------------------------------------------------------------------------------
// Kosovo / Bosnia

static void jack_hurt(edict_t *self, edict_t *other)
{
	vec3_t dir, p;
	VectorSubtract(other->s.origin, self->s.origin, dir);
	VectorSet(p, other->s.origin[0], other->s.origin[1], other->s.origin[2] + other->mins[2]);
	Damage(other, self, self, dir, p, self->s.origin, 1, 3, 0x400, MOD_EXPLOSIVE);
}

static void apjack_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || !other->takedamage)
		return;
	if (other->s.origin[2] + other->mins[2] > self->s.origin[2] + self->maxs[2] - 2)
		jack_hurt(self, other);
}

static void tankjack_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || !other->client)
		return;
	const short *v = other->client->ps.pmove.velocity;
	float v2 = (float)v[0] * v[0] + (float)v[1] * v[1] + (float)v[2] * v[2];
	if (v2 > 250000)
		jack_hurt(self, other);
}

static void SP_antipersonnel_jack(edict_t *e)
{
	e->touch = apjack_touch;
	T(e, T_BOSNIA, 0, -15, -17, -10, 15, 13, 16);
}

static void SP_flag_wall(edict_t *e) { T(e, T_BOSNIA, 1, -45, -18, -25, -39, 42, 77); }

static void landmine_explode(edict_t *self, edict_t *who)
{
	self->touch = NULL;
	self->die = NULL;
	Explode(self, true, true);
	fx::FloorDecal(1, self->s.origin, 0);
	StandardBreak(self, who, 0, 0);
}

static void landmine_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *) { landmine_explode(self, other); }
static void landmine_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t) { landmine_explode(self, attacker); }

static void SP_landmine(edict_t *e)
{
	fx::Index("environ/onfireburst");
	e->touch = landmine_touch;
	T(e, T_BOSNIA, 2, -13, -13, -2, 13, 13, 2);
	e->die = landmine_die;
}

static void SP_rubble1(edict_t *e) { T(e, T_BOSNIA, 3, -13, -10, -5, 13, 10, 5); }
static void SP_rubble2(edict_t *e)
{
	Force(e, 2);
	T(e, T_BOSNIA, 4, -31, -30, -9, 45, 22, 36);
}

static void searchbeam_check(edict_t *) {} // finds no attached parts, never thinks again

static void SP_searchbeam(edict_t *e)
{
	Force(e, 2);
	T(e, T_BOSNIA, 6, -8, -8, -8, 8, 8, 8);
	gg::PlayEnt(e, "beam", true);
	e->think = searchbeam_check;
	e->nextthink = level.time + FRAMETIME;
}

// search light: inst0 arm, inst1 lamp, i0 max health, i1 broken
static void searchlight_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	PropData &d = D(self);
	vec3_t p;
	VectorCopy(self->s.origin, p);
	p[2] += 80;
	if (d.i1) {
		fx::Explosion(fx::TE_EXPLOSION_SMALL, p, 100, 0);
		return;
	}
	if (self->health > (int)(0.85f * d.i0) || !d.inst1)
		return;
	d.i1 = 1;
	gg::IGhoulObject *o = d.inst1->GetGhoulObject();
	ghoul::GhoulID mat = o ? o->FindMaterial("glass") : 0;
	ghoul::GhoulID sk = mat ? o->FindSkin(mat, "bkglass", 0) : 0;
	if (mat && sk)
		d.inst1->SetSkin(mat, sk, 0);
	gg::HidePart(d.inst1, "_BEAM");
	gg::HidePart(d.inst1, "WORLD_OMNI");
	fx::Explosion(fx::TE_EXPLOSION_SMALL, p, 100, 0);
	fx::SlotAdd(self, fx::Index("environ/tv_smoke"), 0, 0);
}

static void SP_searchlight(edict_t *e)
{
	fx::Index("environ/tv_smoke");
	T(e, T_BOSNIA, 7, -40, -40, -8, 40, 40, 100);
	e->touch = NULL; // no player push
	PropData &d = D(e);
	d.inst0 = Bolt(e, T_BOSNIA, 7, "DUMMY01", 8, "DUMMY01");
	if (d.inst0)
		d.inst1 = gg::AddTrackedBoltOnBolt(e, d.inst0, Ref(Entry(T_BOSNIA, 8)), "DUMMY02",
		                                   Ref(Entry(T_BOSNIA, 9)), "DUMMY02", NULL);
	if (e->spawnflags & (4 | 8))
		gg::PlayInst(d.inst0, "arm", 1);
	d.i0 = e->health;
	SetPain(e, searchlight_pain);
}

static void SP_tank_jack(edict_t *e)
{
	e->touch = tankjack_touch;
	T(e, T_BOSNIA, 10, -28, -28, -21, 28, 28, 21);
}

static void SP_truck_chunks(edict_t *e)
{
	static const char *skins[3] = {"chunks", "chunks_red", "chunks_tan"};
	const char *part = e->style == 1 ? "_DOOR" : e->style == 2 ? "_PANEL" : e->style == 3 ? "_TIRE" : "_GRILL";
	int k = (e->s.skinnum >= 0 && e->s.skinnum <= 2) ? e->s.skinnum : 0;
	T(e, T_BOSNIA, 11, -15, -18, -27, 15, 18, 27, skins[k], part);
}

// ---------------------------------------------------------------------------------------------------------------
// driving vehicles: v0 = destination D, f0 = speed, f1 = accel, i0 = state, i1 = first tire part index,
// i2 = 1 truck_old / 2 flatbed

enum { DV_ACCEL = 1, DV_CRUISE, DV_BRAKE, DV_STOP };

static float HLen(const vec3_t v) { return sqrtf(v[0] * v[0] + v[1] * v[1]); }

static void WheelRecord(edict_t *self, float w, int tires_first, int ntires)
{
	PropData &d = D(self);
	if (fx::RotationActive(self)) {
		fx::RotationSetValue(self, 1, (int)(512.0f * w));
		return;
	}
	float rate[3] = {w, 0, 0};
	int inst[4] = {0, 0, 0, 0};
	for (int k = 0; k < ntires && k < 4; k++) {
		gg::IGhoulInst *t = gg::TrackedPart(self, tires_first + k);
		inst[k] = t ? t->GetInstanceId() : 0;
	}
	fx::RotationSet(self, rate, inst);
}

static void WheelUpdate(edict_t *self)
{
	PropData &d = D(self);
	if (d.i2 == 2) {
		WheelRecord(self, d.f0 * (float)M_PI / 94, d.i1, 4);
		return;
	}
	// truck_old: the record is rebuilt once per tire, so only the tire on DUMMY04 ends up in it (original quirk)
	float w = VectorLength(self->velocity) * (float)M_PI / 94;
	float rate[3] = {w, 0, 0};
	gg::IGhoulInst *t = gg::TrackedPart(self, d.i1 + 3);
	int inst[4] = {t ? t->GetInstanceId() : 0, 0, 0, 0};
	fx::RotationSet(self, rate, inst);
}

static void Approach(edict_t *self)
{
	PropData &d = D(self);
	vec3_t t;
	VectorSubtract(d.v0, self->s.origin, t);
	float dist = HLen(t);
	float s = 0.1f * HLen(self->velocity);
	bool moving = self->velocity[0] || self->velocity[1] || self->velocity[2];
	if (moving && dist < 3 * s) {
		float k = dist / (4 * s);
		self->velocity[0] *= k;
		self->velocity[1] *= k;
		self->velocity[2] = 0;
		d.i0 = DV_BRAKE;
	}
}

static void drive_think(edict_t *self)
{
	PropData &d = D(self);
	Riders(self);
	self->nextthink = level.time + FRAMETIME;
	float yaw = self->s.angles[YAW] * (float)(M_PI / 180.0);
	vec3_t m = {cosf(yaw), -sinf(yaw), 0};
	switch (d.i0) {
	case DV_ACCEL:
		if (self->velocity[0] * self->velocity[0] + self->velocity[1] * self->velocity[1] >= d.f0 * d.f0)
			d.i0 = DV_CRUISE;
		else {
			self->velocity[0] += d.f1 * m[0];
			self->velocity[1] += d.f1 * m[1];
		}
		Approach(self);
		WheelUpdate(self);
		break;
	case DV_CRUISE:
		Approach(self);
		WheelUpdate(self);
		break;
	case DV_BRAKE: {
		vec3_t t;
		VectorSubtract(d.v0, self->s.origin, t);
		float dist = HLen(t), V = HLen(self->velocity), s = 0.1f * V;
		float k = V < 5 ? (s > 0 ? dist / s : 0) : dist / (4 * s);
		self->velocity[0] *= k;
		self->velocity[1] *= k;
		self->velocity[2] = 0;
		if (V < 5)
			d.i0 = DV_STOP;
		break;
	}
	default:
		VectorClear(self->velocity);
		self->movetype = MOVETYPE_NONE;
		StopLoop(self);
		self->think = NULL;
		self->nextthink = 0;
		break;
	}
}

static void drive_use(edict_t *self, edict_t *, edict_t *)
{
	PropData &d = D(self);
	Riders(self);
	self->movetype = MOVETYPE_DAN;
	self->friction = 0;
	self->gravity = 0;
	float yaw = self->s.angles[YAW] * (float)(M_PI / 180.0);
	VectorSet(self->velocity, d.f1 * cosf(yaw), -d.f1 * sinf(yaw), 0);
	self->use = NULL;
	d.i0 = DV_ACCEL;
	Approach(self);
	self->think = drive_think;
	self->nextthink = level.time + FRAMETIME;
}

static void spin_use(edict_t *self, edict_t *, edict_t *)
{
	PropData &d = D(self);
	if (fx::RotationActive(self))
		fx::RotationClear(self);
	else
		WheelRecord(self, d.f0 * (float)M_PI / 94, d.i1, 4);
}

static void truck_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *)
{
	if (!plane || !other)
		return;
	vec3_t d;
	VectorSubtract(self->s.origin, other->s.origin, d);
	VectorNormalize(d);
	if (plane->normal[0] * d[0] + plane->normal[1] * d[1] <= 0.9f)
		return;
	VectorMA(other->velocity, 15, self->velocity, other->velocity);
	other->velocity[2] += 25;
	if (other->health > 10)
		other->health -= 5;
}

static void vehicle_smokeoff(edict_t *self) { fx::SlotRemove(self, fx::Index("environ/normalsmoke")); }

static void vehicle_die(edict_t *self, edict_t *, edict_t *, int, vec3_t)
{
	ghoul::GhoulID engine = gg::PartId(self->ghoulInst, "ENGINE");
	if (!engine)
		return;
	fx::SlotAdd(self, fx::Index("environ/normalsmoke"), engine, 0);
	gi.positioned_sound(self->s.origin, g_edicts, CHAN_AUTO, gi.soundindex("impact/explosion/med.wav"), 1,
	                    ATTN_NORM, 0);
	self->die = NULL;
	if (D(self).i2 == 2 && (self->spawnflags & 8)) {
		gg::HideAllRecursive(gg::TrackedPart(self, 1));
		gg::HideAllRecursive(gg::TrackedPart(self, 2));
		gg::HidePart(self->ghoulInst, "HEADLIGHT1");
		gg::HidePart(self->ghoulInst, "HEADLIGHT2");
	}
	self->think = vehicle_smokeoff;
	self->nextthink = level.time + 5;
}

static void DriveSetup(edict_t *e)
{
	PropData &d = D(e);
	d.f0 = e->speed ? e->speed : 200;
	d.f1 = 30;
	float yaw = e->s.angles[YAW] * (float)(M_PI / 180.0);
	VectorCopy(e->s.origin, d.v0);
	d.v0[0] += st.distance * cosf(yaw);
	d.v0[1] += st.distance * sinf(yaw);
	e->s.origin[2] += 1;
	e->elasticity = -1;
	e->flags |= FL_NO_KNOCKBACK;
	e->use = drive_use;
	gi.linkentity(e);
}

static void SP_truck_old(edict_t *e)
{
	static const char *skins[3] = {"truck", "truck2", "truck3"};
	int k = (e->s.skinnum >= 0 && e->s.skinnum <= 2) ? e->s.skinnum : 0;
	fx::Index("environ/normalsmoke");
	Sound("impact/explosion/med.wav");
	Force(e, 2);
	T(e, T_BOSNIA, 12, -96, -39, -45, 96, 39, 45, skins[k]);
	PropData &d = D(e);
	d.i2 = 1;
	d.i1 = 1;
	for (int i = 1; i <= 4; i++)
		Bolt(e, T_BOSNIA, 12, va("DUMMY0%d", i), 13, "DUMMY01");
	e->die = vehicle_die;
	if (e->spawnflags & 16) {
		d.f0 = e->speed ? e->speed : 200;
		e->use = spin_use;
		e->touch = truck_touch;
	}
	if (e->spawnflags & 4)
		DriveSetup(e);
}

// ---------------------------------------------------------------------------------------------------------------
// castle

static void SP_armor(edict_t *e) { T(e, T_CASTLE, 0, -4, -11, -38, 11, 17, 50); }
static void SP_bust(edict_t *e) { T(e, T_CASTLE, 1, -4, -6, -1, 4, 3, 13, e->s.skinnum == 1 ? "bust2" : "bust"); }
static void SP_cannon(edict_t *e) { T(e, T_CASTLE, 2, -27, -11, -13, 55, 11, 20); }
static void SP_hedge(edict_t *e)
{
	Force(e, 2);
	T(e, T_CASTLE, 13, -10, -14, -12, 9, 11, 11);
}
static void SP_tree(edict_t *e)
{
	Force(e, 2);
	T(e, T_CASTLE, 14, -24, -21, -11, 24, 21, 74);
}

// chairs: i0 = 1 big chair
enum { POSE_NONE, POSE_FLAT, POSE_TILT_F, POSE_TILT_B, POSE_TILT_L, POSE_TILT_R };

static void ChairPose(edict_t *self, int pose)
{
	PropData &d = D(self);
	vec3_t f, r, end;
	AngleVectors(self->s.angles, f, r, NULL);
	float m = d.i0 ? -8.7f : -6.0f;
	trace_t tr;
	switch (pose) {
	case POSE_FLAT:
		VectorCopy(d.start_angles, self->s.angles);
		SetBox(self, -10, -9, -2, 10, 9, 18);
		break;
	case POSE_TILT_F:
		VectorMA(self->s.origin, 20, f, end);
		tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
		if (tr.fraction < 1)
			VectorMA(self->s.origin, -20 * tr.fraction, f, self->s.origin);
		self->s.angles[PITCH] += 40;
		SetBox(self, -10, -9, -6, 10, 9, 18);
		break;
	case POSE_TILT_B:
		VectorMA(self->s.origin, -30, f, end);
		tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
		if (tr.fraction < 1)
			VectorMA(self->s.origin, 30 * tr.fraction, f, self->s.origin);
		self->s.angles[PITCH] -= 37;
		SetBox(self, -10, -9, -5.5f, 10, 9, 18);
		break;
	case POSE_TILT_L:
		VectorMA(self->s.origin, -30, r, end);
		tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
		if (tr.fraction < 1)
			VectorMA(self->s.origin, 30 * tr.fraction, f, self->s.origin); // sic: forward
		self->s.angles[ROLL] -= 40;
		SetBox(self, -10, -9, m, 10, 9, 18);
		break;
	case POSE_TILT_R:
		VectorMA(self->s.origin, 30, r, end);
		tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
		if (tr.fraction < 1)
			VectorMA(self->s.origin, -30 * tr.fraction, f, self->s.origin); // sic: forward
		self->s.angles[ROLL] += 40;
		SetBox(self, -10, -9, m, 10, 9, 18);
		break;
	default:
		return;
	}
	gi.linkentity(self);
}

static void chair_legs(edict_t *self, int h, edict_t *, const vec3_t)
{
	PropData &d = D(self);
	bool L[5] = {false, Shown(self, 1), Shown(self, 2), Shown(self, 3), Shown(self, 4)};
	if (!L[1] && !L[2] && !L[3] && !L[4]) {
		ChairPose(self, POSE_FLAT);
		return;
	}
	if (d.i0) { // big chair
		if (h == 1)
			ChairPose(self, L[2] ? POSE_TILT_R : POSE_FLAT);
		else if (h == 2)
			ChairPose(self, L[1] ? POSE_TILT_L : POSE_FLAT);
		return;
	}
	int rem[4], n = 0;
	for (int i = 1; i <= 4; i++)
		if (i != h && L[i])
			rem[n++] = i;
	if (n == 3)
		return;
	if (n == 2) {
		int a = rem[0], b = rem[1];
		if (a == 3 && b == 4)
			ChairPose(self, POSE_TILT_F);
		else if (a == 1 && b == 2)
			ChairPose(self, POSE_TILT_B);
		else if (a == 2 && b == 4)
			ChairPose(self, POSE_TILT_R);
		else if (a == 1 && b == 3)
			ChairPose(self, POSE_TILT_L);
		else { // diagonal
			RemovePart(self, a, true);
			RemovePart(self, b, true);
			ChairPose(self, POSE_FLAT);
		}
		return;
	}
	if (n == 1)
		RemovePart(self, rem[0], true);
	ChairPose(self, POSE_FLAT);
}

static void bigchair_back(edict_t *self, int, edict_t *, const vec3_t)
{
	vec3_t p;
	gg::BoltWorld(self, NULL, "DUMMY05", p);
	PartDebris(self, p);
	if (Shown(self, 3) || Shown(self, 4)) {
		gg::BoltWorld(self, NULL, "DUMMY03", p);
		PartDebris(self, p);
		gg::BoltWorld(self, NULL, "DUMMY04", p);
		PartDebris(self, p);
		gg::AllParts(gg::TrackedPart(self, 3), false); // shown bits stay set (original quirk)
		gg::AllParts(gg::TrackedPart(self, 4), false);
	}
}

static const PartRow kChairParts[] = {{"DUMMY01", 1, chair_legs}, {"DUMMY02", 2, chair_legs},
                                      {"DUMMY03", 3, chair_legs}, {"DUMMY04", 4, chair_legs},
                                      {"DUMMY05", 5, NULL}};
static const PartRow kBigChairParts[] = {{"DUMMY01", 1, chair_legs}, {"DUMMY02", 2, chair_legs},
                                         {"DUMMY03", 3, NULL}, {"DUMMY04", 4, NULL},
                                         {"DUMMY05", 5, bigchair_back}};

static void SP_chair(edict_t *e)
{
	T(e, T_CASTLE, 3, -17, -14, -15, 11, 13, 38);
	VectorCopy(e->s.angles, D(e).start_angles);
	Bolt(e, T_CASTLE, 3, "DUMMY01", 4, "DUMMY01");
	Bolt(e, T_CASTLE, 3, "DUMMY02", 4, "DUMMY01");
	Bolt(e, T_CASTLE, 3, "DUMMY03", 5, "DUMMY01");
	Bolt(e, T_CASTLE, 3, "DUMMY04", 5, "DUMMY01");
	Bolt(e, T_CASTLE, 3, "DUMMY05", 6, "DUMMY05");
	D(e).parts = kChairParts;
	D(e).nparts = 5;
	SetPain(e, Pain_Parts);
}

static void SP_chair_big(edict_t *e)
{
	T(e, T_CASTLE, 7, -17, -14, -17, 12, 14, 38);
	VectorCopy(e->s.angles, D(e).start_angles);
	D(e).i0 = 1;
	Bolt(e, T_CASTLE, 7, "DUMMY01", 8, "DUMMY01");
	Bolt(e, T_CASTLE, 7, "DUMMY02", 8, "DUMMY01");
	Bolt(e, T_CASTLE, 7, "DUMMY03", 9, "DUMMY03");
	Bolt(e, T_CASTLE, 7, "DUMMY04", 9, "DUMMY03");
	Bolt(e, T_CASTLE, 7, "DUMMY05", 10, "DUMMY05");
	D(e).parts = kBigChairParts;
	D(e).nparts = 5;
	SetPain(e, Pain_Parts);
}

// globe: spin record v2 on the globe part
static void globe_spindown(edict_t *self)
{
	int v = (int)(fx::RotationValue(self, 2) * 0.75f);
	if (abs(v) < 153.6f) {
		fx::RotationClear(self);
		return;
	}
	fx::RotationSetValue(self, 2, v);
	self->nextthink = level.time + 0.5f;
}

static void globe_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t p)
{
	float f = 0.1f * damage;
	vec3_t a, r, c;
	const float *src = other ? other->s.origin : self->s.origin;
	VectorSubtract(src, self->s.origin, a);
	VectorNormalize(a);
	VectorSubtract(p, self->s.origin, r);
	CrossProduct(a, r, c);
	if (c[2] != 0) {
		float sg = c[2] < 0 ? 1.0f : -1.0f;
		if (fx::RotationActive(self))
			fx::RotationSetValue(self, 2, fx::RotationValue(self, 2) + (int)(512 * sg * f));
		else {
			gg::IGhoulInst *g = gg::TrackedPart(self, 1);
			float rate[3] = {0, sg * (f < 30 ? f : 30), 0};
			int inst[4] = {g ? g->GetInstanceId() : 0, 0, 0, 0};
			fx::RotationSet(self, rate, inst);
		}
	}
	self->think = globe_spindown;
	self->nextthink = level.time + 0.5f;
}

static void SP_globe(edict_t *e)
{
	T(e, T_CASTLE, 12, -7, -9, -26, 15, 9, 12);
	Bolt(e, T_CASTLE, 12, "DUMMY01", 11, "DUMMY01");
	SetPain(e, globe_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// Iraq

static void SP_basket(edict_t *e) { T(e, T_IRAQ, 0, -6, -7, -18, 7, 6, 15); }
static void SP_cart_camel(edict_t *e) { T(e, T_IRAQ, 1, -20, -48, -24, 20, 48, 24); }
static void SP_melon(edict_t *e) { T(e, T_IRAQ, 5, -8, -4, -4, 8, 4, 4); }
static void SP_orange(edict_t *e) { T(e, T_IRAQ, 6, -2, -2, -2, 2, 2, 2); }
static void SP_partition(edict_t *e) { T(e, T_IRAQ, 9, -63, -1, 0, 62, 2, 35); }
static void SP_pottery(edict_t *e) { T(e, T_IRAQ, 10, -8, -8, -9, 8, 8, 9); }
static void SP_iraq_sprinkler(edict_t *e)
{
	Force(e, 2);
	T(e, T_IRAQ, 11, -7, -63, -2, 6, 64, 2);
}
static void SP_stealth(edict_t *e)
{
	Force(e, 1);
	e->s.angles[YAW] = 270;
	T(e, T_IRAQ, 12, -32, -32, -54, 32, 32, 53);
}
static void SP_tree_palm(edict_t *e)
{
	Force(e, 2);
	T(e, T_IRAQ, 13, -30, -9, -67, -6, 23, 67);
	// not relinked: the entity state keeps the packed box of the setup's link (black-box, irq1a: the original sends
	// the rotated box for this non-solid tree); traces skip it because it is SOLID_NOT
	e->solid = SOLID_NOT;
}

// valve
static void valve_eos(edict_t *self)
{
	gg::PauseEnt(self);
	StopLoop(self);
}
static gg::NoteCallback s_valve_eos(valve_eos);
gg::NoteCallback *NoteValve(void) { return &s_valve_eos; }

static void valve_use(edict_t *self, edict_t *other, edict_t *)
{
	if (self->spawnflags & 128) {
		int stt = other ? other->moveinfo.state : -1;
		self->avelocity[PITCH] = stt == 2 ? 40.0f : stt == 3 ? -40.0f : 0.0f;
		return;
	}
	PlaySound(self, CHAN_VOICE, "Ambient/Models/Valve/ValveTurn.wav", 0.6f, ATTN_NORM);
	if (self->ghoulInst && gg::PlayInstAt(self->ghoulInst, "valve", ghoul::kPlayLoop, level.time, 0)) {
		gg::RemoveNote(self->ghoulInst, "EOS", &s_valve_eos);
		gg::OnNote(self, NULL, "EOS", &s_valve_eos);
	}
}

static void SP_valve(edict_t *e)
{
	Force(e, 3);
	Sound("Ambient/Models/Valve/ValveTurn.wav");
	T(e, T_IRAQ, 14, -12, -3, -10, 12, 1, 11);
	gg::PauseEnt(e);
	e->movetype = MOVETYPE_DAN;
	e->gravity = 0;
	e->use = valve_use;
}

static void SP_generic_sprinkler(edict_t *e)
{
	Force(e, 2);
	SetBox(e, -4, -4, -4, 4, 4, 4);
	gg::SetSimpleModel(e, "objects/generic/urn", "urn");
	e->surfacetype = 21;
	e->health = 10;
	SetupNoTable(e, 21, 0, 10, SOLID_BBOX);
	e->die = Die_StandardBreak;
	fx::SlotAdd(e, fx::Index("environ/sprinkler"), 0, 0);
}

// flatbed: tires start at part 3 with beams (spawnflag 8), else 1
static void SP_flatbed(edict_t *e)
{
	fx::Index("environ/normalsmoke");
	Sound("impact/explosion/med.wav");
	T(e, T_IRAQ, 4, -95, -40, -42, 95, 40, 45);
	PropData &d = D(e);
	d.i2 = 2;
	gg::IGhoulInst *in = e->ghoulInst;
	if (e->style == 0)
		gg::HidePart(in, "OILTANK");
	static const char *lights[] = {"top1", "top2", "back1", "back2", "frt1", "frt2", "frt3", "frt4"};
	if (!(e->spawnflags & 8)) {
		for (const char *p : lights)
			gg::HidePart(in, p);
		d.i1 = 1;
	} else {
		for (const char *p : lights)
			gg::ShowPart(in, p);
		Bolt(e, T_IRAQ, 4, "headlight1", 8, "to_headlight");
		Bolt(e, T_IRAQ, 4, "headlight2", 8, "to_headlight");
		d.i1 = 3;
	}
	for (int i = 1; i <= 4; i++)
		Bolt(e, T_IRAQ, 4, va("DUMMY0%d", i), 3, "DUMMY05");
	e->die = vehicle_die;
	if (e->spawnflags & 16) {
		d.f0 = e->speed ? e->speed : 200;
		e->use = spin_use;
	} else if (e->spawnflags & 4)
		DriveSetup(e);
}

// fruit stand: list[] = fruits (melons first), nlist; fruit: e0 = owning stand (NULL when released)
static void fruit_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t);

static void fruit_launched_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	StandardBreak(self, attacker, 0, 0);
}

static void Launch(edict_t *f)
{
	if (!f || !f->inuse)
		return;
	VectorSet(f->velocity, frand(-100, 100), frand(-100, 100), frand(200, 500));
	f->avelocity[ROLL] = frand(-100, 100);
	f->die = fruit_launched_die;
}

static void fruit_released_die(edict_t *, edict_t *, edict_t *, int, vec3_t) {}

static void fruit_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	edict_t *stand = D(self).e0;
	if (!stand || !stand->inuse) {
		self->die = fruit_released_die;
		return;
	}
	PropData &sd = D(stand);
	int N = ((stand->spawnflags & 12) == 12) ? 10 : 5;
	edict_t *snap[10];
	int n = 0;
	for (int i = 0; i < sd.nlist && i < N; i++)
		snap[n++] = sd.list[i];
	sd.nlist = 0;
	for (int i = 0; i < n; i++)
		if (snap[i] && snap[i]->inuse)
			StandardBreak(snap[i], attacker, 0, 0);
}

static void stand_launch_late(edict_t *self)
{
	PropData &d = D(self);
	for (int i = 0; i < 3; i++)
		Launch(d.list[i]);
	G_FreeEdict(self);
}

static void stand_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	PropData &d = D(self);
	if (!d.nlist) {
		StandardBreak(self, attacker, 0, 0);
		return;
	}
	int N = ((self->spawnflags & 12) == 12) ? 10 : 5;
	edict_t *rel[10];
	int n = 0;
	for (int i = 0; i < d.nlist && i < N; i++) {
		rel[n] = d.list[i];
		if (rel[n] && rel[n]->inuse) {
			D(rel[n]).e0 = NULL;
			rel[n]->die = fruit_released_die;
		}
		n++;
	}
	d.nlist = 0;
	for (int i = 3; i < n; i++)
		Launch(rel[i]);
	// fruits 1-3 are launched 0.1 s later (a helper keeps the list)
	edict_t *h = G_Spawn();
	h->classname = (char *)"fruit_launcher";
	ClearData(h);
	for (int i = 0; i < 3 && i < n; i++)
		D(h).list[i] = rel[i];
	h->think = stand_launch_late;
	h->nextthink = level.time + FRAMETIME;
	StandardBreak(self, attacker, 0, 0);
}

static edict_t *SpawnFruit(edict_t *stand, bool melon, float a, float b, float up, float yawofs)
{
	edict_t *f = G_Spawn();
	f->classname = (char *)(melon ? "misc_iraq_fruit_melon" : "misc_iraq_fruit_orange");
	ClearData(f);
	VectorCopy(stand->s.angles, f->s.angles);
	vec3_t ang = {stand->s.angles[PITCH], stand->s.yawofs, stand->s.angles[ROLL]};
	vec3_t F, R;
	AngleVectors(ang, F, R, NULL);
	for (int i = 0; i < 3; i++)
		f->s.origin[i] = stand->s.origin[i] + a * F[i] + b * R[i];
	f->s.origin[2] += up;
	f->s.yawofs += yawofs;
	spawn_temp_t save = st;
	memset(&st, 0, sizeof(st));
	if (melon)
		SP_melon(f);
	else
		SP_orange(f);
	st = save;
	f->die = fruit_die;
	D(f).e0 = stand;
	PropData &sd = D(stand);
	if (sd.nlist < 10)
		sd.list[sd.nlist++] = f;
	return f;
}

static void SP_fruit_stand(edict_t *e)
{
	T(e, T_IRAQ, 7, -8, -16, -16, 8, 16, 16);
	if (e->spawnflags & 16)
		return; // plain prop
	if (!(e->spawnflags & (4 | 8)))
		e->spawnflags |= 4 | 8;
	if (e->spawnflags & 4) {
		SpawnFruit(e, true, 0, 0, 20, 0);
		SpawnFruit(e, true, 0, -8, 20, 0);
		SpawnFruit(e, true, 0, -16, 20, 0);
		SpawnFruit(e, true, 0, -4, 27, 0);
		if (e->spawnflags & 8)
			SpawnFruit(e, true, 0, -12, 27, 0);
		else
			SpawnFruit(e, true, 0, 12, 20, 25);
	}
	if (e->spawnflags & 8) {
		SpawnFruit(e, false, 2, 8, 18, 0);
		SpawnFruit(e, false, -2, 8, 18, 0);
		SpawnFruit(e, false, 0, 12, 18, 0);
		SpawnFruit(e, false, 4, 12, 18, 0);
		SpawnFruit(e, false, -8, 18, 18, 0);
	}
	e->die = stand_die;
}

static const SpawnEntry s_spawns[] = {
	{"misc_bosnia_antipersonnel_jack", SP_antipersonnel_jack},
	{"misc_bosnia_flag_wall", SP_flag_wall},
	{"misc_bosnia_landmine", SP_landmine},
	{"misc_bosnia_rubble1", SP_rubble1},
	{"misc_bosnia_rubble2", SP_rubble2},
	{"misc_bosnia_searchbeam", SP_searchbeam},
	{"misc_bosnia_searchlight", SP_searchlight},
	{"misc_bosnia_tank_jack", SP_tank_jack},
	{"misc_bosnia_truck_chunks", SP_truck_chunks},
	{"misc_bosnia_truck_old", SP_truck_old},
	{"misc_castle_armor", SP_armor},
	{"misc_castle_bust", SP_bust},
	{"misc_castle_cannon", SP_cannon},
	{"misc_castle_chair", SP_chair},
	{"misc_castle_chair_big", SP_chair_big},
	{"misc_castle_globe", SP_globe},
	{"misc_castle_hedge", SP_hedge},
	{"misc_castle_tree", SP_tree},
	{"misc_iraq_basket", SP_basket},
	{"misc_iraq_cart_camel", SP_cart_camel},
	{"misc_iraq_flatbed", SP_flatbed},
	{"misc_iraq_fruit_melon", SP_melon},
	{"misc_iraq_fruit_orange", SP_orange},
	{"misc_iraq_fruit_stand", SP_fruit_stand},
	{"misc_iraq_partition", SP_partition},
	{"misc_iraq_pottery", SP_pottery},
	{"misc_iraq_sprinkler", SP_iraq_sprinkler},
	{"misc_iraq_stealth", SP_stealth},
	{"misc_iraq_tree_palm", SP_tree_palm},
	{"misc_iraq_valve", SP_valve},
	{"misc_generic_sprinkler", SP_generic_sprinkler},
	{NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
