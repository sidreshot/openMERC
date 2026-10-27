// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — shared prop framework: standard setup, breaking, shared reactions.
// Implements: SPEC-770 §3, §4, §5 (5.1-5.9), SPEC-791 §12

#include "props.h"
#include "../effects/fx.h"
#include "../damage/damage_api.h"

namespace props {

static PropData s_data[MAX_EDICTS];

PropData &D(edict_t *ent)
{
	int n = G_EdictNum(ent);
	if (n < 0 || n >= MAX_EDICTS)
		n = 0;
	return s_data[n];
}

void ClearData(edict_t *ent) { memset(&D(ent), 0, sizeof(PropData)); }

void ResetAllData(void) { memset(s_data, 0, sizeof(s_data)); }

PropData *DataTable(void) { return s_data; } // savegames (prop_save.cpp)

float frand(float a, float b) { return gix.flrand ? gix.flrand(a, b) : a + (b - a) * random(); }
int irand(int a, int b) { return gix.irand ? gix.irand(a, b) : a + (int)((b - a + 1) * random() * 0.99999f); }

int Sound(const char *name) { return gi.soundindex(name); }

void PlaySound(edict_t *ent, int chan, const char *name, float vol, float att)
{
	if (ent)
		gi.sound(ent, chan, gi.soundindex(name), vol, att, 0);
}

void LoopSound(edict_t *ent, const char *name, int data)
{
	ent->s.sound = gi.soundindex(name);
	ent->s.sounddata = data;
}

void StopLoop(edict_t *ent) { ent->s.sound = 0; }

// SPEC-401 client sound list (precache list), names as registered
static const char *const s_soundlist[114] = {
	"Ambient/Gen/Electric/spark1.wav", "Ambient/Gen/Electric/spark2.wav",
	"player/foot/metal/metal1.wav", "player/foot/metal/metal2.wav", "player/foot/metal/metal3.wav",
	"player/foot/metal/metal4.wav",
	"player/foot/snow/snow1.wav", "player/foot/snow/snow2.wav", "player/foot/snow/snow3.wav",
	"player/foot/snow/snow4.wav",
	"player/foot/water/water1.wav", "player/foot/water/water2.wav", "player/foot/water/water3.wav",
	"player/foot/water/water4.wav",
	"player/foot/grass/grass1.wav", "player/foot/grass/grass2.wav", "player/foot/grass/grass3.wav",
	"player/foot/grass/grass4.wav",
	"player/foot/gravel/gravel1.wav", "player/foot/gravel/gravel2.wav", "player/foot/gravel/gravel3.wav",
	"player/foot/gravel/gravel4.wav",
	"player/foot/sand/sand1.wav", "player/foot/sand/sand2.wav", "player/foot/sand/sand3.wav",
	"player/foot/sand/sand4.wav",
	"player/foot/wood/wood1.wav", "player/foot/wood/wood2.wav", "player/foot/wood/wood3.wav",
	"player/foot/wood/wood4.wav",
	"player/foot/stone/stone1.wav", "player/foot/stone/stone2.wav", "player/foot/stone/stone3.wav",
	"player/foot/stone/stone4.wav",
	"Weapons/FX/Rics/ric1.wav", "Weapons/FX/Rics/ric2.wav", "Weapons/FX/Rics/ric3.wav",
	"Impact/Surfs/stone.wav", "Impact/Surfs/water.wav", "Impact/Surfs/wood.wav", "Impact/Surfs/grass.wav",
	"Impact/Surfs/gravel.wav", "Impact/Surfs/sand.wav", "Impact/Surfs/snow.wav",
	NULL, // 44 placeholder, never loaded
	"Impact/Gore/impact1.wav", "Impact/Gore/impact2.wav", "Impact/Gore/impact3.wav",
	"Weapons/FX/FlyBy/FlyBy1.wav", "Weapons/FX/FlyBy/FlyBy2.wav", "Weapons/FX/FlyBy/FlyBy3.wav",
	"Weapons/FX/FlyBy/FlyBy4.wav",
	"Weapons/Glock/fire.wav", "ambient/Nature/weather/thnder1.wav", "weapons/FlshGren/FlshExp.wav",
	"Weapons/Autoshot/fire.wav", "Enemy/MGun/trnslow.wav", "Enemy/MGun/trnstop.wav", "weapons/C4/C4Exp.wav",
	"Impact/Chair/Squeak1.wav", "Impact/Chair/Squeak2.wav", "Impact/Chair/Squeak3.wav",
	"Ambient/Locs/Subway/Train/Brake.wav", "Ambient/Locs/Subway/Train/Horn2.wav",
	"Ambient/Locs/Subway/Train/Impact.wav", "Ambient/Locs/Subway/Train/Move.wav",
	"Weapons/Knife/Swing1.wav", "Weapons/Knife/Swing2.wav", "Weapons/Knife/Swing3.wav",
	"Weapons/Knife/Impact.wav", "Weapons/Knife/Throw.wav",
	"Player/Land/Grass.wav", "Player/Land/Gravel.wav", "Player/Land/Metal.wav", "Player/Land/Sand.wav",
	"Player/Land/Snow.wav", "Player/Land/Stone.wav", "Player/Land/Water.wav", "Player/Land/Wood.wav",
	"impact/surfs/metal1.wav", "impact/surfs/metal2.wav", "impact/surfs/metal3.wav",
	"Impact/GlassBreak/GBBig.wav", "Impact/GlassBreak/GBMed.wav", "Impact/GlassBreak/GBSmall.wav",
	"Impact/Break/Metal.wav", "Impact/Break/Stone.wav", "Impact/Break/Wood.wav",
	"Ambient/Gen/Electric/buzz.wav",
	"Impact/Explosion/Big.wav", "Impact/Explosion/Med.wav", "Impact/Explosion/Small.wav",
	"Ambient/Models/Phone/Hit.wav", "Ambient/Models/TrashCan/LidSpin.wav",
	"weapons/FX/Tink/Metal1.wav", "weapons/FX/Tink/Metal2.wav", "weapons/FX/Tink/Metal3.wav",
	"weapons/FX/Tink/Gen1.wav", "weapons/FX/Tink/Gen2.wav",
	"Impact/Gore/Fall1.wav", "Impact/Gore/Fall2.wav", "Impact/Gore/Fall3.wav",
	"weapons/FX/Crack/Crack1.wav", "weapons/FX/Crack/Crack2.wav", "weapons/FX/Crack/Crack3.wav",
	"enemy/Helicopter/Fire1.wav", "enemy/Helicopter/Fire2.wav", "enemy/Helicopter/Fire3.wav",
	"weapons/FX/Tink/shot1.wav", "weapons/FX/Tink/shot2.wav", "weapons/FX/Tink/shot3.wav",
	"weapons/FX/Tink/hvy1.wav", "weapons/FX/Tink/hvy2.wav", "weapons/FX/Tink/hvy3.wav"};
static_assert(sizeof(s_soundlist) / sizeof(s_soundlist[0]) == 114, "SPEC-401 sound list: 114 entries");
static unsigned char s_marked[114];

void MarkSoundList(int k)
{
	if (k >= 0 && k < 114)
		s_marked[k] = 1;
}

void FlushSoundList(void)
{
	for (int k = 0; k < 114; k++)
		if (s_marked[k] && s_soundlist[k])
			gi.soundindex(s_soundlist[k]);
}

void ResetSoundList(void) { memset(s_marked, 0, sizeof(s_marked)); }

void SetMaterialFlag(int position)
{
	if (position >= 0 && position < 25)
		level.materialflags[position] = 'y';
}

void Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t dir, const vec3_t point,
            const vec3_t origin, int damage, int knockback, int dflags, int mod)
{
	if (!targ || !targ->inuse)
		return;
	T_Damage(targ, inflictor, attacker, dir, point, origin, damage, knockback, dflags, mod);
}

void RadiusDamage(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod,
                  int dflags)
{
	T_RadiusDamage(inflictor, attacker, damage, ignore, radius, mod, dflags);
}

bool IsPlayer(const edict_t *e) { return e && e->inuse && e->client; }
bool IsCharacter(const edict_t *e) { return e && e->inuse && (e->svflags & SVF_MONSTER); }

void Forward(edict_t *e, vec3_t f, vec3_t r, vec3_t u) { AngleVectors(e->s.angles, f, r, u); }

// ---------------------------------------------------------------------------------------------------------------
// pain with the hit point (SPEC-720 §5.3: pain receives attacker, knockback, damage, point)

static void ProbablePoint(edict_t *self, edict_t *other, vec3_t point)
{
	// shim until the damage code passes the point (docs/impl/G7.md, Needs from G18): G3's traced shot, else the
	// area-damage trace end (non-player attackers), else the hit of a player's view ray; the prop origin otherwise
	VectorCopy(self->s.origin, point);
	// a traced shot: the first Ghoul hit of G3's shot hit list on this entity
	if (dmg_shot.ent == self && dmg_shot.count > 0) {
		VectorMA(dmg_shot.worldStart, dmg_shot.rec[0].distance, dmg_shot.worldDir, point);
		return;
	}
	if (!other || !other->inuse)
		return;
	vec3_t start, end, dir;
	VectorCopy(other->s.origin, start);
	if (!other->client) {
		// area damage from a non-player (explosions of props and environ_explosion: inflictor = attacker): the
		// point of SPEC-720 §14.1, the end of a trace (mask 0x6000001) from the centre to the target's box centre.
		// Black-box: g7brk2, the bench top lying at its bolt is shot off by the first environ_explosion only with
		// this point (the box-surface point of the earlier shim missed the part bounds).
		for (int i = 0; i < 3; i++)
			end[i] = self->s.origin[i] + (self->mins[i] + self->maxs[i]) * 0.5f;
		trace_t tr = gi.trace(start, vec3_origin, vec3_origin, end, other, 0x6000001);
		VectorCopy(tr.endpos, point);
		return;
	}
	start[2] += other->viewheight;
	AngleVectors(other->client->v_angle, dir, NULL, NULL);
	VectorMA(start, 8192, dir, end);
	trace_t tr = gi.trace(start, vec3_origin, vec3_origin, end, other, 0x06000081 | 0x2000000);
	if (tr.ent == self)
		VectorCopy(tr.endpos, point);
}

static void PainTramp4(edict_t *self, edict_t *other, float kick, int damage)
{
	PropData &d = D(self);
	if (!d.pain)
		return;
	vec3_t p;
	ProbablePoint(self, other, p);
	d.pain(self, other, kick, damage, p);
}

static void PainTramp5c(edict_t *self, edict_t *other, float kick, int damage, const float *point)
{
	PropData &d = D(self);
	if (d.pain)
		d.pain(self, other, kick, damage, point ? point : self->s.origin);
}

static void PainTramp5(edict_t *self, edict_t *other, float kick, int damage, float *point)
{
	PainTramp5c(self, other, kick, damage, point);
}

// the entity's pain slot takes whichever signature G18's edict_t declares
struct Pri0 {};
struct Pri1 : Pri0 {};
struct Pri2 : Pri1 {};
template <typename P> static auto InstallPain(P &slot, Pri2) -> decltype(slot = &PainTramp5c, void())
{
	slot = &PainTramp5c;
}
template <typename P> static auto InstallPain(P &slot, Pri1) -> decltype(slot = &PainTramp5, void())
{
	slot = &PainTramp5;
}
template <typename P> static auto InstallPain(P &slot, Pri0) -> decltype(slot = &PainTramp4, void())
{
	slot = &PainTramp4;
}

void SetPain(edict_t *ent, prop_pain_t fn)
{
	D(ent).pain = fn;
	if (fn)
		InstallPain(ent->pain, Pri2());
	else
		ent->pain = NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// §3 standard setup

void SetBox(edict_t *ent, float x0, float y0, float z0, float x1, float y1, float z1)
{
	VectorSet(ent->mins, x0, y0, z0);
	VectorSet(ent->maxs, x1, y1, z1);
}

void RotateBox(edict_t *ent)
{
	vec3_t on, ox; // original box
	VectorCopy(ent->mins, on);
	VectorCopy(ent->maxs, ox);
	vec3_t cn, cx; // current box
	VectorCopy(on, cn);
	VectorCopy(ox, cx);
	for (int axis = 0; axis < 3; axis++) { // pitch, yaw, roll
		int a = (int)ent->s.angles[axis];
		if (a != 90 && a != 180 && a != 270)
			continue;
		// every rotation reads the extents of the original box. SPEC-770 §3 step 5 says the x minimum is read after
		// an earlier rotation; black-box measurement contradicts it (OPEN_QUESTIONS G7: pitch 90/180/270 with yaw
		// 180/270 give maxs.x / maxs.y = -mins.x of the class box in the original)
		vec3_t n = {on[0], on[1], on[2]};
		const float *x = ox;
		switch (axis) {
		case YAW:
			if (a == 90) {
				cn[0] = -x[1]; cx[0] = -n[1]; cn[1] = n[0]; cx[1] = x[0];
			} else if (a == 180) {
				cn[0] = -x[0]; cx[0] = -n[0]; cn[1] = -x[1]; cx[1] = -n[1];
			} else {
				cn[0] = n[1]; cx[0] = x[1]; cn[1] = -x[0]; cx[1] = -n[0];
			}
			break;
		case PITCH:
			if (a == 90) {
				cn[0] = n[2]; cx[0] = x[2]; cn[2] = -x[0]; cx[2] = -n[0];
			} else if (a == 180) {
				cn[0] = -x[0]; cx[0] = -n[0]; cn[2] = -x[2]; cx[2] = -n[2];
			} else {
				cn[0] = -x[2]; cx[0] = -n[2]; cn[2] = n[0]; cx[2] = x[0];
			}
			break;
		default: // ROLL
			if (a == 90) {
				cn[1] = -x[2]; cx[1] = -n[2]; cn[2] = n[1]; cx[2] = x[1];
			} else if (a == 180) {
				cn[1] = -x[1]; cx[1] = -n[1]; cn[2] = -x[2]; cx[2] = -n[2];
			} else {
				cn[1] = n[2]; cx[1] = x[2]; cn[2] = -x[1]; cx[2] = -n[1];
			}
			break;
		}
	}
	VectorCopy(cn, ent->mins);
	VectorCopy(cx, ent->maxs);
}

void ApplyDefaults(edict_t *ent, const ModelEntry *e)
{
	if (!e)
		return;
	if (!ent->surfacetype)
		ent->surfacetype = e->surf;
	if (!ent->material)
		ent->material = e->mat;
	if (!ent->health)
		ent->health = e->health;
	if (!ent->solid)
		ent->solid = e->solid;
}

static void FinishSetup(edict_t *ent)
{
	VectorCopy(ent->mins, D(ent).box_mins);
	VectorCopy(ent->maxs, D(ent).box_maxs);
	// step 4 (solid 4 check)
	if (ent->solid == SOLID_GROUND && !ent->ghoulInst) {
		gi.cprintf(NULL, PRINT_HIGH, "ERROR: attempting to set SOLID_GHOUL without ghoulInst (model '%s')!\n",
		           ent->ghoulmodel ? ent->ghoulmodel : (ent->model ? ent->model : ""));
		ent->solid = SOLID_NOT;
	}
	// step 5
	RotateBox(ent);
	// step 6
	ent->clipmask = 0x02030003;
	// step 7
	if (st.scale != 0)
		gg::ScaleEnt(ent, st.scale);
	// step 8
	bool light = !strncmp(ent->classname, "light", 5);
	if (light) {
		if (ent->spawnflags & 2) {
			ent->takedamage = DAMAGE_YES;
			if (!ent->die)
				ent->die = Die_StandardBreak;
		}
		if (ent->spawnflags & 4) {
			ent->movetype = MOVETYPE_DAN;
			if (!ent->touch)
				ent->touch = Touch_PlayerPush;
		} else
			ent->movetype = MOVETYPE_NONE;
	} else {
		if (!(ent->spawnflags & 1)) {
			ent->takedamage = DAMAGE_YES;
			if (!ent->die)
				ent->die = Die_TableBreak;
		}
		if (ent->spawnflags & 2)
			ent->movetype = MOVETYPE_NONE;
		else {
			ent->movetype = MOVETYPE_DAN;
			if (!ent->touch)
				ent->touch = Touch_PlayerPush;
		}
	}
	// step 9
	if (ent->solid == SOLID_GROUND && ent->ghoulInst) {
		ghoul::Matrix4 root;
		memset(&root, 0, sizeof(root));
		vec3_t f, r, u;
		vec3_t a = {-ent->s.angles[0], ent->s.angles[1], -ent->s.angles[2]};
		AngleVectors(a, f, r, u);
		for (int i = 0; i < 3; i++) {
			root.m[0][i] = f[i];
			root.m[1][i] = -r[i];
			root.m[2][i] = u[i];
		}
		root.m[3][3] = 1;
		ent->ghoulInst->GetBounds(level.time, &root, ent->mins, ent->maxs, 0, false);
	}
	// step 10
	if (st.color[0] || st.color[1] || st.color[2]) {
		if (ent->ghoulInst)
			ent->ghoulInst->SetTint(st.color[0], st.color[1], st.color[2], 1);
	}
	gi.linkentity(ent);
}

void Setup(edict_t *ent, int table, int index, const char *skin, const char *onlypart)
{
	const ModelEntry *e = Entry(table, index);
	PropData &d = D(ent);
	d.entry = e;
	d.table = table;
	d.index = index;
	d.seqref = table * 65536 + index;
	d.flags |= 1;
	gg::CreateAttached(ent); // attached-object record (SPEC-791 Interface 4)
	if (!e) {
		gi.dprintf("props: no model-table entry %d/%d\n", table, index);
		FinishSetup(ent);
		return;
	}
	// step 1
	if (!ent->ghoulInst)
		gg::SetSimpleModel(ent, e->dir, e->file, skin, e->skinmat);
	// step 2
	if (skin && skin[0]) {
		if (!ent->ghoulInst)
			gi.cprintf(NULL, PRINT_HIGH, "ERROR: trying to set skin %s on NULL ghoulInst!\n", skin);
		else
			gg::SetSkinInst(ent->ghoulInst, e->dir, e->file, e->skinmat, skin);
	}
	// step 3
	if (onlypart && ent->ghoulInst)
		gg::ShowOnlyPart(ent->ghoulInst, onlypart);
	// step 4
	ApplyDefaults(ent, e);
	FinishSetup(ent);
	// step 11
	ent->mass = 800;
}

void SkinAfterSetup(edict_t *ent, const char *skin)
{
	const ModelEntry *e = D(ent).entry;
	if (e && ent->ghoulInst && skin)
		gg::SetSkinInst(ent->ghoulInst, e->dir, e->file, e->skinmat, skin);
}

void SetupNoTable(edict_t *ent, int surf, int mat, int health, int solid)
{
	ModelEntry e = {"", "", surf, mat, health, solid, NULL, 0, 0};
	D(ent).flags |= 1;
	gg::CreateAttached(ent);
	ApplyDefaults(ent, &e);
	FinishSetup(ent);
}

// ---------------------------------------------------------------------------------------------------------------
// §4 breaking

void StandardBreak(edict_t *self, edict_t *attacker, int n, int s)
{
	if (!n || !s) {
		float V = (self->maxs[0] - self->mins[0]) * (self->maxs[1] - self->mins[1]) * (self->maxs[2] - self->mins[2]);
		if (!n)
			n = (int)(V / 9000 + 2);
		if (n < 10)
			n = 10;
		if (n > 20)
			n = 20;
		if (!s)
			s = 1;
	}
	if (self->material > 0) {
		vec3_t c, dir = {0, 0, 0};
		for (int i = 0; i < 3; i++)
			c[i] = (self->absmin[i] + self->absmax[i]) * 0.5f;
		if (attacker && attacker->inuse) {
			VectorSubtract(attacker->s.origin, c, dir);
			VectorNormalize(dir);
		}
		fx::Debris(c, dir, n, s, self->material, (int)self->maxs[0], (int)self->maxs[1], (int)self->maxs[2],
		           self->surfacetype);
	}
	if (self->message && self->message[0] && attacker && attacker->inuse) {
		gi.centerprintf(attacker, "%s", self->message);
		PlaySound(attacker, CHAN_AUTO, "Misc/Talk.wav", 0.6f, ATTN_NORM);
	}
	G_UseTargets(self, self);
	G_FreeEdict(self);
}

void TableBreak(edict_t *self, edict_t *attacker)
{
	const ModelEntry *e = D(self).entry;
	if (!e)
		StandardBreak(self, attacker, 0, 0);
	else
		StandardBreak(self, attacker, e->debris_n & 0xFF, (int)e->debris_s);
}

void Die_StandardBreak(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t)
{
	StandardBreak(self, attacker, 0, 0);
}

void Die_TableBreak(edict_t *self, edict_t *, edict_t *attacker, int, vec3_t) { TableBreak(self, attacker); }

// ---------------------------------------------------------------------------------------------------------------
// §5.1 player push

static void Think_PushRestore(edict_t *self)
{
	PropData &d = D(self);
	self->friction = 1;
	self->think = d.push_saved_think;
	self->nextthink = d.push_saved_next;
	d.push_saved_think = NULL;
	if (self->think && self->nextthink && self->nextthink <= level.time + 0.001f) {
		self->nextthink = 0;
		self->think(self);
	}
}

void Touch_PlayerPush(edict_t *self, edict_t *other, cplane_t *, csurface_t *)
{
	if (!other || !other->client)
		return;
	vec3_t d;
	VectorSubtract(self->s.origin, other->s.origin, d);
	VectorNormalize(d);
	VectorScale(d, 100, self->velocity);
	self->friction = 0;
	PropData &pd = D(self);
	if (self->think != Think_PushRestore) {
		pd.push_saved_think = self->think;
		pd.push_saved_next = self->nextthink;
	}
	self->think = Think_PushRestore;
	self->nextthink = level.time + FRAMETIME;
}

// ---------------------------------------------------------------------------------------------------------------
// §5.2 knock over, §5.3 throw away

void KnockRates(edict_t *self, edict_t *attacker, vec3_t d)
{
	const float *src = attacker ? attacker->s.origin : self->s.origin;
	VectorSubtract(self->s.origin, src, d);
	VectorNormalize(d);
	float phi = (360.0f - self->s.yawofs) * (float)(M_PI / 180.0);
	float c = cosf(phi), s = sinf(phi);
	self->avelocity[PITCH] = 70 * (d[0] * c - d[1] * s);
	self->avelocity[ROLL] = -70 * (d[0] * s + d[1] * c);
}

void Think_Tumble(edict_t *self)
{
	if (self->health > 0 && fabsf(self->s.angles[PITCH]) < 90 && fabsf(self->s.angles[ROLL]) < 90) {
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
	if (self->health < 25)
		self->nextthink = level.time + 0.3f;
}

void KnockOver(edict_t *self, edict_t *attacker, int damage, float k)
{
	vec3_t d;
	KnockRates(self, attacker, d);
	VectorScale(d, damage * k, self->velocity);
	self->velocity[2] = 150;
	self->elasticity = 0.7f;
	self->think = Think_Tumble;
	self->nextthink = level.time + FRAMETIME;
}

static void AwayDir(edict_t *self, edict_t *attacker, vec3_t d)
{
	const float *src = attacker ? attacker->s.origin : self->s.origin;
	VectorSubtract(self->s.origin, src, d);
	VectorNormalize(d);
}

void ThrowAway(edict_t *self, edict_t *attacker, int damage)
{
	vec3_t d;
	AwayDir(self, attacker, d);
	VectorScale(d, frand(4, 10) * damage, self->velocity);
	self->velocity[2] = frand(4, 6) * damage;
	self->avelocity[YAW] = frand(4, 6) * damage;
}

void ThrowAwaySpin(edict_t *self, edict_t *attacker, int damage, int axis)
{
	vec3_t d;
	AwayDir(self, attacker, d);
	VectorScale(d, frand(10, 15) * damage, self->velocity);
	self->avelocity[axis] = d[axis] * 200;
}

// ---------------------------------------------------------------------------------------------------------------
// §5.4 standard prop explosion

void Ignite(edict_t *self, float radius)
{
	edict_t *e = NULL;
	while ((e = findradius(e, self->s.origin, radius)) != NULL) {
		if (e == self || !e->takedamage)
			continue;
		trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, e->s.origin, self, 3);
		if (tr.fraction < 1 && tr.ent != e)
			continue;
		SetOnFire(e, self);
	}
}

void Explode(edict_t *self, bool ignite, bool flash)
{
	RadiusDamage(self, self, 100, self, 100, MOD_UNKNOWN, 0);
	if (ignite)
		Ignite(self, 100);
	fx::AINoise(self, self->s.origin, 200, 200);
	fx::EffectAt(fx::Index("weapons/world/airexplode"), self->s.origin);
	fx::ExplosionDebris(self);
	if (flash) {
		float r = 10.0f * self->health;
		if (r > 0)
			WhiteOut(self->s.origin, r, 0.9f, 0.5f);
	}
	fx::ShakeTick(self->s.origin, 100, 200, 30);
}

// ---------------------------------------------------------------------------------------------------------------
// §5.6 bolt-on parts, §5.7 shootable parts

gg::IGhoulInst *Bolt(edict_t *ent, int table, int parent, const char *parentbolt, int child, const char *childbolt,
                     const char *skin)
{
	return gg::AddTrackedBolt(ent, Ref(Entry(table, parent)), parentbolt, Ref(Entry(table, child)), childbolt, skin);
}

// entity-space matrix of the parent bolt that bolt-on part i hangs on
static bool PartMatrix(edict_t *self, int i, ghoul::Matrix4 &m)
{
	gg::IGhoulInst *c = gg::TrackedPart(self, i);
	gg::IGhoulInst *p = c ? c->GetParent() : NULL;
	if (!p)
		return false;
	int n = p->NumChildren();
	for (int k = 0; k < n; k++) {
		ghoul::GhoulID b = 0;
		if (p->GetChild(k, &b) != c)
			continue;
		gg::BoltMatrix(p, b, m);
		return true;
	}
	return false;
}

bool PartPos(edict_t *self, int i, vec3_t out)
{
	VectorCopy(self->s.origin, out);
	ghoul::Matrix4 m;
	if (!PartMatrix(self, i, m))
		return false;
	vec3_t lp = {m.m[3][0], m.m[3][1], m.m[3][2]};
	gg::LocalToWorld(self, lp, out);
	return true;
}

// world-axis box of bolt-on part i relative to its bolt position (SPEC-AMEND-006 G7.7): method 51 of the bolt-on
// with root R = the parent bolt's rotation followed by the entity rotation (translations removed), all parts, not
// exact
static void PartBox(edict_t *self, int i, vec3_t mins, vec3_t maxs)
{
	VectorClear(mins);
	VectorClear(maxs);
	ghoul::Matrix4 m;
	gg::IGhoulInst *c = gg::TrackedPart(self, i);
	if (!c || !PartMatrix(self, i, m))
		return;
	// row-vector matrices (v' = v M): rows of E = the entity's axes in world space
	float E[3][3];
	for (int k = 0; k < 3; k++) {
		vec3_t unit = {k == 0 ? 1.0f : 0.0f, k == 1 ? 1.0f : 0.0f, k == 2 ? 1.0f : 0.0f}, w;
		gg::LocalDirToWorld(self, unit, w);
		for (int j = 0; j < 3; j++)
			E[k][j] = w[j];
	}
	ghoul::Matrix4 R = m;
	for (int r = 0; r < 3; r++)
		for (int j = 0; j < 3; j++)
			R.m[r][j] = m.m[r][0] * E[0][j] + m.m[r][1] * E[1][j] + m.m[r][2] * E[2][j];
	gg::BoundsRotated(c, R, mins, maxs);
}

void PartDebris(edict_t *self, const vec3_t at)
{
	static const vec3_t zero = {0, 0, 0};
	fx::Debris(at, zero, 5, 1, self->material, 0, 0, 0, self->surfacetype);
	fx::Puff(at, 120, 120, 120, 200);
}

void RemovePart(edict_t *self, int i, bool debris)
{
	PropData &d = D(self);
	gg::IGhoulInst *c = gg::TrackedPart(self, i);
	if (c)
		gg::AllParts(c, false);
	gg::SetPartShown(self, i, false);
	if (debris) {
		vec3_t at;
		PartPos(self, i, at);
		PartDebris(self, at);
	}
}

void PartReactNone(edict_t *, int, edict_t *, const vec3_t) {}

static bool ValidProp(edict_t *e) { return e && e->inuse && gg::HasAttached(e); }

void ThrowOffLinked(edict_t *self)
{
	PropData &d = D(self);
	edict_t *e = gg::Link(self);
	gg::SetLink(self, NULL);
	int guard = 0;
	while (e && guard++ < MAX_EDICTS) {
		edict_t *next = ValidProp(e) ? gg::Link(e) : NULL;
		if (ValidProp(e))
			gg::SetLink(e, NULL);
		if (e->inuse) {
			vec3_t v;
			VectorSubtract(e->s.origin, self->s.origin, v);
			VectorNormalize(v);
			VectorScale(v, frand(75, 125), e->velocity);
			e->velocity[2] += 50;
			e->s.angles[YAW] += frand(-20, 20);
			e->s.origin[2] += 15;
			gi.linkentity(e);
		}
		e = next;
	}
}

void LinkObject(edict_t *self, edict_t *other)
{
	// append `other` to the chain unless it is already in it; the chain continues only through prop records
	edict_t *cur = self;
	int guard = 0;
	while (guard++ < MAX_EDICTS) {
		if (!ValidProp(cur))
			return; // a non-prop object ends the chain (original quirk)
		edict_t *n = gg::Link(cur);
		if (n == other)
			return;
		if (!n || !n->inuse) {
			gg::SetLink(cur, other);
			return;
		}
		cur = n;
	}
}

int ShootableParts(edict_t *self, edict_t *attacker, const vec3_t point)
{
	PropData &d = D(self);
	int hit = 0;
	for (int r = 0; r < d.nparts && !hit; r++) {
		const PartRow &row = d.parts[r];
		int i = row.index;
		if (!gg::PartShown(self, i))
			continue;
		gg::IGhoulInst *c = gg::TrackedPart(self, i);
		if (!c)
			continue;
		vec3_t at, mins, maxs;
		PartPos(self, i, at);
		// the part's bounds turned by its bolt: black-box g7brk2, the bench top (lying along the bench) is shot off
		// at the same point as in the original only with turned bounds
		PartBox(self, i, mins, maxs);
		bool in = true;
		for (int k = 0; k < 3; k++)
			if (point[k] < at[k] + mins[k] - 2 || point[k] > at[k] + maxs[k] + 2)
				in = false;
		if (!in)
			continue;
		hit = i;
		gg::AllParts(c, false);
		gg::SetPartShown(self, i, false);
		if (row.react)
			row.react(self, i, attacker, point);
		else
			PartDebris(self, at);
	}
	if (self->inuse)
		ThrowOffLinked(self);
	return hit;
}

// ---------------------------------------------------------------------------------------------------------------
// §5.8 fall over

static void Think_FallOver(edict_t *self)
{
	PropData &d = D(self);
	float step = d.fall_speed * FRAMETIME;
	d.fall_speed += 30;
	bool done = false;
	if (d.fall_turned + step >= 85) {
		step = 90 - d.fall_turned;
		done = true;
	}
	d.fall_turned += step;
	// rotate the origin about the pivot edge and turn the angles about the fall axis
	switch (d.fall_dir) {
	case 1: self->s.angles[PITCH] += step; break;
	case 2: self->s.angles[PITCH] -= step; break;
	case 3: self->s.angles[ROLL] -= step; break;
	default: self->s.angles[ROLL] += step; break;
	}
	{
		float a = d.fall_turned * (float)(M_PI / 180.0);
		vec3_t rel;
		VectorCopy(d.v0, rel); // origin relative to the pivot at the start
		// rotate rel about fall_axis by a (Rodrigues)
		vec3_t k;
		VectorCopy(d.fall_axis, k);
		vec3_t kxr;
		CrossProduct(k, rel, kxr);
		float kd = DotProduct(k, rel);
		for (int i = 0; i < 3; i++)
			self->s.origin[i] = d.fall_pivot[i] + rel[i] * cosf(a) + kxr[i] * sinf(a) + k[i] * kd * (1 - cosf(a));
	}
	gi.linkentity(self);
	if (!done) {
		self->nextthink = level.time + FRAMETIME;
		return;
	}
	VectorCopy(d.box_mins, self->mins); // re-rotated box from the class box
	VectorCopy(d.box_maxs, self->maxs);
	RotateBox(self);
	gi.linkentity(self);
	self->think = d.fall_think;
	self->plUse = d.fall_use;
	self->nextthink = self->think ? level.time + FRAMETIME : 0;
}

void FallOverStart(edict_t *self, edict_t *player)
{
	// SPEC-770 §5.8 with the gatekeeper note (OPEN_QUESTIONS, SPEC-791 open question): in 1.07f the fall state is
	// never given its subject, so the prop does not fall and its use-key behaviour is gone. Verified black-box
	// 2026-10-25 (g7fall: misc_generic_table1 spawnflag 4 used three times by a player, original module: no state
	// change; the use key itself works, urinal flush). Default: that observable result; cvar g7_fallover 1 runs the
	// specified fall instead (needs free space of the prop's height on the fall side).
	PropData &d = D(self);
	void (*saved_use)(edict_t *, edict_t *, edict_t *) = self->plUse;
	self->plUse = NULL;
	static cvar_t *fall = NULL;
	if (!fall)
		fall = gi.cvar("g7_fallover", "0", 0);
	if (!fall->value || !player)
		return;
	vec3_t f, r, u, push;
	AngleVectors(self->s.angles, f, r, u);
	VectorSubtract(self->s.origin, player->s.origin, push);
	push[2] = 0;
	VectorNormalize(push);
	float df = DotProduct(f, push), dr = DotProduct(r, push);
	vec3_t dir;
	if (fabsf(df) > 0.707f) {
		d.fall_dir = df > 0 ? 1 : 2;
		VectorScale(f, df > 0 ? 1 : -1, dir);
	} else {
		d.fall_dir = dr > 0 ? 4 : 3;
		VectorScale(r, dr > 0 ? 1 : -1, dir);
	}
	float len = self->maxs[2] - self->mins[2];
	vec3_t end, top;
	VectorMA(self->s.origin, len, dir, end);
	trace_t tr = gi.trace(self->s.origin, vec3_origin, vec3_origin, end, self, self->clipmask);
	VectorMA(self->s.origin, len * 0.7071f, dir, top);
	top[2] += len * 0.7071f;
	trace_t tr2 = gi.trace(self->s.origin, vec3_origin, vec3_origin, top, self, self->clipmask);
	if (tr.fraction < 1 || tr2.fraction < 1) {
		self->plUse = saved_use;
		return;
	}
	// pivot = bottom edge on that side
	float ext = 0;
	for (int i = 0; i < 2; i++)
		ext += fabsf(dir[i]) * (dir[i] > 0 ? self->maxs[i] : -self->mins[i]);
	VectorMA(self->s.origin, ext, dir, d.fall_pivot);
	d.fall_pivot[2] += self->mins[2];
	VectorSubtract(self->s.origin, d.fall_pivot, d.v0);
	vec3_t upv = {0, 0, 1};
	CrossProduct(upv, dir, d.fall_axis);
	VectorNegate(d.fall_axis, d.fall_axis);
	VectorNormalize(d.fall_axis);
	d.fall_turned = 0;
	d.fall_speed = 30;
	d.fall_think = self->think;
	d.fall_use = saved_use;
	self->think = Think_FallOver;
	self->nextthink = level.time + FRAMETIME;
}

// ---------------------------------------------------------------------------------------------------------------
// §5.9 riders

void Riders(edict_t *self)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = g_edicts + i;
		if (!p->inuse || !p->client || p->groundentity != self)
			continue;
		if (p->movetype == MOVETYPE_NONE || p->movetype == MOVETYPE_NOCLIP || p->movetype == MOVETYPE_PUSH ||
		    p->movetype == MOVETYPE_STOP)
			continue;
		vec3_t np;
		VectorMA(p->s.origin, FRAMETIME, self->velocity, np);
		trace_t tr = gi.trace(np, p->mins, p->maxs, np, p, 0x02010003);
		if (tr.startsolid || tr.allsolid)
			continue;
		VectorCopy(np, p->s.origin);
		gi.linkentity(p);
	}
}

} // namespace props
