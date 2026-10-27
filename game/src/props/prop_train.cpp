// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — subway train prop (misc_generic_train) and the delayed-respawn helper.
// Implements: SPEC-770 §6B.9, SPEC-760 B8.5 (delayed respawn record used by props)

#include "props.h"
#include "../effects/fx.h"

namespace props {

static const vec3_t kZero = {0, 0, 0};

// ---------------------------------------------------------------------------------------------------------------
// delayed respawn (SPEC-760 B8.5): the placeholder keeps the record and spawns a fresh copy after the delay

struct Respawn {
	void (*spawn)(edict_t *);
	char *classname;
	vec3_t origin, angles;
	int health, spawnflags, style, skin;
	float wait, accel, delay;
	char *targetname, *target;
};
static Respawn s_respawn[MAX_EDICTS];

static void respawn_think(edict_t *self)
{
	Respawn r = s_respawn[G_EdictNum(self)];
	G_FreeEdict(self);
	edict_t *e = G_Spawn();
	e->classname = r.classname;
	VectorCopy(r.origin, e->s.origin);
	VectorCopy(r.angles, e->s.angles);
	e->health = r.health;
	e->spawnflags = r.spawnflags;
	e->wait = r.wait;
	e->accel = r.accel;
	e->style = r.style;
	e->s.skinnum = r.skin;
	e->targetname = r.targetname;
	e->target = r.target;
	ClearData(e);
	memset(&st, 0, sizeof(st));
	r.spawn(e);
}

static void RecordRespawn(edict_t *ent, void (*spawn)(edict_t *), float delay)
{
	Respawn &r = s_respawn[G_EdictNum(ent)];
	r.spawn = spawn;
	r.classname = ent->classname;
	VectorCopy(ent->s.origin, r.origin);
	VectorCopy(ent->s.angles, r.angles);
	r.health = ent->health;
	r.spawnflags = ent->spawnflags;
	r.style = ent->style;
	r.skin = ent->s.skinnum;
	r.wait = ent->wait;
	r.accel = ent->accel;
	r.delay = delay;
	r.targetname = ent->targetname;
	r.target = ent->target;
}

static void TriggerRespawn(edict_t *ent)
{
	Respawn r = s_respawn[G_EdictNum(ent)];
	if (!r.spawn)
		return;
	edict_t *p = G_Spawn();
	p->classname = (char *)"delayed_respawn";
	s_respawn[G_EdictNum(p)] = r;
	p->think = respawn_think;
	p->nextthink = level.time + (r.delay > 0 ? r.delay : FRAMETIME);
}

// ---------------------------------------------------------------------------------------------------------------
// train: v0 = stop point S, v1 = m (run axis), i0 = state, f0 = top speed, f1 = accel

enum { TS_NONE, TS_ACCEL, TS_CRUISE, TS_BRAKE, TS_STOPPED };

static float HorizLen(const vec3_t v) { return sqrtf(v[0] * v[0] + v[1] * v[1]); }

static void train_stopped(edict_t *self)
{
	VectorClear(self->velocity);
	StopLoop(self);
	self->think = NULL;
	self->nextthink = 0;
	D(self).i0 = TS_STOPPED;
	if (self->spawnflags & 2) {
		TriggerRespawn(self);
		G_FreeEdict(self);
	} else if (self->spawnflags & 16)
		G_FreeEdict(self);
}

static float Remaining(edict_t *self)
{
	PropData &d = D(self);
	vec3_t t;
	VectorSubtract(d.v0, self->s.origin, t);
	return HorizLen(t) - 167;
}

static void ScaleXY(edict_t *self, float k)
{
	self->velocity[0] *= k;
	self->velocity[1] *= k;
	self->velocity[2] = 0;
}

// returns true when the state changed to stopped
static bool StopCheck(edict_t *self)
{
	PropData &d = D(self);
	float r = Remaining(self);
	float s = 0.1f * HorizLen(self->velocity);
	if (!self->velocity[0] && !self->velocity[1] && !self->velocity[2]) {
		d.i0 = TS_STOPPED;
		return true;
	}
	if (!(self->spawnflags & 8)) {
		if (r < s) {
			ScaleXY(self, s > 0 ? r / s : 0);
			d.i0 = TS_STOPPED;
			if (!(self->spawnflags & (2 | 16)))
				StopLoop(self);
			return true;
		}
	} else if (r < 5 * s) {
		ScaleXY(self, s > 0 ? r / (6 * s) : 0);
		d.i0 = TS_BRAKE;
	}
	return false;
}

static void train_think(edict_t *self)
{
	PropData &d = D(self);
	Riders(self);
	self->nextthink = level.time + FRAMETIME;
	switch (d.i0) {
	case TS_ACCEL: {
		float v2 = self->velocity[0] * self->velocity[0] + self->velocity[1] * self->velocity[1];
		if (v2 >= d.f0 * d.f0)
			d.i0 = TS_CRUISE;
		else {
			self->velocity[0] += d.f1 * d.v1[0];
			self->velocity[1] += d.f1 * d.v1[1];
		}
		if (irand(1, 6) == 1) {
			static const float as[3] = {54, -54, -162}, bs[3] = {-105, 105, 315};
			vec3_t f, r, p;
			AngleVectors(self->s.angles, f, r, NULL);
			float a = as[irand(0, 2)], b = bs[irand(0, 2)];
			for (int i = 0; i < 3; i++)
				p[i] = self->s.origin[i] + a * f[i] - b * r[i];
			p[2] += 15;
			fx::Sparks(fx::TE_SPARKS_WHITE, p, kZero);
		}
		StopCheck(self);
		break;
	}
	case TS_CRUISE:
		StopCheck(self);
		if (irand(1, 7) == 1) {
			vec3_t p = {self->s.origin[0] + (irand(0, 1) ? 52 : -52), self->s.origin[1] + (irand(0, 1) ? 80 : -80),
			            self->s.origin[2] - 55};
			fx::Sparks(fx::TE_SPARKS_WHITE, p, kZero);
		}
		break;
	case TS_BRAKE: {
		float V = HorizLen(self->velocity), s = 0.1f * V, r = Remaining(self);
		if (V < 20) {
			ScaleXY(self, s > 0 ? r / s : 0);
			d.i0 = TS_STOPPED;
		} else
			ScaleXY(self, s > 0 ? r / (6 * s) : 0);
		break;
	}
	case TS_STOPPED:
		train_stopped(self);
		break;
	default:
		break;
	}
}

static void train_start(edict_t *self)
{
	PropData &d = D(self);
	vec3_t a, b;
	VectorCopy(self->s.origin, a);
	a[2] += 40;
	VectorMA(a, 8000, d.v1, b);
	trace_t tr = gi.trace(a, vec3_origin, vec3_origin, b, self, self->clipmask);
	VectorMA(self->s.origin, 8000 * tr.fraction, d.v1, d.v0);
	d.v0[2] = self->s.origin[2];
	self->friction = 0;
	self->gravity = 0;
	VectorScale(d.v1, d.f1, self->velocity);
	self->use = NULL;
	LoopSound(self, "Ambient/Locs/Subway/Train/Move.wav", 0xFB);
	if (irand(1, 3) == 1)
		PlaySound(self, CHAN_BODY, "Ambient/Locs/Subway/Train/Horn2.wav", 1, ATTN_NORM);
	d.i0 = TS_ACCEL;
	StopCheck(self);
	self->think = train_think;
	self->nextthink = level.time + FRAMETIME;
}

static void train_use(edict_t *self, edict_t *, edict_t *)
{
	Riders(self);
	train_start(self);
}

static void train_autostart(edict_t *self)
{
	Riders(self);
	train_start(self);
}

static void train_touch(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || other == g_edicts)
		return;
	if (!other->client && !other->takedamage) {
		G_FreeEdict(other);
		return;
	}
	if (!self->velocity[0] && !self->velocity[1] && !self->velocity[2])
		return;
	if (other->s.origin[2] + other->mins[2] >= self->s.origin[2] + self->maxs[2])
		return;
	vec3_t f, r, P, u;
	AngleVectors(self->s.angles, f, r, NULL);
	VectorMA(self->s.origin, 165, r, P);
	VectorSubtract(other->s.origin, P, u);
	VectorNormalize(u);
	float dot = DotProduct(u, f);
	if (fabsf(dot) < 0.25f) {
		PlaySound(self, CHAN_VOICE, "Ambient/Locs/Subway/Train/Impact.wav", 1, ATTN_NORM);
		Damage(other, self, self, kZero, other->s.origin, other->s.origin, 9000, 1000, 0x8008, MOD_CRUSH);
	} else
		gi.cprintf(NULL, PRINT_HIGH, "Failed fDot test %f\n", dot);
}

static void SP_train(edict_t *ent)
{
	Sound("Ambient/Locs/Subway/Train/Move.wav");
	Sound("Ambient/Locs/Subway/Train/Horn2.wav");
	Sound("Ambient/Locs/Subway/Train/Impact.wav");
	static const char *skins[4] = {"train", "train2", "train3", "train4"};
	int k = (ent->s.skinnum >= 1 && ent->s.skinnum <= 3) ? ent->s.skinnum : 0;
	if (!ent->health)
		ent->health = 9000;
	int flags = ent->spawnflags;
	ent->spawnflags = 2;
	SetBox(ent, -54, -164, -60, 55, 168, 60);
	Setup(ent, T_GENERIC, 97, skins[k]);
	ent->spawnflags = flags;
	ent->takedamage = DAMAGE_NO;
	const ModelEntry *e = D(ent).entry;
	if (ent->ghoulInst && e)
		gg::SetSkinInst(ent->ghoulInst, e->dir, e->file, e->skinmat, skins[k]);
	if (k) {
		gg::ShowOnlyPart(ent->ghoulInst, "TRAIN");
		gg::HidePart(ent->ghoulInst, "WORLD_OMNI");
	} else {
		Bolt(ent, T_GENERIC, 97, "headlight1", 58, "to_headlight");
		Bolt(ent, T_GENERIC, 97, "headlight2", 58, "to_headlight");
	}
	PropData &d = D(ent);
	d.f0 = ent->style ? (float)ent->style : 600;
	d.f1 = ent->accel ? ent->accel : 100;
	float yaw = ent->s.angles[YAW] * (float)(M_PI / 180.0);
	VectorSet(d.v1, -sinf(yaw), cosf(yaw), 0);
	if (ent->spawnflags & (1 | 4)) {
		ent->use = train_use;
		ent->movetype = MOVETYPE_DAN;
		ent->s.origin[2] += 1;
		ent->elasticity = -1;
		ent->flags |= FL_NO_KNOCKBACK;
		ent->touch = train_touch;
		gi.linkentity(ent);
	}
	if (ent->spawnflags & 2)
		RecordRespawn(ent, SP_train, ent->wait);
	if (ent->spawnflags & 4) {
		ent->think = train_autostart;
		ent->nextthink = level.time + FRAMETIME;
	}
}

static const SpawnEntry s_spawns[] = {{"misc_generic_train", SP_train}, {NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
