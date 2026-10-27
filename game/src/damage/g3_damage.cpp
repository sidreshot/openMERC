// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - the damage function and everything around it: who can be damaged, team and
// difficulty rules, player protection and armour, knockback, player feedback, death and pain dispatch, the
// character rules, area damage and CanDamage. Hit location, zone multipliers and gore are in the bodies.
// Implements: SPEC-720 Behaviour 1-9, 13-16 (dmflags bits per SPEC-790 "Corrections to other specs")
#include "g3_local.h"
#include "../bots/bot_api.h"
#include "../modes/g_modes.h"
#include "../scriptglue/g_scriptglue.h"

int dmg_lastMod;
bool dmg_knifeDeflected;
G3Hooks g3hooks;

// ---------------------------------------------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------------------------------------------
int Dmg_Toughness(void)
{
	int t = (int)G3_Cvar("sk_toughness");
	if (t < 0)
		t = 0;
	if (t > 4)
		t = 4;
	return t;
}

float Dmg_ToughnessFactor(void)
{
	static const float f[5] = {0.1f, 0.2f, 0.3f, 0.5f, 1.0f};
	return f[Dmg_Toughness()];
}

// SPEC-790 §3: per mode a dmflags bit is read from the cvar, forced on or forced off.
int Dmg_DMFlags(void)
{
	if (g3hooks.dmflags)
		return g3hooks.dmflags();
	if (GM_Mode() >= 0)
		return GM_DMFlags(); // G9 (SPEC-790 §3)
	int mode = G3_DeathmatchMode();
	int cv = (int)G3_Cvar("dmflags");
	struct ModeMask { int off, on; };
	static const ModeMask m[8] = {
		{0xffff, 0},                                                   // 0 single player: all off
		{0x4000 | 0x8000, 0},                                          // 1 DM
		{0x80 | 0x100 | 0x4000 | 0x8000, 0},                           // 2 Assassin
		{0x4 | 0x80 | 0x100 | 0x1000 | 0x2000 | 0x4000 | 0x8000, 0x40 | 0x800}, // 3 Arsenal
		{0x8000, 0x80},                                                // 4 CTF
		{0x1 | 0x2 | 0x4 | 0x8 | 0x40 | 0x100 | 0x800 | 0x4000 | 0x8000, 0x10}, // 5 Realistic
		{0x8000, 0x80},                                                // 6 Control
		{0, 0},                                                        // 7 Conquer
	};
	if (mode < 0 || mode > 7)
		mode = 1;
	return (cv & ~m[mode].off) | m[mode].on;
}

bool Dmg_TeamPlay(void) { return G3_Deathmatch() && (Dmg_DMFlags() & 0x80) != 0; }
bool Dmg_RealisticDamage(void) { return G3_Deathmatch() && (Dmg_DMFlags() & 0x10) != 0; }

void Dmg_SetDamageOwner(edict_t *part, edict_t *owner)
{
	if (!part)
		return;
	part->g3damageowner = owner;
	if (owner)
		part->g3flags |= G3F_SUBPART;
	else
		part->g3flags &= ~G3F_SUBPART;
}

static bool SameTeam(edict_t *a, edict_t *b)
{
	if (g3hooks.sameTeam)
		return g3hooks.sameTeam(a, b);
	return a && b && OnSameTeam(a, b);
}

static bool IsCharacter(const edict_t *e)
{
	return e && e->g3body && e->g3body->owner;
}

static bool IsPlayer(const edict_t *e)
{
	return e && e->client;
}

// ---------------------------------------------------------------------------------------------------------------
// knockback (Behaviour 8)
// ---------------------------------------------------------------------------------------------------------------
static bool Pushable(const edict_t *t)
{
	switch (t->movetype) {
	case MOVETYPE_NONE:
	case MOVETYPE_PUSH:
	case MOVETYPE_STOP:
	case MOVETYPE_BOUNCE:
		return false;
	default:
		return true;
	}
}

static void PushGeneral(edict_t *t, const G3Hit &h)
{
	if (!h.knockback || (h.dflags & DMGF_NO_KNOCKBACK) || (t->flags & FL_NO_KNOCKBACK) || !Pushable(t))
		return;
	float m = t->mass < 50 ? 50.0f : (float)t->mass;
	vec3_t v = {h.dir[0], h.dir[1], h.dir[2] * ((h.dflags & DMGF_PUSH_ONLY) ? 0.1f : 0.25f)};
	VectorNormalize(v);
	VectorMA(t->velocity, 250.0f * h.knockback / m, v, t->velocity);
}

static void PushPlayer(edict_t *t, const G3Hit &h)
{
	if (!h.knockback || (h.dflags & DMGF_NO_KNOCKBACK) || (t->flags & FL_NO_KNOCKBACK) || !Pushable(t))
		return;
	float mass = (float)t->mass;
	if (mass <= 0)
		mass = 200.0f; // guard; players always have a mass (SPEC-780)
	gclient_t *cl = t->client;
	if (h.attacker && h.attacker != world && !(h.dflags & DMGF_PUSH_ONLY)) {
		vec3_t v;
		VectorSubtract(h.point, h.attacker->s.origin, v);
		VectorNormalize(v);
		float k = 250.0f * h.knockback / mass;
		vec3_t add = {v[0] * k, v[1] * k, v[2] * k * 0.25f};
		VectorAdd(t->velocity, add, t->velocity);
		if (cl)
			VectorAdd(cl->g3_push, add, cl->g3_push);
	} else {
		vec3_t v = {h.dir[0], h.dir[1], h.dir[2] * 0.1f};
		VectorNormalize(v);
		float k = 1000.0f * h.knockback / mass;
		VectorMA(t->velocity, k, v, t->velocity);
		if (t->velocity[2] > 200)
			t->velocity[2] = 200;
		if (cl)
			cl->stagger_time = level.time + 0.5f + 0.75f * h.knockback / mass;
	}
}

static void PushCharacter(edict_t *t, const G3Hit &h)
{
	if (!h.knockback || (h.dflags & DMGF_NO_KNOCKBACK) || (t->flags & FL_NO_KNOCKBACK) || !Pushable(t) ||
	    t->movetype == MOVETYPE_NOCLIP)
		return;
	float m = t->mass < 50 ? 50.0f : (float)t->mass;
	vec3_t v = {h.dir[0], h.dir[1], (h.dflags & DMGF_PUSH_ONLY) ? h.dir[2] * 0.2f : h.dir[2] + 0.3f};
	VectorNormalize(v);
	VectorMA(t->velocity, 500.0f * h.knockback / m, v, t->velocity);
}

// ---------------------------------------------------------------------------------------------------------------
// players (Behaviour 3, 4, 13)
// ---------------------------------------------------------------------------------------------------------------
static int HitDirection(edict_t *t, const G3Hit &h)
{
	if (h.dflags & (DMGF_FIRE | DMGF_FALL | DMGF_DROWN))
		return (h.dflags & DMGF_FALL) ? 2 : 0x10;
	if (!h.attacker || h.attacker == world)
		return 0;
	vec3_t d;
	VectorSubtract(h.attacker->s.origin, t->s.origin, d);
	// both angles reduced to 0..360 in 1/65536 turns
	int ya = ((int)(vectoyaw(d) * 65536.0f / 360.0f)) & 65535;
	float pyaw = t->client ? t->client->v_angle[YAW] : t->s.angles[YAW];
	int yp = ((int)(pyaw * 65536.0f / 360.0f)) & 65535;
	float a = (float)((ya - yp) & 65535) * (360.0f / 65536.0f);
	if (a < 30 || a > 340 || (a > 150 && a < 210))
		return 0x10;
	if (a >= 30 && a <= 150)
		return 8;
	return 4;
}

static void PlayerHitSound(edict_t *t, int before, int left, int mod)
{
	if (mod == MOD_FIRE)
		G3_Sound(t, CHAN_BODY, "impact/gore/sizzle.wav", 1, ATTN_NORM);
	else if (left > 0) {
		char s[64];
		Com_sprintf(s, sizeof s, "impact/gore/impact%d.wav", G3_irand(1, 3));
		G3_Sound(t, CHAN_BODY, s, 1, ATTN_NORM);
		Com_sprintf(s, sizeof s, "impact/player/hit%d.wav", G3_irand(1, 4));
		G3_Sound(t, CHAN_VOICE, s, 1, ATTN_NORM);
	} else if (before > 0)
		G3_Sound(t, CHAN_BODY, "impact/player/armor.wav", 1, ATTN_NORM);
}

// Player armour, SPEC-003 method 31 (int damage, float piercing, float fraction) -> damage that remains
// (SPEC-AMEND-005 G3.14: values 11 and 12 are passed unchanged as floats; the share D - trunc(D - D*f) is lost
// against armour > 0).
int Dmg_AbsorbArmour(PlayerInventory *inv, int damage, float piercing, float fraction)
{
	if (!inv)
		return damage;
	return inv->AbsorbArmor(damage, piercing, fraction); // player_api.h now declares the float (G18, 10-25)
}

// Behaviour 13: returns the damage that remains
static int PlayerDamagePath(edict_t *t, G3Hit &h)
{
	gclient_t *cl = t->client;
	G3Body *body = t->g3body;
	int damage = h.damage;
	if (!G3_Deathmatch() || !body) {
		// single player (no body object)
		int left = damage;
		if (!(h.dflags & DMGF_NO_ARMOR) && cl && cl->inv && damage > 0) {
			left = Dmg_AbsorbArmour(cl->inv, damage, h.armorPiercing, h.armorBypass);
			if (left < 0)
				left = 0;
		}
		// hit sounds only for players without a body model (single player), living before the hit, the killing
		// hit included (SPEC-AMEND-005 G3.2)
		if (t->health > 0)
			PlayerHitSound(t, damage, left, h.mod);
		return left;
	}
	// deathmatch with a player body
	if ((h.dflags & (DMGF_TRACED | DMGF_PROJECTILE)) && (dmg_shot.ent != t || dmg_shot.count == 0)) {
		float len = 8192;
		vec3_t d;
		VectorCopy(h.dir, d);
		if (VectorNormalize(d) > 0)
			Dmg_ShotFill(t, h.origin, d, len);
	}
	int armourBefore = cl && cl->inv ? cl->inv->Armor() : 0;
	int left = body->Damage(h);
	if (body->family == BODY_FEMALE)
		left = (int)(left * 1.5f);
	// no hit sounds for a player with a body model (deathmatch), G3.2. When the returned damage is below the health,
	// the body's ordinary hit-reaction choice runs with damage 1.0 (no knock-down for players) and the record is
	// forced (SPEC-AMEND-006 G3.19); the player models lack the disarm and injured-limb records, so the choice falls
	// to the `*unhurt*` records. The record stays until its EOS (the 0.2 s seen = its length); then the player's next
	// movement choice replaces it (G18.26 keep rule; G3Body::Think puts back the record that played before as a
	// stand-in while the player code does not replace it itself).
	if (left < t->health) {
		const G3Anim *prev = body->cur;
		const G3Anim *pa = body->ChoosePain(h.attacker, h.point, h.knockback, 1);
		if (pa && body->SetAnim(pa, true)) {
			body->painPrev = prev;
			body->painUntil = 1; // marker: a player hit reaction is playing
		}
	}
	int armourAfter = cl && cl->inv ? cl->inv->Armor() : 0;
	if ((armourBefore > 0) != (armourAfter > 0) && g3hooks.playerSkinRefresh)
		g3hooks.playerSkinRefresh(t);
	if (t->health - left <= 0) {
		// lethal in deathmatch: armour removed, bolt-ons of both hands dropped (SPEC-780 §7)
		if (cl && cl->inv && armourAfter > 0)
			cl->inv->AddArmor(-armourAfter);
		IGhoulInst *inst = t->ghoulInst;
		if (inst) {
			G3G_RemoveChildrenOnBolt(inst, G3G_Part(inst, "wbolt_hand_r"));
			G3G_RemoveChildrenOnBolt(inst, G3G_Part(inst, "wbolt_hand_l"));
		}
	}
	return left;
}

static void PlayerFeedback(edict_t *t, const G3Hit &h, int damage, bool god)
{
	gclient_t *cl = t->client;
	if (!cl)
		return;
	if (damage > 0) {
		cl->g3_blood += damage;
		cl->g3_knockback += h.knockback;
		VectorCopy(h.point, cl->g3_damagefrom);
		// view feedback kept as G18's first slice did it (SPEC-780 §12/§14 give no amounts; owner G18)
		cl->damage_alpha += damage * 0.02f;
		if (cl->damage_alpha > 0.6f)
			cl->damage_alpha = 0.6f;
		VectorSet(cl->damage_blend, 1, 0, 0);
		float kick = damage * 0.5f;
		if (kick > 50)
			kick = 50;
		cl->v_dmg_pitch = -kick;
		cl->v_dmg_roll = 0;
		cl->v_dmg_time = level.time + DAMAGE_TIME;
		// blood splat (TE 3) attempted with probability 1/6, never for means of death 27 (SPEC-AMEND-005 G3.6)
		if ((h.mod & ~DMG_MOD_FRIENDLY) != MOD_FIRE && G3_irand(0, 5) == 0)
			G3FX_BloodSplat(h.point, h.dir, t);
		PushPlayer(t, h);
	} else if (!god) {
		cl->g3_knockback += h.knockback;
		VectorCopy(h.point, cl->g3_damagefrom);
		PushPlayer(t, h);
	}
}

static void PlayerAfterHealth(edict_t *t, const G3Hit &h, int damage)
{
	gclient_t *cl = t->client;
	if (!cl)
		return;
	if (damage > 0) {
		int m = h.mod & ~DMG_MOD_FRIENDLY;
		if (m & 3)
			G3FX_OnEnt("lighthit", t);
		else if (m & 15)
			G3FX_OnEnt("gotshot", t);
		else if ((m & 63) && damage > 5) {
			G3FX_OnEnt("exphit", t);
			G3FX_OnEnt("land", t);
		}
	}
	if (t->health > 0 || damage == 0) {
		if (h.point[0] == 0 && h.point[1] == 0 && h.point[2] == 0) {
			G3_Printf("Error: T_Damage - NULL location passed\n");
			return;
		}
		cl->damage_dir = HitDirection(t, h);
		if (G3_Deathmatch() && g3hooks.dmPlayerHit)
			g3hooks.dmPlayerHit(t, h.attacker);
		if ((damage > 10 || t->health < 25) && g3hooks.musicDanger)
			g3hooks.musicDanger(t);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// the damage function
// ---------------------------------------------------------------------------------------------------------------
void Dmg_Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t dir, const vec3_t point,
                const vec3_t origin, int damage, int knockback, int dflags, int mod, float armorPiercing,
                float armorBypass)
{
	G3Hit h;
	h.targ = targ;
	h.inflictor = inflictor;
	h.attacker = attacker;
	VectorCopy(dir ? dir : vec3_origin, h.dir);
	VectorCopy(point ? point : vec3_origin, h.point);
	VectorCopy(origin ? origin : vec3_origin, h.origin);
	h.damage = damage;
	h.knockback = knockback;
	h.dflags = dflags;
	h.mod = mod;
	h.armorPiercing = armorPiercing;
	h.armorBypass = armorBypass;
	Dmg_DamageHit(h);
}

static void DamageHit(G3Hit &h);

void Dmg_DamageHit(G3Hit &h)
{
	DamageHit(h);
	Dmg_ShotClear(); // the hit list belongs to this one hit
}

static void DamageHit(G3Hit &h)
{
	edict_t *targ = h.targ;
	dmg_knifeDeflected = false;
	if (!targ || !targ->inuse)
		return;
	// 1.2 sub-parts pass the damage to their owner; helper volumes (server flag 0x80) are sub-parts of their owner
	// (SPEC-780 §15: shots resolve to the player; SPEC-700 §16 characters' helpers)
	if ((targ->svflags & SVF_PLAYERPROXY) && targ->owner && !(targ->g3flags & G3F_SUBPART)) {
		targ = targ->owner;
		if (!targ->inuse)
			return;
		h.targ = targ;
	} else if (targ->g3flags & G3F_SUBPART) {
		targ = targ->g3damageowner;
		if (!targ || !targ->inuse)
			return;
		h.targ = targ;
	}
	// 1.1
	if (!targ->takedamage)
		return;
	if (targ->takedamage == 3) {
		edict_t *p = G_FirstPlayer();
		if (p && p->client && p->client->camera && p->client->remote_type == 4)
			return;
	}
	if (!h.inflictor)
		h.inflictor = world;
	edict_t *attacker = h.attacker;
	if (G3_Cvar("g3_debug") != 0) // OpenMerc diagnostic (no original counterpart)
		G3_Printf("G3 hit %d(%s) by %d inflictor %d: dmg %d kb %d flags %#x mod %d pt %s dir %s org %s hp %d\n",
		          G_EdictNum(targ), targ->classname ? targ->classname : "", attacker ? G_EdictNum(attacker) : -1,
		          G_EdictNum(h.inflictor), h.damage, h.knockback, h.dflags, h.mod, G3_vs(h.point), G3_vs(h.dir),
		          G3_vs(h.origin), targ->health);

	// 1.3 nodamage
	if (IsCharacter(attacker)) {
		int nd = (int)G3_Cvar("nodamage");
		if (IsPlayer(targ)) {
			if (nd == 1)
				h.damage = (int)(1.0f / Dmg_ToughnessFactor() + 1.0f);
			else
				h.damage = (int)(h.damage * 0.5f);
			if (nd == 2)
				h.damage = 0;
		} else if (nd == 2 && IsCharacter(targ))
			h.damage = 0;
	}

	// 1.4 characters
	if (IsCharacter(targ)) {
		if ((G3_ClassIs(targ, "m_x_mskinboss") || G3_ClassIs(targ, "m_x_mraiderboss1")) && targ->count != 100)
			return;
		dmg_lastMod = meansOfDeath = h.mod;
		VectorNormalize(h.dir);
		targ->g3body->owner->Damage(h);
		return;
	}

	// 2. team damage
	if (targ != attacker && attacker && G3_Deathmatch() && Dmg_TeamPlay() && SameTeam(targ, attacker)) {
		if (Dmg_DMFlags() & 0x100) {
			h.damage = 0;
			h.dflags |= DMGF_TEAM_BLOCKED;
		} else
			h.mod |= DMG_MOD_FRIENDLY;
	}
	dmg_lastMod = meansOfDeath = h.mod;
	Bot_Damaged(targ, attacker);      // G5: an attacker becomes a bot's enemy (SPEC-741 6)
	GM_ClientDamaged(targ, attacker); // G9: hurt-carrier record (SPEC-790 B8)

	gclient_t *cl = targ->client;
	// 3.1 toughness (single player)
	if (cl && !G3_Deathmatch()) {
		int d = (int)(Dmg_ToughnessFactor() * h.damage);
		if (h.damage > 0 && d < 1)
			d = 1;
		h.damage = d;
	}
	// 3.2
	VectorNormalize(h.dir);
	if (!cl)
		PushGeneral(targ, h);
	// 3.3 protection
	bool god = false;
	if (!(h.dflags & DMGF_NO_PROTECTION)) {
		if (targ->flags & FL_GODMODE) {
			h.damage = 0;
			god = true;
		}
		if (cl) {
			if (cl->spawnprotect_time > level.time)
				h.damage = 0;
			if ((h.dflags & DMGF_TEAM_BLOCKED) && attacker && SameTeam(targ, attacker))
				h.damage = 0;
		}
	}
	// 3.4 player path
	int damage = h.damage;
	if (cl && !god) {
		int left = PlayerDamagePath(targ, h);
		cl->g3_armorsaved += damage - left;
		damage = left;
	}
	// 4. feedback
	if (cl)
		PlayerFeedback(targ, h, damage, god);

	// 5. health, death, pain
	if (cl && damage > 0)
		ScriptGlue_ClientDamaged(targ); // G6b: SPEC-751 2.6, a hit throws the client back from a remote camera
	if (damage != 0) {
		targ->health -= damage;
		if (targ->health <= 0) {
			if (cl)
				targ->flags |= FL_NO_KNOCKBACK;
			if (targ->health < -999)
				targ->health = -999;
			targ->enemy = attacker;
			if (cl)
				PlayerAfterHealth(targ, h, damage);
			if (targ->die)
				targ->die(targ, h.inflictor, attacker, damage, h.point);
			return;
		}
	}
	if (cl)
		PlayerAfterHealth(targ, h, damage);
	else if (damage != 0 && targ->pain)
		targ->pain(targ, attacker, (float)h.knockback, damage, h.point); // the hit point (SPEC-720 5.3; G18 10-25)
}

// ---------------------------------------------------------------------------------------------------------------
// Behaviour 9: character rules (default G3BodyOwner::Damage)
// ---------------------------------------------------------------------------------------------------------------
static const char *const innocents[] = {"m_irq_fwoman1", "m_irq_fwoman2", "m_sib_mmechanic", "m_tok_mman2",
	"m_kos_mmechanic", "m_irq_mman2", "m_irq_moilworker", "m_sib_escientist1", "m_irq_eman1", "m_nyc_ebum", NULL};

int Dmg_CharacterDamage(G3Hit &h)
{
	edict_t *t = h.targ;
	G3Body *body = t->g3body;
	G3BodyOwner *ai = body ? body->owner : NULL;
	edict_t *attacker = h.attacker;
	// 9.1
	if ((h.mod & ~DMG_MOD_FRIENDLY) == MOD_TELEFRAG)
		return 0;
	if (attacker && attacker != t && IsPlayer(attacker) && SameTeam(t, attacker) && ai)
		ai->OnBetrayed(attacker);
	// 9.2 sneak attack
	if (!(h.dflags & DMGF_AREA) && IsPlayer(attacker) && !t->enemy && t->health > 0)
		h.damage *= 2;
	// 9.3
	if (t->flags & FL_NO_KNOCKBACK)
		h.knockback = 0;
	if (ai && attacker)
		ai->OnHitAlert(attacker);
	// 9.4 body
	int final = h.damage;
	if (body && h.damage != 0) {
		final = body->Damage(h);
		if (ai)
			ai->OnHit();
	}
	// 9.5 push
	PushCharacter(t, h);
	// 9.6
	if (final != 0 && IsPlayer(attacker) && t->health > 0) {
		if (ai && ai->IsHostage()) {
			if (gix.SP_Print)
				gix.SP_Print(attacker, 0x600);
		} else {
			for (int i = 0; innocents[i]; i++)
				if (G3_ClassIs(t, innocents[i])) {
					if (gix.SP_Print)
						gix.SP_Print(attacker, 0x60E);
					break;
				}
		}
	}
	t->health -= final;
	// 9.7
	if (t->health <= 0) {
		if (t->health < -999)
			t->health = -999;
		t->flags |= FL_NO_KNOCKBACK;
		if (ai)
			ai->Die(h.inflictor, attacker, final, h.point, h.dflags);
		return 1;
	}
	if (!(body && body->ProtectionClass() == 3 && final == 0) && ai)
		ai->Pain(attacker, h.point, h.knockback, final);
	return final != 0;
}

// Default death/pain of an owner without G2: the body's choice is played directly.
void G3BodyOwner::Die(edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, int dflags)
{
	(void)point;
	(void)inflictor;
	(void)damage;
	(void)attacker;
	(void)dflags;
}

void G3BodyOwner::Pain(edict_t *attacker, const vec3_t point, int knockback, int damage)
{
	(void)attacker;
	(void)point;
	(void)knockback;
	(void)damage;
}

// ---------------------------------------------------------------------------------------------------------------
// Behaviour 14: area damage
// ---------------------------------------------------------------------------------------------------------------
static int AreaCandidates(const vec3_t c, float r, edict_t **list, int max)
{
	vec3_t mins = {c[0] - r, c[1] - r, c[2] - r}, maxs = {c[0] + r, c[1] + r, c[2] + r};
	int n = 0;
	edict_t *tmp[MAX_EDICTS];
	for (int area = 1; area <= 2; area++) {
		int k = gix.BoxEdicts(mins, maxs, tmp, MAX_EDICTS, area);
		for (int i = 0; i < k && n < max; i++) {
			bool dup = false;
			for (int j = 0; j < n && !dup; j++)
				dup = list[j] == tmp[i];
			if (!dup)
				list[n++] = tmp[i];
		}
	}
	return n;
}

static void EntCentre(const edict_t *e, vec3_t c)
{
	if (e->solid == SOLID_BSP && e->s.origin[0] == 0 && e->s.origin[1] == 0 && e->s.origin[2] == 0) {
		for (int j = 0; j < 3; j++)
			c[j] = (e->absmin[j] + e->absmax[j]) * 0.5f;
		return;
	}
	for (int j = 0; j < 3; j++)
		c[j] = e->s.origin[j] + (e->mins[j] + e->maxs[j]) * 0.5f;
}

static bool AreaSkip(edict_t *e, edict_t *ignore)
{
	if (!e->inuse || e == world || e == ignore || !e->takedamage)
		return true;
	if (e->g3flags & G3F_NO_AREA)
		return true;
	if (e->g3body && e->g3body->gibbed)
		return true;
	return false;
}

static void AreaHit(edict_t *e, edict_t *inflictor, edict_t *attacker, const vec3_t centre, const vec3_t ec,
                    float p, int mod, int dflags)
{
	trace_t tr;
	memset(&tr, 0, sizeof tr);
	gix.trace(centre, vec3_origin, vec3_origin, ec, inflictor, 0x6000001, &tr);
	vec3_t dir;
	VectorSubtract(tr.endpos, centre, dir);
	int dmg = (int)p;
	if (dflags & DMGF_PUSH_ONLY)
		dmg = (int)(dmg * 0.05f);
	Dmg_Damage(e, inflictor, attacker, dir, tr.endpos, centre, dmg, (int)p, dflags | DMGF_AREA, mod, 0, 0);
}

void Dmg_RadiusLinear(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod,
                      int dflags)
{
	Gore_ResetGibCounter();
	if (!inflictor || radius <= 0)
		return;
	vec3_t centre;
	VectorCopy(inflictor->s.origin, centre);
	edict_t *list[MAX_EDICTS];
	int n = AreaCandidates(centre, radius, list, MAX_EDICTS);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (AreaSkip(e, ignore))
			continue;
		if (mod == MOD_FIRE && e->burn_time > level.time)
			continue;
		vec3_t ec, v;
		EntCentre(e, ec);
		VectorSubtract(ec, centre, v);
		float d = VectorLength(v);
		if (d > radius)
			continue;
		if (!Dmg_CanDamage(e, inflictor))
			continue;
		AreaHit(e, inflictor, attacker, centre, ec, damage * (1.0f - d / radius), mod, dflags);
	}
}

void Dmg_RadiusMinMax(edict_t *inflictor, edict_t *attacker, const vec3_t center, float maxdmg, float mindmg,
                      float radius, edict_t *ignore, int mod, int dflags)
{
	Gore_ResetGibCounter();
	if (!inflictor || radius <= 0)
		return;
	vec3_t centre;
	VectorCopy(center, centre);
	edict_t *list[MAX_EDICTS];
	int n = AreaCandidates(centre, radius, list, MAX_EDICTS);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (AreaSkip(e, ignore))
			continue;
		vec3_t ec, v;
		EntCentre(e, ec);
		VectorSubtract(ec, centre, v);
		float d = VectorLength(v);
		if (d > radius)
			continue;
		if (!Dmg_CanDamage(e, inflictor))
			continue;
		AreaHit(e, inflictor, attacker, centre, ec, mindmg + (maxdmg - mindmg) * (1.0f - d / radius), mod, dflags);
	}
}

void Dmg_RadiusCone(edict_t *inflictor, edict_t *attacker, float damage, float radius, edict_t *ignore, int mod,
                    const vec3_t conedir, float mincos)
{
	Gore_ResetGibCounter();
	if (!inflictor || radius <= 0)
		return;
	vec3_t src;
	EntCentre(inflictor, src);
	edict_t *list[MAX_EDICTS];
	int n = AreaCandidates(src, radius, list, MAX_EDICTS);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (AreaSkip(e, ignore))
			continue;
		vec3_t ec, v;
		EntCentre(e, ec);
		VectorSubtract(ec, src, v);
		float d = VectorLength(v);
		if (d > radius)
			continue;
		VectorNormalize(v);
		float dot = DotProduct(v, conedir);
		if (dot < mincos)
			continue;
		if (!Dmg_CanDamage(e, inflictor))
			continue;
		float p = damage * (1.0f - d / radius) * (dot - mincos) / (1.0f - mincos);
		vec3_t dir;
		VectorSubtract(e->s.origin, src, dir);
		Dmg_Damage(e, inflictor, attacker, dir, src, src, (int)p, (int)p, DMGF_AREA, mod, 0, 0);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Behaviour 15: CanDamage (Quake II with mask "opaque")
// ---------------------------------------------------------------------------------------------------------------
static bool ClearTo(edict_t *inflictor, const vec3_t dest, edict_t *targ)
{
	trace_t tr;
	memset(&tr, 0, sizeof tr);
	gix.trace(inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_OPAQUE, &tr);
	return tr.fraction == 1.0f || (targ && tr.ent == targ);
}

bool Dmg_CanDamage(edict_t *targ, edict_t *inflictor)
{
	if (!targ || !inflictor)
		return false;
	vec3_t dest;
	if (targ->movetype == MOVETYPE_PUSH || targ->solid == SOLID_BSP) {
		VectorAdd(targ->absmin, targ->absmax, dest);
		VectorScale(dest, 0.5f, dest);
		return ClearTo(inflictor, dest, targ);
	}
	if (ClearTo(inflictor, targ->s.origin, NULL))
		return true;
	static const float ofs[4][2] = {{15, 15}, {15, -15}, {-15, 15}, {-15, -15}};
	for (int i = 0; i < 4; i++) {
		VectorCopy(targ->s.origin, dest);
		dest[0] += ofs[i][0];
		dest[1] += ofs[i][1];
		if (ClearTo(inflictor, dest, NULL))
			return true;
	}
	return false;
}
