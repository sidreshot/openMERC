// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: helicopter AI (m_x_chopper_black/_green/_white, m_x_hind).
//
// Spawn set-up and model (§2-§5), HELI command table (§1), per-frame update with the action queue and the
// autonomous selector (§8), flight routines (§9), flight bounds and waypoints (§10), auto-combat (§11), damage,
// crew, crash and death (§12), weapons (§13), use/touch (§14), becoming a Hind (§7), savegame records (§6), and the
// helicopter body parts of SPEC-722 (damage smoke, canopy blood, barrel sequences).
//
// Implements: SPEC-731 Part A, SPEC-722 "Helicopter body", SPEC-711 A8 classes 15-18 (helicopter side)
#include "rg_local.h"

using namespace ghoul;

#define HELI_QUEUE 64
#define HELI_WAYPOINTS 64

struct RgHeli;
typedef void (*heli_routine_t)(edict_t *ent, RgHeli *h);

struct RgHeliAction {
	int id;          // HELI done-event id (0 = internal autonomous action, class 15)
	int cmd;
	int cls;         // SPEC-711 action class 15-18
	vec3_t v;
	edict_t *e;
	float s;
	heli_routine_t routine;
	bool forcedone;  // set at queue time by id 27 for the running action
	bool started;
};

struct RgHeliWaypoint {
	vec3_t origin, mins, maxs;
	bool area;
	bool bounded;
};

struct RgHeli {
	edict_t *ent;
	// command slot (§1)
	vec3_t cv;
	edict_t *ce;
	float cs;
	// action queue
	RgHeliAction queue[HELI_QUEUE];
	int qcount;
	RgHeliAction cur;
	bool hascur;
	int idcounter;
	int curid;
	int lastdone;
	int rememberedq;
	// routines
	heli_routine_t routine, pending;
	bool routinedone, remove;
	edict_t *killer;
	int stage;
	float stagetime;
	// state flags
	bool killed, crashing, gotoflag, landreq, attackflag, hoverflag, autocombat, combatjustended;
	bool rockets_on, gun_on, ffw, freefire, navbox;
	int rockets;
	float nextrocket;
	float aggression;
	vec3_t home, exitpt, goal;
	bool crewkills;
	int sniperleft;
	int crewreacted;
	// leg / strafe
	vec3_t p0, p1, mid;
	float v;
	bool exitleg;
	float legstartyaw;
	int strafesign;
	// turn and bank
	vec3_t facepoint;
	bool bank;
	float turnstartyaw;
	// look / aim routines
	vec3_t lookpoint;
	// attack
	vec3_t attackpt, gunaimpt;
	float attacktime, attackend;
	float attackheight;
	bool hoverfiring;
	float dippitch;
	int dipphase;
	// hover
	float hoverend;
	bool hoverengage, hoverwaiting;
	// combat
	int pattern, prevpattern, pstep;
	edict_t *target;
	float targettime;
	float acend;
	int parea;
	float pstrafe;
	vec3_t pfacepoint;
	// crash
	float crashstart;
	// body parts
	IGhoulInst *rotormain, *rotortail, *gunnull, *barrel, *headgunner, *headpilot, *canopy;
	float rotorseqend;
	RgAimPart aimpilot, aimgunhead, aimgunbase, aimbarrel;
	bool barrelspin;
	bool smoke;
	bool hind;
	// waypoints
	RgHeliWaypoint wp[HELI_WAYPOINTS];
	int nwp, npoint, narea;
};

// the helicopter AI object (SPEC-700 type 8), made by the factory registered with the AI framework
class RgHeliAI : public OmAI {
public:
	RgHeli h;
	RgHeliAI() { memset(&h, 0, sizeof(h)); }
	void Think(edict_t *ent) override;
	int Damage(G3Hit &hit) override;
};

om_ai::AI *rg_MakeHeliAI(int code)
{
	(void)code;
	return new RgHeliAI();
}

// globals of §9.10 / §10 / §12.4: flight bounds (not reset), wander point, crash-stage timer
static vec3_t rg_heli_bmin = {-10000, -10000, -10000}, rg_heli_bmax = {10000, 10000, 10000};
static vec3_t rg_heli_wander;
static float rg_heli_stagetime;
static int rg_heli_stage;

static const float HELI_YAWRATE = 9.0f;

void rg_HeliLevelReset(void)
{
	// SPEC-731 §10: the flight bounds are deliberately not reset; the stage timer is a plain global
	rg_heli_stagetime = 0;
	rg_heli_stage = 0;
}

static RgHeli *HELI(edict_t *ent)
{
	if (!ent || !ent->inuse || !ent->ai || ent->ai->type_code != om_ai::TYPE_HELICOPTER)
		return NULL;
	return &static_cast<RgHeliAI *>(ent->ai)->h;
}

static float frand(float a, float b) { return gix.flrand(a, b); }
static int irand(int a, int b) { return gix.irand(a, b); }
static float hdist(const vec3_t a, const vec3_t b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return sqrtf(dx * dx + dy * dy);
}
static float approach(float cur, float target, float rate, bool *reached)
{
	float d = target - cur;
	if (fabsf(d) <= rate) {
		if (reached)
			*reached = true;
		return target;
	}
	if (reached)
		*reached = false;
	return cur + (d > 0 ? rate : -rate);
}
static float norm180(float a)
{
	a = anglemod(a);
	if (a > 180)
		a -= 360;
	return a;
}

// ---------------------------------------------------------------------------------------------------------------
// body helpers (SPEC-722 helicopter body)
// ---------------------------------------------------------------------------------------------------------------
static void heli_Smoke(edict_t *ent, RgHeli *h, float hp)
{
	(void)h;
	Body_HeliSmoke(ent, hp);
}

static void heli_BarrelSpin(RgHeli *h, bool on)
{
	if (!h->barrel || on == h->barrelspin)
		return;
	h->barrelspin = on;
	Body_HeliBarrel(h->ent, h->barrel, on);
}

// ---------------------------------------------------------------------------------------------------------------
// traces (§10)
// ---------------------------------------------------------------------------------------------------------------
static trace_t heli_Trace(edict_t *ent, RgHeli *h, const vec3_t a, const vec3_t b)
{
	vec3_t mn = {0, 0, 0}, mx = {0, 0, 0};
	if (h->navbox) {
		float r = fmaxf(fabsf(fminf(ent->mins[0], ent->mins[1])), fmaxf(ent->maxs[0], ent->maxs[1]));
		float hh = 2 * fmaxf(fabsf(ent->mins[2]), ent->maxs[2]);
		VectorSet(mn, -r, -r, -hh);
		VectorSet(mx, r, r, hh);
	}
	return gi.trace(a, mn, mx, b, ent, 3);
}

static bool heli_InBounds(const vec3_t p)
{
	for (int i = 0; i < 3; i++)
		if (p[i] < rg_heli_bmin[i] || p[i] > rg_heli_bmax[i])
			return false;
	return true;
}

// §9.11 clear spot
static void heli_ClearSpot(edict_t *ent, RgHeli *h, const vec3_t goal, vec3_t out)
{
	VectorCopy(goal, out);
	trace_t tr = heli_Trace(ent, h, goal, ent->s.origin);
	if (!tr.startsolid)
		return;
	static const float dirs[18][3] = {{0, 0, 1},  {-1, 1, 0}, {0, 1, 0},  {1, 1, 0},   {-1, 0, 0}, {1, 0, 0},
	                                  {-1, -1, 0}, {0, -1, 0}, {1, -1, 0}, {0, 0, -1}, {-1, 1, 1}, {0, 1, 1},
	                                  {1, 1, 1},   {-1, 0, 1}, {1, 0, 1},  {-1, -1, 1}, {0, -1, 1}, {1, -1, 1}};
	vec3_t centre;
	VectorCopy(goal, centre);
	float r = 100;
	int tries = 0;
	for (int round = 0; round < 50; round++, r += 100) {
		for (int k = 0; k < 18; k++) {
			if (tries == 100) {
				VectorCopy(ent->s.origin, centre);
				r = 100;
			}
			tries++;
			vec3_t c;
			VectorMA(centre, r, dirs[k], c);
			if (!heli_InBounds(c))
				continue;
			trace_t t1 = heli_Trace(ent, h, c, ent->s.origin);
			if (t1.startsolid)
				continue;
			trace_t t2 = heli_Trace(ent, h, ent->s.origin, c);
			if (t2.fraction <= 0)
				continue;
			VectorCopy(c, out);
			return;
		}
	}
}

// §9.10 random wander point (one shared point)
static void heli_Wander(edict_t *ent, RgHeli *h, vec3_t out)
{
	vec3_t p;
	VectorSet(p, ent->s.origin[0] + frand(-2500, 2500), ent->s.origin[1] + frand(-2500, 2500),
	          ent->s.origin[2] + frand(-250, 250));
	for (int i = 0; i < 3; i++)
		p[i] = fmaxf(rg_heli_bmin[i], fminf(rg_heli_bmax[i], p[i]));
	if (p[2] < rg_heli_bmin[2] + 112)
		p[2] = rg_heli_bmin[2] + 112;
	trace_t tr = heli_Trace(ent, h, ent->s.origin, p);
	VectorCopy(tr.endpos, rg_heli_wander);
	VectorCopy(rg_heli_wander, out);
}

// ---------------------------------------------------------------------------------------------------------------
// crew look and gun aim (§9.9)
// ---------------------------------------------------------------------------------------------------------------
// aim records (SPEC-AMEND-006 G4.10): pilot and gunner head yaw -1 ... 1 rad, pitch axis off; gun base yaw -1 ... 1;
// barrel pitch -0.7854 ... 0.7854; maximum step 0.314159 rad (the 18-degree steps seen on trn1)
static bool heli_PilotLook(edict_t *ent, RgHeli *h, const vec3_t p)
{
	return rg_AimStep(ent, &h->aimpilot, p, true, false, -1, 1, -0.7854f, 0.7854f, 0.314159f) == RG_AIM_ALIGNED;
}

// returns the result of the base/barrel aim (1 aligned, 2 turning, 3 limited)
static int heli_GunAim(edict_t *ent, RgHeli *h, const vec3_t p)
{
	// the gunner's head is turned the same way as the pilot's, before the gun
	rg_AimStep(ent, &h->aimgunhead, p, true, false, -1, 1, -0.7854f, 0.7854f, 0.314159f);
	int a = rg_AimStep(ent, &h->aimgunbase, p, true, false, -1, 1, 0, 0, 0.314159f);
	int b = rg_AimStep(ent, &h->aimbarrel, p, false, true, 0, 0, -0.7854f, 0.7854f, 0.314159f);
	if (a == RG_AIM_LIMITED || b == RG_AIM_LIMITED)
		return RG_AIM_LIMITED;
	if (a == RG_AIM_TURNING || b == RG_AIM_TURNING)
		return RG_AIM_TURNING;
	return RG_AIM_ALIGNED;
}

// a point ahead of the helicopter along the forward vector of its full angles (pitch included; SPEC-AMEND-006 G4.9)
static void heli_Ahead(edict_t *ent, float dist, vec3_t out)
{
	vec3_t f;
	AngleVectors(ent->s.angles, f, NULL, NULL);
	VectorMA(ent->s.origin, dist, f, out);
}

// ---------------------------------------------------------------------------------------------------------------
// weapons (§13)
// ---------------------------------------------------------------------------------------------------------------
static void heli_FireRound(edict_t *ent, RgHeli *h, bool freefire)
{
	if (!h->barrel)
		return;
	heli_BarrelSpin(h, true);
	vec3_t pos, fwd;
	if (!rg_MuzzleWorld(ent, h->barrel, "flash_barrel", pos, fwd))
		return;
	rg_EffectOnBolt("weapons/othermz/heli", ent, h->barrel, "flash_barrel");
	vec3_t dir;
	VectorScale(fwd, 100, dir);
	if (freefire) {
		dir[0] += irand(-10, 10);
		dir[1] += irand(-10, 10);
		irand(-10, 10); // drawn and discarded
	} else if (h->cs != 1337) {
		for (int i = 0; i < 3; i++)
			dir[i] += irand(-10, 10);
	}
	VectorNormalize(dir);
	if (rg_cvar("rg_trace") >= 3)
		rg_Trace("rg: heli %d round from (%.0f %.0f %.0f) dir (%.2f %.2f %.2f)\n", G_EdictNum(ent), pos[0], pos[1],
		         pos[2], dir[0], dir[1], dir[2]);
	rg_FireAttack(28, ent, pos, dir);
	gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/assault/fire.wav"), 0.9f, 0.5f, 0);
}

static edict_t *heli_ChooseTarget(RgHeli *h)
{
	bool valid = h->target && h->target->inuse && h->target->takedamage;
	if (valid && level.time < h->targettime)
		return h->target;
	h->target = NULL;
	for (int i = 1; i <= game.maxclients && i <= 32; i++) {
		edict_t *c = &g_edicts[i];
		if (c->inuse && c->takedamage) {
			h->target = c;
			break;
		}
	}
	h->targettime = level.time + 30;
	return h->target;
}

static void heli_RocketTouch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	if (other == self->owner)
		return;
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: heli rocket %d touches %s at (%.0f %.0f %.0f)%s\n", G_EdictNum(self),
		         other && other->classname ? other->classname : "-", self->s.origin[0], self->s.origin[1],
		         self->s.origin[2], surf && (surf->flags & 4) ? " (sky)" : "");
	if (surf && (surf->flags & 4)) {
		G_FreeEdict(self);
		return;
	}
	T_RadiusDamage(self, self->owner, 130, self, 150, 0);
	om_ai::ReportNoise(self->s.origin, 150, 250);
	rg_EffectAt("weapons/world/rocketexplode", self->s.origin);
	rg_CameraShake(self->s.origin, 100, 300);
	G_FreeEdict(self);
}

static void heli_RocketThink(edict_t *self) { G_FreeEdict(self); }

static void heli_FireRocket(edict_t *ent, RgHeli *h)
{
	if (h->rockets <= 0)
		return;
	int n = h->rockets;
	const char *pod = (n & 1) ? "FLASH_ROCKET1" : "FLASH_ROCKET2";
	int tube = (n + 1) / 2;
	vec3_t pos, f, r, u;
	if (!rg_BoltWorld(ent, ent->ghoulInst, pod, pos))
		VectorCopy(ent->s.origin, pos);
	AngleVectors(ent->s.angles, f, r, u);
	float sr = (tube == 1 || tube == 2) ? 5.0f : -5.0f;
	float su = (tube == 1 || tube == 4) ? 5.0f : -5.0f;
	VectorMA(pos, sr, r, pos);
	VectorMA(pos, su, u, pos);
	edict_t *t = heli_ChooseTarget(h);
	vec3_t aim;
	if (t)
		VectorSubtract(t->s.origin, pos, aim);
	else
		VectorCopy(f, aim);
	VectorNormalize(aim);
	edict_t *rk = G_Spawn();
	rk->classname = (char *)"heli_rocket";
	VectorCopy(pos, rk->s.origin);
	vectoangles(aim, rk->s.angles);
	VectorScale(aim, 600, rk->velocity);
	rk->movetype = MOVETYPE_DAN;
	rk->gravity = 0; // UNVERIFIED: the spec names no gravity for helicopter rockets
	rk->solid = SOLID_BBOX;
	VectorSet(rk->mins, -4, -2, -4);
	VectorSet(rk->maxs, 3, 2, 4);
	rk->clipmask = 0x6000081;
	rk->health = 100;
	rk->owner = ent;
	rk->airfriction = 0.02f;
	rk->touch = heli_RocketTouch;
	rk->think = heli_RocketThink;
	rk->nextthink = level.time + 10;
	if (!GG_SetSimpleModel(rk, "Weapon/Projectiles/Rocket", "Rocket") && rg_cvar("rg_trace"))
		rg_Trace("rg: heli rocket %d: no Ghoul model\n", G_EdictNum(rk));
	rg_SetEffectSlot(rk, "weapons/world/rockettrail2", NULL, 0);
	gi.linkentity(rk);
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: heli %d rocket %d from %s at (%.0f %.0f %.0f) towards %s\n", G_EdictNum(ent), n, pod, pos[0],
		         pos[1], pos[2], t && t->classname ? t->classname : "-");
	h->rockets--;
	if (h->rockets <= 0 && (ent->spawnflags & 4))
		h->rockets = 8;
}

// normal rate: one rocket per second while rockets are on
static int heli_Rockets(edict_t *ent, RgHeli *h)
{
	if (!h->rockets_on || h->rockets <= 0 || level.time <= h->nextrocket)
		return 0;
	heli_FireRocket(ent, h);
	h->nextrocket = level.time + 1;
	return 1;
}

static void heli_Volley(edict_t *ent, RgHeli *h, int n)
{
	for (int i = 0; i < n && h->rockets > 0; i++)
		heli_FireRocket(ent, h);
}

// ---------------------------------------------------------------------------------------------------------------
// turning (§9.3) and levelling (§9.8)
// ---------------------------------------------------------------------------------------------------------------
// SPEC-731 A §9.3 as amended by SPEC-AMEND-006 G4.2: anglemod with 1/65536-turn truncation (SPEC-731 header; note
// that SPEC-760 B3.6a describes the shared anglemod without quantisation, OPEN_QUESTIONS G4 11)
static float heli_anglemod16(float a)
{
	return (float)((360.0 / 65536) * ((int)(a * (65536 / 360.0)) & 65535));
}

// one turn step towards p; returns the absolute yaw error before the step (0 = facing)
static float heli_TurnStep(edict_t *ent, RgHeli *h, const vec3_t p, bool bank)
{
	if (p[0] == ent->s.origin[0] && p[1] == ent->s.origin[1])
		return 0;
	vec3_t dv;
	VectorSubtract(p, ent->s.origin, dv);
	// target: the continuous direction in (-180, 180], not rounded (vectoangles, SPEC-760 B3.6)
	const float want = (float)(atan2(dv[1], dv[0]) * 180 / M_PI);
	if (ent->s.angles[YAW] > 180)
		ent->s.angles[YAW] -= 360;
	const float cur = ent->s.angles[YAW];
	const float d = want - cur; // not wrapped
	// |d| <= 9: snap, facing at once, no bank change (G4.2 rule 4). Measured on trn1, the original's roll changes by
	// less than 2.8 degrees in the frame after the snap, where the arrive routine's 4-degree level-out gives more
	// (OPEN_QUESTIONS G4 12)
	if (fabsf(d) <= HELI_YAWRATE) {
		ent->s.angles[YAW] = want; // may stay negative
		return 0;
	}
	const float res = fabsf(d);
	ent->s.angles[YAW] = heli_anglemod16(heli_anglemod16(d) < 180 ? cur + HELI_YAWRATE : cur - HELI_YAWRATE);
	if (bank) {
		float A = fabsf(norm180(want - h->turnstartyaw));
		float half = A / 2;
		float B = fabsf(norm180(ent->s.angles[YAW] - h->turnstartyaw));
		float p100 = half > 0 ? fabsf(B - half) * 100 / half : 0;
		float sign = norm180(want - h->turnstartyaw) > 0 ? 1.0f : -1.0f; // towards increasing yaw
		float tgt = B < half ? -sign * 0.4f * p100 : sign * 0.16f * p100;
		ent->s.angles[ROLL] = approach(norm180(ent->s.angles[ROLL]), tgt, 2.0f, NULL);
		if (h->ffw) {
			if (p100 < 90) {
				if (h->gun_on)
					heli_FireRound(ent, h, false);
				heli_Rockets(ent, h);
			} else
				heli_BarrelSpin(h, false);
		}
	}
	return res;
}

static bool heli_LevelOutStep(edict_t *ent, RgHeli *h)
{
	bool rollzero = norm180(ent->s.angles[ROLL]) == 0;
	ent->s.angles[ROLL] = approach(norm180(ent->s.angles[ROLL]), 0, 4.0f, NULL);
	vec3_t a;
	heli_Ahead(ent, 100, a);
	heli_PilotLook(ent, h, a);
	if (h->ffw && h->gun_on) {
		heli_Ahead(ent, 500, a);
		heli_GunAim(ent, h, a);
	}
	bool pz = false;
	ent->s.angles[PITCH] = approach(norm180(ent->s.angles[PITCH]), 0, 1.5f, &pz);
	return rollzero && pz;
}

static void R_Arrive(edict_t *ent, RgHeli *h)
{
	if (h->ffw && h->gun_on)
		heli_BarrelSpin(h, false);
	if (heli_LevelOutStep(ent, h))
		h->routinedone = true;
}

static void R_Turn(edict_t *ent, RgHeli *h)
{
	vec3_t a;
	if (h->attackflag)
		heli_GunAim(ent, h, h->gunaimpt);
	else {
		heli_Ahead(ent, 500, a);
		heli_GunAim(ent, h, a);
	}
	heli_PilotLook(ent, h, h->facepoint);
	if (heli_TurnStep(ent, h, h->facepoint, h->bank) == 0)
		h->pending = R_Arrive;
}

static void heli_StartTurn(RgHeli *h, edict_t *ent, const vec3_t p, bool bank)
{
	VectorCopy(p, h->facepoint);
	h->bank = bank;
	h->turnstartyaw = anglemod(ent->s.angles[YAW]);
}

// ---------------------------------------------------------------------------------------------------------------
// flight (§9.2 legs, §9.4 go to, §9.5 strafe, §9.1 take off, §9.6 land, §9.7 hover)
// ---------------------------------------------------------------------------------------------------------------
static void R_Leg(edict_t *ent, RgHeli *h);

static void heli_StartLeg(edict_t *ent, RgHeli *h, const vec3_t dest, bool exitleg)
{
	VectorCopy(ent->s.origin, h->p0);
	VectorCopy(dest, h->p1);
	for (int i = 0; i < 3; i++)
		h->mid[i] = (h->p0[i] + h->p1[i]) * 0.5f;
	h->v = 0;
	h->exitleg = exitleg;
	h->legstartyaw = anglemod(ent->s.angles[YAW]);
	h->pending = R_Leg;
}

static void R_Leg(edict_t *ent, RgHeli *h)
{
	vec3_t d, a;
	VectorSubtract(h->p1, h->p0, d);
	VectorNormalize(d);
	float sz = VectorLength(ent->size);
	VectorMA(h->p1, sz, d, a);
	heli_PilotLook(ent, h, a);
	bool firing = h->ffw && h->gun_on;
	if (h->attackflag)
		heli_GunAim(ent, h, h->gunaimpt);
	else if (!firing) {
		heli_Ahead(ent, 500, a);
		heli_GunAim(ent, h, a);
	} else {
		vec3_t f, t;
		AngleVectors(ent->s.angles, f, NULL, NULL);
		VectorSet(t, f[0], f[1], -1);
		VectorNormalize(t);
		VectorMA(ent->s.origin, 500, t, a);
		heli_GunAim(ent, h, a);
	}
	// arrival
	bool arrived = true;
	for (int i = 0; i < 3; i++)
		if (fabsf(ent->s.origin[i] - h->p1[i]) > h->v)
			arrived = false;
	if (arrived) {
		VectorCopy(h->p1, ent->s.origin);
		h->v = 0;
		if (h->exitleg) {
			h->remove = true;
			h->routinedone = true;
		} else
			h->pending = R_Arrive;
		return;
	}
	bool past = true;
	for (int i = 0; i < 3; i++)
		if (fabsf(h->mid[i] - h->p1[i]) > 1) {
			float s1 = h->p1[i] - h->mid[i], s2 = ent->s.origin[i] - h->mid[i];
			if ((s1 > 0 && s2 < 0) || (s1 < 0 && s2 > 0) || s2 == 0)
				past = false;
		}
	if (!past || h->exitleg) {
		h->v += 8.0f / 3.0f;
		// the pitch step only when both horizontal lengths of q are > 0; otherwise the pitch is left as it is
		// (SPEC-AMEND-006 G4.8: a vertical leg such as the kos2 take-off climbs with the pitch unchanged)
		// q = 1 - |M - origin| / |M - P0| (horizontal): the two lengths G4.8 names
		const float den = hdist(h->mid, h->p0), num = hdist(h->mid, ent->s.origin);
		if (den > 0 && num > 0)
			ent->s.angles[PITCH] = approach(norm180(ent->s.angles[PITCH]), 45 * (num / den), 1.5f, NULL);
	} else {
		h->v -= 8.0f / 3.0f;
		if (h->v < 2)
			h->v = 2;
		const float den = hdist(h->p1, h->mid), num = hdist(h->p1, ent->s.origin);
		if (den > 0 && num > 0) {
			const float r = num / den;
			const float tgt = h->v < 4 ? 0 : -15 * (1 - r);
			ent->s.angles[PITCH] = approach(norm180(ent->s.angles[PITCH]), tgt, 0.75f, NULL);
		}
	}
	VectorSubtract(h->p1, ent->s.origin, d);
	VectorNormalize(d);
	float step = h->exitleg ? h->v : fminf(h->v, 80.0f);
	VectorMA(ent->s.origin, step, d, ent->s.origin);
	float err = heli_TurnStep(ent, h, h->p1, false);
	if (h->ffw) {
		if (err >= 20 || h->v <= 8)
			heli_BarrelSpin(h, false);
		else {
			if (h->gun_on)
				heli_FireRound(ent, h, false);
			if (ent->s.angles[PITCH] > 0)
				heli_Rockets(ent, h);
		}
	}
}

static bool heli_Clear(edict_t *ent, RgHeli *h, const vec3_t a, const vec3_t b)
{
	// "clear" = the trace covers the whole way (a box that starts touching geometry is not refused: the original
	// leaves such a position, trn1 test in docs/impl/G4.md)
	trace_t tr = heli_Trace(ent, h, a, b);
	return tr.fraction >= 1.0f;
}

static void R_Goto(edict_t *ent, RgHeli *h)
{
	vec3_t G;
	VectorCopy(h->goal, G);
	if (VectorCompare(ent->s.origin, G)) {
		// G4.6 rule 7 (measured on trn1: the original's next scripted command acts one frame later, A + 4;
		// OPEN_QUESTIONS G4 12)
		h->gotoflag = false;
		h->routinedone = true;
		return;
	}
	if (heli_Clear(ent, h, ent->s.origin, G)) {
		bool ex = !VectorCompare(h->exitpt, vec3_origin) && VectorCompare(G, h->exitpt);
		heli_StartLeg(ent, h, G, ex);
		return;
	}
	float C = rg_heli_bmin[2] + 0.75f * (rg_heli_bmax[2] - rg_heli_bmin[2]);
	vec3_t cand[5];
	int nc = 0;
	trace_t fw = heli_Trace(ent, h, ent->s.origin, G);
	trace_t bw = heli_Trace(ent, h, G, ent->s.origin);
	if (bw.fraction < 1.0f) {
		VectorSet(cand[nc], bw.endpos[0], bw.endpos[1], C);
		nc++;
	}
	vec3_t near;
	VectorSet(near, fw.endpos[0], fw.endpos[1], C);
	if (VectorCompare(near, ent->s.origin)) {
		if (irand(0, 1) == 0) {
			h->gotoflag = false;
			h->routinedone = true;
			return;
		}
		heli_Wander(ent, h, cand[nc]);
		nc++;
	} else {
		VectorCopy(near, cand[nc]);
		nc++;
	}
	VectorSet(cand[nc], ent->s.origin[0], ent->s.origin[1], C);
	nc++;
	heli_Wander(ent, h, cand[nc]);
	nc++;
	for (int i = 0; i < nc; i++)
		if (heli_Clear(ent, h, ent->s.origin, cand[i])) {
			heli_StartLeg(ent, h, cand[i], false);
			return;
		}
	h->routinedone = true; // nowhere to go this frame; the go-to flag keeps the goal
}

// the go-to commands' own routine (SPEC-AMEND-006 G4.6 rule 2): frame 1 only installs the go-to routine as pending;
// the go-to routine chooses the leg in frame 2 and the leg moves from frame 3
static void R_GotoCmd(edict_t *ent, RgHeli *h)
{
	(void)ent;
	h->pending = R_Goto;
}

static void heli_SetGoto(edict_t *ent, RgHeli *h, const vec3_t g)
{
	heli_ClearSpot(ent, h, g, h->goal);
	h->gotoflag = true;
	h->routine = R_GotoCmd;
}

// a go-to started by a running routine (combat step, interjection): the go-to routine is pending and runs from the
// next frame (G4.6 rule 1)
static void heli_RoutineGoto(edict_t *ent, RgHeli *h, const vec3_t g)
{
	heli_ClearSpot(ent, h, g, h->goal);
	h->gotoflag = true;
	h->pending = R_Goto;
}

static void R_Strafe(edict_t *ent, RgHeli *h)
{
	vec3_t a;
	heli_PilotLook(ent, h, h->p1);
	heli_Ahead(ent, 500, a);
	heli_GunAim(ent, h, a);
	bool arrived = true;
	for (int i = 0; i < 3; i++)
		if (fabsf(ent->s.origin[i] - h->p1[i]) > h->v)
			arrived = false;
	if (arrived) {
		// no snap to the destination: the original's strafe on trn1 (2092 units) stops where this test first
		// holds, 52 units short of the destination (legs, by contrast, end exactly on theirs)
		h->v = 0;
		heli_BarrelSpin(h, false);
		h->pending = R_Arrive;
		return;
	}
	bool past = true;
	for (int i = 0; i < 3; i++)
		if (fabsf(h->mid[i] - h->p1[i]) > 1) {
			float s1 = h->p1[i] - h->mid[i], s2 = ent->s.origin[i] - h->mid[i];
			if ((s1 > 0 && s2 < 0) || (s1 < 0 && s2 > 0) || s2 == 0)
				past = false;
		}
	float tgt;
	if (!past) {
		h->v += 16.0f / 3.0f;
		float den = hdist(h->mid, h->p0);
		float q = den > 0.001f ? fminf(1, hdist(ent->s.origin, h->p0) / den) : 0;
		tgt = h->strafesign * 40 * (1 - q);
	} else {
		h->v -= 4;
		if (h->v < 2)
			h->v = 2;
		float den = hdist(h->p1, h->mid);
		float r = den > 0.001f ? hdist(h->p1, ent->s.origin) / den : 0;
		tgt = -h->strafesign * 16 * (1 - r);
	}
	ent->s.angles[ROLL] = approach(norm180(ent->s.angles[ROLL]), tgt, 3.5f, NULL);
	vec3_t d;
	VectorSubtract(h->p1, ent->s.origin, d);
	VectorNormalize(d);
	VectorMA(ent->s.origin, fminf(h->v, 80.0f), d, ent->s.origin);
	// the gun fires only while v > 20: on trn1 the original's 33-frame strafe fires from its 4th frame (v 21.3) to the
	// arrival, 30 rounds (OPEN_QUESTIONS G4 13; §9.5 says "every frame")
	if (h->v <= 20)
		heli_BarrelSpin(h, false);
	else if (h->gun_on)
		heli_FireRound(ent, h, false);
	heli_Rockets(ent, h);
}

static bool heli_StartStrafe(edict_t *ent, RgHeli *h, float dist)
{
	vec3_t f, r, dest;
	AngleVectors(ent->s.angles, f, r, NULL);
	float d = dist;
	VectorMA(ent->s.origin, d, r, dest);
	int n = 0;
	while (!heli_InBounds(dest) && n < 10) {
		d *= 0.5f;
		VectorMA(ent->s.origin, d, r, dest);
		n++;
	}
	if (!heli_InBounds(dest))
		return false;
	trace_t tr = heli_Trace(ent, h, ent->s.origin, dest);
	VectorCopy(ent->s.origin, h->p0);
	VectorCopy(tr.endpos, h->p1);
	for (int i = 0; i < 3; i++)
		h->mid[i] = (h->p0[i] + h->p1[i]) * 0.5f;
	h->v = 0;
	h->strafesign = d >= 0 ? 1 : -1;
	h->routine = R_Strafe;
	return true;
}

static float heli_RotorDuration(RgHeli *h, const char *seq, int mode)
{
	return h->rotormain ? rg_SeqDuration(h->rotormain, seq, mode) : 0;
}

static void heli_Rotors(RgHeli *h, const char *seq, int mode)
{
	if (h->rotormain)
		rg_Play(h->rotormain, seq, mode);
	if (h->rotortail)
		rg_Play(h->rotortail, seq, mode);
}

static void R_TakeOff(edict_t *ent, RgHeli *h)
{
	if (ent->spawnflags & 1) {
		h->routinedone = true;
		return;
	}
	if (h->stage == 0) {
		heli_Rotors(h, "rotor_start", kPlayOnce);
		h->rotorseqend = level.time + heli_RotorDuration(h, "rotor_start", kPlayOnce);
		h->stage = 1;
		return;
	}
	if (h->stage == 1) {
		if (level.time < h->rotorseqend)
			return;
		heli_Rotors(h, "rotor_anim", kPlayLoop);
		vec3_t up;
		VectorCopy(ent->s.origin, up);
		up[2] += 224;
		h->stage = 2;
		heli_StartLeg(ent, h, up, false);
	}
}

static void R_Finish(edict_t *ent, RgHeli *h)
{
	(void)ent;
	if (h->killed)
		h->remove = true;
	h->routinedone = true;
}

static void R_Land(edict_t *ent, RgHeli *h)
{
	if (h->stage == 0) {
		if (heli_LevelOutStep(ent, h))
			h->stage = 1;
		return;
	}
	if (h->stage == 1) {
		float hgt = ent->s.origin[2] - h->home[2];
		float s = fmaxf(2.0f, hgt * 0.0446428571f);
		if (hgt < s) {
			VectorCopy(h->home, ent->s.origin);
			heli_Rotors(h, "rotor_end", kPlayOnce);
			h->rotorseqend = level.time + heli_RotorDuration(h, "rotor_end", kPlayOnce);
			h->stage = 2;
		} else
			ent->s.origin[2] -= s;
		return;
	}
	if (h->stage == 2 && level.time >= h->rotorseqend) {
		heli_Rotors(h, "rotor", kPlayOnce);
		h->stage = 3;
		h->pending = R_Finish;
	}
}

static void R_LevelFinish(edict_t *ent, RgHeli *h)
{
	if (heli_LevelOutStep(ent, h))
		R_Arrive(ent, h);
}

static void R_Attack(edict_t *ent, RgHeli *h);

static void R_Hover(edict_t *ent, RgHeli *h)
{
	if (h->hoverwaiting)
		return; // the fuselage end-of-sequence notice is never connected (1.07f): a non-engaging hover never ends
	if (level.time > h->hoverend || h->killed || h->combatjustended) {
		h->hoverflag = false;
		h->pending = R_LevelFinish;
		return;
	}
	if (!h->hoverengage) {
		if (ent->ghoulInst)
			rg_Play(ent->ghoulInst, "cobrass", kPlayOnce);
		h->hoverwaiting = true;
		return;
	}
	edict_t *t = heli_ChooseTarget(h);
	if ((h->rockets_on || h->gun_on) && t) {
		vec3_t eye, d, f;
		VectorCopy(t->s.origin, eye);
		eye[2] += t->viewheight;
		trace_t tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin, eye, ent, 0x19);
		VectorSubtract(eye, ent->s.origin, d);
		VectorNormalize(d);
		AngleVectors(ent->s.angles, f, NULL, NULL);
		bool cone = h->autocombat || DotProduct(d, f) >= 0.173f;
		if ((tr.fraction >= 1.0f || tr.ent == t) && cone) {
			VectorCopy(t->s.origin, h->attackpt);
			VectorCopy(t->s.origin, h->gunaimpt);
			h->attacktime = 1;
			if (h->rockets_on && h->rockets > 0 && (h->hoverflag || h->autocombat))
				h->attacktime = irand(0, 10) < 5 ? -1.0f : -2.0f;
			h->attackflag = true;
			h->hoverfiring = false;
			h->pending = R_Attack;
			return;
		}
	}
	// look-around turn: the turn routine banks (§9.8; the original's look-around turns in place on jpn3 roll up to
	// about 11 degrees)
	vec3_t p;
	VectorSet(p, ent->s.origin[0] + frand(-1000, 1000), ent->s.origin[1] + frand(-1000, 1000), ent->s.origin[2]);
	heli_StartTurn(h, ent, p, true);
	h->pending = R_Turn;
}

static void heli_StartHover(RgHeli *h, float secs, bool engage)
{
	h->hoverflag = true;
	h->hoverengage = engage;
	h->hoverwaiting = false;
	h->hoverend = level.time + secs;
	h->routine = R_Hover;
}

// ---------------------------------------------------------------------------------------------------------------
// attack routines (§13.1, §13.5)
// ---------------------------------------------------------------------------------------------------------------
static void R_HoverFire(edict_t *ent, RgHeli *h)
{
	heli_TurnStep(ent, h, h->attackpt, false);
	heli_PilotLook(ent, h, h->attackpt);
	heli_GunAim(ent, h, h->gunaimpt);
	if (h->gun_on)
		heli_FireRound(ent, h, false);
	int fired = heli_Rockets(ent, h);
	bool end = h->killed;
	if (h->attacktime < 0) {
		h->attacktime += fired;
		if (h->rockets <= 0 || h->attacktime >= 0)
			end = true;
	} else if (level.time > h->attackend)
		end = true;
	if (end) {
		heli_BarrelSpin(h, false);
		h->routinedone = true;
	}
}

static void R_Dip(edict_t *ent, RgHeli *h)
{
	heli_PilotLook(ent, h, h->attackpt);
	heli_GunAim(ent, h, h->attackpt);
	if (h->dipphase <= 2 && h->gun_on)
		heli_FireRound(ent, h, false);
	bool reached = false;
	if (h->dipphase == 1) {
		ent->s.angles[PITCH] = approach(norm180(ent->s.angles[PITCH]), h->dippitch, 5.0f, &reached);
		if (reached) {
			heli_Volley(ent, h, (int)(-h->attacktime));
			h->dippitch = -0.5f * h->dippitch;
			h->dipphase = 2;
		}
	} else if (h->dipphase == 2) {
		ent->s.angles[PITCH] = approach(norm180(ent->s.angles[PITCH]), h->dippitch, 5.0f, &reached);
		if (reached) {
			h->dipphase = 3;
			heli_BarrelSpin(h, false);
		}
	} else
		h->pending = R_Arrive;
}

static void R_Attack(edict_t *ent, RgHeli *h)
{
	bool rocket = h->attacktime < 0 || (h->rockets_on && h->rockets > 0);
	if (rg_cvar("rg_trace") >= 2)
		rg_Trace("rg: heli %d attack at (%.0f %.0f %.0f) t %.1f rocket %d rockets %d\n", G_EdictNum(ent), h->attackpt[0],
		         h->attackpt[1], h->attackpt[2], h->attacktime, rocket, h->rockets);
	if (rocket) {
		// "while not facing, the turn routine runs": the original's turn towards the attack point banks (roll to
		// about 11 degrees) and levels out at 4 degrees/frame before the dip (jpn3 combat, OPEN_QUESTIONS G4 16), so
		// the turn routine (bank, then arrive) is used; the selector returns to this routine afterwards
		vec3_t dv;
		VectorSubtract(h->attackpt, ent->s.origin, dv);
		const float want = (float)(atan2(dv[1], dv[0]) * 180 / M_PI);
		if ((dv[0] != 0 || dv[1] != 0) && fabsf(norm180(want - ent->s.angles[YAW])) > HELI_YAWRATE) {
			heli_StartTurn(h, ent, h->attackpt, true);
			h->pending = R_Turn;
			return;
		}
		if (heli_TurnStep(ent, h, h->attackpt, false) != 0)
			return;
		vec3_t d;
		VectorSubtract(h->attackpt, ent->s.origin, d);
		float hz = sqrtf(d[0] * d[0] + d[1] * d[1]);
		float e = (float)(atan2f(fabsf(d[2]), hz) * 180 / M_PI);
		if (d[2] > 0)
			e = -e; // positive when the point is below
		if (h->attacktime < 0 && fabsf(e) < 40) {
			h->dippitch = e * 0.1f;
			h->dipphase = 1;
			h->attackflag = false;
			h->pending = R_Dip;
			return;
		}
		if (fabsf(ent->s.origin[2] - h->attackpt[2]) > 0.5f) {
			vec3_t dest = {ent->s.origin[0], ent->s.origin[1], h->attackpt[2]};
			trace_t tr = heli_Trace(ent, h, ent->s.origin, dest);
			if (tr.fraction < 1.0f) {
				vec3_t d2 = {dest[0], dest[1], tr.endpos[2] + 40};
				trace_t t2 = heli_Trace(ent, h, ent->s.origin, d2);
				VectorCopy(t2.endpos, dest);
			}
			h->attackpt[2] = dest[2];
			heli_StartLeg(ent, h, dest, false);
			return;
		}
		h->attackend = level.time + h->attacktime;
		h->attackflag = false;
		h->pending = R_HoverFire;
		return;
	}
	int r = heli_GunAim(ent, h, h->attackpt);
	if (r == RG_AIM_TURNING)
		return;
	if (r == RG_AIM_LIMITED) {
		if (heli_TurnStep(ent, h, h->attackpt, false) == 0) {
			h->attackflag = false;
			h->routinedone = true;
		}
		return;
	}
	h->attackend = level.time + h->attacktime;
	h->attackflag = false;
	h->pending = R_HoverFire;
}

// ---------------------------------------------------------------------------------------------------------------
// auto-combat (§11)
// ---------------------------------------------------------------------------------------------------------------
static bool heli_SeesEye(edict_t *ent, edict_t *t, const vec3_t from)
{
	vec3_t eye;
	VectorCopy(t->s.origin, eye);
	eye[2] += t->viewheight;
	trace_t tr = gi.trace(from, vec3_origin, vec3_origin, eye, ent, 0x19);
	return tr.fraction >= 1.0f || tr.ent == t;
}

static void heli_RandomInArea(RgHeliWaypoint *w, vec3_t out)
{
	for (int i = 0; i < 3; i++)
		out[i] = frand(w->mins[i], w->maxs[i]);
}

static RgHeliWaypoint *heli_PointWp(RgHeli *h, int k)
{
	for (int i = 0, n = 0; i < h->nwp; i++)
		if (!h->wp[i].area) {
			if (n == k)
				return &h->wp[i];
			n++;
		}
	return NULL;
}
static RgHeliWaypoint *heli_AreaWp(RgHeli *h, int k)
{
	for (int i = 0, n = 0; i < h->nwp; i++)
		if (h->wp[i].area) {
			if (n == k)
				return &h->wp[i];
			n++;
		}
	return NULL;
}

static void heli_AttackInterject(edict_t *ent, RgHeli *h, edict_t *t)
{
	vec3_t eye;
	VectorCopy(t->s.origin, eye);
	eye[2] += t->viewheight;
	if (h->rockets_on && h->rockets > 0) {
		VectorSet(h->attackpt, eye[0] + frand(-10, 10), eye[1] + frand(-10, 10), eye[2]);
		h->attacktime = irand(0, 10) >= 5 ? -1.0f : -2.0f;
		if (h->hoverflag || h->autocombat)
			h->attacktime = irand(0, 10) < 5 ? -1.0f : -2.0f;
	} else {
		VectorCopy(eye, h->attackpt);
		h->attacktime = frand(2, 4);
	}
	VectorCopy(h->attackpt, h->gunaimpt);
	h->attackflag = true;
	h->routine = R_Attack;
	(void)ent;
}

static bool heli_Interject(edict_t *ent, RgHeli *h)
{
	edict_t *t = h->target;
	if (!t || !t->inuse || !t->takedamage) {
		h->pattern = -1;
		return true;
	}
	float a = h->aggression;
	if ((float)irand(0, 10) >= a)
		return false;
	vec3_t eye, d, f;
	VectorCopy(t->s.origin, eye);
	eye[2] += t->viewheight;
	VectorSubtract(eye, ent->s.origin, d);
	VectorNormalize(d);
	AngleVectors(ent->s.angles, f, NULL, NULL);
	bool sees = heli_SeesEye(ent, t, ent->s.origin);
	if ((float)irand(0, 10) >= a && (!sees || DotProduct(d, f) < 0.173f))
		return false;
	if (irand(0, 1) == 0) {
		heli_AttackInterject(ent, h, t);
		return true;
	}
	// reposition
	if (h->npoint > 0 && (float)irand(0, 10) < a) {
		RgHeliWaypoint *vis = NULL, *hid = NULL, *visnear = NULL;
		float bv = 1e30f, bh = 1e30f, bn = 1e30f;
		for (int i = 0; i < h->nwp; i++) {
			RgHeliWaypoint *w = &h->wp[i];
			if (w->area)
				continue;
			vec3_t dd;
			VectorSubtract(w->origin, t->s.origin, dd);
			float dt = VectorLength(dd);
			if (heli_SeesEye(ent, t, w->origin)) {
				if (dt < bv) {
					bv = dt;
					vis = w;
				}
				VectorSubtract(w->origin, ent->s.origin, dd);
				float dh = VectorLength(dd);
				if (dh > 0 && dh < bn) {
					bn = dh;
					visnear = w;
				}
			} else if (dt < bh) {
				bh = dt;
				hid = w;
			}
		}
		vec3_t dd;
		VectorSubtract(ent->s.origin, t->s.origin, dd);
		float D = VectorLength(dd);
		if (sees) {
			if (vis && bv + 800 < D) {
				heli_RoutineGoto(ent, h, vis->origin);
				return true;
			}
		} else {
			RgHeliWaypoint *w = D > 0 ? visnear : vis;
			if (w) {
				heli_RoutineGoto(ent, h, w->origin);
				return true;
			}
			if (hid && bh + 800 < D) {
				heli_RoutineGoto(ent, h, hid->origin);
				return true;
			}
		}
	}
	heli_StartHover(h, frand(1, 3), true);
	return true;
}

static void R_Combat(edict_t *ent, RgHeli *h)
{
	if (h->pattern < 0) {
		heli_ChooseTarget(h);
		int p = irand(0, 4);
		if (p == 4)
			p = 0;
		if (p == h->prevpattern)
			p = (p + 1) % 4;
		h->pattern = p;
		h->prevpattern = p;
		h->pstep = 0;
		h->routinedone = true;
		return;
	}
	if (heli_Interject(ent, h)) {
		if (rg_cvar("rg_trace"))
			rg_Trace("rg: heli %d combat interjection (attack %d hover %d goto %d)\n", G_EdictNum(ent),
			         h->attackflag, h->hoverflag, h->gotoflag);
		return;
	}
	edict_t *t = h->target;
	int step = h->pstep++;
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: heli %d combat pattern %d step %d\n", G_EdictNum(ent), h->pattern, step);
	switch (h->pattern) {
	case 0: { // strafing run along an area waypoint face (geometry UNVERIFIED, open question 4)
		if (step == 0) {
			RgHeliWaypoint *best = NULL;
			float bd = 1e30f;
			for (int i = 0; i < h->nwp; i++)
				if (h->wp[i].area) {
					vec3_t d;
					VectorSubtract(h->wp[i].origin, t->s.origin, d);
					float l = VectorLength(d);
					if (l < bd) {
						bd = l;
						best = &h->wp[i];
					}
				}
			if (!best || t->s.origin[2] < best->mins[2] || t->s.origin[2] > best->maxs[2]) {
				h->pattern = -1;
				h->routinedone = true;
				return;
			}
			int ax = irand(0, 1);
			if (t->s.origin[ax] < best->mins[ax] || t->s.origin[ax] > best->maxs[ax])
				ax ^= 1;
			if (t->s.origin[ax] < best->mins[ax] || t->s.origin[ax] > best->maxs[ax]) {
				h->pattern = -1;
				h->routinedone = true;
				return;
			}
			float s = fminf(fminf(t->s.origin[ax] - best->mins[ax], best->maxs[ax] - t->s.origin[ax]), 800);
			if (s <= 100) {
				h->pattern = -1;
				h->routinedone = true;
				return;
			}
			int other = ax ^ 1;
			vec3_t g;
			VectorCopy(t->s.origin, g);
			bool lowside = (t->s.origin[other] - best->mins[other]) < (best->maxs[other] - t->s.origin[other]);
			g[other] = lowside ? best->mins[other] : best->maxs[other];
			if (irand(0, 10) > 4)
				s = -s;
			g[ax] += s;
			h->pstrafe = s;
			VectorCopy(g, h->pfacepoint);
			h->pfacepoint[ax] -= (s > 0 ? 1000 : -1000);
			heli_RoutineGoto(ent, h, g);
		} else if (step == 1) {
			heli_StartTurn(h, ent, h->pfacepoint, true);
			h->routine = R_Turn;
		} else if (step == 2) {
			if (!heli_StartStrafe(ent, h, 2 * h->pstrafe))
				h->routinedone = true;
		} else {
			h->pattern = -1;
			h->routinedone = true;
		}
		break;
	}
	case 1: // vantage point
		if (step == 0) {
			if (h->npoint <= 0) {
				h->pattern = -1;
				h->routinedone = true;
				return;
			}
			RgHeliWaypoint *w = heli_PointWp(h, irand(0, 100) % h->npoint);
			if (w)
				heli_RoutineGoto(ent, h, w->origin);
		} else if (step == 1) {
			heli_StartTurn(h, ent, t->s.origin, true);
			h->routine = R_Turn;
		} else if (step == 2)
			heli_StartHover(h, frand(1, 3), true);
		else {
			h->pattern = -1;
			h->routinedone = true;
		}
		break;
	case 2: // patrol
		if (step == 0 || step == 2) {
			if (h->narea > 0) {
				RgHeliWaypoint *w = heli_AreaWp(h, irand(0, 100) % h->narea);
				vec3_t p;
				if (w) {
					heli_RandomInArea(w, p);
					heli_RoutineGoto(ent, h, p);
				}
			} else
				h->routinedone = true;
		} else if (step == 1 || step == 3)
			heli_StartHover(h, frand(1, 3), true);
		else {
			h->pattern = -1;
			h->routinedone = true;
		}
		break;
	case 3: // zone
		heli_ChooseTarget(h);
		if (step <= 2) {
			if (h->narea <= 0) {
				h->pattern = -1;
				h->routinedone = true;
				return;
			}
			if (step == 0)
				h->parea = irand(0, 100) % h->narea;
			RgHeliWaypoint *w = heli_AreaWp(h, h->parea);
			vec3_t p;
			if (w) {
				heli_RandomInArea(w, p);
				heli_RoutineGoto(ent, h, p);
			}
		} else if (step == 3)
			heli_StartHover(h, 2, true);
		else {
			h->pattern = -1;
			h->routinedone = true;
		}
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// crash (§12.3)
// ---------------------------------------------------------------------------------------------------------------
static void R_CrashSpin(edict_t *ent, RgHeli *h)
{
	ent->movetype = MOVETYPE_FLY;
	if (ent->s.angles[PITCH] < 20)
		ent->s.angles[PITCH] += 2;
	else
		ent->s.angles[PITCH] -= 2;
	ent->velocity[2] += fmaxf(-10.0f, -4.0f * (level.time - h->crashstart));
	vec3_t f;
	AngleVectors(ent->s.angles, f, NULL, NULL);
	ent->velocity[0] += f[0] * (-2.5f * ent->s.angles[PITCH]);
	ent->velocity[1] += f[1] * (-2.5f * ent->s.angles[PITCH]);
	ent->avelocity[YAW] = fminf(180.0f, ent->avelocity[YAW] + 45);
}

// ---------------------------------------------------------------------------------------------------------------
// the autonomous selector (§8)
// ---------------------------------------------------------------------------------------------------------------
static bool heli_Select(edict_t *ent, RgHeli *h)
{
	h->combatjustended = false;
	if (h->autocombat && ((level.time > h->acend && h->qcount > 0) || h->killed)) {
		h->autocombat = false;
		h->gotoflag = false;
		h->attackflag = false;
		h->combatjustended = true;
	}
	if (h->killed && !h->crashing) {
		h->crashing = true;
		if (ent->spawnflags & 2) {
			vec3_t g;
			heli_ClearSpot(ent, h, h->exitpt, g);
			VectorCopy(g, h->exitpt);
			VectorCopy(g, h->goal);
			h->gotoflag = true;
			h->routine = R_Goto;
		} else {
			vec3_t r;
			AngleVectors(ent->s.angles, NULL, r, NULL);
			VectorSet(h->exitpt, -r[0], -r[1], 0);
			h->crashstart = level.time + 2;
			h->routine = R_CrashSpin;
		}
		return true;
	}
	if (h->gotoflag) {
		h->routine = R_Goto;
		return true;
	}
	if (h->landreq) {
		h->landreq = false;
		h->stage = 0;
		h->routine = R_Land;
		return true;
	}
	if (h->attackflag) {
		h->routine = R_Attack;
		return true;
	}
	if (h->hoverflag) {
		h->routine = R_Hover;
		return true;
	}
	if (h->autocombat) {
		h->routine = R_Combat;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------
// HELI commands (§1)
// ---------------------------------------------------------------------------------------------------------------
static int heli_Family(int cmd)
{
	if ((cmd >= 1 && cmd <= 10) || cmd == 22 || cmd == 23 || (cmd >= 33 && cmd <= 45) || (cmd >= 47 && cmd <= 49))
		return 16;
	if (cmd >= 12 && cmd <= 21)
		return 17;
	if (cmd >= 24 && cmd <= 32)
		return 18;
	return 0;
}

static void R_Repair(edict_t *ent, RgHeli *h)
{
	ent->health += ent->max_health / 50;
	if (ent->health >= ent->max_health) {
		ent->health = ent->max_health;
		h->routinedone = true;
	}
}

static void R_Look(edict_t *ent, RgHeli *h)
{
	if (heli_PilotLook(ent, h, h->lookpoint))
		h->routinedone = true;
}

static void R_GunAimR(edict_t *ent, RgHeli *h)
{
	if (heli_GunAim(ent, h, h->lookpoint) == RG_AIM_ALIGNED)
		h->routinedone = true;
}

static float heli_f(void) { return 0.33f * (rg_Toughness() + 1); }

static void heli_BecomeHind(edict_t *ent);
static void heli_Repaint(edict_t *ent, float s);

// queue-time effects of ids 42-45, 47, 48 and 27
static void heli_QueueTimeEffect(edict_t *ent, RgHeli *h, int cmd, const vec3_t V, float S)
{
	switch (cmd) {
	case 42:
		if (h->nwp < HELI_WAYPOINTS) {
			RgHeliWaypoint *w = &h->wp[h->nwp++];
			memset(w, 0, sizeof(*w));
			VectorCopy(V, w->origin);
			h->npoint++;
		}
		break;
	case 43:
	case 44:
	case 45: {
		RgHeliWaypoint *w = NULL;
		for (int i = h->nwp - 1; i >= 0; i--)
			if (!h->wp[i].area) {
				w = &h->wp[i];
				break;
			}
		if (!w)
			break;
		if (cmd == 43) {
			for (int i = 0; i < 3; i++) {
				w->mins[i] = w->origin[i] - V[i] - ent->mins[i];
				w->maxs[i] = w->origin[i] + V[i] - ent->maxs[i];
			}
			w->area = true;
			h->npoint--;
			h->narea++;
		} else if (cmd == 44)
			VectorCopy(V, w->mins);
		else {
			vec3_t ext;
			VectorCopy(V, w->maxs);
			for (int i = 0; i < 3; i++)
				w->origin[i] = (w->mins[i] + w->mins[i]) / 2; // sic: the minimum corner, not the box centre
			for (int i = 0; i < 3; i++) {
				ext[i] = w->maxs[i] - w->origin[i];
				w->mins[i] -= w->origin[i];
			}
			for (int i = 0; i < 3; i++) {
				w->mins[i] = w->origin[i] - ext[i] - ent->mins[i];
				w->maxs[i] = w->origin[i] + ext[i] - ent->maxs[i];
			}
			w->area = true;
			h->npoint--;
			h->narea++;
		}
		break;
	}
	case 47: heli_Repaint(ent, S); break;
	case 48:
		if (S == 1)
			heli_BecomeHind(ent);
		break;
	case 27:
		h->gun_on = false;
		heli_BarrelSpin(h, false);
		h->cs = 0;
		if (h->hascur)
			h->cur.forcedone = true;
		break;
	}
}

// the action becomes current: set up its routine (or none)
static void heli_StartAction(edict_t *ent, RgHeli *h, RgHeliAction *a)
{
	h->routine = NULL;
	h->pending = NULL;
	h->routinedone = false;
	h->stage = 0;
	VectorCopy(a->v, h->cv);
	h->ce = a->e;
	h->cs = a->s;
	vec3_t g;
	edict_t *E = a->e;
	float S = a->s;
	switch (a->cmd) {
	case 1: h->routine = R_TakeOff; break;
	case 2:
		VectorCopy(h->home, h->cv);
		if (ent->spawnflags & 1)
			break;
		h->landreq = true;
		if (!(ent->s.origin[0] == h->home[0] && ent->s.origin[1] == h->home[1] &&
		      ent->s.origin[2] <= h->home[2] + 224)) {
			VectorCopy(h->home, g);
			g[2] += 224;
			heli_SetGoto(ent, h, g);
		}
		break;
	case 3: h->routine = R_Repair; break;
	case 4: h->rockets = 8; break;
	case 5: heli_SetGoto(ent, h, a->v); break;
	case 6:
		if (E) {
			VectorAdd(E->s.origin, a->v, g);
			heli_SetGoto(ent, h, g);
		}
		break;
	case 7:
	case 8:
	case 9:
		if (E) {
			VectorCopy(E->s.origin, g);
			g[a->cmd - 7] += S;
			heli_SetGoto(ent, h, g);
		}
		break;
	case 10:
		VectorAdd(ent->s.origin, a->v, g);
		heli_SetGoto(ent, h, g);
		break;
	case 22:
	case 23:
		if (!heli_StartStrafe(ent, h, a->cmd == 22 ? fabsf(S) : -fabsf(S)))
			h->routine = NULL;
		break;
	case 33: heli_StartHover(h, S, false); break;
	case 34: heli_StartHover(h, S, true); break;
	case 35: VectorCopy(a->v, rg_heli_bmin); break;
	case 36: VectorCopy(a->v, rg_heli_bmax); break;
	case 37:
		if (ent->max_health < 1)
			ent->max_health = 0;
		else
			ent->max_health = (int)(heli_f() * S) + 1000;
		break;
	case 38: ent->health = S < 1 ? 0 : (int)(heli_f() * S) + 1000; break;
	case 39: VectorCopy(a->v, h->exitpt); break;
	case 40: h->navbox = S != 0; break;
	case 41:
		h->autocombat = true;
		h->acend = level.time + S;
		h->pattern = -1;
		h->routine = R_Combat;
		break;
	case 49:
		VectorClear(h->exitpt);
		h->crashstart = level.time + 2;
		h->routine = R_CrashSpin;
		break;
	// family 2
	case 12:
		if (E) {
			VectorAdd(E->s.origin, a->v, g);
			heli_StartTurn(h, ent, g, true);
			h->routine = R_Turn;
		}
		break;
	case 13:
		heli_StartTurn(h, ent, a->v, true);
		h->routine = R_Turn;
		break;
	case 14: {
		vec3_t n;
		VectorCopy(a->v, n);
		VectorNormalize(n);
		VectorMA(ent->s.origin, 1000, n, g);
		heli_StartTurn(h, ent, g, true);
		h->routine = R_Turn;
		break;
	}
	case 15:
		VectorAdd(ent->s.origin, a->v, g);
		heli_StartTurn(h, ent, g, true);
		h->routine = R_Turn;
		break;
	case 16:
	case 19:
		if (E) {
			VectorAdd(E->s.origin, a->v, h->lookpoint);
			h->routine = a->cmd == 16 ? R_Look : R_GunAimR;
		}
		break;
	case 17:
	case 20:
		VectorAdd(ent->s.origin, a->v, h->lookpoint);
		h->routine = a->cmd == 17 ? R_Look : R_GunAimR;
		break;
	case 18:
	case 21:
		VectorCopy(a->v, h->lookpoint);
		h->routine = a->cmd == 18 ? R_Look : R_GunAimR;
		break;
	// family 3
	case 24: h->rockets_on = true; break;
	case 25: h->rockets_on = false; break;
	case 26: h->gun_on = true; break;
	case 27: break; // acted at queue time
	case 28:
		if (E && E->inuse && E->takedamage) {
			VectorAdd(E->s.origin, a->v, h->attackpt);
			VectorCopy(h->attackpt, h->gunaimpt);
			h->attacktime = S;
			h->attackflag = true;
		}
		break;
	case 29:
	case 30:
		if (a->cmd == 29)
			VectorCopy(a->v, h->attackpt);
		else
			VectorAdd(ent->s.origin, a->v, h->attackpt);
		VectorCopy(h->attackpt, h->gunaimpt);
		h->attacktime = S;
		h->attackflag = true;
		h->routine = R_Attack;
		break;
	case 31: h->ffw = true; break;
	case 32: h->ffw = false; break;
	default: break; // 42-45, 47, 48: no routine (queue-time effects)
	}
}

static void heli_NextAction(edict_t *ent, RgHeli *h)
{
	h->hascur = false;
	if (h->qcount <= 0)
		return;
	h->cur = h->queue[0];
	memmove(&h->queue[0], &h->queue[1], sizeof(h->queue[0]) * (h->qcount - 1));
	h->qcount--;
	h->hascur = true;
	h->rememberedq = h->qcount;
	h->cur.started = true;
	heli_StartAction(ent, h, &h->cur);
}

static void heli_CompleteAction(edict_t *ent, RgHeli *h)
{
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: %.1f heli %d done id %d cmd %d\n", level.time, G_EdictNum(ent), h->cur.id, h->cur.cmd);
	if (h->cur.id)
		h->lastdone = h->cur.id;
	h->hascur = false;
	h->routine = h->pending = NULL;
	VectorClear(h->cv);
	h->ce = NULL;
	h->cs = 0;
	heli_NextAction(ent, h);
}

// one frame of the current action (SPEC-731 §8 "Running a helicopter action")
static void heli_RunAction(edict_t *ent, RgHeli *h)
{
	if (!ent->ghoulInst)
		return;
	RgHeliAction *a = &h->cur;
	if (a->forcedone) {
		heli_CompleteAction(ent, h);
		return;
	}
	if (h->routine) {
		VectorCopy(a->v, h->cv);
		h->cs = a->s;
		h->ce = a->e;
		if (h->pending) {
			h->routine = h->pending;
			h->pending = NULL;
		}
		h->routine(ent, h);
		if (h->pending) {
			// a routine that hands over continues next frame
		}
		heli_Smoke(ent, h, ent->max_health > 0 ? (float)ent->health / ent->max_health : 0);
		if (!h->routinedone && !(h->killed && !h->crashing))
			return;
	}
	if (h->killed && !h->crashing) {
		bool athome = true;
		for (int i = 0; i < 3; i++)
			if (fabsf(fabsf(ent->s.origin[i]) - fabsf(h->home[i])) > 0.1f)
				athome = false;
		if (athome) {
			h->remove = true;
			h->routinedone = false;
			heli_CompleteAction(ent, h);
			return;
		}
	}
	h->routinedone = false;
	h->pending = NULL;
	if (heli_Select(ent, h))
		return;
	heli_CompleteAction(ent, h);
}

// ---------------------------------------------------------------------------------------------------------------
// crew reactions (§12.2), death (§12.4)
// ---------------------------------------------------------------------------------------------------------------
static void heli_CrewReaction(edict_t *ent, RgHeli *h, int code)
{
	ent->movetype = MOVETYPE_FLY;
	if (h->crashing)
		return;
	h->qcount = 0;
	h->hascur = false;
	h->routine = h->pending = NULL;
	if (!VectorCompare(ent->s.origin, h->home)) {
		RgHeliAction a;
		memset(&a, 0, sizeof(a));
		a.cmd = 49;
		a.cls = 16;
		a.id = ++h->idcounter;
		h->queue[h->qcount++] = a;
		h->crashing = true;
	}
	if (!rg_cvar("lock_blood")) {
		Body_HeliCanopyBlood(ent, code);
		if ((code & 1) && h->headgunner)
			h->headgunner->Pause(level.time);
		if ((code & 2) && h->headpilot)
			h->headpilot->Pause(level.time);
	}
}

static void heli_Death(edict_t *ent, RgHeli *h, edict_t *killer)
{
	if (ent->health < -999)
		ent->health = -999;
	if (ent->killtarget) {
		edict_t *t = NULL;
		while ((t = G_Find(t, FOFS(targetname), ent->killtarget)) != NULL)
			if (t->use)
				t->use(t, ent, ent);
	}
	if (ent->deadflag != DEAD_DEAD) {
		om_ai::CountKill(ent);
		om_ai::RemoveAllComponents(ent);
		h->qcount = 0;
		h->hascur = false;
		h->routine = h->pending = NULL;
		ent->deadflag = DEAD_DEAD;
		ent->touch = NULL;
		gi.linkentity(ent);
	}
	ent->enemy = killer;
	ent->s.effects |= 0x400;
	rg_EffectAt("heli_expl", ent->s.origin);
}

static void heli_CrashStage(edict_t *ent, RgHeli *h)
{
	if (!rg_heli_stage || level.time < rg_heli_stagetime)
		return;
	switch (rg_heli_stage) {
	case 1:
		if (h->headgunner)
			h->headgunner->Pause(level.time);
		if (h->headpilot)
			h->headpilot->Pause(level.time);
		rg_EffectOnBolt("weapons/world/autogundeath", ent, ent->ghoulInst, "BODY1");
		gi.sound(ent, CHAN_AUTO, gi.soundindex("impact/explosion/med.wav"), 1, ATTN_NORM, 0);
		rg_heli_stage = 2;
		rg_heli_stagetime = level.time + 0.2f;
		break;
	case 2:
		rg_EffectOnBolt("weapons/world/autogundeath", ent, ent->ghoulInst, "ROTOR_MAIN");
		gi.sound(ent, CHAN_AUTO, gi.soundindex("impact/explosion/exp1.wav"), 1, ATTN_NORM, 0);
		rg_heli_stage = 3;
		rg_heli_stagetime = level.time + 1.0f;
		break;
	case 3: {
		vec3_t p;
		if (!rg_BoltWorld(ent, ent->ghoulInst, "ROTOR_MAIN1", p))
			VectorCopy(ent->s.origin, p);
		rg_EffectAt("heli_expl", p);
		ent->s.effects |= 0x400;
		heli_Smoke(ent, h, -1);
		rg_heli_stage = 4;
		rg_heli_stagetime = level.time + 0.1f;
		break;
	}
	case 4:
		rg_heli_stage = 0;
		om_ai::FreeCharacter(ent);
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// per-frame update (§8)
// ---------------------------------------------------------------------------------------------------------------
void RgHeliAI::Think(edict_t *ent)
{
	RgHeli *h = &this->h;
	ent->nextthink = level.time + FRAMETIME;
	if (!h)
		return;
	if (h->killed && rg_cvar("rg_trace") >= 2)
		rg_Trace("rg: heli %d think killed: org (%.1f %.1f %.1f) vel (%.1f %.1f %.1f) movetype %d ground %s crashing %d\n",
		         G_EdictNum(ent), ent->s.origin[0], ent->s.origin[1], ent->s.origin[2], ent->velocity[0],
		         ent->velocity[1], ent->velocity[2], (int)ent->movetype,
		         ent->groundentity ? (ent->groundentity->classname ? ent->groundentity->classname : "?") : "-",
		         h->crashing);
	if (ent->deadflag == DEAD_DEAD) {
		ent->movetype = MOVETYPE_FLY;
		ent->s.effects &= ~0x400;
		h->curid = 0;
		ent->velocity[2] = 0;
		heli_CrashStage(ent, h);
		return;
	}
	if (rg_cvar("ai_freeze"))
		return;
	if (h->freefire)
		heli_FireRound(ent, h, true);
	// (the common character update, senses, belongs to the AI framework)
	int crew = Body_HeliCrewDead(ent);
	if ((crew & 3) == 3) {
		heli_CrewReaction(ent, h, 3);
		heli_CrewReaction(ent, h, 2);
	} else if (crew & 1)
		heli_CrewReaction(ent, h, 1);
	else if (crew & 2)
		heli_CrewReaction(ent, h, 2);
	if (!h->hascur && h->qcount > 0)
		heli_NextAction(ent, h);
	// A helicopter killed while it has no action does nothing until its next action runs (the crash or the
	// at-home explosion are decided by the action, §8): trn1 test, the original's idle helicopter killed by an
	// explosion at 5 s explodes only when the script's first HELI command reaches it 4 s later.
	if (h->hascur) {
		h->curid = h->cur.id;
		heli_RunAction(ent, h);
	}
	gi.linkentity(ent);
	if (h->remove) {
		heli_Death(ent, h, h->killer);
		if (ent->killtarget && Q_stricmp(ent->killtarget, "musicscript")) {
			VectorSet(ent->s.origin, -1000, 744, 986);
			VectorSet(ent->velocity, -10, 40, 0);
			ent->s.angles[ROLL] = -10;
			rg_heli_stage = 1;
			rg_heli_stagetime = level.time + 1;
			h->remove = false;
			gi.linkentity(ent);
		} else {
			om_ai::FreeCharacter(ent);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// use / touch (§14), damage (§12.1)
// ---------------------------------------------------------------------------------------------------------------
static void heli_Use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	(void)activator;
	RgHeli *h = HELI(self);
	if (!h)
		return;
	h->freefire = !h->freefire;
	heli_BarrelSpin(h, h->freefire);
}

static void heli_Touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)other;
	(void)plane;
	(void)surf;
	RgHeli *h = HELI(self);
	if (h && h->crashing) {
		if (rg_cvar("rg_trace"))
			rg_Trace("rg: %.1f heli %d crash touch %s at (%.0f %.0f %.0f)\n", level.time, G_EdictNum(self),
			         other ? other->classname : "-", self->s.origin[0], self->s.origin[1], self->s.origin[2]);
		h->remove = true;
	}
}

// §12.1: the helicopter's own damage handler (G3's Dmg_Damage hands every hit on the entity to its AI object)
int RgHeliAI::Damage(G3Hit &hit)
{
	edict_t *ent = hit.targ;
	RgHeli *h = &this->h;
	if (hit.mod == MOD_C4 || !ent->takedamage)
		return 0;
	int damage = hit.damage;
	bool routed = false;
	if (body) {
		if (damage == 0)
			return 0;
		if (hit.dflags & (DMGF_PROJECTILE | DMGF_0x400)) {
			damage = body->Damage(hit);
			routed = true;
		}
	}
	ent->health -= damage;
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: %.1f heli %d damage %d (asked %d) mod %d from %s, health %d, org (%.1f %.1f %.1f) movetype %d\n",
		         level.time, G_EdictNum(ent), damage, hit.damage, hit.mod,
		         hit.attacker && hit.attacker->classname ? hit.attacker->classname : "-", ent->health,
		         ent->s.origin[0], ent->s.origin[1], ent->s.origin[2], (int)ent->movetype);
	if (ent->health > 0) {
		if (routed)
			heli_Smoke(ent, h, (float)ent->health / (ent->max_health > 0 ? ent->max_health : 1));
		return damage != 0;
	}
	h->killer = hit.attacker;
	ent->flags |= FL_NO_KNOCKBACK;
	h->killed = true;
	ent->takedamage = DAMAGE_NO;
	ent->nextthink = level.time + FRAMETIME;
	return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// model (§4), repaint (id 47), Hind (§7), spawn (§3)
// ---------------------------------------------------------------------------------------------------------------
static IGhoulInst *heli_Bolt(edict_t *ent, IGhoulInst *parent, const char *pbolt, const char *dir, const char *model,
                             const char *cbolt, float scale, const char *seq, int mode)
{
	RgModel *m = rg_FindModel(dir, model, true);
	IGhoulInst *c = m ? rg_AddBolt(ent, parent, pbolt, m, cbolt, NULL, scale) : NULL;
	if (!c) {
		gi.dprintf("helicopter: couldn't add bolt-on %s/%s\n", dir, model);
		return NULL;
	}
	// a bolt-on without a sequence of its own shows its default sequence as a static pose (observed in the
	// original: gun mount, barrel, crew heads, tail light; docs/impl/G4.md)
	rg_Play(c, seq ? seq : model, seq ? mode : kPlayStatic);
	return c;
}

static bool heli_BuildModel(edict_t *ent, RgHeli *h, bool hind, const char *skin)
{
	const char *dir = hind ? "enemy/hind" : "enemy/chopper";
	RgModel *m = rg_FindModel(dir, hind ? "hind" : "cobrass", true);
	if (!m || !GG_Object(m))
		return false;
	GG_RemoveEntity(ent);
	IGhoulInst *inst = rg_NewInstance(m, ent);
	if (!inst)
		return false;
	ent->s.renderfx = RF_GHOUL;
	// the main skin is set by the spawn only (§3 step 8); a helicopter that becomes a Hind (§7) keeps the model's
	// own skin (no skin override in the original's capture of jpn3, j3intro)
	if (skin)
		rg_SetSkin(inst, hind ? "hind" : "cobra", skin);
	rg_Play(inst, hind ? "hind" : "cobrass", kPlayLoop); // the model's default sequence (an instance without a
	                                                     // sequence is never sent, SPEC-013 §4)
	h->hind = hind;
	h->smoke = false;
	if (!hind) {
		heli_Bolt(ent, inst, "nav_lite", "enemy/chopper", "nav_lite", "to_nav_lite", 1, "nav_lite", kPlayLoop);
		heli_Bolt(ent, inst, "nav_lite2", "enemy/chopper", "nav_lite2", "to_nav_lite2", 1, "nav_lite2", kPlayLoop);
		heli_Bolt(ent, inst, "nav_lite3", "enemy/chopper", "nav_lite3", "to_nav_lite3", 1, "nav_lite3", kPlayLoop);
		heli_Bolt(ent, inst, "tail_lite", "enemy/chopper", "tail_lite", "to_tail_lite", 1, NULL, 0);
	}
	h->gunnull = heli_Bolt(ent, inst, "barrel_base", "objects/generic/gun_auto", "null", "DUMMY01", 1, NULL, 0);
	if (h->gunnull)
		h->gunnull->SetAllPartsOnOff(false); // the mount is invisible (observed in the original)
	h->barrel = h->gunnull ? heli_Bolt(ent, h->gunnull, "DUMMY01", "enemy/chopper", "barrel", "to_barrel_base", 1,
	                                   NULL, 0)
	                       : NULL;
	h->barrelspin = false;
	h->headgunner = heli_Bolt(ent, inst, "body1", "enemy/chopper", "head", "to_body", 1, NULL, 0);
	h->headpilot = heli_Bolt(ent, inst, "body2", "enemy/chopper", "head", "to_body", 1, NULL, 0);
	// tail / main rotor scales (open question A2, answered by the original's captures on trn1 and jpn3: ROTOR_TAIL
	// 0.25 / 0.375, ROTOR_MAIN 1.25 / 1.875)
	float st = hind ? 0.375f : 0.25f, sm = hind ? 1.875f : 1.25f;
	const char *rseq = (ent->spawnflags & 1) ? "rotor_anim" : "rotor";
	int rmode = (ent->spawnflags & 1) ? kPlayLoop : kPlayOnce;
	h->rotortail = heli_Bolt(ent, inst, "rotor_tail", "enemy/chopper", "rotor", "to_rotor", st, rseq, rmode);
	h->rotormain = heli_Bolt(ent, inst, "rotor_main", "enemy/chopper", "rotor", "to_rotor", sm, rseq, rmode);
	h->canopy = heli_Bolt(ent, inst, "canopy", dir, "canopy", "to_canopy", 1, "canopy", kPlayOnce);
	rg_AimInit(&h->aimpilot, h->headpilot, inst, "body2");
	rg_AimInit(&h->aimgunhead, h->headgunner, inst, "body1");
	rg_AimInit(&h->aimgunbase, h->gunnull, inst, "barrel_base");
	rg_AimInit(&h->aimbarrel, h->barrel, h->gunnull, "DUMMY01");
	ent->s.sound = gi.soundindex("Enemy/Helicopter/Blade.wav");
	ent->s.sounddata = 0xFB;
	return true;
}

// the helicopter body (SPEC-722) is attached to the AI once the model exists
static void heli_AttachBody(edict_t *ent, RgHeli *h)
{
	G3Body *b = Body_Create(ent, BODY_HELI);
	if (b && ent->ai) {
		b->owner = ent->ai;
		ent->ai->body = b;
	}
	Body_HeliAllowCrewKill(ent, h->crewkills);
}

static void heli_Repaint(edict_t *ent, float s)
{
	if (!ent->ghoulInst)
		return;
	const char *skin = s == 1 ? "m_x_chopper_black" : s == 2 ? "m_x_chopper_green" : "m_x_chopper_white";
	rg_SetSkin(ent->ghoulInst, "cobra", skin);
	int n = ent->ghoulInst->NumChildren();
	for (int i = 0; i < n; i++) {
		GhoulID b;
		IGhoulInst *c = ent->ghoulInst->GetChild(i, &b);
		if (c)
			rg_SetSkin(c, "cobra", skin);
	}
}

static void heli_ResetBody(RgHeli *h)
{
	h->rockets = 8;
	h->rockets_on = h->gun_on = h->ffw = h->autocombat = h->hoverflag = false;
	h->attackflag = false;
	h->target = NULL;
	h->sniperleft = 1;
	h->navbox = true;
	h->pattern = -1;
	h->prevpattern = -1;
}

static void heli_BecomeHind(edict_t *ent)
{
	RgHeli *h = HELI(ent);
	if (!h)
		return;
	heli_ResetBody(h);
	Body_Destroy(ent);
	heli_BuildModel(ent, h, true, NULL);
	heli_AttachBody(ent, h);
	ent->movetype = MOVETYPE_FLY;
	ent->solid = SOLID_BBOX;
	ent->flags |= FL_NO_KNOCKBACK;
	ent->takedamage = DAMAGE_YES;
	gi.linkentity(ent);
}

// §3 step 10: first think (the AI framework then runs RgHeliAI::Think on this and every later think)
static void heli_FirstThink(edict_t *ent)
{
	if (ent->ai)
		om_ai::AddDecisionSet(ent->ai, om_ai::SET_VEHICLE);
	static const char *fx[] = {"environ/helismoke", "heli_expl", "weapons/world/autogundeath",
	                           "weapons/world/rockettrail2"};
	for (const char *f : fx)
		gi.effectindex(f);
	gi.soundindex("impact/explosion/exp1.wav");
	gi.soundindex("impact/explosion/med.wav");
	gi.soundindex("weapons/assault/fire.wav");
	WeaponPrecacheAttack(14);
	if (rg_cvar("rg_trace")) {
		// test aid: crew-head bolt positions in entity space (x forward, y left, z up) for aimed client shots
		static const char *bolts[] = {"BODY1", "BODY2", "CANOPY", "BARREL_BASE"};
		for (const char *b : bolts) {
			vec3_t w, l;
			if (rg_BoltWorld(ent, ent->ghoulInst, b, w)) {
				rg_WorldToEntity(ent, w, l, true);
				rg_Trace("rg: heli %d bolt %s local (%.1f %.1f %.1f)\n", G_EdictNum(ent), b, l[0], l[1], l[2]);
			}
		}
	}
	if (ent->spawnflags & 16) {
		// a temporary Hind precaches the model and its bolt-ons
		edict_t *tmp = G_Spawn();
		RgHeli th;
		memset(&th, 0, sizeof(th));
		th.ent = tmp;
		heli_BuildModel(tmp, &th, true, NULL);
		GG_RemoveEntity(tmp);
		G_FreeEdict(tmp);
	}
}

void rg_SpawnHeli(edict_t *ent)
{
	const char *cn = ent->classname;
	bool hind = !Q_stricmp(cn, "m_x_hind");
	gi.effectindex("weapons/othermz/heli");
	WeaponPrecacheAttack(28);
	ent->svflags |= SVF_MONSTER;
	if (ent->spawnflags & 32)
		ent->svflags |= 0x100;
	ent->use = heli_Use;
	ent->touch = heli_Touch;
	VectorSet(ent->mins, -280, -280, -50);
	VectorSet(ent->maxs, 280, 280, 70);
	vec3_t exitc;
	VectorCopy(ent->s.origin, exitc);
	if (!(ent->spawnflags & 1)) {
		ent->s.origin[2] += 200;
		vec3_t end = {ent->s.origin[0], ent->s.origin[1], -600};
		trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, end, ent, MASK_SOLID);
		if (tr.fraction > 0 && tr.fraction < 1)
			VectorCopy(tr.endpos, exitc);
		vec3_t mn = {0, 0, -50}, mx = {0, 0, 0};
		trace_t t2 = gi.trace(ent->s.origin, mn, mx, end, ent, MASK_SOLID);
		if (t2.fraction < 1)
			VectorCopy(t2.endpos, ent->s.origin);
		else
			ent->s.origin[2] -= 200;
	}
	ent->health = ent->max_health = 1000;
	RgChar *ch = rg_NewChar(ent, om_ai::TYPE_HELICOPTER, RG_FAM_HELI);
	ch->startset = RG_SET_VEHICLE;
	om_ai::AI *ai = om_ai::CreateAI(ent, om_ai::TYPE_HELICOPTER);
	if (!ai || ai->type_code != om_ai::TYPE_HELICOPTER || !HELI(ent))
		return;
	RgHeli *h = HELI(ent);
	h->ent = ent;
	heli_ResetBody(h);
	h->crewkills = !((ent->spawnflags & 16) && rg_Toughness() >= 2);
	const char *skin = hind ? "hind" : cn; // the main skin is named like the classname (§2)
	if (!heli_BuildModel(ent, h, hind, skin)) {
		gi.dprintf("Couldn't find enemy model %s\n", hind ? "enemy/hind/hind" : "enemy/chopper/cobrass");
		return;
	}
	heli_AttachBody(ent, h);
	VectorCopy(ent->s.origin, h->home);
	VectorCopy(exitc, h->exitpt);
	h->aggression = ent->volume;
	ent->movetype = MOVETYPE_FLY;
	ent->solid = SOLID_BBOX;
	ent->flags |= FL_NO_KNOCKBACK;
	ent->takedamage = DAMAGE_YES;
	ai->first_think = heli_FirstThink;
	ent->nextthink = level.time + FRAMETIME;
	gi.linkentity(ent);
}

// ---------------------------------------------------------------------------------------------------------------
// script interface (SPEC-750 §12.8) and savegame records (§6)
// ---------------------------------------------------------------------------------------------------------------
bool rg_IsHeli(edict_t *ent) { return HELI(ent) != NULL; }

int rg_HeliCommand(edict_t *ent, int cmd, edict_t *target, const vec3_t vec, float scalar)
{
	RgHeli *h = HELI(ent);
	if (!h)
		return -2;
	if (cmd == 46) {
		ent->spawnflags |= 8;
		return -1;
	}
	int fam = heli_Family(cmd);
	if (!fam)
		return -1;
	if (cmd == 47 || cmd == 48)
		target = ent;
	if (h->qcount >= HELI_QUEUE)
		return -1;
	RgHeliAction a;
	memset(&a, 0, sizeof(a));
	a.id = ++h->idcounter;
	a.cmd = cmd;
	a.cls = fam;
	if (vec)
		VectorCopy(vec, a.v);
	a.e = target;
	a.s = scalar;
	h->queue[h->qcount++] = a;
	heli_QueueTimeEffect(ent, h, cmd, a.v, scalar);
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: %.1f heli %d queued id %d cmd %d (%.0f %.0f %.0f) s %.1f\n", level.time, G_EdictNum(ent), a.id,
		           cmd, a.v[0], a.v[1], a.v[2], scalar);
	return a.id;
}

bool rg_HeliActionDone(edict_t *ent, int id)
{
	RgHeli *h = HELI(ent);
	if (!h)
		return true;
	return !h->hascur && h->lastdone == id;
}

static void put32(uint8_t *b, int off, int v) { memcpy(b + off, &v, 4); }
static void putf(uint8_t *b, int off, float v) { memcpy(b + off, &v, 4); }
static int get32(const uint8_t *b, int off)
{
	int v;
	memcpy(&v, b + off, 4);
	return v;
}
static float getf(const uint8_t *b, int off)
{
	float v;
	memcpy(&v, b + off, 4);
	return v;
}

void rg_HeliWrite(edict_t *ent, int part, roster_chunk_write_t w, void *ctx)
{
	RgHeli *h = HELI(ent);
	if (!h)
		return;
	if (part == 0) {
		uint8_t b[48];
		memset(b, 0, sizeof(b));
		put32(b, 0, h->routinedone);
		put32(b, 4, h->remove);
		put32(b, 16, h->killer ? G_EdictNum(h->killer) : -1);
		put32(b, 20, h->idcounter);
		put32(b, 24, h->curid);
		put32(b, 28, h->rememberedq);
		put32(b, 36, h->nwp);
		put32(b, 40, h->npoint);
		put32(b, 44, h->narea);
		w(ctx, "AIHA", b, sizeof(b));
	} else {
		for (int i = 0; i < h->nwp; i++) {
			uint8_t b[44];
			memset(b, 0, sizeof(b));
			for (int k = 0; k < 3; k++) {
				putf(b, 4 + 4 * k, h->wp[i].origin[k]);
				putf(b, 16 + 4 * k, h->wp[i].area ? h->wp[i].mins[k] : 0);
				putf(b, 28 + 4 * k, h->wp[i].area ? h->wp[i].maxs[k] : 0);
			}
			w(ctx, "HEWP", b, sizeof(b));
		}
	}
}

bool rg_HeliRead(edict_t *ent, int part, roster_chunk_read_t r, void *ctx)
{
	RgHeli *h = HELI(ent);
	if (!h)
		return false;
	if (part == 0) {
		uint8_t b[48];
		if (!r(ctx, "AIHA", b, sizeof(b)))
			return false;
		h->routinedone = get32(b, 0) != 0;
		h->remove = get32(b, 4) != 0;
		int k = get32(b, 16);
		h->killer = (k >= 0 && k < globals.num_edicts) ? &g_edicts[k] : NULL;
		h->idcounter = get32(b, 20);
		h->curid = get32(b, 24);
		h->rememberedq = get32(b, 28);
		h->nwp = get32(b, 36);
		if (h->nwp > HELI_WAYPOINTS)
			h->nwp = HELI_WAYPOINTS;
		h->npoint = get32(b, 40);
		h->narea = get32(b, 44);
		return true;
	}
	for (int i = 0; i < h->nwp; i++) {
		uint8_t b[44];
		if (!r(ctx, "HEWP", b, sizeof(b)))
			return false;
		bool point = true;
		for (int k = 0; k < 3; k++) {
			h->wp[i].origin[k] = getf(b, 4 + 4 * k);
			h->wp[i].mins[k] = getf(b, 16 + 4 * k);
			h->wp[i].maxs[k] = getf(b, 28 + 4 * k);
			if (h->wp[i].mins[k] != 0 || h->wp[i].maxs[k] != 0)
				point = false;
		}
		h->wp[i].area = !point;
	}
	return true;
}
