/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f client view for protocol 33: the SoF
 * player-state record kept per frame, the view computation (interpolation,
 * prediction, field of view and remote-camera rules, weapon kick,
 * night-vision jitter, near clip, cinematic-freeze cvar, frame-rate
 * smoothing) and the view-weapon sway. The view computation follows Quake
 * II's CL_CalcViewValues with the SoF differences.
 *
 * Implements: SPEC-310 (sections 12, 13), SPEC-202 section 4 (record)
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "fx/cl_fx.h"

static sofps_t sofps[UPDATE_BACKUP];
static sofps_t sofps_zero;
static int sof_server_dm;
static cvar_t *cl_minfps;
static cvar_t *sc_framerate;
static float sof_fps = 30.0f;          /* smoothed frame rate (refdef value 0) */
static int sof_last_shown_pmtype;

sofps_t *
CL_SoF_PlayerExt(int serverframe)
{
	return &sofps[serverframe & UPDATE_MASK];
}

const sofps_t *
CL_SoF_CurrentPS(void)
{
	const sofps_t *p;

	if (!CL_SoF_Active())
	{
		return &sofps_zero;
	}

	p = &sofps[cl.frame.serverframe & UPDATE_MASK];

	if (!p->valid || (p->serverframe != cl.frame.serverframe))
	{
		return &sofps_zero;
	}

	return p;
}

const sofps_t *
CL_SoF_PreviousPS(void)
{
	const sofps_t *p = &sofps[(cl.frame.serverframe - 1) & UPDATE_MASK];
	const frame_t *of = &cl.frames[(cl.frame.serverframe - 1) & UPDATE_MASK];

	if (!CL_SoF_Active() || !p->valid || (p->serverframe != cl.frame.serverframe - 1) ||
		!of->valid || (of->serverframe != cl.frame.serverframe - 1))
	{
		return NULL;
	}

	return p;
}

int
CL_SoF_ServerDeathmatch(void)
{
	return sof_server_dm;
}

void
CL_SoF_SetServerDeathmatch(int dm)
{
	sof_server_dm = dm;

	/* SPEC-203: the client copies the byte into its own deathmatch cvar
	   (with a local server both are the same cvar) */
	if (!Com_ServerState())
	{
		Cvar_ForceSet("deathmatch", va("%i", dm));
	}

	memset(sofps, 0, sizeof(sofps));
	CL_SoF_ResetViewWeapon();
	CL_Strings_LevelChanged();
	HUD_Ignore_Init();
}

/* SPEC-310 12.8 and 12.9: near clip from the pm_type of the frame shown
   before, the cinematic-freeze value copied to cl_cinematicfreeze */
void
CL_SoF_PlayerExtParsed(sofps_t *ps, unsigned flags)
{
	if (flags & 0x00000001) /* pm_type present */
	{
		Cvar_SetValue("r_nearclipdist",
				((sof_last_shown_pmtype == 2) || (sof_last_shown_pmtype == 7)) ? 1 : 4);
	}

	if (flags & 0x00400000) /* cinematic freeze present */
	{
		Cvar_SetValue("cl_cinematicfreeze", (float)ps->cinfreeze);
	}
}

/* SPEC-310 12.10: frame rate smoothing and the effect detail factor */
float
CL_SoF_EffectDetail(void)
{
	float f = sof_fps / ((sc_framerate && (sc_framerate->value > 0)) ? sc_framerate->value : 30.0f);

	return (f > 1.0f) ? 1.0f : f;
}

float
CL_SoF_SmoothedFps(void)
{
	return sof_fps;
}

static void
CL_SoF_UpdateFps(void)
{
	float minfps, ft = cls.rframetime;

	if (!cl_minfps)
	{
		cl_minfps = Cvar_Get("cl_minfps", "5", 0);
		sc_framerate = Cvar_Get("sc_framerate", "30", 0);
	}

	minfps = (cl_minfps->value > 0) ? cl_minfps->value : 5;

	if (ft > 1.0f / minfps)
	{
		ft = 1.0f / minfps;
	}

	if (ft > 0)
	{
		float r = 1.0f / ft;

		if (r < minfps)
		{
			r = minfps;
		}

		sof_fps = 0.9f * sof_fps + 0.1f * r;
	}

	if (sof_fps < minfps)
	{
		sof_fps = minfps;
	}
}

/* OpenMerc (ENG2): the field of view of the player state (interpolated as in step 6) before the
   horplus widening. The original renders this value as is; the rules that test the view fov
   (view-weapon note effects and their fire sounds only at 95, the sniper alternate flash, the
   mouse zoom factor) read this one, so that a non-4:3 window behaves as a 4:3 one. */
static float sof_game_fov = 95;

float
CL_SoF_GameFov(void)
{
	return sof_game_fov;
}

/* horizontal field of view kept at 4:3 for other aspect ratios (horplus) */
static float
CL_SoF_AdaptFov(float fov, float w, float h)
{
	const float pi = (float)M_PI;

	if ((w <= 0) || (h <= 0) || !horplus || !horplus->value || (w * 3 == h * 4))
	{
		return fov;
	}

	return atanf(tanf(fov / 360.0f * pi) * (w / h * 0.75f)) / pi * 360.0f;
}

/* ---- view-weapon sway (SPEC-310 section 13) ----------------------------- */

static struct
{
	qboolean init;
	vec3_t prev;
	float smooth;    /* S */
	float phase;     /* phi */
	float prev_vz;
	float dip, dip_target;
	vec3_t offset;   /* translation in model space */
} sway;

void
CL_SoF_ResetViewWeapon(void)
{
	memset(&sway, 0, sizeof(sway));
}

/* The sway translation for this frame, to be applied to the view-weapon
   Ghoul instance by the code that draws it. */
const float *
CL_SoF_ViewWeaponSway(void)
{
	return sway.offset;
}

static void
CL_SoF_UpdateSway(const vec3_t origin, int guntype)
{
	float ft = cls.rframetime, s, vz, a;
	vec3_t d;

	if (ft <= 0)
	{
		return;
	}

	if (!sway.init)
	{
		VectorCopy(origin, sway.prev);
		sway.init = true;
	}

	/* 1 */
	d[0] = origin[0] - sway.prev[0];
	d[1] = origin[1] - sway.prev[1];
	s = sqrtf(d[0] * d[0] + d[1] * d[1]) / ft;

	if (s > 300)
	{
		s = 300;
	}

	vz = (origin[2] - sway.prev[2]) / ft;
	VectorCopy(origin, sway.prev);

	/* 2 */
	if (s < ft * sway.smooth * 200)
	{
		s = (3 * sway.smooth + s) / 4;
	}

	sway.smooth = s;

	/* 3 */
	sway.phase += sqrtf(s / 300.0f) * 5.1f * ft;
	a = s * 0.0046667f;

	if ((guntype == 3) || (guntype == 4))
	{
		a *= 0.5f;
	}

	/* 4: landing dip */
	if ((fabsf(vz) < 0.0005f) && (sway.prev_vz < -30))
	{
		sway.dip_target = (fabsf(sway.prev_vz) - 30) * -0.0216667f;

		if (sway.dip_target < -1.3f)
		{
			sway.dip_target = -1.3f;
		}
	}

	sway.prev_vz = vz;

	if (sway.dip_target != 0)
	{
		sway.dip -= 12 * ft;

		if (sway.dip <= sway.dip_target)
		{
			sway.dip = sway.dip_target;
			sway.dip_target = 0;
		}
	}
	else if (sway.dip < 0)
	{
		sway.dip += 4 * ft;

		if (sway.dip > 0)
		{
			sway.dip = 0;
		}
	}

	/* 5 */
	sway.offset[0] = 0;
	sway.offset[1] = 0.9f * a * sinf(sway.phase);
	sway.offset[2] = fabsf(0.4f * a * sinf(sway.phase)) + sway.dip;
}

/* ---- the view (SPEC-310 section 12) ------------------------------------ */

static struct
{
	int type, ent;
} remote_prev;

/*
 * Sets cl.refdef view values for a SoF server (replaces Quake II's
 * CL_CalcViewValues for protocol 33)
 */
void
CL_SoF_CalcViewValues(void)
{
	int i, pmtype;
	float lerp, backlerp, ifov;
	frame_t *oldframe;
	player_state_t *ps, *ops;
	const sofps_t *x = CL_SoF_CurrentPS();
	const sofps_t *ox = CL_SoF_PreviousPS();
	qboolean predict;

	/* find the previous frame to interpolate from */
	ps = &cl.frame.playerstate;
	i = (cl.frame.serverframe - 1) & UPDATE_MASK;
	oldframe = &cl.frames[i];

	if ((oldframe->serverframe != cl.frame.serverframe - 1) || !oldframe->valid)
	{
		oldframe = &cl.frame; /* previous frame was dropped or invalid */
		ox = NULL;
	}

	ops = &oldframe->playerstate;

	if (!ox)
	{
		ox = x;
	}

	/* see if the player entity was teleported this frame */
	if ((abs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256 * 8) ||
		(abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256 * 8) ||
		(abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256 * 8))
	{
		ops = ps; /* don't interpolate */
	}

	lerp = cl_paused->value ? 1.0f : cl.lerpfrac;

	/* demo playback shows the pm_type 5 view (SPEC-202 section 4) */
	pmtype = cl.attractloop ? 5 : x->pm_type;
	sof_last_shown_pmtype = pmtype;

	/* 1: origin; prediction is off with cl_predict 0 or pmove flag 0x80 */
	predict = cl_predict->value && !(ps->pmove.pm_flags & 0x80) && !cl.attractloop;

	if (predict)
	{
		unsigned delta;

		backlerp = 1.0f - lerp;

		for (i = 0; i < 3; i++)
		{
			cl.refdef.vieworg[i] = cl.predicted_origin[i] + ops->viewoffset[i]
				+ lerp * (ps->viewoffset[i] - ops->viewoffset[i])
				- backlerp * cl.prediction_error[i];
		}

		/* smooth out stair climbing */
		delta = cls.realtime - cl.predicted_step_time;

		if (delta < 100)
		{
			cl.refdef.vieworg[2] -= cl.predicted_step * (100 - delta) * 0.01f;
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			cl.refdef.vieworg[i] = ops->pmove.origin[i] * 0.125f +
				ops->viewoffset[i] + lerp * (ps->pmove.origin[i] * 0.125f +
						ps->viewoffset[i] - (ops->pmove.origin[i] * 0.125f +
							ops->viewoffset[i]));
		}
	}

	/* angles: predicted (the client's own) for pm_type 0..2, else interpolated */
	if ((pmtype >= 0) && (pmtype <= 2))
	{
		for (i = 0; i < 3; i++)
		{
			cl.refdef.viewangles[i] = cl.viewangles[i] +
				SHORT2ANGLE(ps->pmove.delta_angles[i]);
		}
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			cl.refdef.viewangles[i] = LerpAngle(ops->viewangles[i],
					ps->viewangles[i], lerp);
		}
	}

	if (cl_kickangles->value)
	{
		for (i = 0; i < 3; i++)
		{
			cl.refdef.viewangles[i] += LerpAngle(ops->kick_angles[i],
					ps->kick_angles[i], lerp);
		}
	}

	/* 2: weapon kick (first component), interpolated */
	cl.refdef.viewangles[PITCH] += ox->weapon_kick + lerp * (x->weapon_kick - ox->weapon_kick);

	/* 4: night-vision jitter */
	if (!strcmp(CL_SoF_ConfigString(15), "1") && (ps->fov == 95) && (x->rdflags & 0x4))
	{
		cl.refdef.vieworg[2] += frandk() - 0.5f;
	}

	/* 5: remote camera; a change of type or entity snaps */
	if (x->remote_type)
	{
		vec3_t org, ang;

		for (i = 0; i < 3; i++)
		{
			org[i] = x->remote_origin[i] * 0.125f;
			ang[i] = SHORT2ANGLE(x->remote_angles[i]);
		}

		if ((remote_prev.type == x->remote_type) && (remote_prev.ent == x->remote_ent) &&
			(ox != x) && (ox->remote_type == x->remote_type) && (ox->remote_ent == x->remote_ent))
		{
			for (i = 0; i < 3; i++)
			{
				float po = ox->remote_origin[i] * 0.125f;

				cl.refdef.vieworg[i] = po + lerp * (org[i] - po);
				cl.refdef.viewangles[i] = LerpAngle(SHORT2ANGLE(ox->remote_angles[i]), ang[i], lerp);
			}
		}
		else
		{
			VectorCopy(org, cl.refdef.vieworg);
			VectorCopy(ang, cl.refdef.viewangles);
		}
	}

	remote_prev.type = x->remote_type;
	remote_prev.ent = x->remote_ent;

	AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

	/* 6: field of view, interpolated only for the scope camera or without
	   a remote view in both frames */
	if ((x->remote_type == 16) || ((x->remote_type == 0) && (ox->remote_type == 0)))
	{
		ifov = ops->fov + lerp * (ps->fov - ops->fov);

		/* SPEC-AMEND-009 D1: zoom sound on a fov change between the frames
		   (no client weapon prediction, so no predicted fov) */
		if (ps->fov != ops->fov)
		{
			CLFX_ZoomChanged();
		}
	}
	else
	{
		ifov = ps->fov;
	}

	if ((ifov < 1) || (ifov > 179))
	{
		Com_Error(ERR_DROP, "Bad fov: %f", ifov);
	}

	sof_game_fov = ifov;
	cl.refdef.fov_x = CL_SoF_AdaptFov(ifov, (float)scr_vrect.width, (float)scr_vrect.height);

	/* 7: blend from the player state (cl_testblend and cl_blend in V_RenderView) */
	for (i = 0; i < 4; i++)
	{
		cl.refdef.blend[i] = ps->blend[i];
	}

	/* 10 */
	CL_SoF_UpdateFps();

	/* 13: view-weapon sway from the interpolated player origin */
	{
		vec3_t p;

		for (i = 0; i < 3; i++)
		{
			p[i] = ops->pmove.origin[i] * 0.125f +
				lerp * (ps->pmove.origin[i] - ops->pmove.origin[i]) * 0.125f;
		}

		CL_SoF_UpdateSway(p, x->gun_type);
	}
}
