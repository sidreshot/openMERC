/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 5 (per-frame think), 6 (perception and enemy choice), 7 (aim, fire, combat movement),
 *             8 (retreat, last-sighting chase), 9 (items), 10 (explosives), 11 (command construction),
 *             13 (waypoint following), 14.7-14.9 (hot spots, routes, roaming), 15 (game-mode objectives);
 *             SPEC-AMEND-005 G5.3 (fire-time values), G5.4 (item requests), G5.5, G5.6 (stuck rule),
 *             G5.7 (route start, last-waypoint refresh, no route), G5.8 (roaming)
 * Owner: package G5 (game/src/bots).
 */
#include "bot_local.h"

#include "bot_modes.h"

#define THINK_MASK MASK_PLAYERSOLID

static float AngleNorm180(float a)
{
	while (a > 180)
		a -= 360;
	while (a <= -180)
		a += 360;
	return a;
}

static float YawTo(const vec3_t from, const vec3_t to)
{
	vec3_t d;
	VectorSubtract(to, from, d);
	return vectoyaw(d);
}

static float HDist(const vec3_t a, const vec3_t b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return sqrtf(dx * dx + dy * dy);
}

static float Dist(const vec3_t a, const vec3_t b)
{
	vec3_t d;
	VectorSubtract(a, b, d);
	return VectorLength(d);
}

static void Eye(edict_t *e, vec3_t out)
{
	VectorCopy(e->s.origin, out);
	out[2] += e->viewheight;
}

bool BotVisible(edict_t *self, edict_t *other)
{
	vec3_t a, b;
	Eye(self, a);
	Eye(other, b);
	trace_t tr = gi.trace(a, vec3_origin, vec3_origin, b, self, MASK_OPAQUE);
	return tr.fraction == 1.0f;
}

static bool Spectating(edict_t *e) { return e->client && (e->client->resp.spectator || e->client->pers.spectator); }
static bool Alive(edict_t *e) { return e->inuse && e->health > 0 && e->deadflag == DEAD_NO; }

/* turning: towards the goal along the shorter way by at most the turning speed (1/65536-turn grid) */
static void TurnTowards(Bot *b, float goal)
{
	float ts = (float)BotP(b).turning_speed;
	float d = AngleNorm180(goal - b->view_yaw);
	if (d > ts)
		d = ts;
	if (d < -ts)
		d = -ts;
	b->view_yaw = (float)SHORT2ANGLE(ANGLE2SHORT(anglemod(b->view_yaw + d)));
}

/* movement windows (11): w = side window */
static void MoveWindows(Bot *b, const vec3_t from, const vec3_t target, float w)
{
	float rel = AngleNorm180(YawTo(from, target) - b->view_yaw);
	float mx = BotMax();
	if (fabsf(rel) < 90)
		b->fwd = mx;
	else if (fabsf(rel) > 90)
		b->fwd = -mx;
	if (fabsf(AngleNorm180(rel + 90)) < w)
		b->side = mx;
	else if (fabsf(AngleNorm180(rel - 90)) < w)
		b->side = -mx;
}

/* point probes */
static bool BlockedLine(edict_t *ent, const vec3_t a, const vec3_t b)
{
	trace_t tr = gi.trace(a, vec3_origin, vec3_origin, b, ent, THINK_MASK);
	return tr.fraction < 1.0f || tr.startsolid;
}

/* the floor below p is more than ~124 units down (d^2 > 15376) or is slime/lava */
static bool BadFloor(edict_t *ent, const vec3_t p, float down = 256)
{
	vec3_t e = {p[0], p[1], p[2] - down};
	trace_t tr = gi.trace(p, vec3_origin, vec3_origin, e, ent, THINK_MASK | CONTENTS_SLIME | CONTENTS_LAVA);
	if (tr.fraction == 1.0f)
		return true;
	float dz = p[2] - tr.endpos[2];
	if (dz * dz > 15376.0f)
		return true;
	return (tr.contents & (CONTENTS_SLIME | CONTENTS_LAVA)) != 0;
}

static void DirPoint(edict_t *ent, float yaw, float dist, float dz, vec3_t out)
{
	vec3_t ang = {0, yaw, 0}, f;
	AngleVectors(ang, f, NULL, NULL);
	VectorMA(ent->s.origin, dist, f, out);
	out[2] += dz;
}

static bool Ducked(edict_t *e) { return e->client && (e->client->ps.pmove.pm_flags & PMF_DUCKED); }

//==========================================================================================
// weapons

static int WeaponRank(int w)
{
	switch (w) {
	case WP_PISTOL1: return 1;
	case WP_PISTOL2: return 2;
	case WP_SNIPER: return 3;
	case WP_MPISTOL: return 4;
	case WP_SHOTGUN: return 5;
	case WP_ASSAULT: return 6;
	case WP_SLUGGER: return 7;
	case WP_ROCKET: return 8;
	case WP_FLAMEGUN: return 9;
	case WP_MPG: return 10;
	case WP_MACHINEGUN: return 11;
	}
	return 0;
}

static bool FreeAmmo(void) { return ((int)dmflags->value & 0x800) || deathmatch->value == 3; }

static bool RetreatsFrom(Bot *b, edict_t *ent, edict_t *enemy)
{
	if (g_wps.roam)
		return false;
	if (BotCarriesFlag(ent))
		return true;
	if (BotP(b).intelligence < 3)
		return false;
	if (ent->health <= 45)
		return true;
	if (enemy) {
		int own = WeaponRank(BotCurWeapon(ent)), their = WeaponRank(BotCurWeapon(enemy));
		if (own < their - 2 || own < 2)
			return true;
		if (BotCurWeapon(ent) == WP_FLAMEGUN && Dist(ent->s.origin, enemy->s.origin) > 350)
			return true;
	}
	return !g_danger.empty(); /* the 1.07f "danger near" test ignores the distance */
}

bool BotRetreats(Bot *b, edict_t *ent) { return RetreatsFrom(b, ent, b->enemy); }

//==========================================================================================
// perception (6)

bool BotAudible(Bot *b, edict_t *ent, edict_t *other)
{
	int n = (int)(other - g_edicts);
	if (n < 1 || n > MAX_CLIENTS)
		return false;
	const BotNoise &r = g_noise[n];
	if (r.time <= 0 || level.time >= r.expiry)
		return false;
	int intel = BotP(b).intelligence;
	if (r.kind > intel)
		return false;
	float range = r.loudness * (intel > 2 ? 2.0f : 1.0f);
	return Dist(ent->s.origin, other->s.origin) <= range;
}

static bool InFov(Bot *b, edict_t *ent, edict_t *other)
{
	int half = BotP(b).fov ? BotP(b).fov / 2 : 45;
	vec3_t d, ang;
	VectorSubtract(other->s.origin, ent->s.origin, d);
	vectoangles(d, ang);
	int dy = (int)AngleNorm180(ang[YAW] - b->view_yaw);
	int dp = (int)AngleNorm180(ang[PITCH]);
	return dy >= -half && dy <= half && dp >= -half && dp <= half;
}

static bool CandidateClient(Bot *b, edict_t *ent, edict_t *o)
{
	if (o == ent || !o->inuse || !o->client || !Alive(o) || Spectating(o))
		return false;
	if (!BotMode_AssassinAllows(ent, o))
		return false;
	if (OnSameTeam(ent, o))
		return false;
	(void)b;
	return true;
}

static void Acquire(Bot *b, edict_t *ent)
{
	if ((bot_notarget && bot_notarget->value) || Spectating(ent))
		return;
	const BotPersonality &P = BotP(b);
	edict_t *best = NULL;
	float bd = 0;
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *o = g_edicts + i;
		if (!CandidateClient(b, ent, o))
			continue;
		float d = Dist(ent->s.origin, o->s.origin);
		if (d > P.view_distance)
			continue;
		if (!BotAudible(b, ent, o) && !InFov(b, ent, o))
			continue;
		if (!gi.inPVS(ent->s.origin, o->s.origin) || !BotVisible(ent, o))
			continue;
		if (!best || d < bd) {
			best = o;
			bd = d;
		}
	}
	if (best)
		b->enemy = best;
}

static void StartChase(Bot *b)
{
	b->chase_stage = 1;
	b->chase_until = level.time + 10.0f;
}

static void EnemyUpdate(Bot *b, edict_t *ent)
{
	const BotPersonality &P = BotP(b);
	if (b->enemy) {
		edict_t *e = b->enemy;
		bool valid = e->inuse && e->client;
		bool vis = valid && gi.inPVS(ent->s.origin, e->s.origin) && BotVisible(ent, e);
		if (valid && vis) {
			if (!Alive(e) || Spectating(e) || OnSameTeam(ent, e) || e == ent || !BotMode_AssassinAllows(ent, e) ||
			    (bot_notarget && bot_notarget->value))
				b->enemy = NULL;
			else
				VectorCopy(e->s.origin, b->enemy_last_pos);
		} else {
			if (valid) {
				bool retreat = BotRetreats(b, ent);
				if (!retreat && !b->item) {
					if (e->s.origin[2] - ent->s.origin[2] < 15 && P.intelligence > 2) {
						int w = BotWp_Nearest(e->s.origin, 128, true, e, false);
						if (w) {
							b->dest_wp = w;
							VectorCopy(g_wps.wp[w].origin, b->dest_origin);
						} else
							StartChase(b);
					}
				} else if (P.intelligence > 3 && Dist(ent->s.origin, e->s.origin) < 512 &&
				           fabsf(e->s.origin[2] - ent->s.origin[2]) <= 5 && bot_runtact && bot_runtact->value)
					b->run_tactic = true;
			}
			b->enemy = NULL;
		}
	}
	if (!b->enemy)
		Acquire(b, ent);
	/* hunting */
	if (!b->enemy && bot_hunt && bot_hunt->value && !b->dest_wp) {
		edict_t *best = NULL;
		float bd = 0;
		for (int i = 1; i <= game.maxclients; i++) {
			edict_t *o = g_edicts + i;
			if (o == ent || !o->inuse || !o->client || !Alive(o) || Spectating(o))
				continue;
			if (!BotAudible(b, ent, o) || RetreatsFrom(b, ent, o))
				continue;
			float d = Dist(ent->s.origin, o->s.origin);
			if (!best || d < bd) {
				best = o;
				bd = d;
			}
		}
		if (best) {
			int w = BotWp_Nearest(best->s.origin, 64, true, best, false);
			if (w) {
				b->dest_wp = w;
				VectorCopy(g_wps.wp[w].origin, b->dest_origin);
			}
		}
	}
}

//==========================================================================================
// items in the inventory

/* "Request item type T" (SPEC-AMEND-005 G5.4): the bot sets the player module's pending item request (1 = itemnext
 * while the selected type differs, 4 = itemuse of the selected slot when it matches); the module carries it out in its
 * next think and a later request in the same frame replaces an earlier one. The request is set through the module's
 * command method, which stores exactly that pending request (SPEC-601 6); unlike the original's direct write it is
 * swallowed while a hand pseudo-weapon is finishing (no interface method writes the request field). */
enum { ITEMREQ_LOCKED, ITEMREQ_MISSING, ITEMREQ_SWITCHING, ITEMREQ_USED };

static int RequestItem(Bot *b, edict_t *ent, int type)
{
	if (level.time < b->switch_lock_until)
		return ITEMREQ_LOCKED;
	PlayerInventory *inv = BotInv(ent);
	if (!inv || !inv->HasItem(type))
		return ITEMREQ_MISSING;
	if (inv->SelectedItemType() != type) {
		inv->HandleCommand("itemnext", 0);
		return ITEMREQ_SWITCHING;
	}
	inv->HandleCommand("itemuse", 0);
	return ITEMREQ_USED;
}

//==========================================================================================
// fighting (7)

struct FireRow {
	float pri, alt;
};
static const FireRow g_fire[16] = {{0, 0},     {0, 2.0f},  {0.5f, 0},  {0.3f, 0},    {0.16f, 0}, {0.08f, 0},
                                   {2.0f, 0},  {0.3f, 2.0f}, {1.5f, 0}, {0.08f, 1.5f}, {3.0f, 3.0f}, {0.08f, 2.0f},
                                   {0, 1.3f},  {1.0f, 0},  {1.0f, 0},  {1.0f, 0}};

static int AmmoA(edict_t *ent)
{
	PlayerInventory *inv = BotInv(ent);
	return inv ? inv->ClipRounds() : 0;
}
static int AmmoB(edict_t *ent)
{
	PlayerInventory *inv = BotInv(ent);
	return inv ? inv->ReserveAmmo() : 0;
}

static bool ChooseAlt(Bot *b, edict_t *ent, float d)
{
	int w = BotCurWeapon(ent);
	int ammo = AmmoA(ent); /* UNVERIFIED which of the two counts (SPEC-741 7) */
	bool fr = FreeAmmo();
	switch (w) {
	case WP_KNIFE: return AmmoB(ent) >= 2 && 300 < d * d && d * d < 500; /* squared as coded: practically never */
	case WP_SLUGGER: return (ammo >= 3 || fr) && 350 < d && d < 500;
	case WP_MACHINEGUN: return (ammo >= 10 || fr) && 350 < d && d < 500;
	case WP_ROCKET: return (ammo >= 4 || fr) && d > 600;
	case WP_MPG: return (ammo > 10 || fr) && d < 300 && BotP(b).accuracy < 20;
	case WP_FLAMEGUN: return (ammo >= 6 || fr) && 350 < d && d < 500;
	}
	return false;
}

/* SPEC-741 7 with SPEC-AMEND-005 G5.3: the request is the constant 1.0 in the chosen mode (0 in the other) for this
 * frame only; Finalize writes it into the client's stored pending fire times and the user command */
static void Fire(Bot *b, edict_t *ent, edict_t *enemy)
{
	const BotPersonality &P = BotP(b);
	int w = BotCurWeapon(ent);
	float d = Dist(ent->s.origin, enemy->s.origin);
	float to = YawTo(ent->s.origin, enemy->s.origin);
	float off = fabsf(AngleNorm180(to - b->view_yaw));
	int acc = P.accuracy < 0 ? 0 : P.accuracy > 160 ? 160 : P.accuracy;
	bool cone = off <= acc + 10;
	if (P.intelligence > 3 && P.accuracy < 20 && (w == WP_SLUGGER || w == WP_ROCKET) && off > 10)
		cone = false;
	b->fire_pri = b->fire_alt = 0;
	if (!cone || level.time < b->weapon_hold_until || level.time < b->no_ammo_until)
		return;
	if (!FreeAmmo() && AmmoA(ent) == 0 && AmmoB(ent) == 0 && w != WP_KNIFE) {
		b->no_ammo_until = level.time + 2.0f;
		return;
	}
	/* "when the next-fire time has passed": strictly later (black box, SPEC-AMEND-005 G5.3 verify: the original's
	 * pistol1 bots fire 0.4 s apart in about 4 of 5 shots and 0.3 s in the rest, shotgun bots 1.6 s; the float sums of
	 * now + delay fall on or just above a later frame time) */
	if (level.time <= b->next_fire)
		return;
	bool alt = ChooseAlt(b, ent, d);
	int wi = w >= 0 && w < 16 ? w : 0;
	if (alt)
		b->fire_alt = 1.0f;
	else
		b->fire_pri = 1.0f;
	b->next_fire = level.time + (alt ? g_fire[wi].alt : g_fire[wi].pri);
}

static void Aim(Bot *b, edict_t *ent, edict_t *enemy)
{
	const BotPersonality &P = BotP(b);
	vec3_t aim, eye, d, ang;
	VectorCopy(enemy->s.origin, aim);
	aim[2] -= 10;
	if (Ducked(enemy))
		aim[2] -= 20;
	if (BotCurWeapon(ent) == WP_ROCKET)
		aim[2] = enemy->s.origin[2] - 30;
	Eye(ent, eye);
	VectorSubtract(aim, eye, d);
	vectoangles(d, ang);
	b->view_pitch = ang[PITCH];
	if (level.time >= b->aim_err_timer) {
		b->aim_err = P.accuracy > 0 ? gix.irand(0, P.accuracy - 1) : 0;
		int in = P.intelligence;
		float t = in < 2 ? gix.flrand(3, 5) : in == 2 ? gix.flrand(2, 4) : in == 3 ? gix.flrand(1, 2) : gix.flrand(0.5f, 1);
		b->aim_err_timer = level.time + t;
	}
	/* the error is always added counter-clockwise (both drawn signs add |e|) */
	b->yaw_goal = ang[YAW] + (float)abs(b->aim_err);
	TurnTowards(b, b->yaw_goal);
}

static bool LedgeReverse(Bot *b, edict_t *ent, float yaw)
{
	if (BotP(b).intelligence <= 2)
		return false;
	vec3_t p;
	DirPoint(ent, yaw, 50, 0, p);
	if (BlockedLine(ent, ent->s.origin, p))
		return false;
	return BadFloor(ent, p);
}

static bool SideUnsafe(Bot *b, edict_t *ent, int dir)
{
	const BotPersonality &P = BotP(b);
	float yaw = b->view_yaw - 90.0f * dir; /* dir 1 = right */
	if (P.intelligence >= 2) {
		for (int k = -1; k <= 1; k++) {
			vec3_t p, f, ang = {0, b->view_yaw, 0};
			DirPoint(ent, yaw, 100, 0, p);
			AngleVectors(ang, f, NULL, NULL);
			VectorMA(p, 8.0f * k, f, p);
			vec3_t e = {p[0], p[1], p[2] - 50};
			trace_t tr = gi.trace(p, vec3_origin, vec3_origin, e, ent, THINK_MASK | CONTENTS_SLIME | CONTENTS_LAVA);
			if (tr.fraction == 1.0f || (tr.contents & (CONTENTS_SLIME | CONTENTS_LAVA)))
				return true;
		}
	}
	if (P.intelligence > 2) {
		vec3_t p;
		DirPoint(ent, yaw, 50, 0, p);
		if (BlockedLine(ent, ent->s.origin, p))
			return true;
		if (b->enemy) {
			vec3_t q, ee;
			DirPoint(ent, yaw, 30, ent->viewheight, q);
			Eye(b->enemy, ee);
			if (BlockedLine(ent, q, ee))
				return true;
		}
	}
	return false;
}

static void Strafe(Bot *b, edict_t *ent)
{
	const BotPersonality &P = BotP(b);
	if (level.time < b->strafe_pause_until) {
		b->strafe = 0;
		return;
	}
	bool early = b->strafe && SideUnsafe(b, ent, b->strafe);
	if (level.time >= b->strafe_switch || early) {
		int nd = b->strafe ? -b->strafe : (gix.irand(0, 1) ? 1 : -1);
		b->strafe = nd;
		b->strafe_switch = level.time + 1 + (early ? gix.flrand(1, 5) : gix.flrand(1, 3));
		if (!early) {
			int chance = P.intelligence < 2 ? 5 : P.intelligence == 2 ? 3 : 0;
			if (chance && gix.irand(1, 10) <= chance) {
				b->strafe_pause_until = level.time + 0.5f + gix.flrand(0, 1);
				b->strafe = 0;
			}
		}
	}
	if (b->strafe > 0)
		b->side = BotMax();
	else if (b->strafe < 0)
		b->side = -BotMax();
}

static void Fight(Bot *b, edict_t *ent)
{
	edict_t *e = b->enemy;
	VectorCopy(ent->s.origin, b->last_fight_pos);
	Aim(b, ent, e);
	Fire(b, ent, e);
	int w = BotCurWeapon(ent);
	float r = (w == WP_SLUGGER || w == WP_ROCKET) ? 350.0f : w == WP_KNIFE ? 30.0f : 100.0f;
	float nearr = sqrtf(r * r + 40 * (10 - r)), farr = sqrtf(r * r + 8 * (5 * r + 50));
	float d = Dist(ent->s.origin, e->s.origin);
	float mx = BotMax();
	if (d < nearr) {
		vec3_t back;
		DirPoint(ent, b->view_yaw + 180, 200, 0, back);
		if (!BlockedLine(ent, ent->s.origin, back)) {
			b->fwd = -mx;
			if (LedgeReverse(b, ent, b->view_yaw + 180))
				b->fwd = -b->fwd;
		}
	} else if (d > farr) {
		b->fwd = mx;
		if (LedgeReverse(b, ent, b->view_yaw))
			b->fwd = -b->fwd;
	}
	Strafe(b, ent);
}

//==========================================================================================
// obstacles (13.5): knee-high block

static bool NeedsJump(edict_t *ent, const vec3_t target)
{
	float yaw = YawTo(ent->s.origin, target);
	vec3_t a, bpt, c, d;
	DirPoint(ent, yaw, 0, 24, a);
	DirPoint(ent, yaw, 50, 24, bpt);
	DirPoint(ent, yaw, 0, -23, c);
	DirPoint(ent, yaw, 50, -23, d);
	if (!BlockedLine(ent, a, bpt) && BlockedLine(ent, c, d))
		return true;
	DirPoint(ent, yaw, 0, 40, a);
	DirPoint(ent, yaw, 20, 40, bpt);
	DirPoint(ent, yaw, 0, -10, c);
	DirPoint(ent, yaw, 20, -10, d);
	return !BlockedLine(ent, a, bpt) && BlockedLine(ent, c, d);
}

static void SideStepWalls(Bot *b, edict_t *ent)
{
	vec3_t base, f, r, ang = {0, b->view_yaw, 0};
	AngleVectors(ang, f, r, NULL);
	bool blocked[3];
	for (int k = -1; k <= 1; k++) {
		VectorMA(ent->s.origin, 18.0f * k, r, base);
		vec3_t end;
		VectorMA(base, 50, f, end);
		blocked[k + 1] = BlockedLine(ent, base, end);
	}
	if (blocked[2])
		b->side = -BotMax(); /* right blocked -> step left */
	else if (blocked[0] || blocked[1])
		b->side = BotMax();
}

//==========================================================================================
// items (9)

static bool WantedItem(Bot *b, edict_t *ent, edict_t *it)
{
	if (!it->inuse || !it->item || it->solid == SOLID_NOT || (it->svflags & SVF_NOCLIENT))
		return false;
	int idx = ITEM_INDEX(it->item);
	if (idx == 36)
		return BotMode_FlagWanted(ent, it);
	if (idx == 37 || idx == 38)
		return ent->health < 100;
	switch (it->item->kind) {
	case IK_AMMO:
	case IK_WEAPON: return true;
	case IK_ARMOR:
	case IK_EQUIP: return BotP(b).intelligence >= 3;
	}
	return false;
}

static bool HalfwayFloor(edict_t *ent, edict_t *it)
{
	vec3_t mid;
	for (int i = 0; i < 3; i++)
		mid[i] = (ent->s.origin[i] + it->s.origin[i]) * 0.5f;
	mid[2] -= 41;
	trace_t tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin, mid, ent, THINK_MASK);
	return tr.fraction < 1.0f;
}

static bool ItemReachable(edict_t *ent, edict_t *it, bool enemyflag)
{
	if (enemyflag)
		return true;
	if (it->s.origin[2] > ent->s.origin[2] + 5)
		return false;
	return HalfwayFloor(ent, it);
}

static bool ItemGoal(Bot *b, edict_t *ent)
{
	if (!b->item && level.time >= b->item_retry) {
		edict_t *best = NULL;
		float bd = 0;
		for (int i = game.maxclients + 1; i < globals.num_edicts; i++) {
			edict_t *it = g_edicts + i;
			if (!it->inuse || !it->item || !WantedItem(b, ent, it))
				continue;
			bool ef = ITEM_INDEX(it->item) == 36 && BotMode_IsEnemyFlag(ent, it);
			float d = Dist(ent->s.origin, it->s.origin);
			if (!ef && d > 512)
				continue;
			if (!ItemReachable(ent, it, ef))
				continue;
			if (!best || d < bd) {
				best = it;
				bd = d;
			}
		}
		if (best) {
			b->item = best;
			b->item_giveup = level.time + 10;
		}
	}
	if (!b->item)
		return false;
	edict_t *it = b->item;
	bool ef = it->inuse && it->item && ITEM_INDEX(it->item) == 36 && BotMode_IsEnemyFlag(ent, it);
	if (!WantedItem(b, ent, it) || !gi.inPVS(ent->s.origin, it->s.origin) || !ItemReachable(ent, it, ef)) {
		b->item = NULL;
		b->item_retry = level.time + 15;
		return false;
	}
	if (Dist(ent->s.origin, it->s.origin) < 10) {
		b->item = NULL;
		b->item_retry = level.time + 5;
		return false;
	}
	if (level.time >= b->item_giveup) {
		b->item = NULL;
		b->item_retry = level.time + 10;
		return false;
	}
	if (!b->enemy) {
		b->view_pitch = 0;
		TurnTowards(b, YawTo(ent->s.origin, it->s.origin));
		MoveWindows(b, ent->s.origin, it->s.origin, 45);
	} else {
		Aim(b, ent, b->enemy);
		Fire(b, ent, b->enemy);
		MoveWindows(b, ent->s.origin, it->s.origin, 90);
	}
	if (NeedsJump(ent, it->s.origin))
		b->jump_until = level.time + 0.1f;
	return true;
}

//==========================================================================================
// explosives (10)

/* SPEC-741 10 with SPEC-AMEND-005 G5.4: the 1/2 decides whether to throw (always while a previous request is still
 * switching); the type is the grenade when carried, else the flash pack; no fire press is involved */
static void GrenadeRule(Bot *b, edict_t *ent)
{
	PlayerInventory *inv = BotInv(ent);
	if (!inv || level.time < b->next_grenade)
		return;
	bool g = inv->HasItem(IT_GRENADE), f = inv->HasItem(IT_FLASHPACK);
	if (!g && !f)
		return;
	if (gix.irand(0, 1) || b->switch_in_progress) {
		int r = RequestItem(b, ent, g ? IT_GRENADE : IT_FLASHPACK);
		b->switch_in_progress = r == ITEMREQ_SWITCHING;
		if (r == ITEMREQ_USED)
			b->next_grenade = level.time + gix.flrand(15, 30);
		/* switching: keep trying next frame; locked or missing: try again next frame */
	} else
		b->next_grenade = level.time + gix.flrand(15, 30);
}

/* run tactic: C4, or a grenade when no C4 is carried; anything but "switching" ends the tactic and sets the 2 s item
 * lock (SPEC-AMEND-005 G5.4) */
static void RunTactic(Bot *b, edict_t *ent)
{
	if (!b->run_tactic || b->enemy)
		return;
	PlayerInventory *inv = BotInv(ent);
	int r = RequestItem(b, ent, inv && inv->HasItem(IT_C4) ? IT_C4 : IT_GRENADE);
	if (r != ITEMREQ_SWITCHING) {
		b->run_tactic = false;
		b->switch_lock_until = level.time + 2.0f;
	}
}

//==========================================================================================
// waypoints (13, 14.7-14.9)

static int HotSpot(Bot *b, edict_t *ent)
{
	int w = BotCurWeapon(ent);
	int minp;
	switch (w) {
	case WP_PISTOL2: minp = 2; break;
	case WP_MPISTOL: minp = 3; break;
	case WP_ASSAULT: minp = 7; break;
	case WP_SNIPER: minp = 4; break;
	case WP_SLUGGER: minp = 6; break;
	case WP_SHOTGUN: minp = 5; break;
	case WP_MACHINEGUN: minp = 11; break;
	case WP_ROCKET: minp = 8; break;
	case WP_MPG: minp = 10; break;
	case WP_FLAMEGUN: minp = 9; break;
	case WP_NONE: minp = 4; break;
	default: minp = 1; break;
	}
	(void)b;
	int best = 0, bp = 0;
	float bd = 0;
	for (int i = 1; i <= g_wps.count; i++) {
		int p = g_wps.wp[i].pref;
		if (p < minp)
			continue;
		float d = Dist(ent->s.origin, g_wps.wp[i].origin);
		if (d <= 1024)
			continue;
		if (!best || p > bp + 3 || (p > bp - 3 && d < bd)) {
			best = i;
			bp = p;
			bd = d;
		}
	}
	return best;
}

static bool WpUsable(Bot *b, edict_t *ent, int w)
{
	if (w < 1 || w > g_wps.count)
		return false;
	(void)b;
	const BotWaypoint &wp = g_wps.wp[w];
	if (VectorCompare(wp.origin, vec3_origin))
		return false;
	if (wp.origin[2] - ent->s.origin[2] >= 5)
		return false;
	return BotWp_Visible(ent->s.origin, wp.origin, ent);
}

static void PickCurrentWaypoint(Bot *b, edict_t *ent)
{
	int best = 0;
	float bd = 0;
	if (b->last_wp >= 1 && b->last_wp <= g_wps.count && g_wps.version >= 4) {
		const BotWaypoint &lw = g_wps.wp[b->last_wp];
		for (int l = 0; l < lw.nlinks; l++) {
			int t = BotWp_LinkTarget(lw.link[l]);
			if (!WpUsable(b, ent, t))
				continue;
			float d = Dist(ent->s.origin, g_wps.wp[t].origin);
			if (!best || d < bd) {
				best = t;
				bd = d;
			}
		}
	} else {
		for (int t = 1; t <= g_wps.count; t++) {
			if (!WpUsable(b, ent, t))
				continue;
			float d = Dist(ent->s.origin, g_wps.wp[t].origin);
			if (!best || d < bd) {
				best = t;
				bd = d;
			}
		}
	}
	if (!best)
		best = BotWp_Nearest(ent->s.origin, 1e9f, true, ent, false);
	if (best) {
		b->cur_wp = best;
		b->wp_deadline = level.time + 30;
	}
}

static bool DoorAhead(edict_t *ent, float yaw)
{
	vec3_t p;
	DirPoint(ent, yaw, 150, 0, p);
	trace_t tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin, p, ent, THINK_MASK);
	return tr.ent && tr.ent->classname && strstr(tr.ent->classname, "door");
}

static void DropCurrentIfLost(Bot *b, edict_t *ent)
{
	if (!b->cur_wp)
		return;
	const BotWaypoint &w = g_wps.wp[b->cur_wp];
	if (w.kind == WPK_VISIBLE)
		return;
	if (!gi.inPVS(ent->s.origin, w.origin)) {
		b->cur_wp = 0;
		return;
	}
	trace_t tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin, w.origin, ent, THINK_MASK);
	if (tr.fraction < 1.0f && !(tr.ent && tr.ent->client) && !DoorAhead(ent, YawTo(ent->s.origin, w.origin)))
		b->cur_wp = 0;
}

/* roaming (no destination) on reaching waypoint c, SPEC-741 14.9 with SPEC-AMEND-005 G5.8; returns the next waypoint, or
 * c to stay (the rule runs again at the next think with the new direction) */
static int RoamNext(Bot *b, int c)
{
	int d = b->walk_dir >= 0 ? 1 : -1;
	int n = c + d;
	/* 1: continue along the consecutive chain */
	if (n >= 1 && n <= g_wps.count && BotWp_Links(c, n))
		return n;
	const BotWaypoint &w = g_wps.wp[c];
	/* 2: jump to the tagged waypoint */
	if (w.tag >= 1 && w.tag <= g_wps.count) {
		int t = w.tag;
		if (t >= 2)
			b->walk_dir = BotWp_Links(t, t - 1) ? -1 : 1;
		else
			b->walk_dir = BotWp_Links(t, 2) ? 1 : -1;
		return t;
	}
	/* 3: path end without a tag: the cheapest link leaving the consecutive chain */
	int best = 0;
	float bc = 0;
	for (int l = 0; l < w.nlinks; l++) {
		int t = BotWp_LinkTarget(w.link[l]);
		if (t < 1 || t > g_wps.count || t == c - 1 || t == c + 1)
			continue;
		if (!best || w.cost[l] < bc) {
			best = t;
			bc = w.cost[l];
		}
	}
	if (best) {
		b->roam_s = c;
		b->roam_ds = d;
		if (BotWp_Links(best, best - 1)) {
			b->walk_dir = -1;
			return best;
		}
		if (BotWp_Links(best, best + 1)) {
			b->walk_dir = 1;
			return best;
		}
		b->walk_dir = -1; /* stays at c */
		return c;
	}
	/* no such link: the waypoint after the remembered branch point in the remembered direction, when c links to it */
	int k = b->roam_s + (b->roam_ds >= 0 ? 1 : -1);
	if (k >= 1 && k <= g_wps.count && BotWp_Links(c, k)) {
		b->walk_dir = b->roam_ds >= 0 ? 1 : -1;
		b->roam_s = c;
		b->roam_ds = d;
		return k;
	}
	b->walk_dir = -d; /* stays at c and reverses */
	return c;
}

/* route following (SPEC-741 14.8 with SPEC-AMEND-005 G5.7): start = the bot's last waypoint (or the nearest non-spawn
 * waypoint); with no route the bot keeps the reached waypoint and tries again at every think (no roaming fallback) */
static int RouteNext(Bot *b, edict_t *ent, int c)
{
	BotRouteCache &r = b->route;
	if (r.dest != b->dest_wp || level.time - r.time > 20.0f) {
		r.dest = b->dest_wp;
		r.time = level.time;
		r.pos = 0;
		int start = b->last_wp >= 1 && b->last_wp <= g_wps.count ? b->last_wp
		                                                          : BotWp_Nearest(ent->s.origin, 1e9f, false, NULL, true);
		if (!BotWp_Route(start, b->dest_wp, r.steps)) {
			r.steps.clear();
			r.dest = 0; /* not cached: retried at the next think */
			return c;
		}
	}
	while (r.pos < (int)r.steps.size() && r.steps[r.pos].node == c) {
		r.pos++;
		if (r.pos < (int)r.steps.size() && r.steps[r.pos].path != r.steps[r.pos - 1].path)
			return r.steps[r.pos].node; /* cross to the other path */
	}
	int target = r.pos < (int)r.steps.size() ? r.steps[r.pos].node : b->dest_wp;
	if (target == c)
		return c;
	int n = c + (target > c ? 1 : -1);
	b->walk_dir = target > c ? 1 : -1;
	return (n >= 1 && n <= g_wps.count) ? n : target;
}

static bool MoverPresent(int w)
{
	const float *o = g_wps.wp[w].origin;
	vec3_t e = {o[0], o[1], o[2] - 40};
	trace_t tr = gi.trace(o, vec3_origin, vec3_origin, e, NULL, MASK_SOLID);
	if (!tr.ent || tr.fraction == 1.0f || !tr.ent->classname)
		return false;
	return !strcmp(tr.ent->classname, "func_door") || !strcmp(tr.ent->classname, "func_train") ||
	       !strcmp(tr.ent->classname, "func_plat");
}

static bool FollowWaypoints(Bot *b, edict_t *ent)
{
	if (g_wps.roam || g_wps.count < 1)
		return false;
	const BotPersonality &P = BotP(b);
	DropCurrentIfLost(b, ent);
	if (!b->cur_wp)
		PickCurrentWaypoint(b, ent);
	if (!b->cur_wp)
		return false;
	BotMode_Objectives(b, ent);
	if (P.intelligence >= 3 && !g_danger.empty()) {
		/* walk away from the danger: towards the neighbouring waypoint farther from it (UNVERIFIED sense) */
		edict_t *dz = g_danger.front().ent;
		if (dz && dz->inuse) {
			int c = b->cur_wp;
			float dp = c < g_wps.count ? Dist(g_wps.wp[c + 1].origin, dz->s.origin) : -1;
			float dm = c > 1 ? Dist(g_wps.wp[c - 1].origin, dz->s.origin) : -1;
			b->walk_dir = dp >= dm ? 1 : -1;
		}
	} else if (bot_wppref && bot_wppref->value && P.intelligence >= 4 && !b->dest_wp) {
		int h = HotSpot(b, ent);
		if (h) {
			b->dest_wp = h;
			VectorCopy(g_wps.wp[h].origin, b->dest_origin);
		}
	}
	const BotWaypoint &w = g_wps.wp[b->cur_wp];
	float reach = w.kind == WPK_LADDER_BOTTOM ? 5.0f : 30.0f;
	if (Dist(ent->s.origin, w.origin) < reach) {
		if (w.kind == WPK_JUMP)
			b->jump_until = level.time + 1.0f;
		b->left_jump_wp = w.kind == WPK_JUMP; /* SPEC-AMEND-005 G5.6 */
		int c = b->cur_wp;
		if (b->dest_wp == c)
			b->dest_wp = 0;
		int next = b->dest_wp ? RouteNext(b, ent, c) : RoamNext(b, c);
		/* a bot that stays at the reached waypoint (no route, or roaming turned back) keeps its last-waypoint record,
		 * which the per-think refresh moves among the neighbours (black box, F5: the original's bot_wp_botroute shows
		 * a hovering bot "near" 95, 96, 97 alternately while it stays at 96) */
		if (next != c) {
			b->prev_wp = c;
			b->last_wp = c;
		}
		b->cur_wp = next;
		b->wp_deadline = level.time + 30;
	}
	if (!b->cur_wp)
		return false;
	const BotWaypoint &cw = g_wps.wp[b->cur_wp];
	float remaining = b->wp_deadline - level.time;
	/* SPEC-741 13.5 with SPEC-AMEND-005 G5.6: 10 < remaining < 29 (both strict); "the waypoint is a jump waypoint" =
	 * the waypoint just reached (the one the bot is leaving) was kind 2 */
	if (remaining > 10 && remaining < 29) {
		bool jump = NeedsJump(ent, cw.origin) || b->left_jump_wp || (cw.kind == WPK_JUMP && remaining < 26) ||
		            remaining < 15;
		if (jump)
			b->jump_until = level.time + 1.0f;
		else if (level.time >= b->jump_until)
			SideStepWalls(b, ent);
	} else if (remaining < 0 && !b->enemy) {
		b->cur_wp = 0;
		return true; /* stands */
	}
	/* elevators */
	if (cw.kind == WPK_ELEVATOR && !MoverPresent(b->cur_wp)) {
		if (!b->enemy) {
			b->view_pitch = 0;
			TurnTowards(b, YawTo(ent->s.origin, cw.origin));
		}
		bool other = false;
		for (int i = 1; i <= game.maxclients; i++)
			if (g_bots[i].inuse && &g_bots[i] != b && g_bots[i].cur_wp == b->cur_wp &&
			    Dist(g_edicts[i].s.origin, cw.origin) < 256)
				other = true;
		if (other || Dist(ent->s.origin, cw.origin) < 128)
			b->fwd = -BotMax();
		return true;
	}
	if (cw.kind == WPK_LADDER_TOP && b->prev_wp >= 1 && g_wps.wp[b->prev_wp].kind == WPK_LADDER_BOTTOM) {
		TurnTowards(b, YawTo(ent->s.origin, cw.origin));
		b->fwd = BotMax();
		b->up = 0;
		return true;
	}
	if (!b->enemy) {
		b->view_pitch = 0;
		TurnTowards(b, YawTo(ent->s.origin, cw.origin));
		MoveWindows(b, ent->s.origin, cw.origin, 45);
		RunTactic(b, ent);
	} else {
		Aim(b, ent, b->enemy);
		GrenadeRule(b, ent);
		Fire(b, ent, b->enemy);
		MoveWindows(b, ent->s.origin, cw.origin, 90);
	}
	if (cw.kind == WPK_CROUCH)
		b->crouch_wp = 1;
	return true;
}

//==========================================================================================
// last-sighting chase (8)

static bool Chase(Bot *b, edict_t *ent)
{
	if (!b->chase_stage)
		return false;
	if (level.time > b->chase_until) {
		b->chase_stage = 0;
		return false;
	}
	const float *t = b->chase_stage == 1 ? b->last_fight_pos : b->enemy_last_pos;
	if (Dist(ent->s.origin, t) < 30) {
		if (b->chase_stage == 1)
			b->chase_stage = 2;
		else {
			b->chase_stage = 0;
			return false;
		}
		t = b->enemy_last_pos;
	}
	vec3_t p;
	DirPoint(ent, b->view_yaw, 150, 0, p);
	if (!BlockedLine(ent, ent->s.origin, p) && BadFloor(ent, p)) {
		b->chase_stage = 0;
		return true; /* the way drops off: stand */
	}
	b->view_pitch = 0;
	TurnTowards(b, YawTo(ent->s.origin, t));
	MoveWindows(b, ent->s.origin, t, 45);
	return false;
}

//==========================================================================================
// roam mode (11)

static void Roam(Bot *b, edict_t *ent)
{
	b->view_pitch = 0;
	vec3_t p;
	DirPoint(ent, b->view_yaw, 50, 0, p);
	bool unsafe = BlockedLine(ent, ent->s.origin, p) || BadFloor(ent, p);
	if (!unsafe)
		for (int k = -1; k <= 1; k += 2) {
			vec3_t base, f, r, ang = {0, b->view_yaw, 0}, end;
			AngleVectors(ang, f, r, NULL);
			VectorMA(ent->s.origin, 18.0f * k, r, base);
			VectorMA(base, 50, f, end);
			if (BlockedLine(ent, base, end))
				unsafe = true;
		}
	if (unsafe) {
		b->fwd = 0;
		b->view_yaw = gix.flrand(0, 360);
	} else
		b->fwd = BotMax();
}

//==========================================================================================
// the use probe (SPEC-760 B3.8 selection without using)

static edict_t *UseProbe(edict_t *ent)
{
	vec3_t eye, mins, maxs, fwd, flat;
	edict_t *list[1024];
	Eye(ent, eye);
	for (int i = 0; i < 3; i++) {
		mins[i] = eye[i] - 16;
		maxs[i] = eye[i] + 16;
	}
	int n = gi.BoxEdicts(mins, maxs, list, 1024, 2);
	for (int i = 0; i < n; i++)
		if (list[i]->inuse && list[i]->plUse && list[i]->classname && strstr(list[i]->classname, "trigger"))
			return list[i];
	vec3_t ang = {0, ent->client ? ent->client->v_angle[YAW] : ent->s.angles[YAW], 0};
	AngleVectors(ang, fwd, NULL, NULL);
	VectorCopy(fwd, flat);
	for (int i = 0; i < 3; i++) {
		mins[i] = eye[i] - 80;
		maxs[i] = eye[i] + 80;
	}
	n = gi.BoxEdicts(mins, maxs, list, 1024, 1);
	edict_t *best = NULL;
	float bestdot = 0.2f;
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (!e->inuse || !e->plUse || e == ent)
			continue;
		vec3_t c, d;
		for (int j = 0; j < 3; j++)
			c[j] = (e->absmin[j] + e->absmax[j]) * 0.5f;
		VectorSubtract(c, eye, d);
		if (VectorLength(d) > 80)
			continue;
		d[2] = 0;
		VectorNormalize(d);
		float dot = DotProduct(d, flat);
		if (dot < bestdot)
			continue;
		trace_t tr = gi.trace(eye, vec3_origin, vec3_origin, c, ent, MASK_PLAYERSOLID);
		if (tr.fraction < 1.0f && tr.ent != e)
			continue;
		best = e;
		bestdot = dot;
	}
	if (best)
		return best;
	vec3_t end;
	AngleVectors(ent->client ? ent->client->v_angle : ent->s.angles, fwd, NULL, NULL);
	VectorMA(eye, 75, fwd, end);
	trace_t tr = gi.trace(eye, vec3_origin, vec3_origin, end, ent, MASK_PLAYERSOLID);
	if (tr.ent && tr.ent != g_edicts && tr.ent->plUse)
		return tr.ent;
	return NULL;
}

//==========================================================================================
// command finalization (11)

static void Finalize(Bot *b, edict_t *ent, usercmd_t *cmd)
{
	float mx = BotMax();
	/* up: crouch at a crouch waypoint or under a low ceiling; jump while requested */
	vec3_t a, c;
	DirPoint(ent, b->view_yaw, 50, 0, a);
	DirPoint(ent, b->view_yaw, 50, -23, c);
	vec3_t lo = {ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] - 23};
	bool low_ceiling = BlockedLine(ent, ent->s.origin, a) && !BlockedLine(ent, lo, c);
	if (b->crouch_wp || low_ceiling)
		b->up = -mx;
	else if (level.time < b->jump_until)
		b->up = mx;
	if (b->up != 0) {
		if (level.time < b->up_suppress_until)
			b->up = 0;
		else
			b->up_suppress_until = level.time + 0.2f;
	}
	/* SPEC-AMEND-005 G5.3: the attack bit is pressed only with the flame gun while its primary request is set (and by a
	 * dead bot); every other shot is requested through the fire-time values alone */
	if (b->fire_pri != 0 && BotCurWeapon(ent) == WP_FLAMEGUN)
		b->buttons |= BUTTON_ATTACK;
	/* scaling (1.07f asymmetry: left strafes at full command) */
	float fwd = b->fwd, side = b->side;
	if (fwd >= mx)
		fwd = 0.2f * mx;
	else if (fwd <= -mx)
		fwd = -0.2f * mx;
	if (side >= mx)
		side = 0.2f * mx;
	if (BotMode_Tired(b)) {
		if (fwd)
			fwd = fwd > 0 ? 0.1f * mx : -0.1f * mx;
		if (side)
			side = side > 0 ? 0.1f * mx : -0.1f * mx;
	}
	cmd->msec = 100;
	cmd->buttons = (uint8_t)b->buttons;
	cmd->forwardmove = (short)fwd;
	cmd->sidemove = (short)side;
	cmd->upmove = (short)(b->up > 32767 ? 32767 : b->up < -32768 ? -32768 : b->up);
	cmd->angles[PITCH] = (short)ANGLE2SHORT(b->view_pitch);
	cmd->angles[YAW] = (short)ANGLE2SHORT(b->view_yaw);
	cmd->angles[ROLL] = 0;
	/* the request goes straight into the client's stored pending fire times and into the command's fire-time fields:
	 * 1.0 in the requested mode, 0 otherwise (the fire sequences then start at level time 1.0, SPEC-AMEND-005 G5.3) */
	cmd->firetime = b->fire_pri;
	cmd->altfiretime = b->fire_alt;
	if (ent->client) {
		ent->client->firetime = b->fire_pri;
		ent->client->altfiretime = b->fire_alt;
		for (int i = 0; i < 3; i++)
			ent->client->ps.pmove.delta_angles[i] = 0;
	}
}

//==========================================================================================
// think (5)

/* the last-waypoint record (SPEC-741 5 step 1 with SPEC-AMEND-005 G5.7): every think it moves to the link target of
 * the previous last waypoint that is nearest to the bot, or becomes none when that waypoint has no links */
static void RefreshLastWaypoint(Bot *b, edict_t *ent)
{
	if (b->last_wp < 1 || b->last_wp > g_wps.count)
		return;
	const BotWaypoint &w = g_wps.wp[b->last_wp];
	int best = 0;
	float bd = 0;
	for (int l = 0; l < w.nlinks; l++) {
		int t = BotWp_LinkTarget(w.link[l]);
		if (t < 1 || t > g_wps.count)
			continue;
		float d = Dist(ent->s.origin, g_wps.wp[t].origin);
		if (!best || d < bd) {
			best = t;
			bd = d;
		}
	}
	b->last_wp = best;
}

void Bot_Think(Bot *b, edict_t *ent, usercmd_t *cmd)
{
	memset(cmd, 0, sizeof(*cmd));
	b->fwd = b->side = b->up = 0;
	b->buttons = 0;
	b->crouch_wp = 0;
	b->fire_pri = b->fire_alt = 0;
	if (ent->client)
		b->view_yaw = ent->client->v_angle[YAW];

	if (ent->health <= 0 || ent->deadflag) {
		b->enemy = NULL;
		b->cur_wp = 0;
		b->dest_wp = 0;
		b->item = NULL;
		b->chase_stage = 0;
		if (gix.irand(0, 1))
			b->buttons |= BUTTON_ATTACK;
		Finalize(b, ent, cmd);
		return;
	}
	/* 1 prepare */
	int w = BotCurWeapon(ent);
	if (w != b->last_weapon) {
		b->last_weapon = w;
		b->weapon_hold_until = level.time + 2.0f;
	}
	edict_t *u = UseProbe(ent);
	if (u && u->classname && strstr(u->classname, "door") && level.time - b->door_pause_until > 2.0f) {
		b->door_pause_until = level.time + 1.5f;
		b->buttons |= BUTTON_USE;
	}
	if (ent->health >= 1 && ent->health <= 49)
		RequestItem(b, ent, IT_MEDKIT); /* selected, then used (SPEC-AMEND-005 G5.4) */
	RefreshLastWaypoint(b, ent);
	/* 2 enemy */
	EnemyUpdate(b, ent);
	b->retreating = BotRetreats(b, ent);
	/* 3 chat */
	if (b->chat.pending) {
		if (BotP(b).intelligence > 2 && b->enemy)
			b->chat.pending = false;
		else {
			Finalize(b, ent, cmd); /* typing: stands, view kept */
			return;
		}
	}
	if (!b->enemy)
		BotChat_Idle(b, ent);
	/* 4 fight */
	bool fought = false;
	if (b->enemy && !b->retreating && !b->item) {
		Fight(b, ent);
		fought = true;
	}
	/* 5 chase */
	if (!fought && !b->enemy && Chase(b, ent)) {
		Finalize(b, ent, cmd);
		return;
	}
	if (!fought && !b->enemy && b->chase_stage) {
		Finalize(b, ent, cmd);
		return;
	}
	/* 6 item goal */
	if (!fought && ItemGoal(b, ent)) {
		Finalize(b, ent, cmd);
		return;
	}
	/* 7 waypoints */
	if (!fought && level.time >= b->door_pause_until) {
		if (FollowWaypoints(b, ent)) {
			Finalize(b, ent, cmd);
			return;
		}
	}
	if (!fought && (!b->enemy || b->retreating) && !b->item && !b->cur_wp && g_wps.roam)
		Roam(b, ent);
	/* 8 */
	Finalize(b, ent, cmd);
}
