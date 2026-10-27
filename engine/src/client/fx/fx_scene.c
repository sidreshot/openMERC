/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: hands the frame to the renderer through the SPEC-002
 * export table of the OpenMerc GL3 renderer (OM_GetSoFRefExports, package
 * I4a, common/header/sof/refapi.h) so alpha quads and quad chains are drawn:
 * the Quake II refdef the view code builds becomes a SPEC-002 frame record
 * (entities, dynamic lights, light styles) plus the effects' scene lists.
 * Without that table (other renderers, the Quake II protocol, or cvar
 * om_fxrefapi 0) the Quake II RenderFrame is used unchanged. Also gives the
 * effects the renderer's sprite registration (slot 0x28) and light sampling
 * (slot 0xa4).
 *
 * Implements: SPEC-002 (frame record 0x00-0x80, slots 0x28, 0x40, 0xa4),
 * SPEC-400 (Rendering hand-off)
 */

#include "fx_local.h"
#include "../header/cl_ghoul.h" /* I1c: game clock */

extern void *reflib_handle; /* vid.c */

static const sofrefexport_t *fx_re;
static void *fx_re_handle;
static qboolean fx_re_tried;
static cvar_t *fx_refapi;

static sofrefdef_t fx_fd;
static sofentity_t fx_ents[SOF_MAX_ENTITIES];
static sofentity_t *fx_entptrs[SOF_MAX_ENTITIES];
static sofdlight_t fx_dlights[SOF_MAX_DLIGHTS];
static float fx_dirtable[250][3];

static void FX_ViewOverride(void);
static void FX_SceneDump(void);

const sofrefexport_t *
FX_Ref(void)
{
	if (reflib_handle != fx_re_handle)
	{
		fx_re_handle = reflib_handle;
		fx_re = NULL;
		fx_re_tried = false;
	}

	if (!fx_re_tried && reflib_handle)
	{
		OM_GetSoFRefExports_t f;
		void (*setdir)(const float (*table)[3], int count);
		int i;

		fx_re_tried = true;
		f = (OM_GetSoFRefExports_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_EXPORTS_NAME);
		fx_re = f ? f() : NULL;

		/* the data of SPEC-002 import 0x68 for a Yamagi-hosted renderer: the SPEC-201 direction table,
		   250 entries (SPEC-AMEND-005 I4a.4); lit alpha quads (decals: -normal) sample their light along it */
		setdir = (void (*)(const float (*)[3], int))Sys_GetProcAddress(reflib_handle, "OM_SetSoFDirTable");

		if (setdir)
		{
			for (i = 0; i < 250; i++)
			{
				CL_SoF_DirVector(i, fx_dirtable[i]);
			}

			setdir((const float (*)[3])fx_dirtable, 250);
		}
	}

	return fx_re;
}

/* the effects draw through the SPEC-002 frame record */
qboolean
FX_SceneActive(void)
{
	const sofrefexport_t *re;

	if (!fx_refapi)
	{
		fx_refapi = Cvar_Get("om_fxrefapi", "1", 0);
	}

	if (!fx_refapi->value || !CL_SoF_Active())
	{
		return false;
	}

	re = FX_Ref();
	return re && re->RenderFrame;
}

void
FX_RegisterSpriteImage(const char *name)
{
	const sofrefexport_t *re = FX_Ref();
	char buf[MAX_QPATH];

	if (re && re->RegisterSprite && name && name[0])
	{
		Q_strlcpy(buf, name, sizeof(buf));
		re->RegisterSprite(buf);
	}
}

qboolean
FX_LightPoint(const vec3_t p, vec3_t out)
{
	const sofrefexport_t *re = FX_Ref();
	vec3_t pt;

	if (!re || !re->LightPoint)
	{
		return false;
	}

	VectorCopy(p, pt);
	re->LightPoint(pt, out, 255);
	return true;
}

void
CLFX_RenderFrame(refdef_t *rd)
{
	const sofrefexport_t *re;
	int i, n;
	const entity_t *e;
	sofentity_t *se;

	if (!FX_SceneActive())
	{
		R_RenderFrame(rd);
		return;
	}

	re = FX_Ref();
	memset(&fx_fd, 0, sizeof(fx_fd));
	fx_fd.fps = FX_FrameRate();
	fx_fd.x = rd->x;
	fx_fd.y = rd->y;
	fx_fd.width = rd->width;
	fx_fd.height = rd->height;
	fx_fd.fov_x = rd->fov_x;
	fx_fd.fov_y = rd->fov_y;
	VectorCopy(rd->vieworg, fx_fd.vieworg);
	VectorCopy(rd->viewangles, fx_fd.viewangles);
	memcpy(fx_fd.blend, rd->blend, sizeof(fx_fd.blend));
	fx_fd.time = rd->time;
	/* I1c: the game clock (SPEC-202 section 5; animation clock of the renderer and of Ghoul models) */
	CL_Ghoul_FrameRecordClock(&fx_fd.oldgametime, &fx_fd.gametime_delta, &fx_fd.gametime); /* AMEND-006 I1c.1 */
	fx_fd.rdflags = rd->rdflags;
	fx_fd.areabits = rd->areabits;
	fx_fd.lightstyles = (soflightstyle_t *)rd->lightstyles;

	n = rd->num_entities;

	if (n > SOF_MAX_ENTITIES)
	{
		n = SOF_MAX_ENTITIES;
	}

	for (i = 0; i < n; i++)
	{
		e = &rd->entities[i];
		se = &fx_ents[i];
		memset(se, 0, sizeof(*se));
		se->model = e->model;
		VectorCopy(e->angles, se->angles);
		VectorCopy(e->origin, se->origin);
		se->frame = e->frame;
		se->oldframe = e->oldframe;
		se->backlerp = e->backlerp;
		se->skinnum = e->skinnum;
		se->alpha = e->alpha;
		se->skin = e->skin;
		se->flags = e->flags;
		se->unknown5c = 255;
		fx_entptrs[i] = se;
	}

	fx_fd.num_entities = n;
	fx_fd.entities = fx_entptrs;

	/* the renderer appends lights after the engine's: room for 32 */
	n = rd->num_dlights;

	if (n > SOF_MAX_DLIGHTS)
	{
		n = SOF_MAX_DLIGHTS;
	}

	for (i = 0; i < n; i++)
	{
		VectorCopy(rd->dlights[i].origin, fx_dlights[i].origin);
		VectorCopy(rd->dlights[i].color, fx_dlights[i].color);
		fx_dlights[i].intensity = rd->dlights[i].intensity;
	}

	fx_fd.num_dlights = n;
	fx_fd.dlights = fx_dlights;

	fx_fd.num_quadchains = CLFX_SceneQuadChains(&fx_fd.quadchains);
	fx_fd.num_alphaquads = CLFX_SceneAlphaQuads(&fx_fd.alphaquads);
	fx_fd.scratch = NULL;

	FX_ViewOverride();
	FX_SceneDump();
	re->RenderFrame(&fx_fd);
}

/* OpenMerc test aid (not in 1.07f): cvar om_fxdump = interval in ms (0 = off). Every interval the
   frame record's effect part (alpha quads and every quad of every chain) is written to
   <user>/fxdump/d_<client ms>.txt in the line format of the capture shim's renderer trace
   (gl3/test/refcap/reftrace.h), so the scenes of both clients can be compared by one script. */
static void
FX_SceneDump(void)
{
	static cvar_t *dump;
	static int last;
	char path[MAX_OSPATH];
	FILE *f;
	int i, k;

	if (!dump)
	{
		dump = Cvar_Get("om_fxdump", "0", 0);
	}

	if ((dump->value <= 0) || (cls.realtime - last < (int)dump->value) ||
			!(fx_fd.num_alphaquads + fx_fd.num_quadchains))
	{
		return;
	}

	last = cls.realtime;
	Com_sprintf(path, sizeof(path), "%s/fxdump/d_%08d.txt", FS_Userdir(), cls.realtime);
	FS_CreatePath(path);
	f = Q_fopen(path, "w");

	if (!f)
	{
		return;
	}

	fprintf(f, "RenderFrame org(%g %g %g) ang(%g %g %g) time %g ents %d chains %d quads %d\n", fx_fd.vieworg[0],
			fx_fd.vieworg[1], fx_fd.vieworg[2], fx_fd.viewangles[0], fx_fd.viewangles[1], fx_fd.viewangles[2],
			fx_fd.time, fx_fd.num_entities, fx_fd.num_quadchains, fx_fd.num_alphaquads);

	for (i = 0; i < fx_fd.num_alphaquads; i++)
	{
		const sofalphaquad_t *q = fx_fd.alphaquads[i];

		fprintf(f, "  quad %d %s flags %04x org(%g %g %g) v(%g %g %g) f18 %g f1c %g rgba(%d %d %d %d) leaf %d\n", i,
				q->texname ? q->texname : "(null)", q->flags, q->origin[0], q->origin[1], q->origin[2], q->v0c[0],
				q->v0c[1], q->v0c[2], q->f18, q->f1c, q->color[0], q->color[1], q->color[2], q->color[3], q->leaf);
	}

	for (i = 0; i < fx_fd.num_quadchains; i++)
	{
		const sofquadchain_t *c = fx_fd.quadchains[i];
		const sofalphaquad_t *q = c->first;

		fprintf(f, "  chain %d count %d weight %g first %s flags %04x\n", i, c->count, c->weight,
				(q && q->texname) ? q->texname : "-", q ? q->flags : 0);
		fprintf(f, "    box (%g %g %g)-(%g %g %g)\n", c->mins[0], c->mins[1], c->mins[2], c->maxs[0], c->maxs[1],
				c->maxs[2]);

		for (k = 0; q && (k < c->count) && (k < 1024); k++, q = q->next)
		{
			fprintf(f, "    q %d %s flags %04x org(%g %g %g) v(%g %g %g) f18 %g f1c %g rgba(%d %d %d %d)\n", k,
					q->texname ? q->texname : "(null)", q->flags, q->origin[0], q->origin[1], q->origin[2],
					q->v0c[0], q->v0c[1], q->v0c[2], q->f18, q->f1c, q->color[0], q->color[1], q->color[2],
					q->color[3]);
		}
	}

	fclose(f);
}

/* OpenMerc test aid (not in 1.07f), the same as the original-renderer capture shim's
   `om_capview` (gl3/test/refcap): cvar om_fxview "x y z pitch yaw roll [noghoul]" draws every
   world frame from that eye with all areas open; a non-zero 7th value leaves out Ghoul, weapon
   and viewer entities. Only the frame record handed to the renderer changes: the effects keep
   simulating (level of detail, culling) against the real view, as in the original with the
   shim. Lets OpenMerc and the original be captured from identical eyes. */
static void
FX_ViewOverride(void)
{
	static cvar_t *view;
	static byte allareas[64]; /* as the shim: at least the 256 areas of SPEC-101 */
	float v[7] = {0, 0, 0, 0, 0, 0, 0};
	int i, k, n;

	if (!view)
	{
		view = Cvar_Get("om_fxview", "", 0);
	}

	if (!view->string[0] || (fx_fd.rdflags & SOF_RDF_NOWORLDMODEL))
	{
		return;
	}

	n = sscanf(view->string, "%f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);

	if (n < 6)
	{
		return;
	}

	for (i = 0; i < 3; i++)
	{
		fx_fd.vieworg[i] = v[i];
		fx_fd.viewangles[i] = v[3 + i];
	}

	memset(allareas, 0xff, sizeof(allareas));
	fx_fd.areabits = allareas;

	if ((n >= 7) && (v[6] != 0))
	{
		for (i = k = 0; i < fx_fd.num_entities; i++)
		{
			if (!(fx_entptrs[i]->flags & (SOF_RF_GHOUL | SOF_RF_WEAPONMODEL | SOF_RF_VIEWERMODEL)))
			{
				fx_entptrs[k++] = fx_entptrs[i];
			}
		}

		fx_fd.num_entities = k;
	}
}
