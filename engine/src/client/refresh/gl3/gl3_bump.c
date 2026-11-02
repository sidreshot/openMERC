/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GFX-modern r_om_bump: normal maps of the world textures, made from
 * the game's own images when they are loaded (no new data files).
 *
 * Per wall texture: luminance -> detail height (the luminance minus a wide
 * blur, so stains and large shading do not count, only grout, cracks, seams
 * and panels) -> light smoothing against single-texel grain -> contrast
 * normalised per texture (every texture gets comparable relief; nearly flat
 * textures are not amplified into noise) -> normals by central differences.
 * The map is RGBA8 with mipmaps: RGB = tangent-space normal (x along s, y
 * along t), A = the detail height (cavity term). Bound on GL_TEXTURE11 with
 * its texture; textures without a map get a flat 1x1 map.
 */

#include "header/local.h"

static GLuint *nmaps;   /* normal map by colour texture name */
static int nmapcount;
static GLuint nflat;

static float *
Bump_Blur(const float *src, int w, int h, int r)
{
	/* separable box blur with wrap-around (textures tile), twice for a tent shape */
	float *a = malloc(sizeof(float) * w * h), *b = malloc(sizeof(float) * w * h);
	int pass, x, y, k;

	if (!a || !b)
	{
		free(a);
		free(b);
		return NULL;
	}

	memcpy(a, src, sizeof(float) * w * h);

	for (pass = 0; pass < 2; pass++)
	{
		for (y = 0; y < h; y++)
		{
			const float *row = a + (size_t)y * w;
			float s = 0;

			for (k = -r; k <= r; k++)
			{
				s += row[((k % w) + w) % w];
			}

			for (x = 0; x < w; x++)
			{
				b[(size_t)y * w + x] = s / (2 * r + 1);
				s += row[(x + r + 1) % w] - row[((x - r) % w + w) % w];
			}
		}

		for (x = 0; x < w; x++)
		{
			float s = 0;

			for (k = -r; k <= r; k++)
			{
				s += b[(size_t)(((k % h) + h) % h) * w + x];
			}

			for (y = 0; y < h; y++)
			{
				a[(size_t)y * w + x] = s / (2 * r + 1);
				s += b[(size_t)((y + r + 1) % h) * w + x] - b[(size_t)(((y - r) % h + h) % h) * w + x];
			}
		}
	}

	free(b);
	return a;
}

/* one-axis box blur with wrap-around: axis 0 along x, 1 along y */
static float *
Bump_Blur1(const float *src, int w, int h, int r, int axis)
{
	float *o = malloc(sizeof(float) * w * h);
	int x, y, k;

	if (!o)
	{
		return NULL;
	}

	if (axis == 0)
	{
		for (y = 0; y < h; y++)
		{
			const float *row = src + (size_t)y * w;
			float s = 0;

			for (k = -r; k <= r; k++)
			{
				s += row[((k % w) + w) % w];
			}

			for (x = 0; x < w; x++)
			{
				o[(size_t)y * w + x] = s / (2 * r + 1);
				s += row[(x + r + 1) % w] - row[((x - r) % w + w) % w];
			}
		}
	}
	else
	{
		for (x = 0; x < w; x++)
		{
			float s = 0;

			for (k = -r; k <= r; k++)
			{
				s += src[(size_t)(((k % h) + h) % h) * w + x];
			}

			for (y = 0; y < h; y++)
			{
				o[(size_t)y * w + x] = s / (2 * r + 1);
				s += src[(size_t)((y + r + 1) % h) * w + x] - src[(size_t)(((y - r) % h + h) % h) * w + x];
			}
		}
	}

	return o;
}

static void
Bump_Store(GLuint texnum, GLuint nm)
{
	if ((int)texnum >= nmapcount)
	{
		int n = texnum + 1024;
		GLuint *p = realloc(nmaps, sizeof(GLuint) * n);

		if (!p)
		{
			glDeleteTextures(1, &nm);
			return;
		}

		memset(p + nmapcount, 0, sizeof(GLuint) * (n - nmapcount));
		nmaps = p;
		nmapcount = n;
	}

	if (nmaps[texnum])
	{
		glDeleteTextures(1, &nmaps[texnum]);
	}

	nmaps[texnum] = nm;
}

/* builds and uploads the normal map of a wall texture (rgba: w x h, 4 bytes per texel) */
void
GL3_Bump_Make(GLuint texnum, const char *name, const byte *rgba, int w, int h)
{
	/* pictures on the walls stay flat: paintings, posters, signs, maps, screens, vending fronts */
	static const char *flat[] = {"painting", "poster", "sign", "/1_map", "directory", "news_", "soda_", "vend",
		"screen", "static", "_cam", "whiteboard", "danger", "warning", "restroom", "elenumber", "fishtank", "meter",
		"button", "/med_", "light", NULL};
	float *L, *low, *H, *Hs, sum = 0, sum2 = 0, mean, sd, scale, strength = 0;
	byte *out;
	int x, y, step = 1, i, n;

	if (!texnum || !rgba || (w < 8) || (h < 8))
	{
		return;
	}

	if (name)
	{
		char lname[MAX_QPATH];

		Q_strlcpy(lname, name, sizeof(lname));
		Q_strlwr(lname);

		for (i = 0; flat[i]; i++)
		{
			if (strstr(lname, flat[i]))
			{
				return;
			}
		}
	}

	/* large replacement textures are measured at most 1024 wide */
	while ((w / step > 1024) || (h / step > 1024))
	{
		step *= 2;
	}

	w /= step;
	h /= step;
	n = w * h;
	L = malloc(sizeof(float) * n);
	out = malloc((size_t)n * 4);

	if (!L || !out)
	{
		free(L);
		free(out);
		return;
	}

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			float s = 0;
			int a, b;

			for (b = 0; b < step; b++)
			{
				for (a = 0; a < step; a++)
				{
					const byte *p = rgba + ((size_t)(y * step + b) * w * step + (x * step + a)) * 4;
					s += 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
				}
			}

			L[y * w + x] = s / (255.0f * step * step);
		}
	}

	/* detail height from the lines of the texture. Grout, mortar, seams and panel edges are long straight lines
	   along the texture axes and are grooves whether darker or lighter than their surroundings (light mortar is
	   recessed too). Line response across one axis (|L - its blur across x|, resp. y) is averaged over a long
	   stretch along the other axis: straight lines keep their full response, stains and grain average out. A soft
	   threshold at the mean response keeps what is left inside a tile or brick flat. */
	{
		int rn = (w > h ? w : h) / 96 + 1, rl = (w > h ? w : h) / 24 + 2;
		float *bx = Bump_Blur1(L, w, h, rn, 0), *by = Bump_Blur1(L, w, h, rn, 1);
		float *dx = NULL, *dy = NULL, *rv = NULL, *rh = NULL, mr = 0;

		low = NULL;
		H = malloc(sizeof(float) * n);
		dx = malloc(sizeof(float) * n);
		dy = malloc(sizeof(float) * n);

		if (bx && by && dx && dy && H)
		{
			for (i = 0; i < n; i++)
			{
				dx[i] = fabsf(L[i] - bx[i]); /* vertical lines */
				dy[i] = fabsf(L[i] - by[i]); /* horizontal lines */
			}

			rv = Bump_Blur1(dx, w, h, rl, 1); /* along a vertical line */
			rh = Bump_Blur1(dy, w, h, rl, 0); /* along a horizontal line */
		}

		if (!rv || !rh)
		{
			free(bx);
			free(by);
			free(dx);
			free(dy);
			free(rv);
			free(rh);
			free(L);
			free(H);
			free(out);
			return;
		}

		for (i = 0; i < n; i++)
		{
			H[i] = rv[i] > rh[i] ? rv[i] : rh[i];
			mr += H[i];
		}

		mr /= n;

		/* how strongly the texture is built of lines: their spread against the mean response. Only clear joints
		   become grooves (threshold above the mean); a texture without them (plaster, stains) stays flat. */
		{
			float s2 = 0, sr, th;

			for (i = 0; i < n; i++)
			{
				s2 += (H[i] - mr) * (H[i] - mr);
			}

			sr = sqrtf(s2 / n);
			strength = sr / fmaxf(mr, 1.0e-4f);
			th = mr + 0.75f * sr;

			for (i = 0; i < n; i++)
			{
				float d = H[i] - th;
				H[i] = -(d > 0 ? d : 0);
			}

			R_Printf(PRINT_DEVELOPER, "OMBUMP %s %dx%d lines %.2f mean %.4f\n", name ? name : "?", w, h, strength, mr);
		}

		/* no clear joints (plaster, smooth metal, plain stone; strength < 0.35 on jpn1) or nearly one colour:
		   relief would only be noise */
		if ((strength < 0.35f) || (mr < 0.004f))
		{
			free(bx);
			free(by);
			free(dx);
			free(dy);
			free(rv);
			free(rh);
			free(L);
			free(H);
			free(out);
			GL3_Bump_Free(texnum);
			return;
		}

		free(bx);
		free(by);
		free(dx);
		free(dy);
		free(rv);
		free(rh);
	}

	Hs = Bump_Blur(H, w, h, (w > h ? w : h) >= 512 ? 2 : 1);

	if (!Hs)
	{
		free(L);
		free(low);
		free(H);
		free(out);
		return;
	}

	/* contrast: about +-1 at 2.5 standard deviations; textures with little detail stay shallow */
	for (i = 0; i < n; i++)
	{
		sum += Hs[i];
		sum2 += Hs[i] * Hs[i];
	}

	mean = sum / n;
	sd = sqrtf(fmaxf(sum2 / n - mean * mean, 0.0f));
	scale = 1.0f / (2.5f * fmaxf(sd, 0.015f));

	for (i = 0; i < n; i++)
	{
		float v = (Hs[i] - mean) * scale;
		Hs[i] = v < -1 ? -1 : (v > 1 ? 1 : v);
	}

	/* normals: a height of +-1 over a texture 32 texels wide makes a 45 degree slope at the steepest */
	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			float dx = (Hs[y * w + (x + 1) % w] - Hs[y * w + (x + w - 1) % w]) * 0.5f;
			float dy = (Hs[((y + 1) % h) * w + x] - Hs[((y + h - 1) % h) * w + x]) * 0.5f;
			float k = (float)(w > h ? w : h) / 80.0f;
			float nx = -dx * k, ny = -dy * k, nz = 1.0f, l = sqrtf(nx * nx + ny * ny + nz * nz);
			byte *o = out + (size_t)(y * w + x) * 4;

			o[0] = (byte)(127.5f + 127.0f * nx / l);
			o[1] = (byte)(127.5f + 127.0f * ny / l);
			o[2] = (byte)(127.5f + 127.0f * nz / l);
			o[3] = (byte)(127.5f + 127.0f * Hs[y * w + x]);
		}
	}

	{
		GLuint nm;

		glGenTextures(1, &nm);
		GL3_SelectTMU(GL_TEXTURE11);
		glBindTexture(GL_TEXTURE_2D, nm);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		Bump_Store(texnum, nm);
		GL3_SelectTMU(GL_TEXTURE0);
	}

	free(L);
	free(low);
	free(H);
	free(Hs);
	free(out);
}

/* the colour texture is deleted: its normal map goes too */
void
GL3_Bump_Free(GLuint texnum)
{
	if (((int)texnum < nmapcount) && nmaps[texnum])
	{
		glDeleteTextures(1, &nmaps[texnum]);
		nmaps[texnum] = 0;
	}
}

/* binds the normal map of a colour texture (or a flat one) to texture unit 11 */
void
GL3_Bump_Bind(GLuint texnum)
{
	if (!nflat)
	{
		static const byte flat[4] = {128, 128, 255, 128};

		glGenTextures(1, &nflat);
		GL3_SelectTMU(GL_TEXTURE11);
		glBindTexture(GL_TEXTURE_2D, nflat);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	GL3_SelectTMU(GL_TEXTURE11);
	glBindTexture(GL_TEXTURE_2D, (((int)texnum < nmapcount) && nmaps[texnum]) ? nmaps[texnum] : nflat);
	GL3_SelectTMU(GL_TEXTURE0);
}

/* ---------------------------------------------------------------------
 * the map's lamps for per-pixel relief and highlights (lump 20)
 * --------------------------------------------------------------------- */

#define BUMP_MAX_VIEW_LIGHTS 8

typedef struct
{
	vec3_t origin;
	vec3_t color;
	float radius;
} bumplight_t;

static bumplight_t *blights;
static int numblights;

/* the static lights of the new world: origin, colour, intensity, type (0 surface patch, 1 point, 2 spot, 3 sky) */
void
GL3_Bump_SetLights(const byte *lights, int len)
{
	int i, n = len / 36;

	free(blights);
	blights = NULL;
	numblights = 0;

	if (n <= 0)
	{
		return;
	}

	blights = malloc(sizeof(bumplight_t) * n);

	if (!blights)
	{
		return;
	}

	for (i = 0; i < n; i++)
	{
		const byte *r = lights + 36 * i;
		float intensity = LittleFloat(*(const float *)(r + 24));
		int type = LittleLong(*(const int *)(r + 28));
		bumplight_t *l = &blights[numblights];
		float m;

		if ((intensity <= 0) || (type == 3))
		{
			continue;
		}

		l->origin[0] = LittleFloat(*(const float *)(r + 0));
		l->origin[1] = LittleFloat(*(const float *)(r + 4));
		l->origin[2] = LittleFloat(*(const float *)(r + 8));
		l->color[0] = LittleFloat(*(const float *)(r + 12));
		l->color[1] = LittleFloat(*(const float *)(r + 16));
		l->color[2] = LittleFloat(*(const float *)(r + 20));
		m = fmaxf(l->color[0], fmaxf(l->color[1], l->color[2]));

		if (m > 0)
		{
			VectorScale(l->color, 1.0f / m, l->color);
		}

		/* reach: points their intensity, surface patches more (their light falls off as 1/d) */
		l->radius = (type == 0) ? fminf(sqrtf(intensity) * 8.0f, 600.0f) : fminf(intensity, 800.0f);
		numblights++;
	}
}

/* the lamps nearest in influence to the eye go to the shader: omBL[i] = origin, radius; omBC[i] = colour */
void
GL3_Bump_ViewLights(GLuint prog, const float *eye)
{
	float pos[BUMP_MAX_VIEW_LIGHTS * 4], col[BUMP_MAX_VIEW_LIGHTS * 4], score[BUMP_MAX_VIEW_LIGHTS];
	int i, j, n = 0;
	GLint loc;

	for (i = 0; i < numblights; i++)
	{
		const bumplight_t *l = &blights[i];
		vec3_t d;
		float dist, s;

		VectorSubtract(l->origin, eye, d);
		dist = VectorLength(d);

		if (dist > l->radius + 700.0f)
		{
			continue;
		}

		s = l->radius / (dist + 64.0f);

		/* insertion into the best BUMP_MAX_VIEW_LIGHTS by score */
		for (j = n; j > 0 && score[j - 1] < s; j--)
		{
			if (j < BUMP_MAX_VIEW_LIGHTS)
			{
				score[j] = score[j - 1];
				memcpy(pos + j * 4, pos + (j - 1) * 4, sizeof(float) * 4);
				memcpy(col + j * 4, col + (j - 1) * 4, sizeof(float) * 4);
			}
		}

		if (j >= BUMP_MAX_VIEW_LIGHTS)
		{
			continue;
		}

		score[j] = s;
		pos[j * 4 + 0] = l->origin[0];
		pos[j * 4 + 1] = l->origin[1];
		pos[j * 4 + 2] = l->origin[2];
		pos[j * 4 + 3] = l->radius;
		col[j * 4 + 0] = l->color[0];
		col[j * 4 + 1] = l->color[1];
		col[j * 4 + 2] = l->color[2];
		col[j * 4 + 3] = 1.0f;

		if (n < BUMP_MAX_VIEW_LIGHTS)
		{
			n++;
		}
	}

	loc = glGetUniformLocation(prog, "omBL");
	if (loc != -1 && n > 0)
	{
		glUniform4fv(loc, n, pos);
	}

	loc = glGetUniformLocation(prog, "omBC");
	if (loc != -1 && n > 0)
	{
		glUniform4fv(loc, n, col);
	}

	loc = glGetUniformLocation(prog, "omBN");
	if (loc != -1)
	{
		glUniform1i(loc, n);
	}
}

