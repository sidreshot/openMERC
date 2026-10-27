// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — environment effect entities (environ_*).
// Implements: SPEC-771 A (A.0-A.3), SPEC-720 §14.3 (cone damage of the steam generator)

#include "fx.h"
#include "../props/props.h"
#include "../damage/damage_api.h"

namespace fx {

attack_fn g_fire_attack = NULL;
scripted_kill_fn g_arm_scripted_kill = NULL;

using props::D;
using props::frand;
using props::irand;

// A.0 standard environ setup
static void EnvironSetup(edict_t *ent)
{
	ent->movetype = MOVETYPE_NONE;
	if (ent->solid != SOLID_TRIGGER)
		ent->solid = SOLID_NOT;
	ent->clipmask = 0x02030003;
	props::SetBox(ent, -5, -5, -7, 5, 5, 12);
	props::RotateBox(ent);
	ent->s.modelindex = 0;
	gi.linkentity(ent);
}

static void SetBrushModel(edict_t *ent)
{
	if (ent->model && ent->model[0])
		gi.setmodel(ent, ent->model);
	ent->s.modelindex = 0;
}

static void BrushCentre(edict_t *ent, bool top)
{
	vec3_t c;
	for (int i = 0; i < 3; i++)
		c[i] = (ent->absmin[i] + ent->absmax[i]) * 0.5f;
	if (top)
		c[2] = ent->absmax[2];
	VectorCopy(c, ent->s.origin);
	gi.linkentity(ent);
}

// Slot effects of the environment generators: D(ent).i3 = which effect (SlotName), D(ent).i0 = its effect index once
// registered, D(ent).f0 = scale. The name is registered when the effect is first put in a slot, not at spawn
// (SPEC-771 C.1 "registered on first use"; black-box: ger3's start-off steam generators register
// `environ/normalsteam` only later in the original)
enum { SLOT_NONE, SLOT_STEAM, SLOT_SMOKE, SLOT_STREAM, SLOT_SOUNDNAME, SLOT_SNOWCHUTE, SLOT_WATERDRIP, SLOT_BLOODRAIN };

static const char *SlotName(edict_t *self)
{
	switch (D(self).i3) {
	case SLOT_STEAM: return "environ/normalsteam";
	case SLOT_SMOKE: return "environ/normalsmoke";
	case SLOT_STREAM: return "environ/stream";
	case SLOT_SOUNDNAME: return self->soundname;
	case SLOT_SNOWCHUTE: return "environ/snowchute";
	case SLOT_WATERDRIP: return "environ/waterdrip";
	case SLOT_BLOODRAIN: return "environ/bloodrain";
	}
	return NULL;
}

static bool SlotOn(edict_t *self) { return D(self).i0 && SlotHas(self, D(self).i0); }

static void SlotStart(edict_t *self)
{
	props::PropData &d = D(self);
	if (!d.i0) {
		const char *n = SlotName(self);
		if (n && n[0])
			d.i0 = Index(n);
	}
	SlotAdd(self, d.i0, 0, d.f0);
}

static void SlotStop(edict_t *self)
{
	if (D(self).i0)
		SlotRemove(self, D(self).i0);
}

// toggle use for slot effects
static void Use_ToggleSlot(edict_t *self, edict_t *, edict_t *)
{
	if (SlotOn(self))
		SlotStop(self);
	else
		SlotStart(self);
}

static void SlotEffect(edict_t *ent, int which, float scale, bool start)
{
	props::PropData &d = D(ent);
	d.i3 = which;
	d.i0 = 0;
	d.f0 = scale;
	// registered at spawn, also when starting off (ger3: start-off `environ_effect_continual` jz_rocket_* are
	// registered with the spawn); only a start-off steam generator registers at its first think (steam_think)
	if (which != SLOT_STEAM) {
		const char *n = SlotName(ent);
		if (n && n[0])
			d.i0 = Index(n);
	}
	if (start)
		SlotStart(ent);
}

// ---------------------------------------------------------------------------------------------------------------

static void SP_environ_snow(edict_t *ent)
{
	EnvironSetup(ent);
	SlotEffect(ent, SLOT_SNOWCHUTE, 0, true);
}

static void playersnow_use(edict_t *self, edict_t *, edict_t *activator)
{
	if (!activator || !activator->inuse)
		return;
	int fx = Index("environ/playersnow");
	if (self->count)
		SlotAdd(activator, fx, 0, 0);
	else
		SlotRemove(activator, fx);
}

static void SP_environ_player_snow(edict_t *ent)
{
	EnvironSetup(ent);
	ent->use = playersnow_use;
}

// steam: i0 effect, f0 scale, f1 toggle time T, f2 height, v0 cone axis
static void steam_on(edict_t *self) { SlotStart(self); }
static void steam_off(edict_t *self) { SlotStop(self); }
static bool steam_is_on(edict_t *self) { return SlotOn(self); }

static void steam_think(edict_t *self)
{
	props::PropData &d = D(self);
	// a start-off generator registers its effect at the first think (black-box: ger3's spawnflag-1 generators
	// register `environ/normalsteam` in the first frame, after the start-on line traps' `environ/tripbeam`)
	if (!d.i0)
		d.i0 = Index("environ/normalsteam");
	if (steam_is_on(self) && self->dmg)
		Dmg_RadiusCone(self, self, (float)self->dmg, d.f2 * 96.0f / 35.0f, self, MOD_UNKNOWN, d.v0, 0.7f);
	if (d.f1 != 0 && d.f1 < level.time) {
		if (steam_is_on(self)) {
			steam_off(self);
			d.f1 = self->delay ? level.time + self->delay : 0;
		} else {
			steam_on(self);
			d.f1 = level.time + self->wait;
		}
	}
	self->nextthink = level.time + FRAMETIME;
}

static void steam_use(edict_t *self, edict_t *, edict_t *)
{
	props::PropData &d = D(self);
	if (steam_is_on(self)) {
		steam_off(self);
		d.f1 = 0;
	} else {
		steam_on(self);
		d.f1 = level.time + self->wait;
	}
}

static void SP_environ_steamgen(edict_t *ent)
{
	EnvironSetup(ent);
	props::PropData &d = D(ent);
	int H = st.height ? st.height : 35;
	d.f2 = (float)H;
	SlotEffect(ent, SLOT_STEAM, (float)(H / 35), !(ent->spawnflags & 1));
	AngleVectors(ent->s.angles, d.v0, NULL, NULL);
	d.f1 = ent->wait == 0 ? 0 : level.time + ent->wait;
	if (ent->spawnflags & 1)
		d.f1 = 0;
	ent->use = steam_use;
	ent->think = steam_think;
	ent->nextthink = level.time + FRAMETIME;
}

static void SP_environ_smokegen(edict_t *ent)
{
	EnvironSetup(ent);
	float scale = st.height ? 60.0f / st.height : 0; // H = 0: infinite scale, stored as 0 = default size
	SlotEffect(ent, SLOT_SMOKE, scale, !(ent->spawnflags & 1));
	ent->use = Use_ToggleSlot;
}

// sparks: i1 style, v0 direction
static void spark_schedule(edict_t *self)
{
	// a time that is already due runs in this frame when the generator comes later in the entity order
	self->nextthink = level.time + self->delay + frand(0, 1) * self->wait - self->wait / 2;
	if (self->nextthink <= 0)
		self->nextthink = 0.001f;
}

static void spark_think(edict_t *self)
{
	props::PropData &d = D(self);
	static const int te[3] = {TE_SPARKS_WHITE, TE_SPARKS_ORANGE, TE_SPARKS};
	if (d.i1 >= 0 && d.i1 <= 2)
		Sparks(te[d.i1], self->s.origin, d.v0); // other styles write a malformed message in 1.07f: not sent
	if (!(self->spawnflags & 2))
		spark_schedule(self);
}

static void spark_use(edict_t *self, edict_t *, edict_t *) { spark_schedule(self); }

static void SP_environ_sparkgen(edict_t *ent)
{
	EnvironSetup(ent);
	props::PropData &d = D(ent);
	d.i1 = st.lip;
	AngleVectors(ent->s.angles, d.v0, NULL, NULL);
	ent->think = spark_think;
	ent->use = spark_use;
	if (!(ent->spawnflags & 3))
		spark_schedule(ent);
}

static void SP_environ_waterglobgen(edict_t *ent)
{
	EnvironSetup(ent);
	SetBrushModel(ent);
	BrushCentre(ent, true);
	SlotEffect(ent, SLOT_WATERDRIP, 0, true);
}

static void SP_environ_raingen(edict_t *ent)
{
	EnvironSetup(ent);
	SetBrushModel(ent);
	BrushCentre(ent, false);
}

static void nop_use(edict_t *, edict_t *, edict_t *) {}

static void SP_environ_rain_worldtrigger(edict_t *ent)
{
	EnvironSetup(ent);
	ent->use = nop_use;
}

static void SP_environ_cloudtrigger(edict_t *ent)
{
	EnvironSetup(ent);
	if (!ent->wait)
		ent->wait = 0.1f;
	if (!ent->count)
		ent->count = 10;
	ent->use = nop_use;
}

static void SP_environ_bloodrain(edict_t *ent)
{
	EnvironSetup(ent);
	// SPEC-721 §13 lists environ_bloodrain under lock_blood, but the original game sends the effect slot with
	// lock_blood 1 (black-box, sud2 single player, 4 entities; OPEN_QUESTIONS G7): never suppressed here
	SlotEffect(ent, SLOT_BLOODRAIN, 0, true);
}

static void SP_environ_linefountain(edict_t *ent)
{
	EnvironSetup(ent);
	SlotEffect(ent, SLOT_STREAM, (float)(ent->count / 50), !(ent->spawnflags & 1));
	ent->use = Use_ToggleSlot;
}

static void SP_environ_splashfountain(edict_t *ent) { EnvironSetup(ent); }

// lightning: f0 = R
static void lightning_think(edict_t *self)
{
	Lightning();
	float R = D(self).f0;
	self->nextthink = level.time + frand(0.5f * R, 1.5f * R);
}

static void lightning_use(edict_t *self, edict_t *, edict_t *)
{
	if (self->nextthink) {
		self->nextthink = 0;
		return;
	}
	float R = D(self).f0;
	self->nextthink = level.time + frand(0.5f * R, 1.5f * R);
}

static void SP_environ_lightning(edict_t *ent)
{
	EnvironSetup(ent);
	float R = ent->random ? ent->random : 10;
	D(ent).f0 = R;
	ent->think = lightning_think;
	ent->use = lightning_use;
	if (!(ent->spawnflags & 1))
		ent->nextthink = level.time + frand(0.5f * R, 1.5f * R);
}

// emergency lights: i0 = L, i1 = step
static void Colour5(float r, float g, float b) { gi.configstring(5, va("%f %f %f", r, g, b)); }

static void emergency_think(edict_t *self)
{
	props::PropData &d = D(self);
	int &L = d.i0;
	switch (d.i1) {
	case 1: // dim
		L -= 10;
		Colour5(L / 100.0f, L / 100.0f, L / 100.0f);
		if (L < 10)
			d.i1 = 2;
		self->nextthink = level.time + FRAMETIME;
		break;
	case 2: // red rise
		L += 10;
		Colour5(L / 100.0f, 0, 0);
		if (L > 70) {
			d.i1 = 4;
			self->nextthink = level.time + frand(2, 10);
		} else
			self->nextthink = level.time + FRAMETIME;
		break;
	case 3: // steady
		Colour5(L / 100.0f, 0, 0);
		d.i1 = 4;
		self->nextthink = level.time + frand(2, 10);
		break;
	default: // flicker
		Colour5(frand(0.001f, 0.005f) * L, 0, 0);
		d.i1 = 3;
		self->nextthink = level.time + FRAMETIME;
		break;
	}
}

static void emergency_use(edict_t *self, edict_t *, edict_t *)
{
	D(self).i1 = 1;
	self->think = emergency_think;
	self->nextthink = level.time + FRAMETIME;
}

static void SP_environ_emergency_lights(edict_t *ent)
{
	EnvironSetup(ent);
	D(ent).i0 = 100;
	Colour5(1, 1, 1);
	ent->use = emergency_use;
}

// sky fade: v0 C0, v1 C1, f0 S, f1 D
static void skyfade_think(edict_t *self)
{
	props::PropData &d = D(self);
	if (d.f0 + d.f1 < level.time) {
		G_FreeEdict(self);
		return;
	}
	float f = d.f1 ? (level.time - d.f0) / d.f1 : 1;
	Colour5(f * d.v1[0] + (1 - f) * d.v0[0], f * d.v1[1] + (1 - f) * d.v0[1], f * d.v1[2] + (1 - f) * d.v0[2]);
	self->nextthink = level.time + (d.f1 * 0.01f > FRAMETIME ? d.f1 * 0.01f : FRAMETIME);
}

static void SP_environ_skyfade(edict_t *ent)
{
	EnvironSetup(ent);
	if (ent->spawnflags & 1)
		return;
	props::PropData &d = D(ent);
	VectorCopy(st.color, d.v0);
	VectorCopy(st.endpoint, d.v1);
	d.f0 = (float)(int)level.time;
	d.f1 = ent->delay;
	ent->think = skyfade_think;
	ent->nextthink = level.time + (d.f1 * 0.01f > FRAMETIME ? d.f1 * 0.01f : FRAMETIME);
}

// explosion: i0 = H
static void explosion_use(edict_t *self, edict_t *, edict_t *)
{
	int H = D(self).i0;
	if (!(self->spawnflags & 2)) {
		props::RadiusDamage(self, self, 2.0f * H, self, (float)H, MOD_UNKNOWN, 0x8000);
		props::Ignite(self, 2.0f * H);
	}
	AINoise(self, self->s.origin, 2.0f * H, 2.0f * H);
	EffectAt(Index("weapons/world/airexplodemute"), self->s.origin);
	gi.positioned_sound(self->s.origin, g_edicts, CHAN_AUTO,
	                    gi.soundindex(va("impact/explosion/exp%d.wav", irand(1, 3))), 1, ATTN_NORM, 0);
	ExplosionDebris(self);
	if (!(self->spawnflags & 1)) {
		vec3_t p;
		VectorCopy(self->s.origin, p);
		p[2] += 10;
		if (H > 0)
			WhiteOut(p, 10.0f * H, 0.9f, 0.5f);
	}
	ShakeTick(self->s.origin, (float)H, 2.0f * H, 100);
}

static void SP_environ_explosion(edict_t *ent)
{
	EnvironSetup(ent);
	D(ent).i0 = ent->health ? ent->health : 150;
	props::MarkSoundList(58);
	props::SetMaterialFlag(19);
	props::SetMaterialFlag(20);
	gi.soundindex("impact/explosion/exp1.wav");
	gi.soundindex("impact/explosion/exp2.wav");
	gi.soundindex("impact/explosion/exp3.wav");
	Index("weapons/world/airexplodemute");
	ent->use = explosion_use;
}

static void smokeburst_use(edict_t *self, edict_t *, edict_t *) { SmokeBurst(self->s.origin, D(self).i0); }

static void SP_environ_smoke_burst(edict_t *ent)
{
	EnvironSetup(ent);
	D(ent).i0 = ent->health ? ent->health : 50;
	ent->use = smokeburst_use;
}

// fire: i0 effect, i1 sound (0 small, 1 medium, 2 big)
static const char *const kFireSounds[3] = {"Ambient/Gen/FireFX/FireSmall.wav", "Ambient/Gen/FireFX/FireMed.wav",
                                           "Ambient/Gen/FireFX/FireBig.wav"};

static void fire_think(edict_t *self) { self->nextthink = level.time + FRAMETIME; }

static void fire_start(edict_t *self)
{
	props::PropData &d = D(self);
	if (!(self->spawnflags & 2)) {
		self->s.sound = gi.soundindex(kFireSounds[d.i1]);
		self->s.sounddata = 0xF8;
	}
	SlotAdd(self, d.i0, 0, 0);
}

static void fire_use(edict_t *self, edict_t *, edict_t *)
{
	props::PropData &d = D(self);
	if (SlotHas(self, d.i0)) {
		SlotRemove(self, d.i0);
		self->s.sound = 0;
	} else
		fire_start(self);
}

static void SP_environ_fire(edict_t *ent)
{
	EnvironSetup(ent);
	props::PropData &d = D(ent);
	int N = ent->health ? ent->health : 12;
	ent->health = N;
	if (!ent->count)
		ent->count = 64;
	// Registration order (black-box, dm/raven test maps g7ord1-4 and ger2): the effect of the size first, the loop
	// sound when the fire starts with sound, then all sounds big, medium, small and all effects firesmall2, gbfire,
	// firelarge2, firelarge3 (SPEC-771 A.2 "always registered at spawn").
	const char *effect;
	if (N <= 3) {
		effect = "environ/firesmall2";
		d.i1 = 0;
	} else if (N <= 10) {
		effect = "gbfire";
		d.i1 = 0;
	} else if (N <= 19) {
		effect = "gbfire";
		d.i1 = 1;
	} else if (N == 20) {
		effect = "environ/firelarge2";
		d.i1 = 1;
	} else if (N <= 29) {
		effect = "environ/firelarge2";
		d.i1 = 2;
	} else {
		effect = "environ/firelarge3";
		d.i1 = 2;
	}
	d.i0 = Index(effect);
	d.f1 = (float)(int)level.time; // remembered start time (no behaviour found)
	if (!(ent->spawnflags & 1))
		fire_start(ent);
	for (int k = 2; k >= 0; k--)
		gi.soundindex(kFireSounds[k]);
	Index("environ/firesmall2");
	Index("gbfire");
	Index("environ/firelarge2");
	Index("environ/firelarge3");
	ent->use = fire_use;
	if (ent->delay) {
		ent->think = fire_think;
		ent->nextthink = level.time + FRAMETIME;
	}
}

// line trap: v0 = E, i0 on, i1 inert
static void linetrap_think(edict_t *self)
{
	props::PropData &d = D(self);
	if (!d.i0)
		return;
	trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, d.v0, self, 0x02010003);
	if (tr.ent && tr.ent != g_edicts && tr.fraction < 1) {
		edict_t *other = tr.ent;
		G_UseTargets(self, other);
		if (self->spawnflags & 1)
			d.i0 = 0;
		if ((self->spawnflags & 4) && other->inuse && other->client) {
			static const vec3_t zero = {0, 0, 0};
			props::Damage(other, self, self, zero, other->s.origin, other->s.origin, self->dmg, self->dmg, 0,
			              MOD_TRIGGER_HURT);
		}
	}
	if (!d.i0)
		return;
	ExtPos2(d.v0);
	EffectAt(Index("environ/tripbeam"), self->s.origin);
	self->nextthink = level.time + FRAMETIME;
}

static void linetrap_use(edict_t *self, edict_t *, edict_t *)
{
	props::PropData &d = D(self);
	d.i0 = !d.i0;
	if (d.i0)
		self->nextthink = level.time + FRAMETIME;
}

static void SP_environ_linetrap(edict_t *ent)
{
	EnvironSetup(ent);
	props::PropData &d = D(ent);
	vec3_t f, end;
	AngleVectors(ent->s.angles, f, NULL, NULL);
	VectorMA(ent->s.origin, 2000, f, end);
	trace_t tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin, end, ent, 0x02010003);
	if (tr.fraction >= 0.99f)
		return; // inert
	VectorCopy(tr.endpos, d.v0);
	if ((ent->spawnflags & 4) && !ent->dmg)
		ent->dmg = 2;
	// `environ/tripbeam` is registered at the first beam (SPEC-771 C.1 "registered on first use"), not here:
	// black-box, jpn2's start-off traps (spawnflags 7) never register it in the original
	ent->think = linetrap_think;
	ent->use = linetrap_use;
	if (!(ent->spawnflags & 2)) {
		d.i0 = 1;
		ent->nextthink = level.time + FRAMETIME;
	}
}

static void SP_environ_inferno(edict_t *ent)
{
	ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_NONE;
	props::SetBox(ent, -5, -5, -7, 5, 5, 12);
	props::RotateBox(ent);
	ent->clipmask = 0x02030003;
	gi.linkentity(ent);
	SetBrushModel(ent);
	ent->health = 100;
	ent->classname = (char *)"environ_inferno";
	BrushCentre(ent, false);
	Index("environ/onfireburst");
}

// invisible attack: f0 interval, i0 attack, f1 jitter, i1 on
static void FireAttack(edict_t *self, int attack, const vec3_t dir)
{
	fx::FireAttack(self, attack, self->s.origin, dir, 500, (self->spawnflags & 2) != 0);
}

static void mouth_shot(edict_t *self)
{
	edict_t *t = self->target ? G_PickTarget(self->target) : NULL;
	if (t && t->ghoulInst) {
		vec3_t bp;
		if (gg::BoltWorld(t, NULL, "sbolt_mouth", bp)) {
			vec3_t dir;
			VectorSubtract(bp, self->s.origin, dir);
			if (g_arm_scripted_kill && (t->svflags & SVF_MONSTER))
				g_arm_scripted_kill(t, 2);
			FireAttack(self, D(self).i0, dir);
		}
	}
	G_FreeEdict(self);
}

static void invattack_think(edict_t *self)
{
	props::PropData &d = D(self);
	if (self->wait == -1) {
		mouth_shot(self);
		return;
	}
	if (!d.i1)
		return;
	vec3_t a, f;
	for (int i = 0; i < 3; i++)
		a[i] = self->s.angles[i] + frand(-d.f1, d.f1);
	AngleVectors(a, f, NULL, NULL);
	FireAttack(self, d.i0, f);
	self->nextthink = level.time + d.f0;
}

static void invattack_use(edict_t *self, edict_t *, edict_t *)
{
	props::PropData &d = D(self);
	if (self->wait == -1) {
		mouth_shot(self);
		return;
	}
	d.i1 = !d.i1;
	self->nextthink = d.i1 ? level.time + d.f0 : 0;
}

static void SP_environ_invisible_attack(edict_t *ent)
{
	EnvironSetup(ent);
	props::PropData &d = D(ent);
	d.f0 = ent->delay ? ent->delay : 0.2f;
	d.i0 = ent->count ? ent->count : 3;
	d.f1 = ent->random ? ent->random : 5;
	ent->think = invattack_think;
	ent->use = invattack_use;
	if (!(ent->spawnflags & 1)) {
		d.i1 = 1;
		ent->nextthink = level.time + d.f0;
	}
}

// train arm
static void trainarm_think(edict_t *self)
{
	if (self->s.origin[0] < -4000) {
		G_FreeEdict(self);
		return;
	}
	vec3_t mins = {-512, -64, 0}, maxs = {512, 64, 64}, p;
	VectorCopy(self->s.origin, p);
	p[1] += 128;
	p[2] += 240;
	trace_t tr = gi.trace(p, mins, maxs, p, NULL, 0x06000081);
	if (tr.ent && tr.ent != g_edicts && tr.ent->inuse) {
		static const vec3_t up = {0, 0, 1};
		props::Damage(tr.ent, self, self, up, tr.ent->s.origin, tr.ent->s.origin, 999, 100, 0, MOD_CRUSH);
	}
	self->nextthink = level.time + FRAMETIME;
}

static void trainarm_use(edict_t *self, edict_t *, edict_t *)
{
	edict_t *a = G_Spawn();
	a->classname = (char *)"environ_trainarm";
	a->movetype = MOVETYPE_PUSH;
	a->solid = SOLID_BBOX;
	props::SetBox(a, -4, -2, -4, 3, 2, 4);
	VectorCopy(self->s.origin, a->s.origin);
	a->s.origin[2] += 1;
	VectorSet(a->velocity, -3500, 0, 0);
	a->health = 100;
	if (gg::SetSimpleModel(a, "objects/uganda/train_arm", "arm") && a->ghoulInst) {
		ghoul::Matrix4 m;
		memset(&m, 0, sizeof(m));
		float c = cosf(3 * (float)M_PI / 2), s = sinf(3 * (float)M_PI / 2);
		// scale 1.5, then a rotation of 270 degrees about the third Euler axis, then translate (0, 128, 176)
		m.m[0][0] = 1.5f * c;
		m.m[0][1] = 1.5f * s;
		m.m[1][0] = -1.5f * s;
		m.m[1][1] = 1.5f * c;
		m.m[2][2] = 1.5f;
		m.m[3][1] = 128;
		m.m[3][2] = 176;
		m.m[3][3] = 1;
		a->ghoulInst->SetTransform(&m);
	}
	a->think = trainarm_think;
	a->nextthink = level.time + FRAMETIME;
	gi.linkentity(a);
}

static void SP_environ_trainarm_spawn(edict_t *ent)
{
	EnvironSetup(ent);
	ent->use = trainarm_use;
}

// dust source: i0 A, f0 P
static void dust_act(edict_t *self)
{
	int A = D(self).i0;
	if (!(self->spawnflags & 1)) {
		Dust(self->s.origin, A);
		return;
	}
	vec3_t end;
	VectorCopy(self->s.origin, end);
	end[2] += 512;
	trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, 3);
	static const vec3_t down = {0, 0, -1};
	bool stage = tr.surface && gix.damagetexture ? gix.damagetexture(tr.surface, 25) != 0 : false;
	WeaponImpact(tr, down, A, 1, 1, stage);
}

static void dust_think(edict_t *self)
{
	dust_act(self);
	float P = D(self).f0;
	self->nextthink = level.time + frand(0.8f * P, 1.2f * P);
}

static void dust_use(edict_t *self, edict_t *, edict_t *) { dust_act(self); }

static void SP_environ_dustsource(edict_t *ent)
{
	EnvironSetup(ent);
	props::PropData &d = D(ent);
	d.i0 = ent->health ? ent->health : 5;
	d.f0 = ent->delay ? ent->delay : 10;
	ent->use = dust_use;
	ent->think = dust_think;
	if (d.i0 != -1)
		ent->nextthink = level.time + frand(0.8f * d.f0, 1.2f * d.f0);
}

static void effect_use(edict_t *self, edict_t *, edict_t *)
{
	vec3_t f;
	AngleVectors(self->s.angles, f, NULL, NULL);
	ExtDir(f);
	EffectAt(D(self).i0, self->s.origin);
}

static void SP_environ_effect(edict_t *ent)
{
	EnvironSetup(ent);
	D(ent).i0 = Index(ent->soundname);
	if (ent->soundname && strstr(ent->soundname, "gore") && BloodLocked()) {
		G_FreeEdict(ent);
		return;
	}
	ent->use = effect_use;
}

static void SP_environ_effect_continual(edict_t *ent)
{
	EnvironSetup(ent);
	SlotEffect(ent, SLOT_SOUNDNAME, 0, !(ent->spawnflags & 1));
	ent->use = Use_ToggleSlot;
}

// chunk spewer
static void spewer_spew(edict_t *self)
{
	vec3_t f;
	AngleVectors(self->s.angles, f, NULL, NULL);
	int n = self->count > 0 ? self->count : irand(3, 7);
	Debris(self->s.origin, f, n, 1, 32, 0, 0, 0, 35);
	if (self->count > 0) {
		self->think = G_FreeEdict;
		self->nextthink = level.time + 3;
	} else {
		self->nextthink = level.time + (float)(-self->count) + frand(-0.2f, 0.2f);
	}
}

static void spewer_use(edict_t *self, edict_t *other, edict_t *)
{
	if (other && other->client)
		return; // direct player use does nothing
	spewer_spew(self);
}

static void SP_environ_chunk_spewer(edict_t *ent)
{
	if (!ent->target) {
		gi.dprintf("Object 'environ_chunk_spewer' without a target.\n");
		G_FreeEdict(ent);
		return;
	}
	if (!ent->count) {
		gi.dprintf("Object 'environ_chunk_spewer' has zero chunks.\n");
		G_FreeEdict(ent);
		return;
	}
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	props::SetBox(ent, -4, -4, -4, 4, 4, 4);
	edict_t *t = G_Find(NULL, FOFS(targetname), ent->target);
	if (t) {
		vec3_t d;
		VectorSubtract(t->s.origin, ent->s.origin, d);
		VectorNormalize(d);
		vectoangles(d, ent->s.angles);
	}
	ent->use = spewer_use;
	ent->think = spewer_spew;
	if (ent->count < 1 && !(ent->spawnflags & 1))
		ent->nextthink = level.time + FRAMETIME;
}

static const props::SpawnEntry s_spawns[] = {
	{"environ_bloodrain", SP_environ_bloodrain},
	{"environ_chunk_spewer", SP_environ_chunk_spewer},
	{"environ_cloudtrigger", SP_environ_cloudtrigger},
	{"environ_dustsource", SP_environ_dustsource},
	{"environ_effect", SP_environ_effect},
	{"environ_effect_continual", SP_environ_effect_continual},
	{"environ_emergency_lights", SP_environ_emergency_lights},
	{"environ_explosion", SP_environ_explosion},
	{"environ_fire", SP_environ_fire},
	{"environ_inferno", SP_environ_inferno},
	{"environ_invisible_attack", SP_environ_invisible_attack},
	{"environ_lightning", SP_environ_lightning},
	{"environ_linefountain", SP_environ_linefountain},
	{"environ_linetrap", SP_environ_linetrap},
	{"environ_player_snow", SP_environ_player_snow},
	{"environ_rain_worldtrigger", SP_environ_rain_worldtrigger},
	{"environ_raingen", SP_environ_raingen},
	{"environ_skyfade", SP_environ_skyfade},
	{"environ_smoke_burst", SP_environ_smoke_burst},
	{"environ_smokegen", SP_environ_smokegen},
	{"environ_snow", SP_environ_snow},
	{"environ_sparkgen", SP_environ_sparkgen},
	{"environ_splashfountain", SP_environ_splashfountain},
	{"environ_steamgen", SP_environ_steamgen},
	{"environ_trainarm_spawn", SP_environ_trainarm_spawn},
	{"environ_waterglobgen", SP_environ_waterglobgen},
	{NULL, NULL}};
static props::SpawnTable s_reg(s_spawns);

} // namespace fx
