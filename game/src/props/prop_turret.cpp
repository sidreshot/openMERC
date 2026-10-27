// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — automatic guns (turret props).
// Implements: SPEC-770 §6A.7, SPEC-791 Behaviour 11 (aim controller use)

#include "props.h"
#include "../effects/fx.h"

namespace props {

static const float DEG = (float)(M_PI / 180.0);

// per-turret state: inst0 = yaw part (NULL: the entity), inst1 = gun (gun_auto family);
// list[] unused; i0 shots in burst, i1 bursts done, i2 active/searching, i3 aligned-last-frame;
// f0 next shot time, f1 step (radians), f2 detection radius, f3 pause until; e0 enemy
float st_minpitch(edict_t *e);
float st_maxpitch(edict_t *e);

enum { TK_AUTO = 1, TK_AUTO2, TK_AUTO3, TK_TEST, TK_CASTLE, TK_BIG };

static void GunPrecacheAuto(bool castle)
{
	Sound("Enemy/MGun/trnslow.wav");
	Sound("Enemy/MGun/trnstop.wav");
	Sound("Weapons/Autoshot/fire.wav");
	Sound("Enemy/MGun/wake.wav");
	if (castle)
		Sound("Enemy/MGun/Wake2.wav");
	fx::Index("weapons/othermz/autogun");
	fx::Index("weapons/world/autogundeath");
	fx::Index("environ/onfireburst");
}

static void Pain_Nothing(edict_t *, edict_t *, float, int, const vec3_t) {}

static void GunDeath(edict_t *self, edict_t *attacker)
{
	StopLoop(self);
	self->avelocity[YAW] = 0;
	fx::EffectEnt(fx::Index("weapons/world/autogundeath"), self);
	self->die = NULL;
	self->takedamage = DAMAGE_NO;
	Explode(self, true, true);
	TableBreak(self, attacker);
}

static void gun_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t) { GunDeath(self, attacker); }

static void gunauto_die(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	self->health = irand(5, 20); // the flash of the explosion then blinds within 10 x this health
	GunDeath(self, attacker);
}

static void TurretCommon(edict_t *e)
{
	e->s.effects |= 0x200;
	e->flags |= FL_NO_KNOCKBACK;
	SetPain(e, Pain_Nothing);
}

// search (shared): returns the enemy or NULL
static edict_t *Search(edict_t *self, bool notarget)
{
	PropData &d = D(self);
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = g_edicts + i;
		if (!p->inuse || !p->client)
			continue;
		if (notarget && (p->flags & FL_NOTARGET))
			continue;
		vec3_t dd;
		VectorSubtract(p->s.origin, self->s.origin, dd);
		if (VectorLength(dd) > d.f2)
			continue;
		trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, p->s.origin, self, 0x060000B9);
		if (!tr.ent || !tr.ent->client || tr.ent->health <= 0) {
			d.e0 = NULL;
			return NULL;
		}
		d.e0 = p;
	}
	return d.e0;
}

static bool TraceHitsPlayer(edict_t *self, const vec3_t from, const vec3_t to)
{
	trace_t tr = gi.trace(from, vec3_origin, vec3_origin, to, self, 0x060000B9);
	return tr.ent && tr.ent->client;
}

static void FireFrom(edict_t *self, gg::IGhoulInst *gun, const char *bolt, const vec3_t aim, bool towardaim)
{
	vec3_t pos, dir;
	gg::BoltWorld(self, gun, bolt, pos, dir);
	if (towardaim) {
		VectorSubtract(aim, pos, dir);
		VectorNormalize(dir);
	}
	fx::FireAttack(self, 21, pos, dir, 0, false);
	if (gun)
		fx::EffectInstBolt(fx::Index("weapons/othermz/autogun"), self, gun->GetInstanceId(), gg::PartId(gun, bolt));
}

// ---------------------------------------------------------------------------------------------------------------
// gun_auto family

static void autogun_think(edict_t *self)
{
	PropData &d = D(self);
	self->nextthink = level.time + FRAMETIME;
	edict_t *had = d.e0;
	edict_t *enemy = Search(self, true);
	if (enemy && !had)
		PlaySound(self, CHAN_VOICE, "Enemy/MGun/wake.wav", 0.6f, ATTN_NORM);
	if (!enemy)
		return;
	vec3_t dd;
	VectorSubtract(enemy->s.origin, self->s.origin, dd);
	int slow = gi.soundindex("Enemy/MGun/trnslow.wav");
	if (VectorLength(dd) > d.f2) {
		self->avelocity[YAW] = 0;
		if (self->s.sound == slow)
			self->s.sound = gi.soundindex("Enemy/MGun/trnstop.wav");
		return;
	}
	// aim: yaw first, pitch only when the yaw has reached the target
	gg::AimParams a;
	gg::AimDefaults(a);
	a.step = d.f1;
	a.minyaw = -(float)M_PI;
	a.maxyaw = (float)M_PI;
	a.yaw = true;
	int r;
	if (d.inst0) {
		a.inst = d.inst0;
		a.parent = self->ghoulInst;
		a.parentbolt = gg::PartId(self->ghoulInst, "DUMMY01");
		r = gg::Aim(self, a, enemy->s.origin);
	} else
		r = gg::Aim(self, a, enemy->s.origin);
	if (r == 2)
		LoopSound(self, "Enemy/MGun/trnslow.wav");
	else if (self->spawnflags & 8) {
		gg::AimParams p;
		gg::AimDefaults(p);
		p.step = d.f1;
		p.pitch = true;
		p.minpitch = -st_minpitch(self) * DEG;
		p.maxpitch = st_maxpitch(self) * DEG;
		p.inst = d.inst1;
		p.parent = d.inst0 ? d.inst0 : self->ghoulInst;
		p.parentbolt = gg::PartId(p.parent, d.inst0 ? "DUMMY01" : "DUMMY02");
		gg::Aim(self, p, enemy->s.origin);
	}
	// firing
	vec3_t gpos, gdir, to;
	gg::BoltWorld(self, d.inst1, "DUMMY01", gpos, gdir);
	VectorNormalize(gdir);
	VectorSubtract(enemy->s.origin, self->s.origin, to);
	VectorNormalize(to);
	if (DotProduct(gdir, to) < 0.9f)
		return;
	if (self->s.sound == slow) {
		self->s.sound = 0;
		PlaySound(self, CHAN_VOICE, "Enemy/MGun/trnstop.wav", 0.6f, ATTN_NORM);
	}
	if (level.time < d.f0)
		return;
	vec3_t t1;
	VectorCopy(enemy->s.origin, t1);
	t1[2] += 20;
	if (TraceHitsPlayer(self, self->s.origin, t1) || TraceHitsPlayer(self, self->s.origin, enemy->s.origin)) {
		FireFrom(self, d.inst1, "_LEFTFLASH", t1, false);
		FireFrom(self, d.inst1, "_FLASHRIGHT", t1, false);
		PlaySound(self, CHAN_VOICE, "Weapons/Autoshot/fire.wav", 0.9f, ATTN_NORM);
	} else
		self->s.sound = 0;
	if (++d.i0 >= 3) {
		d.i0 = 0;
		if (++d.i1 >= 6) {
			d.i1 = 0;
			d.f0 = level.time + 3;
		} else
			d.f0 = level.time + 0.5f;
	} else
		d.f0 = level.time + FRAMETIME;
}

// key values kept in the scratch block: v0 = (minpitch, maxpitch, maxyaw)
float st_minpitch(edict_t *e) { return D(e).v0[0]; }
float st_maxpitch(edict_t *e) { return D(e).v0[1]; }

static void AutoSetup(edict_t *e, int kind)
{
	PropData &d = D(e);
	d.v0[0] = st.minpitch;
	d.v0[1] = st.maxpitch;
	d.v0[2] = st.maxyaw;
	const char *skin = e->s.skinnum == 1 ? "gun2" : "gun";
	if (kind == TK_TEST)
		skin = "gun";
	if (kind != TK_TEST) {
		Toggle(e, 3);
		Force(e, 32);
		if (kind == TK_AUTO2 || kind == TK_AUTO3)
			Force(e, 16);
	}
	int T = game.skill_toughness;
	e->health = kind == TK_TEST ? 10000 : 100 + T;
	if (kind == TK_AUTO2) {
		SetBox(e, -27, -18, -8, 21, 21, 18);
		Setup(e, T_GENERIC, 51, skin);
		e->gravity = 0;
		d.inst1 = Bolt(e, T_GENERIC, 51, "DUMMY02", 49, "DUMMY02", skin);
	} else {
		if (kind == TK_AUTO3)
			SetBox(e, -21, -18, -8, 21, 21, 8);
		else
			SetBox(e, -28, -21, -34, 21, 21, 19);
		Setup(e, T_GENERIC, 50, skin);
		// the hidden yaw mount has no material, so it is bolted without a skin: with one, the parent's material `gun`
		// was looked up on it and every gun_auto printed "WARNING: Can't find material:gun", which the original does not
		// (mode A probes of ger2, ger3, jpn2, nyc1: no such line, mode B: one per map)
		d.inst0 = Bolt(e, T_GENERIC, 50, "DUMMY01", 52, "DUMMY01", NULL);
		gg::AllParts(d.inst0, false);
		if (d.inst0) {
			const ModelEntry *nul = Entry(T_GENERIC, 52), *gun = Entry(T_GENERIC, 49);
			d.inst1 = gg::AddTrackedBoltOnBolt(e, d.inst0, Ref(nul), "DUMMY01", Ref(gun), "DUMMY01", skin);
		}
		if (kind == TK_AUTO3) {
			gg::AllParts(e->ghoulInst, false);
			ClearFlags(e, 16);
		}
	}
	TurretCommon(e);
	d.f1 = (e->speed ? e->speed : 45) / 10.0f * DEG;
	d.f2 = e->volume ? e->volume : 400;
	if (!e->wait)
		e->wait = 30;
}

static void SP_gun_auto_kind(edict_t *e, int kind)
{
	GunPrecacheAuto(false);
	AutoSetup(e, kind);
	e->die = gunauto_die;
	if (e->spawnflags & 4) {
		e->takedamage = DAMAGE_YES;
		e->think = autogun_think;
		e->nextthink = level.time + e->wait * 0.1f;
	}
}

static void SP_gun_auto(edict_t *e) { SP_gun_auto_kind(e, TK_AUTO); }
static void SP_gun_auto2(edict_t *e) { SP_gun_auto_kind(e, TK_AUTO2); }
static void SP_gun_auto3(edict_t *e) { SP_gun_auto_kind(e, TK_AUTO3); }

static void testgun_think(edict_t *self)
{
	vec3_t f, p;
	AngleVectors(self->s.angles, f, NULL, NULL);
	VectorMA(self->s.origin, 27, f, p);
	fx::ExtDir(f);
	fx::EffectAt(fx::Index("weapons/othermz/autogun"), p);
	self->nextthink = level.time + 2.5f;
}

static void SP_test_gun_auto(edict_t *e)
{
	fx::Index("weapons/othermz/autogun");
	AutoSetup(e, TK_TEST);
	// black-box (g7all3, 2026-10-25): the original's test gun is sent with effects 0 (no turret bit 0x200), in the
	// baseline and in every frame; OPEN_QUESTIONS G7 12
	e->s.effects &= ~0x200;
	e->die = gun_die;
	e->think = testgun_think;
	e->nextthink = level.time + 2.5f;
}

// ---------------------------------------------------------------------------------------------------------------
// gun_castle / gun_big: inst0 = shield, list[0..3] = right DUMMY02, right DUMMY04, left DUMMY03, left DUMMY05

static void BigAim(edict_t *self, const vec3_t aim, bool castle)
{
	PropData &d = D(self);
	gg::AimParams a;
	gg::AimDefaults(a);
	a.step = d.f1;
	a.yaw = true;
	a.inst = d.inst0;
	a.parent = self->ghoulInst;
	a.parentbolt = gg::PartId(self->ghoulInst, "DUMMY01");
	float my = (castle && (self->spawnflags & 8)) ? d.v0[2] * DEG : 4000;
	a.minyaw = -my;
	a.maxyaw = my;
	gg::Aim(self, a, aim);
	static const char *bolts[4] = {"DUMMY02", "DUMMY04", "DUMMY03", "DUMMY05"};
	for (int i = 0; i < 4; i++) {
		gg::AimParams p;
		gg::AimDefaults(p);
		p.step = d.f1;
		p.pitch = true;
		p.minpitch = -0.314159f;
		p.maxpitch = 0.785398f;
		p.inst = gg::TrackedPart(self, 2 + i);
		p.parent = d.inst0;
		p.parentbolt = gg::PartId(d.inst0, bolts[i]);
		vec3_t t;
		VectorCopy(aim, t);
		if (i == 1 || i == 3)
			t[0] -= 20; // right gun on DUMMY04 and left gun on DUMMY05 (original quirk; pair UNVERIFIED)
		gg::Aim(self, p, t);
	}
}

static void bigsearch_think(edict_t *self)
{
	PropData &d = D(self);
	bool castle = d.i3 == TK_CASTLE;
	self->nextthink = level.time + FRAMETIME;
	edict_t *enemy = Search(self, false);
	if (!enemy)
		return;
	vec3_t aim;
	VectorCopy(enemy->s.origin, aim);
	aim[2] += 30;
	BigAim(self, aim, castle);
	vec3_t sp, sd, to;
	gg::BoltWorld(self, d.inst0, "DUMMY01", sp, sd);
	VectorNormalize(sd);
	VectorSubtract(aim, sp, to);
	VectorNormalize(to);
	if (DotProduct(sd, to) < 0.95f)
		return;
	if (!TraceHitsPlayer(self, self->s.origin, enemy->s.origin))
		return;
	for (int i = 0; i < 4; i++) {
		gg::IGhoulInst *g = gg::TrackedPart(self, 2 + i);
		FireFrom(self, g, i < 2 ? "_FLASHTOPRIGHT" : "_FLASHTOPLEFT", aim, true);
		if (castle)
			gg::PlayInst(g, i < 2 ? "gunright" : "gunleft", 0);
	}
}

static void castle_wake(edict_t *self)
{
	PlaySound(self, CHAN_VOICE, "Enemy/MGun/Wake2.wav", 1, ATTN_NORM);
	D(self).i2 = 1;
	self->think = bigsearch_think;
	self->nextthink = level.time + 5;
}

static void castle_use(edict_t *self, edict_t *, edict_t *)
{
	PropData &d = D(self);
	if (d.i2 || self->think == bigsearch_think) {
		d.i2 = 0;
		self->think = NULL;
		self->nextthink = 0;
		return;
	}
	float t = self->wait / 2 - 5;
	self->think = castle_wake;
	self->nextthink = level.time + (t > 0 ? t : 0) + 0.001f;
}

static void BigSetup(edict_t *e, bool castle)
{
	PropData &d = D(e);
	d.v0[2] = st.maxyaw;
	d.i3 = castle ? TK_CASTLE : TK_BIG;
	const char *skin;
	if (castle)
		skin = e->s.skinnum == 1 ? "gun" : e->s.skinnum == 2 ? "gun2" : "gun_dekker";
	else
		skin = e->s.skinnum == 1 ? "gun2" : "gun";
	int T = game.skill_toughness;
	if (castle) {
		Force(e, 3);
		e->health = 150 * T + 200;
		SetBox(e, -56, -37, -29, 24, 38, 19);
	} else {
		Force(e, 2);
		e->health = 40 * (5 * T + 10);
		SetBox(e, -111, -74, -58, 47, 75, 38);
	}
	Setup(e, T_GENERIC, 53, skin);
	if (castle)
		gg::ScaleEnt(e, 0.5f);
	d.inst0 = Bolt(e, T_GENERIC, 53, "DUMMY01", 56, "DUMMY01", skin);
	const ModelEntry *sh = Entry(T_GENERIC, 56), *gr = Entry(T_GENERIC, 55), *gl = Entry(T_GENERIC, 54);
	gg::AddTrackedBoltOnBolt(e, d.inst0, Ref(sh), "DUMMY02", Ref(gr), "DUMMY02", skin);
	gg::AddTrackedBoltOnBolt(e, d.inst0, Ref(sh), "DUMMY04", Ref(gr), "DUMMY02", skin);
	gg::AddTrackedBoltOnBolt(e, d.inst0, Ref(sh), "DUMMY03", Ref(gl), "DUMMY03", skin);
	gg::AddTrackedBoltOnBolt(e, d.inst0, Ref(sh), "DUMMY05", Ref(gl), "DUMMY03", skin);
	TurretCommon(e);
	d.f2 = e->volume ? e->volume : 400;
	if (!e->wait)
		e->wait = castle ? 15 : 30;
	d.f1 = castle ? (e->speed ? e->speed : 45) / 10.0f * DEG : 0.04f;
	e->die = gun_die;
}

static void SP_gun_castle(edict_t *e)
{
	GunPrecacheAuto(true);
	BigSetup(e, true);
	if ((e->spawnflags & 4) && (e->spawnflags & 16))
		e->use = castle_use;
	if ((e->spawnflags & 4) && !(e->spawnflags & 32)) {
		float t = e->wait / 2 - 5;
		e->think = castle_wake;
		e->nextthink = level.time + (t > 0 ? t : 0) + 0.001f;
	}
}

static void SP_gun_big(edict_t *e)
{
	fx::Index("weapons/othermz/autogun");
	BigSetup(e, false);
	if (e->spawnflags & 4) {
		e->takedamage = DAMAGE_YES;
		e->think = bigsearch_think;
		e->nextthink = level.time + e->wait * 0.1f;
	}
}

static const SpawnEntry s_spawns[] = {
	{"misc_generic_gun_auto", SP_gun_auto},
	{"misc_generic_gun_auto2", SP_gun_auto2},
	{"misc_generic_gun_auto3", SP_gun_auto3},
	{"misc_test_gun_auto", SP_test_gun_auto},
	{"misc_generic_gun_castle", SP_gun_castle},
	{"misc_generic_gun_big", SP_gun_big},
	{NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
