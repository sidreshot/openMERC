/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: liquid surfaces (texinfo flag 0x8) of Soldier of
 * Fortune maps. Per vertex and frame: the Quake II warp through the warp
 * sine table (halved at every renderer start), the light sampled straight
 * down as vertex colour, the waves of texinfo flag 0x2000 (height, colour
 * and alpha), and the sphere-mapped second pass of liquids whose texture has
 * an environment map (SPEC-AMEND-006 I4c.1, see GL3_Liquid_Draw).
 *
 * Implements: SPEC-410 (Liquids, turb(x)), SPEC-413 §1 step 1 (warp table
 *             halving), SPEC-AMEND-006 I4c.1
 */

#include "header/local.h"

#define LIQUID_MAX_VERTS 64

/* the warp table in world units: 8 sin(2 pi k / 256), halved at every renderer start */
static float turbtable[256];
static int turbstarts;

/* SPEC-413 §1 step 1, called by the renderer start-up */
void
GL3_Liquid_Init(void)
{
	int k;
	float amp;

	turbstarts++;
	amp = 8.0f;

	for (k = 0; k < turbstarts; k++)
	{
		amp *= 0.5f;
	}

	for (k = 0; k < 256; k++)
	{
		turbtable[k] = amp * sinf(2.0f * (float)M_PI * k / 256.0f);
	}
}

/* turb(x) of SPEC-410: k = integer part (toward zero) of x 256 / 2 pi, masked to 0..255 */
static float
Turb(float x)
{
	int k = (int)(x * (256.0f / (2.0f * (float)M_PI)));

	return turbtable[k & 255];
}

/* model space to world for inline models (yaw, -pitch, -roll, SPEC-411 section 8) */
typedef struct
{
	qboolean model;
	vec3_t origin, fwd, right, up;
} liquidxform_t;

static void
Xform(const liquidxform_t *x, const float *in, vec3_t out)
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
XformDir(const liquidxform_t *x, const float *in, vec3_t out)
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

/* OpenGL sphere-map generation for a world point with the given normal (eye-space reflection) */
static void
SphereMap(const vec3_t pos, const vec3_t normal, const vec3_t fwd, const vec3_t right, const vec3_t up, float *st)
{
	vec3_t d, u, ne, r;
	float m, kk;

	VectorSubtract(pos, r_newrefdef.vieworg, d);
	u[0] = DotProduct(d, right);
	u[1] = DotProduct(d, up);
	u[2] = -DotProduct(d, fwd);
	VectorNormalize(u);
	ne[0] = DotProduct(normal, right);
	ne[1] = DotProduct(normal, up);
	ne[2] = -DotProduct(normal, fwd);
	kk = 2 * DotProduct(ne, u);
	VectorMA(u, -kk, ne, r);
	m = 2 * sqrtf(r[0] * r[0] + r[1] * r[1] + (r[2] + 1) * (r[2] + 1));

	if (m == 0)
	{
		m = 1;
	}

	st[0] = r[0] / m + 0.5f;
	st[1] = r[1] / m + 0.5f;
}

/*
 * Draws one liquid surface. ent: the entity of an inline model (NULL or the
 * world entity for the world); translucent: drawn in the sorted pass with
 * blending (texinfo 0x800), otherwise opaque; fogregion: gl_fogmode 1 region.
 *
 * Second pass (SPEC-AMEND-006 I4c.1): when the image of the first pass has
 * an environment second-pass texture, the polygons are drawn again with the
 * base texture still bound (the environment map is never drawn), colour 1,
 * sphere-mapped coordinates of the stored plane normal, the same wave height,
 * and the blend factors (destination colour, source colour) without changing
 * the blend enable: in the opaque pass (no blending) the sphere-mapped base
 * texture replaces the first pass; in the sorted pass of 0x800 liquids the
 * result is 2 x texture x framebuffer. The texture's alpha is not used.
 */
void
GL3_Liquid_Draw(msurface_t *surf, entity_t *ent, qboolean translucent, int fogregion)
{
	const mtexinfo_t *tex = surf->texinfo;
	gl3image_t *image;
	gl3fxstate_t st;
	liquidxform_t xf;
	glpoly_t *p;
	float clock = GL3_GameTime();
	qboolean waves = (tex->flags & SOFSURF_WAVES) != 0;
	qboolean sphere;
	vec3_t normal, fwd, right, up;
	int pass;

	memset(&xf, 0, sizeof(xf));

	if (ent && ent->model && (ent->model != (struct model_s *)gl3_worldmodel))
	{
		vec3_t a = {-ent->angles[0], ent->angles[1], -ent->angles[2]};

		xf.model = true;
		VectorCopy(ent->origin, xf.origin);
		AngleVectors(a, xf.fwd, xf.right, xf.up);
	}

	image = GL3_SoF_TextureAnimation(tex, (ent && xf.model) ? ent->frame : (int)(clock * 2));
	sphere = image->detail && (image->detail->softype == SOFIMG_ENV);

	/* the stored plane normal, not flipped for back-side faces (SPEC-AMEND-006 I4c.1) */
	XformDir(&xf, surf->plane->normal, normal);

	AngleVectors(r_newrefdef.viewangles, fwd, right, up);

	GL3_FX_DefaultState(&st);
	st.texnum = image->texnum;
	st.texmode = 0;
	st.alphatest = true;
	st.culling = !translucent && (r_cull->value != 0);

	if (translucent)
	{
		st.blend = 1;
		st.depthwrite = false;
	}
	else
	{
		st.blend = 0;
		st.depthwrite = true;
	}

	GL3_Fog_SetHardwareFxState(fogregion, &st);

	/* pass 0: the liquid; pass 1 (image with an environment second-pass texture): the base texture again,
	   sphere-mapped, colour 1, the same wave height (SPEC-AMEND-006 I4c.1) */
	for (pass = 0; pass < (sphere ? 2 : 1); pass++)
	{
		if (pass == 0)
		{
			GL3_FX_SetState(&st);
		}
		else
		{
			gl3fxstate_t es = st;

			/* blend factors (destination colour, source colour), the enable inherited: none in the opaque
			   pass (replace), on in the sorted pass (2 x texture x framebuffer) */
			es.blend = translucent ? 5 : 0;
			es.alphatest = false;
			GL3_FX_SetState(&es);
		}

		for (p = surf->polys; p; p = p->next)
		{
			gl3fxvtx_t v[LIQUID_MAX_VERTS];
			int i, n = (p->numverts > LIQUID_MAX_VERTS) ? LIQUID_MAX_VERTS : p->numverts;

			for (i = 0; i < n; i++)
			{
				const float *lp = p->vertices[i].pos;
				vec3_t w, light;
				float s, t, h = 0, k = 1, alpha = 1;

				Xform(&xf, lp, w);

				if (waves)
				{
					float a = (2.3f * w[0] + w[1]) * 0.015f;
					float b = (1.7f * w[1] + w[0]) * 0.015f;

					h = 0.5625f * Turb(a + 2.0f * clock);
					k = (h + 4.5f) * 0.1f + 0.1f;
					alpha = (0.5625f * Turb(a + 2.7f * clock) * 0.125f * Turb(b + 3.37f * clock) + 4.5f) *
						0.038889f + 0.6f;
				}

				VectorCopy(w, v[i].pos);
				v[i].pos[2] += h; /* texture coordinates are computed before the shift */

				if (pass == 0)
				{
					/* texture coordinates without the offset (as in Quake II), then the warp */
					s = DotProduct(lp, tex->vecs[0]);
					t = DotProduct(lp, tex->vecs[1]);
					v[i].st[0] = (s + Turb(t * 0.125f + clock)) / 64.0f;
					v[i].st[1] = (t + Turb(s * 0.125f + clock)) / 64.0f;

					GL3_SoF_LightPoint(w, light, 255);
					v[i].rgba[0] = light[0] * k;
					v[i].rgba[1] = light[1] * k;
					v[i].rgba[2] = light[2] * k;
					v[i].rgba[3] = alpha;
				}
				else
				{
					SphereMap(v[i].pos, normal, fwd, right, up, v[i].st);
					v[i].rgba[0] = v[i].rgba[1] = v[i].rgba[2] = v[i].rgba[3] = 1;
				}
			}

			GL3_FX_Add(v, n, GL_TRIANGLE_FAN);
		}
	}

	GL3_FX_Flush();
}
