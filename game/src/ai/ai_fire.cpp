/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-782 sections 3 (bullets), 4 (shotgun), 11 (melee strike), table 3 (attack descriptors) — fallback
 *             only; SPEC-AMEND-006 G2.11 (SPEC-722 Firing with a target-bolt selector or the lethal flag)
 * Owner: package G2.
 *
 * The body (G3) fires a character's weapon through g3hooks.fireAttack. Since 2026-10-25 G18 sets it (SPEC-782 for
 * characters); at level start this file then only puts the separate shot path of shots with a target-bolt selector
 * or the lethal flag (SPEC-AMEND-006 G2.11) in front of G18's attack (ScriptedShotAttack). When no attack is
 * installed it still installs a reduced fallback (hitscan bullets with the table damage, falloff, the character
 * double damage, the deliberate-miss flag and the own-team rule; the shotgun's pellets; the melee strike; no
 * effects, sounds or projectiles).
 */
#include "ai_local.h"

namespace om_ai {

struct AttackRow {
	int dmg, flags, mod, range;
	float gA, gB;
};

/* SPEC-782 table 3 (dmg, flags, MOD, range, gA, gB) */
static const AttackRow attack_rows[31] = {
	{0, 0, 0, 0, 0, 0},              {22, 0x1000000, 0, 30, 0, 0.25f}, {30, 0x1000000, 0, 0, 0, 0},
	{45, 0, 3, 512, 0, 0},           {120, 0x20000, 4, 800, 0.5f, 0},  {40, 0x1010000, 5, 512, 0, 0.5f},
	{45, 0x10000, 6, 512, 0, 0},     {80, 0x1080000, 7, 1600, 0.5f, 0}, {30, 0, 0, 512, 0, 0},
	{80, 0x28000, 8, 512, 0, 0},     {200, 0x80, 37, 256, 0, 0},       {40, 0x20000, 9, 128, 0, 0.5f},
	{100, 0x20000, 10, 1024, 0.25f, 0}, {30, 0, 0, 512, 0, 0},         {30, 0, 0, 512, 0, 0},
	{15, 0, 0, 512, 0, 0},           {10, 0, 14, 512, 1, 0},           {200, 0, 14, 512, 1, 0},
	{15, 0, 0, 512, 0, 0},           {30, 0, 0, 512, 0, 0},            {30, 0, 0, 512, 0, 0},
	{10, 0, 0, 512, 0, 0},           {30, 0, 0, 512, 0, 0},            {30, 0, 0, 512, 0, 0},
	{30, 0, 0, 512, 0, 0},           {30, 0, 0, 512, 0, 0},            {30, 0, 0, 512, 0, 0},
	{30, 0, 0, 512, 0, 0},           {180, 0x20000, 0, 512, 0, 0},     {300, 0, 0, 512, 0, 0},
	{30, 0, 0, 0, 0, 0}};

static float DistToLine(const vec3_t p, const vec3_t a, const vec3_t b)
{
	vec3_t ab, ap;
	VectorSubtract(b, a, ab);
	VectorSubtract(p, a, ap);
	float l2 = DotProduct(ab, ab);
	float t = l2 > 0 ? DotProduct(ap, ab) / l2 : 0;
	if (t < 0)
		t = 0;
	if (t > 1)
		t = 1;
	vec3_t c;
	VectorMA(a, t, ab, c);
	return sqrtf(Dist2(c, p));
}

static void Bullet(edict_t *ent, int a, const vec3_t start, const vec3_t dir)
{
	const AttackRow &row = attack_rows[a];
	trace_t tr = Trace(ent->s.origin, nullptr, nullptr, start, ent, 0x6000081);
	if (tr.fraction > 0.99f) {
		float len = row.range == 128 ? 256.0f : row.range * 3.0f;
		vec3_t end;
		VectorMA(start, len, dir, end);
		edict_t *pass = (ent->flags & FL_DELIBERATE_MISS) && SightClient() ? SightClient() : ent;
		tr = Dmg_ShotTrace(start, vec3_origin, vec3_origin, end, pass, 0x6000081);
		if (tr.ent == ent && pass != ent) /* the shooter's own box (deliberate-miss pass-through, see Shotgun) */
			tr = Dmg_ShotTrace(start, vec3_origin, vec3_origin, end, ent, 0x6000081);
		/* the AI helper entities ("Nug Buddy", SPEC-700 rule 16) are reported by the engine's traces although they
		 * are not solid; let the shot pass them */
		for (int i = 0; i < 8 && tr.ent && (tr.ent->flags & FL_AI_HELPER) && tr.fraction < 1.0f; i++) {
			vec3_t s2;
			VectorMA(tr.endpos, 1, dir, s2);
			tr = Dmg_ShotTrace(s2, vec3_origin, vec3_origin, end, tr.ent, 0x6000081);
		}
	}
	if (CvarValue(cv.ai_trace))
		Dbg("ai %d: bullet %d hits %d (%s) frac %.2f\n", G_EdictNum(ent), a, tr.ent ? G_EdictNum(tr.ent) : -1,
		    tr.ent && tr.ent->classname ? tr.ent->classname : "-", tr.fraction);
	/* SPEC-782 section 3 step 7: glass/water along the shot (SHOT_MASK omits WINDOW) */
	{
		vec3_t impact;
		float glen = row.range == 128 ? 256.0f : row.range * 3.0f;
		if (tr.fraction < 1.0f)
			VectorCopy(tr.endpos, impact);
		else
			VectorMA(start, glen, dir, impact);
		trace_t tw = Trace(start, nullptr, nullptr, impact, ent, 0x22);
		if (tw.fraction < 1.0f) {
			int mat = tw.surface ? (int)(((unsigned)tw.surface->flags >> 24) & 0xff) : 0;
			bool glass = (mat == 37 || mat == 38) || (tw.contents & CONTENTS_WINDOW) ||
			             (tw.ent && tw.ent != world && tw.ent->material == 7);
			if (glass && tw.ent && tw.ent != world && tw.ent->takedamage)
				Dmg_Damage(tw.ent, ent, ent, dir, tw.endpos, start, row.dmg, 1, row.flags | 0x200, row.mod, 0, 0);
		}
	}
	if (!tr.ent || tr.ent == world || !tr.ent->takedamage)
		return;
	int dmg = row.dmg;
	bool nofall = (row.flags & (0x1000000 | 0x80000)) != 0;
	if (!nofall && tr.fraction > 0.5f)
		dmg = (int)(dmg / (2 * tr.fraction));
	if (nofall && !ent->client)
		dmg *= 2;
	if (!ent->client && SameTeam(ent, tr.ent) && !Cinematic()) {
		edict_t *sc = SightClient();
		if (!(sc && DistToLine(tr.endpos, start, sc->s.origin) <= 12 && IRand(0, 10) < 6))
			return;
	}
	Dmg_Damage(tr.ent, ent, ent, dir, tr.endpos, start, dmg, dmg, row.flags | 0x200, row.mod, row.gA, row.gB);
}

/* SPEC-782 §4 shotgun (attack 11) by a character: the centre bullet, then 12 pellets of length 256-512; creatures
 * hit by n pellets take 10·n (knockback 100, gB 0.2, flags | 0x8 against non-teammates; a teammate only with
 * probability 2/10 and not within 16 units of the line to the single-player player); other hits take 12 */
static void Shotgun(edict_t *ent, const vec3_t start_in, const vec3_t dir)
{
	const AttackRow &row = attack_rows[11];
	vec3_t start;
	VectorCopy(start_in, start);
	trace_t t0 = Trace(ent->s.origin, nullptr, nullptr, start, ent, 0x6000081);
	if (t0.fraction <= 0.99f)
		VectorCopy(ent->s.origin, start);
	Bullet(ent, 11, start, dir);
	vec3_t ang, right, up;
	vectoangles(dir, ang);
	AngleVectors(ang, nullptr, right, up);
	edict_t *hit[12];
	int count[12], nhit = 0;
	edict_t *pass = (ent->flags & FL_DELIBERATE_MISS) && SightClient() ? SightClient() : ent;
	for (int i = 0; i < 12; i++) {
		vec3_t d, end;
		VectorMA(dir, FlRand(-0.1f, 0.1f), right, d);
		VectorMA(d, FlRand(-0.05f, 0.05f), up, d);
		VectorNormalize(d);
		VectorMA(start, (float)IRand(256, 512), d, end);
		trace_t tr = Dmg_ShotTrace(start, vec3_origin, vec3_origin, end, pass, 0x6000081);
		/* with the deliberate-miss pass-through the shooter itself is not ignored; a pellet starting inside its
		 * own box must not hit it */
		if (tr.ent == ent && pass != ent)
			tr = Dmg_ShotTrace(start, vec3_origin, vec3_origin, end, ent, 0x6000081);
		if (!tr.ent || tr.fraction >= 1.0f)
			continue;
		bool creature = tr.ent->client || tr.ent->ai;
		if (!creature) {
			if (tr.ent != world && tr.ent->takedamage)
				Dmg_Damage(tr.ent, ent, ent, d, tr.endpos, start, 12, 100, row.flags | 0x200, row.mod, 0, 0);
			continue;
		}
		int k = 0;
		while (k < nhit && hit[k] != tr.ent)
			k++;
		if (k == nhit) {
			hit[nhit] = tr.ent;
			count[nhit++] = 0;
		}
		count[k]++;
	}
	for (int k = 0; k < nhit; k++) {
		edict_t *t = hit[k];
		if (!t->takedamage)
			continue;
		int flags = row.flags; /* the attack flags; 0x8 (spread hit location, SPEC-720 10.4) against non-teammates */
		if (SameTeam(ent, t)) {
			edict_t *sc = SightClient();
			if (IRand(0, 9) >= 2 || (sc && DistToLine(t->s.origin, start, sc->s.origin) <= 16))
				continue;
		} else
			flags |= 0x8;
		Dmg_Damage(t, ent, ent, dir, t->s.origin, start, 10 * count[k], 100, flags, row.mod, row.gA, 0.2f);
	}
}

/* SPEC-AMEND-006 G2.11: the selector's bolt of the target (mode 5 at the level time, times the target entity's
 * matrix from origin and angles); selector 2 and unknown selectors have no bolt */
static bool SelectorBolt(edict_t *t, int selector, vec3_t out)
{
	static const char *const bolts[10] = {nullptr,       "sbolt_mouth",   nullptr,       "gbolt_shoulder_lf",
	                                      "gbolt_shoulder_rf", "gbolt_chest_f", "gbolt_gut_f", "gbolt_groin",
	                                      "wbolt_hand_r", "sbolt_mouth"};
	if (selector < 1 || selector > 9 || !bolts[selector] || !t->ghoulInst)
		return false;
	ghoul::IGhoulObject *obj = t->ghoulInst->GetGhoulObject();
	ghoul::GhoulID b = obj ? obj->FindPart(bolts[selector]) : 0;
	if (!b)
		return false;
	ghoul::Matrix4 m;
	memset(&m, 0, sizeof m);
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1;
	m.flags = 1;
	m.m[3][0] = 1.0e30f; /* marker: the bolt is not in the current pose */
	t->ghoulInst->GetBoltMatrix(Now(), &m, b, 5, true);
	if (m.m[3][0] == 1.0e30f)
		return false;
	vec3_t ang = {-t->s.angles[PITCH], t->s.angles[YAW], -t->s.angles[ROLL]}, f, r, u;
	AngleVectors(ang, f, r, u);
	for (int i = 0; i < 3; i++)
		out[i] = t->s.origin[i] + m.m[3][0] * f[i] - m.m[3][1] * r[i] + m.m[3][2] * u[i];
	return true;
}

/* SPEC-AMEND-006 G2.11: a shot of a character whose current action carries the lethal flag or a target-bolt selector
 * at the AI's target takes a separate path: start at the origin + 20 up, straight at the selected bolt (or the
 * target's origin), no facing test and no per-type spread (the body's spread is replaced); attack types 9, 14, 16,
 * 17, 18 aim at the target's origin (a leaning player's lean box; 16 lower for a crouching player), type 11 mixes 60 %
 * of the bolt direction with 40 % towards the origin; blind fire adds U(-0.1, 0.1) per component. With the lethal
 * flag the target's body first gets the scripted kill setting (SPEC-720: variant 1, 2 for selector 9); selector 8
 * also makes the target drop the weapons in both hands. Returns false for an ordinary shot (left unchanged). */
static bool ScriptedShot(edict_t *ent, int a, vec3_t start, vec3_t dir, edict_t *target)
{
	AI *ai = ent ? ent->ai : nullptr;
	Action *cur = ai ? ai->current : nullptr;
	if (!cur || !(cur->lethal || cur->target_bolt) || !target || !target->inuse || ai->Target() != target)
		return false;
	int sel = cur->target_bolt;
	VectorCopy(ent->s.origin, start);
	start[2] += 20;
	vec3_t aim;
	if (!(sel && SelectorBolt(target, sel, aim)))
		VectorCopy(target->s.origin, aim);
	VectorSubtract(aim, start, dir);
	VectorNormalize(dir);
	if (!ai->FiresBlind()) {
		vec3_t to;
		edict_t *lp = target->client && PlayerLeaning(target) ? LeanProxy(target) : nullptr;
		VectorCopy(lp ? lp->s.origin : target->s.origin, to);
		if (target->client && (target->client->ps.pmove.pm_flags & PMF_DUCKED))
			to[2] -= 16;
		VectorSubtract(to, start, to);
		VectorNormalize(to);
		if (a == 9 || a == 14 || a == 16 || a == 17 || a == 18)
			VectorCopy(to, dir);
		else if (a == 11) {
			for (int k = 0; k < 3; k++)
				dir[k] = 0.6f * dir[k] + 0.4f * to[k];
			VectorNormalize(dir);
		}
	} else {
		for (int k = 0; k < 3; k++)
			dir[k] += FlRand(-0.1f, 0.1f);
		VectorNormalize(dir);
	}
	if (cur->lethal)
		Body_ScriptedKill(target, sel == 9 ? 2 : 1);
	if (sel == 8 && target->g3body)
		target->g3body->DropWeapons();
	if (CvarValue(cv.ai_trace))
		Dbg("ai %d: scripted shot %d at %d selector %d lethal %d aim (%.0f %.0f %.0f)\n", G_EdictNum(ent), a,
		    G_EdictNum(target), sel, cur->lethal, aim[0], aim[1], aim[2]);
	return true;
}

static void FallbackFireAttack(edict_t *ent, int a, const vec3_t start_in, const vec3_t dir_in, edict_t *target)
{
	if (!ent || a < 0 || a > 30)
		return;
	vec3_t start, dir;
	VectorCopy(start_in, start);
	VectorCopy(dir_in, dir);
	ScriptedShot(ent, a, start, dir, target);
	switch (a) {
	case 3: case 4: case 5: case 6: case 7: case 12: case 21: case 28: case 29:
		Bullet(ent, a, start, dir);
		break;
	case 11:
		Shotgun(ent, start, dir);
		break;
	case 20: {
		vec3_t s, e;
		VectorMA(start, -16, dir, s);
		VectorMA(start, 48, dir, e);
		trace_t tr = Trace(s, nullptr, nullptr, e, ent, 0x60000b9);
		if (tr.ent && tr.ent != world && tr.ent->takedamage)
			Dmg_Damage(tr.ent, ent, ent, dir, tr.endpos, start, 30, 100, 0x100, 31, 0, 0);
		break;
	}
	default:
		break;
	}
}

/* The game's character attack (G18, SPEC-782) with the separate path of scripted shots in front (ScriptedShot,
 * SPEC-AMEND-006 G2.11), until the body (G3) takes that path itself. */
typedef void (*fire_attack_fn)(edict_t *, int, const vec3_t, const vec3_t, edict_t *);
static fire_attack_fn game_attack;

static void ScriptedShotAttack(edict_t *ent, int a, const vec3_t start_in, const vec3_t dir_in, edict_t *target)
{
	vec3_t start, dir;
	VectorCopy(start_in, start);
	VectorCopy(dir_in, dir);
	ScriptedShot(ent, a, start, dir, target);
	if (game_attack)
		game_attack(ent, a, start, dir, target);
}

void InstallFireFallback(void)
{
	if (!g3hooks.fireAttack)
		g3hooks.fireAttack = FallbackFireAttack;
	else if (g3hooks.fireAttack != FallbackFireAttack && g3hooks.fireAttack != ScriptedShotAttack) {
		game_attack = g3hooks.fireAttack;
		g3hooks.fireAttack = ScriptedShotAttack;
	}
}

} /* namespace om_ai */
