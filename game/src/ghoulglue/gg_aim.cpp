// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - Ghoul glue placement helpers and aim controller (G9).
// Implements: SPEC-791 Behaviour 10, 11
#include <math.h>

#include "gg_internal.h"

void GG_PlaceRelative(edict_t *source, edict_t *target, float forward, float right, float up)
{
	if (!source || !target)
		return;
	VectorCopy(source->s.origin, target->s.origin);
	VectorCopy(source->s.angles, target->s.angles);
	target->s.origin[2] += up;
	// the second angle is the entity-state yaw-offset field, not the yaw (SPEC-791 B10, original quirk)
	vec3_t a, f, r;
	a[0] = source->s.angles[0];
	a[1] = source->s.yawofs;
	a[2] = source->s.angles[2];
	AngleVectors(a, f, r, NULL);
	VectorMA(target->s.origin, forward, f, target->s.origin);
	VectorMA(target->s.origin, right, r, target->s.origin);
}

bool GG_InFrontCone(edict_t *ent, const vec3_t point, float halfangledeg)
{
	if (!ent || !point)
		return false;
	vec3_t f, d;
	AngleVectors(ent->s.angles, f, NULL, NULL);
	f[2] = 0;
	VectorNormalize(f);
	VectorSubtract(point, ent->s.origin, d);
	d[2] = 0;
	VectorNormalize(d);
	float c = (float)cos(halfangledeg * M_PI / 180.0);
	float dot = DotProduct(f, d);
	gg_conprint("Threshold %f  Dot %f\n", c, dot);
	return dot >= c;
}

//-----------------------------------------------------------------------------------------------------------
// aim controller

#define AIM_UNLIMITED 4000.0f
#define AIM_LIMIT_TOL 0.0005f
#define AIM_DEADZONE 0.01f

void GG_AimInit(GGAim *a)
{
	if (!a)
		return;
	memset(a, 0, sizeof(*a));
	a->throughparents = true;
}

static float anglemod_rad(float a)
{
	while (a > (float)M_PI)
		a -= 2.0f * (float)M_PI;
	while (a <= -(float)M_PI)
		a += 2.0f * (float)M_PI;
	return a;
}

// clamp the desired angle into [lo, hi] (+-4000 = unlimited) and the change to +-step; result code as B11
static float aim_axis(float current, float desired, float lo, float hi, float step, int *result, bool wrap)
{
	if (lo > -AIM_UNLIMITED + 1 && desired < lo - AIM_LIMIT_TOL) {
		desired = lo;
		*result = 3;
	}
	if (hi < AIM_UNLIMITED - 1 && desired > hi + AIM_LIMIT_TOL) {
		desired = hi;
		*result = 3;
	}
	float delta = desired - current;
	if (wrap)
		delta = anglemod_rad(delta);
	if (step < AIM_UNLIMITED - 1) {
		if (delta > step) {
			delta = step;
			if (*result < 2)
				*result = 2;
		} else if (delta < -step) {
			delta = -step;
			if (*result < 2)
				*result = 2;
		}
	}
	return delta;
}

int GG_AimEntity(GGAim *a)
{
	if (!a || !a->ent || !a->ent->ghoulInst || (!a->yawon && !a->pitchon))
		return 3;
	edict_t *e = a->ent;
	vec3_t d;
	VectorSubtract(a->target, e->s.origin, d);
	float dyaw = (float)atan2(d[1], d[0]);
	float dpitch = (float)atan2(d[2], sqrt(d[0] * d[0] + d[1] * d[1]));
	int result = 1;
	if (a->yawon) {
		float cur = e->s.angles[YAW] * (float)M_PI / 180.0f;
		float delta = aim_axis(anglemod_rad(cur), dyaw, a->yawmin, a->yawmax, a->maxstep, &result, true);
		e->s.angles[YAW] = (cur + delta) * 180.0f / (float)M_PI;
		a->yaw = cur + delta;
	}
	if (a->pitchon) {
		// entity pitch follows Quake (positive = down); the controller works with elevation (UNVERIFIED signs)
		float cur = -e->s.angles[PITCH] * (float)M_PI / 180.0f;
		float delta = aim_axis(cur, dpitch, a->pitchmin, a->pitchmax, a->maxstep, &result, false);
		e->s.angles[PITCH] = -(cur + delta) * 180.0f / (float)M_PI;
		a->pitch = cur + delta;
	}
	return result;
}

// p' = p * m (row vectors), 3x3 part and translation
static void mat_point(const ghoul::Matrix4 &m, const float *p, float *out)
{
	for (int c = 0; c < 3; c++)
		out[c] = p[0] * m.m[0][c] + p[1] * m.m[1][c] + p[2] * m.m[2][c] + m.m[3][c];
}

// inverse of a rigid matrix (rotation rows orthogonal, possibly scaled)
static void mat_invert_rigid(const ghoul::Matrix4 &m, ghoul::Matrix4 &out)
{
	memset(&out, 0, sizeof(out));
	for (int r = 0; r < 3; r++) {
		float l2 = m.m[r][0] * m.m[r][0] + m.m[r][1] * m.m[r][1] + m.m[r][2] * m.m[r][2];
		if (l2 < 1e-12f)
			l2 = 1;
		for (int c = 0; c < 3; c++)
			out.m[c][r] = m.m[r][c] / l2;
	}
	for (int c = 0; c < 3; c++)
		out.m[3][c] = -(m.m[3][0] * out.m[0][c] + m.m[3][1] * out.m[1][c] + m.m[3][2] * out.m[2][c]);
	out.m[3][3] = 1;
}

static void mat_mul(const ghoul::Matrix4 &a, const ghoul::Matrix4 &b, ghoul::Matrix4 &out)
{
	ghoul::Matrix4 r;
	memset(&r, 0, sizeof(r));
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++)
				r.m[i][j] += a.m[i][k] * b.m[k][j];
	r.flags = 0;
	out = r;
}

int GG_AimPart(GGAim *a)
{
	if (!a || !a->ent || !a->inst || !a->parent || (!a->yawon && !a->pitchon))
		return 3;
	edict_t *e = a->ent;
	// bolt of the parent in entity space (mode 5: pose including the instance transform)
	ghoul::Matrix4 bolt;
	memset(&bolt, 0, sizeof(bolt));
	for (int i = 0; i < 4; i++)
		bolt.m[i][i] = 1;
	bolt.flags = 1;
	a->parent->GetBoltMatrix(level.time, &bolt, a->parentbolt, 5, a->throughparents);
	// target in entity space
	vec3_t d, f, r, u, local;
	VectorSubtract(a->target, e->s.origin, d);
	AngleVectors(e->s.angles, f, r, u);
	local[0] = DotProduct(d, f);
	local[1] = -DotProduct(d, r);
	local[2] = DotProduct(d, u);
	// into the bolt's frame
	ghoul::Matrix4 inv;
	mat_invert_rigid(bolt, inv);
	float p[3];
	mat_point(inv, local, p);
	float dyaw = (float)atan2(p[1], p[0]);
	float dpitch = (float)atan2(p[2], sqrt(p[0] * p[0] + p[1] * p[1]));
	// current orientation of the part from its transform's forward row
	ghoul::Matrix4 t;
	a->inst->GetTransform(&t);
	float cyaw = (float)atan2(t.m[0][1], t.m[0][0]);
	float cpitch = (float)atan2(t.m[0][2], sqrt(t.m[0][0] * t.m[0][0] + t.m[0][1] * t.m[0][1]));
	int result = 1;
	float ychange = 0, pchange = 0;
	if (a->yawon)
		ychange = aim_axis(cyaw, dyaw, a->yawmin, a->yawmax, a->maxstep, &result, true);
	if (a->pitchon)
		pchange = aim_axis(cpitch, dpitch, a->pitchmin, a->pitchmax, a->maxstep, &result, false);
	if (fabs(ychange) < AIM_DEADZONE)
		ychange = 0;
	if (fabs(pchange) < AIM_DEADZONE)
		pchange = 0;
	if (ychange != 0 || pchange != 0) {
		ghoul::Matrix4 rz, ry;
		memset(&rz, 0, sizeof(rz));
		memset(&ry, 0, sizeof(ry));
		float cy = (float)cos(ychange), sy = (float)sin(ychange);
		rz.m[0][0] = cy; rz.m[0][1] = sy;
		rz.m[1][0] = -sy; rz.m[1][1] = cy;
		rz.m[2][2] = 1; rz.m[3][3] = 1;
		// pitch: rotate the forward axis towards up about the part's own y axis
		float cp = (float)cos(pchange), sp = (float)sin(pchange);
		ry.m[0][0] = cp; ry.m[0][2] = sp;
		ry.m[1][1] = 1;
		ry.m[2][0] = -sp; ry.m[2][2] = cp;
		ry.m[3][3] = 1;
		ghoul::Matrix4 tmp;
		mat_mul(ry, rz, tmp);
		mat_mul(tmp, t, t);
		t.flags = 0;
		a->inst->SetTransform(&t);
	}
	a->yaw = cyaw + ychange;
	a->pitch = cpitch + pchange;
	return result;
}
