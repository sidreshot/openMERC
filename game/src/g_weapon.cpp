// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the server half of the player weapons: attack table, aiming, hitscan
// bullets, shotgun, knife, projectiles (slug, rockets, grenades, fireball, thrown knife), beams (MPG,
// flame stream) and equipment (grenade, C4, claymore, flash pack, medkit, goggles), explosion helpers.
// Character-only attacks (20-30) exist in the table for precaching; their AI callers belong to G2.
// Implements: SPEC-782

#include "g_local.h"
#include "bots/bot_api.h" /* G5 */
#include "ai/ai_hooks.h" /* G2 */
#include "damage/damage_api.h" /* G3: thrown-knife marker and deflection */
#include "effects/fx.h" /* G7: fx::g_fire_attack (turrets, environ_invisible_attack) */

int SP_Id(const char *package, const char *entry);

struct attack_t {
	const char *P, *D, *S;
	int dmg, imp, flags;
	float alertP, alertC;
	int mod;
	float gA, gB;
	int range;
	const char *tracer;
	int T;
};

// SPEC-782 table 3 (muzzle effects relative to weapons/)
static const attack_t attacks[31] = {
	{NULL, NULL, NULL, 0, 3, 0, 0, 0, 0, 0, 0, 0, NULL, 0},
	{NULL, "othermz/knife", "othermz/knife", 22, 3, 0x1000000, 0, 0, 0, 0, 0.25f, 30, NULL, 0},
	{NULL, NULL, NULL, 30, 2, 0x1000000, 0, 0, 0, 0, 0, 0, NULL, 0},
	{"playermz/pistol1", "othermz/pistol1_p", "othermz/pistol1", 45, 2, 0, 0.03f, 0.03f, 3, 0, 0, 512, "world/tracer_s", 0},
	{"playermz/pistol2", "othermz/pistol2_p", "othermz/pistol2", 120, 5, 0x20000, 0.08f, 0.08f, 4, 0.5f, 0, 800, "world/tracer_l", 1},
	{"playermz/machinepistol", "othermz/machinepistol", "othermz/machinepistol", 40, 2, 0x1010000, 0, 0, 5, 0, 0.5f, 512, "world/tracer_s", 0},
	{"playermz/assaultrifle", "othermz/assaultrifle_p", "othermz/assaultrifle", 45, 4, 0x10000, 0.03f, 0.03f, 6, 0, 0, 512, "world/tracer", 0},
	{"playermz/sniper", "othermz/sniper", "othermz/sniperenemy", 80, 6, 0x1080000, 0, 0, 7, 0.5f, 0, 1600, "world/tracer_n", 0},
	{NULL, NULL, NULL, 30, 3, 0, 0, 0, 0, 0, 0, 512, "world/tracer_n", 0},
	{"playermz/autoshot", "othermz/autoshot_p", "othermz/autoshot", 80, 3, 0x28000, 0.04f, 0.04f, 8, 0, 0, 512, NULL, 0},
	{"playermz/autoalt", "othermz/autoalt_p", "othermz/autoalt", 200, 3, 0x80, 0.02f, 0.02f, 37, 0, 0, 256, NULL, 0},
	{"playermz/shotgun", "othermz/shotgun_p", "othermz/shotgun", 40, 1, 0x20000, 0.2f, 0.2f, 9, 0, 0.5f, 128, NULL, 0},
	{"playermz/machinegun", "othermz/machinegun_p", "othermz/machinegun", 100, 7, 0x20000, 0.08f, 0.04f, 10, 0.25f, 0, 1024, "world/tracer_l", 1},
	{"playermz/phos", "othermz/phos_p", "othermz/phos", 30, 3, 0, 0.01f, 0.01f, 0, 0, 0, 512, NULL, 0},
	{"playermz/rocket", "othermz/rocket_p", "othermz/rocket", 30, 5, 0, 0.2f, 0.2f, 0, 0, 0, 512, NULL, 0},
	{"playermz/rocket_alt", "othermz/rocket_alt_p", "othermz/rocket_alt", 15, 5, 0, 0.1f, 0.1f, 0, 0, 0, 512, NULL, 0},
	{"playermz/mpgbeam1", NULL, NULL, 10, 3, 0, 0, 0, 14, 1, 0, 512, NULL, 0},
	{"playermz/mpg", "othermz/mpg", "othermz/mpgenemy", 200, 3, 0, 0, 0, 14, 1, 0, 512, NULL, 0},
	{"playermz/flamegun", NULL, NULL, 15, 3, 0, 0, 0, 0, 0, 0, 512, NULL, 0},
	{"playermz/flamerelease", "othermz/flamerelease", "othermz/flamerelease", 30, 3, 0, 0.3f, 0.3f, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 30, 3, 0, 0.03f, 0.03f, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 10, 3, 0, 0.1f, 0.1f, 0, 0, 0, 512, "world/tracer_l", 0},
	{NULL, NULL, NULL, 30, 3, 0, 0.05f, 0.05f, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 30, 3, 0, 0.1f, 0.1f, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 30, 3, 0, 0, 0, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 30, 3, 0, 0, 0, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 30, 3, 0, 0, 0, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 30, 3, 0, 0, 0, 0, 0, 0, 512, NULL, 0},
	{NULL, NULL, NULL, 180, 15, 0x20000, 0.1f, 0.1f, 0, 0, 0, 512, "world/tracer_l", 0},
	{NULL, "othermz/gb_dekmz", "othermz/gb_dekmz", 300, 3, 0, 0.02f, 0.02f, 0, 0, 0, 512, "world/tracer_l", 1},
	{NULL, NULL, NULL, 30, 2, 0, 0, 0, 0, 0, 0, 0, NULL, 0},
};

#define SHOT_MASK 0x06000081
#define PROJ_MASK 0x06000083

static int fxi(const char *name) { return name ? gi.effectindex(va("weapons/%s", name)) : 0; }
static int fxn(const char *name) { return gi.effectindex(name); }
static bool DM() { return deathmatch->value != 0; }
static bool RealisticDamage() { return ((int)dmflags->value & 16) || deathmatch->value == 5; }

static int AttackDamage(int a)
{
	if ((a == 7 || a == 8) && RealisticDamage())
		return 110;
	return attacks[a].dmg;
}
static int AttackFlags(int a)
{
	int f = attacks[a].flags;
	if ((a == 4 || a == 7 || a == 8) && RealisticDamage())
		f |= 0x2;
	return f;
}
static int AttackRange(int a)
{
	if ((a == 7 || a == 8) && DM())
		return 4800;
	return attacks[a].range;
}

//======================================================================
// precache (SPEC-782 12)

void WeaponsLevelPrecache(void)
{
	// per item type: flash pack, C4, goggles, claymore, grenade
	gi.soundindex("Weapons/gren/bounce.wav");
	gi.soundindex("Weapons/C4/c4attch.wav");
	gi.soundindex("Weapons/C4/Beep.wav");
	gi.soundindex("Weapons/Goggles/on.wav");
	gi.soundindex("Weapons/Goggles/off.wav");
	gi.effectindex("weapons/world/claymore");
	gi.soundindex("Weapons/Claymore/Beep.wav");
	gi.effectindex("weapons/world/grenadeexplode");

	static const char *models[] = {"flashpak", "C4", "claymore", "grenade", "shell"};
	for (const char *m : models)
		GhoulModelObject("Items/Projectiles", m, NULL, false);
	GhoulModelObject("Items/Pick-ups/Armor", "armor", NULL, false);

	static const char *fx[] = {"environ/onfireburst",       "environ/quickfireburst",       "spark",
	                           "environ/wallspark",         "land",                         "gotshot",
	                           "exphit",                    "lighthit",                     "weapons/world/airexplode",
	                           "weapons/world/grenadeexplode", "weapons/world/c4explode"};
	for (const char *f : fx)
		gi.effectindex(f);
	static const char *snd[] = {"weapons/fx/tink/gen1.wav",    "weapons/fx/tink/gen2.wav",   "weapons/fx/tink/metal1.wav",
	                            "weapons/fx/tink/metal2.wav",  "weapons/fx/tink/metal3.wav", "weapons/fx/tink/shot1.wav",
	                            "weapons/fx/tink/shot2.wav",   "weapons/fx/tink/shot3.wav",  "weapons/fx/tink/hvy1.wav",
	                            "weapons/fx/tink/hvy2.wav",    "weapons/fx/tink/hvy3.wav",   "weapons/fx/crack/crack1.wav",
	                            "weapons/fx/crack/crack2.wav", "weapons/fx/crack/crack3.wav", "Weapons/gren/throw.wav",
	                            "Weapons/FX/FlyBy/FlyBy1.wav", "Weapons/FX/FlyBy/FlyBy2.wav", "Weapons/FX/FlyBy/FlyBy3.wav",
	                            "Weapons/FX/FlyBy/FlyBy4.wav", "Weapons/FX/Rics/ric1.wav",   "Weapons/FX/Rics/ric2.wav",
	                            "Weapons/FX/Rics/ric3.wav",    "Weapons/FX/Rics/ric4.wav",   "Weapons/FX/Rics/ric5.wav",
	                            "Weapons/FX/Rics/ric6.wav",    "dm/armorpu.wav",             "dm/healthpu.wav",
	                            "weapons/gpistol/cock.wav",    "misc/talk.wav",              "misc/type.wav",
	                            "weapons/FlshGren/FlshExp.wav", "impact/gore/impact1.wav",   "impact/gore/impact2.wav",
	                            "impact/gore/impact3.wav",     "impact/player/hit1.wav",     "impact/player/hit2.wav",
	                            "impact/player/hit3.wav",      "impact/player/hit4.wav",     "impact/player/armor.wav",
	                            "player/jump.wav",             "misc/padd/warning.wav",      "misc/padd/warning2.wav",
	                            "misc/padd/max.wav",           "weapons/knife/throwloop.wav"};
	for (const char *s : snd)
		gi.soundindex(s);
}

void WeaponPrecacheAttack(int a)
{
	if (a <= 0 || a > 30)
		return;
	const attack_t &at = attacks[a];
	fxi(at.P);
	fxi(DM() ? at.D : at.S);
	fxi(at.tracer);
	switch (a) {
	case 1:
		gi.soundindex("weapons/knife/swing2.wav");
		gi.soundindex("weapons/knife/swing3.wav");
		break;
	case 2:
	case 30:
		gi.soundindex("weapons/knife/throwloop.wav");
		gi.soundindex("weapons/knife/impact.wav");
		GhoulModelObject("Enemy/Bolt", a == 2 ? "w_knife" : "w_throw_star", NULL, false);
		if (a == 30)
			fxn("weapons/world/startrail");
		break;
	case 7:
		gi.soundindex("weapons/sniper/zoom.wav");
		fxn("weapons/playermz/sniper_alt");
		break;
	case 9:
		fxn("weapons/world/slugthrow");
		fxn("weapons/world/enemyslug");
		fxn("weapons/world/slughit");
		fxn("weapons/world/slugexplode");
		break;
	case 10:
		GhoulModelObject("Items/Projectiles", "grenade", NULL, false);
		fxn("weapons/world/sluggren");
		fxn("weapons/world/conctrail");
		gi.soundindex("Ambient/Gen/Electric/spark2.wav");
		break;
	case 11:
		gi.soundindex("weapons/shotgun/fire.wav");
		fxn("gore/shotgun");
		break;
	case 13:
		GhoulModelObject("Items/Projectiles", "grenade", NULL, false);
		fxn("weapons/world/phostrail");
		gi.soundindex("weapons/mgun/explode.wav");
		gi.soundindex("Weapons/gren/bounce.wav");
		fxn("weapons/world/phosexplode");
		fxn("weapons/world/phosburn");
		break;
	case 15:
		fxn("weapons/world/rocketexplode_alt");
		fxn("weapons/world/rocketsplit");
		fxn("weapons/world/rockettrail_alt1");
		fxn("weapons/world/rockettrail_alt2");
		// fall through
	case 14:
		GhoulModelObject("Weapon/Projectiles/Rocket", "Rocket", NULL, false);
		fxn("weapons/world/rocketexplode");
		fxn("weapons/world/rockettrail2");
		fxn("weapons/world/rockettrail_p");
		gi.soundindex("Weapons/Rocket/FlyLP.wav");
		break;
	case 16:
		fxn("weapons/world/mpgzap");
		fxn("weapons/othermz/mpgbeam1");
		fxn("weapons/othermz/mpgbeam2");
		fxn("environ/onfireburst");
		fxn("environ/quickfireburst");
		break;
	case 17:
	case 29:
		if (a == 29) {
			fxn("weapons/world/gb_dek2");
			fxn("weapons/othermz/gb_dekmz");
			gi.soundindex("Enemy/Dekker/Fire.wav");
		} else {
			fxn("weapons/world/mpgburst");
			fxn("weapons/othermz/mpgcharge");
			fxn("weapons/othermz/mpgcharge2");
		}
		fxn("environ/pulsecook");
		fxn("gore/mpgsplatter");
		gi.soundindex("Weapons/MPG/Expand.wav");
		gi.soundindex("Weapons/MPG/Burst.wav");
		for (int i = 1; i <= 4; i++)
			gi.soundindex(va("Weapons/MPG/alt%d.wav", i));
		gi.soundindex("Weapons/MPG/charge.wav");
		break;
	case 18:
		fxn("weapons/world/fthrower");
		fxn("weapons/world/flamehit");
		fxn("weapons/world/flamehitwall");
		fxn("environ/onfireburst");
		fxn("environ/quickfireburst");
		fxn("weapons/othermz/flamecharge");
		break;
	case 19:
		fxn("weapons/world/fballtrail");
		fxn("weapons/playermz/flamebuildup");
		fxn("weapons/playermz/fthrowidle");
		fxn("weapons/world/fballimpact");
		break;
	case 22:
		GhoulModelObject("Items/Projectiles", "grenade", NULL, false);
		break;
	}
}

//======================================================================
// shared explosion rules (SPEC-782 10)

void WhiteOut(const vec3_t point, float R, float cap, float fade)
{
	om_ai::FlashMute(point, R); /* G2: flash blinding of characters (SPEC-701 4) */
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client) // dead players too: a rocket that kills its shooter still blinds him (oracle)
			continue;
		vec3_t eye, d, fwd;
		VectorCopy(p->s.origin, eye);
		eye[2] += p->viewheight;
		VectorSubtract(point, eye, d);
		float dist = VectorNormalize(d);
		if (dist > R)
			continue;
		trace_t tr = gi.trace(point, NULL, NULL, eye, NULL, SHOT_MASK);
		if (tr.fraction < 0.99f)
			continue;
		AngleVectors(p->client->v_angle, fwd, NULL, NULL);
		float q = DotProduct(fwd, d);
		if (q <= 0)
			continue;
		float amount = R * 0.03f / (dist < 1 ? 1 : dist) * q;
		float C = cap, Fd = fade;
		if (p->client->goggles) {
			amount = amount < 0.4f ? amount + 0.4f : amount * 2;
			Fd *= 0.5f;
			C = C + (1 - C) * 0.5f;
		}
		gclient_t *c = p->client;
		c->flash_alpha += amount;
		if (c->flash_alpha > C)
			c->flash_alpha = C;
		if (c->flash_fade == 0 || c->flash_fade > Fd)
			c->flash_fade = Fd;
	}
}

static void Recoil(edict_t *ent, const vec3_t dir, float k)
{
	gclient_t *c = ent->client;
	if (!c)
		return;
	c->kick_pitch -= k;
	if (c->kick_pitch < -20)
		c->kick_pitch = -20;
	float e = c->kick_endtime + k / 30;
	if (e < level.time + k / 30)
		e = level.time + k / 30;
	c->kick_endtime = e;
	if (ent->movetype == MOVETYPE_NONE || ent->movetype == MOVETYPE_PUSH || ent->movetype == MOVETYPE_STOP ||
	    ent->movetype == MOVETYPE_NOCLIP || (ent->flags & FL_NO_KNOCKBACK))
		return;
	vec3_t v;
	VectorSet(v, -dir[0], -dir[1], -dir[2] * 0.25f);
	VectorNormalize(v);
	float m = ent->mass < 50 ? 50.0f : (float)ent->mass;
	VectorMA(ent->velocity, 12 * k * 250 / m, v, ent->velocity);
}

void SetOnFire(edict_t *targ, edict_t *attacker)
{
	if (game.lock_deaths || !targ->takedamage || (targ->flags & FL_GODMODE))
		return;
	if (targ->classname && (!strcmp(targ->classname, "m_x_mskinboss") || !strcmp(targ->classname, "m_x_mraiderboss1") ||
	                        !strcmp(targ->classname, "m_x_mraiderboss2")) &&
	    targ->count != 100)
		return;
	targ->burn_attacker = attacker;
	if (targ->health < 1 && targ->burn_time <= level.time + 2) {
		FX_EffectEnt(fxn("environ/onfireburst"), targ, MULTICAST_PVS);
		targ->burn_time = level.time + 4;
	} else
		FX_EffectEnt(fxn("environ/quickfireburst"), targ, MULTICAST_PVS);
}

static bool CinematicRemove(edict_t *ent)
{
	if (game.cinematic) {
		G_FreeEdict(ent);
		return true;
	}
	return false;
}

static bool SkyHit(edict_t *ent, csurface_t *surf)
{
	if (surf && (surf->flags & 4)) {
		G_FreeEdict(ent);
		return true;
	}
	return false;
}

static void GlassHit(edict_t *ent, edict_t *other, csurface_t *surf)
{
	int mat = surf ? ((unsigned)surf->flags >> 24) : 0;
	if ((mat == 37 || mat == 38 || (other && (other->material == 7))) && other && other->takedamage)
		T_Damage(other, ent, ent->owner, ent->velocity, ent->s.origin, vec3_origin, 1000, 1, 0x8200, MOD_ROCKET);
}

//======================================================================
// bullets (SPEC-782 3)

static edict_t *g_shotowner;
static vec3_t g_shotstart, g_shotdir;

static int ShotFilter(trace_t *tr)
{
	edict_t *e = tr->ent;
	if (!e || e == world)
		return 1;
	if (e->ghoulInst) {
		float dist;
		vec3_t pt;
		if (!GhoulRayHit(e, g_shotstart, g_shotdir, 1e9f, &dist, pt))
			return 0;
		VectorCopy(pt, tr->endpos);
		return 1;
	}
	if (tr->contents & CONTENTS_WINDOW)
		return 0;
	return 1;
}

static bool g_attack_nodamage; // environ_invisible_attack spawnflag 2: effects only (SPEC-782 11)

namespace om_ai { /* G2 (ai_api.h): the single-player sight client and the team rule */
edict_t *SightClient(void);
bool SameTeam(edict_t *a, edict_t *b);
}

static float DistToSegment(const vec3_t p, const vec3_t a, const vec3_t b)
{
	vec3_t ab, ap, c;
	VectorSubtract(b, a, ab);
	VectorSubtract(p, a, ap);
	float l2 = DotProduct(ab, ab), t = l2 > 0 ? DotProduct(ap, ab) / l2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	VectorMA(a, t, ab, c);
	VectorSubtract(p, c, c);
	return VectorLength(c);
}

// the target of the character attack being run (G3's fireAttack passes it)
static edict_t *g_attack_target;

// SPEC-782 3 step 6, single player: what the player hears of a character's shot. The fly-by is temp entity 9 to the
// player (distance and direction from the eye to the nearest point of the shot), the crack one of
// `weapons/fx/crack/crack1..3.wav` on the player, the ricochet temp entity 54 at the impact point. G18's reading of
// the distance/direction values (the spec gives the rule, not the encoding).
static void CharacterShotFeedback(const vec3_t start, const vec3_t end)
{
	edict_t *sc = om_ai::SightClient();
	if (!sc || !sc->client || !sc->inuse)
		return;
	vec3_t eye, ab, c, d;
	VectorCopy(sc->s.origin, eye);
	eye[2] += sc->viewheight;
	VectorSubtract(end, start, ab);
	float l2 = DotProduct(ab, ab);
	vec3_t ae;
	VectorSubtract(eye, start, ae);
	float t = l2 > 0 ? DotProduct(ae, ab) / l2 : 0;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	VectorMA(start, t, ab, c);
	VectorSubtract(c, eye, d);
	float dist = VectorNormalize(d);
	int what; // 0 fly-by, 1 crack, 2 ricochet, -1 nothing
	if (g_attack_target == sc)
		what = gix.irand(0, 1);
	else if (dist < 128)
		what = gix.irand(0, 2);
	else
		what = gix.flrand(0, 1) < 0.3f ? 2 : -1;
	if (what == 0) {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(9);
		gi.WriteByte(dist > 255 ? 255 : (int)dist);
		WriteUnit1000(d);
		WriteUnit1000(d);
		gi.WriteByte(0);
		gi.WriteByte(0);
		gi.unicast(sc, false);
	} else if (what == 1) {
		gi.sound(sc, CHAN_AUTO, gi.soundindex(va("weapons/fx/crack/crack%d.wav", gix.irand(1, 3))), 1, ATTN_NORM, 0);
	} else if (what == 2) {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(54);
		gi.WritePosition(end);
		gi.multicast(end, MULTICAST_PVS);
	}
}

// SPEC-782 3 step 4: a character's shot hurts a target of its own team only while a cinematic runs, or with
// probability 6/11 when the hit point lies within 12 units of the line from the muzzle to the single-player player
static bool CharacterShotHurts(edict_t *shooter, edict_t *targ, const vec3_t muzzle, const vec3_t point)
{
	if (shooter->client || !(targ->client || targ->ai) || !om_ai::SameTeam(shooter, targ))
		return true;
	if (game.cinematic)
		return true;
	edict_t *sc = om_ai::SightClient();
	return sc && DistToSegment(point, muzzle, sc->s.origin) <= 12 && gix.irand(0, 10) < 6;
}

static void ImpactTempEnt(int type, const trace_t &tr, const vec3_t dir, int power, int style)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(type);
	WriteImpactWorld(tr.endpos, tr.plane.normal);
	WriteUnit1000(dir);
	gi.WriteByte(power);
	gi.WriteByte(style);
	gi.WriteByte(rand() & 1);
	gi.multicast(tr.endpos, MULTICAST_PVS);
}


// SPEC-782 section 3 step 7: re-trace the shot segment with mask window|water so glass
// (CONTENTS_WINDOW / material 37-38 / entity material 7) takes base damage and water
// shows bulletsplash. The main SHOT_MASK omits WINDOW, so without this pass token-booth
// glass on tsr1 never breaks and leaves an invisible player clip.
static void BulletGlassWaterPass(edict_t *ent, int a, const vec3_t start, const vec3_t dir, const vec3_t impact)
{
	trace_t tw = gi.trace(start, NULL, NULL, impact, ent, 0x22);
	if (tw.fraction >= 1.0f)
		return;
	if ((tw.contents & CONTENTS_WATER) && !DM()) {
		FX_Effect(fxn("environ/bulletsplash"), tw.endpos, MULTICAST_PVS);
		return;
	}
	int mat = tw.surface ? (int)(((unsigned)tw.surface->flags >> 24) & 0xff) : 0;
	bool glassSurf = (mat == 37 || mat == 38);
	bool glassEnt = tw.ent && tw.ent != world && tw.ent->material == 7;
	bool glassWin = (tw.contents & CONTENTS_WINDOW) != 0;
	if (!(glassSurf || glassEnt || glassWin))
		return;
	if (!tw.ent || tw.ent == world || !tw.ent->takedamage || g_attack_nodamage) {
		gi.dprintf("glasspass miss: ent=%s take=%d nodmg=%d mat=%d win=%d\n",
			tw.ent ? (tw.ent->classname ? tw.ent->classname : "?") : "null",
			tw.ent && tw.ent != world ? tw.ent->takedamage : -1,
			g_attack_nodamage ? 1 : 0, mat, glassWin ? 1 : 0);
		return;
	}
	{
		int enumber = (int)(tw.ent - g_edicts);
		int hp0 = tw.ent->health;
		gi.dprintf("glasspass HIT %s#%d model=%s hp=%d take=%d dmg=%d mat=%d solid=%d die=%s\n",
			tw.ent->classname ? tw.ent->classname : "?", enumber,
			tw.ent->model ? tw.ent->model : "?", hp0, tw.ent->takedamage,
			AttackDamage(a), mat, (int)tw.ent->solid, tw.ent->die ? "yes" : "no");
		int basedmg = AttackDamage(a);
		int flags = AttackFlags(a);
		if (basedmg < 1000 && (tw.ent->material == 7 || mat == 37 || mat == 38))
			basedmg = 1000;
		T_Damage(tw.ent, ent, ent, dir, tw.endpos, start, basedmg, 1, flags | 0x200, attacks[a].mod);
		edict_t *e2 = &g_edicts[enumber];
		gi.dprintf("glasspass AFTER #%d inuse=%d hp=%d take=%d\n",
			enumber, e2->inuse ? 1 : 0, e2->health, (int)e2->takedamage);
		if (e2->inuse && !DM() && !(e2->client || (e2->svflags & SVF_MONSTER)))
			ImpactTempEnt(6, tw, dir, attacks[a].imp, 1);
	}
	G_AlertNoise(0.2f);
}

static void FireBullet(edict_t *ent, int a, const vec3_t start_in, const vec3_t dir, float rangefactor = 3)
{
	vec3_t start, end;
	VectorCopy(start_in, start);
	int range = AttackRange(a);
	float len = range == 128 ? 256.0f : range * rangefactor;
	trace_t tr = gi.trace(ent->s.origin, NULL, NULL, start, ent, SHOT_MASK);
	if (tr.fraction <= 0.99f) {
		// blocked between the owner and the muzzle
	} else {
		VectorMA(start, len, dir, end);
		g_shotowner = ent;
		VectorCopy(start, g_shotstart);
		VectorCopy(dir, g_shotdir);
		memset(&tr, 0, sizeof(tr));
		gix.tracefilter(start, NULL, NULL, end, ent, SHOT_MASK, &tr, ShotFilter);
	}
	int dmg = AttackDamage(a);
	int flags = AttackFlags(a);
	if (!(flags & (0x1000000 | 0x80000)) && tr.fraction > 0.5f)
		dmg = (int)(dmg / (2 * tr.fraction));
	if ((flags & (0x1000000 | 0x80000)) && !ent->client)
		dmg *= 2;

	// deathmatch: the multiplayer hit temp entity goes out before the damage and its effects (oracle order, G7
	// capture g7shot), then the damage (SPEC-782 3 steps 4, 5)
	if (tr.fraction < 1.0f && DM()) {
		if (tr.surface)
			gix.damagetexture(tr.surface, 25);
		bool worldhit = !tr.ent || tr.ent == world || tr.ent->solid == SOLID_BSP;
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(worldhit ? (attacks[a].T ? 62 : 61) : 60);
		gi.WriteByte(fxi(attacks[a].tracer));
		gi.WriteByte(fxi(attacks[a].D));
		gi.WriteByte(G_EdictNum(ent) & 0xFF);
		gi.WritePosition(start);
		if (worldhit)
			WriteUnit2048(dir);
		else
			gi.WritePosition(tr.endpos);
		// the shooter receives it too (the impact mark is drawn for the local player as well, SPEC-401
		// types 61/62; observed: 6 world hits -> 6 temp entities 61 to the shooting client, orig_walk)
		gi.multicast(start, MULTICAST_PVS);
	}

	if (tr.ent && tr.ent != world && tr.ent->takedamage && !g_attack_nodamage &&
	    CharacterShotHurts(ent, tr.ent, start, tr.endpos))
		T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, dmg, dmg, flags | 0x200, attacks[a].mod);

	if (tr.fraction < 1.0f && !DM()) {
		if (!tr.ent || !(tr.ent->client || (tr.ent->svflags & SVF_MONSTER))) {
			bool door = tr.ent && tr.ent->classname && strstr(tr.ent->classname, "door");
			ImpactTempEnt(6, tr, dir, attacks[a].imp, door ? 0 : 1);
			if (ent->client)
				om_ai::ShotAtNotice(ent, start, tr.endpos); /* G2: "shot at me" notices (SPEC-710 E1) */
		}
		if (!ent->client)
			CharacterShotFeedback(start, tr.endpos);
		// characters' world hits show the tracer from the muzzle with probability 1/4 (SPEC-782 3 step 6)
		if (!ent->client && attacks[a].tracer && (!tr.ent || tr.ent == world) && gix.irand(0, 3) == 0)
			FX_EffectBeam(fxi(attacks[a].tracer), start, tr.endpos, MULTICAST_PVS);
	}
	// the attack's alert was raised once by RunAttack (SPEC-782 2); a second raise here doubled it (G9: nyc1 meter
	// 4 per pistol shot, the oracle 2)
	G_ReportNoise(ent, start, 800, 1);

	{
		vec3_t impact;
		if (tr.fraction < 1.0f)
			VectorCopy(tr.endpos, impact);
		else
			VectorMA(start, len, dir, impact);
		BulletGlassWaterPass(ent, a, start, dir, impact);
	}
}

// SPEC-782 4
static void FireShotgun(edict_t *ent, const vec3_t start_in, const vec3_t dir)
{
	vec3_t start, right, up;
	VectorCopy(start_in, start);
	trace_t tr = gi.trace(ent->s.origin, NULL, NULL, start, ent, SHOT_MASK);
	if (tr.fraction <= 0.99f)
		VectorCopy(ent->s.origin, start);
	FireBullet(ent, 11, start, dir);

	vec3_t ang;
	vectoangles(dir, ang);
	AngleVectors(ang, NULL, right, up);
	edict_t *hits[12];
	int counts[12], nh = 0;
	for (int i = 0; i < 12; i++) {
		vec3_t d, end;
		for (int j = 0; j < 3; j++)
			d[j] = dir[j] + right[j] * gix.flrand(-0.1f, 0.1f) + up[j] * gix.flrand(-0.05f, 0.05f);
		VectorNormalize(d);
		VectorMA(start, (float)gix.irand(256, 512), d, end);
		tr = gi.trace(start, NULL, NULL, end, ent, SHOT_MASK);
		if (!tr.ent || tr.fraction == 1.0f)
			continue;
		if (tr.ent->client || tr.ent->ai || (tr.ent->svflags & SVF_MONSTER)) {
			int k;
			for (k = 0; k < nh; k++)
				if (hits[k] == tr.ent)
					break;
			if (k == nh) {
				hits[nh] = tr.ent;
				counts[nh++] = 0;
			}
			counts[k]++;
		} else if (tr.ent->takedamage)
			T_Damage(tr.ent, ent, ent, d, tr.endpos, start, 12, 100, attacks[11].flags, MOD_SHOTGUN);
		{
			vec3_t impact;
			if (tr.fraction < 1.0f)
				VectorCopy(tr.endpos, impact);
			else
				VectorCopy(end, impact);
			BulletGlassWaterPass(ent, 11, start, d, impact);
		}
	}
	for (int k = 0; k < nh; k++) {
		if (ent->client) {
			T_Damage(hits[k], ent, ent, dir, hits[k]->s.origin, start, 14 * counts[k], 100, attacks[11].flags,
			         MOD_SHOTGUN);
			continue;
		}
		// character shooter (SPEC-782 4 step 4): 10 per pellet, flags | 0x8 against non-teammates; a teammate is hurt
		// only with probability 2/10 and only when it is not within 16 units of the line muzzle → player
		if ((hits[k]->client || hits[k]->ai) && om_ai::SameTeam(ent, hits[k])) {
			edict_t *sc = om_ai::SightClient();
			if (gix.irand(0, 9) >= 2 || (sc && DistToSegment(hits[k]->s.origin, start, sc->s.origin) <= 16))
				continue;
			T_Damage(hits[k], ent, ent, dir, hits[k]->s.origin, start, 10 * counts[k], 100, attacks[11].flags,
			         MOD_SHOTGUN);
		} else
			T_Damage(hits[k], ent, ent, dir, hits[k]->s.origin, start, 10 * counts[k], 100, attacks[11].flags | 0x8,
			         MOD_SHOTGUN);
	}
	vec3_t d32;
	VectorScale(dir, 1.0f / 32.0f * 32.0f, d32);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(34);
	gi.WritePosition(start);
	vec3_t s32;
	VectorScale(dir, 32, s32);
	gi.WritePosition(s32);
	gi.multicast(start, MULTICAST_PHS);
}

// SPEC-782 5
static void KnifeSlash(edict_t *ent, const vec3_t start, const vec3_t dir, int damage)
{
	vec3_t end, back;
	VectorMA(start, 48, dir, end);
	VectorMA(start, -16, dir, back);
	trace_t tr = gi.trace(back, NULL, NULL, end, ent, 0x60000b9);
	if (tr.fraction == 1.0f || !tr.ent)
		return;
	if (!DM() && !(tr.ent->client || (tr.ent->svflags & SVF_MONSTER)))
		ImpactTempEnt(6, tr, dir, 6, 2);
	if (tr.ent->takedamage) {
		T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, damage, 1, 0x400100, MOD_KNIFE_SLASH);
		if (ent->client && !ent->client->pers.isbot && ent->client->inv &&
		    (tr.ent->client || (tr.ent->svflags & SVF_MONSTER)) &&
		    !gix.cvar_value("lock_blood"))
			ent->client->inv->WeaponEvent(1);
	}
}

//======================================================================
// projectiles (SPEC-782 5-7)

static edict_t *NewProjectile(edict_t *owner, const vec3_t start, const vec3_t vel, const char *cls, int mt,
                              float G, float E, float Fg, float Fa, const float *mins, const float *maxs, int mask)
{
	edict_t *p = G_Spawn();
	p->classname = (char *)cls;
	VectorCopy(start, p->s.origin);
	VectorCopy(vel, p->velocity);
	vectoangles(vel, p->s.angles);
	p->movetype = mt;
	p->solid = SOLID_NOT;
	p->clipmask = mask;
	p->owner = owner;
	p->gravity = G;
	p->elasticity = E;
	p->friction = Fg;
	p->airfriction = Fa;
	VectorCopy(mins, p->mins);
	VectorCopy(maxs, p->maxs);
	return p;
}

static void ProjectileExplodeRadius(edict_t *ent, float M, float m, float R, int mod, int flags, edict_t *ignore,
                                    const char *effect, float wR, float wC, float wF, float alert)
{
	vec3_t c;
	VectorCopy(ent->s.origin, c);
	T_RadiusDamageMinMax(ent, ent->owner, c, M, m, R, ignore, mod, flags);
	if (effect)
		FX_Effect(fxn(effect), c, MULTICAST_PHS);
	if (wR > 0)
		WhiteOut(c, wR, wC, wF);
	G_AlertNoise(alert);
	G_ReportNoise(ent->owner, c, 1000, 2);
}

// SPEC-AMEND-006 G18.31: the weapon impact of a projectile touch (rocket 20, split-rocket piece 10, slug 10; mark
// style 0, no debris), once per explosion, before the explosion damage: the touch's own trace (impact descriptor of
// the touched surface or character), direction = the reversed plane normal, with the SPEC-771 C.2 rules (sky,
// lock_blood on characters, the 0.2 s limit). Oracle self-rocket captures: (0, 0, -1) on a floor, power 20.
static void ProjectileImpact(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf, int power)
{
	if (!plane)
		return; // exploding on its own (timeout, stopped): no touch trace
	trace_t tr;
	memset(&tr, 0, sizeof(tr));
	VectorCopy(self->s.origin, tr.endpos);
	tr.plane = *plane;
	tr.surface = surf;
	tr.ent = other;
	vec3_t dir;
	VectorNegate(plane->normal, dir);
	fx::WeaponImpact(tr, dir, power, 0, 0, false);
}

// rocket (14) and split halves
static void rocket_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (CinematicRemove(self) || SkyHit(self, surf))
		return;
	GlassHit(self, other, surf);
	bool half = self->count == 2;
	ProjectileImpact(self, other, plane, surf, half ? 10 : 20);
	if (self->owner && self->owner->client) {
		if (other && other->takedamage)
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane ? plane->normal : vec3_origin,
			         half ? 150 : 250, half ? 175 : 300, 0x8000, MOD_ROCKET);
		ProjectileExplodeRadius(self, half ? 200.0f : 225.0f, 32, half ? 150.0f : 175.0f, MOD_ROCKET_SPLASH, 0x108000,
		                        other, half ? "weapons/world/rocketexplode_alt" : "weapons/world/rocketexplode",
		                        half ? 1000.0f : 1500.0f, 0.7f, 0.5f, 0.4f);
	} else
		ProjectileExplodeRadius(self, 225, 32, 175, MOD_ROCKET_SPLASH, 0x908000, NULL, "weapons/world/rocketexplode", 0,
		                        0, 0, 0.2f);
	G_FreeEdict(self);
}

static void rocket_think(edict_t *self)
{
	if (--self->health <= 0 || VectorCompare(self->velocity, vec3_origin)) {
		rocket_touch(self, world, NULL, NULL);
		return;
	}
	self->nextthink = level.time + FRAMETIME;
}

static edict_t *SpawnRocket(edict_t *owner, const vec3_t start, const vec3_t dir, float speed, const char *trail,
                            float scale, int half)
{
	vec3_t vel;
	VectorScale(dir, speed, vel);
	vec3_t mins = {-1, -1, -1}, maxs = {1, 1, 1};
	edict_t *r = NewProjectile(owner, start, vel, "rocket", MOVETYPE_DANTOUCH, 0, 0, 0, 0, mins, maxs, PROJ_MASK);
	GhoulSetModel(r, "Weapon/Projectiles/Rocket", "Rocket", NULL, scale);
	FX_SetSlot(r, 0, trail);
	r->s.sound = gi.soundindex("Weapons/Rocket/FlyLP.wav");
	r->s.sounddata = 0xF8;
	r->health = 100;
	r->count = half;
	r->touch = rocket_touch;
	r->think = rocket_think;
	r->nextthink = level.time + FRAMETIME;
	gi.linkentity(r);
	return r;
}

static void splitrocket_think(edict_t *self)
{
	if (level.time - self->timestamp < 0.2f - 0.001f) {
		rocket_think(self);
		return;
	}
	vec3_t up, ang;
	VectorCopy(self->s.angles, ang);
	ang[ROLL] += gix.flrand(0, 180);
	AngleVectors(ang, NULL, NULL, up);
	for (int s = -1; s <= 1; s += 2) {
		vec3_t start, vel;
		VectorMA(self->s.origin, 6.0f * s, up, start);
		VectorMA(self->velocity, 48.0f * s, up, vel);
		float speed = VectorNormalize(vel);
		edict_t *h = SpawnRocket(self->owner, start, vel, speed, "weapons/world/rockettrail_alt2", 0.5f, 2);
		h->health = 100;
	}
	FX_Effect(fxn("weapons/world/rocketsplit"), self->s.origin, MULTICAST_PHS);
	G_FreeEdict(self);
}

// slug (9)
static void slug_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (CinematicRemove(self) || SkyHit(self, surf))
		return;
	GlassHit(self, other, surf);
	ProjectileImpact(self, other, plane, surf, 10);
	if (other && other->takedamage)
		T_Damage(other, self, self->owner, self->velocity, self->s.origin, vec3_origin, 100, 100, 0x20000, MOD_SLUG);
	T_RadiusDamage(self, self->owner, 60, other, 80, MOD_SLUG, 0x108000);
	FX_Effect(fxn("weapons/world/slugexplode"), self->s.origin, MULTICAST_PHS);
	G_FreeEdict(self);
}

// concussion grenade (10)
static void conc_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (CinematicRemove(self) || SkyHit(self, surf))
		return;
	vec3_t c;
	VectorCopy(self->s.origin, c);
	if (plane)
		VectorMA(c, 8, plane->normal, c);
	FX_Effect(fxn("weapons/world/sluggren"), c, MULTICAST_PHS);
	G_AlertNoise(0.1f);
	edict_t *list[1024];
	int n = G_RadiusList(c, 210, list, 1);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (!e->takedamage)
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, c, d);
		float dist = VectorLength(d);
		trace_t tr = gi.trace(c, NULL, NULL, e->s.origin, self, 3);
		if (tr.fraction < 1 && tr.ent != e)
			continue;
		float f = 1 - 0.004f * dist;
		int K = (int)(400 * f);
		if (e == self->owner)
			K /= 2;
		if (e->client) {
			T_Damage(e, self, self->owner, d, e->s.origin, c, (int)(0.15f * K), K, 0x80, MOD_CONC_GRENADE);
			e->client->flash_alpha += f + 0.2f;
			if (e->client->flash_alpha > 0.95f)
				e->client->flash_alpha = 0.95f;
			e->client->flash_fade = 0.15f;
			G_AddEvent(e, 17 + (int)(3.2f * f), 30);
		} else {
			int k6 = (int)(0.6f * K);
			T_Damage(e, self, self->owner, d, e->s.origin, c, (int)(0.05f * k6), k6, 0x80, MOD_CONC_GRENADE);
		}
	}
	G_FreeEdict(self);
}

// phosphorus grenade (13)
static void phos_explode(edict_t *self)
{
	if (CinematicRemove(self))
		return;
	vec3_t c;
	VectorCopy(self->s.origin, c);
	c[2] += 8;
	FX_Effect(fxn("weapons/world/phosexplode"), c, MULTICAST_PHS);
	G_AlertNoise(0.3f);
	edict_t *list[1024];
	int n = G_RadiusList(c, 120, list, 1);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (!e->takedamage || !(e->client || (e->svflags & SVF_MONSTER)))
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, c, d);
		float dist = VectorLength(d);
		trace_t tr = gi.trace(c, NULL, NULL, e->s.origin, self, MASK_SOLID);
		if (tr.fraction < 1 && tr.ent != e)
			continue;
		T_Damage(e, self, self->owner, d, e->s.origin, c, (int)((150 - dist) * 0.5f), 1, 0x1000, MOD_PHOS_GRENADE);
		if (e->burn_time > level.time)
			e->burn_time += 0.5f;
		else {
			FX_EffectEnt(fxn("weapons/world/phosburn"), e, MULTICAST_PVS);
			e->burn_time = level.time + 2;
		}
	}
	G_FreeEdict(self);
}

static void phos_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (other && (other->client || (other->svflags & SVF_MONSTER))) {
		phos_explode(self);
		return;
	}
	int mat = surf ? ((unsigned)surf->flags >> 24) : 0;
	static const int soft[] = {2, 3, 4, 5, 9, 10, 11, 12, 13, 25, 26, 27, 33, 34, 36, 40};
	bool issoft = false;
	for (int s : soft)
		issoft |= s == mat;
	if (!issoft)
		G_Sound(self, CHAN_AUTO, "Weapons/gren/bounce.wav", 0.6f, ATTN_IDLE);
}

// fireball (19) and the fire pool
static void firepool_think(edict_t *self)
{
	if (self->count <= 0) {
		G_FreeEdict(self);
		return;
	}
	T_RadiusDamage(self, self->owner, 2.0f * self->count, NULL, 100, MOD_FLAMEGUN_NAPALM, 0x1002);
	self->count--;
	self->nextthink = level.time + 0.2f;
}

static void fireball_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (CinematicRemove(self) || SkyHit(self, surf))
		return;
	if ((!plane || plane->normal[2] <= 0.5f) && self->health > 0) {
		self->health--;
		return;
	}
	G_AlertNoise(0.3f);
	T_RadiusDamage(self, self->owner, 300, NULL, 150, MOD_FLAMEGUN_NAPALM, 0x9000);
	FX_Effect(fxn("weapons/world/fballimpact"), self->s.origin, MULTICAST_PHS);
	self->movetype = MOVETYPE_NONE;
	VectorClear(self->velocity);
	FX_ClearSlots(self);
	self->touch = NULL;
	self->count = 15;
	self->think = firepool_think;
	self->nextthink = level.time + 0.2f;
}

// thrown knife (2)
static void knife_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (SkyHit(self, surf))
		return;
	vec3_t dir;
	VectorCopy(self->velocity, dir);
	VectorNormalize(dir);
	float life = (DM() && ((int)dmflags->value & 0x800)) || deathmatch->value == 3 ? 10.0f : 30.0f;
	if (other && other->takedamage) {
		dmg_knifeDeflected = false;
		T_Damage(other, self, self->owner, dir, self->s.origin, vec3_origin, 30, 0, 0x1000100, MOD_KNIFE_THROWN);
		if (other->client || (other->svflags & SVF_MONSTER)) {
			// a knife the damage system deflected (armour, G3) lies 40 units in front of the target as a pickup
			// (SPEC-782 6); otherwise it is used up
			if (dmg_knifeDeflected) {
				dmg_knifeDeflected = false;
				vec3_t at;
				VectorMA(self->s.origin, -40, dir, at);
				SpawnKnifePickup(at, self->s.angles, life);
			}
			G_FreeEdict(self);
			return;
		}
	}
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(32);
	gi.WritePosition(self->s.origin);
	gi.WriteDir(plane ? plane->normal : vec3_origin);
	gi.WriteByte(3);
	gi.multicast(self->s.origin, MULTICAST_PVS);
	vec3_t ang;
	if (plane)
		vectoangles(plane->normal, ang);
	else
		VectorCopy(self->s.angles, ang);
	SpawnKnifePickup(self->s.origin, ang, life);
	G_FreeEdict(self);
}

static void projectile_expire(edict_t *self) { G_FreeEdict(self); }

//======================================================================
// beams (SPEC-782 8)

static void MPGCook(edict_t *e)
{
	if (e->client) {
		e->client->flash_alpha += 0.3f;
		if (e->client->flash_alpha > 0.9f)
			e->client->flash_alpha = 0.9f;
		e->client->flash_fade = 0.2f;
		G_AddEvent(e, 20, 30);
		e->client->zapped_time = level.time + 0.8f;
	}
	if (e->health > 0) {
		if (level.time - e->mpg_timer >= 1) {
			FX_EffectEnt(fxn("weapons/world/mpgzap"), e, MULTICAST_PVS);
			e->mpg_timer = level.time;
		}
	}
}

static void MPGBeam(edict_t *ent, const vec3_t start, const vec3_t dir)
{
	trace_t tr = gi.trace(ent->s.origin, NULL, NULL, start, ent, SHOT_MASK);
	if (tr.startsolid)
		return;
	vec3_t s, end;
	VectorCopy(start, s);
	VectorMA(start, 1024, dir, end);
	for (int i = 0; i < 12; i++) {
		tr = gi.trace(s, NULL, NULL, end, ent, SHOT_MASK);
		if (!tr.ent || tr.fraction == 1.0f)
			break;
		edict_t *e = tr.ent;
		if (e == world || !e->takedamage)
			break;
		float T = e->mpg_timer;
		int dmg;
		if (T <= level.time) {
			dmg = DM() ? 4 : 2;
			e->mpg_timer = level.time + (DM() ? 0.4f : 0.2f);
			if (DM())
				G_Sound(e, CHAN_BODY, "Ambient/Gen/Electric/spark2.wav", 1, ATTN_IDLE);
		} else {
			dmg = (int)((T - level.time) * (DM() ? 30 : 15));
			e->mpg_timer += DM() ? 0.4f : 0.2f;
		}
		T_Damage(e, ent, ent, dir, tr.endpos, start, dmg, 0, 0x200000, MOD_MPG);
		MPGCook(e);
		VectorMA(tr.endpos, 8, dir, s);
	}
	FX_EffectBeam(fxn(level.framenum & 1 ? "weapons/othermz/mpgbeam1" : "weapons/othermz/mpgbeam2"), start,
	              tr.endpos, MULTICAST_PHS);
}

static void MPGBurst(edict_t *ent, const vec3_t start, const vec3_t dir)
{
	vec3_t b1 = {-3, -3, -3}, b2 = {3, 3, 3}, s, end;
	trace_t tr = gi.trace(ent->s.origin, b1, b2, start, ent, SHOT_MASK);
	if (tr.ent && tr.ent != world && tr.ent->takedamage && tr.fraction < 1)
		T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, 240, 100, 0x800, MOD_MPG);
	VectorCopy(start, s);
	VectorMA(start, 1024, dir, end);
	for (int i = 0; i < 12; i++) {
		tr = gi.trace(s, NULL, NULL, end, ent, SHOT_MASK);
		if (!tr.ent || tr.fraction == 1.0f || tr.ent == world)
			break;
		if (tr.ent->takedamage)
			T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, 240, 100, 0x800, MOD_MPG);
		VectorMA(tr.endpos, 8, dir, s);
	}
	FX_EffectDir(fxn("weapons/world/mpgburst"), start, dir, MULTICAST_PHS);
}

static void FlameStream(edict_t *ent, const vec3_t start, const vec3_t dir)
{
	FX_EffectDir(fxn("weapons/world/fthrower"), start, dir, MULTICAST_PHS);
	vec3_t b1 = {-1, -1, -1}, b2 = {1, 1, 1}, s, end;
	trace_t tr = gi.trace(ent->s.origin, b1, b2, start, ent, SHOT_MASK);
	if (tr.startsolid || (tr.fraction < 1 && (!tr.ent || tr.ent == world)))
		return;
	float reach = DM() ? 325.0f : 280.0f;
	VectorCopy(start, s);
	VectorMA(start, reach, dir, end);
	for (int i = 0; i < 3; i++) {
		tr = gi.trace(s, NULL, NULL, end, ent, SHOT_MASK);
		if (!tr.ent || tr.fraction == 1.0f)
			break;
		if (tr.ent == world || tr.ent->solid == SOLID_BSP) {
			vec3_t d;
			VectorSubtract(tr.endpos, start, d);
			if ((level.framenum & 1) && VectorLength(d) < 175)
				FX_Effect(fxn("weapons/world/flamehitwall"), tr.endpos, MULTICAST_PVS);
			break;
		}
		if (tr.ent != ent && tr.ent->takedamage) {
			T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, DM() ? 16 : 10, 0, 0x1008, MOD_FLAMEGUN);
			if (level.framenum & 1)
				SetOnFire(tr.ent, ent);
		}
		VectorMA(tr.endpos, 8, dir, s);
	}
}

//======================================================================
// aiming and dispatch (SPEC-782 1, 2)

//======================================================================
// attacks shared by players and characters (SPEC-782 2-8, 11); characters reach them through G_FireAttack

static void grenade_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
static void grenade_think(edict_t *self);
static void flashpack_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
static void flashpack_explode(edict_t *self);
static void ThrowSetup(edict_t *e);

// melee strike (attack 20, characters; SPEC-782 11); the caller's hit callback is not part of this interface
static void MeleeStrike(edict_t *ent, const vec3_t start, const vec3_t dir)
{
	vec3_t a, b;
	VectorMA(start, -16, dir, a);
	VectorMA(start, 48, dir, b);
	trace_t tr = gi.trace(a, NULL, NULL, b, ent, 0x60000b9);
	if (tr.fraction == 1.0f || !tr.ent)
		return;
	if (tr.ent->takedamage)
		T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, 30, 100, 0x100, MOD_DOGBITE);
}

// molotov (attack 23, characters; SPEC-782 7)
static void molotov_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (CinematicRemove(self) || SkyHit(self, surf))
		return;
	FX_Effect(fxn("weapons/world/molitovburst"), self->s.origin, MULTICAST_PHS);
	T_RadiusDamage(self, self->owner, 50, NULL, 150, MOD_FIRE, 0x1000);
	edict_t *e = NULL;
	while ((e = findradius(e, self->s.origin, 150)) != NULL) {
		if (!e->inuse || !e->takedamage)
			continue;
		trace_t tr = gi.trace(self->s.origin, NULL, NULL, e->s.origin, self, MASK_SOLID);
		if (tr.fraction == 1.0f || tr.ent == e)
			SetOnFire(e, self->owner);
	}
	G_FreeEdict(self);
}

// throwing star (attack 30; SPEC-782 6)
static void star_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (SkyHit(self, surf))
		return;
	VectorClear(self->avelocity);
	FX_ClearSlots(self);
	self->s.sound = 0;
	vec3_t dir;
	VectorCopy(self->velocity, dir);
	VectorNormalize(dir);
	if (other && other->takedamage) {
		T_Damage(other, self, self->owner, dir, self->s.origin, vec3_origin, 30, 0, 0x1000100, MOD_THROWINGSTAR);
		if (other->client || (other->svflags & SVF_MONSTER)) {
			G_FreeEdict(self);
			return;
		}
	}
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(32);
	gi.WritePosition(self->s.origin);
	gi.WriteDir(plane ? plane->normal : vec3_origin);
	gi.WriteByte(3);
	gi.multicast(self->s.origin, MULTICAST_PVS);
	VectorClear(self->velocity);
	self->movetype = MOVETYPE_NONE;
	self->touch = NULL;
	self->think = G_FreeEdict;
	self->nextthink = level.time + 10;
	gi.linkentity(self);
}

// MPG burst of a character: a marker fires it 0.4 s later from the recorded start and direction if the character
// still lives (SPEC-782 8)
static void MPGBurst(edict_t *ent, const vec3_t start, const vec3_t dir);
static void mpgburst_marker(edict_t *self)
{
	edict_t *o = self->owner;
	if (o && o->inuse && o->health > 0)
		MPGBurst(o, self->pos1, self->movedir);
	G_FreeEdict(self);
}

// Dekker pulse gun (attack 29; SPEC-782 8)
static void DekkerGun(edict_t *ent, const vec3_t start, const vec3_t dir)
{
	vec3_t s, end;
	VectorCopy(start, s);
	VectorMA(start, 1024, dir, end);
	trace_t tr;
	memset(&tr, 0, sizeof(tr));
	for (int i = 0; i < 12; i++) {
		tr = gi.trace(s, NULL, NULL, end, ent, SHOT_MASK);
		if (!tr.ent || tr.fraction == 1.0f)
			break;
		if (!DM() && !(tr.ent->client || (tr.ent->svflags & SVF_MONSTER)))
			ImpactTempEnt(6, tr, dir, attacks[29].imp, 1);
		if (tr.ent == world)
			break;
		if (tr.ent->takedamage)
			T_Damage(tr.ent, ent, ent, dir, tr.endpos, start, 240, 100, 0x2000800, MOD_MPG);
		VectorMA(tr.endpos, 8, dir, s);
	}
	FX_EffectDir(fxn("weapons/world/gb_dek2"), start, dir, MULTICAST_PHS);
	gi.sound(ent, CHAN_WEAPON, gi.soundindex("Enemy/Dekker/Fire.wav"), 1, ATTN_NONE, 0);
}

static void RunAttack(edict_t *ent, int a, const vec3_t start_in, const vec3_t dir_in, float throwspeed)
{
	gclient_t *c = ent->client;
	vec3_t start, dir, up;
	VectorCopy(start_in, start);
	VectorCopy(dir_in, dir);
	{
		vec3_t ang;
		if (c)
			VectorCopy(c->v_angle, ang);
		else
			vectoangles(dir, ang);
		AngleVectors(ang, NULL, NULL, up);
	}
	G_AlertNoise(c ? attacks[a].alertP : attacks[a].alertC);
	// characters firing through a model with a `flash` bolt: the D (deathmatch) or S (single player) muzzle effect
	// (SPEC-782 2; shown at the muzzle point the human body passes as the start). Vehicles and other bodies emit their
	// own on their muzzle bolt (SPEC-731; G4: irq2b tank MG showed two effects per shot)
	int fam = !c && ent->g3body ? ent->g3body->family : 0;
	if (!c && (fam == BODY_HUMAN || fam == BODY_MESO || fam == BODY_ECTO || fam == BODY_FEMALE ||
	           fam == BODY_MESO_ARMOURED)) {
		const char *mz = DM() ? attacks[a].D : attacks[a].S;
		/* the table names are relative to weapons/ (fxi); the body gets the full effect name */
		if (mz && !ent->g3body->MuzzleEffect(va("weapons/%s", mz)))
			FX_EffectDir(fxi(mz), start, dir, MULTICAST_PVS);
	}

	vec3_t mins2 = {-2, -2, -3}, maxs2 = {2, 2, 3}, mins1 = {-1, -1, -1}, maxs1 = {1, 1, 1};
	switch (a) {
	case 1:
	case 27:
		KnifeSlash(ent, start, dir, a == 27 ? 30 : 22);
		break;
	case 2: {
		vec3_t st, vel;
		VectorMA(start, 10, dir, st);
		VectorScale(dir, 800, vel);
		edict_t *k = NewProjectile(ent, st, vel, "thrown_knife", MOVETYPE_DANTOUCH, 0.7f, 0.4f, 1, 0, mins1, maxs1,
		                           PROJ_MASK);
		k->s.angles[YAW] += 90;
		k->avelocity[PITCH] = -2160;
		k->buoyancy = 0.7f;
		k->health = 31337;
		k->g3flags |= G3F_THROWN_KNIFE; // the damage system's thrown-knife rules (SPEC-721 10, G3)
		GhoulSetModel(k, "Enemy/Bolt", "w_knife", NULL, 2.0f);
		k->s.sound = gi.soundindex("weapons/knife/throwloop.wav");
		k->s.sounddata = 0xF8;
		k->touch = knife_touch;
		k->think = projectile_expire;
		k->nextthink = level.time + 30;
		gi.linkentity(k);
		break;
	}
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 12:
	case 21:
	case 28:
		FireBullet(ent, a, start, dir);
		break;
	case 8:
		break; // zoom only
	case 9: {
		vec3_t vel;
		VectorScale(dir, c ? 1200 : 600, vel);
		edict_t *sl = NewProjectile(ent, start, vel, "slug", MOVETYPE_DANTOUCH, 0, 0, 1, 0, mins1, maxs1, PROJ_MASK);
		sl->buoyancy = 0.7f;
		sl->touch = slug_touch;
		sl->think = projectile_expire;
		sl->nextthink = level.time + 5;
		FX_Effect(fxn(c ? "weapons/world/slugthrow" : "weapons/world/enemyslug"), start, MULTICAST_PHS);
		gi.linkentity(sl);
		Recoil(ent, dir, 0);
		break;
	}
	case 10: {
		vec3_t vel;
		VectorScale(dir, 800, vel);
		vel[2] += 150;
		edict_t *g = NewProjectile(ent, start, vel, "conc_grenade", MOVETYPE_DANTOUCH, 1, 0, 1, 0, mins2, maxs2,
		                           PROJ_MASK);
		GhoulSetModel(g, "Items/Projectiles", "grenade", NULL, 1.0f);
		g->avelocity[PITCH] = 1000;
		g->health = 40;
		FX_SetSlot(g, 0, "weapons/world/conctrail");
		g->touch = conc_touch;
		g->think = projectile_expire;
		g->nextthink = level.time + 6;
		gi.linkentity(g);
		break;
	}
	case 11:
		FireShotgun(ent, start, dir);
		break;
	case 13: {
		vec3_t vel, st;
		VectorCopy(start, st);
		st[2] -= 16;
		VectorScale(dir, 600, vel);
		VectorMA(vel, gix.flrand(100, 300), up, vel);
		edict_t *g = NewProjectile(ent, st, vel, "phos_grenade", MOVETYPE_DANTOUCH, 1, 0.7f, 0.7f, 0, mins2, maxs2,
		                           PROJ_MASK);
		GhoulSetModel(g, "Items/Projectiles", "grenade", NULL, 1.0f);
		FX_SetSlot(g, 0, "weapons/world/phostrail");
		g->touch = phos_touch;
		g->think = phos_explode;
		g->nextthink = level.time + 1.5f;
		gi.linkentity(g);
		break;
	}
	case 14: {
		vec3_t st;
		trace_t tr = gi.trace(ent->s.origin, tv(-2, -2, -2), tv(2, 2, 2), start, ent, PROJ_MASK);
		VectorCopy(tr.endpos, st);
		SpawnRocket(ent, st, dir, c ? 800 : 550, c ? "weapons/world/rockettrail_p" : "weapons/world/rockettrail2", 1.0f,
		            0);
		Recoil(ent, dir, 12);
		break;
	}
	case 15: {
		vec3_t d;
		for (int i = 0; i < 3; i++)
			d[i] = dir[i] + gix.flrand(-0.1f, 0.1f);
		VectorNormalize(d);
		edict_t *r = SpawnRocket(ent, start, d, 700, "weapons/world/rockettrail_alt1", 1.0f, 0);
		r->timestamp = level.time;
		r->think = splitrocket_think;
		Recoil(ent, dir, 6);
		break;
	}
	case 16:
		MPGBeam(ent, start, dir);
		break;
	case 17:
		if (c) {
			MPGBurst(ent, start, dir);
		} else {
			edict_t *m = G_Spawn();
			m->classname = (char *)"mpg_burst";
			m->owner = ent;
			VectorCopy(start, m->pos1);
			VectorCopy(dir, m->movedir);
			m->think = mpgburst_marker;
			m->nextthink = level.time + 0.4f;
		}
		break;
	case 18:
		FlameStream(ent, start, dir);
		break;
	case 19: {
		vec3_t vel;
		VectorScale(dir, 600, vel);
		VectorMA(vel, 100, up, vel);
		vel[2] += 150;
		edict_t *f = NewProjectile(ent, start, vel, "fireball", MOVETYPE_DANTOUCH, 1.625f, 0.3f, 1, 0, mins2, maxs2, 3);
		f->health = 5;
		FX_SetSlot(f, 0, "weapons/world/fballtrail");
		f->touch = fireball_touch;
		f->think = projectile_expire;
		f->nextthink = level.time + 10;
		gi.linkentity(f);
		break;
	}
	case 20:
		MeleeStrike(ent, start, dir);
		break;
	case 22:
	case 24: {
		// characters' hand grenade: the grenade object with elasticity 0.15 from the attack start (SPEC-782 9.1)
		vec3_t vel;
		VectorScale(dir, throwspeed > 0 ? throwspeed : 500, vel);
		edict_t *g = NewProjectile(ent, start, vel, "grenade", MOVETYPE_DANTOUCH, 1, 0.15f, 0.7f, 0, mins2, maxs2,
		                           PROJ_MASK);
		g->health = 21;
		g->touch = grenade_touch;
		g->think = grenade_think;
		g->nextthink = level.time + FRAMETIME;
		ThrowSetup(g);
		Bot_Explosive(g, BOT_EXPLOSIVE_GRENADE, 1); /* G5 */
		break;
	}
	case 23: {
		vec3_t vel;
		VectorScale(dir, throwspeed > 0 ? throwspeed : 500, vel);
		edict_t *m = NewProjectile(ent, start, vel, "molotov", MOVETYPE_DANTOUCH, 1, 0, 1, 0, mins2, maxs2, PROJ_MASK);
		GhoulSetModel(m, "Items/Projectiles", "w_molitov", NULL, 1.0f);
		m->avelocity[ROLL] = 300;
		FX_SetSlot(m, 0, "weapons/world/molitrail2");
		m->touch = molotov_touch;
		m->think = projectile_expire;
		m->nextthink = level.time + 4;
		gi.linkentity(m);
		break;
	}
	case 26: {
		vec3_t vel;
		VectorScale(dir, throwspeed > 0 ? throwspeed : 400, vel);
		edict_t *f = NewProjectile(ent, start, vel, "flashpack", MOVETYPE_DANTOUCH, 1, 0.7f, 0.7f, 0, mins2, maxs2,
		                           PROJ_MASK);
		f->health = 100;
		f->touch = flashpack_touch;
		f->think = flashpack_explode;
		f->nextthink = level.time + 1.5f;
		ThrowSetup(f);
		Bot_Explosive(f, BOT_EXPLOSIVE_FLASHPACK, 1); /* G5 */
		break;
	}
	case 29:
		DekkerGun(ent, start, dir);
		break;
	case 30: {
		vec3_t vel;
		VectorScale(dir, 500, vel);
		edict_t *s = NewProjectile(ent, start, vel, "thrown_star", MOVETYPE_DANTOUCH, 0.7f, 0.4f, 1, 0, mins1, maxs1,
		                           PROJ_MASK);
		s->avelocity[YAW] = 2160;
		s->health = 10000;
		GhoulSetModel(s, "Enemy/Bolt", "w_throw_star", NULL, 1.0f);
		s->s.sound = gi.soundindex("Weapons/Knife/ThrowLoop.wav");
		s->s.sounddata = 0xF8;
		FX_SetSlot(s, 0, "weapons/world/startrail");
		s->touch = star_touch;
		s->think = projectile_expire;
		s->nextthink = level.time + 3;
		gi.linkentity(s);
		break;
	}
	default: // 25 and unknown: nothing
		break;
	}

	// third-person "projectile fired" effect for other players (SPEC-782 2)
	if (DM() && (a == 9 || a == 10 || a == 13 || a == 14 || a == 15 || a == 19 || a == 17)) {
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(63);
		gi.WriteByte(fxi(attacks[a].D));
		gi.WriteByte(G_EdictNum(ent) & 0xFF);
		gi.WritePosition(start);
		WriteUnit2048(dir);
		gi.multicast(start, MULTICAST_PVS); // the shooter gets it too (oracle self-rocket captures; the client
		                                   // ignores its own, SPEC-401 63)
	}
}

// SPEC-782 for characters: G3's body fires its hand weapon through g3hooks.fireAttack (start = the weapon's `flash`
// bolt, direction already spread); G7's turrets and environ_invisible_attack through fx::g_fire_attack (throw speed,
// effects-only flag)
static void G_FireAttackG3(edict_t *shooter, int attack, const vec3_t start, const vec3_t dir, edict_t *target)
{
	if (!shooter || attack <= 0 || attack > 30)
		return;
	g_attack_target = target;
	RunAttack(shooter, attack, start, dir, 0);
	g_attack_target = NULL;
}

static void G_FireAttackFx(edict_t *owner, int attack, const vec3_t start, const vec3_t dir, float extra, bool nodamage)
{
	if (!owner || attack <= 0 || attack > 30)
		return;
	g_attack_nodamage = nodamage;
	RunAttack(owner, attack, start, dir, extra);
	g_attack_nodamage = false;
}

void G_InstallAttackHooks(void)
{
	g3hooks.fireAttack = G_FireAttackG3;
	fx::g_fire_attack = G_FireAttackFx;
}

void WeaponFire(edict_t *ent, PlayerInventory *inv, int alt)
{
	if (!ent || !ent->client || !inv)
		return;
	int weapon = InvCurWeapon(inv);
	WeaponDesc *wd = PlayerWeaponDesc(weapon);
	if (!wd)
		return;
	uint8_t *wdb = (uint8_t *)wd;
	int a = *(int *)(wdb + (alt ? WDESC_ATTACK_ALT : WDESC_ATTACK_PRI));
	if (a <= 0 || a > 30)
		return;
	gclient_t *c = ent->client;

	vec3_t eye, ang, fwd, dir;
	VectorCopy(ent->s.origin, eye);
	eye[2] += ent->viewheight;
	VectorCopy(c->v_angle, ang);
	if (weapon == WP_SNIPER) {
		ang[PITCH] += c->sniper_offset[0];
		ang[YAW] += c->sniper_offset[1];
		if (ang[PITCH] < -90)
			ang[PITCH] = -90;
	}

	float per = *(float *)(wdb + (alt ? WDESC_SPREAD_ALT : WDESC_SPREAD_PRI));
	float mx = *(float *)(wdb + (alt ? WDESC_MAXSPREAD_ALT : WDESC_MAXSPREAD_PRI));
	float s = InvSpread(inv) * per;
	if (s > mx)
		s = mx;
	if (RealisticDamage() || weapon == WP_SNIPER) {
		if (!DM()) {
			if (weapon == WP_SNIPER && c->ps.fov > 60)
				s += 4.5f;
		} else if (weapon == WP_SNIPER) {
			if (c->ps.fov > 60)
				s += 4.5f;
		} else {
			float v2 = DotProduct(ent->velocity, ent->velocity) * 3.75e-5f;
			if (deathmatch->value == 5)
				v2 *= 0.5625f;
			s += v2;
			if (!ent->groundentity)
				s += 3;
		}
	}
	if (s > 0)
		for (int i = 0; i < 3; i++)
			ang[i] += gix.flrand(-s, s);
	AngleVectors(ang, fwd, NULL, NULL);
	VectorCopy(fwd, dir);

	vec3_t start;
	VectorCopy(eye, start);
	if (wd->FiresFromMuzzle()) {
		ghoul::IGhoulInst *vi = InvViewInst(inv);
		vec3_t muzzle;
		if (vi && GhoulInstBoltPos(vi, eye, c->v_angle, "flash", muzzle)) {
			vec3_t d;
			VectorSubtract(muzzle, eye, d);
			float along = DotProduct(d, dir);
			VectorMA(muzzle, -along, dir, start);
		}
		if (wd->Converges()) {
			vec3_t end;
			VectorMA(eye, 2048, dir, end);
			trace_t tr = gi.trace(eye, NULL, NULL, end, ent, SHOT_MASK);
			VectorSubtract(tr.endpos, start, dir);
			VectorNormalize(dir);
		}
	}

	RunAttack(ent, a, start, dir, 0);
}

//======================================================================
// equipment (SPEC-782 9)

static bool SoftMaterial(csurface_t *surf)
{
	int mat = surf ? ((unsigned)surf->flags >> 24) : 0;
	static const int soft[] = {2, 3, 4, 5, 9, 10, 11, 12, 13, 25, 26, 27, 33, 34, 36, 40};
	for (int s : soft)
		if (s == mat)
			return true;
	return false;
}

static void grenade_explode(edict_t *self)
{
	if (CinematicRemove(self))
		return;
	vec3_t c;
	VectorCopy(self->s.origin, c);
	c[2] += 10;
	VectorCopy(c, self->s.origin);
	ProjectileExplodeRadius(self, 200, 40, 200, MOD_GRENADE, 0x108000, NULL, "weapons/world/grenadeexplode", 2000, 0.8f,
	                        0.5f, 0.4f);
	Bot_Explosive(self, BOT_EXPLOSIVE_GRENADE, 0); /* G5 */
	G_FreeEdict(self);
}

static void grenade_think(edict_t *self)
{
	if (self->health > 1) {
		self->health--;
		self->nextthink = level.time + FRAMETIME;
		return;
	}
	grenade_explode(self);
}

static void grenade_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (DM() && other && other->client) {
		grenade_explode(self);
		return;
	}
	if (VectorLength(self->velocity) > 10 && !SoftMaterial(surf))
		G_Sound(self, CHAN_AUTO, "Weapons/gren/bounce.wav", 0.6f, ATTN_IDLE);
}

static void ThrowSetup(edict_t *e)
{
	for (int i = 0; i < 3; i++)
		e->avelocity[i] = gix.flrand(-300, 300);
	e->flags |= FL_THROWNEQUIP;
	GhoulSetModel(e, "Items/Projectiles", e->classname[0] == 'g' ? "grenade" : e->classname[0] == 'C' ? "C4" : "flashpak",
	              NULL, 1.0f);
	gi.linkentity(e);
}

int WeaponUseGrenade(edict_t *ent, ItemSlot *slot)
{
	PModel_ThrowUsed(ent); // third-person throw (SPEC-780 16)
	vec3_t fwd, right, up, vel, start;
	AngleVectors(ent->client->v_angle, fwd, right, up);
	VectorScale(fwd, 475, vel);
	VectorMA(vel, gix.flrand(-15, 15), right, vel);
	VectorMA(vel, gix.flrand(300, 350), up, vel);
	VectorAdd(vel, ent->velocity, vel);
	VectorCopy(ent->s.origin, start);
	start[2] += ent->viewheight - 20;
	vec3_t mins = {-2, -2, -3}, maxs = {2, 2, 3};
	edict_t *g = NewProjectile(ent, start, vel, "grenade", MOVETYPE_DANTOUCH, 1, 0.5f, 0.7f, 0, mins, maxs, PROJ_MASK);
	g->health = 21;
	g->touch = grenade_touch;
	g->think = grenade_think;
	g->nextthink = level.time + FRAMETIME;
	ThrowSetup(g);
	Bot_Explosive(g, BOT_EXPLOSIVE_GRENADE, 1); /* G5: announced to bots (SPEC-741 10) */
	G_Sound(ent, CHAN_VOICE, "Weapons/gren/throw.wav", 0.6f, ATTN_NORM);
	return 1;
}

static void c4_explode(edict_t *self)
{
	if (CinematicRemove(self))
		return;
	vec3_t fwd, c;
	AngleVectors(self->s.angles, fwd, NULL, NULL);
	VectorMA(self->s.origin, 10, fwd, c);
	VectorCopy(c, self->s.origin);
	T_RadiusDamageMinMax(self, self->owner, c, 250, 32, 200, NULL, MOD_C4, 0x108000);
	WhiteOut(c, 2500, 0.8f, 0.5f);
	FX_EffectDir(fxn("weapons/world/c4explode"), c, fwd, MULTICAST_PHS);
	G_AlertNoise(0.6f);
	G_ReportNoise(self->owner, c, 1000, 2);
	Bot_Explosive(self, BOT_EXPLOSIVE_C4, 0); /* G5 */
	G_FreeEdict(self);
}

static void c4_think(edict_t *self)
{
	if (++self->count >= 5) {
		c4_explode(self);
		return;
	}
	G_Sound(self, CHAN_AUTO, "Weapons/C4/Beep.wav", 0.6f, ATTN_NORM);
	self->nextthink = level.time + 0.4f;
}

static void c4_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner || self->armed || !plane)
		return;
	if (other != world && other->solid != SOLID_BSP)
		return;
	self->armed = 1;
	G_Sound(self, CHAN_AUTO, "Weapons/C4/c4attch.wav", 0.6f, ATTN_NORM);
	self->gravity = 0;
	VectorClear(self->velocity);
	VectorClear(self->avelocity);
	vectoangles(plane->normal, self->s.angles);
	self->movetype = MOVETYPE_NONE;
}

int WeaponUseC4(edict_t *ent, ItemSlot *slot)
{
	PModel_ThrowUsed(ent); // third-person throw (SPEC-780 16)
	vec3_t fwd, right, up, vel, start;
	AngleVectors(ent->client->v_angle, fwd, right, up);
	VectorScale(fwd, 410, vel);
	VectorMA(vel, gix.flrand(-30, 30), right, vel);
	VectorMA(vel, gix.flrand(150, 180), up, vel);
	VectorCopy(ent->s.origin, start);
	start[2] += ent->viewheight;
	vec3_t mins = {-2, -2, -2}, maxs = {2, 2, 2};
	edict_t *c = NewProjectile(ent, start, vel, "C4", MOVETYPE_DANTOUCH, 1, 0, 1, 0, mins, maxs, PROJ_MASK);
	c->health = 100;
	c->touch = c4_touch;
	c->think = c4_think;
	c->nextthink = level.time + 0.5f;
	ThrowSetup(c);
	Bot_Explosive(c, BOT_EXPLOSIVE_C4, 1); /* G5: announced to bots (SPEC-741 10) */
	G_Sound(ent, CHAN_VOICE, "Weapons/gren/throw.wav", 0.6f, ATTN_NORM);
	return 1;
}

static void claymore_fire(edict_t *self)
{
	if (CinematicRemove(self))
		return;
	T_RadiusDamageMinMax(self, self->owner, self->s.origin, 300, 150, 100, NULL, MOD_CLAYMORE, 0x108000);
	FX_Effect(fxn("weapons/world/claymore"), self->s.origin, MULTICAST_PHS);
	G_AlertNoise(0.4f);
	G_FreeEdict(self);
}

static void claymore_think(edict_t *self)
{
	if (!self->armed) {
		self->armed = 1;
		G_Sound(self, CHAN_AUTO, "Weapons/Claymore/Beep.wav", 0.6f, ATTN_NORM);
	}
	self->count++;
	edict_t *list[1024];
	int n = G_RadiusList(self->s.origin, 128, list, 1);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (e == self || e == world)
			continue;
		if (((e->client || (e->svflags & SVF_MONSTER)) && e->health > 0) || (self->count > 200 && e->solid)) {
			claymore_fire(self);
			return;
		}
	}
	self->nextthink = level.time + 0.2f;
}

static void claymore_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (self->bounces)
		return;
	self->bounces = 1;
	G_Sound(self, CHAN_AUTO, "Weapons/C4/c4attch.wav", 0.6f, ATTN_NORM);
}

static void claymore_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->takedamage = DAMAGE_NO;
	self->think = claymore_fire;
	self->nextthink = level.time + 0.2f;
}

int WeaponUseClaymore(edict_t *ent, ItemSlot *slot)
{
	PModel_ThrowUsed(ent); // third-person throw (SPEC-780 16)
	vec3_t start, vel = {0, 0, 0};
	VectorCopy(ent->s.origin, start);
	start[2] += 24;
	vec3_t mins = {-4, -4, 0}, maxs = {4, 4, 6};
	edict_t *c = NewProjectile(ent, start, vel, "claymore", MOVETYPE_DANTOUCH, 1, 0, 1, 0, mins, maxs, 0x2000081);
	VectorSet(c->s.angles, 270, 90, 90);
	GhoulSetModel(c, "Items/Projectiles", "claymore", NULL, 1.0f);
	c->takedamage = DAMAGE_YES;
	c->health = 1;
	c->die = claymore_die;
	c->touch = claymore_touch;
	c->think = claymore_think;
	c->nextthink = level.time + 3;
	c->flags |= FL_THROWNEQUIP;
	gi.linkentity(c);
	return 1;
}

static void flashpack_explode(edict_t *self)
{
	if (CinematicRemove(self))
		return;
	vec3_t c;
	VectorCopy(self->s.origin, c);
	c[2] += 15;
	WhiteOut(c, 10000, 0.99f, 0.15f);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(20);
	gi.WritePosition(c);
	gi.multicast(c, MULTICAST_PHS);
	T_RadiusDamage(self, self->owner, 20, self, 100, MOD_FIRE, 0);
	Bot_Explosive(self, BOT_EXPLOSIVE_FLASHPACK, 0); /* G5 */
	G_FreeEdict(self);
}

static void flashpack_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == self->owner)
		return;
	if (VectorLength(self->velocity) > 10)
		G_Sound(self, CHAN_AUTO, "Weapons/gren/bounce.wav", 0.6f, ATTN_IDLE);
}

int WeaponUseFlashpack(edict_t *ent, ItemSlot *slot)
{
	PModel_ThrowUsed(ent); // third-person throw (SPEC-780 16)
	vec3_t fwd, up, vel, start;
	AngleVectors(ent->client->v_angle, fwd, NULL, up);
	VectorScale(fwd, 400, vel);
	VectorMA(vel, gix.flrand(200, 400), up, vel);
	VectorCopy(ent->s.origin, start);
	start[2] += ent->viewheight;
	vec3_t mins = {-2, -2, -3}, maxs = {2, 2, 3};
	edict_t *f = NewProjectile(ent, start, vel, "flashpack", MOVETYPE_DANTOUCH, 1, 0.7f, 0.7f, 0, mins, maxs, PROJ_MASK);
	f->health = 100;
	f->touch = flashpack_touch;
	f->think = flashpack_explode;
	f->nextthink = level.time + 1.5f;
	ThrowSetup(f);
	Bot_Explosive(f, BOT_EXPLOSIVE_FLASHPACK, 1); /* G5: announced to bots (SPEC-741 10) */
	G_Sound(ent, CHAN_VOICE, "Weapons/gren/throw.wav", 0.6f, ATTN_NORM);
	return 1;
}

int WeaponUseMedkit(edict_t *ent, ItemSlot *slot)
{
	if (!RealisticDamage()) {
		if (ent->health >= ent->max_health) {
			int id = SP_Id("items", "FULL_HEALTH");
			if (id >= 0)
				gix.SP_Print(ent, id);
			return 0;
		}
		ent->health += 25;
		if (ent->health > ent->max_health)
			ent->health = ent->max_health;
	}
	ent->client->legslow = 0;
	G_Sound(ent, CHAN_ITEM, "dm/healthpu.wav", 1, ATTN_IDLE);
	return 1;
}

int WeaponGoggles(edict_t *ent, ItemSlot *slot)
{
	if (slot && slot->state == 1) {
		ent->client->goggles = 1;
		G_Sound(ent, CHAN_VOICE, "Weapons/Goggles/on.wav", 0.6f, ATTN_IDLE);
	} else {
		ent->client->goggles = 0;
		if (!game.cinematic)
			G_Sound(ent, CHAN_VOICE, "Weapons/Goggles/off.wav", 0.6f, ATTN_IDLE);
	}
	return 1;
}
