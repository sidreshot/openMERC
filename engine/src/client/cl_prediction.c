/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * This file implements interpolation between two frames. This is used
 * to smooth down network play
 *
 * =======================================================================
 *
 * OpenMerc: SoF servers pack entity bounding boxes differently.
 * Implements: SPEC-202
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "../common/header/sof_pmove.h"
#include "../ghoul/ghoul_host.h"

void
CL_CheckPredictionError(void)
{
	int frame;
	int delta[3];
	int i;
	int len;

	/* ENG2: SoF's no-prediction flag is 0x80 (SPEC-314 section 20) */
	if (!cl_predict->value ||
		(cl.frame.playerstate.pmove.pm_flags &
		 (CL_SoF_Active() ? SOFPMF_NO_PREDICTION : PMF_NO_PREDICTION)))
	{
		return;
	}

	/* calculate the last usercmd_t we sent that the server has processed */
	frame = cls.netchan.incoming_acknowledged;
	frame &= (CMD_BACKUP - 1);

	/* compare what the server returned with what we had predicted it to be */
	VectorSubtract(cl.frame.playerstate.pmove.origin,
			cl.predicted_origins[frame], delta);

	/* save the prediction error for interpolation */
	len = abs(delta[0]) + abs(delta[1]) + abs(delta[2]);

	/* 80 world units */
	if (len > 640)
	{
		/* a teleport or something */
		VectorClear(cl.prediction_error);
	}
	else
	{
		if (cl_showmiss->value && (delta[0] || delta[1] || delta[2]))
		{
			Com_Printf("prediction miss on %i: %i\n", cl.frame.serverframe,
					delta[0] + delta[1] + delta[2]);
		}

		VectorCopy(cl.frame.playerstate.pmove.origin,
				cl.predicted_origins[frame]);

		/* save for error itnerpolation */
		for (i = 0; i < 3; i++)
		{
			cl.prediction_error[i] = delta[i] * 0.125f;
		}
	}
}

void
CL_ClipMoveToEntities(vec3_t start, vec3_t mins, vec3_t maxs,
		vec3_t end, trace_t *tr)
{
	int i, x, zd, zu;
	trace_t trace;
	int headnode;
	float *angles;
	entity_state_t *ent;
	int num;
	cmodel_t *cmodel;
	vec3_t bmins, bmaxs;

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		num = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		ent = &cl_parse_entities[num];

		if (!ent->solid)
		{
			continue;
		}

		if (ent->number == cl.playernum + 1)
		{
			continue;
		}

		if (ent->solid == 31)
		{
			/* special value for bmodel */
			cmodel = cl.model_clip[ent->modelindex];

			if (!cmodel)
			{
				continue;
			}

			headnode = cmodel->headnode;
			angles = ent->angles;
		}
		else if (CL_SoF_Active())
		{
			/* OpenMerc: SoF packs whole units (SPEC-202 2.3) */
			CL_SoF_SolidBox(ent->solid, bmins, bmaxs);

			headnode = CM_HeadnodeForBox(bmins, bmaxs);
			angles = vec3_origin;
		}
		else
		{
			/* encoded bbox */
			x = 8 * (ent->solid & 31);
			zd = 8 * ((ent->solid >> 5) & 31);
			zu = 8 * ((ent->solid >> 10) & 63) - 32;

			bmins[0] = bmins[1] = -(float)x;
			bmaxs[0] = bmaxs[1] = (float)x;
			bmins[2] = -(float)zd;
			bmaxs[2] = (float)zu;

			headnode = CM_HeadnodeForBox(bmins, bmaxs);
			angles = vec3_origin; /* boxes don't rotate */
		}

		if (tr->allsolid)
		{
			return;
		}

		trace = CM_TransformedBoxTrace(start, end,
				mins, maxs, headnode, MASK_PLAYERSOLID,
				ent->origin, angles);

		if (trace.allsolid || trace.startsolid ||
			(trace.fraction < tr->fraction))
		{
			trace.ent = (struct edict_s *)ent;

			if (tr->startsolid)
			{
				*tr = trace;
				tr->startsolid = true;
			}
			else
			{
				*tr = trace;
			}
		}
	}
}

trace_t
CL_PMTrace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t t;

	/* check against world */
	t = CM_BoxTrace(start, end, mins, maxs, 0, MASK_PLAYERSOLID);

	if (t.fraction < 1.0)
	{
		t.ent = (struct edict_s *)1;
	}

	/* check all other solid models */
	CL_ClipMoveToEntities(start, mins, maxs, end, &t);

	return t;
}

int
CL_PMpointcontents(const vec3_t point)
{
	int i;
	entity_state_t *ent;
	int num;
	cmodel_t *cmodel;
	int contents;

	contents = CM_PointContents(point, 0);

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		num = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		ent = &cl_parse_entities[num];

		if (ent->solid != 31) /* special value for bmodel */
		{
			continue;
		}

		cmodel = cl.model_clip[ent->modelindex];

		if (!cmodel)
		{
			continue;
		}

		contents |= CM_TransformedPointContents(point, cmodel->headnode,
				ent->origin, ent->angles);
	}

	return contents;
}


/* ---- OpenMerc ENG2: SoF movement prediction (SPEC-314 section 20/21,
   SPEC-AMEND-010 M4/M6) through the shared routine of sof_pmove.c ---- */

static void
CL_SoFPM_Trace(void *ctx, sofpm_trace_t *out, const float *start,
		const float *mins, const float *maxs, const float *end, int mask)
{
	trace_t t;

	(void)ctx;
	(void)mask; /* the client always traces with 0x02010003 (section 21) */

	t = CL_PMTrace((float *)start, (float *)mins, (float *)maxs, (float *)end);

	out->allsolid = t.allsolid;
	out->startsolid = t.startsolid;
	out->fraction = t.fraction;
	VectorCopy(t.endpos, out->endpos);
	VectorCopy(t.plane.normal, out->normal);
	out->surfflags = t.surface ? t.surface->flags : 0;
	out->contents = t.contents;
	out->ent = t.ent;
}

static int
CL_SoFPM_PointContents(void *ctx, const float *p)
{
	(void)ctx;
	return CL_PMpointcontents(p);
}

/* "a real entity whose number is 1..configstring 31 (maxclients)"; the
   world placeholder is (void *)1, frame entities are entity_state_t */
static int
CL_SoFPM_GroundIsClient(void *ctx, void *ent)
{
	int n, maxcl;

	(void)ctx;

	if (!ent || (ent == (void *)1))
	{
		return 0;
	}

	n = ((entity_state_t *)ent)->number;
	maxcl = atoi(cl.configstrings[CS_MAXCLIENTS]);

	return (n >= 1) && (n <= maxcl);
}

static cvar_t *cl_om_predlog;

static void
CL_PredictLog(const sofpm_t *pm, const sofps_t *x, int ncmds, int nmoves)
{
	if (!cl_om_predlog)
	{
		cl_om_predlog = Cvar_Get("cl_om_predlog", "0", 0);
	}

	if (!cl_om_predlog->value)
	{
		return;
	}

	/* real time, frame time, predicted origin, view origin of the previous
	   rendered frame, commands and moves replayed, server frame, movement
	   type and flags, cinematic freeze, speed byte, received origin */
	Com_Printf("PRED %d %.4f %.3f %.3f %.3f view %.3f %.3f %.3f cmds %d sf %d"
			" mv %d pt %d fl %d cf %d q %d srv %.3f %.3f %.3f\n",
			cls.realtime, cls.rframetime,
			pm ? pm->s.origin[0] * 0.125f : cl.predicted_origin[0],
			pm ? pm->s.origin[1] * 0.125f : cl.predicted_origin[1],
			pm ? pm->s.origin[2] * 0.125f : cl.predicted_origin[2],
			cl.refdef.vieworg[0], cl.refdef.vieworg[1], cl.refdef.vieworg[2],
			ncmds, cl.frame.serverframe, nmoves, x ? x->pm_type : -1,
			cl.frame.playerstate.pmove.pm_flags, x ? x->cinfreeze : -1,
			x ? x->pm_extra : -1,
			cl.frame.playerstate.pmove.origin[0] * 0.125f,
			cl.frame.playerstate.pmove.origin[1] * 0.125f,
			cl.frame.playerstate.pmove.origin[2] * 0.125f);
}

static void
CL_SoF_PredictMovement(void)
{
	const sofps_t *x = CL_SoF_CurrentPS();
	pmove_state_t *ps = &cl.frame.playerstate.pmove;
	int ack, current, frame, lastseq, i, q, flags, nmoves = 0;
	sofpm_t pm;
	sof_usercmd_t sc;
	usercmd_t *cmd;
	int step;

	/* section 20: nothing at all while the cinematic-freeze byte is set */
	if (x && x->cinfreeze)
	{
		CL_PredictLog(NULL, x, 0, 0);
		return;
	}

	flags = ps->pm_flags;

	if (!cl_predict->value || (flags & SOFPMF_NO_PREDICTION) || cl.attractloop)
	{
		for (i = 0; i < 3; i++)
		{
			cl.predicted_angles[i] = cl.viewangles[i] + SHORT2ANGLE(ps->delta_angles[i]);
		}

		return;
	}

	ack = cls.netchan.incoming_acknowledged;
	current = cls.netchan.outgoing_sequence;

	if (current - ack >= CMD_BACKUP)
	{
		if (cl_showmiss->value)
		{
			Com_Printf("exceeded CMD_BACKUP\n");
		}

		return;
	}

	memset(&pm, 0, sizeof(pm));
	pm.s.pm_type = x ? x->pm_type : 0;
	VectorCopy(ps->origin, pm.s.origin);
	VectorCopy(ps->velocity, pm.s.velocity);
	pm.s.pm_flags = (uint8_t)ps->pm_flags;
	pm.s.pm_time = (uint8_t)ps->pm_time;
	pm.s.gravity = ps->gravity;
	VectorCopy(ps->delta_angles, pm.s.delta_angles);
	q = x ? (x->pm_extra & 255) : 255;
	pm.s.move_scale = (uint8_t)q;
	pm.snapinitial = 0;
	pm.stagger = 0;
	pm.sizematters = OMGhoul_SizeMatters();
	pm.trace = CL_SoFPM_Trace;
	pm.pointcontents = CL_SoFPM_PointContents;
	pm.groundisclient = CL_SoFPM_GroundIsClient;
	pm.mask = SOFPM_MASK_PLAYERSOLID;

	lastseq = -1;

	/* every sent, unacknowledged command, oldest first (M4); the slot at
	   outgoing_sequence is the next, still empty one */
	while (++ack <= current)
	{
		frame = ack & (CMD_BACKUP - 1);
		cmd = &cl.cmds[frame];

		if (!cmd->msec)
		{
			continue;
		}

		memset(&sc, 0, sizeof(sc));
		sc.msec = cmd->msec;
		sc.buttons = cmd->buttons;
		sc.lightlevel = cmd->lightlevel;
		VectorCopy(cmd->angles, sc.angles);
		sc.forwardmove = cmd->forwardmove;
		sc.sidemove = cmd->sidemove;
		sc.upmove = cmd->upmove;

		/* the received flags decide the run doubling (section 20) */
		nmoves += SoFPM_RunCommand(&pm, &sc, flags, q);

		VectorCopy(pm.s.origin, cl.predicted_origins[frame]);
		lastseq = ack;
	}

	if (lastseq < 0)
	{
		CL_PredictLog(NULL, x, 0, 0);
		return;
	}

	/* step smoothing: final z against the z stored for the command before
	   the last predicted one (section 20) */
	{
		step = pm.s.origin[2] - cl.predicted_origins[(lastseq - 1) & (CMD_BACKUP - 1)][2];

		if ((step > 63) && (step < 160) && (pm.s.pm_flags & SOFPMF_ON_GROUND))
		{
			cl.predicted_step = step * 0.125f;
			cl.predicted_step_time = cls.realtime - (int)(cls.nframetime * 500);
		}
	}

	CL_PredictLog(&pm, x, current - cls.netchan.incoming_acknowledged, nmoves);

	for (i = 0; i < 3; i++)
	{
		cl.predicted_origin[i] = pm.s.origin[i] * 0.125f;
	}

	VectorCopy(pm.viewangles, cl.predicted_angles);
}

/*
 * Sets cl.predicted_origin and cl.predicted_angles
 */
void
CL_PredictMovement(void)
{
	int ack, current;
	int frame;
	usercmd_t *cmd;
	pmove_t pm;
	int i;
	int step;
	vec3_t tmp;

	if (cls.state != ca_active)
	{
		return;
	}

	if (cl_paused->value)
	{
		return;
	}

	if (CL_SoF_Active())
	{
		CL_SoF_PredictMovement();
		return;
	}

	if (!cl_predict->value ||
		(cl.frame.playerstate.pmove.pm_flags & PMF_NO_PREDICTION))
	{
		/* just set angles */
		for (i = 0; i < 3; i++)
		{
			cl.predicted_angles[i] = cl.viewangles[i] + SHORT2ANGLE(
					cl.frame.playerstate.pmove.delta_angles[i]);
		}

		return;
	}

	ack = cls.netchan.incoming_acknowledged;
	current = cls.netchan.outgoing_sequence;

	/* if we are too far out of date, just freeze */
	if (current - ack >= CMD_BACKUP)
	{
		if (cl_showmiss->value)
		{
			Com_Printf("exceeded CMD_BACKUP\n");
		}

		return;
	}

	/* copy current state to pmove */
	memset (&pm, 0, sizeof(pm));
	pm.trace = CL_PMTrace;
	pm.pointcontents = CL_PMpointcontents;
	pm_airaccelerate = atof(cl.configstrings[CS_AIRACCEL]);
	pm.s = cl.frame.playerstate.pmove;

	/* run frames */
	while (++ack <= current)
	{
		frame = ack & (CMD_BACKUP - 1);
		cmd = &cl.cmds[frame];

		// Ignore null entries
		if (!cmd->msec)
		{
			continue;
		}

		pm.cmd = *cmd;
		Pmove(&pm);

		/* save for debug checking */
		VectorCopy(pm.s.origin, cl.predicted_origins[frame]);
	}

	// step is used for movement prediction on stairs
	// (so moving up/down stairs is smooth)
	step = pm.s.origin[2] - (int)(cl.predicted_origin[2] * 8);
	VectorCopy(pm.s.velocity, tmp);

	if (((step > 62 && step < 66) || (step > 94 && step < 98) || (step > 126 && step < 130))
		&& !VectorCompare(tmp, vec3_origin)
		&& (pm.s.pm_flags & PMF_ON_GROUND))
	{
		cl.predicted_step = step * 0.125f;
		cl.predicted_step_time = cls.realtime - (int)(cls.nframetime * 500);
	}

	/* OpenMerc (ENG2): per-frame trace for jitter measurements: real time,
	   frame msec, predicted origin, the view origin of the previous rendered
	   frame and the commands replayed */
	{
		static cvar_t *cl_om_predlog;

		if (!cl_om_predlog)
		{
			cl_om_predlog = Cvar_Get("cl_om_predlog", "0", 0);
		}

		if (cl_om_predlog->value)
		{
			Com_Printf("PRED %d %.4f %.3f %.3f %.3f view %.3f %.3f %.3f cmds %d sf %d\n", cls.realtime,
					cls.rframetime, pm.s.origin[0] * 0.125f, pm.s.origin[1] * 0.125f,
					pm.s.origin[2] * 0.125f, cl.refdef.vieworg[0], cl.refdef.vieworg[1],
					cl.refdef.vieworg[2], current - cls.netchan.incoming_acknowledged,
					cl.frame.serverframe);
		}
	}
	/* copy results out for rendering */
	cl.predicted_origin[0] = pm.s.origin[0] * 0.125f;
	cl.predicted_origin[1] = pm.s.origin[1] * 0.125f;
	cl.predicted_origin[2] = pm.s.origin[2] * 0.125f;

	VectorCopy(pm.viewangles, cl.predicted_angles);
}

