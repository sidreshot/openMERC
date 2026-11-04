/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2016-2017 Daniel Gibson
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
 * Lightmap handling
 *
 * =======================================================================
 *
 * OpenMerc: lightmap size and sample spacing come from msurface_t
 * (per-face scale of IBSP v46). Package I4c: gl_monolightmap latched at
 * map load (applied in the lightmapped shaders).
 * Implements: SPEC-101, SPEC-410
 */

#include "header/local.h"


#define TEXNUM_LIGHTMAPS 1024

extern gl3lightmapstate_t gl3_lms;

/* OpenMerc (I4c): gl_monolightmap latched when the world's lightmaps are built */
int gl3_monolightmap_mode;

void
GL3_LM_InitBlock(void)
{
	memset(gl3_lms.allocated, 0, sizeof(gl3_lms.allocated));
}

void
GL3_LM_UploadBlock(void)
{
	int map;

	// NOTE: we don't use the dynamic lightmap anymore - all lightmaps are loaded at level load
	//       and not changed after that. they're blended dynamically depending on light styles
	//       though, and dynamic lights are (will be) applied in shader, hopefully per fragment.

	GL3_BindLightmap(gl3_lms.current_lightmap_texture);

	// upload all 4 lightmaps
	for(map=0; map < MAX_LIGHTMAPS_PER_SURFACE; ++map)
	{
		GL3_SelectTMU(GL_TEXTURE1+map); // this relies on GL_TEXTURE2 being GL_TEXTURE1+1 etc
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_LIGHTMAP_FORMAT,
		             BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_LIGHTMAP_FORMAT,
		             GL_UNSIGNED_BYTE, gl3_lms.lightmap_buffers[map]);
	}

	if (++gl3_lms.current_lightmap_texture == MAX_LIGHTMAPS)
	{
		Com_Error(ERR_DROP, "LM_UploadBlock() - MAX_LIGHTMAPS exceeded\n");
	}
}

/*
 * returns a texture number and the position inside it
 */
qboolean
GL3_LM_AllocBlock(int w, int h, int *x, int *y)
{
	int i, j;
	int best, best2;

	best = BLOCK_HEIGHT;

	for (i = 0; i < BLOCK_WIDTH - w; i++)
	{
		best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (gl3_lms.allocated[i + j] >= best)
			{
				break;
			}

			if (gl3_lms.allocated[i + j] > best2)
			{
				best2 = gl3_lms.allocated[i + j];
			}
		}

		if (j == w)
		{
			/* this is a valid spot */
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > BLOCK_HEIGHT)
	{
		return false;
	}

	for (i = 0; i < w; i++)
	{
		gl3_lms.allocated[*x + i] = best + h;
	}

	return true;
}

void
GL3_LM_BuildPolygonFromSurface(gl3model_t *currentmodel, msurface_t *fa)
{
	int i, lindex, lnumverts;
	medge_t *pedges, *r_pedge;
	float *vec;
	float s, t;
	glpoly_t *poly;
	vec3_t total;
	vec3_t normal;

	/* reconstruct the polygon */
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	VectorClear(total);

	/* draw texture */
	poly = Hunk_Alloc(sizeof(glpoly_t) +
		   (lnumverts - 4) * sizeof(gl3_3D_vtx_t));
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	VectorCopy(fa->plane->normal, normal);

	if(fa->flags & SURF_PLANEBACK)
	{
		// if for some reason the normal sticks to the back of the plane, invert it
		// so it's usable for the shader
		for (i=0; i<3; ++i)  normal[i] = -normal[i];
	}

	for (i = 0; i < lnumverts; i++)
	{
		gl3_3D_vtx_t* vert = &poly->vertices[i];

		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = currentmodel->vertexes[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = currentmodel->vertexes[r_pedge->v[1]].position;
		}

		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->image->width;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->image->height;

		VectorAdd(total, vec, total);
		VectorCopy(vec, vert->pos);
		vert->texCoord[0] = s;
		vert->texCoord[1] = t;

		/* lightmap texture coordinates: sample i lies at
		   texturemins + lmstep * i (16 texels in Quake II, 16 or 32 in
		   SoF v46, SPEC-101), at the center of lightmap texel i */
		s = DotProduct(vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s /= fa->lmstep[0];
		/* OpenMerc (I4c), SPEC-410 Texture coordinates: with a sample spacing other than 16 the
		   sample position is floored */
		if (fa->lmstep[0] != 16)
		{
			s = floorf(s);
		}
		s += fa->light_s + 0.5f;
		s /= BLOCK_WIDTH;

		t = DotProduct(vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t /= fa->lmstep[1];
		if (fa->lmstep[1] != 16)
		{
			t = floorf(t);
		}
		t += fa->light_t + 0.5f;
		t /= BLOCK_HEIGHT;

		vert->lmTexCoord[0] = s;
		vert->lmTexCoord[1] = t;

		VectorCopy(normal, vert->normal);
		vert->lightFlags = 0;
	}
}

void
GL3_LM_CreateSurfaceLightmap(msurface_t *surf)
{
	int smax, tmax;

	if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB))
	{
		return;
	}

	smax = surf->lmsize[0];
	tmax = surf->lmsize[1];

	if (!GL3_LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
	{
		GL3_LM_UploadBlock();
		GL3_LM_InitBlock();

		if (!GL3_LM_AllocBlock(smax, tmax, &surf->light_s, &surf->light_t))
		{
			Com_Error(ERR_FATAL, "Consecutive calls to LM_AllocBlock(%d,%d) failed\n",
					smax, tmax);
		}
	}

	surf->lightmaptexturenum = gl3_lms.current_lightmap_texture;

	GL3_BuildLightMap(surf, (surf->light_t * BLOCK_WIDTH + surf->light_s) * LIGHTMAP_BYTES, BLOCK_WIDTH * LIGHTMAP_BYTES);
}

void
GL3_LM_BeginBuildingLightmaps(gl3model_t *m)
{
	static lightstyle_t lightstyles[MAX_LIGHTSTYLES];
	int i;

	GL3_DynLight_Free(); /* OpenMerc (I4c): new lightmap pages, the dynamic layers start again */
	GL3_Seam_Invalidate(); /* gl3_surf.c r_om_seams: a new map */

	memset(gl3_lms.allocated, 0, sizeof(gl3_lms.allocated));

	gl3_framecount = 1; /* no dlightcache */

	/* setup the base lightstyles so the lightmaps
	   won't have to be regenerated the first time
	   they're seen */
	for (i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		lightstyles[i].rgb[0] = 1;
		lightstyles[i].rgb[1] = 1;
		lightstyles[i].rgb[2] = 1;
		lightstyles[i].white = 3;
	}

	r_newrefdef.lightstyles = lightstyles;

	gl3_lms.current_lightmap_texture = 0;

	/* OpenMerc (I4c): the texel and upload format of SoF lightmaps follow the first character of
	   gl_monolightmap when the lightmaps are built (SPEC-410 Lightmaps): 0 colour table, L, I, C, A, other */
	switch ((gl_monolightmap && gl_monolightmap->string) ? gl_monolightmap->string[0] : '0')
	{
		case '0': gl3_monolightmap_mode = 0; break;
		case 'L': gl3_monolightmap_mode = 1; break;
		case 'I': gl3_monolightmap_mode = 2; break;
		case 'C': gl3_monolightmap_mode = 3; break;
		case 'A': gl3_monolightmap_mode = 4; break;
		default: gl3_monolightmap_mode = 5; break;
	}

	// Note: the dynamic lightmap used to be initialized here, we don't use that anymore.
}

void
GL3_LM_EndBuildingLightmaps(void)
{
	GL3_LM_UploadBlock();
}

/*
 * OpenMerc GFX-modern r_om_lmstitch: lightmap seam stitching. Every BSP face has a lightmap of its own; where a
 * plane is cut into several faces (a shop shutter split by the planes of the brushes around it) the compiler lit
 * each face on its own grid and the two lightmaps disagree along the shared border: a straight step of light
 * across a wall. As the light bakers do it (Unity / Bakery "stitch seams", Sylvan's lightmap seam stitching): along
 * every border of two coplanar faces, every half sample, both lightmaps are read bilinearly as drawn; the samples
 * that make up each reading are moved toward the mean of the two (three rounds), and the correction fades into the
 * face over r_om_lmstitch_width samples, so no new step appears one sample in. Per light style (the same style on
 * both sides); before the lightmaps are uploaded, so everything that reads them (models' light point) agrees.
 */

typedef struct
{
	const cplane_t *plane;
	int back, face;
	vec3_t a, b;
} lmedge_t;

static int
LMEdgeCmp(const void *x, const void *y)
{
	const lmedge_t *p = x, *q = y;

	if (p->plane != q->plane)
	{
		return (p->plane < q->plane) ? -1 : 1;
	}

	return p->back - q->back;
}

static qboolean
LMStitchFace(const msurface_t *s)
{
	return s->samples && s->texinfo && (s->styles[0] != 255) &&
		!(s->texinfo->flags & (SURF_SKY | SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_NODRAW | SOFSURF_ALPHA));
}

/* bilinear position in the face's samples: the 4 samples and weights at p */
static void
LMTaps(const msurface_t *s, const vec3_t p, int idx[4], float w[4])
{
	float u = (DotProduct(p, s->texinfo->vecs[0]) + s->texinfo->vecs[0][3] - s->texturemins[0]) / s->lmstep[0];
	float v = (DotProduct(p, s->texinfo->vecs[1]) + s->texinfo->vecs[1][3] - s->texturemins[1]) / s->lmstep[1];
	int x0, y0, x1, y1;
	float fx, fy;

	u = Q_clamp(u, 0.0f, (float)(s->lmsize[0] - 1));
	v = Q_clamp(v, 0.0f, (float)(s->lmsize[1] - 1));
	x0 = (int)u;
	y0 = (int)v;
	x1 = Q_min(x0 + 1, s->lmsize[0] - 1);
	y1 = Q_min(y0 + 1, s->lmsize[1] - 1);
	fx = u - x0;
	fy = v - y0;
	idx[0] = y0 * s->lmsize[0] + x0;
	idx[1] = y0 * s->lmsize[0] + x1;
	idx[2] = y1 * s->lmsize[0] + x0;
	idx[3] = y1 * s->lmsize[0] + x1;
	w[0] = (1 - fx) * (1 - fy);
	w[1] = fx * (1 - fy);
	w[2] = (1 - fx) * fy;
	w[3] = fx * fy;
}

void
GL3_LM_StitchSeams(gl3model_t *m)
{
	cvar_t *on = ri.Cvar_Get("r_om_lmstitch", "1", CVAR_ARCHIVE);
	cvar_t *width = ri.Cvar_Get("r_om_lmstitch_width", "3", CVAR_ARCHIVE);
	lmedge_t *ed;
	float **acc;   /* per face: per style per sample r g b weight */
	int numed = 0, i, j, g0, g1, round, faces = 0, points = 0, K;

	if (!on->value || (m->bspversion != BSPVERSION_SOF) || !m->numsurfaces)
	{
		return;
	}

	K = (int)Q_clamp(width->value, 1.0f, 8.0f);
	ed = malloc(sizeof(*ed) * (size_t)m->numsurfedges);
	acc = calloc(m->numsurfaces, sizeof(*acc));

	if (!ed || !acc)
	{
		free(ed);
		free(acc);
		return;
	}

	for (i = 0; i < m->numsurfaces; i++)
	{
		const msurface_t *s = &m->surfaces[i];

		if (!LMStitchFace(s))
		{
			continue;
		}

		for (j = 0; j < s->numedges; j++)
		{
			int e0 = m->surfedges[s->firstedge + j], e1 = m->surfedges[s->firstedge + (j + 1) % s->numedges];
			const float *va = m->vertexes[(e0 > 0) ? m->edges[e0].v[0] : m->edges[-e0].v[1]].position;
			const float *vb = m->vertexes[(e1 > 0) ? m->edges[e1].v[0] : m->edges[-e1].v[1]].position;
			lmedge_t *e = &ed[numed];
			vec3_t d;

			VectorSubtract(vb, va, d);

			if (DotProduct(d, d) < 1.0f)
			{
				continue;
			}

			e->plane = s->plane;
			e->back = (s->flags & SURF_PLANEBACK) ? 1 : 0;
			e->face = i;
			VectorCopy(va, e->a);
			VectorCopy(vb, e->b);
			numed++;
		}
	}

	qsort(ed, numed, sizeof(*ed), LMEdgeCmp);

	for (round = 0; round < 3; round++)
	{
		for (i = 0; i < m->numsurfaces; i++)
		{
			if (acc[i])
			{
				msurface_t *s = &m->surfaces[i];
				int ns, n = s->lmsize[0] * s->lmsize[1];

				for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && s->styles[ns] != 255; ns++)
				{
				}

				memset(acc[i], 0, sizeof(float) * 4 * n * ns);
			}
		}

		for (g0 = 0; g0 < numed; g0 = g1)
		{
			for (g1 = g0 + 1; (g1 < numed) && !LMEdgeCmp(&ed[g0], &ed[g1]); g1++)
			{
			}

			for (i = g0; i < g1; i++)
			{
				vec3_t t;
				float len;

				VectorSubtract(ed[i].b, ed[i].a, t);
				len = VectorNormalize(t);

				for (j = i + 1; j < g1; j++)
				{
					msurface_t *A = &m->surfaces[ed[i].face], *B = &m->surfaces[ed[j].face];
					vec3_t r0, r1, off, p;
					float s0, s1, lo, hi, step, x;
					int ka, kb;

					if (ed[i].face == ed[j].face)
					{
						continue;
					}

					VectorSubtract(ed[j].a, ed[i].a, r0);
					VectorSubtract(ed[j].b, ed[i].a, r1);
					s0 = DotProduct(r0, t);
					s1 = DotProduct(r1, t);
					VectorMA(r0, -s0, t, off);
					if (DotProduct(off, off) > 0.01f)
					{
						continue;
					}
					VectorMA(r1, -s1, t, off);
					if (DotProduct(off, off) > 0.01f)
					{
						continue;
					}

					lo = Q_max(0.0f, Q_min(s0, s1));
					hi = Q_min(len, Q_max(s0, s1));

					if (hi - lo < 2.0f)
					{
						continue;
					}

					step = Q_min(A->lmstep[0], B->lmstep[0]) * 0.5f;

					for (ka = 0; ka < MAX_LIGHTMAPS_PER_SURFACE && A->styles[ka] != 255; ka++)
					{
						for (kb = 0; kb < MAX_LIGHTMAPS_PER_SURFACE && B->styles[kb] != 255; kb++)
						{
							if (A->styles[ka] == B->styles[kb])
							{
								break;
							}
						}

						if ((kb == MAX_LIGHTMAPS_PER_SURFACE) || (B->styles[kb] == 255))
						{
							continue;
						}

						for (x = lo + Q_min(step * 0.5f, (hi - lo) * 0.5f); x < hi; x += step)
						{
							int ia[4], ib[4], q, c, na = A->lmsize[0] * A->lmsize[1], nb = B->lmsize[0] * B->lmsize[1];
							float wa[4], wb[4], VA[3] = {0, 0, 0}, VB[3] = {0, 0, 0};
							const byte *sa = A->samples + (size_t)ka * na * 3, *sb = B->samples + (size_t)kb * nb * 3;

							VectorMA(ed[i].a, x, t, p);
							LMTaps(A, p, ia, wa);
							LMTaps(B, p, ib, wb);

							for (q = 0; q < 4; q++)
							{
								for (c = 0; c < 3; c++)
								{
									VA[c] += wa[q] * sa[ia[q] * 3 + c];
									VB[c] += wb[q] * sb[ib[q] * 3 + c];
								}
							}

							/* the samples of each side are moved toward the mean, by their share of the reading */
							for (q = 0; q < 2; q++)
							{
								msurface_t *S = q ? B : A;
								int fi = (int)(S - m->surfaces), k = q ? kb : ka, n = q ? nb : na;
								const int *ix = q ? ib : ia;
								const float *w = q ? wb : wa, *V = q ? VB : VA;
								int z;

								if (!acc[fi])
								{
									int ns;

									for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && S->styles[ns] != 255; ns++)
									{
									}

									acc[fi] = calloc((size_t)4 * n * ns, sizeof(float));

									if (!acc[fi])
									{
										continue;
									}

									faces++;
								}

								for (z = 0; z < 4; z++)
								{
									float *a = acc[fi] + ((size_t)k * n + ix[z]) * 4;

									for (c = 0; c < 3; c++)
									{
										a[c] += w[z] * (0.5f * (VA[c] + VB[c]) - V[c]);
									}

									a[3] += w[z];
								}
							}

							points++;
						}
					}
				}
			}
		}

		/* the corrections: full on the samples that make up the border, fading into the face over K samples */
		for (i = 0; i < m->numsurfaces; i++)
		{
			msurface_t *s = &m->surfaces[i];
			int ns, k, n, W, H, it, x, y, c;
			float *D;
			int *dist;

			if (!acc[i])
			{
				continue;
			}

			W = s->lmsize[0];
			H = s->lmsize[1];
			n = W * H;

			for (ns = 0; ns < MAX_LIGHTMAPS_PER_SURFACE && s->styles[ns] != 255; ns++)
			{
			}

			D = malloc(sizeof(float) * 3 * n);
			dist = malloc(sizeof(int) * n);

			if (!D || !dist)
			{
				free(D);
				free(dist);
				continue;
			}

			for (k = 0; k < ns; k++)
			{
				const float *a = acc[i] + (size_t)k * n * 4;
				byte *smp = s->samples + (size_t)k * n * 3;
				qboolean any = false;

				for (x = 0; x < n; x++)
				{
					if (a[x * 4 + 3] > 0.05f)
					{
						for (c = 0; c < 3; c++)
						{
							D[x * 3 + c] = a[x * 4 + c] / a[x * 4 + 3];
						}

						dist[x] = 0;
						any = true;
					}
					else
					{
						D[x * 3] = D[x * 3 + 1] = D[x * 3 + 2] = 0;
						dist[x] = -1;
					}
				}

				if (!any)
				{
					continue;
				}

				for (it = 1; it <= K; it++)
				{
					for (y = 0; y < H; y++)
					{
						for (x = 0; x < W; x++)
						{
							int l = y * W + x, nn = 0, d;
							float sum[3] = {0, 0, 0};
							static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

							if (dist[l] >= 0)
							{
								continue;
							}

							for (d = 0; d < 4; d++)
							{
								int xx = x + dx[d], yy = y + dy[d], o;

								if ((xx < 0) || (yy < 0) || (xx >= W) || (yy >= H))
								{
									continue;
								}

								o = yy * W + xx;

								if ((dist[o] >= 0) && (dist[o] < it))
								{
									for (c = 0; c < 3; c++)
									{
										sum[c] += D[o * 3 + c];
									}

									nn++;
								}
							}

							if (nn)
							{
								for (c = 0; c < 3; c++)
								{
									D[l * 3 + c] = sum[c] / nn * (1.0f - 1.0f / (K + 1 - it + 1));
								}

								dist[l] = it;
							}
						}
					}
				}

				for (x = 0; x < n; x++)
				{
					for (c = 0; c < 3; c++)
					{
						int v = (int)floorf(smp[x * 3 + c] + D[x * 3 + c] + 0.5f);

						smp[x * 3 + c] = (byte)Q_clamp(v, 0, 255);
					}
				}
			}

			free(D);
			free(dist);
		}
	}

	for (i = 0; i < m->numsurfaces; i++)
	{
		free(acc[i]);
	}

	free(acc);
	free(ed);
	R_Printf(PRINT_DEVELOPER, "r_om_lmstitch: %d faces, %d border points\n", faces, points / 3);
}

