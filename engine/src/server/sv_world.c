/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
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
 * World query functions: entity linking, area queries, traces.
 *
 * OpenMerc: SoF 1.07f linking rules (rotated brush bounds, swinging
 * doors, linked non-solid entities, 32 clusters), the 56-byte trace
 * result with texinfo records, the filtered trace, the fog-region
 * distance, and the per-map data the server keeps besides the collision
 * model (map check string, texinfo records, leaf regions).
 *
 * Implements: SPEC-313 s4-s7, SPEC-202 s2.3 (solid packing),
 *             SPEC-203 (map check string), SPEC-001 (#52-#59, #25, #26, #80)
 *
 * =======================================================================
 */

#include "header/server.h"

#define AREA_DEPTH 4
#define AREA_NODES 32
#define MAX_TOTAL_ENT_LEAFS 128
#define MAX_FILTER_HITS 1024

#define STRUCT_FROM_LINK(l, t, m) ((t *)((byte *)l - (byte *)&(((t *)NULL)->m)))
#define EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l, edict_t, area)

typedef struct areanode_s
{
	int axis; /* -1 = leaf node */
	float dist;
	struct areanode_s *children[2];
	sof_link_t trigger_edicts;
	sof_link_t solid_edicts;
} areanode_t;

static areanode_t sv_areanodes[AREA_NODES];
static int sv_numareanodes;

static const float *area_mins, *area_maxs;
static edict_t **area_list;
static int area_count, area_maxcount;
static int area_type;

/* per-map data besides the collision model */
static sof_surface_t sv_nullsurface;
static short *sv_leafregion;
static int sv_numleafs;
static int *sv_modelfaceflags; /* per inline model: OR of its faces' texinfo flags */
static int sv_nummodels;
static byte *sv_regions;
static int sv_numregions;

/* the collision model's surfaces (common/collision.c) */
extern mapsurface_t map_surfaces[];
extern int numtexinfo;

static int SV_HullForEntity(const edict_t *ent);

static void
ClearLink(sof_link_t *l)
{
	l->prev = l->next = l;
}

static void
RemoveLink(sof_link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

static void
InsertLinkBefore(sof_link_t *l, sof_link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

static qboolean
SV_IsBrushSolid(int solid)
{
	return (solid == SOF_SOLID_BSP) || (solid == SOF_SOLID_BSP2);
}

/*
 * Builds a uniformly subdivided tree for the given world size
 */
static areanode_t *
SV_CreateAreaNode(int depth, vec3_t mins, vec3_t maxs)
{
	areanode_t *anode;
	vec3_t size;
	vec3_t mins1, maxs1, mins2, maxs2;

	anode = &sv_areanodes[sv_numareanodes];
	sv_numareanodes++;

	ClearLink(&anode->trigger_edicts);
	ClearLink(&anode->solid_edicts);

	if (depth == AREA_DEPTH)
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}

	VectorSubtract(maxs, mins, size);

	anode->axis = (size[0] > size[1]) ? 0 : 1;
	anode->dist = 0.5f * (maxs[anode->axis] + mins[anode->axis]);
	VectorCopy(mins, mins1);
	VectorCopy(mins, mins2);
	VectorCopy(maxs, maxs1);
	VectorCopy(maxs, maxs2);

	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

	anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
	anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

	return anode;
}

void
SV_ClearWorld(void)
{
	memset(sv_areanodes, 0, sizeof(sv_areanodes));
	sv_numareanodes = 0;

	if (sv.models[1])
	{
		SV_CreateAreaNode(0, sv.models[1]->mins, sv.models[1]->maxs);
	}
}

void
SV_UnlinkEdict(edict_t *ent)
{
	if (!ent->area.prev)
	{
		return; /* not linked in anywhere */
	}

	RemoveLink(&ent->area);
	ent->area.prev = ent->area.next = NULL;
}

/* SPEC-202 s2.3: whole units, 32 bits */
static int
SV_PackSolid(const edict_t *ent)
{
	int x, y, zd, zu;

	if ((ent->solid == SOF_SOLID_BBOX) && !(ent->svflags & SOF_SVF_DEADMONSTER))
	{
		x = Q_clamp((int)ent->maxs[0], 1, 255);
		y = Q_clamp((int)ent->maxs[1], 1, 255);
		zd = Q_clamp((int)(-ent->mins[2]), 1, 255);
		zu = Q_clamp((int)(ent->maxs[2] + 128), 1, 255);

		return x | (y << 8) | (zd << 16) | (zu << 24);
	}

	if (SV_IsBrushSolid(ent->solid))
	{
		return 31;
	}

	return 0;
}

/* SPEC-313 s6: the swinging-door box covers the hinge-to-edge segment */
static void
SV_SwingingDoorBounds(edict_t *ent)
{
	int u, v, w, i, best;
	float a, cand_r[4], cand_a[4], r, th, du, dv;
	qboolean negv = false;

	if (ent->svflags & SOF_SVF_ROTATE_ROLL)
	{
		w = 0; u = 1; v = 2; a = ent->s.angles[2];
	}
	else if (ent->svflags & SOF_SVF_ROTATE_PITCH)
	{
		w = 1; u = 0; v = 2; a = ent->s.angles[0];
		negv = true;
	}
	else
	{
		w = 2; u = 0; v = 1; a = ent->s.angles[1];
	}

	cand_r[0] = ent->mins[u]; cand_a[0] = a;
	cand_r[1] = ent->mins[v]; cand_a[1] = a + 90;
	cand_r[2] = ent->maxs[u]; cand_a[2] = a;
	cand_r[3] = ent->maxs[v]; cand_a[3] = a + 90;

	best = 0;

	for (i = 1; i < 4; i++)
	{
		if (abs((int)cand_r[i]) > abs((int)cand_r[best]))
		{
			best = i;
		}
	}

	r = cand_r[best];
	th = cand_a[best] * (float)M_PI / 180.0f;
	du = r * cosf(th);
	dv = r * sinf(th);

	if (negv)
	{
		dv = -dv;
	}

	ent->absmin[u] = ent->s.origin[u] + ((du < 0) ? du : 0);
	ent->absmax[u] = ent->s.origin[u] + ((du < 0) ? 0 : du);
	ent->absmin[v] = ent->s.origin[v] + ((dv < 0) ? dv : 0);
	ent->absmax[v] = ent->s.origin[v] + ((dv < 0) ? 0 : dv);
	ent->absmin[w] = ent->s.origin[w] + ent->mins[w];
	ent->absmax[w] = ent->s.origin[w] + ent->maxs[w];
}

void
SV_LinkEdict(edict_t *ent)
{
	areanode_t *node;
	int leafs[MAX_TOTAL_ENT_LEAFS];
	int clusters[MAX_TOTAL_ENT_LEAFS];
	int num_leafs, topnode, i, j;
	qboolean rotated;

	if (ent->area.prev)
	{
		SV_UnlinkEdict(ent); /* unlink from old position */
	}

	if (ent == ge->edicts)
	{
		return; /* don't add the world */
	}

	if (!ent->inuse)
	{
		return;
	}

	/* set the size */
	VectorSubtract(ent->maxs, ent->mins, ent->size);

	/* encode the size into the entity_state for client prediction */
	ent->s.solid = SV_PackSolid(ent);

	/* set the abs box */
	rotated = SV_IsBrushSolid(ent->solid) &&
		(ent->s.angles[0] || ent->s.angles[1] || ent->s.angles[2]);

	if (rotated && (ent->svflags & (SOF_SVF_ROTATE_ROLL | SOF_SVF_ROTATE_PITCH |
				SOF_SVF_ROTATE_YAW)))
	{
		SV_SwingingDoorBounds(ent);
	}
	else if (rotated)
	{
		float max = 0, v;

		for (i = 0; i < 3; i++)
		{
			v = (float)fabs(ent->mins[i]);

			if (v > max)
			{
				max = v;
			}

			v = (float)fabs(ent->maxs[i]);

			if (v > max)
			{
				max = v;
			}
		}

		max *= 1.75f;

		for (i = 0; i < 3; i++)
		{
			ent->absmin[i] = ent->s.origin[i] - max;
			ent->absmax[i] = ent->s.origin[i] + max;
		}
	}
	else
	{
		VectorAdd(ent->s.origin, ent->mins, ent->absmin);
		VectorAdd(ent->s.origin, ent->maxs, ent->absmax);
	}

	/* because movement is clipped an epsilon away from an actual edge,
	   we must fully check even when bounding boxes don't quite touch */
	for (i = 0; i < 3; i++)
	{
		ent->absmin[i] -= 1;
		ent->absmax[i] += 1;
	}

	/* link to PVS leafs */
	ent->num_clusters = 0;
	ent->areanum = 0;
	ent->areanum2 = 0;

	/* get all leafs, including solids */
	num_leafs = CM_BoxLeafnums(ent->absmin, ent->absmax,
			leafs, MAX_TOTAL_ENT_LEAFS, &topnode);

	/* set areas */
	for (i = 0; i < num_leafs; i++)
	{
		int area;

		clusters[i] = CM_LeafCluster(leafs[i]);
		area = CM_LeafArea(leafs[i]);

		if (area)
		{
			/* doors may legally straggle two areas,
			   but nothing should evern need more than that */
			if (ent->areanum && (ent->areanum != area))
			{
				ent->areanum2 = area;
			}
			else
			{
				ent->areanum = area;
			}
		}
	}

	if (num_leafs >= MAX_TOTAL_ENT_LEAFS)
	{
		/* assume we missed some leafs, and mark by headnode */
		ent->num_clusters = -1;
		ent->headnode = topnode;
	}
	else
	{
		ent->num_clusters = 0;

		for (i = 0; i < num_leafs; i++)
		{
			if (clusters[i] == -1)
			{
				continue; /* not a visible leaf */
			}

			for (j = 0; j < i; j++)
			{
				if (clusters[j] == clusters[i])
				{
					break;
				}
			}

			if (j == i)
			{
				if (ent->num_clusters == SOF_MAX_ENT_CLUSTERS)
				{
					/* assume we missed some leafs, and mark by headnode */
					ent->num_clusters = -1;
					ent->headnode = topnode;
					break;
				}

				ent->clusternums[ent->num_clusters++] = clusters[i];
			}
		}
	}


	ent->linkcount++;

	/* SPEC-313 s6: non-solid entities stay out unless flagged 0x80 */
	if ((ent->solid == SOF_SOLID_NOT) && !(ent->svflags & SOF_SVF_LINKNONSOLID))
	{
		return;
	}

	/* find the first node that the ent's box crosses */
	node = sv_areanodes;

	while (1)
	{
		if (node->axis == -1)
		{
			break;
		}

		if (ent->absmin[node->axis] > node->dist)
		{
			node = node->children[0];
		}
		else if (ent->absmax[node->axis] < node->dist)
		{
			node = node->children[1];
		}
		else
		{
			break; /* crosses the node */
		}
	}

	/* link it in */
	if (ent->solid == SOF_SOLID_TRIGGER)
	{
		InsertLinkBefore(&ent->area, &node->trigger_edicts);
	}
	else
	{
		InsertLinkBefore(&ent->area, &node->solid_edicts);
	}
}

static void
SV_AreaEdicts_r(areanode_t *node)
{
	sof_link_t *l, *next, *start;
	edict_t *check;

	/* touch linked edicts */
	if (area_type == AREA_SOLID)
	{
		start = &node->solid_edicts;
	}
	else
	{
		start = &node->trigger_edicts;
	}

	for (l = start->next; l != start; l = next)
	{
		next = l->next;
		check = EDICT_FROM_AREA(l);

		if ((check->solid == SOF_SOLID_NOT) && !(check->svflags & SOF_SVF_LINKNONSOLID))
		{
			continue; /* deactivated */
		}

		if ((check->absmin[0] > area_maxs[0]) ||
			(check->absmin[1] > area_maxs[1]) ||
			(check->absmin[2] > area_maxs[2]) ||
			(check->absmax[0] < area_mins[0]) ||
			(check->absmax[1] < area_mins[1]) ||
			(check->absmax[2] < area_mins[2]))
		{
			continue; /* not touching */
		}

		if (area_count == area_maxcount)
		{
			Com_Printf("SV_AreaEdicts: MAXCOUNT\n");
			return;
		}

		area_list[area_count] = check;
		area_count++;
	}

	if (node->axis == -1)
	{
		return; /* terminal node */
	}

	/* recurse down both sides */
	if (area_maxs[node->axis] > node->dist)
	{
		SV_AreaEdicts_r(node->children[0]);
	}

	if (area_mins[node->axis] < node->dist)
	{
		SV_AreaEdicts_r(node->children[1]);
	}
}

int
SV_AreaEdicts(const vec3_t mins, const vec3_t maxs, edict_t **list,
		int maxcount, int areatype)
{
	area_mins = mins;
	area_maxs = maxs;
	area_list = list;
	area_maxcount = maxcount;
	area_type = areatype;
	area_count = 0;

	SV_AreaEdicts_r(sv_areanodes);

	area_mins = 0;
	area_maxs = 0;
	area_list = 0;
	area_maxcount = 0;
	area_type = 0;

	return area_count;
}

int
SV_PointContents(const vec3_t p)
{
	edict_t *touch[SOF_MAX_EDICTS];
	int i, num, contents;

	/* get base contents from world */
	contents = CM_PointContents(p, sv.models[1]->headnode);

	/* or in contents from all the other entities */
	num = SV_AreaEdicts(p, p, touch, SOF_MAX_EDICTS, AREA_SOLID);

	for (i = 0; i < num; i++)
	{
		edict_t *hit = touch[i];
		int headnode;

		if (hit->solid == SOF_SOLID_NOT)
		{
			continue;
		}

		/* might intersect, so do an exact clip */
		headnode = SV_HullForEntity(hit);
		contents |= CM_TransformedPointContents(p, headnode,
				hit->s.origin, hit->s.angles);
	}

	return contents;
}

/* ---------------------------------------------------------------------
 * traces
 * --------------------------------------------------------------------- */

/* the texinfo record of a collision-model surface */
sof_surface_t *
SV_SurfaceFromQ2(const csurface_t *s)
{
	ptrdiff_t i;

	if (!s || !sv.surfaces)
	{
		return &sv_nullsurface;
	}

	/* range check on addresses first: a trace that hit nothing carries the collision model's own null
	   surface, which is not in map_surfaces, and subtracting pointers into different objects is undefined
	   (GCC's exact division turned it into an in-range index and a wild pointer in i386 release builds) */
	{
		uintptr_t a = (uintptr_t)s, lo = (uintptr_t)map_surfaces;

		if ((a < lo) || ((a - lo) % sizeof(mapsurface_t)) != 0)
		{
			return &sv_nullsurface;
		}

		i = (ptrdiff_t)((a - lo) / sizeof(mapsurface_t));
	}

	if ((i >= sv.numsurfaces) || (i >= numtexinfo))
	{
		return &sv_nullsurface;
	}

	return &sv.surfaces[i];
}

int
SV_SurfaceIndex(const sof_surface_t *s)
{
	if (!s || !sv.surfaces || (s < sv.surfaces) || (s >= sv.surfaces + sv.numsurfaces))
	{
		return -1;
	}

	return (int)(s - sv.surfaces);
}

static void
SV_ConvertTrace(const trace_t *in, sof_trace_t *out)
{
	memset(out, 0, sizeof(*out));
	out->allsolid = in->allsolid ? 1 : 0;
	out->startsolid = in->startsolid ? 1 : 0;
	out->fraction = in->fraction;
	VectorCopy(in->endpos, out->endpos);
	VectorCopy(in->plane.normal, out->plane.normal);
	out->plane.dist = in->plane.dist;
	out->plane.type = in->plane.type;
	out->plane.signbits = in->plane.signbits;
	out->surface = SV_SurfaceFromQ2(in->surface);
	out->contents = in->contents;
	out->ent = (sof_edict_t *)in->ent;
	out->leafnum = -1; /* not provided by the collision model (OPEN_QUESTIONS I1b) */
}

/*
 * Returns a headnode that can be used for testing or clipping an
 * object of mins/maxs size.
 */
static int
SV_HullForEntity(const edict_t *ent)
{
	if (SV_IsBrushSolid(ent->solid))
	{
		int mi = ent->s.modelindex;
		const cmodel_t *model = ((mi >= 0) && (mi < SOF_MAX_MODELS)) ?
			sv.models[mi] : NULL;

		return model ? model->headnode : 0;
	}

	return CM_HeadnodeForBox((float *)ent->mins, (float *)ent->maxs);
}

static trace_t
SV_ClipToEntity(const edict_t *touch, const vec3_t start, const vec3_t mins,
		const vec3_t maxs, const vec3_t end, int contentmask)
{
	int headnode = SV_HullForEntity(touch);
	const float *angles = touch->s.angles;

	if (!SV_IsBrushSolid(touch->solid))
	{
		angles = vec3_origin; /* boxes don't rotate */
	}

	return CM_TransformedBoxTrace(start, end, mins, maxs, headnode, contentmask,
			touch->s.origin, angles);
}

static void
SV_TraceBounds(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
	int i;

	for (i = 0; i < 3; i++)
	{
		if (end[i] > start[i])
		{
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
}

static qboolean
SV_SkipForPass(const edict_t *touch, const edict_t *passedict, int contentmask)
{
	if (touch == passedict)
	{
		return true;
	}

	if (passedict)
	{
		if (touch->owner == passedict)
		{
			return true; /* don't clip against own missiles */
		}

		if (passedict->owner == touch)
		{
			return true; /* don't clip against owner */
		}
	}

	if (!(contentmask & CONTENTS_DEADMONSTER) && (touch->svflags & SOF_SVF_DEADMONSTER))
	{
		return true;
	}

	return false;
}

/*
 * Moves the given mins/maxs volume through the world from start to end.
 * Passedict and edicts owned by passedict are explicitly not checked.
 */
static trace_t
SV_TraceQ2(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
		const edict_t *passedict, int contentmask)
{
	edict_t *touchlist[SOF_MAX_EDICTS];
	vec3_t boxmins, boxmaxs;
	trace_t result, trace;
	int i, num;

	/* clip to world */
	result = CM_BoxTrace(start, end, mins, maxs, 0, contentmask);
	result.ent = (struct edict_s *)ge->edicts;

	if (result.fraction == 0)
	{
		return result; /* blocked by the world */
	}

	/* clip to other solid entities */
	SV_TraceBounds(start, mins, maxs, end, boxmins, boxmaxs);
	num = SV_AreaEdicts(boxmins, boxmaxs, touchlist, SOF_MAX_EDICTS, AREA_SOLID);

	for (i = 0; i < num; i++)
	{
		edict_t *touch = touchlist[i];

		if (touch->solid == SOF_SOLID_NOT)
		{
			continue;
		}

		if (SV_SkipForPass(touch, passedict, contentmask))
		{
			continue;
		}

		if (result.allsolid)
		{
			return result;
		}

		trace = SV_ClipToEntity(touch, start, mins, maxs, end, contentmask);

		if (trace.allsolid && sv_hostdebug && ((int)sv_hostdebug->value & 16))
		{
			Com_Printf("trace allsolid in entity %i: solid %i model %i svflags %x origin "
					"(%.1f %.1f %.1f) mins (%.1f %.1f %.1f) maxs (%.1f %.1f %.1f)\n",
					NUM_FOR_EDICT(touch), touch->solid, touch->s.modelindex, touch->svflags,
					touch->s.origin[0], touch->s.origin[1], touch->s.origin[2], touch->mins[0],
					touch->mins[1], touch->mins[2], touch->maxs[0], touch->maxs[1], touch->maxs[2]);
		}

		if (trace.allsolid || trace.startsolid || (trace.fraction < result.fraction))
		{
			trace.ent = (struct edict_s *)touch;

			if (result.startsolid)
			{
				result = trace;
				result.startsolid = true;
			}
			else
			{
				result = trace;
			}
		}
	}

	return result;
}

void
SV_TraceSoF(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
		const edict_t *passedict, int contentmask, sof_trace_t *out)
{
	trace_t tr;

	if (!mins)
	{
		mins = vec3_origin;
	}

	if (!maxs)
	{
		maxs = vec3_origin;
	}

	tr = SV_TraceQ2(start, mins, maxs, end, passedict, contentmask);
	SV_ConvertTrace(&tr, out);
}

typedef struct
{
	trace_t tr;
	float fraction; /* along the full sweep */
} filterhit_t;

static int
SV_CompareHits(const void *a, const void *b)
{
	float fa = ((const filterhit_t *)a)->fraction;
	float fb = ((const filterhit_t *)b)->fraction;

	return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

/* test aid (sv_watchdog crash reports): the last filtered trace and the
   entities it offered to the game's callback */
static struct
{
	int passent, contentmask, numhits, offered, incallback, returned;
	float worldfraction;
	struct
	{
		int num, solid, svflags, modelindex, owner, hasghoul, hasclient, accepted;
		float fraction;
	} c[8];
} sv_lastfilter;

void
SV_World_DumpLastFilter(void *file)
{
	FILE *f = (FILE *)file;
	int i;

	fprintf(f, " last filtered trace: passent %i, mask %08x, world fraction %.3f, %i hits, %i offered%s%s\n",
			sv_lastfilter.passent, sv_lastfilter.contentmask, sv_lastfilter.worldfraction,
			sv_lastfilter.numhits, sv_lastfilter.offered,
			sv_lastfilter.incallback ? ", INSIDE the game's callback" : "",
			sv_lastfilter.returned ? ", returned to the game" : ", not returned");

	for (i = 0; i < sv_lastfilter.offered && i < 8; i++)
	{
		fprintf(f, "  offered entity %i: solid %i svflags %x model %i owner %i ghoul %i client %i fraction %.3f%s\n",
				sv_lastfilter.c[i].num, sv_lastfilter.c[i].solid, sv_lastfilter.c[i].svflags,
				sv_lastfilter.c[i].modelindex, sv_lastfilter.c[i].owner, sv_lastfilter.c[i].hasghoul,
				sv_lastfilter.c[i].hasclient, sv_lastfilter.c[i].fraction,
				sv_lastfilter.c[i].accepted ? " accepted" : "");
	}
}

/* The game's callback takes two more arguments: the start point of the trace
   (the very pointer the game passed to import #53) and the end point of the
   entity sweep (the world hit point when the world was hit, else the end).
   Black box (I1b tracing proxy game module under the original 1.07f engine,
   2026-10-25): the original calls filter(candidate, start, sweep end); the
   original game module reads them - with NULLs it faults on the first offered
   candidate, with stray stack words it faulted at random (kos1, most runs).
   SPEC-313 s7 lists one argument (OPEN_QUESTIONS I1b).
   Test aid: cvar sv_tracefilterarg 1 = the one-argument call of the spec. */
typedef int (*sv_tracefilter1_t)(sof_trace_t *tr);

static int
SV_CallTraceFilter(sof_tracefilter_t filter, sof_trace_t *candidate, const float *start, const float *sweepend)
{
	static cvar_t *mode;

	if (!mode)
	{
		mode = Cvar_Get("sv_tracefilterarg", "0", 0);
	}

	if (mode->value == 1)
	{
		return ((sv_tracefilter1_t)(void *)filter)(candidate);
	}

	return filter(candidate, start, sweepend);
}

/* SPEC-313 s7: the filtered trace of game import #53 */
void
SV_TraceFiltered(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
		const edict_t *passedict, int contentmask, sof_trace_t *out,
		sof_tracefilter_t filter)
{
	static filterhit_t hits[MAX_FILTER_HITS];
	edict_t *touchlist[SOF_MAX_EDICTS];
	vec3_t boxmins, boxmaxs, sweepend;
	trace_t world, trace;
	sof_trace_t candidate;
	float scale;
	int i, num, numhits = 0;

	if (!mins)
	{
		mins = vec3_origin;
	}

	if (!maxs)
	{
		maxs = vec3_origin;
	}

	world = CM_BoxTrace(start, end, mins, maxs, 0, contentmask);
	world.ent = (struct edict_s *)ge->edicts;

	sv_lastfilter.passent = passedict ? NUM_FOR_EDICT(passedict) : -1;
	sv_lastfilter.contentmask = contentmask;
	sv_lastfilter.worldfraction = world.fraction;
	sv_lastfilter.numhits = 0;
	sv_lastfilter.offered = 0;
	sv_lastfilter.incallback = 0;
	sv_lastfilter.returned = 0;

	if (world.fraction == 0)
	{
		SV_ConvertTrace(&world, out);
		sv_lastfilter.returned = 1;
		return;
	}

	VectorCopy(end, sweepend);
	scale = 1.0f;

	if (world.fraction < 1)
	{
		VectorCopy(world.endpos, sweepend);
		scale = world.fraction;
		world.fraction = 0.99f;
	}

	SV_ConvertTrace(&world, out);

	SV_TraceBounds(start, mins, maxs, sweepend, boxmins, boxmaxs);
	num = SV_AreaEdicts(boxmins, boxmaxs, touchlist, SOF_MAX_EDICTS, AREA_SOLID);

	for (i = 0; i < num && numhits < MAX_FILTER_HITS; i++)
	{
		edict_t *touch = touchlist[i];

		if ((touch->solid == SOF_SOLID_NOT) && !(touch->svflags & SOF_SVF_LINKNONSOLID))
		{
			continue;
		}

		if (SV_SkipForPass(touch, passedict, contentmask))
		{
			continue;
		}

		trace = SV_ClipToEntity(touch, start, mins, maxs, sweepend, contentmask);

		if (trace.fraction < 1)
		{
			trace.ent = (struct edict_s *)touch;
			hits[numhits].tr = trace;
			hits[numhits].fraction = trace.fraction * scale;
			numhits++;
		}
	}

	if (!numhits)
	{
		sv_lastfilter.returned = 1;
		return;
	}

	qsort(hits, numhits, sizeof(hits[0]), SV_CompareHits);

	sv_lastfilter.numhits = numhits;

	for (i = 0; i < numhits; i++)
	{
		const edict_t *e = (const edict_t *)hits[i].tr.ent;

		SV_ConvertTrace(&hits[i].tr, &candidate);
		candidate.fraction = hits[i].fraction;

		if (i < 8)
		{
			sv_lastfilter.offered = i + 1;
			sv_lastfilter.c[i].num = NUM_FOR_EDICT(e);
			sv_lastfilter.c[i].solid = e->solid;
			sv_lastfilter.c[i].svflags = e->svflags;
			sv_lastfilter.c[i].modelindex = e->s.modelindex;
			sv_lastfilter.c[i].owner = e->owner ? NUM_FOR_EDICT(e->owner) : -1;
			sv_lastfilter.c[i].hasghoul = e->ghoulInst != NULL;
			sv_lastfilter.c[i].hasclient = e->client != NULL;
			sv_lastfilter.c[i].fraction = candidate.fraction;
			sv_lastfilter.c[i].accepted = 0;
		}

		sv_lastfilter.incallback = 1;

		if (!filter || SV_CallTraceFilter(filter, &candidate, start, sweepend))
		{
			sv_lastfilter.incallback = 0;

			if (i < 8)
			{
				sv_lastfilter.c[i].accepted = 1;
			}

			*out = candidate;
			sv_lastfilter.returned = 1;
			return;
		}

		sv_lastfilter.incallback = 0;
	}

	sv_lastfilter.returned = 1;
}

/* ---------------------------------------------------------------------
 * visibility
 * --------------------------------------------------------------------- */

static qboolean
SV_InSet(const vec3_t p1, const vec3_t p2, qboolean phs)
{
	int leafnum, cluster, area1, area2;
	byte *mask;

	leafnum = CM_PointLeafnum(p1);
	cluster = CM_LeafCluster(leafnum);
	area1 = CM_LeafArea(leafnum);
	mask = phs ? CM_ClusterPHS(cluster) : CM_ClusterPVS(cluster);

	leafnum = CM_PointLeafnum(p2);
	cluster = CM_LeafCluster(leafnum);
	area2 = CM_LeafArea(leafnum);

	if ((cluster < 0) || !(mask[cluster >> 3] & (1 << (cluster & 7))))
	{
		return false;
	}

	if (!CM_AreasConnected(area1, area2))
	{
		return false; /* a door blocks sight */
	}

	return true;
}

qboolean
SV_InPVS(const vec3_t p1, const vec3_t p2)
{
	return SV_InSet(p1, p2, false);
}

qboolean
SV_InPHS(const vec3_t p1, const vec3_t p2)
{
	return SV_InSet(p1, p2, true);
}

/* ---------------------------------------------------------------------
 * per-map data (SPEC-203 map check string, SPEC-313 s4/s5, SPEC-101)
 * --------------------------------------------------------------------- */

#define BSP_HEADER 184
#define BSP_LUMPS 22
#define LUMP_TEXINFO_SOF 5
#define LUMP_LEAFS_SOF 8
#define LUMP_FACES_SOF 6
#define LUMP_MODELS_SOF 13
#define LUMP_REGIONS_SOF 21

static int
SV_LumpRead(const byte *buf, int len, int lump, int *ofs)
{
	int o = LittleLong(*(const int *)(buf + 8 + 8 * lump));
	int l = LittleLong(*(const int *)(buf + 12 + 8 * lump));

	if ((o < 0) || (l < 0) || (o + l > len))
	{
		*ofs = 0;
		return 0;
	}

	*ofs = o;
	return l;
}

void
SV_FreeMapExtras(void)
{
	if (sv.surfaces)
	{
		Z_Free(sv.surfaces);
		sv.surfaces = NULL;
	}

	if (sv.surfacedamage)
	{
		Z_Free(sv.surfacedamage);
		sv.surfacedamage = NULL;
	}

	if (sv.surfacestage)
	{
		Z_Free(sv.surfacestage);
		sv.surfacestage = NULL;
	}

	sv.numsurfaces = 0;

	if (sv_leafregion)
	{
		Z_Free(sv_leafregion);
		sv_leafregion = NULL;
	}

	if (sv_regions)
	{
		Z_Free(sv_regions);
		sv_regions = NULL;
	}

	sv_numleafs = sv_numregions = 0;

	if (sv_modelfaceflags)
	{
		Z_Free(sv_modelfaceflags);
		sv_modelfaceflags = NULL;
	}

	sv_nummodels = 0;
}

void
SV_LoadMapExtras(const char *mapfile)
{
	byte *buf;
	int len, i, ofs, l;
	char check[BSP_LUMPS * 2 + 1];

	SV_FreeMapExtras();
	memset(&sv_nullsurface, 0, sizeof(sv_nullsurface));

	len = FS_LoadFile(mapfile, (void **)&buf);

	if (!buf)
	{
		return;
	}

	if (len < BSP_HEADER)
	{
		FS_FreeFile(buf);
		return;
	}

	/* map check string: low byte of each lump's block checksum */
	check[0] = 0;

	for (i = 0; i < BSP_LUMPS; i++)
	{
		l = SV_LumpRead(buf, len, i, &ofs);
		Q_strlcat(check, va("%02X", Com_BlockChecksum(buf + ofs, l) & 0xFF), sizeof(check));
	}

	Q_strlcpy(sv.configstrings[SOF_CS_MAPCHECKSUM], check, SOF_CS_LENGTH);
	Com_DPrintf("Map checkstring: %s\n", check);

	/* texinfo records (76 bytes on disk, the record without its link) */
	l = SV_LumpRead(buf, len, LUMP_TEXINFO_SOF, &ofs);

	if (l >= 76)
	{
		sv.numsurfaces = l / 76;
		sv.surfaces = Z_Malloc(sv.numsurfaces * sizeof(sof_surface_t));
		sv.surfacedamage = Z_Malloc(sv.numsurfaces * sizeof(int));
		sv.surfacestage = Z_Malloc(sv.numsurfaces * sizeof(int));

		for (i = 0; i < sv.numsurfaces; i++)
		{
			const byte *in = buf + ofs + 76 * i;
			sof_surface_t *out = &sv.surfaces[i];
			int k;

			for (k = 0; k < 8; k++)
			{
				out->vecs[k / 4][k % 4] = LittleFloat(*(const float *)(in + 4 * k));
			}

			out->flags = LittleLong(*(const int *)(in + 32));
			out->value = LittleLong(*(const int *)(in + 36));
			memcpy(out->texture, in + 40, 32);
			out->texture[31] = 0;
		}
	}

	/* leaf regions (leaf offset 8) and the region records (lump 21) */
	l = SV_LumpRead(buf, len, LUMP_LEAFS_SOF, &ofs);

	if (l >= 32)
	{
		sv_numleafs = l / 32;
		sv_leafregion = Z_Malloc(sv_numleafs * sizeof(short));

		for (i = 0; i < sv_numleafs; i++)
		{
			sv_leafregion[i] = LittleShort(*(const short *)(buf + ofs + 32 * i + 8));
		}
	}

	l = SV_LumpRead(buf, len, LUMP_REGIONS_SOF, &ofs);

	if (l >= 76)
	{
		sv_numregions = l / 76;
		sv_regions = Z_Malloc(sv_numregions * 76);
		memcpy(sv_regions, buf + ofs, sv_numregions * 76);
	}

	/* texinfo flags of each model's faces (models 48 bytes: first face at 40, count at 44;
	   faces 44 bytes: texinfo at 10) */
	{
		int mofs, ml, fofs, fl, nfaces;

		ml = SV_LumpRead(buf, len, LUMP_MODELS_SOF, &mofs);
		fl = SV_LumpRead(buf, len, LUMP_FACES_SOF, &fofs);
		nfaces = fl / 44;

		if (ml >= 48)
		{
			sv_nummodels = ml / 48;
			sv_modelfaceflags = Z_Malloc(sv_nummodels * sizeof(int));

			for (i = 0; i < sv_nummodels; i++)
			{
				const byte *in = buf + mofs + 48 * i;
				int first = LittleLong(*(const int *)(in + 40));
				int num = LittleLong(*(const int *)(in + 44));
				int f, flags = 0;

				for (f = first; (f < first + num) && (f >= 0) && (f < nfaces); f++)
				{
					int ti = LittleShort(*(const short *)(buf + fofs + 44 * f + 10));

					if ((ti >= 0) && (ti < sv.numsurfaces))
					{
						flags |= sv.surfaces[ti].flags;
					}
				}

				sv_modelfaceflags[i] = flags;
			}
		}
	}

	FS_FreeFile(buf);
}

/* OR of the texinfo flags of an inline model's faces ("*n" -> n), 0 when unknown */
int
SV_InlineModelFaceFlags(int n)
{
	if (!sv_modelfaceflags || (n < 0) || (n >= sv_nummodels))
	{
		return 0;
	}

	return sv_modelfaceflags[n];
}

/* game import #55: fog visibility distance of the region at the point */
float
SV_RegionDistance(const vec3_t p)
{
	int leaf, r;
	float d, D;

	if (!sv_leafregion)
	{
		return -1;
	}

	leaf = CM_PointLeafnum(p);

	if ((leaf < 0) || (leaf >= sv_numleafs))
	{
		return -1;
	}

	r = sv_leafregion[leaf];

	if ((r < 0) || (r >= sv_numregions))
	{
		return -1;
	}

	d = LittleFloat(*(const float *)(sv_regions + 76 * r + 4));
	D = LittleFloat(*(const float *)(sv_regions + 76 * r + 16));

	if (d >= 1)
	{
		return D;
	}

	if (d > 0)
	{
		return D / d;
	}

	return -1;
}

/* game import #26: the distinct material codes (texinfo flags high byte) */
int
SV_MaterialList(byte *buf, int capacity)
{
	qboolean seen[256];
	int i, n = 0;

	memset(seen, 0, sizeof(seen));

	for (i = 0; i < sv.numsurfaces && n < capacity; i++)
	{
		int m = ((unsigned)sv.surfaces[i].flags >> 24) & 0xFF;

		if (!seen[m])
		{
			seen[m] = true;
			buf[n++] = (byte)m;
		}
	}

	return n;
}

/* game import #25 (SPEC-313 s4). The damaged-texture chain comes from the
   .m32 headers; not loaded yet, so no surface takes damage (I1b.md). */
int
SV_DamageTexture(sof_surface_t *surface, int damage)
{
	(void)surface;
	(void)damage;
	return 0;
}

/* OpenMerc debugging: sv_tracetest <x> <y> <z> - position tests of the
   standing player box around the point and a sweep straight down */
void
SV_TraceTest_f(void)
{
	static const vec3_t mins = {-16, -16, -24}, maxs = {16, 16, 40};
	vec3_t p, e;
	sof_trace_t tr;
	int i;

	if ((Cmd_Argc() < 4) || !sv.models[1])
	{
		Com_Printf("usage: sv_tracetest <x> <y> <z>\n");
		return;
	}

	p[0] = atof(Cmd_Argv(1));
	p[1] = atof(Cmd_Argv(2));
	p[2] = atof(Cmd_Argv(3));

	if ((Cmd_Argc() == 5) && !strcmp(Cmd_Argv(4), "ents"))
	{
		/* the linked entities whose boxes touch a player box at the point */
		edict_t *list[SOF_MAX_EDICTS];
		vec3_t lo, hi;
		int t, n, k;

		for (k = 0; k < 3; k++)
		{
			lo[k] = p[k] + mins[k] - 1;
			hi[k] = p[k] + maxs[k] + 1;
		}

		for (t = AREA_SOLID; t <= AREA_TRIGGERS; t++)
		{
			n = SV_AreaEdicts(lo, hi, list, SOF_MAX_EDICTS, t);

			for (k = 0; k < n; k++)
			{
				edict_t *c = list[k];

				Com_Printf("  %s ent %i: solid %i svflags %x clipmask %x model %i owner %i origin "
						"(%.3f %.3f %.3f) abs (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n",
						(t == AREA_SOLID) ? "solid" : "trigger", NUM_FOR_EDICT(c), c->solid,
						c->svflags, c->clipmask, c->s.modelindex,
						c->owner ? NUM_FOR_EDICT(c->owner) : -1, c->s.origin[0], c->s.origin[1],
						c->s.origin[2], c->absmin[0], c->absmin[1], c->absmin[2], c->absmax[0],
						c->absmax[1], c->absmax[2]);
			}
		}

		return;
	}

	if (Cmd_Argc() >= 6)
	{
		/* scan along x: position tests at z and z - 0.5, box sweep from z + 1 down */
		float step = atof(Cmd_Argv(5));
		int n = atoi(Cmd_Argv(4));

		for (i = 0; i < n; i++)
		{
			vec3_t q, q2, s2, e2;
			sof_trace_t a, b, c;

			VectorCopy(p, q);
			q[0] += i * step;
			VectorCopy(q, q2);
			q2[2] -= 0.5f;
			VectorCopy(q, s2);
			s2[2] += 1;
			VectorCopy(q, e2);
			e2[2] -= 64;
			SV_TraceSoF(q, mins, maxs, q, NULL, 0x02010003, &a);
			SV_TraceSoF(q2, mins, maxs, q2, NULL, 0x02010003, &b);
			SV_TraceSoF(s2, mins, maxs, e2, NULL, 0x02010003, &c);
			Com_Printf("  x %.3f: pos(z) %i pos(z-0.5) %i sweep end z %.3f\n", q[0], a.allsolid,
					b.allsolid, c.endpos[2]);
		}

		return;
	}

	for (i = -2; i <= 4; i++)
	{
		vec3_t q;

		VectorCopy(p, q);
		q[2] += i * 0.125f;
		SV_TraceSoF(q, mins, maxs, q, NULL, 0x02010003, &tr);
		Com_Printf("  position z %.3f: allsolid %i startsolid %i\n", q[2], tr.allsolid,
				tr.startsolid);
	}

	for (i = 0; i < 4; i++)
	{
		static const float up[4] = {0, 6, 0.25f, 40};
		vec3_t s;
		trace_t q2;

		VectorCopy(p, s);
		s[2] += up[i];
		VectorCopy(p, e);
		e[2] -= 64;
		SV_TraceSoF(s, mins, maxs, e, NULL, 0x02010003, &tr);
		Com_Printf("  box sweep down from z %.3f: fraction %.5f end z %.5f normal (%.2f %.2f %.2f) "
				"allsolid %i startsolid %i\n", s[2], tr.fraction, tr.endpos[2], tr.plane.normal[0],
				tr.plane.normal[1], tr.plane.normal[2], tr.allsolid, tr.startsolid);
		q2 = CM_BoxTrace(s, e, vec3_origin, vec3_origin, 0, 0x02010003);
		Com_Printf("  point sweep down from z %.3f: fraction %.5f end z %.5f startsolid %i\n",
				s[2], q2.fraction, q2.endpos[2], q2.startsolid);
	}
}
