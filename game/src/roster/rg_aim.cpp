// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: the aim step that turns a bolted part (turret, gun, crew head) towards a
// point, shared by the helicopter and the ground vehicles.
//
// The target is expressed in the frame of the parent's bolt (bolt matrix of the parent including the entity
// transform, pose mode, through parents) as yaw about the bolt's vertical axis and pitch (elevation), relative to
// the part's rest orientation. Only enabled axes change; limits with tolerance 0.0005 rad give "limited", a step
// capped at the rate gives "turning", otherwise "aligned"; wanted changes of at most 0.01 rad are ignored.
//
// Implements: SPEC-731 Part B §9.1, SPEC-791 §11 (turning a bolted part)
#include "rg_local.h"

using namespace ghoul;

void rg_AimInit(RgAimPart *p, IGhoulInst *inst, IGhoulInst *parent, const char *pbolt, float scale)
{
	memset(p, 0, sizeof(*p));
	p->inst = inst;
	p->parent = parent;
	strncpy(p->pbolt, pbolt ? pbolt : "", sizeof(p->pbolt) - 1);
	p->scale = scale;
}

static float rg_wrap(float a)
{
	while (a > (float)M_PI)
		a -= 2 * (float)M_PI;
	while (a <= -(float)M_PI)
		a += 2 * (float)M_PI;
	return a;
}

int rg_AimStep(edict_t *ent, RgAimPart *p, const vec3_t target, bool yawon, bool pitchon, float yawmin,
               float yawmax, float pitchmin, float pitchmax, float rate)
{
	if (!ent || !p || !p->inst || !p->parent || (!yawon && !pitchon))
		return RG_AIM_LIMITED;
	GhoulID b = rg_Bolt(p->parent, p->pbolt);
	if (!b)
		return RG_AIM_LIMITED;
	Matrix4 m;
	memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1;
	p->parent->GetBoltMatrix(level.time, &m, b, kMatrixWithTransform | kMatrixPose, true);
	// target in entity space, then in the bolt frame
	vec3_t te, d, local;
	rg_WorldToEntity(ent, target, te, true);
	VectorSet(d, te[0] - m.m[3][0], te[1] - m.m[3][1], te[2] - m.m[3][2]);
	for (int i = 0; i < 3; i++) {
		vec3_t ax = {m.m[i][0], m.m[i][1], m.m[i][2]};
		float l2 = DotProduct(ax, ax);
		local[i] = l2 > 0 ? DotProduct(d, ax) / l2 : 0;
	}
	float wantyaw = atan2f(local[1], local[0]);
	float wantpitch = atan2f(local[2], sqrtf(local[0] * local[0] + local[1] * local[1]));
	const float tol = 0.0005f;
	bool limited = false, capped = false;
	const float oldyaw = p->yaw, oldpitch = p->pitch;
	bool unlimitedyaw = yawmin <= -3999 && yawmax >= 3999;
	if (yawon) {
		float w = wantyaw;
		if (!unlimitedyaw) {
			if (w > yawmax + tol) {
				w = yawmax;
				limited = true;
			} else if (w < yawmin - tol) {
				w = yawmin;
				limited = true;
			}
		}
		float dd = unlimitedyaw ? rg_wrap(w - p->yaw) : w - p->yaw;
		if (fabsf(dd) <= 0.01f)
			dd = 0;
		if (rate < 3999 && fabsf(dd) > rate) {
			dd = dd > 0 ? rate : -rate;
			capped = true;
		}
		p->yaw = unlimitedyaw ? rg_wrap(p->yaw + dd) : p->yaw + dd;
	}
	if (pitchon) {
		float w = wantpitch;
		if (w > pitchmax + tol) {
			w = pitchmax;
			limited = true;
		} else if (w < pitchmin - tol) {
			w = pitchmin;
			limited = true;
		}
		float dd = w - p->pitch;
		if (fabsf(dd) <= 0.01f)
			dd = 0;
		if (rate < 3999 && fabsf(dd) > rate) {
			dd = dd > 0 ? rate : -rate;
			capped = true;
		}
		p->pitch += dd;
	}
	// a part that has not turned keeps its untouched transform (the original sends none for its helicopter
	// gunner head and gun mount while they stay aligned)
	if (p->yaw != oldyaw || p->pitch != oldpitch)
		rg_SetPartRotation(p->inst, p->yaw, p->pitch, p->scale);
	if (rg_cvar("rg_trace") > 2)
		rg_Trace("rg: aim %s bolt (%.1f %.1f %.1f) local (%.1f %.1f %.1f) want y %.4f p %.4f now y %.4f p %.4f %s%s\n",
		         p->pbolt, m.m[3][0], m.m[3][1], m.m[3][2], local[0], local[1], local[2], wantyaw, wantpitch, p->yaw,
		         p->pitch, limited ? "L" : "", capped ? "C" : "");
	if (limited)
		return RG_AIM_LIMITED;
	if (capped)
		return RG_AIM_TURNING;
	return RG_AIM_ALIGNED;
}
