// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: tank and snowcat AI (m_x_tank, m_x_snowcat).
//
// Spawn values and models (§1-§2), VEHICLE commands 1-5 (§3) as a queue run one step per think, the vehicle think
// (§6), disabled state and use (§7), driving (§8), aiming of turret, cannon and guns with the shared aim step (§9),
// cannon shell (§10), automatic machine-gun bursts (§11), self-destruct (§12), damage (§13), death (§14), tank
// touch (§15), the turret end of sequence after a shot (§16) and the AITA record (§4). The bodies (moving and
// disabled state, snowcat alternate look, machine-gun hit points, aiming parameters, hit handling) are G3's
// tank/snowcat bodies (SPEC-722).
//
// Implements: SPEC-731 Part B, SPEC-711 A8 classes 20/21 (vehicle side)
#include "rg_local.h"

using namespace ghoul;

#define VEH_QUEUE 32

struct RgVehCmd {
	int id;
	int cmd;
	vec3_t v;
	edict_t *e;
	float s;
	int state;          // command-specific step state
	float remaining;    // cannon shell flight distance
	vec3_t impact;
};

struct RgVehicle {
	edict_t *ent;
	bool snowcat;
	RgVehCmd queue[VEH_QUEUE];
	int qcount;
	RgVehCmd cur;
	bool hascur;
	int idcounter, curid, lastdone;
	edict_t *killer;
	int removal;
	bool reverse;
	bool mg_on;
	bool lof;
	bool stepfinished;
	float burststart;
	vec3_t mgtarget;
	float params[5];    // hull turn rate, drive speed, turret yaw rate, cannon pitch rate, machine-gun rate
	IGhoulInst *turret, *cannon, *mount, *mgun, *gun1, *gun2;
	RgAimPart aturret, acannon, amount, amgun, agun1, agun2;
	bool fired;
	float turretend;
	bool dead;
	bool idle;          // think stopped (disabled), SPEC-731 B §6 step 2
};

class RgVehicleAI : public OmAI {
public:
	RgVehicle v;
	RgVehicleAI() { memset(&v, 0, sizeof(v)); }
	void Think(edict_t *ent) override;
	int Damage(G3Hit &hit) override;
};

om_ai::AI *rg_MakeVehicleAI(int code)
{
	(void)code;
	return new RgVehicleAI();
}

static RgVehicle *VEH(edict_t *ent)
{
	if (!ent || !ent->inuse || !ent->ai ||
	    (ent->ai->type_code != om_ai::TYPE_TANK && ent->ai->type_code != om_ai::TYPE_SNOWCAT))
		return NULL;
	return &static_cast<RgVehicleAI *>(ent->ai)->v;
}

bool rg_IsVehicle(edict_t *ent) { return VEH(ent) != NULL; }

static float frand(float a, float b) { return gix.flrand(a, b); }

// ---------------------------------------------------------------------------------------------------------------
// aiming (§9)
// ---------------------------------------------------------------------------------------------------------------
enum { AIM_NOTYET = 0, AIM_DONE = 1, AIM_POSSIBLE = 2 };

static int veh_TankCannonAim(edict_t *ent, RgVehicle *v, const vec3_t point)
{
	if (!v->turret || !v->cannon)
		return AIM_POSSIBLE;
	vec3_t t;
	VectorCopy(point, t);
	t[2] += 12;
	int a = rg_AimStep(ent, &v->aturret, t, true, false, -4000, 4000, 0, 0, v->params[2]);
	float y = v->aturret.yaw;
	float p = ((y > -0.13f && y < 0.13f) || (y > 0.8f && y < 2.4f) || (y > -2.4f && y < -0.8f)) ? -0.05f : -0.02f;
	int b = rg_AimStep(ent, &v->acannon, t, false, true, 0, 0, p, 0.2f, v->params[3]);
	if (a == RG_AIM_ALIGNED && b == RG_AIM_ALIGNED)
		return AIM_DONE;
	if ((a == RG_AIM_ALIGNED || a == RG_AIM_LIMITED) && (b == RG_AIM_ALIGNED || b == RG_AIM_LIMITED))
		return AIM_POSSIBLE;
	return AIM_NOTYET;
}

static bool veh_TankMGAim(edict_t *ent, RgVehicle *v, const vec3_t point)
{
	if (rg_Toughness() == 0 || !v->mount || !v->mgun)
		return false;
	vec3_t t;
	VectorCopy(point, t);
	t[2] += 20;
	int a = rg_AimStep(ent, &v->amount, t, true, false, -4000, 4000, 0, 0, v->params[4]);
	int b = rg_AimStep(ent, &v->amgun, t, false, true, 0, 0, -0.314159f, 0.785398f, v->params[4]);
	return a == RG_AIM_ALIGNED && b == RG_AIM_ALIGNED;
}

static bool veh_SnowcatAim(edict_t *ent, RgVehicle *v, const vec3_t point)
{
	if (rg_Toughness() == 0 || !v->turret)
		return false;
	vec3_t t;
	VectorCopy(point, t);
	t[2] += 20;
	float rate = v->params[2];
	int a = rg_AimStep(ent, &v->aturret, t, true, false, -4000, 4000, 0, 0, rate);
	float y = fabsf(v->aturret.yaw);
	float p = y < 0.5f ? -0.3f : (y > 1.0f && y < 1.65f) ? -0.2f : -0.1f;
	if (v->gun1)
		rg_AimStep(ent, &v->agun1, t, false, true, 0, 0, p, 0.785398f, rate);
	int c = v->gun2 ? rg_AimStep(ent, &v->agun2, t, false, true, 0, 0, p, 0.785398f, rate) : RG_AIM_LIMITED;
	return a == RG_AIM_ALIGNED && c == RG_AIM_ALIGNED;
}

// ---------------------------------------------------------------------------------------------------------------
// cannon (§10), machine guns (§11)
// ---------------------------------------------------------------------------------------------------------------
static void veh_FireCannon(edict_t *ent, RgVehicle *v, RgVehCmd *c)
{
	fx::Explosion(fx::TE_EXPLOSION_LARGE, ent->s.origin, 1, 2);
	rg_EffectOnBolt("weapons/othermz/tank", ent, v->cannon, "flash_canon");
	rg_Play(v->cannon, "canon", kPlayOnce);
	rg_Play(v->turret, "turret", kPlayOnce);
	v->turretend = level.time + rg_SeqDuration(v->turret, "turret", kPlayOnce);
	bool moving = false;
	if (ent->ghoulInst) {
		moving = VectorLength(ent->velocity) > 0;
		rg_Play(ent->ghoulInst, moving ? "tank_fire" : "tank_still", kPlayOnce);
	}
	v->fired = true;
	vec3_t pos, fwd, end;
	if (!rg_MuzzleWorld(ent, v->cannon, "flash_canon", pos, fwd)) {
		c->state = 3; // nothing to fire from: finished
		return;
	}
	VectorMA(pos, 4096, fwd, end);
	trace_t tr = gi.trace(pos, vec3_origin, vec3_origin, end, ent, 3);
	if (rg_cvar("rg_trace")) {
		vec3_t p2, f2, l2, u2;
		rg_BoltWorld(ent, v->cannon, "flash_canon", p2, f2, l2, u2);
		rg_Trace("rg: %.1f vehicle %d cannon from (%.1f %.1f %.1f) dir (%.3f %.3f %.3f) hit (%.1f %.1f %.1f) frac %.3f"
		         " left (%.3f %.3f %.3f) up (%.3f %.3f %.3f)\n",
		         level.time, (int)(ent - g_edicts), pos[0], pos[1], pos[2], fwd[0], fwd[1], fwd[2], tr.endpos[0],
		         tr.endpos[1], tr.endpos[2], tr.fraction, l2[0], l2[1], l2[2], u2[0], u2[1], u2[2]);
	}
	if (tr.fraction >= 1.0f) {
		c->state = 3; // missed: finishes at once without an explosion
		return;
	}
	c->remaining = tr.fraction * 4000;
	VectorCopy(tr.endpos, c->impact);
	c->state = 2;
}

static void veh_Explode(edict_t *ent, RgVehCmd *c)
{
	vec3_t p;
	VectorCopy(c->impact, p);
	p[2] += 10;
	rg_EffectAt("weapons/world/rocketexplode", p);
	edict_t *inf = G_Spawn();
	VectorCopy(p, inf->s.origin);
	inf->owner = ent;
	T_RadiusDamage(inf, ent, 130, NULL, 150, 0, DMGF_EXPLOSIVE);
	G_FreeEdict(inf);
	om_ai::ReportNoise(p, 150, 250);
	rg_CameraShake(p, 100, 300);
}

static bool veh_LineBlocked(edict_t *ent, const vec3_t from, const vec3_t to, float threshold)
{
	trace_t tr = gi.trace(from, vec3_origin, vec3_origin, to, ent, 3);
	if (tr.fraction >= threshold)
		return false;
	if (threshold >= 1.0f)
		return true; // snowcat: any hit blocks this muzzle
	if (!tr.ent || tr.ent == world)
		return true;
	om_ai::TypeInfo ti;
	return tr.ent->ai && om_ai::GetTypeInfo(tr.ent->ai->type_code, &ti) && ti.team == 1;
}

static void veh_Spread(vec3_t dir, float s)
{
	for (int i = 0; i < 3; i++)
		dir[i] += frand(-s, s);
	VectorNormalize(dir);
}

static void veh_MachineGun(edict_t *ent, RgVehicle *v)
{
	int T = rg_Toughness();
	float t = level.time - v->burststart;
	float L = 0.2f + frand(0, 0.2f) * T;
	if (v->snowcat) {
		vec3_t p1, d1, p2, d2;
		bool ok1 = v->gun1 && rg_MuzzleWorld(ent, v->gun1, "_flash1", p1, d1);
		bool ok2 = v->gun2 && rg_MuzzleWorld(ent, v->gun2, "_flash1", p2, d2);
		if (!ok2)
			return;
		bool b1 = !ok1 || veh_LineBlocked(ent, p1, v->mgtarget, 1.0f);
		bool b2 = veh_LineBlocked(ent, p2, v->mgtarget, 1.0f);
		if (b1 && b2)
			return;
		if (t <= 0)
			return;
		if (t >= L) {
			v->burststart = level.time + frand(0.3f, 0.6f) * (5 - T);
			return;
		}
		float s = (5 - T) * 0.05f;
		vec3_t d;
		if (ok1 && !b1) {
			VectorCopy(d1, d);
			veh_Spread(d, s);
			rg_FireAttack(12, ent, p1, d);
		} else {
			VectorCopy(d2, d);
			veh_Spread(d, s);
			rg_FireAttack(12, ent, p2, d);
		}
		rg_FireAttack(12, ent, p2, d2);
		if (ok1)
			rg_EffectOnBolt("weapons/othermz/machinegun", ent, v->gun1, "_flash1");
		rg_EffectOnBolt("weapons/othermz/machinegun", ent, v->gun2, "_flash1");
		return;
	}
	vec3_t pos, dir;
	if (!v->mgun || !rg_MuzzleWorld(ent, v->mgun, "flash_machine_gun", pos, dir))
		return;
	if (v->lof && veh_LineBlocked(ent, pos, v->mgtarget, 0.95f))
		return;
	if (t <= 0)
		return;
	if (t >= L) {
		v->burststart = level.time + frand(0.3f, 0.6f) * (5 - T);
		return;
	}
	if (rg_cvar("rg_trace") > 1)
		rg_Trace("rg: %.1f vehicle %d mg shot t %.2f L %.2f\n", level.time, (int)(ent - g_edicts), t, L);
	rg_EffectOnBolt("weapons/othermz/machinegun", ent, v->mgun, "flash_machine_gun");
	veh_Spread(dir, (5 - T) * 0.03f);
	rg_FireAttack(12, ent, pos, dir);
}

// ---------------------------------------------------------------------------------------------------------------
// death (§14)
// ---------------------------------------------------------------------------------------------------------------
static void veh_Wreck(edict_t *ent, RgVehicle *v, edict_t *attacker, bool full)
{
	if (ent->health < -999)
		ent->health = -999;
	ent->s.sound = 0;
	rg_UseTargets(ent, ent->killtarget, ent, ent);
	ent->enemy = attacker;
	if (!v->dead) {
		v->dead = true;
		om_ai::CountKill(ent);
		om_ai::RemoveAllComponents(ent);
		v->qcount = 0;
		v->hascur = false;
		ent->touch = NULL;
		ent->deadflag = DEAD_DEAD;
		if (ent->ai)
			VectorCopy(ent->s.angles, ent->ai->death_angles);
	}
	if (!Body_Of(ent))
		return;
	if (!full) {
		// snowcat self-destruct (§14.2)
		ent->s.effects |= 0x400;
		fx::LargeExplosion(ent->s.origin, 255);
		WhiteOut(ent->s.origin, 5000, 0.9f, 0.5f);
		return;
	}
	VectorClear(ent->velocity);
	VectorClear(ent->avelocity);
	fx::LargeExplosion(ent->s.origin, 255);
	WhiteOut(ent->s.origin, 5000, 0.9f, 0.5f);
	Body_VehicleMoving(ent, false);
	IGhoulInst *parts[3] = {v->turret, v->snowcat ? v->gun1 : v->cannon, v->snowcat ? v->gun2 : v->mgun};
	for (IGhoulInst *p : parts)
		if (p)
			p->SetAllPartsOnOff(false);
	if (!v->snowcat)
		rg_SetSkin(ent->ghoulInst, "tank", "tank_d");
	vec3_t up = {0, 0, 1};
	fx::Debris(ent->s.origin, up, 30, 2, 9, 0, 0, 0, 28);
	rg_SetEffectSlot(ent, "environ/helismoke", "bolt_turret", 1.5f);
	Body_VehicleSetDisabled(ent, true);
	if (v->snowcat)
		Body_VehicleAlternate(ent, true);
}

// ---------------------------------------------------------------------------------------------------------------
// commands (§3) and the think (§6)
// ---------------------------------------------------------------------------------------------------------------
// returns true when the step finished
static bool veh_Drive(edict_t *ent, RgVehicle *v, RgVehCmd *c)
{
	v->reverse = c->s == 1;
	trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, c->v, ent, 3);
	if (rg_cvar("rg_trace") > 1)
		rg_Trace("rg: %.1f vehicle %d drive trace frac %.3f startsolid %d allsolid %d\n", level.time,
		         (int)(ent - g_edicts), tr.fraction, tr.startsolid, tr.allsolid);
	// "blocked" = the trace does not cover the whole way; a box that starts touching the floor is not refused
	// (sib1 `catgarage`: the original's snowcat drives from its spawn position)
	if (tr.fraction < 1.0f)
		return false;
	ent->s.sound = gi.soundindex("Ambient/Models/Tank/tanklp.wav");
	ent->s.sounddata = 0xFB;
	vec3_t d;
	VectorSubtract(c->v, ent->s.origin, d);
	if (v->reverse) {
		d[0] = -d[0];
		d[1] = -d[1];
	}
	float want = anglemod(vectoyaw(d));
	float cur = anglemod(ent->s.angles[YAW]);
	if (cur > 180)
		cur -= 360;
	float diff = want - cur;
	if (fabsf(diff) < 1.5f) {
		ent->s.angles[YAW] = want;
		ent->avelocity[YAW] = 0;
	} else {
		float m = fmodf(diff, 360.0f);
		if (m < 0)
			m += 360;
		ent->avelocity[YAW] = m < 180 ? v->params[0] : -v->params[0];
		return false;
	}
	if (fabsf(c->v[0] - ent->s.origin[0]) < 5 && fabsf(c->v[1] - ent->s.origin[1]) < 5) {
		VectorClear(ent->velocity);
		Body_VehicleMoving(ent, false);
		return true;
	}
	if (ent->velocity[0] == 0 && ent->velocity[1] == 0 && ent->velocity[2] == 0) {
		vec3_t f;
		AngleVectors(ent->s.angles, f, NULL, NULL);
		VectorScale(f, v->reverse ? -v->params[1] : v->params[1], ent->velocity);
		Body_VehicleMoving(ent, true);
	}
	return false;
}

static bool veh_Step(edict_t *ent, RgVehicle *v, RgVehCmd *c)
{
	switch (c->cmd) {
	case 1: return veh_Drive(ent, v, c);
	case 2:
		if (v->snowcat)
			return veh_SnowcatAim(ent, v, c->v);
		if (c->state == 0) {
			if (veh_TankCannonAim(ent, v, c->v) == AIM_NOTYET)
				return false;
			veh_FireCannon(ent, v, c);
			if (c->state == 3)
				return true;
		}
		if (c->state == 2) {
			if (c->remaining > 500) {
				c->remaining -= 500;
				return false;
			}
			c->remaining = 0;
			veh_Explode(ent, c);
			return true;
		}
		return true;
	case 3:
		v->mg_on = c->s != 0;
		return true;
	case 4:
		veh_Wreck(ent, v, ent, !v->snowcat);
		return true;
	case 5:
		if (v->snowcat)
			return veh_SnowcatAim(ent, v, c->v);
		return veh_TankCannonAim(ent, v, c->v) != AIM_NOTYET;
	default:
		return true; // no step: finishes at its first execution
	}
}

static void veh_NextCommand(RgVehicle *v)
{
	v->hascur = false;
	if (v->qcount <= 0)
		return;
	v->cur = v->queue[0];
	memmove(&v->queue[0], &v->queue[1], sizeof(v->queue[0]) * (v->qcount - 1));
	v->qcount--;
	v->hascur = true;
}

void RgVehicleAI::Think(edict_t *ent)
{
	RgVehicle *v = &this->v;
	if (rg_cvar("ai_freeze"))
		return; // the think is not re-armed either (§6 step 1)
	if (Body_VehicleDisabled(ent)) {
		Body_VehicleMoving(ent, false);
		if (!v->snowcat)
			ent->s.sound = 0;
		else
			ent->nextthink = 0;
		v->idle = true;
		return; // no further think is scheduled (§6 step 2)
	}
	edict_t *p1 = rg_Player1();
	if (v->mg_on && Body_VehicleMGHealth(ent) > 0 && p1 && p1->inuse &&
	    !(v->snowcat && (p1->flags & FL_NOTARGET))) {
		bool on = v->snowcat ? veh_SnowcatAim(ent, v, p1->s.origin) : veh_TankMGAim(ent, v, p1->s.origin);
		if (on) {
			VectorCopy(p1->s.origin, v->mgtarget);
			veh_MachineGun(ent, v);
		}
	}
	ent->nextthink = level.time + FRAMETIME;
	if (!v->hascur && v->qcount > 0)
		veh_NextCommand(v);
	if (v->hascur) {
		v->curid = v->cur.id;
		v->stepfinished = Body_Of(ent) && veh_Step(ent, v, &v->cur);
		if (v->stepfinished) {
			if (rg_cvar("rg_trace"))
				rg_Trace("rg: %.1f vehicle %d done id %d cmd %d\n", level.time, (int)(ent - g_edicts), v->cur.id,
				           v->cur.cmd);
			v->lastdone = v->cur.id;
			veh_NextCommand(v);
		}
	}
	// §16: the turret's end of sequence after a cannon shot
	if (v->fired && v->turret && level.time >= v->turretend) {
		v->fired = false;
		bool moving = VectorLength(ent->velocity) > 0;
		if (ent->ghoulInst)
			rg_Play(ent->ghoulInst, v->snowcat ? "snowcat" : "tank", moving ? kPlayLoop : kPlayStatic);
	}
	gi.linkentity(ent);
}

// §13
int RgVehicleAI::Damage(G3Hit &hit)
{
	edict_t *ent = hit.targ;
	RgVehicle *v = &this->v;
	edict_t *att = hit.attacker;
	if (att && att->client && att->client->inv && InvCurWeapon(att->client->inv) == 1) {
		rg_EffectAt("environ/wallspark", hit.point);
		gi.sound(ent, CHAN_VOICE, gi.soundindex(va("impact/surfs/metal%d.wav", gix.irand(1, 3))), 1, ATTN_NORM, 0);
	}
	int damage = hit.damage;
	bool routed = false;
	if (body) {
		if (damage == 0)
			return 0;
		if (hit.dflags & (DMGF_PROJECTILE | DMGF_0x400)) {
			damage = body->Damage(hit);
			routed = true;
			if (damage == 0)
				return 0;
		}
	}
	ent->health -= damage;
	if (ent->health > 0) {
		(void)routed; // the vehicle's pain only updates the body's damage state: no visible effect
		return 1;
	}
	if (!v->snowcat && ent->killtarget && ent->killtarget[0]) {
		// a tank with a killtarget does not die: its targets are used on every lethal hit (§13 step 4)
		rg_UseTargets(ent, ent->killtarget, ent, ent);
		return 1;
	}
	v->killer = att;
	ent->flags |= FL_NO_KNOCKBACK;
	if (v->snowcat)
		ent->takedamage = DAMAGE_NO;
	if (!v->dead)
		veh_Wreck(ent, v, att, true);
	ent->nextthink = level.time + FRAMETIME;
	return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// touch (§15), use (§7), first think (§5), spawn (§1-§2)
// ---------------------------------------------------------------------------------------------------------------
static void veh_TankTouch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	(void)surf;
	if (!other || other == world || other->s.origin[2] + other->mins[2] <= self->s.origin[2])
		return;
	vec3_t up = {0, 0, 1}, pt, n;
	VectorSet(pt, other->s.origin[0], other->s.origin[1], other->absmin[2]);
	if (other->takedamage)
		T_Damage(other, self, self, up, pt, vec3_origin, 10, 100, DMGF_SPREAD, 0);
	VectorSubtract(other->s.origin, self->s.origin, n);
	VectorNormalize(n);
	other->velocity[0] += n[0] * 100;
	other->velocity[1] += n[1] * 100;
	other->velocity[2] += fmaxf(n[2] * 100, 10);
}

static void veh_TankUse(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	(void)activator;
	self->spawnflags ^= 1;
	Body_VehicleSetDisabled(self, (self->spawnflags & 1) != 0);
}

static void veh_FirstThink(edict_t *ent)
{
	ent->nextthink = level.time + FRAMETIME;
	if (ent->ai)
		om_ai::AddDecisionSet(ent->ai, om_ai::SET_VEHICLE);
}

static IGhoulInst *veh_Bolt(edict_t *ent, IGhoulInst *parent, const char *pbolt, const char *dir,
                            const char *model, const char *cbolt)
{
	RgModel *m = rg_FindModel(dir, model, true);
	IGhoulInst *c = m ? rg_AddBolt(ent, parent, pbolt, m, cbolt, NULL, 1.0f) : NULL;
	if (!c) {
		gi.dprintf("vehicle: couldn't add bolt-on %s/%s\n", dir, model);
		return NULL;
	}
	// as for the helicopter: a bolt-on shows its default sequence as a static pose (observed in the original on
	// the tank's gun mount and machine gun; docs/impl/G4.md)
	rg_Play(c, model, kPlayStatic);
	return c;
}

void rg_SpawnVehicle(edict_t *ent)
{
	bool sc = !Q_stricmp(ent->classname, "m_x_snowcat");
	int T = rg_Toughness();
	bool disabled = (ent->spawnflags & 1) != 0;
	// precache (§1)
	gi.soundindex("Ambient/Models/Tank/tanklp.wav");
	for (int i = 1; i <= 3; i++)
		gi.soundindex(va("impact/surfs/metal%d.wav", i));
	static const char *fx[] = {"environ/wallspark", "environ/helismoke", "weapons/othermz/tank",
	                           "weapons/world/rocketexplode", "weapons/othermz/machinegun", "environ/machgun_smoke2"};
	for (const char *f : fx)
		gi.effectindex(f);
	if (sc) {
		gi.effectindex("environ/kf_exhaust01");
		gi.effectindex("environ/snowspray");
	}
	GG_FindModel("effects/explosion", "explode80", true);
	WeaponPrecacheAttack(12);
	// spawn values
	if (sc) {
		if (disabled) {
			VectorSet(ent->mins, -91, -66, -33);
			VectorSet(ent->maxs, 91, 66, 58);
		} else {
			VectorSet(ent->mins, -92, -67, -34);
			VectorSet(ent->maxs, 92, 67, 58);
		}
		ent->health = ent->max_health = 1000;
	} else {
		VectorSet(ent->mins, -152, -85, -44);
		VectorSet(ent->maxs, 152, 85, 80);
		ent->health = ent->max_health = 500 * (T + 1);
	}
	ent->solid = SOLID_BBOX;
	ent->takedamage = DAMAGE_YES;
	ent->movetype = MOVETYPE_DAN;
	ent->flags |= FL_NO_KNOCKBACK;
	ent->clipmask = 0x2030003;
	ent->s.renderfx |= RF_GHOUL;
	ent->pain = NULL;
	ent->die = NULL;
	ent->blocked = NULL;
	if (!sc) {
		ent->touch = veh_TankTouch;
		ent->use = veh_TankUse;
	}
	int code = sc ? om_ai::TYPE_SNOWCAT : om_ai::TYPE_TANK;
	RgChar *ch = rg_NewChar(ent, code, RG_FAM_VEHICLE);
	ch->startset = RG_SET_VEHICLE;
	om_ai::AI *ai = om_ai::CreateAI(ent, code);
	RgVehicle *v = VEH(ent);
	if (!ai || !v)
		return;
	v->ent = ent;
	v->snowcat = sc;
	v->lof = true;
	// model and bolt-ons (§2)
	// hull: enemy/tank/tank (tank.gsq) and enemy/snowcat/body (BODY.ghb; the stock data has no snowcat.gsq)
	RgModel *m = rg_FindModel(sc ? "enemy/snowcat" : "enemy/tank", sc ? "body" : "tank", true);
	IGhoulInst *hull = m ? rg_NewInstance(m, ent) : NULL;
	if (hull) {
		ent->movetype = MOVETYPE_PUSH;
		if (sc) {
			v->turret = veh_Bolt(ent, hull, "DUMMY03", "enemy/snowcat", "turret", "DUMMY03");
			v->gun1 = v->turret ? veh_Bolt(ent, v->turret, "DUMMY01", "enemy/snowcat", "gun", "DUMMY01") : NULL;
			v->gun2 = v->turret ? veh_Bolt(ent, v->turret, "DUMMY02", "enemy/snowcat", "gun", "DUMMY01") : NULL;
			veh_Bolt(ent, hull, "headlight_left", "objects/generic/beam", "beam", "to_headlight");
			veh_Bolt(ent, hull, "headlight_right", "objects/generic/beam", "beam", "to_headlight");
			// the hull model has no sequence `snowcat` (the request finds nothing); the original's hull shows its
			// default sequence `body` looping (sib1 capture), as a new object instance does
			rg_Play(hull, "body", kPlayLoop);
			rg_Play(hull, "snowcat", kPlayOnce);
		} else {
			v->turret = veh_Bolt(ent, hull, "bolt_turret", "enemy/tank", "turret", "to_bolt_turret");
			v->cannon = v->turret ? veh_Bolt(ent, v->turret, "bolt_canon", "enemy/tank", "canon", "to_bolt_canon")
			                      : NULL;
			v->mount = v->turret ? veh_Bolt(ent, v->turret, "bolt_machine_gun", "objects/generic/gun_auto",
			                                "null", "DUMMY01")
			                     : NULL;
			if (v->mount)
				v->mount->SetAllPartsOnOff(false);
			v->mgun = v->mount ? veh_Bolt(ent, v->mount, "DUMMY01", "enemy/tank", "machine_gun",
			                              "to_bolt_machine_gun")
			                   : NULL;
			rg_Play(hull, "tank", kPlayStatic);
		}
		rg_AimInit(&v->aturret, v->turret, hull, sc ? "DUMMY03" : "bolt_turret");
		rg_AimInit(&v->acannon, v->cannon, v->turret, "bolt_canon");
		rg_AimInit(&v->amount, v->mount, v->turret, "bolt_machine_gun");
		rg_AimInit(&v->amgun, v->mgun, v->mount, "DUMMY01");
		rg_AimInit(&v->agun1, v->gun1, v->turret, "DUMMY01");
		rg_AimInit(&v->agun2, v->gun2, v->turret, "DUMMY02");
	}
	// body (SPEC-722 tank / snowcat body): moving and disabled state, aiming parameters
	G3Body *b = Body_Create(ent, sc ? BODY_SNOWCAT : BODY_TANK);
	if (b) {
		b->owner = ai;
		ai->body = b;
	}
	Body_VehicleAimParams(ent, v->params);
	Body_VehicleSetDisabled(ent, disabled);
	if (sc)
		Body_VehicleAlternate(ent, disabled);
	ent->spawnflags = 2; // bit 1 was read; the field is replaced (§1)
	ai->first_think = veh_FirstThink;
	ent->nextthink = level.time + FRAMETIME;
	gi.linkentity(ent);
}

// ---------------------------------------------------------------------------------------------------------------
// script interface (SPEC-750 §12.8, SPEC-AMEND-003 §6) and AITA (§4)
// ---------------------------------------------------------------------------------------------------------------
int rg_VehicleCommand(edict_t *ent, int cmd, edict_t *target, const vec3_t vec, float scalar)
{
	RgVehicle *v = VEH(ent);
	if (!v)
		return -2;
	if (cmd < 1 || cmd > 5 || v->qcount >= VEH_QUEUE)
		return -1; // scripts create a vehicle action only for ids 1-5
	RgVehCmd c;
	memset(&c, 0, sizeof(c));
	c.id = ++v->idcounter;
	c.cmd = cmd;
	if (vec)
		VectorCopy(vec, c.v);
	c.e = target;
	c.s = scalar;
	v->queue[v->qcount++] = c;
	if (rg_cvar("rg_trace"))
		rg_Trace("rg: %.1f vehicle %d queued id %d cmd %d (%.0f %.0f %.0f) s %.1f T %d mg %d rates %.3f %.3f %.3f\n",
		           level.time, (int)(ent - g_edicts), c.id, cmd, c.v[0], c.v[1], c.v[2], scalar, rg_Toughness(),
		           Body_VehicleMGHealth(ent), v->params[2], v->params[3], v->params[4]);
	return c.id;
}

bool rg_VehicleActionDone(edict_t *ent, int id)
{
	RgVehicle *v = VEH(ent);
	return !v || (!v->hascur && v->lastdone == id);
}

static void put32(uint8_t *b, int off, int v) { memcpy(b + off, &v, 4); }
static int get32(const uint8_t *b, int off)
{
	int v;
	memcpy(&v, b + off, 4);
	return v;
}

void rg_VehicleWrite(edict_t *ent, roster_chunk_write_t w, void *ctx)
{
	RgVehicle *v = VEH(ent);
	if (!v)
		return;
	uint8_t b[52];
	memset(b, 0, sizeof(b));
	put32(b, 0, v->idcounter);
	put32(b, 4, v->curid);
	put32(b, 8, v->killer ? G_EdictNum(v->killer) : -1);
	put32(b, 12, v->removal);
	b[16] = v->reverse;
	b[32] = v->mg_on;
	b[33] = v->lof;
	b[34] = v->stepfinished;
	memcpy(b + 36, &v->burststart, 4);
	memcpy(b + 40, v->mgtarget, 12);
	w(ctx, "AITA", b, sizeof(b));
}

bool rg_VehicleRead(edict_t *ent, roster_chunk_read_t r, void *ctx)
{
	RgVehicle *v = VEH(ent);
	uint8_t b[52];
	if (!v || !r(ctx, "AITA", b, sizeof(b)))
		return false;
	v->idcounter = get32(b, 0);
	v->curid = get32(b, 4);
	int k = get32(b, 8);
	v->killer = (k >= 0 && k < globals.num_edicts) ? &g_edicts[k] : NULL;
	v->removal = get32(b, 12);
	v->reverse = b[16] != 0;
	v->mg_on = b[32] != 0;
	v->lof = b[33] != 0;
	v->stepfinished = b[34] != 0;
	memcpy(&v->burststart, b + 36, 4);
	memcpy(v->mgtarget, b + 40, 12);
	return true;
}
