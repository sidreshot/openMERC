/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * =======================================================================
 *
 * Player movement for game import #81.
 *
 * OpenMerc (ENG2): the movement itself is the shared SPEC-314 routine
 * (common/sof_pmove.c), the same code the client predicts with. This file
 * only adapts the game's 0x13C-byte block: state, command, callbacks
 * (trace 0xF0, point contents 0xF4, ground-entity-is-client 0xF8) and the
 * stagger value (0xFC) in; state, command, box, view, ground, water and
 * touches out.
 *
 * Traces: the block's trace callback (the game's #52 with the player as
 * pass entity and its own mask, SPEC-314 section 19). sv_pmove_gametrace 0
 * uses the engine trace with the moving client found by origin instead
 * (the pre-ENG2 behaviour). The type 2 spectator slide always uses the
 * engine trace with the world-only mask (SPEC-313 section 8).
 *
 * Implements: SPEC-001 (#81), SPEC-314 sections 4, 5, 19, SPEC-313 s8
 *
 * =======================================================================
 */

#include "header/server.h"
#include "../common/header/sof_pmove.h"
#include "../ghoul/ghoul_host.h"
#include "../common/header/omrec.h"

/* om_autorecord (common/header/omrec.h): last move of the local player; sv_omrec_local is set by
   SV_GameClientThink while edict 1 runs its think */
omrec_pm_t omrec_pm;
int sv_omrec_local;

const char *
SV_OmRec_MapName(void)
{
	return (sv.state == ss_game) ? sv.name : "";
}

typedef int (*sv_groundisclient_t)(sof_edict_t *ent);

typedef struct
{
	sof_pmove_t *block;
	const edict_t *mover;
	int gametrace;
} svpm_ctx_t;

static cvar_t *sv_pmove_gametrace;

static void
SVPM_ConvertTrace(const sof_trace_t *in, sofpm_trace_t *out)
{
	out->allsolid = in->allsolid;
	out->startsolid = in->startsolid;
	out->fraction = in->fraction;
	VectorCopy(in->endpos, out->endpos);
	VectorCopy(in->plane.normal, out->normal);
	out->surfflags = in->surface ? in->surface->flags : 0;
	out->contents = in->contents;
	out->ent = in->ent;
}

static void
SVPM_Trace(void *ctx, sofpm_trace_t *out, const float *start, const float *mins,
		const float *maxs, const float *end, int mask)
{
	svpm_ctx_t *c = ctx;
	sof_trace_t tr;

	memset(&tr, 0, sizeof(tr));

	if (mask == SOFPM_MASK_SPECTATOR)
	{
		SV_TraceSoF(start, mins, maxs, end, NULL, mask, &tr);
	}
	else if (c->gametrace && c->block->trace)
	{
#if defined(__i386__) && !defined(_MSC_VER)
		tr = c->block->trace(start, mins, maxs, end); /* see sof_pmove_t.trace */
#else
		c->block->trace(&tr, start, mins, maxs, end);
#endif
	}
	else
	{
		SV_TraceSoF(start, mins, maxs, end, c->mover, mask, &tr);
	}

	SVPM_ConvertTrace(&tr, out);
}

static int
SVPM_PointContents(void *ctx, const float *p)
{
	svpm_ctx_t *c = ctx;

	if (c->block->pointcontents)
	{
		return c->block->pointcontents(p);
	}

	return SV_PointContents(p);
}

static int
SVPM_GroundIsClient(void *ctx, void *ent)
{
	svpm_ctx_t *c = ctx;
	sv_groundisclient_t fn;
	int num;

	memcpy(&fn, &c->block->sof_extra[0], sizeof(fn));

	if (fn)
	{
		return fn((sof_edict_t *)ent);
	}

	/* no callback (engine-driven moves): "entity number 1..maxclients" */
	num = NUM_FOR_EDICT((edict_t *)ent);
	return (num >= 1) && (num <= svs.numclients);
}

static void
SVPM_DevPrint(void *ctx, const char *msg)
{
	(void)ctx;
	Com_DPrintf("%s", msg);
}

/* the entity moving, for the engine trace (sv_pmove_gametrace 0): the
   client whose think is running, else the client entity whose position
   the block carries (game-driven moves, bots) */
static const edict_t *
SVPM_FindMover(const sof_pmove_t *pm)
{
	int i, k;

	if (sv_player && sv_player->client)
	{
		for (k = 0; k < 3; k++)
		{
			if (fabs(sv_player->s.origin[k] * 8 - pm->s.origin[k]) > 2)
			{
				break;
			}
		}

		if (k == 3)
		{
			return sv_player;
		}
	}

	for (i = 1; i <= svs.numclients; i++)
	{
		edict_t *e = EDICT_NUM(i);

		if (!e->inuse || !e->client)
		{
			continue;
		}

		for (k = 0; k < 3; k++)
		{
			if (fabs(e->s.origin[k] * 8 - pm->s.origin[k]) > 2)
			{
				break;
			}
		}

		if (k == 3)
		{
			return e;
		}
	}

	return sv_player;
}

static void
SVPM_DebugDump(const sof_pmove_t *block)
{
	static int dumped;
	const unsigned *w = (const unsigned *)block;
	char line[256];
	int i, n = 0;

	if (!sv_hostdebug || !((int)sv_hostdebug->value & SV_DBG_PMOVE) || (dumped >= 8))
	{
		return;
	}

	dumped++;
	Com_Printf("Pmove block #%d:\n", dumped);
	line[0] = 0;

	for (i = 0; i < (int)(sizeof(sof_pmove_t) / 4); i++)
	{
		n += snprintf(line + n, sizeof(line) - n, "%08x ", w[i]);

		if ((i & 7) == 7)
		{
			Com_Printf("  %03x: %s\n", (i - 7) * 4, line);
			line[0] = 0;
			n = 0;
		}
	}

	if (n)
	{
		Com_Printf("  ...: %s\n", line);
	}
}

void
SV_Pmove(sof_pmove_t *block)
{
	svpm_ctx_t ctx;
	sofpm_t pm;
	int i;

	if (!sv_pmove_gametrace)
	{
		sv_pmove_gametrace = Cvar_Get("sv_pmove_gametrace", "1", 0);
	}

	SVPM_DebugDump(block);

	ctx.block = block;
	ctx.gametrace = (int)sv_pmove_gametrace->value;
	ctx.mover = ctx.gametrace ? NULL : SVPM_FindMover(block);

	memset(&pm, 0, sizeof(pm));
	pm.s = block->s;
	pm.cmd = block->cmd;
	VectorCopy(block->mins, pm.mins);
	VectorCopy(block->maxs, pm.maxs);
	pm.snapinitial = block->snapinitial;
	memcpy(&pm.stagger, &block->sof_extra[sizeof(sv_groundisclient_t)], sizeof(pm.stagger)); /* after the callback */
	pm.sizematters = OMGhoul_SizeMatters();

	pm.trace = SVPM_Trace;
	pm.pointcontents = SVPM_PointContents;
	pm.groundisclient = SVPM_GroundIsClient;
	pm.devprint = SVPM_DevPrint;
	pm.debug = sv_hostdebug && ((int)sv_hostdebug->value & 64);
	pm.ctx = &ctx;
	pm.mask = ((block->s.pm_type == SOF_PM_DEAD) || (block->s.pm_type == SOF_PM_GIB)) ?
		SOFPM_MASK_DEADSOLID : SOFPM_MASK_PLAYERSOLID;

	SoFPM_Move(&pm);

	block->s = pm.s;
	block->cmd = pm.cmd;
	block->numtouch = pm.numtouch;

	for (i = 0; i < pm.numtouch; i++)
	{
		block->touchents[i] = pm.touchents[i];
	}

	VectorCopy(pm.viewangles, block->viewangles);
	block->viewheight = pm.viewheight;
	VectorCopy(pm.mins, block->mins);
	VectorCopy(pm.maxs, block->maxs);
	block->groundentity = pm.groundentity;
	block->watertype = pm.watertype;
	block->waterlevel = pm.waterlevel;

	if (sv_omrec_local)
	{
		omrec_pm.count++;
		omrec_pm.pm_type = pm.s.pm_type;

		for (i = 0; i < 3; i++)
		{
			omrec_pm.origin[i] = pm.s.origin[i] * 0.125f;
			omrec_pm.velocity[i] = pm.s.velocity[i] * 0.125f;
			omrec_pm.probenormal[i] = pm.probenormal[i];
		}

		omrec_pm.pm_flags = pm.s.pm_flags;
		omrec_pm.pm_time = pm.s.pm_time;
		omrec_pm.msec = pm.cmd.msec;
		omrec_pm.forwardmove = pm.cmd.forwardmove;
		omrec_pm.sidemove = pm.cmd.sidemove;
		omrec_pm.upmove = pm.cmd.upmove;
		omrec_pm.buttons = pm.cmd.buttons;
		omrec_pm.onground = pm.groundentity != NULL;
		omrec_pm.waterlevel = pm.waterlevel;
		omrec_pm.watertype = pm.watertype;
		omrec_pm.stagger = pm.stagger;
	}

	if (sv_hostdebug && ((int)sv_hostdebug->value & 64))
	{
		Com_Printf("pmove: type %d msec %d origin (%d %d %d) vel (%d %d %d) flags 0x%02x time %d "
				"ground %i water %d/%d touches %d probe n.z %.3f\n", pm.s.pm_type, pm.cmd.msec, pm.s.origin[0],
				pm.s.origin[1], pm.s.origin[2], pm.s.velocity[0], pm.s.velocity[1], pm.s.velocity[2],
				pm.s.pm_flags, pm.s.pm_time,
				pm.groundentity ? NUM_FOR_EDICT((edict_t *)pm.groundentity) : -1, pm.waterlevel,
				pm.watertype, pm.numtouch, pm.probenormal[2]);
	}
}
