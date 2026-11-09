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

/* GFX-modern r_om_water: 1 (default) a slow swell lifting the surface, per-pixel ripples moving the texture, a
   highlight of the light on them, the Fresnel term (grazing angles reflect and hide more) and, on translucent water
   over the scene depth, soft edges where it meets the ground and walls and more opacity where it is deep; 0 the
   original's liquids. r_om_water_quality 2 (default): irregular fine chop, the light dancing under the
   surface (caustics), and translucent water refracts the scene under it, tints it with depth and has foam where it
   meets walls and objects; 1 the lighter set. r_om_water_glint: strength of the highlight. Lava, molten
   metal and steam keep the original look. */
static cvar_t *r_om_water, *r_om_water_glint, *r_om_water_quality, *r_om_water_clear, *r_om_water_3d;
static GLuint liqtile;
static GLuint liqenv;
static int liqenvmap = -1;

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
	liqtile = liqenv = 0; /* a new context: the ripple tile and the sky panorama are made again */
	liqenvmap = -1;
	r_om_water = ri.Cvar_Get("r_om_water", "1", CVAR_ARCHIVE);
	r_om_water_glint = ri.Cvar_Get("r_om_water_glint", "1", CVAR_ARCHIVE);
	/* r_om_water_clear 1: level water the original draws solid is see-through too (quality 2 and up: the scene under
	   it refracted, darker and tinted with depth), as the water of the Source engine */
	r_om_water_clear = ri.Cvar_Get("r_om_water_clear", "1", CVAR_ARCHIVE);
	/* r_om_water_3d: height of the ripples the surface really has near the eye (units; 0 flat as the original) */
	r_om_water_3d = ri.Cvar_Get("r_om_water_3d", "1", CVAR_ARCHIVE);
	/* 2 on phones too: it measured the same as 1 there (Adreno 610); the refraction needs the depth copy */
#ifdef YQ2_GL3_GLES3
	r_om_water_quality = ri.Cvar_Get("r_om_water_quality", "2", CVAR_ARCHIVE);
#else
	/* 3: and the planar reflection (a second draw of the world in the water's box; the traced reflection with the
	   blurred sky looked as good on the tour views for much less) */
	r_om_water_quality = ri.Cvar_Get("r_om_water_quality", "2", CVAR_ARCHIVE);
#endif

	for (k = 0; k < turbstarts; k++)
	{
		amp *= 0.5f;
	}

	for (k = 0; k < 256; k++)
	{
		turbtable[k] = amp * sinf(2.0f * (float)M_PI * k / 256.0f);
	}
}

/*
 * GFX-modern r_om_water: the ripple tile. The slopes of a periodic height field with an isotropic spectrum - every
 * integer wave vector in a ring of the 256-texel tile, height falling as |k|^-2.2, random phases - made by two
 * inverse FFTs at the first water face. Every direction has the same weight, so no row of waves shows; the shader
 * reads it three times at other scales, turned by odd angles and drifting different ways (the scrolling normal maps
 * of the Source engine's water, on DX8 hardware too). RGBA8: slope x, y as 0.5 + 0.5 s (RMS 0.35), the height in
 * blue; mipmapped, so far water calms instead of shimmering.
 */
#define TILE_N 256
#define TILE_LOG 8

static void
FFT1(float *re, float *im, int stride)
{
	int i, j, k, len;

	for (i = 1, j = 0; i < TILE_N; i++)
	{
		int bit = TILE_N >> 1;

		for (; j & bit; bit >>= 1)
		{
			j ^= bit;
		}

		j ^= bit;

		if (i < j)
		{
			float t = re[i * stride];

			re[i * stride] = re[j * stride];
			re[j * stride] = t;
			t = im[i * stride];
			im[i * stride] = im[j * stride];
			im[j * stride] = t;
		}
	}

	for (len = 2; len <= TILE_N; len <<= 1)
	{
		double ang = 2.0 * M_PI / len; /* inverse transform: positive exponent */
		float wr = (float)cos(ang), wi = (float)sin(ang);

		for (i = 0; i < TILE_N; i += len)
		{
			float cr = 1, ci = 0;

			for (k = 0; k < len / 2; k++)
			{
				int a = (i + k) * stride, b = (i + k + len / 2) * stride;
				float xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr, t;

				re[b] = re[a] - xr;
				im[b] = im[a] - xi;
				re[a] += xr;
				im[a] += xi;
				t = cr * wr - ci * wi;
				ci = cr * wi + ci * wr;
				cr = t;
			}
		}
	}
}

static void
FFT2(float *re, float *im)
{
	int r;

	for (r = 0; r < TILE_N; r++)
	{
		FFT1(re + r * TILE_N, im + r * TILE_N, 1);
	}

	for (r = 0; r < TILE_N; r++)
	{
		FFT1(re + r, im + r, TILE_N);
	}
}

static unsigned int
TileRand(unsigned int *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s >> 8;
}

static void
MakeTile(void)
{
	const int nn = TILE_N * TILE_N;
	float *xr = calloc(nn, sizeof(float)), *xi = calloc(nn, sizeof(float));
	float *yr = calloc(nn, sizeof(float)), *yi = calloc(nn, sizeof(float));
	float *hr = calloc(nn, sizeof(float)), *hi = calloc(nn, sizeof(float));
	byte *px = malloc(nn * 4);
	unsigned int seed = 0x5eed1234u;
	double sx = 0, sh = 0, gs, hs;
	int u, v, i;

	if (!xr || !xi || !yr || !yi || !hr || !hi || !px)
	{
		free(xr); free(xi); free(yr); free(yi); free(hr); free(hi); free(px);
		return;
	}

	for (v = 0; v < TILE_N; v++)
	{
		for (u = 0; u < TILE_N; u++)
		{
			int kx = (u < TILE_N / 2) ? u : u - TILE_N, ky = (v < TILE_N / 2) ? v : v - TILE_N;
			double k = sqrt((double)(kx * kx + ky * ky)), a, ph, ar, ai;

			/* the ring: the longest ripple a fifth of the tile, the shortest 6 texels; soft ends */
			if ((k < 3.0) || (k > 44.0))
			{
				continue;
			}

			a = pow(k, -2.2) * (0.5 + (TileRand(&seed) & 0xffff) / 65536.0);
			a *= Q_min(1.0, (k - 3.0) / 2.0) * Q_min(1.0, (44.0 - k) / 8.0);
			ph = (TileRand(&seed) & 0xffff) / 65536.0 * 2.0 * M_PI;
			ar = a * cos(ph);
			ai = a * sin(ph);
			i = v * TILE_N + u;
			hr[i] = (float)ar;
			hi[i] = (float)ai;
			/* the slope: i k H */
			xr[i] = (float)(-kx * ai);
			xi[i] = (float)(kx * ar);
			yr[i] = (float)(-ky * ai);
			yi[i] = (float)(ky * ar);
		}
	}

	FFT2(xr, xi);
	FFT2(yr, yi);
	FFT2(hr, hi);

	for (i = 0; i < nn; i++)
	{
		sx += xr[i] * xr[i] + yr[i] * yr[i];
		sh += hr[i] * hr[i];
	}

	gs = 0.35 / sqrt(Q_max(sx / (2.0 * nn), 1e-12));
	hs = 0.25 / sqrt(Q_max(sh / nn, 1e-12));

	for (i = 0; i < nn; i++)
	{
		px[i * 4 + 0] = (byte)Q_clamp((int)(127.5 + 127.5 * xr[i] * gs), 0, 255);
		px[i * 4 + 1] = (byte)Q_clamp((int)(127.5 + 127.5 * yr[i] * gs), 0, 255);
		px[i * 4 + 2] = (byte)Q_clamp((int)(127.5 + 127.5 * hr[i] * hs), 0, 255);
		px[i * 4 + 3] = 255;
	}

	glGenTextures(1, &liqtile);
	GL3_SelectTMU(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, liqtile);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TILE_N, TILE_N, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	GL3_SelectTMU(GL_TEXTURE0);

	free(xr); free(xi); free(yr); free(yi); free(hr); free(hi); free(px);
}

/*
 * GFX-modern r_om_water: what the water reflects without the planar reflection (the env_cubemap of the Source
 * engine, the look of its DX8 water): under open sky the map's sky - a panorama of the sky box, elevation 0..90
 * degrees, blurred - and under a roof the room, blurred (the light around the water, made in the shader). How open
 * the sky is over a vertex: rays straight up from it and four points 32 units around, through the BSP, to the
 * first face: a sky face counts as open (cached per vertex, cleared with the map).
 */
#define ENV_W 64
#define ENV_H 16

static int liqmapcount;

/* 1 sky, 2 another face or solid, 0 out of the world */
static int
SkyTrace(mnode_t *node, const vec3_t start, const vec3_t end)
{
	float front, back, frac;
	int side, i, r;
	vec3_t mid;
	msurface_t *surf;

	if (node->contents != CONTENTS_NODE)
	{
		return (node->contents & CONTENTS_SOLID) ? 2 : 0;
	}

	front = DotProduct(start, node->plane->normal) - node->plane->dist;
	back = DotProduct(end, node->plane->normal) - node->plane->dist;
	side = front < 0;

	if ((back < 0) == side)
	{
		return SkyTrace(node->children[side], start, end);
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	r = SkyTrace(node->children[side], start, mid);

	if (r)
	{
		return r;
	}

	surf = gl3_worldmodel->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		const mtexinfo_t *tex = surf->texinfo;
		float s, t;

		if (surf->flags & SURF_DRAWTURB)
		{
			continue;
		}

		s = DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3];

		if ((s < surf->texturemins[0]) || (t < surf->texturemins[1]) ||
			(s - surf->texturemins[0] > surf->extents[0]) || (t - surf->texturemins[1] > surf->extents[1]))
		{
			continue;
		}

		return (tex->flags & SURF_SKY) ? 1 : 2;
	}

	return SkyTrace(node->children[!side], mid, end);
}

#define SKYCACHE 8192

static struct
{
	int key[3];
	float open;
	qboolean used;
} skycache[SKYCACHE];

static float
SkyOpen(const vec3_t p)
{
	static const float off[5][2] = {{0, 0}, {32, 0}, {-32, 0}, {0, 32}, {0, -32}};
	int key[3] = {(int)floorf(p[0] + 0.5f), (int)floorf(p[1] + 0.5f), (int)floorf(p[2] + 0.5f)};
	unsigned int h = ((unsigned int)key[0] * 73856093u) ^ ((unsigned int)key[1] * 19349663u) ^
		((unsigned int)key[2] * 83492791u);
	int slot = (int)(h % SKYCACHE), probe, k;
	float open = 0;

	for (probe = 0; probe < 16; probe++)
	{
		int s = (slot + probe) % SKYCACHE;

		if (!skycache[s].used)
		{
			slot = s;
			break;
		}

		if (!memcmp(skycache[s].key, key, sizeof(key)))
		{
			return skycache[s].open;
		}
	}

	if (!gl3_worldmodel || !gl3_worldmodel->nodes)
	{
		return 0;
	}

	for (k = 0; k < 5; k++)
	{
		vec3_t a = {p[0] + off[k][0], p[1] + off[k][1], p[2] + 4}, b = {a[0], a[1], p[2] + 16384};

		open += (SkyTrace(gl3_worldmodel->nodes, a, b) == 1) ? 0.2f : 0.0f;
	}

	if (probe < 16)
	{
		memcpy(skycache[slot].key, key, sizeof(key));
		skycache[slot].open = open;
		skycache[slot].used = true;
	}

	return open;
}

/* a new map (gl3_model.c): the sky cache and the sky panorama go */
void
GL3_Liquid_MapLoad(void)
{
	memset(skycache, 0, sizeof(skycache));
	liqmapcount++;
}

/* the sky panorama of this map on the texture liqenv: the sky images read back (desktop GL) and sampled 3x3 per
   texel, blurred; a map without a sky, or GLES (no read-back), gets an even pale sky */
static void
MakeEnv(void)
{
	static byte px[ENV_H][ENV_W][4];
	static float acc[ENV_H][ENV_W][3], tmp[ENV_H][ENV_W][3];
	gl3image_t *imgs[6];
	byte *pix[6];
	int iw[6], ih[6], nimg = 0, x, y, k, pass;
	qboolean any = false;

	liqenvmap = liqmapcount;
	memset(pix, 0, sizeof(pix));

	for (y = 0; y < ENV_H; y++)
	{
		for (x = 0; x < ENV_W; x++)
		{
			float sum[3] = {0, 0, 0};
			int n = 0, sy, sx;

			for (sy = 0; sy < 3; sy++)
			{
				for (sx = 0; sx < 3; sx++)
				{
					float az = ((x + (sx + 0.5f) / 3.0f) / ENV_W * 2 - 1) * (float)M_PI;
					float el = (y + (sy + 0.5f) / 3.0f) / ENV_H * (float)M_PI * 0.5f, s, t;
					vec3_t d = {cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el)};
					gl3image_t *im = GL3_SkyAt(d, &s, &t);

					if (!im)
					{
						continue;
					}

					for (k = 0; (k < nimg) && (imgs[k] != im); k++)
					{
					}

					if ((k == nimg) && (nimg < 6))
					{
						imgs[k] = im;
						iw[k] = ih[k] = 0;
						nimg++;
#ifndef YQ2_GL3_GLES3
						GL3_SelectTMU(GL_TEXTURE0);
						GL3_Bind(im->texnum);
						glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iw[k]);
						glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &ih[k]);

						if ((iw[k] > 0) && (ih[k] > 0) && (iw[k] <= 4096) && (ih[k] <= 4096) &&
							(pix[k] = malloc(iw[k] * ih[k] * 4)))
						{
							glPixelStorei(GL_PACK_ALIGNMENT, 1);
							glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix[k]);
							glPixelStorei(GL_PACK_ALIGNMENT, 4);
						}
#endif
					}

					if ((k < nimg) && pix[k])
					{
						int ix = Q_clamp((int)(s * (iw[k] - 1)), 0, iw[k] - 1);
						int iy = Q_clamp((int)(t * (ih[k] - 1)), 0, ih[k] - 1);
						const byte *c = pix[k] + (iy * iw[k] + ix) * 4;

						sum[0] += c[0];
						sum[1] += c[1];
						sum[2] += c[2];
						n++;
					}
				}
			}

			if (n)
			{
				any = true;
				acc[y][x][0] = sum[0] / n;
				acc[y][x][1] = sum[1] / n;
				acc[y][x][2] = sum[2] / n;
			}
			else
			{
				/* pale sky, a little lighter up high */
				acc[y][x][0] = 150 + 30.0f * y / ENV_H;
				acc[y][x][1] = 165 + 30.0f * y / ENV_H;
				acc[y][x][2] = 180 + 30.0f * y / ENV_H;
			}
		}
	}

	for (k = 0; k < nimg; k++)
	{
		free(pix[k]);
	}

	/* blurred: three 3x3 box passes (around in yaw, clamped at the horizon and the zenith) */
	for (pass = 0; pass < 3; pass++)
	{
		for (y = 0; y < ENV_H; y++)
		{
			for (x = 0; x < ENV_W; x++)
			{
				float s3[3] = {0, 0, 0};
				int dx, dy, c;

				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						int yy = Q_clamp(y + dy, 0, ENV_H - 1), xx = (x + dx + ENV_W) % ENV_W;

						for (c = 0; c < 3; c++)
						{
							s3[c] += acc[yy][xx][c];
						}
					}
				}

				for (c = 0; c < 3; c++)
				{
					tmp[y][x][c] = s3[c] / 9.0f;
				}
			}
		}

		memcpy(acc, tmp, sizeof(acc));
	}

	for (y = 0; y < ENV_H; y++)
	{
		for (x = 0; x < ENV_W; x++)
		{
			px[y][x][0] = (byte)Q_clamp((int)acc[y][x][0], 0, 255);
			px[y][x][1] = (byte)Q_clamp((int)acc[y][x][1], 0, 255);
			px[y][x][2] = (byte)Q_clamp((int)acc[y][x][2], 0, 255);
			px[y][x][3] = any ? 255 : 0;
		}
	}

	if (!liqenv)
	{
		glGenTextures(1, &liqenv);
	}

	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, liqenv);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ENV_W, ENV_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl3state.currenttexture = liqenv;
}

/* r_om_water_3d: the triangles of a level water face split along their longest edge down to 16 units within 1200
   units of the eye (the vertex shader fades the lift out by 1100: no cracks where the coarse faces begin); the new
   vertices take the mean of the two ends (texture coordinates, light, sky openness) */
#define SUBDIV_EDGE 16.0f
#define SUBDIV_NEAR 1200.0f
static gl3fxvtx_t subdivbuf[3 * 1024];
static int subdivn;

static void
SubdivFlush(void)
{
	if (subdivn)
	{
		GL3_FX_Add(subdivbuf, subdivn, GL_TRIANGLES);
		subdivn = 0;
	}
}

static void
SubdivMid(const gl3fxvtx_t *a, const gl3fxvtx_t *b, gl3fxvtx_t *m)
{
	int k;

	for (k = 0; k < 3; k++) m->pos[k] = 0.5f * (a->pos[k] + b->pos[k]);
	for (k = 0; k < 2; k++) m->st[k] = 0.5f * (a->st[k] + b->st[k]);
	for (k = 0; k < 4; k++) m->rgba[k] = 0.5f * (a->rgba[k] + b->rgba[k]);
	for (k = 0; k < 2; k++) m->aux[k] = 0.5f * (a->aux[k] + b->aux[k]);
}

static float
Dist2(const float *a, const float *b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];

	return dx * dx + dy * dy + dz * dz;
}

static void
SubdivTri(const gl3fxvtx_t *a, const gl3fxvtx_t *b, const gl3fxvtx_t *c, int depth)
{
	float ab = Dist2(a->pos, b->pos), bc = Dist2(b->pos, c->pos), ca = Dist2(c->pos, a->pos);
	float m = Q_max(ab, Q_max(bc, ca));
	float near = Q_min(Dist2(a->pos, r_newrefdef.vieworg), Q_min(Dist2(b->pos, r_newrefdef.vieworg),
		Dist2(c->pos, r_newrefdef.vieworg)));

	if ((depth < 12) && (m > SUBDIV_EDGE * SUBDIV_EDGE) && (near < (SUBDIV_NEAR + sqrtf(m)) * (SUBDIV_NEAR + sqrtf(m))))
	{
		gl3fxvtx_t mid;

		if (m == ab)
		{
			SubdivMid(a, b, &mid);
			SubdivTri(a, &mid, c, depth + 1);
			SubdivTri(&mid, b, c, depth + 1);
		}
		else if (m == bc)
		{
			SubdivMid(b, c, &mid);
			SubdivTri(a, b, &mid, depth + 1);
			SubdivTri(a, &mid, c, depth + 1);
		}
		else
		{
			SubdivMid(c, a, &mid);
			SubdivTri(a, b, &mid, depth + 1);
			SubdivTri(&mid, b, c, depth + 1);
		}

		return;
	}

	if (subdivn + 3 > (int)(sizeof(subdivbuf) / sizeof(subdivbuf[0])))
	{
		SubdivFlush();
	}

	subdivbuf[subdivn++] = *a;
	subdivbuf[subdivn++] = *b;
	subdivbuf[subdivn++] = *c;
}

/* the ripple tile for the traced reflection (gl3_post.c): its texture, 0 before the first water face; the time the
   water moves by; 1 when the third, finest layer is on (quality 2) */
GLuint
GL3_Liquid_Tile(float *time, float *fine)
{
	*time = GL3_GameTime();
	*fine = (r_om_water_quality && (r_om_water_quality->value >= 2)) ? 1.0f : 0.0f;
	return (r_om_water && r_om_water->value) ? liqtile : 0;
}

/* units 2 and 3 belong to the lightmaps and the post passes: what is bound there is put back after the water */
static GLint savedtex[2];

static void
BindWaterTextures(GLuint refl)
{
	GL3_SelectTMU(GL_TEXTURE2);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedtex[0]);
	glBindTexture(GL_TEXTURE_2D, liqtile);
	GL3_SelectTMU(GL_TEXTURE3);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedtex[1]);
	glBindTexture(GL_TEXTURE_2D, refl);
	GL3_SelectTMU(GL_TEXTURE0);
}

static void
RestoreWaterTextures(void)
{
	GL3_SelectTMU(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, (GLuint)savedtex[0]);
	GL3_SelectTMU(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_2D, (GLuint)savedtex[1]);
	GL3_SelectTMU(GL_TEXTURE0);
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
	qboolean water = false, level3d = false;
	GLuint refltex = 0;

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

	/* GFX-modern r_om_ssr: liquids are mirrors with a little roughness (the trace adds ripples); a translucent
	   one writes its depth too, so the reflection is traced from its surface and not from the bottom below */
	if (GL3_Post_SSRActive())
	{
		st.matrefl = 0.5f;
		st.matrough = 0.05f;

		if (translucent)
		{
			st.depthwrite = true;
		}

		sphere = false; /* the traced reflection replaces the sphere-mapped second pass */
	}

	/* the ripples lie in the horizontal plane: level surfaces only (no walls of falling water or steam sheets) */
	if (r_om_water && r_om_water->value && (fabsf(normal[2]) > 0.7f) && !image->is_lava &&
		!Q_strcasestr(image->name, "molten") && !Q_strcasestr(image->name, "lava") &&
		!Q_strcasestr(image->name, "steam"))
	{
		float pl[4] = {0, 0, 1, 0};
		qboolean level = (fabsf(normal[2]) > 0.99f) && surf->polys && (surf->polys->numverts >= 3);

		water = true;

		/* r_om_water_clear: solid level water goes to the sorted pass as see-through water (not inside the mirror
		   passes: they have no sorted pass) */
		if (!translucent && r_om_water_clear->value && (r_om_water_quality->value >= 2) && !GL3_Mirror_InPass() &&
			GL3_Post_DepthCopyPossible())
		{
			/* the world's entity is a local of GL3_DrawWorld (gone by the sorted pass): NULL for the world;
			   inline models keep theirs (r_newrefdef.entities, alive for the frame) */
			GL3_FX_AddAlphaSurface(surf, (ent && ent->model && (ent->model != (struct model_s *)gl3_worldmodel)) ?
				ent : NULL);
			return;
		}

		if (level)
		{
			vec3_t w0;

			Xform(&xf, surf->polys->vertices[0].pos, w0);
			pl[3] = w0[2];

			/* inside the water's own reflection pass: not drawn */
			if (GL3_WaterRefl_Skip(pl[3]))
			{
				return;
			}
		}

		if (!liqtile)
		{
			MakeTile();
		}

		if (!liqenv || (liqenvmap != liqmapcount))
		{
			MakeEnv();
		}

		refltex = liqenv;

		st.liq[0] = (r_om_water_quality->value >= 2) ? 2 : 1;
		st.liqrefl[3] = liqtile ? 1.0f : 0.0f;
		level3d = level && liqtile && (r_om_water_3d->value > 0);
		/* w: 1 = the tile, above 1 the height the vertex shader lifts the ripples by (+ 3 units per r_om_water_3d:
		   under a unit RMS: just a little) - only on the faces cut small */
		if (level3d)
		{
			st.liqrefl[3] = 1.0f + 3.0f * Q_clamp(r_om_water_3d->value, 0.0f, 4.0f);
		}

		/* quality 3: this face's plane for the next frame's reflection, the reflection of this frame */
		if (level && (r_om_water_quality->value >= 3))
		{
			vec3_t mins = {1e30f, 1e30f, 1e30f}, maxs = {-1e30f, -1e30f, -1e30f}, ctr = {0, 0, 0}, w, e1, e2, x;
			float area = 0, rect[2];
			int nv = 0;

			for (p = surf->polys; p; p = p->next)
			{
				int i, a;
				vec3_t first;

				for (i = 0; i < p->numverts; i++)
				{
					Xform(&xf, p->vertices[i].pos, w);

					for (a = 0; a < 3; a++)
					{
						mins[a] = Q_min(mins[a], w[a]);
						maxs[a] = Q_max(maxs[a], w[a]);
					}

					VectorAdd(ctr, w, ctr);
					nv++;

					if (i == 0)
					{
						VectorCopy(w, first);
					}
					else if (i >= 2)
					{
						vec3_t prev;

						Xform(&xf, p->vertices[i - 1].pos, prev);
						VectorSubtract(prev, first, e1);
						VectorSubtract(w, first, e2);
						CrossProduct(e1, e2, x);
						area += 0.5f * VectorLength(x);
					}
				}
			}

			if (nv)
			{
				VectorScale(ctr, 1.0f / nv, ctr);
				GL3_WaterRefl_Note(pl, mins, maxs, ctr, area);
			}

			if (GL3_WaterRefl_For(pl, &refltex, rect))
			{
				st.liqrefl[0] = 1;
				st.liqrefl[1] = rect[0];
				st.liqrefl[2] = rect[1];
				/* the planar reflection replaces the traced one and the sphere map */
				st.matrefl = 0;
				sphere = false;
			}
		}

		st.liq[1] = clock;
		/* r_om_water_glint -1: debug view of what the water reflects */
		st.liq[2] = (r_om_water_glint->value < 0) ? -1.0f : Q_clamp(r_om_water_glint->value, 0.0f, 4.0f);
		st.liq[3] = (GL3_Post_SSRActive() && !st.liqrefl[0]) ? 1.0f : 0.0f;

		/* translucent water: quality 2 over copies of the scene and its depth under it (refraction), else over the
		   depth copy of soft particles if there is one */
		if (translucent && (st.liq[0] >= 2) && GL3_Post_WaterCopy())
		{
			st.soft = 4;
		}
		else if (translucent && GL3_Post_DepthReady())
		{
			st.soft = 3;
		}
	}

	if (water)
	{
		BindWaterTextures(refltex);
	}

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
				v[i].aux[0] = water ? SkyOpen(w) : 0;
				v[i].aux[1] = 0;

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

			if (water && level3d && (pass == 0))
			{
				/* r_om_water_3d: the face cut into small triangles near the eye; the vertex shader lifts them by
				   the ripple tile's heights */
				for (i = 1; i + 1 < n; i++)
				{
					SubdivTri(&v[0], &v[i], &v[i + 1], 0);
				}

				SubdivFlush();
			}
			else
			{
				GL3_FX_Add(v, n, GL_TRIANGLE_FAN);
			}
		}
	}

	GL3_FX_Flush();

	if (water)
	{
		RestoreWaterTextures();
	}
}
