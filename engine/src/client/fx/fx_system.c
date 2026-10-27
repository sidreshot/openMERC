/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects system: effect tables (configstring and by-name), running
 * effects (start, attachment refresh, one-shot primitives, end), the effects
 * clock, entity effect slots, svc_effect, camera shake, the effect editor
 * commands and the public life-cycle hooks.
 *
 * Implements: SPEC-400 (Cvars and commands, Effect parameters, Timebase,
 * Effect tables and lookup, Starting an effect, Updating a running effect,
 * Entity effect slots, View weapon, Camera shake, Editor), SPEC-AMEND-001 B
 * (svc_effect flag 0x20), SPEC-201 s.5 (svc_effect payload), SPEC-202 s.2
 * (events and effect slots of the entity state)
 */

#include "fx_local.h"
#include "../header/sofclient.h" /* CL_SoF_GameFov (ENG2) */
#include "../header/cl_ghoul.h" /* CL_Ghoul_GameClock (D1) */

static fx_def_t fx_cs_defs[FX_MAX_DEFS];
static fx_def_t fx_named[FX_MAX_NAMED];
static int fx_num_named;
static fx_def_t fx_editdef;
static qboolean fx_editloaded;
static float fx_editnext;
static qboolean fx_inited;
static cvar_t *fx_enable;

fx_run_t fx_running[FX_MAX_RUNNING];
float fx_now;
float fx_shake, fx_shake_decay;

static float fx_slot_next[1024][4];
/* OpenMerc: the instance last started by each entity effect slot (index + 1, 0 = none) and its serial */
static short fx_slot_run[1024][4];
static unsigned fx_slot_serial[1024][4];
static unsigned fx_run_serial;
static cvar_t *fx_slotstack;
int fx_pool_drop_world, fx_pool_drop_own, fx_pool_evicted, fx_pool_slot_capped;
static float fx_framerate = 30;
static int fx_last_realtime;

/* ---- entity-state extension (events, effect slots) ------------------------ */

typedef struct
{
	byte ev[3];
	byte parm[3];
	short slots[12];
} fx_stateext_t;

static fx_stateext_t fx_ext_ring[MAX_PARSE_ENTITIES];
static fx_stateext_t fx_ext_base[1024];

static fx_stateext_t *
FX_Ext(const entity_state_t *s)
{
	if (!s)
	{
		return NULL;
	}

	if ((s >= cl_parse_entities) && (s < cl_parse_entities + MAX_PARSE_ENTITIES))
	{
		return &fx_ext_ring[s - cl_parse_entities];
	}

	if (cl_entities && (s->number > 0) && (s->number < 1024) && (s->number < cl_numentities) &&
		(s == &cl_entities[s->number].baseline))
	{
		return &fx_ext_base[s->number];
	}

	return NULL;
}

void
CLFX_DeltaBegin(const entity_state_t *from, entity_state_t *to)
{
	fx_stateext_t *t = FX_Ext(to), *f = FX_Ext(from);

	if (!t)
	{
		return;
	}

	if (f)
	{
		if (f != t)
		{
			*t = *f;
		}
	}
	else
	{
		memset(t, 0, sizeof(*t));
	}

	/* events are never inherited (SPEC-202 2.2, receiver rule) */
	t->ev[0] = t->ev[1] = t->ev[2] = 0;
}

void
CLFX_DeltaEvent(entity_state_t *to, int i, int event, int parm)
{
	fx_stateext_t *t = FX_Ext(to);

	if (!t || (i < 0) || (i > 2))
	{
		return;
	}

	t->ev[i] = (byte)(event & 0x7F);
	t->parm[i] = (byte)parm;
}

void
CLFX_DeltaSlot(entity_state_t *to, int i, int value)
{
	fx_stateext_t *t = FX_Ext(to);

	if (!t || (i < 0) || (i > 11))
	{
		return;
	}

	t->slots[i] = (short)value;
}

const short *
CLFX_StateSlots(const entity_state_t *s)
{
	fx_stateext_t *e = FX_Ext(s);

	return e ? e->slots : NULL;
}

/* entity events of a new frame (SPEC-401 Dispatch item 2) */
void
CLFX_FrameParsed(void)
{
	int i, j, n;
	entity_state_t *s;
	fx_stateext_t *e;

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		n = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		s = &cl_parse_entities[n];
		e = &fx_ext_ring[n];

		for (j = 0; j < 3; j++)
		{
			if (e->ev[j])
			{
				FX_EntityEvent(s->number, e->ev[j], e->parm[j]);
			}
		}
	}
}

void
CLFX_CulledEvent(int event, int parm, int dist, const vec3_t dir)
{
	/* every 1.07f event handler needs an entity: culled events do nothing
	   (SPEC-401 Dispatch item 2, SPEC-201 s.24) */
	FX_TraceLog("culledEvent %d parm %d dist %d dir (%.2f %.2f %.2f): nothing\n", event, parm,
			dist * 4, dir[0], dir[1], dir[2]);
}

/* ---- definitions ----------------------------------------------------------- */

static void
FX_RegisterDef(fx_def_t *d)
{
	int i;
	fx_eft_prim_t *p;
	char name[MAX_QPATH];

	for (i = 0; i < d->eft.count; i++)
	{
		p = &d->eft.prims[i];
		d->sfx[i] = NULL;
		d->model[i] = NULL;

		if (p->type == FXP_SOUND)
		{
			if (p->u.sound.name[0])
			{
				d->sfx[i] = S_RegisterSound(p->u.sound.name);
			}
		}
		else if (p->type == FXP_EMITTER)
		{
			if ((p->u.emitter.p.ptype == FXPT_GHOUL) || (p->u.emitter.p.ptype == FXPT_DEBRIS))
			{
				Q_strlcpy(name, p->u.emitter.p.graphic, sizeof(name));
				d->model[i] = FXG_RegisterModel(NULL, name);
			}
			else if (p->u.emitter.p.graphic[0])
			{
				FX_RegisterSpriteName(p->u.emitter.p.graphic);
			}
		}
		else if (p->type == FXP_DECAL)
		{
			FX_RegisterSpriteName(p->u.decal.name);
		}
	}
}

static qboolean
FX_LoadDef(fx_def_t *d, const char *name)
{
	char path[MAX_OSPATH], err[128];
	byte *buf;
	int len, left;

	memset(d, 0, sizeof(*d));
	Q_strlcpy(d->name, name, sizeof(d->name));

	Com_sprintf(path, sizeof(path), "effects/%s.eft", name);
	len = FS_LoadFile(path, (void **)&buf);

	if (!buf)
	{
		Com_Printf("FX_System: Unable to find file %s\n", path);
		return false;
	}

	if (FX_ParseEft(buf, len, &d->eft, err, sizeof(err), &left))
	{
		/* invalid files are empty definitions (SPEC-400 .eft layout) */
		Com_DPrintf("FX_System: %s is invalid: %s\n", path, err);
		memset(&d->eft, 0, sizeof(d->eft));
		FS_FreeFile(buf);
		return false;
	}

	FS_FreeFile(buf);
	d->loaded = true;
	FX_RegisterDef(d);
	return true;
}

fx_def_t *
FX_ConfigEffect(int slot)
{
	if ((slot <= 0) || (slot >= FX_MAX_DEFS))
	{
		return NULL;
	}

	return &fx_cs_defs[slot];
}

fx_def_t *
FX_FindEffect(const char *name)
{
	int i;
	fx_def_t *d;

	if (!name || !name[0])
	{
		return NULL;
	}

	for (i = 1; i < FX_MAX_DEFS; i++)
	{
		if (fx_cs_defs[i].loaded && !Q_stricmp(fx_cs_defs[i].name, name))
		{
			return &fx_cs_defs[i];
		}
	}

	for (i = 0; i < fx_num_named; i++)
	{
		if (fx_named[i].loaded && !Q_stricmp(fx_named[i].name, name))
		{
			return &fx_named[i];
		}
	}

	if (fx_num_named >= FX_MAX_NAMED)
	{
		return NULL; /* table full: the request is ignored */
	}

	d = &fx_named[fx_num_named++];

	if (!FX_LoadDef(d, name))
	{
		d->name[0] = 0; /* the entry is used up and the name forgotten */
	}

	return d;
}

void
CLFX_EffectConfigString(int slot, const char *name)
{
	if ((slot <= 0) || (slot >= FX_MAX_DEFS))
	{
		return;
	}

	if (!cl.refresh_prepped)
	{
		return; /* loaded by the precache */
	}

	if (!name || !name[0])
	{
		memset(&fx_cs_defs[slot], 0, sizeof(fx_cs_defs[slot]));
		return;
	}

	FX_LoadDef(&fx_cs_defs[slot], name);
}

void
CLFX_RegisterSounds(void)
{
	int i;

	FX_ClearSoundList();

	for (i = 1; i < FX_MAX_DEFS; i++)
	{
		if (fx_cs_defs[i].loaded)
		{
			FX_RegisterDef(&fx_cs_defs[i]);
		}
	}

	for (i = 0; i < fx_num_named; i++)
	{
		if (fx_named[i].loaded)
		{
			FX_RegisterDef(&fx_named[i]);
		}
	}

	if (fx_editloaded)
	{
		FX_RegisterDef(&fx_editdef);
	}
}

/* ---- running effects -------------------------------------------------------- */

void
FX_DefaultParams(fx_params_t *p)
{
	memset(p, 0, sizeof(*p));
	VectorSet(p->fwd, 1, 0, 0);
	VectorSet(p->right, 0, 1, 0);
	VectorSet(p->up, 0, 0, 1);
	p->mode = FXA_POSITION;
	p->part = -1;
}

void
FX_ClearRunning(void)
{
	memset(fx_running, 0, sizeof(fx_running));
}

/* OpenMerc: effects of the local player's own entity (view-weapon notes, own muzzle
   flash and shells, effects attached to the own entity) are never refused */
static qboolean
FX_IsPriority(const fx_params_t *p)
{
	return ((p->mode == FXA_ENTITY) || (p->mode == FXA_BOLT)) && (p->ent > 0) && FX_IsLocalPlayer(p->ent);
}

/* frees a running effect before its slot is reused: its particles live on unowned
   (they keep their own life time, as particles of a detached effect do) */
static void
FX_ReleaseRun(fx_run_t *r)
{
	int j, idx, next;

	if (!r->inuse)
	{
		return;
	}

	for (j = 0; (j < FX_MAX_PRIMS) && r->def && (j < r->def->eft.count); j++)
	{
		for (idx = r->slot[j].head; (idx >= 0) && (idx < FX_MAX_PARTICLES); idx = next)
		{
			next = fx_parts[idx].next;
			fx_parts[idx].fx = -1;
			fx_parts[idx].prim = -1;
			fx_parts[idx].next = -1;
		}

		r->slot[j].head = -1;
		r->slot[j].live = 0;
	}

	r->inuse = false;
}

/* the running world effect to replace when the pool is full: the farthest from the
   view (the oldest on a tie); a world request takes it only when it is at least
   1024 units nearer than that effect. -1 = none. */
static int
FX_EvictCandidate(const fx_params_t *p, qboolean priority)
{
	int i, best = -1;
	float d, bestd = -1, bests = 0;
	vec3_t v;

	for (i = 0; i < FX_MAX_RUNNING; i++)
	{
		if (!fx_running[i].inuse || fx_running[i].priority)
		{
			continue;
		}

		VectorSubtract(fx_running[i].p.origin, cl.refdef.vieworg, v);
		d = VectorLength(v);

		if ((d > bestd) || ((d == bestd) && (fx_running[i].start < bests)))
		{
			best = i;
			bestd = d;
			bests = fx_running[i].start;
		}
	}

	if ((best >= 0) && !priority)
	{
		if ((p->mode == FXA_ENTITY) || (p->mode == FXA_BOLT))
		{
			FX_EntityLerpOrigin(p->ent, v);
		}
		else
		{
			VectorCopy(p->origin, v);
		}

		VectorSubtract(v, cl.refdef.vieworg, v);

		if (VectorLength(v) + 1024.0f > bestd)
		{
			return -1;
		}
	}

	return best;
}

int
FX_StartEffect(fx_def_t *def, const fx_params_t *p, int source)
{
	int i, seg;
	fx_run_t *r;
	const fx_eft_prim_t *pr;
	qboolean priority;

	if (fx_edit && fx_edit->value && fx_editloaded)
	{
		seg = (int)fx_editsegment->value;

		if (!seg || (seg == source))
		{
			def = &fx_editdef;
		}
	}

	if (!def || !def->loaded || (def->eft.count <= 0))
	{
		return -1; /* an empty definition plays nothing */
	}

	priority = FX_IsPriority(p);

	for (i = 0; i < FX_MAX_RUNNING; i++)
	{
		if (!fx_running[i].inuse)
		{
			break;
		}
	}

	if (i == FX_MAX_RUNNING)
	{
		/* all 256 in use. 1.07f ignores the request (SPEC-400); OpenMerc deviation
		   (docs/DEVIATIONS.md): the local player's own effects are never refused, and a
		   world effect much nearer than the farthest running one replaces it */
		i = FX_EvictCandidate(p, priority);

		if (i < 0)
		{
			if (priority)
			{
				fx_pool_drop_own++;
				Com_DPrintf("fx: pool full, own effect '%s' dropped\n", def->name);
			}
			else
			{
				fx_pool_drop_world++;
			}

			if (fx_trace && fx_trace->value)
			{
				Com_Printf("fx: pool full, '%s' ent %d dropped%s\n", def->name, p->ent,
						priority ? " (OWN)" : "");
			}

			return -1;
		}

		if (fx_trace && fx_trace->value)
		{
			Com_Printf("fx: pool full, '%s' ent %d evicted for '%s' ent %d%s\n", fx_running[i].def->name,
					fx_running[i].p.ent, def->name, p->ent, priority ? " (own)" : "");
		}

		FX_ReleaseRun(&fx_running[i]);
		fx_pool_evicted++;
	}

	r = &fx_running[i];
	memset(r, 0, sizeof(*r));
	r->inuse = true;
	r->def = def;
	r->source = source;
	r->priority = priority;
	r->serial = ++fx_run_serial;
	r->p = *p;
	r->start = fx_now;
	r->refresh = true;

	for (i = 0; i < def->eft.count; i++)
	{
		pr = &def->eft.prims[i];
		r->slot[i].head = -1;
		r->slot[i].trigger = fx_now + FX_Range(&pr->trigger);

		if (pr->type == FXP_EMITTER)
		{
			r->slot[i].end = r->slot[i].trigger + FX_Range(&pr->u.emitter.duration);
			r->slot[i].next = r->slot[i].trigger;
		}
	}

	FX_TraceLog("  effect '%s' started (slot %d, source %d) at (%.0f %.0f %.0f)\n",
			def->name, (int)(r - fx_running), source, p->origin[0], p->origin[1], p->origin[2]);

	return (int)(r - fx_running);
}

int
FX_StartNamed(const char *name, const fx_params_t *p, int source)
{
	return FX_StartEffect(FX_FindEffect(name), p, source);
}

int
CLFX_StartNamedEffect(const char *name, const vec3_t origin, const vec3_t forward)
{
	fx_params_t p;

	FX_DefaultParams(&p);
	VectorCopy(origin, p.origin);

	if (forward)
	{
		FX_AxesFromDir(forward, p.fwd, p.right, p.up);
	}

	return FX_StartNamed(name, &p, FXS_GAME);
}

static void
FX_Detach(fx_run_t *r, const char *why)
{
	/* OpenMerc test trace (SPEC-AMEND-011 gate 6): a detached effect never fires its sound */
	if (!r->detached && fx_trace && fx_trace->value && r->def)
	{
		Com_Printf("fx: detach '%s' ent %d: %s\n", r->def->name, r->p.ent, why);
	}

	r->detached = true;
}

static void
FX_RefreshAttachment(fx_run_t *r)
{
	fx_params_t *p = &r->p;
	const entity_state_t *s;
	vec3_t angles, end;
	fx_part_t *part;
	trace_t tr;
	int hit;

	switch (p->mode)
	{
		case FXA_POSITION:
			return; /* position mode never changes */

		case FXA_ENTITY:
		case FXA_BOLT:
			if (!FX_EntityInFrame(p->ent))
			{
				FX_Detach(r, "entity not in frame");
				return;
			}

			s = FX_EntityCurrent(p->ent);

			if (s && (s->effects & 0x4000))
			{
				FX_Detach(r, "effects bit 0x4000");
				return;
			}

			if ((p->mode == FXA_BOLT) &&
				FXG_BoltFrame(p->ent, p->ghoul_inst, p->bolt, p->bolt == 0, p->origin, p->fwd,
					p->right, p->up, p->bolt_reversed))
			{
				break;
			}

			/* entity following (and bolts while the Ghoul system cannot
			   resolve them: the entity origin) */
			if (FX_IsLocalPlayer(p->ent))
			{
				VectorCopy(cl.refdef.vieworg, p->origin);
				VectorCopy(cl.refdef.viewangles, angles);
			}
			else
			{
				FX_EntityLerpOrigin(p->ent, p->origin);
				FX_EntityLerpAngles(p->ent, angles);
			}

			AngleVectors(angles, p->fwd, p->right, p->up);
			break;

		case FXA_PARTICLE:
			if ((p->part < 0) || (p->part >= FX_MAX_PARTICLES))
			{
				FX_Detach(r, "particle slot out of range");
				return;
			}

			part = &fx_parts[p->part];

			if ((part->kind == FXK_FREE) || (part->serial != p->part_serial))
			{
				FX_Detach(r, "particle gone");
				return;
			}

			VectorCopy(part->pos, p->origin);

			if (VectorLength(part->vel) > 0)
			{
				FX_AxesFromDir(part->vel, p->fwd, p->right, p->up);
			}

			break;
	}

	if (p->trace_pos2)
	{
		VectorMA(p->origin, 1024, p->fwd, end);
		FX_Trace(p->origin, NULL, NULL, end, FXTR_WORLD, &tr, &hit);
		VectorCopy(tr.endpos, p->pos2);
		p->has_pos2 = true;
	}
}

static void
FX_FireSound(fx_run_t *r, int i, const fx_eft_sound_t *s)
{
	float vol = FX_Range(&s->volume), atten = FX_Range(&s->atten);

	FX_TraceLog("  sound %s vol %.2f atten %.2f at (%.1f %.1f %.1f) view (%.1f %.1f %.1f)\n", s->name, vol, atten, r->p.origin[0], r->p.origin[1], r->p.origin[2], cl.refdef.vieworg[0], cl.refdef.vieworg[1], cl.refdef.vieworg[2]);
	/* SPEC-400 type 1: positional, no entity (-1, as the original starts it; entity 0 is the world) */
	FX_PlaySfx(r->def->sfx[i], r->p.origin, vol, atten, -1, 0, 0);
}

static void
FX_FireLight(fx_run_t *r, const fx_eft_light_t *l)
{
	int red = (int)FX_Range(&l->rgb[0]);
	int green = (int)FX_Range(&l->rgb[1]);
	int blue = (int)FX_Range(&l->rgb[2]);
	float radius = FX_Range(&l->radius);
	float life = FX_Range(&l->life);

	FX_TraceLog("  light (%d %d %d) radius %.0f life %.3f\n", red, green, blue, radius, life);
	CL_SoF_AddLight(0, r->p.origin, red, green, blue, radius, life * 1000.0f, 0);
}

static void
FX_FireDecal(fx_run_t *r, const fx_eft_decal_t *d)
{
	vec3_t dir, end;
	trace_t tr;
	int hit;
	fx_impact_t im;

	switch (d->orientation)
	{
		case 1:
			VectorSet(dir, 0, 0, -1);
			break;

		case 2:
			VectorNegate(r->p.fwd, dir);
			break;

		default:
			VectorCopy(r->p.fwd, dir);
			break;
	}

	VectorMA(r->p.origin, 512, dir, end);
	FX_Trace(r->p.origin, NULL, NULL, end, FXTR_SOLID, &tr, &hit);

	if ((tr.fraction >= 1.0f) || hit || tr.allsolid)
	{
		return; /* nothing, or an entity, was hit */
	}

	memset(&im, 0, sizeof(im));
	im.kind = 0;
	VectorCopy(tr.endpos, im.point);
	VectorCopy(tr.plane.normal, im.normal);
	im.ent = -1;
	im.material = FX_TraceMaterial(&tr);
	im.surfflags = tr.surface ? tr.surface->flags : 0;
	im.leaf = CM_PointLeafnum(im.point);

	FX_AddDecal(&im, d->name, 255, 255, 255, 250, d->start_size, d->end_size, d->accel, d->time, 0);
}

static void
FX_FireFeedback(fx_run_t *r, const fx_eft_feedback_t *f)
{
	float s = FX_Range(&f->shake), d;
	vec3_t v;

	if (s > 0.05f)
	{
		VectorSubtract(r->p.origin, cl.refdef.vieworg, v);
		d = VectorLength(v);
		fx_shake += FX_Range(&f->shake) * 200.0f * (600.0f - d) / 600.0f;

		if (fx_shake > 200)
		{
			fx_shake = 200;
		}

		fx_shake_decay = 80;
		FX_TraceLog("  shake -> %.1f\n", fx_shake);
	}

	/* force feedback (f->ff_id): no device support */
}

static qboolean
FX_UpdateOne(fx_run_t *r)
{
	int i;
	qboolean pending = false, emitting = false, attached_live = false, alive = false;
	const fx_eft_prim_t *pr;
	fx_slot_t *s;
	float d;

	if (r->refresh)
	{
		FX_RefreshAttachment(r);
	}

	for (i = 0; i < r->def->eft.count; i++)
	{
		pr = &r->def->eft.prims[i];
		s = &r->slot[i];

		if (pr->type == FXP_EMITTER)
		{
			if (!s->done)
			{
				if (r->detached)
				{
					s->done = true; /* detached emitters stop emitting */
				}
				else if (fx_now >= s->next)
				{
					if (s->next <= s->end)
					{
						float t = 1.0f;

						if (s->end > s->trigger)
						{
							t = 1.0f - (fx_now - s->trigger) / (s->end - s->trigger);
						}

						FX_EmitterBurst(r, i, t);
						d = FX_Range(&pr->u.emitter.delay);

						if (d < 0.01f)
						{
							s->done = true; /* exactly one burst */
						}

						s->next = fx_now + FX_Range(&pr->u.emitter.delay);
					}
					else if ((fx_now > s->end) && (s->live <= 0))
					{
						/* finished only at a step where the next burst time has been
						   reached, the end time has passed and none of its particles
						   live; while the next burst time lies ahead the emitter stays
						   active (SPEC-AMEND-005 I5.18) */
						s->done = true;
					}
				}
			}

			if (!s->done)
			{
				emitting = true;
			}

			if (s->live > 0)
			{
				alive = true;

				if (pr->u.emitter.flags & FXE_ATTACHED)
				{
					attached_live = true;
				}
			}

			continue;
		}

		if (s->done)
		{
			continue;
		}

		if (r->detached)
		{
			s->done = true; /* never fires */
			continue;
		}

		if (fx_now > s->trigger)
		{
			switch (pr->type)
			{
				case FXP_SOUND:
					FX_FireSound(r, i, &pr->u.sound);
					break;

				case FXP_LIGHT:
					FX_FireLight(r, &pr->u.light);
					break;

				case FXP_DECAL:
					FX_FireDecal(r, &pr->u.decal);
					break;

				case FXP_FEEDBACK:
					FX_FireFeedback(r, &pr->u.feedback);
					break;
			}

			s->done = true;
		}
		else
		{
			pending = true;
		}
	}

	r->refresh = pending || emitting || attached_live;

	return pending || emitting || alive;
}

void
FX_UpdateRunning(void)
{
	int i;
	fx_run_t *r;

	for (i = 0; i < FX_MAX_RUNNING; i++)
	{
		r = &fx_running[i];

		if (!r->inuse)
		{
			continue;
		}

		if (!FX_UpdateOne(r))
		{
			r->inuse = false; /* every primitive fired or skipped, every emitter done */
		}
	}
}

/* ---- svc_effect (SPEC-201 s.5, SPEC-400 Effect parameters) -------------------- */

void
CLFX_ParseEffect(void)
{
	int index, flags, ext = 0, i;
	fx_params_t p;
	vec3_t dir;

	FX_DefaultParams(&p);
	index = MSG_ReadByte(&net_message);
	flags = MSG_ReadByte(&net_message);

	if (flags & 0x01)
	{
		FX_ReadPos(p.origin);
	}
	else if (flags & 0x02)
	{
		p.ent = MSG_ReadShort(&net_message);
		p.mode = FXA_ENTITY;
	}
	else if (flags & 0x04)
	{
		p.ent = MSG_ReadShort(&net_message);

		if (flags & 0x08)
		{
			p.ghoul_inst = MSG_ReadShort(&net_message);
		}

		p.bolt = MSG_ReadShort(&net_message);
		p.mode = FXA_BOLT;
		p.bolt_reversed = (flags & 0x40) != 0;
	}

	if (flags & 0x20)
	{
		p.trace_pos2 = true;
	}

	if (flags & 0x80)
	{
		ext = MSG_ReadByte(&net_message);

		if (ext & 0x01)
		{
			p.scale = MSG_ReadShort(&net_message) / 128.0f;
		}

		if (ext & 0x02)
		{
			p.count = MSG_ReadByte(&net_message);
		}

		if (ext & 0x04)
		{
			FX_ReadPos(p.pos2);
			p.has_pos2 = true;
		}

		if (ext & 0x08)
		{
			FX_ReadScaledPos(dir, 1.0f / 2048.0f);
			FX_AxesFromDir(dir, p.fwd, p.right, p.up);
		}

		if (ext & 0x10)
		{
			FX_ReadPos(p.rmin);
			p.has_rmin = true;
		}

		if (ext & 0x20)
		{
			FX_ReadPos(p.rmax);
			p.has_rmax = true;
		}

		if (ext & 0x40)
		{
			(void)MSG_ReadShort(&net_message); /* stored, unused */
		}

		if (ext & 0x80)
		{
			(void)MSG_ReadByte(&net_message); /* stored, unused */
		}
	}

	i = FX_ConfigEffect(index) ? index : 0;
	FX_TraceLog("svc_effect %d '%s' flags 0x%02x mode %d ent %d bolt %d scale %.2f count %d\n",
			index, i ? fx_cs_defs[i].name : "?", flags, p.mode, p.ent, p.bolt, p.scale, p.count);

	if (fx_trace && (fx_trace->value >= 2))
	{
		FX_TraceLog("  ext 0x%02x origin (%.1f %.1f %.1f) fwd (%.3f %.3f %.3f)%s pos2 (%.1f %.1f %.1f)\n", ext,
				p.origin[0], p.origin[1], p.origin[2], p.fwd[0], p.fwd[1], p.fwd[2], p.has_pos2 ? "" : " no",
				p.pos2[0], p.pos2[1], p.pos2[2]);
	}

	FX_ShotEvent('e', index);
	FX_StartEffect(FX_ConfigEffect(index), &p, FXS_GAME);
}

/* ---- entity effect slots (SPEC-400 Entity effect slots) ------------------------ */

/* the instance last started by entity effect slot k of entity num still runs `def`
   and is still emitting or has primitives pending (OpenMerc) */
static qboolean
FX_SlotBusy(int num, int k, const fx_def_t *def)
{
	int run = fx_slot_run[num][k] - 1, j;
	const fx_run_t *r;

	if ((run < 0) || (run >= FX_MAX_RUNNING))
	{
		return false;
	}

	r = &fx_running[run];

	if (!r->inuse || (r->serial != fx_slot_serial[num][k]) || (r->def != def) || r->detached ||
		(r->p.ent != num))
	{
		return false;
	}

	for (j = 0; j < r->def->eft.count; j++)
	{
		if (!r->slot[j].done)
		{
			return true;
		}
	}

	return false;
}

static void
FX_EntitySlots(void)
{
	int i, k, n, idx, run;
	entity_state_t *s;
	const short *slots;
	fx_def_t *def;
	fx_params_t p;
	vec3_t angles;

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		n = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		s = &cl_parse_entities[n];

		if ((s->effects & 0x8001) || (s->number <= 0) || (s->number >= 1024))
		{
			continue;
		}

		slots = fx_ext_ring[n].slots;

		for (k = 0; k < 4; k++)
		{
			idx = (unsigned short)slots[k * 3];

			if (!idx || (fx_now < fx_slot_next[s->number][k]))
			{
				continue;
			}

			def = FX_ConfigEffect(idx);

			/* OpenMerc deviation (docs/DEVIATIONS.md): 1.07f restarts a slot every rand(repeat
			   delay) even while the instance it started is still running, so long effects with a
			   short repeat delay (environ/onfireburst: 0.05 s, emitters live 11-13 s) pile up
			   hundreds of copies and fill the running-effect pool. A slot here starts its next
			   instance only once its previous one has stopped emitting and fired every
			   primitive (om_fxslotstack 1: the 1.07f rule). */
			if (!fx_slotstack->value && FX_SlotBusy(s->number, k, def))
			{
				fx_pool_slot_capped++;
				fx_slot_next[s->number][k] = fx_now +
					((def && def->loaded) ? FX_Range(&def->eft.repeat) : 0);
				continue;
			}

			FX_DefaultParams(&p);
			FX_EntityLerpOrigin(s->number, p.origin);
			FX_EntityLerpAngles(s->number, angles);
			AngleVectors(angles, p.fwd, p.right, p.up);
			p.ent = s->number;
			p.bolt = (unsigned short)slots[k * 3 + 1];
			p.mode = p.bolt ? FXA_BOLT : FXA_ENTITY;
			p.ghoul_inst = s->frame & 0xFFFF;

			if (slots[k * 3 + 2])
			{
				p.scale = slots[k * 3 + 2] / 16.0f;
			}

			run = FX_StartEffect(def, &p, FXS_GAME);
			fx_slot_run[s->number][k] = (short)(run + 1);
			fx_slot_serial[s->number][k] = (run >= 0) ? fx_running[run].serial : 0;
			fx_slot_next[s->number][k] = fx_now +
				((def && def->loaded) ? FX_Range(&def->eft.repeat) : 0);
		}
	}
}

/* ---- camera shake (SPEC-400 Camera shake) ----------------------------------------- */

void
CLFX_AddViewShake(float amount, float decay)
{
	fx_shake += amount;

	if (fx_shake > 200)
	{
		fx_shake = 200;
	}

	fx_shake_decay = decay;
}

void
CLFX_ApplyCameraShake(vec3_t vieworg, vec3_t viewangles)
{
	if (fx_shake <= 0.02f)
	{
		return;
	}

	viewangles[PITCH] += fx_shake * 0.04f * (float)(FX_crand() % 100 - 50) / 100.0f;
	viewangles[YAW] += fx_shake * 0.04f * (float)(FX_crand() % 100 - 50) / 100.0f;
	vieworg[0] += fx_shake * 0.01f * (float)(FX_crand() % 5 - 2);
	vieworg[1] += fx_shake * 0.01f * (float)(FX_crand() % 5 - 2);
	fx_shake -= fx_shake_decay * cls.rframetime;
}

/* ---- view weapon (SPEC-400 View weapon) --------------------------------------------- */

static float fx_zoom_last = -1.0e9f; /* client game-time estimate of the last zoom sound */

void
CLFX_ViewWeaponNote(const char *note, qboolean shown, float fov)
{
	char name[MAX_QPATH], bolt[MAX_QPATH];
	fx_params_t p;

	if (!note || !shown || (fov != 95.0f))
	{
		return;
	}

	name[0] = 0;
	Q_strlcpy(bolt, "flash", sizeof(bolt));

	if (sscanf(note, "%63s %63s", name, bolt) < 1)
	{
		return;
	}

	if (strstr(name, "exhaust"))
	{
		Q_strlcpy(bolt, "exhause_l", sizeof(bolt));
	}

	/* attached to the bolt of the view model; without view-model bolts from
	   the Ghoul system the effect follows the view */
	FX_DefaultParams(&p);
	p.mode = FXA_ENTITY;
	p.ent = cl.playernum + 1;
	/* OpenMerc test trace (SPEC-AMEND-011 gates 3 and 6): the own entity must be in the frame, bit 0x4000 clear */
	if (fx_trace && fx_trace->value)
	{
		const entity_state_t *s = FX_EntityCurrent(p.ent);

		Com_Printf("fx: view note '%s' bolt '%s' own ent %d in frame %d effects 0x%x (server frame %d)\n", name, bolt, p.ent,
				FX_EntityInFrame(p.ent), s ? (unsigned)s->effects : 0u, cl.frame.serverframe);
	}

	FX_StartNamed(name, &p, FXS_GAME);
}

void
CLFX_PredictedFire(int code)
{
	trace_t tr;
	int hit, mat, snd = -1;
	vec3_t end;
	float chance = 0, lo = 0, hi = 0;
	fx_params_t p;

	if (fx_noflash && fx_noflash->value)
	{
		return;
	}

	if ((code < 0) || (code > 10))
	{
		return; /* alternate codes (weapon id + 11) do nothing */
	}

	if ((code == 5) && (CL_SoF_GameFov() != 95.0f))
	{
		FX_DefaultParams(&p);
		p.mode = FXA_ENTITY;
		p.ent = cl.playernum + 1;
		FX_StartNamed("weapons/playermz/sniper_alt", &p, FXS_GAME);
	}

	VectorCopy(cl.refdef.vieworg, end);
	end[2] -= 256;
	FX_Trace(cl.refdef.vieworg, NULL, NULL, end, FXTR_WORLD, &tr, &hit);

	if (tr.fraction >= 1.0f)
	{
		return;
	}

	mat = FX_TraceMaterial(&tr);

	if (mat >= FX_NUM_MATERIALS)
	{
		return;
	}

	switch (code)
	{
		case 1: case 2: case 3: case 4:
			chance = 0.5f; lo = 0.4f; hi = 0.6f;
			break;
		case 5: case 8:
			chance = 1.0f; lo = 0.4f; hi = 0.7f;
			break;
		case 7:
			chance = 0.75f; lo = 0.4f; hi = 0.8f;
			break;
		default:
			return;
	}

	if (FX_flrand(0, 1) >= chance)
	{
		return;
	}

	if ((fx_materials[mat].impact == 37) || (fx_materials[mat].impact == 39))
	{
		if ((code == 5) || (code == 8))
		{
			snd = FX_irand(111, 113);
		}
		else if (code == 7)
		{
			snd = FX_irand(108, 110);
		}
		else
		{
			snd = FX_irand(97, 98);
		}
	}
	else if (fx_materials[mat].impact == 44)
	{
		if ((code == 5) || (code == 8))
		{
			snd = FX_irand(111, 113);
		}
		else if (code == 7)
		{
			snd = FX_irand(108, 110);
		}
		else
		{
			snd = FX_irand(94, 96);
		}
	}

	if (snd >= 0)
	{
		FX_PlaySound(snd, tr.endpos, 1.0f, 2.0f, -1, 0, FX_flrand(lo, hi));
	}
}

void
CLFX_ZoomChanged(void)
{
	/* SPEC-AMEND-009 D1: on the client game-time estimate, more than 0.18 s since
	   the last play, or that clock moved back by more than 1 s (new level, load) */
	float now = CL_Ghoul_GameClock();

	if (!((now - fx_zoom_last > 0.18f) || (fx_zoom_last - now > 1.0f)))
	{
		return;
	}

	fx_zoom_last = now;
	/* local sound: listener entity, channel 0, volume 0.6, attenuation 1 */
	S_StartSound(NULL, cl.playernum + 1, 0, S_RegisterSound("Weapons/Sniper/Zoom.wav"),
			0.6f, 1.0f, 0);
}

/* ---- entity drawing helpers (SPEC-400 Entity effects bits) ------------------------- */

void
CLFX_EntityAngles(const entity_state_t *s, const vec3_t net_angles, vec3_t out)
{
	const short *v = CLFX_StateSlots(s);
	int i;

	VectorCopy(net_angles, out);

	if (!v || !(s->effects & 0x0001) || v[4])
	{
		return; /* instance-rotating branch (v4 != 0) needs Ghoul instances */
	}

	for (i = 0; i < 3; i++)
	{
		out[i] = anglemod(net_angles[i] + v[1 + i] * (float)cl.time / 512.0f);
	}
}

qboolean
CLFX_EntityTint(const entity_state_t *s, float tint[4])
{
	const short *v = CLFX_StateSlots(s);
	float t = cl.time * 0.001f, sn;
	int start, d;

	tint[0] = tint[1] = tint[2] = tint[3] = 1;

	if (s->effects & 0x0800)
	{
		sn = sinf(5 * t);
		tint[0] = 0.6f - 0.3f * sn;
		tint[1] = 0.8f - 0.3f * sn;
		tint[2] = 1;
		tint[3] = 0.6f + 0.2f * sn;
		return true;
	}

	if ((s->effects & 0x8000) && v)
	{
		start = (unsigned short)v[0] + 65536 * (unsigned short)v[1];
		d = v[2];

		if (d > 0)
		{
			tint[3] = (cl.time < start + d) ? 1.0f - (float)(cl.time - start) / d : 0.0f;
		}
		else if (d < 0)
		{
			tint[3] = (cl.time < start - d) ? (float)(cl.time - start) / -d : 1.0f;
		}

		if (tint[3] < 0)
		{
			tint[3] = 0;
		}

		if (tint[3] > 1)
		{
			tint[3] = 1;
		}

		return true;
	}

	return false;
}

/* ---- editor (SPEC-400 Editor) ------------------------------------------------------- */

static void
FX_Load_f(void)
{
	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: fx_load <EffectFileName>\n");
		return;
	}

	fx_editloaded = FX_LoadDef(&fx_editdef, Cmd_Argv(1));

	if (fx_editloaded)
	{
		Com_Printf("loaded effects/%s.eft (%d primitives)\n", Cmd_Argv(1), fx_editdef.eft.count);
	}
}

static void
FX_Save_f(void)
{
	char path[MAX_OSPATH];
	static unsigned char buf[16384];
	int n;
	FILE *f;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: fx_save <EffectFileName>\n");
		return;
	}

	if (!fx_editloaded)
	{
		Com_Printf("fx_save: no effect is being edited\n");
		return;
	}

	n = FX_WriteEft(&fx_editdef.eft, buf, sizeof(buf));

	if (n < 0)
	{
		Com_Printf("fx_save: effect too large\n");
		return;
	}

	Com_sprintf(path, sizeof(path), "%s/base/effects/%s.eft", fx_editdir->string, Cmd_Argv(1));
	FS_CreatePath(path);
	f = Q_fopen(path, "wb");

	if (!f || (fwrite(buf, 1, n, f) != (size_t)n))
	{
		Com_Printf("fx_save: could not write %s\n", path);

		if (f)
		{
			fclose(f);
		}

		return;
	}

	fclose(f);
	Com_Printf("wrote %s (%d bytes)\n", path, n);
}

static void
FX_EditorStep(void)
{
	fx_params_t p;

	if (!fx_edit->value || !fx_editloaded)
	{
		return;
	}

	FX_DefaultParams(&p);
	VectorSet(p.origin, 0, 0, 32);

	if (fx_run->value)
	{
		Cvar_Set("fx_run", "0");
		FX_StartEffect(&fx_editdef, &p, FXS_EDITOR);
		fx_editnext = fx_now + FX_Range(&fx_editdef.eft.repeat);
	}
	else if ((fx_editdef.eft.repeat_mode == 1) && (fx_editnext > 0) && (fx_now >= fx_editnext))
	{
		FX_StartEffect(&fx_editdef, &p, FXS_EDITOR);
		fx_editnext = fx_now + FX_Range(&fx_editdef.eft.repeat);
	}

	if ((int)fx_editsegment->value < 0 || (int)fx_editsegment->value > 4)
	{
		Cvar_SetValue("fx_editsegment", (float)(((int)fx_editsegment->value % 5 + 5) % 5));
	}
}

/* ---- life cycle ------------------------------------------------------------------------ */

/* OpenMerc: running-effect pool statistics since the level start */
static void
FX_Pool_f(void)
{
	int i, running = 0, own = 0;

	for (i = 0; i < FX_MAX_RUNNING; i++)
	{
		running += fx_running[i].inuse;
		own += fx_running[i].inuse && fx_running[i].priority;
	}

	Com_Printf("fxpool: running %d/%d (own %d); since level start: dropped world %d, dropped own %d, "
			"evicted %d, slot restarts skipped %d\n", running, FX_MAX_RUNNING, own, fx_pool_drop_world,
			fx_pool_drop_own, fx_pool_evicted, fx_pool_slot_capped);
}

void
CLFX_Init(void)
{
	if (fx_inited)
	{
		return;
	}

	fx_inited = true;

	fx_sim_time = Cvar_Get("fx_sim_time", "60", 0);
	cl_max_quads = Cvar_Get("cl_max_quads", "256", CVAR_ARCHIVE);
	cl_max_debris = Cvar_Get("cl_max_debris", "128", CVAR_ARCHIVE);
	fx_maxdebrisonscreen = Cvar_Get("fx_maxdebrisonscreen", "16", 0);
	cl_freezequads = Cvar_Get("cl_freezequads", "0", 0);
	freezeworld = Cvar_Get("freezeworld", "0", 0);
	fx_noflash = Cvar_Get("fx_noflash", "0", 0);
	fx_edit = Cvar_Get("fx_edit", "0", 0);
	fx_editsegment = Cvar_Get("fx_editsegment", "0", 0);
	fx_editdir = Cvar_Get("fx_editdir", "p:", CVAR_ARCHIVE);
	fx_run = Cvar_Get("fx_run", "0", 0);
	fx_partadd = Cvar_Get("fx_partadd", "0", 0);
	fx_partsub = Cvar_Get("fx_partsub", "0", 0);
	fx_partadjust = Cvar_Get("fx_partadjust", "0", 0);
	fx_graphic = Cvar_Get("fx_graphic", "0", 0);
	fx_sound = Cvar_Get("fx_sound", "null", 0);
	fx_collide = Cvar_Get("fx_collide", "0", 0);
	fx_trail = Cvar_Get("fx_trail", "0", 0);
	sc_framerate = Cvar_Get("sc_framerate", "30", 0);
	/* OpenMerc additions (not in 1.07f): verification log, interim drawing */
	fx_trace = Cvar_Get("om_fxtrace", "0", 0);
	fx_interim = Cvar_Get("om_fxinterim", "1", 0);
	fx_enable = Cvar_Get("om_fx", "1", 0);
	fx_slotstack = Cvar_Get("om_fxslotstack", "0", 0);
	FX_ShotInit();

	Cmd_AddCommand("fx_save", FX_Save_f);
	Cmd_AddCommand("fx_load", FX_Load_f);
	Cmd_AddCommand("om_fxselftest", FX_SelfTest_f); /* OpenMerc test vectors */
	Cmd_AddCommand("om_fxpool", FX_Pool_f);
	Cmd_AddCommand("om_fxmeatdemo", FX_MeatDemo_f);
}

qboolean
CLFX_Wanted(void)
{
	CLFX_Init();
	return fx_enable->value != 0;
}

void
CLFX_Shutdown(void)
{
	if (!fx_inited)
	{
		return;
	}

	Cmd_RemoveCommand("fx_save");
	Cmd_RemoveCommand("fx_load");
	Cmd_RemoveCommand("om_fxselftest");
	Cmd_RemoveCommand("om_fxpool");
	Cmd_RemoveCommand("om_fxmeatdemo");
	fx_inited = false;
}

void
CLFX_ClearState(void)
{
	CLFX_Init();
	FX_ClearRunning();
	FX_ClearParticles();
	FX_ClearDecals();
	FX_ClearDebris();
	FX_ClearTempState();
	memset(fx_slot_next, 0, sizeof(fx_slot_next));
	memset(fx_slot_run, 0, sizeof(fx_slot_run));
	memset(fx_slot_serial, 0, sizeof(fx_slot_serial));
	fx_pool_drop_world = fx_pool_drop_own = fx_pool_evicted = fx_pool_slot_capped = 0;
	fx_shake = 0;
	fx_shake_decay = 0;
	fx_now = cl.time * 0.001f;
}

void
CLFX_Precache(void)
{
	int i;
	const char *name;

	CLFX_Init();

	/* the precache clears every definition and every running effect first */
	memset(fx_cs_defs, 0, sizeof(fx_cs_defs));
	memset(fx_named, 0, sizeof(fx_named));
	fx_num_named = 0;
	CLFX_ClearState();
	FX_ClearSoundList();

	for (i = 1; i < FX_MAX_DEFS; i++)
	{
		name = CL_SoF_ConfigString(FX_CS_EFFECTS + i);

		if (name && name[0])
		{
			FX_LoadDef(&fx_cs_defs[i], name);
		}
	}

	FX_RegisterSprites();
	FX_PreloadDebris();
}

/* ---- per rendered frame (SPEC-400 Timebase) ----------------------------------------------- */

static void
FX_Step(float dt)
{
	FX_UpdateRunning();

	if (!FX_Frozen())
	{
		FX_RunDecals(dt);
		FX_RunDebris(dt);
		FX_RunParticles(dt);
	}
}

/* at most 0.2 s of catch-up per frame; every step integrates a full step,
   the clock is clamped to the client time (SPEC-400 Timebase item 2) */
int
FX_AdvanceClock(float *now, float ctime, float dt, void (*step)(float))
{
	int steps = 0;

	if (*now < ctime - 0.2f)
	{
		*now = ctime - 0.2f;
	}

	/* 0.1 ms tolerance: in single precision k * dt sums fall just short of the
	   client time (10 + 3/60 < 10.05f) and would run one step too many */
	while (*now < ctime - 0.0001f)
	{
		if (step)
		{
			step(dt);
		}

		*now += dt;
		steps++;

		if (*now > ctime)
		{
			*now = ctime;
		}
	}

	return steps;
}

static int fx_stats_time, fx_stat_frames, fx_stat_steps;

void
CLFX_Frame(void)
{
	float ctime, dt, rate;
	int steps = 0;

	CLFX_Init();

	if (cls.state != ca_active)
	{
		return;
	}

	ctime = cl.time * 0.001f;
	rate = fx_sim_time->value;

	if (rate < 1)
	{
		rate = 1;
	}

	dt = 1.0f / rate;

	/* smoothed frame rate for the effect detail factor (SPEC-310 s.12 item 10) */
	if (cls.rframetime > 0)
	{
		float f = 1.0f / cls.rframetime;

		if (f < 5)
		{
			f = 5;
		}

		fx_framerate = 0.9f * fx_framerate + 0.1f * f;
	}

	(void)fx_last_realtime;

	/* the clock went backwards by a lot (new level): restart it */
	if (fx_now > ctime + 1.0f)
	{
		fx_now = ctime;
	}

	CLFX_ApplyCameraShake(cl.refdef.vieworg, cl.refdef.viewangles);

	if (!FX_Frozen())
	{
		FX_FreeDeadParticles();
	}

	FX_EntitySlots();
	FX_EditorStep();

	/* the original's order (SPEC-AMEND-005 I5.2): submission, the effects steps, then the
	   renderer reads the referenced records (RenderFrame from the view code) */
	FX_BeginScene();
	FX_SubmitDecals(NULL, 0, 0);
	FX_SubmitDebris();
	FX_SubmitParticles();
	FX_SubmitChains();
	CL_SoF_HoldNewLights(true);

	steps = FX_AdvanceClock(&fx_now, ctime, dt, FX_Step);
	fx_stat_steps += steps;
	fx_stat_frames++;

	CL_SoF_HoldNewLights(false);
	FX_RefreshScene();
	FX_InterimDraw();
	FX_ShotFrame();

	/* om_fxtrace 2: once a second what lives and what is handed to the renderer */
	if (fx_trace && (fx_trace->value >= 2) && (cls.realtime - fx_stats_time >= 1000))
	{
		int i, kinds[5] = {0, 0, 0, 0, 0}, emitted = 0, running = 0;
		sof_alphaquad_t **ql;
		sof_quadchain_t **cl_;

		fx_stats_time = cls.realtime;

		for (i = 0; i < FX_MAX_PARTICLES; i++)
		{
			kinds[fx_parts[i].kind]++;
			emitted += (fx_parts[i].kind != FXK_FREE) && (fx_parts[i].fx >= 0);
		}

		for (i = 0; i < FX_MAX_RUNNING; i++)
		{
			running += fx_running[i].inuse;
		}

		FX_TraceLog("FXSTATS t %.2f particles %d (billboard %d, line %d, ghoul %d, timer %d; from emitters %d), "
				"running effects %d, scene quads %d, chains %d; last second: chains %d with %d quads, emitter "
				"particles freed by age %d, by alpha < 2 %d, without image %d; frames %d, effects steps %d\n", fx_now,
				FX_MAX_PARTICLES - kinds[FXK_FREE], kinds[FXK_BILLBOARD], kinds[FXK_LINE], kinds[FXK_GHOUL],
				kinds[FXK_TIMER], emitted, running, CLFX_SceneAlphaQuads(&ql), CLFX_SceneQuadChains(&cl_),
				fx_stat_chains, fx_stat_chainquads, fx_stat_freed[0], fx_stat_freed[1], fx_stat_freed[2],
				fx_stat_frames, fx_stat_steps);
		fx_stat_frames = fx_stat_steps = 0;
		FX_TraceDebris();
		fx_stat_chains = fx_stat_chainquads = 0;
		fx_stat_freed[0] = fx_stat_freed[1] = fx_stat_freed[2] = 0;
	}
}

float
FX_FrameRate(void)
{
	return fx_framerate;
}

float
FX_DetailFactor(void)
{
	float k = fx_framerate / (sc_framerate->value > 0 ? sc_framerate->value : 30);

	return k > 1 ? 1 : k;
}
