/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: the Ghoul services the effects need, on the client Ghoul system of package I1c.
 * - Bolt frames of server instances (svc_effect / entity effect slots attached to a bolt, the muzzle bolt of
 *   temp entities 60-63, fx_ghoulbolt.cpp), in world space with the entity matrix the renderer uses for Ghoul
 *   entities.
 * - Models and instances the effects create themselves (debris, Ghoul-model particles, fx_ghoulmodel.cpp):
 *   drawn as renderer entities with flags Ghoul model + ambient-only lighting and frame = -(client instance
 *   id) (SPEC-AMEND-005 I4b.1, I4b.3). Without Ghoul drawing om_fxinterim shows placeholders for debris.
 *
 * Implements: SPEC-400 (svc_effect bolt attachment: axes from the bolt matrix, bolt 0; Debris 1-2; Ghoul
 * particles; Rendering hand-off 2/3/5), SPEC-401 (types 60-63 muzzle point; chunk parts and skins),
 * SPEC-010 (client use of the model system), SPEC-AMEND-005 I4b.1, I4b.3, I5.9
 */

#include "fx_local.h"
#include "../header/cl_ghoul.h"

/* fx_ghoulbolt.cpp */
int FXGB_BoltMatrix(int handle, int bolt, float time, float out[16]);
int FXGB_RandomPart(int handle, int (*rnd)(int));
int FXGB_MuzzleMatrix(int handle, float time, float out[16]);
int FXGB_HasActivePart(int handle, const char *name);

/* fx_ghoulmodel.cpp */
void *FXGM_Model(const char *path);
void *FXGM_NewInstance(void *model, float time, const char *material, const char *skin, const char *part);
int FXGM_PartName(void *model, int index, char *out, int size);
void FXGM_FreeInstance(void *instance);
int FXGM_Frame(void *instance);
void FXGM_SetScaleAlpha(void *instance, float scale, float alpha);

/* a model record for debris (dir, file of SPEC-400 Debris 1) or an effect's Ghoul graphic (dir NULL,
   file = path below ghoul/ without extension) */
void *
FXG_RegisterModel(const char *dir, const char *file)
{
	char path[MAX_QPATH];

	if (!file || !file[0])
	{
		return NULL;
	}

	{
		char base[MAX_QPATH], *dot, *slash;

		/* the file without an extension; ".ghl" names the sequence (SPEC-010 typical set-up) */
		Q_strlcpy(base, file, sizeof(base));
		dot = strrchr(base, '.');
		slash = strrchr(base, '/');

		if (dot && (!slash || (dot > slash)))
		{
			*dot = 0;
		}

		if (dir)
		{
			Com_sprintf(path, sizeof(path), "ghoul/%s/%s.ghl", dir, base);
		}
		else
		{
			Com_sprintf(path, sizeof(path), "ghoul/%s.ghl", base);
		}
	}

	return FXGM_Model(path);
}

qboolean
FXG_InstanceHasPart(int handle, const char *name)
{
	return CL_Ghoul_Active() && handle && FXGB_HasActivePart(handle, name);
}

void
FXG_FreeInstance(void *ginst)
{
	if (ginst)
	{
		FXGM_FreeInstance(ginst);
	}
}

/* SPEC-401 chunk types: the material group of each debris model's skins */
static const char *
FXG_MaterialGroup(int model)
{
	if (model == 0)
	{
		return "Brick";
	}

	if ((model >= 1) && (model <= 3))
	{
		return "Glass";
	}

	if ((model >= 4) && (model <= 6))
	{
		return "Metal";
	}

	if ((model >= 7) && (model <= 10))
	{
		return "Rock";
	}

	if ((model >= 11) && (model <= 14))
	{
		return "Wall";
	}

	if ((model >= 15) && (model <= 17))
	{
		return "Wood";
	}

	return NULL;
}

static void
FXG_AddInstanceEntity(void *ginst, const vec3_t org, const vec3_t angles)
{
	entity_t ent;
	int frame = FXGM_Frame(ginst);

	if (!frame)
	{
		return;
	}

	memset(&ent, 0, sizeof(ent));
	VectorCopy(org, ent.origin);
	VectorCopy(org, ent.oldorigin);
	VectorCopy(angles, ent.angles);
	ent.frame = frame;
	ent.oldframe = frame;
	ent.alpha = 1.0f;
	ent.flags = SOF_RF_GHOUL | SOF_RF_AMBIENTONLY;
	V_AddEntity(&ent);
}

qboolean
FXG_Available(void)
{
	return CL_Ghoul_Active() ? true : false;
}

static int
FXG_Rand(int n)
{
	return (n > 0) ? FX_irand(0, n - 1) : 0;
}

/* the Ghoul instance handle of a packet entity drawn as a Ghoul model (SPEC-002 entity 0x34), 0 = none */
static int
FXG_EntityHandle(const entity_state_t *s)
{
	if (!s || !(s->renderfx & SOF_RF_GHOUL))
	{
		return 0;
	}

	return s->frame & 0xFFFF;
}

/* the renderer's entity matrix of a Ghoul entity (rows forward, -right, up of the angle vectors of
   (-pitch, yaw, -roll) of the entity's drawn angles, then the origin; gl3_ghoul.c / SPEC-011) */
static void
FXG_EntityMatrix(int num, const entity_state_t *s, vec3_t axes[3], vec3_t origin)
{
	entity_t tmp;
	vec3_t a, r;

	memset(&tmp, 0, sizeof(tmp));
	CL_Ghoul_EntityValues(&tmp, s);
	a[0] = -tmp.angles[0];
	a[1] = tmp.angles[1];
	a[2] = -tmp.angles[2];
	AngleVectors(a, axes[0], r, axes[2]);
	VectorNegate(r, axes[1]);
	FX_EntityLerpOrigin(num, origin);
}

/* row matrix b (entity space) to world: rows 0..2 axes, row 3 the point */
static void
FXG_ToWorld(const float b[16], vec3_t axes[3], const vec3_t origin, vec3_t out[4])
{
	int i, k;

	for (i = 0; i < 4; i++)
	{
		for (k = 0; k < 3; k++)
		{
			out[i][k] = b[i * 4 + 0] * axes[0][k] + b[i * 4 + 1] * axes[1][k] + b[i * 4 + 2] * axes[2][k] +
				((i == 3) ? origin[k] : 0);
		}
	}
}

qboolean
FXG_BoltFrame(int ent, int ghoul_inst, int bolt, qboolean random, vec3_t org, vec3_t fwd,
		vec3_t right, vec3_t up, qboolean reversed)
{
	const entity_state_t *s;
	vec3_t axes[3], origin, w[4];
	float b[16];
	int handle;

	if (!CL_Ghoul_Active() || FX_IsLocalPlayer(ent) || !FX_EntityInFrame(ent))
	{
		return false;
	}

	s = FX_EntityCurrent(ent);
	handle = ghoul_inst ? ghoul_inst : FXG_EntityHandle(s);

	if (!handle)
	{
		return false;
	}

	if (random || !bolt)
	{
		bolt = FXGB_RandomPart(handle, FXG_Rand);
	}

	if (!bolt || !FXGB_BoltMatrix(handle, bolt, CL_Ghoul_GameClock(), b))
	{
		return false;
	}

	FXG_EntityMatrix(ent, s, axes, origin);
	FXG_ToWorld(b, axes, origin, w);
	VectorCopy(w[3], org);

	/* forward = bolt Z, right = bolt Y, up = bolt X; reversed (flag 0x40): X, Z, Y */
	if (reversed)
	{
		VectorCopy(w[0], fwd);
		VectorCopy(w[2], right);
		VectorCopy(w[1], up);
	}
	else
	{
		VectorCopy(w[2], fwd);
		VectorCopy(w[1], right);
		VectorCopy(w[0], up);
	}

	return true;
}

/* SPEC-401 types 60-63 for a shooter in the frame: the world position of bolt `flash` on the model at the
   shooter's bolt `wbolt_hand_r`; (0, 0, 0) when that model has no `flash` bolt (quirk). False when there is
   no such model (or no Ghoul drawing): the caller keeps the payload start. */
qboolean
FXG_MuzzlePoint(int shooter, vec3_t out)
{
	const entity_state_t *s;
	vec3_t axes[3], origin, w[4];
	float b[16];
	int handle, r;

	if (!CL_Ghoul_Active() || !FX_EntityInFrame(shooter))
	{
		return false;
	}

	s = FX_EntityCurrent(shooter);
	handle = FXG_EntityHandle(s);

	if (!handle)
	{
		return false;
	}

	r = FXGB_MuzzleMatrix(handle, CL_Ghoul_GameClock(), b);

	if (r == 0)
	{
		return false;
	}

	if (r == 2)
	{
		VectorClear(out);
		return true;
	}

	FXG_EntityMatrix(shooter, s, axes, origin);
	FXG_ToWorld(b, axes, origin, w);
	VectorCopy(w[3], out);
	return true;
}

void
FXG_AddDebrisEntity(fx_debris_t *d)
{
	entity_t ent;

	if (!CL_Ghoul_Active() || !d->inst)
	{
		if (!fx_interim || !fx_interim->value)
		{
			return;
		}

		/* placeholder: the renderer's null-model marker at the piece */
		memset(&ent, 0, sizeof(ent));
		VectorCopy(d->pos, ent.origin);
		VectorCopy(d->pos, ent.oldorigin);
		VectorCopy(d->angles, ent.angles);
		V_AddEntity(&ent);
		return;
	}

	if (d->ginst && !FXGM_Frame(d->ginst))
	{
		FXG_FreeInstance(d->ginst); /* the client system was reset */
		d->ginst = NULL;
	}

	if (!d->ginst)
	{
		const char *part = NULL;
		char partname[64];

		/* chunk models: one part of the model, chosen by the creator (SPEC-401: irand(0, n), n = no such part);
		   the parts are taken from the model itself, in alphabetical order.
		   part=NULL leaves every mesh on — for ROCK_LG that is a half-person cluster.
		   Emitter debris (model < 0) keeps NULL (single-mesh chunks_meat / shells). */
		if ((d->model >= 0) && (d->model < FX_NUM_DEBRIS_MODELS) && (fx_debris_models[d->model].parts > 1))
		{
			int n = fx_debris_models[d->model].parts;

			part = ((d->part >= 0) && (d->part < n) &&
				FXGM_PartName(d->inst, d->part, partname, sizeof(partname))) ? partname : "";
		}

		d->ginst = FXGM_NewInstance(d->inst, CL_Ghoul_GameClock(),
				(d->model >= 0) ? FXG_MaterialGroup(d->model) : NULL, d->skin, part);

		if (!d->ginst)
		{
			return;
		}

		if (fx_trace && (fx_trace->value >= 2))
		{
			FX_TraceLog("  debris newinst model %d (%s) part '%s' skin '%s' scale %.3f at (%.1f %.1f %.1f)\n",
					d->model,
					((d->model >= 0) && (d->model < FX_NUM_DEBRIS_MODELS)) ? fx_debris_models[d->model].file : "emitter",
					part ? part : "(all)",
					d->skin ? d->skin : "",
					d->scale ? d->scale : 1.0f,
					d->pos[0], d->pos[1], d->pos[2]);
		}
	}

	FXGM_SetScaleAlpha(d->ginst, d->scale ? d->scale : 1.0f, -1);
	FXG_AddInstanceEntity(d->ginst, d->pos, d->angles);
}

/* Ghoul-model particles (types 3, 4 of an effect) */
void
FXG_AddParticleEntity(fx_part_t *p)
{
	if (!CL_Ghoul_Active() || !p->model)
	{
		return;
	}

	if (p->ginst && !FXGM_Frame(p->ginst))
	{
		FXG_FreeInstance(p->ginst);
		p->ginst = NULL;
	}

	if (!p->ginst)
	{
		p->ginst = FXGM_NewInstance(p->model, CL_Ghoul_GameClock(), NULL, NULL, NULL);

		if (!p->ginst)
		{
			return;
		}
	}

	FXGM_SetScaleAlpha(p->ginst, p->gscale, p->arate ? p->alpha / 255.0f : -1);
	FXG_AddInstanceEntity(p->ginst, p->pos, p->gangles);
}
