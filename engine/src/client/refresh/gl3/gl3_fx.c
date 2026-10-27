/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: effect primitives and the depth-sorted translucent
 * pass of Soldier of Fortune. Alpha quads (billboards, oriented quads, beams),
 * quad chains with their level-of-detail reduction, translucent world
 * surfaces, entities, the full-screen overlays of the 3D view (night vision,
 * screen tint, warning tint) and the light sampling used by them and by
 * export slot 0xa4.
 *
 * Effects are drawn through one batch of textured, coloured vertices; the
 * batch state (blending, alpha test, depth test and writes, polygon offset,
 * texture mode, linear fog) mirrors the fixed-function state of SPEC-412.
 *
 * Implements: SPEC-412 §8-§11, SPEC-410 (light sampling, point fog),
 *             SPEC-411 §4, §8, §9 (entity order, brush entities, placeholder),
 *             SPEC-002 (alpha-quad and quad-chain records), SPEC-AMEND-002
 */

#include "header/local.h"

/* ------------------------------------------------------------------
 * effect batch
 * ------------------------------------------------------------------ */

#define MAX_FX_VERTS 16384

static GLuint vaofx, vbofx;
static gl3fxvtx_t fxverts[MAX_FX_VERTS];
static int numfxverts;
static gl3fxstate_t fxstate;      /* state of the pending vertices */
static qboolean fxstate_valid;

static GLint u_texmode, u_alphatest, u_fogmode, u_fogstart, u_fogend, u_fogcolor, u_fogplane;
static GLint u_emis, u_softp, u_softv, u_depth; /* GFX-modern (gl3_post.c) */
static GLint u_pf0, u_pf1, u_pf2, u_pfpl; /* GFX-modern r_om_ppfog (gl3_fog.c) */

static GLuint u_program; /* program the uniform locations belong to */

static void
QueryUniforms(void)
{
	GLuint prog = gl3state.siSoFfx.shaderProgram;

	u_program = prog;
	u_texmode = glGetUniformLocation(prog, "texmode");
	u_alphatest = glGetUniformLocation(prog, "alphatest");
	u_fogmode = glGetUniformLocation(prog, "fogmode");
	u_fogstart = glGetUniformLocation(prog, "fogstart");
	u_fogend = glGetUniformLocation(prog, "fogend");
	u_fogcolor = glGetUniformLocation(prog, "fogcolor");
	u_fogplane = glGetUniformLocation(prog, "fogplane");
	u_emis = glGetUniformLocation(prog, "omEmis");
	u_softp = glGetUniformLocation(prog, "omSoftP");
	u_softv = glGetUniformLocation(prog, "omSoftV");
	u_depth = glGetUniformLocation(prog, "omDepth");
	u_pf0 = glGetUniformLocation(prog, "omPF0");
	u_pf1 = glGetUniformLocation(prog, "omPF1");
	u_pf2 = glGetUniformLocation(prog, "omPF2");
	u_pfpl = glGetUniformLocation(prog, "omPFPl");
	glUniform1i(u_depth, 7); /* depth copy of the post path on GL_TEXTURE7 */
}

void
GL3_FX_Init(void)
{
	glGenVertexArrays(1, &vaofx);
	GL3_BindVAO(vaofx);
	glGenBuffers(1, &vbofx);
	GL3_BindVBO(vbofx);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(gl3fxvtx_t), 0);
	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3fxvtx_t), offsetof(gl3fxvtx_t, st));
	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(gl3fxvtx_t), offsetof(gl3fxvtx_t, rgba));
	GL3_BindVAO(0);

	u_program = 0;
	numfxverts = 0;
	fxstate_valid = false;
}

void
GL3_FX_Shutdown(void)
{
	if (vbofx)
	{
		glDeleteBuffers(1, &vbofx);
	}

	if (vaofx)
	{
		glDeleteVertexArrays(1, &vaofx);
	}

	vbofx = vaofx = 0;
	numfxverts = 0;
}

void
GL3_FX_DefaultState(gl3fxstate_t *st)
{
	memset(st, 0, sizeof(*st));
	st->blend = 1;
	st->alphatest = true;
	st->depthtest = true;
	st->depthwrite = false;
	st->texmode = 0;
}

static void
ApplyBlend(int blend)
{
	switch (blend)
	{
		case 0:
			glDisable(GL_BLEND);
			return;
		case 2:
			glBlendFunc(GL_ONE, GL_ONE);
			break;
		case 3:
			glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
			break;
		case 4:
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
			break;
		case 5:
			glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
			break;
		default:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
	}

	glEnable(GL_BLEND);
}

void
GL3_FX_Flush(void)
{
	const gl3fxstate_t *st = &fxstate;
	vec3_t forward, right, up;

	if (numfxverts == 0)
	{
		return;
	}

	GL3_UseProgram(gl3state.siSoFfx.shaderProgram);

	if (u_program != gl3state.siSoFfx.shaderProgram)
	{
		QueryUniforms();
	}

	GL3_BindVAO(vaofx);
	GL3_BindVBO(vbofx);
	glBufferData(GL_ARRAY_BUFFER, numfxverts * sizeof(gl3fxvtx_t), fxverts, GL_STREAM_DRAW);

	if (st->texnum)
	{
		GL3_Bind(st->texnum);
	}

	glUniform1i(u_texmode, st->texnum ? st->texmode : 2);
	glUniform1i(u_alphatest, st->alphatest ? 1 : 0);
	glUniform1i(u_fogmode, st->fogmode);

	if (st->fogmode)
	{
		AngleVectors(r_newrefdef.viewangles, forward, right, up);
		glUniform1f(u_fogstart, st->fogstart);
		glUniform1f(u_fogend, st->fogend);
		glUniform4fv(u_fogcolor, 1, st->fogcolor);
		glUniform4f(u_fogplane, forward[0], forward[1], forward[2], -DotProduct(forward, r_newrefdef.vieworg));
	}

	/* GFX-modern: what this batch writes to the emissive target, and the soft particle fade */
	{
		float emis[4];
		int soft = (st->soft && GL3_Post_DepthReady()) ? st->soft : 0;

		GL3_Post_FxEmis(st->blend, emis);
		glUniform4fv(u_emis, 1, emis);

		if (soft)
		{
			GL3_Post_SoftUniforms(u_softp, u_softv, soft);
		}
		else
		{
			glUniform4f(u_softv, 1, 1, 1, 0);
		}
	}

	if (st->ppfog)
	{
		glUniform4fv(u_pf0, 1, st->pf0);
		glUniform4fv(u_pf1, 1, st->pf1);
		glUniform4fv(u_pf2, 1, st->pf2);
		glUniform4fv(u_pfpl, 5, &st->pfpl[0][0]);
	}
	else
	{
		glUniform4f(u_pf0, 0, 0, 0, 0);
	}

	ApplyBlend(st->blend);

	if (st->depthtest)
	{
		glEnable(GL_DEPTH_TEST);
	}
	else
	{
		glDisable(GL_DEPTH_TEST);
	}

	glDepthMask(st->depthwrite ? GL_TRUE : GL_FALSE);

	if (st->polyoffset)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(gl_offsetfactor->value, gl_offsetunits->value);
	}

	if (st->culling)
	{
		glEnable(GL_CULL_FACE);
	}
	else
	{
		glDisable(GL_CULL_FACE);
	}

	glDrawArrays(GL_TRIANGLES, 0, numfxverts);
	++gl3_num3Ddraws;
	++gl3_numBufferVtxData;
	numfxverts = 0;

	if (st->polyoffset)
	{
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	/* back to the state of the translucent pass */
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

void
GL3_FX_SetState(const gl3fxstate_t *st)
{
	if (fxstate_valid && !memcmp(st, &fxstate, sizeof(*st)))
	{
		return;
	}

	GL3_FX_Flush();
	fxstate = *st;
	fxstate_valid = true;
}

/* adds a primitive (GL_TRIANGLES, GL_TRIANGLE_FAN or quads as fans) with the current state */
void
GL3_FX_Add(const gl3fxvtx_t *v, int n, GLenum mode)
{
	int i, count;

	if (n < 3)
	{
		return;
	}

	GL3_Draw3DBatchesNow();
	count = (mode == GL_TRIANGLES) ? n : 3 * (n - 2);

	if (numfxverts + count > MAX_FX_VERTS)
	{
		GL3_FX_Flush();
	}

	if (mode == GL_TRIANGLES)
	{
		memcpy(&fxverts[numfxverts], v, n * sizeof(*v));
		numfxverts += n;
		return;
	}

	for (i = 1; i < n - 1; i++)
	{
		fxverts[numfxverts++] = v[0];
		fxverts[numfxverts++] = v[i];
		fxverts[numfxverts++] = v[i + 1];
	}
}

/* ------------------------------------------------------------------
 * light sampling (SPEC-410 Light sampling, export slot 0xa4)
 * ------------------------------------------------------------------ */

static const float (*dirtable)[3];
static int dirtablecount;

void
GL3_SoF_SetDirTable(const float (*table)[3], int count)
{
	dirtable = table;
	dirtablecount = count;
}

#define LIGHTCACHE_MAX 1000

typedef struct
{
	int x, y, z, dir;
	vec3_t rgb;
	int lastuse;
} lightcache_t;

static lightcache_t lightcache[LIGHTCACHE_MAX];
static int numlightcache;
static int lightqueries;
static gl3model_t *lightcacheworld;

void
GL3_SoF_ClearLightCache(void)
{
	numlightcache = 0;
	lightqueries = 0;
}

/* first surface with a lightmap hit by the ray; static light into color */
static int
LightTrace(mnode_t *node, const vec3_t start, const vec3_t end, vec3_t color)
{
	float front, back, frac;
	int side, i, r;
	cplane_t *plane;
	vec3_t mid;
	msurface_t *surf;

	if (node->contents != CONTENTS_NODE)
	{
		return -1;
	}

	plane = node->plane;
	front = DotProduct(start, plane->normal) - plane->dist;
	back = DotProduct(end, plane->normal) - plane->dist;
	side = front < 0;

	if ((back < 0) == side)
	{
		return LightTrace(node->children[side], start, end, color);
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	r = LightTrace(node->children[side], start, mid, color);

	if (r >= 0)
	{
		return r;
	}

	surf = gl3_worldmodel->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		mtexinfo_t *tex = surf->texinfo;
		int s, t, ds, dt, maps;
		const byte *lightmap;

		if (tex->flags & (SOFSURF_SKY | SOFSURF_WARP | SOFSURF_ALPHA))
		{
			continue;
		}

		s = (int)(DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3]);
		t = (int)(DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3]);

		if ((s < surf->texturemins[0]) || (t < surf->texturemins[1]))
		{
			continue;
		}

		ds = s - surf->texturemins[0];
		dt = t - surf->texturemins[1];

		if ((ds > surf->extents[0]) || (dt > surf->extents[1]))
		{
			continue;
		}

		if (!surf->samples)
		{
			color[0] = color[1] = color[2] = 0;
			return 1;
		}

		ds = (ds / 16) / (surf->lmstep[0] / 16);
		dt = (dt / 16) / (surf->lmstep[1] / 16);

		if ((ds >= surf->lmsize[0]) || (dt >= surf->lmsize[1]))
		{
			continue;
		}

		VectorClear(color);
		lightmap = surf->samples + 3 * (dt * surf->lmsize[0] + ds);

		for (maps = 0; maps < MAX_LIGHTMAPS_PER_SURFACE && surf->styles[maps] != 255; maps++)
		{
			const float *rgb = r_newrefdef.lightstyles ?
				r_newrefdef.lightstyles[surf->styles[maps]].rgb : vec3_origin;
			int j;

			for (j = 0; j < 3; j++)
			{
				color[j] += lightmap[j] / 255.0f * rgb[j] * gl3_modulate_latched;
			}

			lightmap += 3 * surf->lmsize[0] * surf->lmsize[1];
		}

		for (i = 0; i < 3; i++)
		{
			if (color[i] > 1)
			{
				color[i] = 1;
			}
		}

		return 1;
	}

	return LightTrace(node->children[!side], mid, end, color);
}

static void
StaticLight(const vec3_t p, int dir, vec3_t color)
{
	vec3_t end;

	if ((dir == 255) || !dirtable || (dir >= dirtablecount))
	{
		VectorSet(end, p[0], p[1], p[2] - 2048);
	}
	else
	{
		VectorMA(p, 256, dirtable[dir], end);
	}

	if (LightTrace(gl3_worldmodel->nodes, p, end, color) < 0)
	{
		VectorSet(color, 0.1f, 0.1f, 0.1f);
	}
}

void
GL3_SoF_LightPoint(const vec3_t p, vec3_t color, int dir)
{
	int i, cx, cy, cz, free = -1;
	dlight_t *dl;

	if (!gl3_worldmodel || !gl3_worldmodel->lightdata)
	{
		VectorSet(color, 1, 1, 1);
		return;
	}

	if (lightcacheworld != gl3_worldmodel)
	{
		lightcacheworld = gl3_worldmodel;
		GL3_SoF_ClearLightCache();
	}

	/* static light, cached per 32-unit cell and direction */
	cx = (int)floor((p[0] + 4096) / 32);
	cy = (int)floor((p[1] + 4096) / 32);
	cz = (int)floor((p[2] + 4096) / 32);
	lightqueries++;

	for (i = 0; i < numlightcache; i++)
	{
		lightcache_t *c = &lightcache[i];

		if ((c->x == cx) && (c->y == cy) && (c->z == cz) && (c->dir == dir))
		{
			c->lastuse = lightqueries;
			VectorCopy(c->rgb, color);
			break;
		}
	}

	if (i == numlightcache)
	{
		StaticLight(p, dir, color);

		if (numlightcache > 200)
		{
			int j = 0;

			for (i = 0; i < numlightcache; i++)
			{
				if (lightqueries - lightcache[i].lastuse <= 100)
				{
					lightcache[j++] = lightcache[i];
				}
			}

			numlightcache = j;
		}

		if (numlightcache < LIGHTCACHE_MAX)
		{
			free = numlightcache++;
			lightcache[free].x = cx;
			lightcache[free].y = cy;
			lightcache[free].z = cz;
			lightcache[free].dir = dir;
			lightcache[free].lastuse = lightqueries;
			VectorCopy(color, lightcache[free].rgb);
		}
	}

	/* dynamic lights, then the modulate factor again, clamped */
	for (i = 0, dl = r_newrefdef.dlights; i < r_newrefdef.num_dlights; i++, dl++)
	{
		vec3_t d;
		float add;

		VectorSubtract(p, dl->origin, d);
		add = (dl->intensity - VectorLength(d)) / 256.0f;

		if (add > 0)
		{
			VectorMA(color, add, dl->color, color);
		}
	}

	for (i = 0; i < 3; i++)
	{
		color[i] *= gl3_modulate_latched;

		if (color[i] > 1)
		{
			color[i] = 1;
		}
	}
}

/* ------------------------------------------------------------------
 * alpha quads (SPEC-412 §8) and quad chains (§9)
 * ------------------------------------------------------------------ */

static vec3_t vforward, vright, vup;
static float effecttotal; /* T: sum of the chain weights of this frame */

/* blend mode of a quad: 1 normal, 2 additive, 3 darkening */
static int
QuadBlend(int flags)
{
	if (flags & SOF_AQ_DARKEN)
	{
		return 3;
	}

	if (flags & SOF_AQ_ADDITIVE)
	{
		return 2;
	}

	return 1;
}

static gl3image_t *
QuadImage(sofalphaquad_t *q)
{
	if (!q->image)
	{
		gl3image_t *image = NULL;

		if (q->texname)
		{
			image = GL3_SoF_FindImage(q->texname, SOFIMG_SPRITE, true);
		}

		q->image = image ? image : GL3_SoF_NoTexture();
	}

	return (gl3image_t *)q->image;
}

/* fog region of a quad (+0x3c: < 0 not computed, 0 none, else region + 1) */
static int
QuadFog(sofalphaquad_t *q)
{
	if (q->fogcache < 0)
	{
		int r = GL3_Fog_RegionOfPoint(q->origin);

		q->fogcache = (r >= 0) ? r + 1 : 0;
	}

	return q->fogcache - 1;
}

/* the colour of a quad as floats (SPEC-412 §8 blend table and lit rule) */
static void
QuadColor(const sofalphaquad_t *q, int blend, const vec3_t light, qboolean lit, float out[4])
{
	float r = q->color[0], g = q->color[1], b = q->color[2], a = q->color[3];

	if (blend != 1)
	{
		float k = a / (255.0f * 255.0f);

		if (lit)
		{
			out[0] = r * light[0] * a / (255.0f * 255.0f);
			out[1] = g * light[1] * a / (255.0f * 255.0f);
			out[2] = b * light[2] * a / (255.0f * 255.0f);
		}
		else
		{
			out[0] = r * k;
			out[1] = g * k;
			out[2] = b * k;
		}

		out[3] = 1;
	}
	else
	{
		if (lit)
		{
			/* bytes trunc(r L) without clamping: values above 255 wrap */
			out[0] = (float)(((int)(r * light[0])) & 255) / 255.0f;
			out[1] = (float)(((int)(g * light[1])) & 255) / 255.0f;
			out[2] = (float)(((int)(b * light[2])) & 255) / 255.0f;
		}
		else
		{
			out[0] = r / 255.0f;
			out[1] = g / 255.0f;
			out[2] = b / 255.0f;
		}

		out[3] = a / 255.0f;
	}
}

/* fog settings for drawing a point p of region r with a given blend mode */
static void
SetPointFog(gl3fxstate_t *st, int region, const vec3_t p, int blend)
{
	float color[4];
	float f;

	st->fogmode = 0;

	if (region < 0)
	{
		return;
	}

	f = GL3_Fog_PointFactor(region, p, color);

	if (f <= 0.01f)
	{
		return;
	}

	{
		vec3_t d;

		VectorSubtract(p, r_newrefdef.vieworg, d);
		st->fogmode = 1;
		st->fogstart = 0;
		st->fogend = fabsf(DotProduct(vforward, d)) / f;
	}

	if ((blend == 2) || (blend == 3))
	{
		st->fogcolor[0] = st->fogcolor[1] = st->fogcolor[2] = 0;
		st->fogcolor[3] = 1;
	}
	else
	{
		st->fogcolor[0] = color[0];
		st->fogcolor[1] = color[1];
		st->fogcolor[2] = color[2];
		st->fogcolor[3] = 1;
	}
}

/* billboard corners (SPEC-412 §8): image top row at the -A edge */
static void
BillboardVerts(const sofalphaquad_t *q, const float rgba[4], gl3fxvtx_t v[4])
{
	static const float uv[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	static const float st[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
	vec3_t A, B;
	float a = q->v0c[0], b = q->v0c[1], c = q->f18, s = q->f1c;
	int i;

	if (q->flags & SOF_AQ_ORIENTED)
	{
		/* B: the coordinate axis along which |N| is smallest, made perpendicular to N */
		vec3_t n, axis = {0, 0, 0};
		int k = 0;
		float d;

		VectorCopy(q->normal, n);

		if (fabsf(n[1]) < fabsf(n[k]))
		{
			k = 1;
		}

		if (fabsf(n[2]) < fabsf(n[k]))
		{
			k = 2;
		}

		axis[k] = 1;
		d = DotProduct(axis, n);
		VectorMA(axis, -d, n, B);
		VectorNormalize(B);
		CrossProduct(n, B, A);
	}
	else
	{
		VectorCopy(vup, A);
		VectorCopy(vright, B);
	}

	for (i = 0; i < 4; i++)
	{
		float u = uv[i][0] * a, w = uv[i][1] * b;
		float ka = u * c + w * s, kb = w * c - u * s;
		int j;

		for (j = 0; j < 3; j++)
		{
			v[i].pos[j] = q->origin[j] + ka * A[j] + kb * B[j];
		}

		v[i].st[0] = st[i][0];
		v[i].st[1] = st[i][1];
		memcpy(v[i].rgba, rgba, sizeof(v[i].rgba));
	}
}

/* beam corners (SPEC-412 §8) */
static qboolean
BeamVerts(const sofalphaquad_t *q, const float rgba[4], gl3fxvtx_t v[4])
{
	vec3_t e, d, side;
	float w = q->f18;
	int i;

	VectorSubtract(q->origin, r_newrefdef.vieworg, e);
	VectorSubtract(q->v0c, q->origin, d);
	CrossProduct(e, d, side);

	if (VectorNormalize(side) == 0)
	{
		return false;
	}

	for (i = 0; i < 3; i++)
	{
		v[0].pos[i] = q->origin[i] + w * side[i];
		v[1].pos[i] = q->origin[i] - w * side[i];
		v[2].pos[i] = q->v0c[i] - w * side[i];
		v[3].pos[i] = q->v0c[i] + w * side[i];
	}

	v[0].st[0] = 0; v[0].st[1] = 0;
	v[1].st[0] = 0; v[1].st[1] = 1;
	v[2].st[0] = 1; v[2].st[1] = 1;
	v[3].st[0] = 1; v[3].st[1] = 0;

	for (i = 0; i < 4; i++)
	{
		memcpy(v[i].rgba, rgba, sizeof(v[i].rgba));
	}

	return true;
}

/* one alpha quad drawn alone (SPEC-412 §8) */
static void
DrawAlphaQuad(sofalphaquad_t *q)
{
	gl3fxstate_t st;
	gl3fxvtx_t v[4];
	vec3_t light = {1, 1, 1};
	float rgba[4];
	int blend = QuadBlend(q->flags);
	gl3image_t *image = QuadImage(q);
	qboolean lit = (q->flags & SOF_AQ_LIT) != 0;

	if (lit)
	{
		GL3_SoF_LightPoint(q->origin, light, q->lightdir);
	}

	QuadColor(q, blend, light, lit, rgba);

	GL3_FX_DefaultState(&st);
	st.texnum = image ? image->texnum : 0;
	st.blend = blend;
	st.alphatest = (blend == 1);
	st.polyoffset = (q->flags & SOF_AQ_DECAL) &&
		((gl_offsetfactor->value != 0) || (gl_offsetunits->value != 0));
	SetPointFog(&st, QuadFog(q), q->origin, blend);
	/* GFX-modern: camera-facing smoke, dust and blood fade where they cut into the world */
	st.soft = GL3_Post_SoftMode(blend, (q->flags & SOF_AQ_BILLBOARD) &&
		!(q->flags & (SOF_AQ_ORIENTED | SOF_AQ_DECAL)), st.depthtest);

	if (q->flags & SOF_AQ_BILLBOARD)
	{
		BillboardVerts(q, rgba, v);
	}
	else if (q->flags & SOF_AQ_BEAM)
	{
		if (!BeamVerts(q, rgba, v))
		{
			return;
		}
	}
	else
	{
		return;
	}

	GL3_FX_SetState(&st);
	GL3_FX_Add(v, 4, GL_TRIANGLE_FAN);
	/* no drawn mark: SPEC-412 §9 rule 7 marks chain members only, and the
	   original leaves quads drawn alone unmarked (seen in the call traces) */
}

static void
DropQuad(sofalphaquad_t *q)
{
	q->flags |= SOF_AQ_HIDDEN;
	q->v0c[0] = q->v0c[1] = 0;
	q->f18 = 0;
}

/* one quad chain (SPEC-412 §9) */
static void
DrawQuadChain(sofquadchain_t *chain)
{
	sofalphaquad_t *first = chain->first, *q;
	gl3fxstate_t st;
	vec3_t light = {1, 1, 1};
	int blend, drop = 0, i, count;
	float F = gl_effectfill->value;
	gl3image_t *image;
	qboolean lit, batched, billboard, prevdrawn;

	if (!first)
	{
		return;
	}

	if (!r_nocull->value && R_CullBox(chain->mins, chain->maxs, frustum))
	{
		return;
	}

	/* level of detail */
	count = chain->count;

	if ((effecttotal > F) && (effecttotal > 0))
	{
		drop = (int)((1.0f - F / effecttotal) * count);

		if (drop < 0)
		{
			drop = 0;
		}

		if (drop > count)
		{
			drop = count;
		}

		if (count == 1)
		{
			if (((float)rand() / ((float)RAND_MAX + 1.0f)) < 1.0f - F / effecttotal)
			{
				first->flags |= SOF_AQ_HIDDEN;
				return;
			}

			drop = 0;
		}
		else
		{
			int pass;

			/* 1st, 3rd, 5th ... then 2nd, 4th ... quad; quads already drawn are kept */
			for (pass = 0; (pass < 2) && (drop > 0); pass++)
			{
				for (q = first, i = 0; q && (drop > 0); q = q->next, i++)
				{
					if (((i & 1) == pass) && !(q->flags & SOF_AQ_DRAWN) && !(q->flags & SOF_AQ_HIDDEN))
					{
						DropQuad(q);
						drop--;
					}
				}
			}
		}
	}

	/* shared settings from the first quad */
	blend = QuadBlend(first->flags);
	image = QuadImage(first);
	lit = (first->flags & SOF_AQ_LIT) != 0;
	batched = (first->flags & SOF_AQ_BATCHED) != 0;
	billboard = (first->flags & SOF_AQ_BILLBOARD) != 0;

	if (lit)
	{
		vec3_t p;

		p[0] = (chain->mins[0] + chain->maxs[0]) * 0.5f;
		p[1] = (chain->mins[1] + chain->maxs[1]) * 0.5f;
		p[2] = chain->maxs[2];
		GL3_SoF_LightPoint(p, light, first->lightdir);
	}

	GL3_FX_DefaultState(&st);
	st.texnum = image ? image->texnum : 0;
	st.blend = blend;
	st.alphatest = (blend == 1);
	st.depthtest = !(first->flags & SOF_AQ_NODEPTH);
	SetPointFog(&st, QuadFog(first), first->origin, blend);
	st.soft = GL3_Post_SoftMode(blend, billboard && !(first->flags & (SOF_AQ_ORIENTED | SOF_AQ_DECAL)),
		st.depthtest);
	GL3_FX_SetState(&st);

	prevdrawn = false;

	for (q = first; q; q = q->next)
	{
		gl3fxvtx_t v[4];
		float rgba[4];

		if ((drop > 0) && prevdrawn && !(q->flags & SOF_AQ_DRAWN))
		{
			DropQuad(q);
			drop--;
			prevdrawn = false;
			continue;
		}

		if (batched)
		{
			QuadColor(first, blend, light, lit, rgba);
		}
		else
		{
			QuadColor(q, QuadBlend(q->flags) == blend ? blend : QuadBlend(q->flags), light,
				(q->flags & SOF_AQ_LIT) != 0, rgba);
		}

		if (billboard)
		{
			if (batched)
			{
				/* one triangle per quad: origin, origin + a U, origin + b R */
				for (i = 0; i < 3; i++)
				{
					v[0].pos[i] = q->origin[i];
					v[1].pos[i] = q->origin[i] + q->v0c[0] * vup[i];
					v[2].pos[i] = q->origin[i] + q->v0c[1] * vright[i];
				}

				v[0].st[0] = 0; v[0].st[1] = 0;
				v[1].st[0] = 0; v[1].st[1] = 1.25f;
				v[2].st[0] = 1.25f; v[2].st[1] = 0;

				for (i = 0; i < 3; i++)
				{
					memcpy(v[i].rgba, rgba, sizeof(rgba));
				}

				GL3_FX_Add(v, 3, GL_TRIANGLES);
			}
			else
			{
				BillboardVerts(q, rgba, v);
				GL3_FX_Add(v, 4, GL_TRIANGLE_FAN);
			}
		}
		else if (BeamVerts(q, rgba, v))
		{
			GL3_FX_Add(v, 4, GL_TRIANGLE_FAN);
		}

		q->flags |= SOF_AQ_DRAWN;
		prevdrawn = true;
	}
}

/* ------------------------------------------------------------------
 * the sorted translucent pass (SPEC-412 §10)
 * ------------------------------------------------------------------ */

enum
{
	ITEM_ENTITY,
	ITEM_QUAD,
	ITEM_CHAIN,
	ITEM_SURFACE
};

typedef struct
{
	float key;
	int kind;
	void *ptr;
	entity_t *ent;
} sortitem_t;

#define MAX_SORT_ITEMS (SOF_MAX_ENTITIES + SOF_MAX_ALPHAQUADS + SOF_MAX_QUADCHAINS + 4096)

static sortitem_t sortitems[MAX_SORT_ITEMS];
static int numsortitems;

/* translucent world surfaces collected while drawing the world */
#define MAX_ALPHA_SURFACES 4096
static msurface_t *alphasurfs[MAX_ALPHA_SURFACES];
static entity_t *alphasurfents[MAX_ALPHA_SURFACES];
static int numalphasurfs;

void
GL3_FX_AddAlphaSurface(msurface_t *surf, entity_t *ent)
{
	if (numalphasurfs < MAX_ALPHA_SURFACES)
	{
		alphasurfs[numalphasurfs] = surf;
		alphasurfents[numalphasurfs] = ent;
		numalphasurfs++;
	}
}

void
GL3_FX_BeginFrame3D(void)
{
	numalphasurfs = 0;
	effecttotal = 0;
	AngleVectors(r_newrefdef.viewangles, vforward, vright, vup);
}

static void
AddItem(float key, int kind, void *ptr, entity_t *ent)
{
	if (numsortitems < MAX_SORT_ITEMS)
	{
		sortitems[numsortitems].key = key;
		sortitems[numsortitems].kind = kind;
		sortitems[numsortitems].ptr = ptr;
		sortitems[numsortitems].ent = ent;
		numsortitems++;
	}
}

static int
CompareItems(const void *a, const void *b)
{
	float ka = ((const sortitem_t *)a)->key, kb = ((const sortitem_t *)b)->key;

	return (ka < kb) ? 1 : (ka > kb) ? -1 : 0; /* largest key first */
}

static float
DistSq(const vec3_t a, const vec3_t b)
{
	vec3_t d;

	VectorSubtract(a, b, d);
	return DotProduct(d, d);
}

/* alpha quads that passed the collection test this frame (r_speeds 1 "qp", SPEC-AMEND-006 I4c.4) */
int gl3_c_alphaquads;

/*
 * A quad whose leaf is set is drawn only if that leaf's first listed surface was
 * marked by this frame's world walk (PVS, frustum, areas); for a leaf without
 * surfaces the first surface listed after it in the shared list decides
 * (SPEC-412 §10 Collection item 1, SPEC-AMEND-006 I4c.4).
 */
static qboolean
QuadLeafVisible(const sofalphaquad_t *q)
{
	mleaf_t *leaf;
	msurface_t **mark;

	if ((q->leaf < 0) || !gl3_worldmodel || (q->leaf >= gl3_worldmodel->numleafs))
	{
		return true;
	}

	leaf = &gl3_worldmodel->leafs[q->leaf];
	mark = leaf->firstmarksurface;

	if (!mark || (mark < gl3_worldmodel->marksurfaces) ||
		(mark >= gl3_worldmodel->marksurfaces + gl3_worldmodel->nummarksurfaces) || !*mark)
	{
		return false;
	}

	return (*mark)->visframe == gl3_framecount;
}

/* vertex-lit translucent world surface (SPEC-410 Alpha surfaces) */
static void
DrawAlphaSurface(msurface_t *surf, entity_t *ent)
{
	extern int c_brush_polys;
	gl3image_t *image;
	gl3fxstate_t st;
	glpoly_t *p;
	float scroll[2] = {0, 0};
	int region;

	c_brush_polys++; /* I4c: r_speeds 1 "wp" counts world alpha surfaces (SPEC-AMEND-006 I4c.4) */

	if ((surf->flags & SURF_DRAWTURB) && gl3_worldmodel && (gl3_worldmodel->bspversion == BSPVERSION_SOF))
	{
		/* I4c: SoF liquids with blending (SPEC-410 Liquids, Alpha surfaces) */
		GL3_FX_Flush();
		GL3_Liquid_Draw(surf, ent, true, GL3_Fog_HardwareRegion(surf));
		GL3_Fog_DrawSurfaceNow(surf); /* I4c: the face's fog overlay right after it */
		return;
	}

	if (surf->flags & SURF_DRAWTURB)
	{
		/* liquids keep the I0 warp shader; alpha blending from the batch */
		gl3drawCmd_t cmd = GL3_CreateDrawCmd();

		GL3_FX_Flush();
		cmd.texnum = GL3_SoF_TextureAnimation(surf->texinfo, ent ? ent->frame : (int)(GL3_GameTime() * 2))->texnum;
		cmd.flags |= DCFlag_Blend | DCFlag_DisableDepthMask;
		cmd.alpha = 1.0f;

		if (ent && (ent->model != (struct model_s *)gl3_worldmodel))
		{
			GL3_RotateForEntity(ent, &cmd);
		}

		GL3_EmitWaterPolys(surf, cmd);
		GL3_Draw3DBatchesNow();
		return;
	}

	image = GL3_SoF_TextureAnimation(surf->texinfo, (ent && ent->frame) ? ent->frame : (int)(GL3_GameTime() * 2));

	if (surf->texinfo->flags & SOFSURF_FLOWING)
	{
		int value = surf->texinfo->value;
		short vs = (short)((value >> 16) & 0xFFFF), vt = (short)(value & 0xFFFF);

		scroll[0] = vs * GL3_GameTime() * 0.01f;
		scroll[1] = vt * GL3_GameTime() * 0.01f;
	}

	GL3_FX_DefaultState(&st);
	st.texnum = image->texnum;
	st.blend = 1;
	st.alphatest = true;
	/* I4c: gl_fogmode 1 linear fog of the face's region (SPEC-410 Fog regions) */
	GL3_Fog_SetHardwareFxState(GL3_Fog_HardwareRegion(surf), &st);
	GL3_FX_SetState(&st);

	for (p = surf->polys; p; p = p->chain)
	{
		gl3fxvtx_t v[64];
		int i, n = p->numverts > 64 ? 64 : p->numverts;

		for (i = 0; i < n; i++)
		{
			vec3_t light;

			VectorCopy(p->vertices[i].pos, v[i].pos);
			v[i].st[0] = p->vertices[i].texCoord[0] - scroll[0];
			v[i].st[1] = p->vertices[i].texCoord[1] - scroll[1];
			GL3_SoF_LightPoint(p->vertices[i].pos, light, 255);
			v[i].rgba[0] = light[0];
			v[i].rgba[1] = light[1];
			v[i].rgba[2] = light[2];
			v[i].rgba[3] = 1;
		}

		GL3_FX_Add(v, n, GL_TRIANGLE_FAN);
	}

	/* I4c: the face's fog overlay right after it (SPEC-410 Alpha surfaces) */
	GL3_FX_Flush();
	GL3_Fog_DrawSurfaceNow(surf);
	(void)region;
}

/* the placeholder of SPEC-411 §9 */
static void
DrawNullModel(entity_t *e)
{
	gl3fxstate_t st;
	gl3fxvtx_t v[6];
	vec3_t light, fwd, right, up;
	int i, k;

	if (e->flags & SOF_RF_FULLBRIGHT)
	{
		VectorSet(light, 1, 1, 1);
	}
	else
	{
		GL3_SoF_LightPoint(e->origin, light, 255);
	}

	/* yaw about Z, -pitch about Y, -roll about X: the angle vectors of (-pitch, yaw, -roll) */
	{
		vec3_t a = {-e->angles[0], e->angles[1], -e->angles[2]};

		AngleVectors(a, fwd, right, up);
	}

	GL3_FX_DefaultState(&st);
	st.blend = 0;
	st.alphatest = false;
	st.depthwrite = true;
	GL3_FX_SetState(&st);

	for (k = 0; k < 2; k++)
	{
		float z = k ? 16.0f : -16.0f;

		for (i = 0; i < 6; i++)
		{
			float lx, ly, lz;
			int j;

			if (i == 0)
			{
				lx = ly = 0;
				lz = z;
			}
			else
			{
				int idx = k ? (5 - i) : (i - 1);

				lx = 16.0f * cosf(idx * (float)M_PI / 2);
				ly = 16.0f * sinf(idx * (float)M_PI / 2);
				lz = 0;
			}

			for (j = 0; j < 3; j++)
			{
				v[i].pos[j] = e->origin[j] + lx * fwd[j] - ly * right[j] + lz * up[j];
			}

			v[i].st[0] = v[i].st[1] = 0;
			v[i].rgba[0] = light[0];
			v[i].rgba[1] = light[1];
			v[i].rgba[2] = light[2];
			v[i].rgba[3] = 1;
		}

		GL3_FX_Add(v, 6, GL_TRIANGLE_FAN);
	}
}

/* hook for package I4b: Ghoul model drawing through the Ghoul system (SPEC-411 §5) */
extern void GL3_Ghoul_DrawEntity(entity_t *e, sofentity_t *se); /* I4b: gl3_ghoul.c */

static void
DrawGhoulEntity(entity_t *e, sofentity_t *se)
{
	GL3_Ghoul_DrawEntity(e, se); /* I4b */
}

static void
DrawEntity(entity_t *e, int index)
{
	sofentity_t *se = (gl3_sofframe.active && (index < gl3_sofframe.num_entities)) ?
		gl3_sofframe.entities[index] : NULL;

	GL3_FX_Flush();

	if (e->flags & SOF_RF_GHOUL)
	{
		DrawGhoulEntity(e, se);
	}
	else if (!e->model)
	{
		DrawNullModel(e);
	}
	else if (((gl3model_t *)e->model)->type == mod_brush)
	{
		glDepthMask(GL_TRUE);
		GL3_DrawBrushModel(e, (gl3model_t *)e->model);
		GL3_Draw3DBatchesNow();
	}

	GL3_FX_Flush();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
}

void
GL3_FX_SortedPass(void)
{
	int i;
	float nearsq = r_nearclipdist->value * r_nearclipdist->value;
	float farsq = r_farclipdist->value * r_farclipdist->value;

	numsortitems = 0;
	gl3_c_alphaquads = 0;
	GL3_Draw3DBatchesNow();

	/* alpha quads; 0x0040 quads are drawn at once, in submission order */
	if (gl3_sofframe.active)
	{
		for (i = 0; i < gl3_sofframe.num_alphaquads; i++)
		{
			sofalphaquad_t *q = gl3_sofframe.alphaquads[i];

			if (!q || !QuadLeafVisible(q))
			{
				continue;
			}

			gl3_c_alphaquads++;

			if (q->flags & SOF_AQ_IMMEDIATE)
			{
				DrawAlphaQuad(q);
				continue;
			}

			AddItem(DistSq(q->origin, r_newrefdef.vieworg), ITEM_QUAD, q, NULL);
		}

		/* quad chains: key, weight / sqrt(key), total T */
		for (i = 0; i < gl3_sofframe.num_quadchains; i++)
		{
			sofquadchain_t *c = gl3_sofframe.quadchains[i];
			float key;

			if (!c || !c->first)
			{
				continue;
			}

			if (c->first->flags & SOF_AQ_NODEPTH)
			{
				key = nearsq;
			}
			else
			{
				vec3_t d;
				int j;

				for (j = 0; j < 3; j++)
				{
					float a = c->mins[j] - r_newrefdef.vieworg[j];
					float b = c->maxs[j] - r_newrefdef.vieworg[j];

					d[j] = (fabsf(a) < fabsf(b)) ? a : b;
				}

				key = DotProduct(d, d);
			}

			c->weight = (key > 0) ? c->weight / sqrtf(key) : c->weight;
			effecttotal += c->weight;
			AddItem(key, ITEM_CHAIN, c, NULL);
		}

		if (gl_showeffectfill->value)
		{
			Com_Printf("Fill used is %f\n", effecttotal);
		}
	}

	/* translucent world surfaces */
	for (i = 0; i < numalphasurfs; i++)
	{
		msurface_t *s = alphasurfs[i];
		glpoly_t *p;
		float best = farsq;

		for (p = s->polys; p; p = p->chain)
		{
			int j;

			for (j = 0; j < p->numverts; j++)
			{
				float d = DistSq(p->vertices[j].pos, r_newrefdef.vieworg);

				if (d < best)
				{
					best = d;
				}
			}
		}

		AddItem(best, ITEM_SURFACE, s, alphasurfents[i]);
	}

	/* entities of a SoF frame (Quake II frames draw them before this pass) */
	if (gl3_sofframe.active && r_drawentities->value)
	{
		for (i = 0; i < r_newrefdef.num_entities; i++)
		{
			entity_t *e = &r_newrefdef.entities[i];
			float key;

			if (e->flags & SOF_RF_WEAPONMODEL)
			{
				key = 0;
			}
			else if (e->model && !(e->flags & SOF_RF_GHOUL) &&
				(((gl3model_t *)e->model)->type == mod_brush))
			{
				gl3model_t *m = (gl3model_t *)e->model;
				vec3_t c;
				int j;

				for (j = 0; j < 3; j++)
				{
					c[j] = e->origin[j] + (m->mins[j] + m->maxs[j]) * 0.5f;
				}

				key = DistSq(c, r_newrefdef.vieworg);
			}
			else
			{
				key = DistSq(e->origin, r_newrefdef.vieworg);
			}

			AddItem(key, ITEM_ENTITY, (void *)(intptr_t)i, e);
		}
	}

	qsort(sortitems, numsortitems, sizeof(sortitems[0]), CompareItems);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	for (i = 0; i < numsortitems; i++)
	{
		sortitem_t *it = &sortitems[i];

		switch (it->kind)
		{
			case ITEM_QUAD:
				DrawAlphaQuad((sofalphaquad_t *)it->ptr);
				break;
			case ITEM_CHAIN:
				GL3_FX_Flush();
				DrawQuadChain((sofquadchain_t *)it->ptr);
				GL3_FX_Flush();
				break;
			case ITEM_SURFACE:
				DrawAlphaSurface((msurface_t *)it->ptr, it->ent);
				break;
			case ITEM_ENTITY:
				DrawEntity(it->ent, (int)(intptr_t)it->ptr);
				break;
		}
	}

	GL3_FX_Flush();
	GL3_Draw3DBatchesNow();

	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	if (gl3_sofframe.active && gl3_sofframe.fd)
	{
		gl3_sofframe.fd->scratch = NULL;
	}

	numalphasurfs = 0;
}

/* ------------------------------------------------------------------
 * full-screen overlays of the 3D view (SPEC-412 §11)
 * ------------------------------------------------------------------ */

static GLuint noisetex;
static float nvtime;
static qboolean nvbright;
static qboolean nvhavetex;

static int
IRand(int lo, int hi)
{
	return lo + (int)((hi - lo + 1) * ((float)rand() / ((float)RAND_MAX + 1.0f)));
}

static float
FRand(float lo, float hi)
{
	return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

static void
NightVisionTexture(void)
{
	static byte data[64 * 64 * 3];
	float m = 1.0f;
	int i;

	nvbright = (IRand(0, 10) == 0);

	if (nvtime < 2)
	{
		m = (nvtime <= 1.5f) ? nvtime : 3.0f - nvtime;
	}

	for (i = 0; i < 64 * 64; i++)
	{
		int r, g, b;

		if (nvbright)
		{
			r = 0;
			g = IRand(220, 255);
			b = 0;
		}
		else
		{
			r = 40;
			g = IRand(160, 210);
			b = 40;
		}

		if (nvtime < 2)
		{
			r = (int)(r * m);
			b = (int)(b * m);
			g = (int)(g * m);

			if (g > 255)
			{
				g = IRand(230, 255);
			}

			if (r > 255)
			{
				r = 255;
			}

			if (b > 255)
			{
				b = 255;
			}
		}

		data[i * 3 + 0] = r;
		data[i * 3 + 1] = g;
		data[i * 3 + 2] = b;
	}

	if (!noisetex)
	{
		glGenTextures(1, &noisetex);
	}

	GL3_Bind(noisetex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 64, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	nvhavetex = true;
}

/* last frame's statistics for the warning tint (gl3_main.c) */
extern int c_brush_polys;

static void
ViewQuad(float xy[4][2])
{
	float x = r_newrefdef.x, y = r_newrefdef.y, w = r_newrefdef.width, h = r_newrefdef.height;

	xy[0][0] = x;     xy[0][1] = y;
	xy[1][0] = x + w; xy[1][1] = y;
	xy[2][0] = x + w; xy[2][1] = y + h;
	xy[3][0] = x;     xy[3][1] = y + h;
}

/* drawn in 2D over the view rectangle, which is what the original's quad at distance 15.9 covers */
void
GL3_FX_ScreenOverlays(void)
{
	float blend[4];
	float xy[4][2];

	if (r_newrefdef.rdflags & SOF_RDF_NOWORLDMODEL)
	{
		nvtime = 0;
	}

	/* night vision */
	if (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES)
	{
		float st[4][2];
		float u0 = FRand(-16, -8), v0 = FRand(-16, -8), u1 = FRand(8, 16), v1 = FRand(8, 16);
		const byte white[4] = {255, 255, 255, 255};

		if (nvtime < 2)
		{
			nvtime += gl3_sofframe.active ? gl3_sofframe.gametime_delta : 0.1f;
			NightVisionTexture();
		}
		else if (!nvhavetex || (IRand(0, 10) < 2) || nvbright)
		{
			NightVisionTexture();
		}

		ViewQuad(xy);
		st[0][0] = u0; st[0][1] = v0;
		st[1][0] = u1; st[1][1] = v0;
		st[2][0] = u1; st[2][1] = v1;
		st[3][0] = u0; st[3][1] = v1;

		GL3_SoF2D_Flush();
		glEnable(GL_BLEND);
		glBlendFunc(GL_DST_COLOR, GL_ZERO);
		{
			/* the 2D batch applies normal blending; draw this one directly */
			extern void GL3_SoF2D_DrawMultiply(GLuint tex, const float xy[4][2], const float st[4][2], const byte rgba[4]);

			GL3_SoF2D_DrawMultiply(noisetex, xy, st, white);
		}
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}
	else
	{
		nvtime = 0;

		if (nvhavetex && noisetex)
		{
			glDeleteTextures(1, &noisetex);
			noisetex = 0;
			nvhavetex = false;
		}
	}

	/* screen tint, replaced by the warning tint when a budget was exceeded */
	memcpy(blend, r_newrefdef.blend, sizeof(blend));

	if (r_warnings->value && gl3_worldmodel && gl3_worldmodel->vis && (gl3_viewcluster >= 0))
	{
		float a = 0;
		qboolean any = false;

		memset(blend, 0, sizeof(blend));

		if (c_brush_polys > 700)
		{
			blend[0] = 1;
			a += (c_brush_polys - 700) / 150.0f + 0.2f;
			any = true;
		}

		if (any)
		{
			blend[3] = (a > 1) ? 1 : a;
		}
		else
		{
			memcpy(blend, r_newrefdef.blend, sizeof(blend));
		}
	}

	if (blend[3] != 0)
	{
		byte c[4];
		int i;

		for (i = 0; i < 4; i++)
		{
			float f = blend[i] * 255.0f;

			c[i] = (byte)(f < 0 ? 0 : f > 255 ? 255 : f);
		}

		ViewQuad(xy);
		SoF_DrawFill((int)xy[0][0], (int)xy[0][1], r_newrefdef.width, r_newrefdef.height, c);
	}
}
