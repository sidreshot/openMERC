// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — light fixtures (light_generic_*).
// Implements: SPEC-770 §8 (L.1-L.5)

#include "props.h"
#include "../effects/fx.h"

namespace props {

static const vec3_t kUp = {0, 0, 1};

static void Style(edict_t *e, const char *v) { gi.configstring(CS_LIGHTS + e->style, v); }

// Quake II light toggle (configstring step of every use)
static void StyleToggle(edict_t *self)
{
	if (self->spawnflags & 1) {
		Style(self, "m");
		self->spawnflags &= ~1;
	} else {
		Style(self, "a");
		self->spawnflags |= 1;
	}
}

static void q2_light_use(edict_t *self, edict_t *, edict_t *) { StyleToggle(self); }

// model changes: a list of "seq X", "seq_loop X", "pause", "hide P", "show P" steps
struct Change {
	char op; // 's' seq once, 'l' seq loop, 'p' pause, 'h' hide, 'v' show
	const char *name;
};
struct LightClass {
	const char *classname;
	int index;
	float box[6];
	const Change *init, *off, *on; // initial state with spawnflag 1 / off / on
	int kind;                      // 0 plain toggle, 1 bulb-blow death, 2 Quake II toggle only
};

static void Apply(edict_t *e, const Change *c)
{
	for (; c && c->op; c++) {
		switch (c->op) {
		case 's': gg::PlayEnt(e, c->name, false); break;
		case 'l': gg::PlayEnt(e, c->name, true); break;
		case 'p': gg::PauseEnt(e); break;
		case 'h': gg::HidePart(e->ghoulInst, c->name); break;
		case 'v': gg::ShowPart(e->ghoulInst, c->name); break;
		}
	}
}

static const Change c_alley_off[] = {{'s', "light_off"}, {'h', "BEAM"}, {0, NULL}};
static const Change c_alley_on[] = {{'s', "light"}, {'v', "BEAM"}, {0, NULL}};
static const Change c_alleyold_off[] = {{'s', "light_off"}, {'h', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_alleyold_on[] = {{'v', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_caged_off[] = {{'h', "SPRITEBALL_FLASH02"}, {'h', "LIGHTBULB01"}, {0, NULL}};
static const Change c_caged_on[] = {{'v', "SPRITEBALL_FLASH02"}, {'v', "LIGHTBULB01"}, {0, NULL}};
static const Change c_desk_off[] = {{'s', "lamp_off"}, {'h', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_desk_on[] = {{'s', "lamp"}, {'v', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_hidef1[] = {{'h', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_lant2_off[] = {{'s', "lantern2_off"}, {'h', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_lant2_on[] = {{'s', "lantern2"}, {'v', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_lant3_off[] = {{'s', "lantern3"}, {'h', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_lant3_on[] = {{'s', "lantern3"}, {'v', "SPRITEBALL_FLASH01"}, {0, NULL}};
static const Change c_mil_off[] = {{'h', "SPRITEBALL_FLASH01"}, {'h', "LIGHT_BULB01"}, {0, NULL}};
static const Change c_mil_on[] = {{'v', "SPRITEBALL_FLASH01"}, {'v', "LIGHT_BULB01"}, {0, NULL}};
static const Change c_milo_off[] = {{'h', "SPRITEBALL_FLASH01"}, {'h', "BEAM01"}, {'h', "LIGHT_BULB01"}, {0, NULL}};
static const Change c_milo_on[] = {{'v', "SPRITEBALL_FLASH01"}, {'v', "BEAM01"}, {'v', "LIGHT_BULB01"}, {0, NULL}};
static const Change c_mod_off[] = {{'s', "modern_off"}, {'h', "SPRITEBALL_GLOW"}, {'h', "BEAM"}, {0, NULL}};
static const Change c_mod_on[] = {{'v', "SPRITEBALL_GLOW"}, {'v', "BEAM"}, {'s', "modern"}, {0, NULL}};
static const Change c_old_off[] = {{'s', "off"}, {'h', "SPRITEBALL_FLASH"}, {0, NULL}};
static const Change c_old_on[] = {{'v', "SPRITEBALL_FLASH"}, {'s', "old"}, {0, NULL}};

static const LightClass s_plain[] = {
	{"light_generic_alley_wall", 0, {-48, -41, -53, 38, 41, 21}, c_alley_off, c_alley_off, c_alley_on, 0},
	{"light_generic_alley_wall_old", 1, {-28, -4, -13, 4, 4, 9}, NULL, c_alleyold_off, c_alleyold_on, 0},
	{"light_generic_caged", 2, {-3, -3, -10, 3, 3, 5}, c_caged_off, c_caged_off, c_caged_on, 1},
	{"light_generic_desk_lamp", 9, {-6, -6, -5, 6, 6, 5}, NULL, c_desk_off, c_desk_on, 0},
	{"light_generic_halfshell", 12, {-2, -7, -2, 2, 7, 2}, NULL, NULL, NULL, 2},
	{"light_generic_lantern2", 15, {-8, -7, -9, 8, 7, 8}, c_hidef1, c_lant2_off, c_lant2_on, 1},
	{"light_generic_lantern3", 16, {-7, -7, -13, 4, 7, 8}, c_hidef1, c_lant3_off, c_lant3_on, 0},
	{"light_generic_military_door", 17, {-6, -6, -2, 6, 9, 3}, NULL, c_mil_off, c_mil_on, 0},
	{"light_generic_military_door_outside", 18, {-19, -22, -8, 19, 27, 10}, NULL, c_milo_off, c_milo_on, 0},
	{"light_generic_street_modern", 20, {-88, -75, -218, 75, 75, 17}, NULL, c_mod_off, c_mod_on, 0},
	{"light_generic_street_old", 21, {-7, -7, -48, 7, 7, 48}, NULL, c_old_off, c_old_on, 0},
	{"light_generic_candelabra_small", 7, {-10, -5, -17, 10, 5, 0}, NULL, NULL, NULL, 2},
	{NULL, 0, {0}, NULL, NULL, NULL, 0}};

static const LightClass *Find(edict_t *e, const LightClass *t)
{
	for (; t->classname; t++)
		if (!strcmp(t->classname, e->classname))
			return t;
	return NULL;
}

// initial light style (L.1)
static void InitStyle(edict_t *e, bool ownuse)
{
	if (e->style >= 32) {
		Style(e, (e->spawnflags & 1) ? "a" : "m");
		if (!ownuse)
			e->use = q2_light_use;
	}
}

// toggle with model changes: i3 = index into s_plain (or -1)
static void plain_use(edict_t *self, edict_t *, edict_t *)
{
	const LightClass *c = &s_plain[D(self).i3];
	Apply(self, (self->spawnflags & 1) ? c->on : c->off);
	StyleToggle(self);
}

// bulb blow (L.2.3)
static void bulb_dark(edict_t *self)
{
	Style(self, "a");
	self->health = 50;
	self->die = Die_StandardBreak;
}

static void bulb_wait_die(edict_t *, edict_t *, edict_t *, int, vec3_t) {}

static void bulb_die(edict_t *self, edict_t *, edict_t *attacker, int damage, vec3_t)
{
	bool on = !(self->spawnflags & 1);
	if (self->health - damage < -100) {
		fx::Sparks(fx::TE_SPARKS, self->s.origin, kUp);
		StandardBreak(self, attacker, 0, 0);
		return;
	}
	if (on) {
		fx::Sparks(fx::TE_SPARKS, self->s.origin, kUp);
		Style(self, "z");
	}
	self->die = bulb_wait_die;
	self->think = bulb_dark;
	self->nextthink = level.time + 0.2f;
	if (on)
		gg::HidePart(self->ghoulInst, "SPRITEBALL_FLASH01");
}

static void SP_plain(edict_t *e)
{
	const LightClass *c = Find(e, s_plain);
	if (!c)
		return;
	const char *skin = NULL;
	if (c->index == 9)
		skin = e->s.skinnum == 1 ? "lightsilver" : "light";
	SetBox(e, c->box[0], c->box[1], c->box[2], c->box[3], c->box[4], c->box[5]);
	Setup(e, T_LIGHTS, c->index, skin);
	D(e).i3 = (int)(c - s_plain);
	if ((e->spawnflags & 1) && c->init)
		Apply(e, c->init);
	if (c->kind == 2)
		InitStyle(e, false);
	else {
		InitStyle(e, true);
		e->use = plain_use;
	}
	if (c->kind == 1 && e->takedamage)
		e->die = bulb_die;
}

// ---------------------------------------------------------------------------------------------------------------
// L.3 swinging and hanging fixtures

static void Ricochet(edict_t *self)
{
	PlaySound(self, CHAN_VOICE, va("Weapons/FX/Rics/ric%d.wav", irand(1, 3)), 0.6f, ATTN_NORM);
}

static void ForceDamageable(edict_t *e)
{
	if (!(e->spawnflags & 2))
		e->health = 999999;
	e->spawnflags |= 2;
}

// chandelier
static void chandelier_check(edict_t *self)
{
	int v = (int)(fx::RotationValue(self, 3) * 0.75f);
	if (abs(v) < 153.6f) {
		fx::RotationClear(self);
		return;
	}
	fx::RotationSetValue(self, 3, v);
	self->nextthink = level.time + 0.5f;
}

static void chandelier_pain(edict_t *self, edict_t *other, float, int damage, const vec3_t p)
{
	gg::PlayEnt(self, "swing", true);
	vec3_t d, h, c;
	const float *src = other ? other->s.origin : self->s.origin;
	VectorSubtract(src, self->s.origin, d);
	VectorNormalize(d);
	VectorSubtract(p, self->s.origin, h);
	CrossProduct(d, h, c);
	if (c[2] != 0) {
		int s = c[2] < 0 ? 1 : -1;
		if (!fx::RotationActive(self)) {
			float rate[3] = {0, 0, s * 5.0f};
			int inst[4] = {self->ghoulInst ? self->ghoulInst->GetInstanceId() : 0, 0, 0, 0};
			fx::RotationSet(self, rate, inst);
		} else {
			int v = (int)(fx::RotationValue(self, 3) + s * 0.1f * damage * 512);
			if (v > 2560)
				v = 2560;
			if (v < -2560)
				v = -2560;
			fx::RotationSetValue(self, 3, v);
		}
	}
	self->think = chandelier_check;
	self->nextthink = level.time + 0.5f;
}

static void chandelier_use(edict_t *self, edict_t *, edict_t *)
{
	static const char *f[4] = {"SPRITEBALL_FLASH01", "SPRITEBALL_FLASH02", "SPRITEBALL_FLASH03",
	                           "SPRITEBALL_FLASH04"};
	bool on = !(self->spawnflags & 1);
	for (int i = 0; i < 4; i++) {
		if (on)
			gg::HidePart(self->ghoulInst, f[i]);
		else
			gg::ShowPart(self->ghoulInst, f[i]);
	}
	StyleToggle(self);
}

static void SP_chandelier(edict_t *e)
{
	ForceDamageable(e);
	SetBox(e, -24, -21, 0, 25, 21, 51);
	Setup(e, T_LIGHTS, 8, e->s.skinnum == 1 ? "light2" : "light");
	gg::PauseEnt(e);
	InitStyle(e, true);
	e->use = chandelier_use;
	SetPain(e, chandelier_pain);
}

// hanging / lantern / shop: i0 = 1 hanging, 2 lantern, 3 shop
static void hang_apply(edict_t *self, bool on)
{
	switch (D(self).i0) {
	case 1:
		gg::PlayEnt(self, on ? "hanging" : "hanging_off", false);
		gg::PauseEnt(self);
		if (on)
			gg::ShowPart(self->ghoulInst, "SPRITEBALL_FLASH01");
		else
			gg::HidePart(self->ghoulInst, "SPRITEBALL_FLASH01");
		break;
	case 2:
		gg::PlayEnt(self, on ? "lantern" : "lantern_off", false);
		gg::PauseEnt(self);
		break;
	default:
		gg::PlayEnt(self, on ? "shop" : "shop_off", false);
		gg::PauseEnt(self);
		break;
	}
}

static void hang_use(edict_t *self, edict_t *, edict_t *)
{
	hang_apply(self, (self->spawnflags & 1) != 0);
	StyleToggle(self);
}

static void hang_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	bool on = !(self->spawnflags & 1);
	static const char *seq[4][2] = {{NULL, NULL}, {"hanging_off", "hanging"}, {"lantern_off", "lantern"},
	                                {"shop_off", "shop"}};
	int k = D(self).i0;
	if (k != 2)
		Ricochet(self);
	gg::PlayEnt(self, seq[k][on ? 1 : 0], false);
}

static void HangCommon(edict_t *e, int kind, int index, const float *box, const char *skin)
{
	ForceDamageable(e);
	SetBox(e, box[0], box[1], box[2], box[3], box[4], box[5]);
	Setup(e, T_LIGHTS, index, skin);
	D(e).i0 = kind;
	if (kind == 1 && (e->spawnflags & 1)) {
		gg::PlayEnt(e, "hanging_off", false);
		gg::PauseEnt(e);
		gg::HidePart(e->ghoulInst, "SPRITEBALL_FLASH01");
	} else
		gg::PauseEnt(e);
	InitStyle(e, true);
	e->use = hang_use;
	SetPain(e, hang_pain);
}

static void SP_hanging(edict_t *e)
{
	static const float b[6] = {-10, -10, -6, 10, 10, 44};
	HangCommon(e, 1, 13, b, NULL);
}
static void SP_lantern(edict_t *e)
{
	static const float b[6] = {-7, -6, -22, 7, 6, 22};
	HangCommon(e, 2, 14, b, NULL);
}
static void SP_shop(edict_t *e)
{
	static const float b[6] = {-19, -23, -4, 19, 22, 32};
	HangCommon(e, 3, 19, b, e->s.skinnum == 1 ? "light2" : "light");
}

// lamp_hang
static void lamphang_rest(edict_t *self)
{
	if (self->spawnflags & 16)
		gg::PauseEnt(self);
	else
		gg::PlayEnt(self, "lamp", true);
}
static gg::NoteCallback s_lamphang_eos(lamphang_rest);
gg::NoteCallback *NoteLampHang(void) { return &s_lamphang_eos; }

static void lamphang_start(edict_t *self) { gg::PlayEnt(self, "lamp", true); }

static void lamphang_use(edict_t *self, edict_t *, edict_t *)
{
	bool loop = !(self->spawnflags & 16);
	if (!(self->spawnflags & 1)) {
		gg::HidePart(self->ghoulInst, "SPRITEBALL_FLASH01");
		gg::HidePart(self->ghoulInst, "SPRITEBALL_FLASH02");
		gg::PlayEnt(self, "lamp_off", loop);
	} else {
		gg::ShowPart(self->ghoulInst, "SPRITEBALL_FLASH01");
		gg::ShowPart(self->ghoulInst, "SPRITEBALL_FLASH02");
		gg::PlayEnt(self, "lamp", loop);
	}
	StyleToggle(self);
}

static void lamphang_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	if (self->spawnflags & 8)
		return;
	gg::PlayEnt(self, (self->spawnflags & 1) ? "shot_off" : "shot", false);
	gg::OnNote(self, NULL, "EOS", &s_lamphang_eos);
}

static void SP_lamp_hang(edict_t *e)
{
	ForceDamageable(e);
	SetBox(e, -6, -7, -10, 5, 7, 15);
	Setup(e, T_LIGHTS, 22, NULL);
	if (e->spawnflags & 8) {
		gg::HidePart(e->ghoulInst, "HANDLE_LANTERN");
		gg::HidePart(e->ghoulInst, "ROPE_LANTERN");
		gg::PauseEnt(e);
		gi.linkentity(e);
	} else if (e->spawnflags & 16)
		gg::PauseEnt(e);
	else {
		e->think = lamphang_start;
		e->nextthink = level.time + (rand() % 5);
	}
	InitStyle(e, true);
	e->use = lamphang_use;
	SetPain(e, lamphang_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// L.4 emergency lights

static void emergency_restart(edict_t *self)
{
	if (self->ghoulInst)
		gg::PlayInstAt(self->ghoulInst, "emergency", ghoul::kPlayLoop, level.time, 0);
}

static void emergency_use(edict_t *self, edict_t *, edict_t *)
{
	if (!(self->spawnflags & 1)) {
		gg::PlayEnt(self, "emergency_off", false);
		StopLoop(self);
		self->think = NULL;
	} else {
		gg::PlayEnt(self, "emergency", true);
		LoopSound(self, "Ambient/Models/Alarm/alarm1.wav");
		self->think = emergency_restart;
		self->nextthink = level.time + irand(0, 9) * 0.1f;
	}
	StyleToggle(self);
}

static void SP_emergency(edict_t *e)
{
	Sound("Ambient/Models/Alarm/alarm1.wav");
	SetBox(e, -6, -6, -5, 6, 6, 5);
	Setup(e, T_LIGHTS, strcmp(e->classname, "light_generic_emergency_blue") ? 10 : 11, NULL);
	gg::ScaleEnt(e, 1.75f);
	if (!(e->spawnflags & 1)) {
		LoopSound(e, "Ambient/Models/Alarm/alarm1.wav");
		e->think = emergency_restart;
		e->nextthink = level.time + irand(0, 4) * 0.1f;
	} else
		gg::PlayEnt(e, "emergency_off", false);
	InitStyle(e, true);
	e->use = emergency_use;
}

// ---------------------------------------------------------------------------------------------------------------
// L.5 candelabra

static void candle_react(edict_t *self, int part, edict_t *, const vec3_t)
{
	vec3_t at;
	PartPos(self, part, at);
	fx::Debris(at, kUp, 5, 1, 31, 0, 0, 0, 35);
	fx::Puff(at, 120, 120, 120, 200);
}

static const PartRow kCandleParts[] = {
	{"DUMMY01", 1, candle_react}, {"DUMMY02", 2, candle_react}, {"DUMMY03", 3, candle_react}};

static void candelabra_pain(edict_t *self, edict_t *other, float, int, const vec3_t p)
{
	ShootableParts(self, other, p);
}

static void candelabra_use(edict_t *self, edict_t *, edict_t *)
{
	bool on = !(self->spawnflags & 1);
	PropData &d = D(self);
	for (int i = 1; i <= 3; i++) {
		gg::IGhoulInst *c = gg::TrackedPart(self, i);
		if (on) {
			gg::HidePart(c, "SPRITEBALL_FLASH03");
			gg::HidePart(c, "SPRITEBALL_FLAME01");
		} else if (gg::PartShown(self, i)) {
			gg::ShowPart(c, "SPRITEBALL_FLASH03");
			gg::ShowPart(c, "SPRITEBALL_FLAME01");
		}
	}
	StyleToggle(self);
}

static void SP_candelabra(edict_t *e)
{
	Force(e, 2);
	SetBox(e, -11, -11, -55, 11, 11, 23);
	Setup(e, T_LIGHTS, 6, e->s.skinnum == 1 ? "candle2" : "candle");
	int r = irand(0, 2);
	Bolt(e, T_LIGHTS, 6, "DUMMY01", 3 + (r % 3), "DUMMY01");
	Bolt(e, T_LIGHTS, 6, "DUMMY02", 3 + ((r + 1) % 3), "DUMMY01");
	Bolt(e, T_LIGHTS, 6, "DUMMY03", 3 + ((r + 2) % 3), "DUMMY01");
	D(e).parts = kCandleParts;
	D(e).nparts = 3;
	SetPain(e, candelabra_pain);
	InitStyle(e, true);
	e->use = candelabra_use;
}

static const SpawnEntry s_spawns[] = {
	{"light_generic_alley_wall", SP_plain},
	{"light_generic_alley_wall_old", SP_plain},
	{"light_generic_caged", SP_plain},
	{"light_generic_candelabra", SP_candelabra},
	{"light_generic_candelabra_small", SP_plain},
	{"light_generic_chandelier", SP_chandelier},
	{"light_generic_desk_lamp", SP_plain},
	{"light_generic_emergency", SP_emergency},
	{"light_generic_emergency_blue", SP_emergency},
	{"light_generic_halfshell", SP_plain},
	{"light_generic_hanging", SP_hanging},
	{"light_generic_lamp_hang", SP_lamp_hang},
	{"light_generic_lantern", SP_lantern},
	{"light_generic_lantern2", SP_plain},
	{"light_generic_lantern3", SP_plain},
	{"light_generic_military_door", SP_plain},
	{"light_generic_military_door_outside", SP_plain},
	{"light_generic_shop", SP_shop},
	{"light_generic_street_modern", SP_plain},
	{"light_generic_street_old", SP_plain},
	{NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
