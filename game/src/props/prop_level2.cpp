// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — level prop families: New York, Siberia, Tokyo, Uganda.
// Implements: SPEC-770 §7B

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

// ---------------------------------------------------------------------------------------------------------------
// New York

static void SP_burned_car(edict_t *e) { T(e, T_NEWYORK, 0, -91, -33, -25, 91, 33, 25); }

static void gumball_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	if (self->s.skinnum == 1)
		return;
	gg::SetSkinInst(self->ghoulInst, "objects/newyork/gumball", "gumball", "gumball", "gumball2");
	self->s.skinnum = 1;
	vec3_t p;
	VectorCopy(self->s.origin, p);
	p[2] += 10;
	fx::Debris(p, kZero, 5, 1, 7, 0, 0, 0, 37);
	static const int te[3] = {fx::TE_LITSPARKS, fx::TE_SPARKSHOWER, fx::TE_LITSPARKS};
	for (int i = 0; i < 3; i++) {
		vec3_t d = {(float)irand(-21, 20), (float)irand(-21, 20), 0};
		fx::Sparks(te[i], p, d);
	}
	PlaySound(self, 5, "Impact/GlassBreak/GBSmall.wav", 0.6f, ATTN_NORM);
	self->health = 1;
}

static void SP_gumball(edict_t *e)
{
	SetMaterialFlag(1);
	Sound("Impact/GlassBreak/GBSmall.wav");
	// model with all its skins (original object: gumball, gumball2), then the skin
	T(e, T_NEWYORK, 1, -8, -8, -19, 8, 8, 19);
	SkinAfterSetup(e, e->s.skinnum == 1 ? "gumball2" : "gumball");
	SetPain(e, gumball_pain);
}

// i0 = first pain done
static void news_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	PropData &d = D(self);
	if (d.i0) {
		fx::Paper(self->s.origin, 3);
		return;
	}
	d.i0 = 1;
	vec3_t f, r, q, s;
	AngleVectors(self->s.angles, f, r, NULL);
	VectorMA(self->s.origin, 10, f, q);
	fx::Paper(q, 3);
	fx::Debris(q, kZero, 5, 1, 7, 0, 0, 0, 37);
	VectorMA(self->s.origin, 15, r, s);
	s[2] += 10;
	fx::Sparks(fx::TE_SPARKSHOWER, s, f);
	gg::PlayEnt(self, "news", false);
}

static void SP_news_machine(edict_t *e)
{
	SetMaterialFlag(1);
	SetMaterialFlag(22);
	T(e, T_NEWYORK, 2, -21, -35, -20, 21, 12, 19);
	gg::PauseEnt(e);
	SetPain(e, news_pain);
}

static float s_trashcan_timer = -999;
void TrashcanReset(void) { s_trashcan_timer = -999; }

static void trashcan_pain(edict_t *self, edict_t *, float, int damage, const vec3_t p)
{
	if (level.time < s_trashcan_timer + 0.5f)
		return;
	s_trashcan_timer = level.time;
	if (p[2] > self->s.origin[2] + 10) {
		gg::PlayEnt(self, "trashcan", false);
		self->health += damage;
		fx::PointTE(fx::TE_LIDSPIN, self->s.origin);
	}
}

void GarbageBreak(edict_t *self, edict_t *inflictor, edict_t *attacker); // prop_generic1.cpp

static void trashcan_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int, vec3_t)
{
	for (int i = 0; i < 3; i++)
		fx::Puff(self->s.origin, 200, 200, 200, 250);
	GarbageBreak(self, inflictor, attacker);
}

static void SP_trashcan(edict_t *e)
{
	Sound("Ambient/Models/TrashCan/LidSpin.wav");
	SetMaterialFlag(21);
	SetMaterialFlag(22);
	T(e, T_NEWYORK, 4, -10, -10, -18, 10, 10, 20);
	gg::PlayEnt(e, "trashcan", false);
	e->die = trashcan_die;
	SetPain(e, trashcan_pain);
}

static void pileitem_remove(edict_t *self) { G_FreeEdict(self); }

static void trashpile_pain(edict_t *self, edict_t *, float, int, const vec3_t)
{
	static const char *parts[4] = {"_CAN1", "_CAN2", "_SHOE", "_OILCAN"};
	static const char *bolts[4] = {"DUMMY01", "DUMMY02", "DUMMY03", "DUMMY04"};
	int c = self->count;
	PlaySound(self, CHAN_VOICE, va("Weapons/FX/Rics/ric%d.wav", irand(1, 3)), 0.6f, ATTN_NORM);
	if (c >= 0 && c <= 3) { // a counter above 3 names no part in 1.07f (original defect): nothing is thrown
		gg::HidePart(self->ghoulInst, parts[c]);
		edict_t *it = G_Spawn();
		it->classname = self->classname;
		ClearData(it);
		VectorCopy(self->s.origin, it->s.origin);
		it->s.origin[2] += 30;
		it->spawnflags = 0;
		spawn_temp_t save = st;
		memset(&st, 0, sizeof(st));
		T(it, T_NEWYORK, 3, -5, -5, -5, 5, 5, 5, NULL, parts[c]);
		st = save;
		VectorSet(it->velocity, frand(-200, 200), frand(-200, 200), frand(50, 150));
		it->think = pileitem_remove;
		it->nextthink = level.time + frand(0, 30);
		vec3_t bp;
		gg::BoltWorld(self, NULL, bolts[c], bp);
		fx::Puff(bp, 200, 200, 200, 250);
	}
	self->count++;
	if (self->count >= 4)
		G_FreeEdict(self);
}

static void SP_trash_pile(edict_t *e)
{
	Force(e, 2);
	T(e, T_NEWYORK, 3, -10, -11, -6, 10, 11, 18);
	SetPain(e, trashpile_pain);
}

// ---------------------------------------------------------------------------------------------------------------
// Siberia

static void SP_carton1(edict_t *e) { T(e, T_SIBERIA, 0, -15, -15, -15, 15, 15, 15); }
static void SP_carton2(edict_t *e) { T(e, T_SIBERIA, 1, -8, -8, -7, 8, 8, 7); }
static void SP_carton3(edict_t *e) { T(e, T_SIBERIA, 2, -3, -3, -3, 3, 3, 4); }
static void SP_carton4(edict_t *e) { T(e, T_SIBERIA, 3, -10, -10, -15, 10, 10, 15); }
static void SP_sib_flag(edict_t *e)
{
	Force(e, 2);
	T(e, T_SIBERIA, 4, -16, -1, -1, 1, 1, 79);
}
static void SP_icicles(edict_t *e)
{
	Force(e, 2);
	T(e, T_SIBERIA, 5, -1, -41, -41, 1, 41, 41);
}

static void roadflare_on(edict_t *self)
{
	LoopSound(self, "Ambient/Gen/FireFX/FlareLP.wav");
	fx::SlotAdd(self, fx::Index((self->spawnflags & 8) ? "environ/roadflare_blue" : "environ/roadflare"), 0, 0);
}

static void roadflare_use(edict_t *self, edict_t *, edict_t *)
{
	if (!(self->spawnflags & 4)) {
		self->spawnflags |= 4;
		StopLoop(self);
		fx::SlotRemove(self, fx::Index((self->spawnflags & 8) ? "environ/roadflare_blue" : "environ/roadflare"));
	} else {
		self->spawnflags &= ~4;
		roadflare_on(self);
	}
}

static void SP_roadflare(edict_t *e)
{
	Force(e, 2);
	Sound("Ambient/Gen/FireFX/FlareLP.wav");
	T(e, T_SIBERIA, 7, -2, -2, -2, 2, 2, 2);
	gg::AllParts(e->ghoulInst, true);
	e->use = roadflare_use;
	if (!(e->spawnflags & 4))
		roadflare_on(e);
}

static void SP_sib_searchlight(edict_t *e)
{
	Force(e, 2);
	T(e, T_SIBERIA, 9, -10, -10, -8, 10, 10, 12);
	e->touch = NULL;
	gg::IGhoulInst *lamp = Bolt(e, T_SIBERIA, 9, "DUMMY02", 8, "DUMMY02");
	edict_t *t = e->target ? G_Find(NULL, FOFS(targetname), e->target) : NULL;
	if (!t)
		return;
	gg::AimParams a;
	gg::AimDefaults(a);
	a.yaw = true;
	a.minyaw = a.minpitch = -4000;
	a.maxyaw = a.maxpitch = 4000;
	a.step = 4000;
	gg::Aim(e, a, t->s.origin);
	gi.linkentity(e);
	if (lamp) {
		gg::AimParams p;
		gg::AimDefaults(p);
		p.pitch = true;
		p.minyaw = p.minpitch = -4000;
		p.maxyaw = p.maxpitch = 4000;
		p.step = 4000;
		p.inst = lamp;
		p.parent = e->ghoulInst;
		p.parentbolt = gg::PartId(e->ghoulInst, "DUMMY02");
		gg::Aim(e, p, t->s.origin);
	}
}

static void SP_snowcat(edict_t *e)
{
	Force(e, 2);
	T(e, T_SIBERIA, 10, -91, -66, -33, 91, 66, 58);
	if (e->spawnflags & 4) {
		Bolt(e, T_SIBERIA, 10, "HEADLIGHT01", 6, "to_headlight");
		Bolt(e, T_SIBERIA, 10, "HEADLIGHT02", 6, "to_headlight");
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Tokyo

static void SP_bowl(edict_t *e) { T(e, T_TOKYO, 0, -5, -5, -1, 5, 5, 2); }
static void SP_buddha(edict_t *e) { T(e, T_TOKYO, 1, -14, -24, -32, 19, 24, 32); }
static void SP_tokyo_fan(edict_t *e) { T(e, T_TOKYO, 2, -14, -24, -32, 19, 24, 32); }

static void gong_pain(edict_t *self, edict_t *, float, int, const vec3_t) { gg::PlayEnt(self, "gong", false); }

static void SP_gong(edict_t *e)
{
	T(e, T_TOKYO, 3, -3, -29, -34, 3, 29, 34);
	gg::PauseEnt(e);
	SetPain(e, gong_pain);
}
static void SP_gym1(edict_t *e) { T(e, T_TOKYO, 4, -36, -13, -27, 36, 14, 27); }
static void SP_gym2(edict_t *e) { T(e, T_TOKYO, 5, -48, -28, -34, 58, 33, 35); }
static void SP_bonsai(edict_t *e) { T(e, T_TOKYO, 6, -5, -10, -8, 5, 10, 8); }
static void SP_samurai(edict_t *e)
{
	T(e, T_TOKYO, 7, -6, -18, -37, 9, 16, 37);
	gg::ScaleEnt(e, 1.1f);
}
static void SP_samurai_small(edict_t *e)
{
	T(e, T_TOKYO, 8, -3, -8, -17, 4, 7, 17);
	gg::ScaleEnt(e, 0.5f);
}
static void SP_shoe(edict_t *e) { T(e, T_TOKYO, 9, -6, -3, -2, 6, 3, 2); }
static void SP_stone_light(edict_t *e)
{
	Force(e, 2);
	T(e, T_TOKYO, 10, -15, -13, 14, 15, 13, 14);
}

// ---------------------------------------------------------------------------------------------------------------
// Uganda

static void SP_african_tree(edict_t *e) { T(e, T_UGANDA, 0, -42, -77, -76, 42, 77, 76); }
static void SP_chainsaw(edict_t *e)
{
	T(e, T_UGANDA, 1, -14, -4, -5, 14, 4, 5);
	if (e->spawnflags & 4)
		fx::SlotAdd(e, fx::Index("environ/normalsmoke"), 0, 0);
}
static void SP_cow(edict_t *e) { T(e, T_UGANDA, 2, -13, -38, -29, 13, 43, 28); }

static void bloodpool_think(edict_t *self)
{
	fx::BloodPool(self->s.origin);
	self->think = NULL;
}

static void SP_cow_head(edict_t *e)
{
	T(e, T_UGANDA, 3, -10, -13, -8, 10, 13, 8);
	if (e->spawnflags & 4) {
		e->think = bloodpool_think;
		e->nextthink = level.time + 2;
	}
}

static void gorepile_pain(edict_t *self, edict_t *other, float, int, const vec3_t p)
{
	if (!other)
		return;
	vec3_t d;
	VectorSubtract(p, other->s.origin, d); // not normalised: the reach is 128 x the attacker distance
	fx::BloodSplat(p, d, self, 128);
}

static void SP_gore_pile(edict_t *e)
{
	T(e, T_UGANDA, 4, -26, -23, -3, 26, 23, 3);
	SetPain(e, gorepile_pain);
}

// hanging meat: i0 = 1 gutted cow / 2 side of beef
static void meat_eos(edict_t *self)
{
	if (D(self).i0 == 1) {
		if (self->spawnflags & 16)
			gg::PlayEnt(self, "gutted_cow", true);
	} else if (self->spawnflags & 4)
		gg::PlayEnt(self, "SLOW", true);
}
static gg::NoteCallback s_meat_eos(meat_eos);
gg::NoteCallback *NoteMeat(void) { return &s_meat_eos; }

static void meat_pain(edict_t *self, edict_t *other, float, int, const vec3_t)
{
	if (!other)
		return;
	vec3_t d;
	VectorSubtract(other->s.origin, self->s.origin, d);
	float a = vectoyaw(d) - self->s.angles[YAW];
	int q = (int)(a * 65536.0f / 360.0f) & 65535; // 1/65536 turn
	a = q * 360.0f / 65536.0f;
	bool frontback = a < 30 || a > 340 || (a > 150 && a < 210);
	if (D(self).i0 == 1)
		gg::PlayEnt(self, frontback ? "swing1" : "swing2", false);
	else
		gg::PlayEnt(self, frontback ? "SWING2" : "SWING1", false);
	gg::OnNote(self, NULL, "EOS", &s_meat_eos); // each pain adds one more reaction (original behaviour)
}

static void gutted_think(edict_t *self)
{
	if (self->spawnflags & 16)
		gg::PlayEnt(self, "gutted_cow", true);
	else if (self->spawnflags & 4)
		fx::BloodPool(self->s.origin);
	self->think = NULL;
}

static void SP_gutted_cow(edict_t *e)
{
	T(e, T_UGANDA, 5, -15, -33, -99, 27, 25, 1);
	SetBox(e, -15, -33, -99, 27, 25, 1); // the right-angle rotation of the setup is undone
	gi.linkentity(e);
	D(e).i0 = 1;
	if (e->spawnflags & 8)
		fx::SlotAdd(e, fx::Index("environ/bloodrain"), 0, 0);
	if (e->spawnflags & 16) {
		e->think = gutted_think; // shares the timer with spawnflag 4: no blood pool then (original quirk)
		e->nextthink = level.time + irand(0, 9);
	} else {
		gg::PauseEnt(e);
		if (e->spawnflags & 4) {
			e->think = gutted_think;
			e->nextthink = level.time + 2;
		}
	}
	SetPain(e, meat_pain);
}

static void SP_hook(edict_t *e) { T(e, T_UGANDA, 6, -1, -4, -7, 1, 4, 7); }
static void SP_nuke(edict_t *e)
{
	Force(e, 3);
	T(e, T_UGANDA, 7, -31, -28, -24, 30, 14, 25);
}
static void SP_rocket_core(edict_t *e) { T(e, T_UGANDA, 8, -55, -14, -14, 55, 14, 14); }
static void SP_rocket_engine(edict_t *e) { T(e, T_UGANDA, 9, -76, -28, -27, 76, 33, 32); }

static void beef_sway(edict_t *self)
{
	gg::PlayEnt(self, "SLOW", true);
	self->think = NULL;
}

static void SP_side_o_beef(edict_t *e)
{
	T(e, T_UGANDA, 10, -13, -16, -55, 19, 17, 48, e->s.skinnum == 1 ? "beef_f" : "beef");
	D(e).i0 = 2;
	if (e->spawnflags & 4) {
		e->think = beef_sway;
		e->nextthink = level.time + irand(1, 10);
	}
	SetPain(e, meat_pain);
}

static void SP_train_arm(edict_t *e)
{
	Force(e, 3);
	T(e, T_UGANDA, 11, -99, -5, -110, 99, 5, 110);
}
static void SP_train_bumper(edict_t *e)
{
	Force(e, 3);
	T(e, T_UGANDA, 12, -32, -15, -16, 32, 15, 10);
}
static void SP_train_coupling(edict_t *e)
{
	Force(e, 3);
	T(e, T_UGANDA, 13, -31, -11, -5, 31, 11, 5);
}
static void SP_train_wheel(edict_t *e) { T(e, T_UGANDA, 14, -14, -1, -14, 14, 1, 14); }

static const SpawnEntry s_spawns[] = {
	{"misc_newyork_burned_car", SP_burned_car},
	{"misc_newyork_gumball", SP_gumball},
	{"misc_newyork_news_machine", SP_news_machine},
	{"misc_newyork_trash_pile", SP_trash_pile},
	{"misc_newyork_trashcan", SP_trashcan},
	{"misc_siberia_carton1", SP_carton1},
	{"misc_siberia_carton2", SP_carton2},
	{"misc_siberia_carton3", SP_carton3},
	{"misc_siberia_carton4", SP_carton4},
	{"misc_siberia_flag", SP_sib_flag},
	{"misc_siberia_icicles", SP_icicles},
	{"misc_siberia_roadflare", SP_roadflare},
	{"misc_siberia_searchlight", SP_sib_searchlight},
	{"misc_siberia_snowcat", SP_snowcat},
	{"misc_tokyo_bowl", SP_bowl},
	{"misc_tokyo_buddha", SP_buddha},
	{"misc_tokyo_fan", SP_tokyo_fan},
	{"misc_tokyo_gong", SP_gong},
	{"misc_tokyo_gym_equipment1", SP_gym1},
	{"misc_tokyo_gym_equipment2", SP_gym2},
	{"misc_tokyo_plant_bonsai", SP_bonsai},
	{"misc_tokyo_samurai_suit", SP_samurai},
	{"misc_tokyo_samurai_suit_small", SP_samurai_small},
	{"misc_tokyo_shoe", SP_shoe},
	{"misc_tokyo_stone_light", SP_stone_light},
	{"misc_uganda_african_tree", SP_african_tree},
	{"misc_uganda_chainsaw", SP_chainsaw},
	{"misc_uganda_cow", SP_cow},
	{"misc_uganda_cow_head", SP_cow_head},
	{"misc_uganda_gore_pile", SP_gore_pile},
	{"misc_uganda_gutted_cow", SP_gutted_cow},
	{"misc_uganda_hook", SP_hook},
	{"misc_uganda_nuke", SP_nuke},
	{"misc_uganda_rocket_core", SP_rocket_core},
	{"misc_uganda_rocket_engine", SP_rocket_engine},
	{"misc_uganda_side_o_beef", SP_side_o_beef},
	{"misc_uganda_train_arm", SP_train_arm},
	{"misc_uganda_train_bumper", SP_train_bumper},
	{"misc_uganda_train_coupling", SP_train_coupling},
	{"misc_uganda_train_wheel", SP_train_wheel},
	{NULL, NULL}};
static SpawnTable s_reg(s_spawns);

} // namespace props
