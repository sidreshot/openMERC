/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: texture animation by the .m32 rate and the
 * detail / environment-map second pass of Soldier of Fortune world surfaces.
 *
 * Package I4c: the pass for the faces of inline brush models (vertices,
 * normals and the distance test in world space through the model's
 * position and angles).
 *
 * Implements: SPEC-410 (Texture animation, Detail and environment pass,
 *             Flowing surfaces, Inline brush models), SPEC-413
 *             (gl_detailtexturing*, gl_offset*)
 */

#include "header/local.h"

#define MAX_DETAIL_SURFACES 1024

static msurface_t *detailsurfs[MAX_DETAIL_SURFACES];
static gl3image_t *detailimages[MAX_DETAIL_SURFACES];
static int numdetail;

/* I4c: model-space faces of an inline brush model (origin, angle vectors of (-pitch, yaw, -roll)) */
typedef struct
{
	qboolean model;
	vec3_t origin, fwd, right, up;
} detailxform_t;

static detailxform_t detailxforms[MAX_DETAIL_SURFACES];

float
GL3_GameTime(void)
{
	return gl3_sofframe.active ? gl3_sofframe.gametime : r_newrefdef.time;
}

/*
 * SPEC-410 Texture animation: with texinfo flag 0x1000 and a non-zero .m32
 * rate r the frame is floor(r x game clock), otherwise the entity frame
 * (world: floor(2 x game clock)); the image is the one index mod frame count
 * steps along the nexttexinfo chain.
 */
gl3image_t *
GL3_SoF_TextureAnimation(const mtexinfo_t *tex, int entframe)
{
	int c;

	if (!tex->next || (tex->numframes <= 1))
	{
		return tex->image;
	}

	if ((tex->flags & SOFSURF_ANIMRATE) && tex->image && tex->image->animrate)
	{
		c = (int)floorf(tex->image->animrate * GL3_GameTime());
	}
	else
	{
		c = entframe;
	}

	c %= tex->numframes;

	if (c < 0)
	{
		c += tex->numframes;
	}

	while (c && tex->next)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}

void
GL3_Detail_Queue(msurface_t *surf, gl3image_t *image, entity_t *ent)
{
	if (!gl_detailtexturing || !gl_detailtexturing->value || !image || !image->detail)
	{
		return;
	}

	if (surf->texinfo->flags & SOFSURF_NODETAIL)
	{
		return;
	}

	if (numdetail < MAX_DETAIL_SURFACES)
	{
		detailxform_t *x = &detailxforms[numdetail];

		detailsurfs[numdetail] = surf;
		detailimages[numdetail] = image;
		x->model = (ent != NULL);

		if (ent)
		{
			/* inline models: model space to world by yaw, -pitch, -roll (SPEC-411 section 8) */
			vec3_t a = {-ent->angles[0], ent->angles[1], -ent->angles[2]};

			VectorCopy(ent->origin, x->origin);
			AngleVectors(a, x->fwd, x->right, x->up);
		}

		numdetail++;
	}
}

/* flowing offset of SPEC-410 (texture repeats) */
static void
FlowOffset(const mtexinfo_t *tex, float out[2])
{
	out[0] = out[1] = 0;

	if (tex->flags & SOFSURF_FLOWING)
	{
		short vs = (short)((tex->value >> 16) & 0xFFFF);
		short vt = (short)(tex->value & 0xFFFF);

		out[0] = vs * GL3_GameTime() * 0.01f;
		out[1] = vt * GL3_GameTime() * 0.01f;
	}
}

/* I4c: a model-space point or direction in world space */
static void
XformPoint(const detailxform_t *x, const float *in, vec3_t out)
{
	int k;

	if (!x->model)
	{
		VectorCopy(in, out);
		return;
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = x->origin[k] + in[0] * x->fwd[k] - in[1] * x->right[k] + in[2] * x->up[k];
	}
}

static void
XformDir(const detailxform_t *x, const float *in, vec3_t out)
{
	int k;

	if (!x->model)
	{
		VectorCopy(in, out);
		return;
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = in[0] * x->fwd[k] - in[1] * x->right[k] + in[2] * x->up[k];
	}
}

/* the second pass after all opaque world surfaces, and after the faces of each inline model */
void
GL3_Detail_DrawPass(void)
{
	gl3fxstate_t st;
	vec3_t forward, right, up;
	float maxdist = gl_detailtexturingdistance ? gl_detailtexturingdistance->value : 250;
	int mode = gl_detailtexturing ? (int)gl_detailtexturing->value : 0;
	int i, j;

	if (numdetail == 0)
	{
		return;
	}

	if (mode == 0)
	{
		numdetail = 0;
		return;
	}

	GL3_Draw3DBatchesNow();
	AngleVectors(r_newrefdef.viewangles, forward, right, up);

	GL3_FX_DefaultState(&st);
	st.blend = (mode >= 3) ? 0 : 5; /* (destination colour, source colour), 128 grey neutral */
	st.alphatest = false;
	st.depthwrite = false;
	st.polyoffset = (gl_offsetfactor->value != 0) || (gl_offsetunits->value != 0);

	for (i = 0; i < numdetail; i++)
	{
		msurface_t *surf = detailsurfs[i];
		gl3image_t *base = detailimages[i];
		const detailxform_t *xf = &detailxforms[i];
		gl3image_t *detail = base->detail;
		glpoly_t *p = surf->polys;
		float flow[2];
		vec3_t n;
		qboolean sphere = false, env = (detail->softype == SOFIMG_ENV);

		if (!p)
		{
			continue;
		}

		if (mode == 2)
		{
			qboolean near = false;

			for (j = 0; j < p->numverts; j++)
			{
				vec3_t d, w;

				XformPoint(xf, p->vertices[j].pos, w);
				VectorSubtract(w, r_newrefdef.vieworg, d);

				if (DotProduct(d, forward) < maxdist)
				{
					near = true;
					break;
				}
			}

			if (!near)
			{
				continue;
			}
		}

		FlowOffset(surf->texinfo, flow);
		XformDir(xf, surf->plane->normal, n);

		if (surf->flags & SURF_PLANEBACK)
		{
			VectorNegate(n, n);
		}

		if (env)
		{
			float cx = n[1], cy = -n[0]; /* n x (0, 0, 1) */

			sphere = (surf->texinfo->flags & SOFSURF_SPHEREMAP) || (cx * cx + cy * cy < 0.1f);
		}

		st.texnum = detail->texnum;
		GL3_FX_SetState(&st);

		{
			gl3fxvtx_t v[64];
			int count = (p->numverts > 64) ? 64 : p->numverts;
			vec3_t U, V;

			if (env && !sphere)
			{
				vec3_t z = {0, 0, 1};

				CrossProduct(n, z, U);
				VectorNormalize(U);
				CrossProduct(U, n, V);
				VectorNormalize(V);
			}

			for (j = 0; j < count; j++)
			{
				const gl3_3D_vtx_t *pv = &p->vertices[j];

				XformPoint(xf, pv->pos, v[j].pos);
				v[j].rgba[0] = v[j].rgba[1] = v[j].rgba[2] = v[j].rgba[3] = 1;

				if (!env)
				{
					v[j].st[0] = (pv->texCoord[0] - flow[0]) * base->detail_scale[0] + base->detail_scroll[0];
					v[j].st[1] = (pv->texCoord[1] - flow[1]) * base->detail_scale[1] + base->detail_scroll[1];
				}
				else if (sphere)
				{
					/* OpenGL sphere-map generation with the face normal */
					vec3_t d, u, ne, r;
					float m, k;

					VectorSubtract(v[j].pos, r_newrefdef.vieworg, d);
					u[0] = DotProduct(d, right);
					u[1] = DotProduct(d, up);
					u[2] = -DotProduct(d, forward);
					VectorNormalize(u);
					ne[0] = DotProduct(n, right);
					ne[1] = DotProduct(n, up);
					ne[2] = -DotProduct(n, forward);
					k = 2 * DotProduct(ne, u);
					VectorMA(u, -k, ne, r);
					m = 2 * sqrtf(r[0] * r[0] + r[1] * r[1] + (r[2] + 1) * (r[2] + 1));

					if (m == 0)
					{
						m = 1;
					}

					v[j].st[0] = r[0] / m + 0.5f;
					v[j].st[1] = r[1] / m + 0.5f;
				}
				else
				{
					/* planar reflection */
					vec3_t d, r;
					float k;

					VectorSubtract(r_newrefdef.vieworg, v[j].pos, d);
					VectorNormalize(d);
					k = 2 * DotProduct(n, d);
					VectorScale(n, k, r);
					VectorSubtract(r, d, r);
					v[j].st[0] = (DotProduct(r, U) + 1) * 0.5f;
					v[j].st[1] = (DotProduct(r, V) + 1) * 0.5f;
				}
			}

			GL3_FX_Add(v, count, GL_TRIANGLE_FAN);
		}
	}

	GL3_FX_Flush();
	numdetail = 0;
}
