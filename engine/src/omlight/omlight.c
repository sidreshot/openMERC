/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * omlight - re-bakes the lightmaps of a Soldier of Fortune map (IBSP 46, SPEC-101) on the player's machine
 * from the player's own game data, for the GL3 renderer's r_om_relight (gl3_relight.c). Nothing of the
 * game is shipped: the result is a cache file next to the player's settings.
 *
 * Pipeline:
 *   A. per lightmap sample of the original grid: terms of the map compiler's light model - point lights
 *      and spotlights (linear falloff, _fade/_wait, _cone/_focus), surface-light patches (1/d), the sun
 *      (direction fitted against the lightmap) and the sky dome - traced through the BSP
 *   fit 1: least squares of the terms (+ a constant) against the map's own lightmap: every map calibrates
 *      itself, so its brightness and colours stay as the mapper left them
 *   B. gathered bounces of the fit-1 direct light times the texture colours; fit 2 without the constant
 *   C. the final pass on the original grid: 4x4 supersampling, soft shadows (light radius, sun size)
 * Lamp faces (SURF_LIGHT) keep the compiler's emission colour, switchable lights (targetname, styles 32+)
 * and brush-model faces keep their original lightmaps; only style 0 of world faces is replaced.
 *
 * usage: omlight -map maps/<name>.bsp -base <game base dir with the paks> -out <file.oml> [options]
 *   -base may be given more than once (base, then the mod directories: later paks override earlier ones).
 *   -map may also be a .bsp file. Options (tests): -ls u (high-resolution grid, u units; -1 = original),
 *   -ss n, -lr r, -sunang deg, -sky n, -bounce n, -bounces n, -cap v, -debug prefix (analysis files),
 *   -outbsp file (a patched copy of the map), -threads n.
 * Output .oml: "OMLT", int version 1, int lighting length, uint32 FNV-1a 64 of the original lighting lump
 * (low, high), then the new lighting lump of the same length. Written to <out>.tmp and renamed.
 */

#include <math.h>
#include <omp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define CONTENTS_SOLID 1
#define SURF_LIGHT 0x1
#define SURF_SKY 0x4
#define NT 6 /* terms: point, surf, sun, sky, spot, bounce */
#define ND 5 /* direct terms (all but bounce) */
#define TB 5 /* index of the bounce term */

typedef struct { float n[3], d; } plane_t;
typedef struct { int plane, child[2]; } node_t;
typedef struct { int contents, cluster; } leaf_t;
typedef struct { float vecs[2][4]; int flags; char tex[33]; } texinfo_t;
typedef struct
{
	int plane, side, firstedge, numedges, texinfo, lightofs, lmw, lmh, scs, sct, mins[2], ext[2], nstyles;
	float normal[3], dist;
	int nv;
	float *poly; /* nv * 3 */
	float refl[3];
	/* native grid */
	int lofs; /* offset of the face's samples in the native arrays (-1: not baked) */
} face_t;
typedef struct { float o[3], color[3], intensity, fade, n[3]; int cluster; int spot; float dir[3], stopdot, focus; } light_t;

static unsigned char *bspdata;
static long bsplen;
static plane_t *planes;
static node_t *nodes;
static leaf_t *leafs;
static texinfo_t *texinfos;
static face_t *faces;
static float *verts;
static int nplanes, nnodes, nleafs, ntex, nfaces, nverts, headnode, wfirst, wnum;
static int *edges, *surfedges;
static unsigned char *lighting;
static int nclusters, rowbytes;
static unsigned char *pvs; /* nclusters * rowbytes */
static light_t *points, *surfs;
static int npoints, nsurfs, nswitch;
static const char *opt_outbsp;
static float sundir[3], suncolor[3] = {1, 1, 1};
static int **skyplane; /* per plane: list of sky face indices (count first) */
static int **litplane; /* per plane: list of baked face indices */

/* settings */
static int opt_opaque = CONTENTS_SOLID, opt_bmodels = 0;
static float opt_conescale = 1, opt_focusscale = 1, opt_surfk = 0.5f;
typedef struct { int head; float mins[3], maxs[3]; } smodel_t;
static smodel_t smodels[1024];
static int nsmodels;
static float opt_ls = 0, opt_lr = 0, opt_sunang = 0, opt_cap = 196;
static int opt_ss = 1, opt_sky = 24, opt_bounce = 32, opt_bounces = 2, opt_surfmode = 3;

/* ---------------- small helpers ---------------- */

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void cross3(const float *a, const float *b, float *o)
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}

static void norm3(float *v)
{
	float l = sqrtf(dot3(v, v));
	if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

static uint32_t hash32(uint32_t x)
{
	x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
	return x;
}

static float rnd(uint32_t *s)
{
	*s = hash32(*s + 0x9e3779b9u);
	return (*s >> 8) * (1.0f / 16777216.0f);
}

static void *xmalloc(size_t n)
{
	void *p = calloc(1, n ? n : 1);
	if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
	return p;
}

static int lump(int i, int *len)
{
	int ofs = *(int *)(bspdata + 8 + i * 8);
	*len = *(int *)(bspdata + 8 + i * 8 + 4);
	return ofs;
}

/* ---------------- pak / m32 ---------------- */

static int pak_find(const char *paklist, const char *name, unsigned char **out, int *outlen)
{
	char list[4096], *p, *next;
	int found = 0;
	strncpy(list, paklist, sizeof(list) - 1);
	list[sizeof(list) - 1] = 0;
	for (p = list; p && *p; p = next)
	{
		FILE *f;
		char hdr[4];
		int dofs, dlen, i;
		next = strchr(p, ';');
		if (next) *next++ = 0;
		f = fopen(p, "rb");
		if (!f) continue;
		if (fread(hdr, 1, 4, f) == 4 && !memcmp(hdr, "PACK", 4) && fread(&dofs, 4, 1, f) && fread(&dlen, 4, 1, f))
		{
			unsigned char *dir = xmalloc(dlen);
			fseek(f, dofs, SEEK_SET);
			fread(dir, 1, dlen, f);
			for (i = 0; i + 64 <= dlen; i += 64)
			{
				if (!_stricmp((char *)dir + i, name))
				{
					int o = *(int *)(dir + i + 56), l = *(int *)(dir + i + 60);
					if (found) free(*out);
					*out = xmalloc(l);
					fseek(f, o, SEEK_SET);
					fread(*out, 1, l, f);
					*outlen = l;
					found = 1; /* later paks override */
				}
			}
			free(dir);
		}
		fclose(f);
	}
	return found;
}

static void texture_reflectivity(const char *paks, const char *tex, float *out)
{
	char name[128];
	unsigned char *d = NULL;
	int len = 0;
	out[0] = out[1] = out[2] = 0.5f;
	snprintf(name, sizeof(name), "textures/%s.m32", tex);
	if (pak_find(paks, name, &d, &len) && len >= 968)
	{
		unsigned w = *(unsigned *)(d + 0x204), h = *(unsigned *)(d + 0x244), o = *(unsigned *)(d + 0x284);
		if (w && h && (size_t)o + (size_t)w * h * 4 <= (size_t)len)
		{
			double s[3] = {0, 0, 0};
			size_t i, n = (size_t)w * h;
			for (i = 0; i < n; i++)
			{
				s[0] += d[o + i * 4]; s[1] += d[o + i * 4 + 1]; s[2] += d[o + i * 4 + 2];
			}
			out[0] = (float)(s[0] / n / 255); out[1] = (float)(s[1] / n / 255); out[2] = (float)(s[2] / n / 255);
		}
	}
	free(d);
}

/* ---------------- BSP ---------------- */

static int point_leaf(const float *p)
{
	int n = headnode;
	while (n >= 0)
	{
		const plane_t *pl = &planes[nodes[n].plane];
		n = nodes[n].child[(dot3(p, pl->n) - pl->d) >= 0 ? 0 : 1];
	}
	return -n - 1;
}

/* first solid along p->q: returns 1 and the hit fraction / node of the hit plane */
static int trace_r(int n, float f1, float f2, const float *p, const float *q, int split, float *hf, int *hn)
{
	const plane_t *pl;
	float t1, t2, fr, m[3], fm;
	int nearc, farc;
	if (n < 0)
	{
		if (leafs[-n - 1].contents & opt_opaque) { *hf = f1; *hn = split; return 1; }
		return 0;
	}
	pl = &planes[nodes[n].plane];
	t1 = dot3(p, pl->n) - pl->d;
	t2 = dot3(q, pl->n) - pl->d;
	if (t1 >= 0 && t2 >= 0) return trace_r(nodes[n].child[0], f1, f2, p, q, split, hf, hn);
	if (t1 < 0 && t2 < 0) return trace_r(nodes[n].child[1], f1, f2, p, q, split, hf, hn);
	fr = t1 / (t1 - t2);
	m[0] = p[0] + (q[0] - p[0]) * fr; m[1] = p[1] + (q[1] - p[1]) * fr; m[2] = p[2] + (q[2] - p[2]) * fr;
	fm = f1 + (f2 - f1) * fr;
	nearc = nodes[n].child[t1 >= 0 ? 0 : 1];
	farc = nodes[n].child[t1 >= 0 ? 1 : 0];
	if (trace_r(nearc, f1, fm, p, m, split, hf, hn)) return 1;
	return trace_r(farc, fm, f2, m, q, n, hf, hn);
}

/* does segment p->q touch the box (slab test)? */
static int seg_box(const float *p, const float *q, const float *mn, const float *mx)
{
	float t0 = 0, t1 = 1;
	int k;
	for (k = 0; k < 3; k++)
	{
		float d = q[k] - p[k];
		if (fabsf(d) < 1e-6f) { if (p[k] < mn[k] || p[k] > mx[k]) return 0; }
		else
		{
			float a = (mn[k] - p[k]) / d, b = (mx[k] - p[k]) / d;
			if (a > b) { float t = a; a = b; b = t; }
			if (a > t0) t0 = a;
			if (b < t1) t1 = b;
			if (t0 > t1) return 0;
		}
	}
	return 1;
}

/* first hit fraction of the shadow-casting brush models along p->q (2 if none) */
static float bmodel_hit(const float *p, const float *q)
{
	float best = 2, hf;
	int i, hn;
	for (i = 0; i < nsmodels; i++)
		if (seg_box(p, q, smodels[i].mins, smodels[i].maxs) && trace_r(smodels[i].head, 0, 1, p, q, -1, &hf, &hn) && hf < best)
			best = hf;
	return best;
}

static int blocked(const float *p, const float *q)
{
	float hf; int hn;
	if (trace_r(headnode, 0, 1, p, q, -1, &hf, &hn)) return 1;
	return nsmodels && bmodel_hit(p, q) <= 1;
}

static int in_poly(const face_t *f, const float *h)
{
	float mn = 1e9f, mx = -1e9f;
	int i;
	for (i = 0; i < f->nv; i++)
	{
		const float *a = f->poly + i * 3, *b = f->poly + ((i + 1) % f->nv) * 3;
		float e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, c[3], v[3] = {h[0] - a[0], h[1] - a[1], h[2] - a[2]}, s;
		cross3(f->normal, e, c);
		s = dot3(c, v) / (sqrtf(dot3(e, e)) + 1e-6f);
		if (s < mn) mn = s;
		if (s > mx) mx = s;
	}
	return mn >= -1.0f || mx <= 1.0f;
}

/* ray p + d*16384: first face hit on the plane of the hit node, among list (sky faces or lit faces) */
static int ray_face(const float *p, const float *d, int **byplane, int needfacing, float *hit)
{
	float q[3] = {p[0] + d[0] * 16384, p[1] + d[1] * 16384, p[2] + d[2] * 16384}, hf;
	int hn, *lst, i;
	if (!trace_r(headnode, 0, 1, p, q, -1, &hf, &hn) || hn < 0) return -1;
	if (nsmodels && bmodel_hit(p, q) < hf) return -1; /* a brush model is in front */
	lst = byplane[nodes[hn].plane];
	if (!lst) return -1;
	hit[0] = p[0] + (q[0] - p[0]) * hf; hit[1] = p[1] + (q[1] - p[1]) * hf; hit[2] = p[2] + (q[2] - p[2]) * hf;
	for (i = 1; i <= lst[0]; i++)
	{
		face_t *f = &faces[lst[i]];
		if (needfacing && dot3(d, f->normal) >= 0) continue;
		if (in_poly(f, hit)) return lst[i];
	}
	return -1;
}

static int pvs_vis(int a, int b)
{
	if (a < 0 || b < 0 || !pvs) return 1;
	return (pvs[a * rowbytes + (b >> 3)] >> (b & 7)) & 1;
}

static void face_point(const face_t *f, float s, float t, float *out)
{
	/* solve vs.p = s - vs3, vt.p = t - vt3, n.p = dist */
	const texinfo_t *ti = &texinfos[f->texinfo];
	const float *a = ti->vecs[0], *b = ti->vecs[1], *c = f->normal;
	float r[3] = {s - a[3], t - b[3], f->dist};
	float det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0]);
	float i00 = (b[1] * c[2] - b[2] * c[1]) / det, i01 = -(a[1] * c[2] - a[2] * c[1]) / det, i02 = (a[1] * b[2] - a[2] * b[1]) / det;
	float i10 = -(b[0] * c[2] - b[2] * c[0]) / det, i11 = (a[0] * c[2] - a[2] * c[0]) / det, i12 = -(a[0] * b[2] - a[2] * b[0]) / det;
	float i20 = (b[0] * c[1] - b[1] * c[0]) / det, i21 = -(a[0] * c[1] - a[1] * c[0]) / det, i22 = (a[0] * b[1] - a[1] * b[0]) / det;
	out[0] = i00 * r[0] + i01 * r[1] + i02 * r[2];
	out[1] = i10 * r[0] + i11 * r[1] + i12 * r[2];
	out[2] = i20 * r[0] + i21 * r[1] + i22 * r[2];
}

/* ---------------- entities ---------------- */

typedef struct
{
	char cls[64], target[64], targetname[64], sunv[64], suna[64], sunc[64];
	float origin[3], fade, cone, focus, mangle[2], angle;
	int hasorigin, hasmangle, hasangle, model;
} ent_t;

static int parse_entities(const char *txt, ent_t **out)
{
	int n = 0, cap = 256, inq = 0, iskey = 1;
	char key[64] = "", tok[256];
	int tl = 0;
	ent_t *e = xmalloc(sizeof(ent_t) * cap), *cur = NULL;
	for (; *txt; txt++)
	{
		char c = *txt;
		if (inq)
		{
			if (c == '"')
			{
				tok[tl] = 0;
				inq = 0;
				if (iskey) strncpy(key, tok, 63);
				else if (cur)
				{
					if (!strcmp(key, "classname")) strncpy(cur->cls, tok, 63);
					else if (!strcmp(key, "origin")) cur->hasorigin = sscanf(tok, "%f %f %f", &cur->origin[0], &cur->origin[1], &cur->origin[2]) == 3;
					else if (!strcmp(key, "_fade") || !strcmp(key, "_wait")) { cur->fade = (float)atof(tok); if (cur->fade <= 0) cur->fade = 1; }
					else if (!strcmp(key, "_sun_vector")) strncpy(cur->sunv, tok, 63);
					else if (!strcmp(key, "_sun_angle")) strncpy(cur->suna, tok, 63);
					else if (!strcmp(key, "_sun_color")) strncpy(cur->sunc, tok, 63);
					else if (!strcmp(key, "target")) strncpy(cur->target, tok, 63);
					else if (!strcmp(key, "targetname")) strncpy(cur->targetname, tok, 63);
					else if (!strcmp(key, "_cone")) cur->cone = (float)atof(tok);
					else if (!strcmp(key, "_focus")) cur->focus = (float)atof(tok);
					else if (!strcmp(key, "_mangle")) cur->hasmangle = sscanf(tok, "%f %f", &cur->mangle[0], &cur->mangle[1]) >= 1;
					else if (!strcmp(key, "angle")) { cur->angle = (float)atof(tok); cur->hasangle = 1; }
					else if (!strcmp(key, "model") && tok[0] == '*') cur->model = atoi(tok + 1);
				}
				iskey = !iskey;
			}
			else if (tl < 255) tok[tl++] = c;
		}
		else if (c == '"') { inq = 1; tl = 0; }
		else if (c == '{')
		{
			if (n == cap) { cap *= 2; e = realloc(e, sizeof(ent_t) * cap); }
			cur = &e[n++];
			memset(cur, 0, sizeof(*cur));
			cur->fade = 1;
			iskey = 1;
		}
		else if (c == '}') cur = NULL;
	}
	*out = e;
	return n;
}

/* ---------------- loading ---------------- */

/* the paks of a game directory in the engine's order (pak0..pak9, then the others alphabetically): a later
   one overrides an earlier one, as in pak_find */
static void list_paks(const char *dir, char *out, size_t outsize)
{
	char names[256][260];
	int n = 0, i, j;
	out[0] = 0;
	for (i = 0; i < 10; i++)
	{
		char p[1024];
		FILE *f;
		snprintf(p, sizeof(p), "%s/pak%d.pak", dir, i);
		if ((f = fopen(p, "rb")) != NULL)
		{
			fclose(f);
			if (out[0]) strncat(out, ";", outsize - strlen(out) - 1);
			strncat(out, p, outsize - strlen(out) - 1);
		}
	}
#ifdef _WIN32
	{
		struct _finddata_t fd;
		char pat[1024];
		intptr_t h;
		snprintf(pat, sizeof(pat), "%s/*.pak", dir);
		if ((h = _findfirst(pat, &fd)) != -1)
		{
			do
				if (n < 256) strncpy(names[n++], fd.name, 259);
			while (_findnext(h, &fd) == 0);
			_findclose(h);
		}
	}
#else
	{
		DIR *d = opendir(dir);
		struct dirent *e;
		while (d && (e = readdir(d)) != NULL)
		{
			size_t l = strlen(e->d_name);
			if (l > 4 && !strcasecmp(e->d_name + l - 4, ".pak") && n < 256) strncpy(names[n++], e->d_name, 259);
		}
		if (d) closedir(d);
	}
#endif
	for (i = 0; i < n; i++)
		for (j = i + 1; j < n; j++)
			if (strcmp(names[j], names[i]) < 0)
			{
				char t[260];
				memcpy(t, names[i], 260); memcpy(names[i], names[j], 260); memcpy(names[j], t, 260);
			}
	for (i = 0; i < n; i++)
	{
		char p[1024];
		int numbered = (!_strnicmp(names[i], "pak", 3) && names[i][3] >= '0' && names[i][3] <= '9' && !_stricmp(names[i] + 4, ".pak"));
		if (numbered) continue;
		snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
		if (out[0]) strncat(out, ";", outsize - strlen(out) - 1);
		strncat(out, p, outsize - strlen(out) - 1);
	}
}

static uint64_t fnv64(const unsigned char *p, size_t n)
{
	uint64_t h = 1469598103934665603ULL;
	size_t i;
	for (i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
	return h;
}

/* map: a .bsp file, or a name inside the paks ("maps/tsr1.bsp") */
static void load_bsp(const char *map, const char *paks)
{
	FILE *f = fopen(map, "rb");
	int o, l, i, j;
	ent_t *ents;
	int nents;
	if (f)
	{
		long sz;
		fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
		bspdata = xmalloc(sz);
		bsplen = sz;
		fread(bspdata, 1, sz, f);
		fclose(f);
	}
	else
	{
		int len = 0;
		if (!pak_find(paks, map, &bspdata, &len)) { fprintf(stderr, "cannot find %s\n", map); exit(1); }
		bsplen = len;
	}
	if (bsplen < 8 + 22 * 8 || memcmp(bspdata, "IBSP", 4) || *(int *)(bspdata + 4) != 46) { fprintf(stderr, "not IBSP 46\n"); exit(1); }

	o = lump(1, &l); nplanes = l / 20; planes = xmalloc(sizeof(plane_t) * nplanes);
	for (i = 0; i < nplanes; i++) memcpy(&planes[i], bspdata + o + i * 20, 16);
	o = lump(2, &l); nverts = l / 12; verts = (float *)(bspdata + o);
	o = lump(4, &l); nnodes = l / 28; nodes = xmalloc(sizeof(node_t) * nnodes);
	for (i = 0; i < nnodes; i++) memcpy(&nodes[i], bspdata + o + i * 28, 12);
	o = lump(8, &l); nleafs = l / 32; leafs = xmalloc(sizeof(leaf_t) * nleafs);
	for (i = 0; i < nleafs; i++)
	{
		leafs[i].contents = *(int *)(bspdata + o + i * 32);
		leafs[i].cluster = *(short *)(bspdata + o + i * 32 + 4);
	}
	o = lump(5, &l); ntex = l / 76; texinfos = xmalloc(sizeof(texinfo_t) * ntex);
	for (i = 0; i < ntex; i++)
	{
		memcpy(texinfos[i].vecs, bspdata + o + i * 76, 32);
		texinfos[i].flags = *(int *)(bspdata + o + i * 76 + 32);
		memcpy(texinfos[i].tex, bspdata + o + i * 76 + 40, 32);
	}
	o = lump(11, &l); edges = xmalloc(sizeof(int) * 2 * (l / 4));
	for (i = 0; i < l / 4; i++)
	{
		edges[i * 2] = *(unsigned short *)(bspdata + o + i * 4);
		edges[i * 2 + 1] = *(unsigned short *)(bspdata + o + i * 4 + 2);
	}
	o = lump(12, &l); surfedges = (int *)(bspdata + o);
	o = lump(7, &l); lighting = bspdata + o;
	o = lump(13, &l);
	headnode = *(int *)(bspdata + o + 36);
	wfirst = *(int *)(bspdata + o + 40);
	wnum = *(int *)(bspdata + o + 44);

	o = lump(6, &l); nfaces = l / 44; faces = xmalloc(sizeof(face_t) * nfaces);
	for (i = 0; i < nfaces; i++)
	{
		unsigned char *r = bspdata + o + i * 44;
		face_t *fc = &faces[i];
		plane_t *pl;
		fc->plane = *(unsigned short *)r;
		fc->side = *(short *)(r + 2);
		fc->firstedge = *(int *)(r + 4);
		fc->numedges = *(short *)(r + 8);
		fc->texinfo = *(short *)(r + 10);
		for (j = 0; j < 4; j++) fc->nstyles += r[22 + j] != 255;
		fc->lightofs = *(int *)(r + 28);
		fc->lmw = r[32]; fc->lmh = r[33]; fc->scs = r[34]; fc->sct = r[35];
		fc->mins[0] = *(short *)(r + 36); fc->mins[1] = *(short *)(r + 38);
		fc->ext[0] = *(short *)(r + 40); fc->ext[1] = *(short *)(r + 42);
		if (!fc->lmw || !fc->lmh) fc->nstyles = 0;
		pl = &planes[fc->plane];
		for (j = 0; j < 3; j++) fc->normal[j] = fc->side ? -pl->n[j] : pl->n[j];
		fc->dist = fc->side ? -pl->d : pl->d;
		fc->nv = fc->numedges;
		fc->poly = xmalloc(sizeof(float) * 3 * fc->nv);
		for (j = 0; j < fc->nv; j++)
		{
			int se = surfedges[fc->firstedge + j];
			int v = se >= 0 ? edges[se * 2] : edges[-se * 2 + 1];
			memcpy(fc->poly + j * 3, verts + v * 3, 12);
		}
		fc->lofs = -1;
	}

	o = lump(3, &l);
	if (l >= 4)
	{
		nclusters = *(int *)(bspdata + o);
		rowbytes = (nclusters + 7) / 8;
		pvs = xmalloc((size_t)nclusters * rowbytes);
		for (i = 0; i < nclusters; i++)
		{
			unsigned char *v = bspdata + o + *(int *)(bspdata + o + 4 + i * 8), *row = pvs + (size_t)i * rowbytes;
			int out = 0;
			while (out < rowbytes)
			{
				if (*v) { row[out++] = *v++; }
				else { out += v[1]; v += 2; }
			}
		}
	}

	/* sky and lit faces by plane */
	skyplane = xmalloc(sizeof(int *) * nplanes);
	litplane = xmalloc(sizeof(int *) * nplanes);
	for (i = 0; i < nfaces; i++)
	{
		int **t;
		if (texinfos[faces[i].texinfo].flags & SURF_SKY) t = skyplane;
		else if (i >= wfirst && i < wfirst + wnum && faces[i].nstyles) t = litplane;
		else continue;
		if (!t[faces[i].plane]) { t[faces[i].plane] = xmalloc(sizeof(int) * 2); }
		else t[faces[i].plane] = realloc(t[faces[i].plane], sizeof(int) * (t[faces[i].plane][0] + 2));
		t[faces[i].plane][++t[faces[i].plane][0]] = i;
	}

	/* reflectivity per texinfo (cached by name) */
	for (i = wfirst; i < wfirst + wnum; i++)
	{
		int k;
		for (k = wfirst; k < i; k++)
			if (faces[k].nstyles && !strcmp(texinfos[faces[k].texinfo].tex, texinfos[faces[i].texinfo].tex)) break;
		if (k < i) memcpy(faces[i].refl, faces[k].refl, 12);
		else texture_reflectivity(paks, texinfos[faces[i].texinfo].tex, faces[i].refl);
	}

	/* entities: sun vector and light fades */
	o = lump(0, &l);
	{
		char *txt = xmalloc(l + 1);
		memcpy(txt, bspdata + o, l);
		nents = parse_entities(txt, &ents);
		free(txt);
	}
	sundir[0] = 0; sundir[1] = 0; sundir[2] = 1;
	if (nents && ents[0].sunv[0])
	{
		float v[3];
		if (sscanf(ents[0].sunv, "%f %f %f", &v[0], &v[1], &v[2]) == 3)
		{
			sundir[0] = -v[0]; sundir[1] = -v[1]; sundir[2] = -v[2];
			norm3(sundir);
		}
	}
	else if (nents && ents[0].suna[0])
	{
		/* "_sun_angle yaw pitch": the direction the sunlight travels */
		float yaw, pitch;
		if (sscanf(ents[0].suna, "%f %f", &yaw, &pitch) == 2)
		{
			yaw *= 3.14159265f / 180; pitch *= 3.14159265f / 180;
			sundir[0] = -cosf(pitch) * cosf(yaw); sundir[1] = -cosf(pitch) * sinf(yaw); sundir[2] = -sinf(pitch);
		}
	}
	if (nents && ents[0].sunc[0]) sscanf(ents[0].sunc, "%f %f %f", &suncolor[0], &suncolor[1], &suncolor[2]);

	/* brush models that cast shadows (everything but triggers), with -bmodels 1 */
	if (opt_bmodels)
	{
		int mo, ml;
		mo = lump(13, &ml);
		for (j = 0; j < nents && nsmodels < 1024; j++)
		{
			float *m;
			int k;
			if (ents[j].model <= 0 || ents[j].model >= ml / 48 || !strncmp(ents[j].cls, "trigger", 7)) continue;
			m = (float *)(bspdata + mo + ents[j].model * 48);
			for (k = 0; k < 3; k++) { smodels[nsmodels].mins[k] = m[k] - 1; smodels[nsmodels].maxs[k] = m[3 + k] + 1; }
			smodels[nsmodels].head = *(int *)(m + 9);
			nsmodels++;
		}
	}

	/* lights lump */
	o = lump(20, &l);
	points = xmalloc(sizeof(light_t) * (l / 36 + 1));
	surfs = xmalloc(sizeof(light_t) * (l / 36 + 1));
	for (i = 0; i < l / 36; i++)
	{
		float *r = (float *)(bspdata + o + i * 36);
		int type = *(int *)(r + 7);
		light_t lt;
		memset(&lt, 0, sizeof(lt));
		memcpy(lt.o, r, 12);
		memcpy(lt.color, r + 3, 12);
		lt.intensity = r[6];
		lt.fade = 1;
		if (type == 1 || type == 2)
		{
			int switchable = 0;
			for (j = 0; j < nents; j++)
				if (ents[j].hasorigin && !strncmp(ents[j].cls, "light", 5) && fabsf(ents[j].origin[0] - lt.o[0]) < 1 &&
				    fabsf(ents[j].origin[1] - lt.o[1]) < 1 && fabsf(ents[j].origin[2] - lt.o[2]) < 1)
				{
					ent_t *e = &ents[j];
					lt.fade = e->fade;
					/* a light with a targetname is switchable: the compiler puts it in its own style
					   block (32+), which is kept from the original; it must not be baked into style 0 */
					switchable = e->targetname[0] != 0;
					if (type == 2)
					{
						/* spotlight: axis to the target, else _mangle (yaw pitch), else angle (-1 up, -2 down) */
						int k, found = 0;
						lt.spot = 1;
						lt.stopdot = cosf((e->cone > 0 ? e->cone : 10) * opt_conescale * 3.14159265f / 180);
						lt.focus = (e->focus > 0 ? e->focus : 1) * opt_focusscale;
						if (e->target[0])
							for (k = 0; k < nents; k++)
								if (ents[k].hasorigin && !strcmp(ents[k].targetname, e->target))
								{
									lt.dir[0] = ents[k].origin[0] - lt.o[0];
									lt.dir[1] = ents[k].origin[1] - lt.o[1];
									lt.dir[2] = ents[k].origin[2] - lt.o[2];
									found = 1;
									break;
								}
						if (!found && e->hasmangle)
						{
							float y = e->mangle[0] * 3.14159265f / 180, pt = e->mangle[1] * 3.14159265f / 180;
							lt.dir[0] = cosf(pt) * cosf(y); lt.dir[1] = cosf(pt) * sinf(y); lt.dir[2] = sinf(pt);
							found = 1;
						}
						if (!found)
						{
							float y = (e->hasangle ? e->angle : 0) * 3.14159265f / 180;
							if (e->hasangle && e->angle == -1) { lt.dir[2] = 1; }
							else if (e->hasangle && e->angle == -2) { lt.dir[2] = -1; }
							else { lt.dir[0] = cosf(y); lt.dir[1] = sinf(y); }
						}
						norm3(lt.dir);
					}
					break;
				}
			if (switchable) { nswitch++; continue; }
			lt.cluster = leafs[point_leaf(lt.o)].cluster;
			points[npoints++] = lt;
		}
		else if (type == 0)
		{
			/* patch normal: the SURF_LIGHT face whose plane passes closest */
			float best = 1e9f;
			for (j = 0; j < nfaces; j++)
			{
				face_t *fc = &faces[j];
				float c[3] = {0, 0, 0}, dd, dc[3];
				int k;
				if (!(texinfos[fc->texinfo].flags & SURF_LIGHT) || (texinfos[fc->texinfo].flags & SURF_SKY)) continue;
				for (k = 0; k < fc->nv; k++) { c[0] += fc->poly[k * 3]; c[1] += fc->poly[k * 3 + 1]; c[2] += fc->poly[k * 3 + 2]; }
				c[0] /= fc->nv; c[1] /= fc->nv; c[2] /= fc->nv;
				dc[0] = lt.o[0] - c[0]; dc[1] = lt.o[1] - c[1]; dc[2] = lt.o[2] - c[2];
				dd = fabsf(dot3(lt.o, fc->normal) - fc->dist) + 0.01f * sqrtf(dot3(dc, dc));
				if (dd < best) { best = dd; memcpy(lt.n, fc->normal, 12); }
			}
			{
				float p1[3] = {lt.o[0] + lt.n[0], lt.o[1] + lt.n[1], lt.o[2] + lt.n[2]};
				lt.cluster = leafs[point_leaf(p1)].cluster;
			}
			surfs[nsurfs++] = lt;
		}
	}
	free(ents);
}

/* ---------------- sampling ---------------- */

/* cosine-weighted direction around n, from two uniforms */
static void cos_dir(const float *n, float u1, float u2, float *d)
{
	float a[3] = {1, 0, 0}, tx[3], ty[3], r = sqrtf(u1), ph = 6.2831853f * u2;
	float x = r * cosf(ph), y = r * sinf(ph), z = sqrtf(1 - u1);
	if (fabsf(n[0]) > 0.9f) { a[0] = 0; a[1] = 1; }
	cross3(n, a, tx); norm3(tx);
	cross3(n, tx, ty);
	d[0] = tx[0] * x + ty[0] * y + n[0] * z;
	d[1] = tx[1] * x + ty[1] * y + n[1] * z;
	d[2] = tx[2] * x + ty[2] * y + n[2] * z;
}

/* direct terms at one point (point, surf, sun, sky, spot) x RGB; returns 0 if the point is in solid */
static int direct_terms(const float *p0, const float *n, uint32_t seed, float out[ND][3], int soft)
{
	float p[3];
	int leaf, cl, i, k, nsky;
	uint32_t s = seed;
	p[0] = p0[0] + n[0]; p[1] = p0[1] + n[1]; p[2] = p0[2] + n[2];
	leaf = point_leaf(p);
	if (leafs[leaf].contents & CONTENTS_SOLID) return 0;
	cl = leafs[leaf].cluster;
	memset(out, 0, sizeof(float) * ND * 3);
	for (i = 0; i < npoints; i++)
	{
		light_t *lt = &points[i];
		float dv[3] = {lt->o[0] - p[0], lt->o[1] - p[1], lt->o[2] - p[2]}, dist = sqrtf(dot3(dv, dv)), cs, fall, tgt[3];
		float axis = 1;
		cs = dot3(dv, n) / (dist > 1e-3f ? dist : 1e-3f);
		fall = lt->intensity - dist * lt->fade;
		if (cs <= 0 || fall <= 0) continue;
		if (lt->spot)
		{
			/* cosine to the spot axis (light -> point), cut at the cone, shaped by _focus */
			float ca = -dot3(dv, lt->dir) / (dist > 1e-3f ? dist : 1e-3f);
			if (ca < lt->stopdot || ca <= 0) continue;
			axis = powf(ca, lt->focus);
		}
		if (!pvs_vis(cl, lt->cluster)) continue;
		memcpy(tgt, lt->o, 12);
		if (soft && opt_lr > 0)
		{
			/* random point in a sphere of radius lr around the light */
			float u, v, w;
			do { u = rnd(&s) * 2 - 1; v = rnd(&s) * 2 - 1; w = rnd(&s) * 2 - 1; } while (u * u + v * v + w * w > 1);
			tgt[0] += u * opt_lr; tgt[1] += v * opt_lr; tgt[2] += w * opt_lr;
			if (leafs[point_leaf(tgt)].contents & CONTENTS_SOLID) memcpy(tgt, lt->o, 12);
		}
		if (blocked(p, tgt)) continue;
		for (k = 0; k < 3; k++) out[lt->spot ? 4 : 0][k] += lt->color[k] * fall * cs * axis;
	}
	for (i = 0; i < nsurfs; i++)
	{
		light_t *lt = &surfs[i];
		float dv[3] = {lt->o[0] - p[0], lt->o[1] - p[1], lt->o[2] - p[2]}, d2 = dot3(dv, dv), dist, cs, cs2, sc, tgt[3];
		if (d2 < 1) d2 = 1;
		dist = sqrtf(d2);
		cs = dot3(dv, n) / dist;
		cs2 = -dot3(dv, lt->n) / dist;
		if (opt_surfmode == 1 || opt_surfmode == 4) cs2 = 1.0f;          /* patch emits in all directions */
		else if (opt_surfmode == 5) cs2 = 0.5f * (1.0f + cs2);         /* wrapped: half strength sideways, none straight back */
		else if (opt_surfmode == 6) cs2 = cs2 > 0 ? cs2 : 0.25f * (1.0f + cs2); /* front cosine, dim glow behind */
		if (cs <= 0 || cs2 <= 0.001f) continue;
		if (opt_surfmode == 7) { sc = (opt_surfk * sqrtf(lt->intensity) - dist) * cs * cs2; if (sc <= 0) continue; } /* linear, range k*sqrt(I) */
		else if (opt_surfmode == 2) sc = sqrtf(lt->intensity * cs * cs2) / dist;      /* square root of the qrad3 term */
		else if (opt_surfmode >= 3) sc = lt->intensity * cs * cs2 / dist * 0.01f; /* inverse-linear falloff */
		else sc = lt->intensity / d2 * cs * cs2;
		if (sc < 0.05f || !pvs_vis(cl, lt->cluster)) continue;
		tgt[0] = lt->o[0] + lt->n[0]; tgt[1] = lt->o[1] + lt->n[1]; tgt[2] = lt->o[2] + lt->n[2];
		if (opt_surfmode == 1 || opt_surfmode >= 4)
			for (k = 0; k < 3; k++) tgt[k] = lt->o[k] - dv[k] / dist * 2.0f; /* step off the emitter toward the sample */
		if (blocked(p, tgt)) continue;
		for (k = 0; k < 3; k++) out[1][k] += lt->color[k] * sc;
	}
	{
		float cs = dot3(n, sundir), d[3], hit[3];
		if (cs > 0)
		{
			memcpy(d, sundir, 12);
			if (soft && opt_sunang > 0)
			{
				float r = tanf(opt_sunang * 3.14159265f / 180) * sqrtf(rnd(&s)), ph = 6.2831853f * rnd(&s), a[3] = {1, 0, 0}, tx[3], ty[3];
				if (fabsf(d[0]) > 0.9f) { a[0] = 0; a[1] = 1; }
				cross3(d, a, tx); norm3(tx); cross3(d, tx, ty);
				for (k = 0; k < 3; k++) d[k] += (tx[k] * cosf(ph) + ty[k] * sinf(ph)) * r;
				norm3(d);
			}
			if (ray_face(p, d, skyplane, 0, hit) >= 0)
				for (k = 0; k < 3; k++) out[2][k] += cs * suncolor[k];
		}
	}
	nsky = 0;
	{
		int sq = (int)sqrtf((float)opt_sky), a, b;
		float hit[3], d[3];
		if (sq < 1) sq = 1;
		for (a = 0; a < sq; a++)
			for (b = 0; b < sq; b++)
			{
				cos_dir(n, (a + rnd(&s)) / sq, (b + rnd(&s)) / sq, d);
				if (ray_face(p, d, skyplane, 0, hit) >= 0) nsky++;
			}
		for (k = 0; k < 3; k++) out[3][k] = (float)nsky / (sq * sq);
	}
	return 1;
}

/* grid of a face at luxel size (ls_s, ls_t) */
static void face_grid(const face_t *f, float ls, float *lss, float *lst, int *w, int *h)
{
	if (ls > 0) { *lss = *lst = ls; *w = (int)(f->ext[0] / ls) + 1; *h = (int)(f->ext[1] / ls) + 1; }
	else { *lss = 16.0f * f->scs; *lst = 16.0f * f->sct; *w = f->lmw; *h = f->lmh; }
}

/* ---------------- native-grid state ---------------- */

static int nsamp;
static float *T;      /* nsamp * NT * 3 terms */
static unsigned char *V; /* valid */
static unsigned char *E; /* sample of a light-emitting face (SURF_LIGHT): keeps the compiler's emission colour */
static float *O;      /* original lightmap, nsamp * 3 */
static float *D;      /* direct light from fit 1, nsamp * 3 (for the bounce) */
static int *bfaces, nbfaces;

static void fill_invalid(float *buf, int stride, const unsigned char *valid, int w, int h)
{
	int i, j, k;
	for (i = 0; i < w * h; i++)
	{
		int best = -1, bd = 1 << 30;
		if (valid[i]) continue;
		for (j = 0; j < w * h; j++)
			if (valid[j])
			{
				int dx = (i % w) - (j % w), dy = (i / w) - (j / w), dd = dx * dx + dy * dy;
				if (dd < bd) { bd = dd; best = j; }
			}
		if (best >= 0) for (k = 0; k < stride; k++) buf[i * stride + k] = buf[best * stride + k];
	}
}

static void bake_native(void)
{
	int fi;
	nsamp = 0;
	bfaces = xmalloc(sizeof(int) * wnum);
	for (fi = wfirst; fi < wfirst + wnum; fi++)
		if (faces[fi].nstyles)
		{
			faces[fi].lofs = nsamp;
			nsamp += faces[fi].lmw * faces[fi].lmh;
			bfaces[nbfaces++] = fi;
		}
	T = xmalloc(sizeof(float) * nsamp * NT * 3);
	V = xmalloc(nsamp);
	E = xmalloc(nsamp);
	O = xmalloc(sizeof(float) * nsamp * 3);
	D = xmalloc(sizeof(float) * nsamp * 3);
#pragma omp parallel for schedule(dynamic, 4)
	for (fi = 0; fi < nbfaces; fi++)
	{
		face_t *f = &faces[bfaces[fi]];
		float lss, lst, p[3];
		int w, h, i, j, k;
		face_grid(f, 0, &lss, &lst, &w, &h);
		for (j = 0; j < h; j++)
			for (i = 0; i < w; i++)
			{
				int idx = f->lofs + j * w + i;
				float t[ND][3];
				face_point(f, f->mins[0] + lss * i, f->mins[1] + lst * j, p);
				V[idx] = (unsigned char)direct_terms(p, f->normal, hash32(idx * 7919u + 1), t, 0);
				E[idx] = (texinfos[f->texinfo].flags & SURF_LIGHT) != 0;
				if (V[idx]) memcpy(T + (size_t)idx * NT * 3, t, sizeof(t));
				for (k = 0; k < 3; k++) O[idx * 3 + k] = lighting[f->lightofs + (j * w + i) * 3 + k];
			}
	}
}

/* bilinear sample of a per-face native buffer (3 floats per sample) at world point h */
static void sample_native(const float *buf, const face_t *f, const float *h, float *out)
{
	const texinfo_t *ti = &texinfos[f->texinfo];
	float s = dot3(h, ti->vecs[0]) + ti->vecs[0][3], t = dot3(h, ti->vecs[1]) + ti->vecs[1][3];
	float x = (s - f->mins[0]) / (16.0f * f->scs), y = (t - f->mins[1]) / (16.0f * f->sct), fx, fy;
	int x0, y0, x1, y1, k;
	const float *b = buf + (size_t)f->lofs * 3;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > f->lmw - 1) x = (float)(f->lmw - 1);
	if (y > f->lmh - 1) y = (float)(f->lmh - 1);
	x0 = (int)x; y0 = (int)y;
	x1 = x0 + 1 < f->lmw ? x0 + 1 : x0;
	y1 = y0 + 1 < f->lmh ? y0 + 1 : y0;
	fx = x - x0; fy = y - y0;
	for (k = 0; k < 3; k++)
		out[k] = (b[(y0 * f->lmw + x0) * 3 + k] * (1 - fx) + b[(y0 * f->lmw + x1) * 3 + k] * fx) * (1 - fy) +
		         (b[(y1 * f->lmw + x0) * 3 + k] * (1 - fx) + b[(y1 * f->lmw + x1) * 3 + k] * fx) * fy;
}

/* one gathered bounce: dst = mean over hemisphere rays of (reflectivity x src at the hit) */
static void bake_bounce(const float *src, float *dst)
{
	int fi;
#pragma omp parallel for schedule(dynamic, 4)
	for (fi = 0; fi < nbfaces; fi++)
	{
		face_t *f = &faces[bfaces[fi]];
		float lss, lst, p[3];
		int w, h, i, j, k, a, b, sq = (int)sqrtf((float)opt_bounce);
		if (sq < 1) sq = 1;
		face_grid(f, 0, &lss, &lst, &w, &h);
		for (j = 0; j < h; j++)
			for (i = 0; i < w; i++)
			{
				int idx = f->lofs + j * w + i;
				float acc[3] = {0, 0, 0}, d[3], hit[3], L[3];
				uint32_t s = hash32(idx * 104729u + 7);
				if (!V[idx]) continue;
				face_point(f, f->mins[0] + lss * i, f->mins[1] + lst * j, p);
				p[0] += f->normal[0]; p[1] += f->normal[1]; p[2] += f->normal[2];
				for (a = 0; a < sq; a++)
					for (b = 0; b < sq; b++)
					{
						int hf;
						cos_dir(f->normal, (a + rnd(&s)) / sq, (b + rnd(&s)) / sq, d);
						hf = ray_face(p, d, litplane, 1, hit);
						if (hf < 0) continue;
						sample_native(src, &faces[hf], hit, L);
						for (k = 0; k < 3; k++) acc[k] += faces[hf].refl[k] * L[k];
					}
				for (k = 0; k < 3; k++) dst[(size_t)idx * 3 + k] = acc[k] / (sq * sq);
			}
	}
}

/* least squares over valid, unclipped channel values; terms mask selects columns, plus optional constant */
static void fit(int mask, int withconst, double *wout, double *r2, double *mae)
{
	double A[8][8], y[8], ym = 0, ss = 0, se = 0, sa = 0;
	int cols[8], nc = 0, i, j, k, r, cnt = 0;
	memset(A, 0, sizeof(A)); memset(y, 0, sizeof(y));
	for (i = 0; i < NT; i++) if (mask & (1 << i)) cols[nc++] = i;
	if (withconst) cols[nc++] = -1;
	for (i = 0; i < nsamp; i++)
	{
		if (!V[i] || E[i]) continue;
		for (k = 0; k < 3; k++)
		{
			double x[8], o = O[i * 3 + k];
			if (o >= opt_cap - 4) continue;
			for (j = 0; j < nc; j++) x[j] = cols[j] < 0 ? 1.0 : T[((size_t)i * NT + cols[j]) * 3 + k];
			for (j = 0; j < nc; j++) { for (r = 0; r < nc; r++) A[j][r] += x[j] * x[r]; y[j] += x[j] * o; }
			ym += o; cnt++;
		}
	}
	for (j = 0; j < nc; j++) A[j][j] += 1e-6 * (A[j][j] + 1);
	/* Gauss-Jordan */
	for (j = 0; j < nc; j++)
	{
		int piv = j;
		for (r = j + 1; r < nc; r++) if (fabs(A[r][j]) > fabs(A[piv][j])) piv = r;
		for (k = 0; k < nc; k++) { double t = A[j][k]; A[j][k] = A[piv][k]; A[piv][k] = t; }
		{ double t = y[j]; y[j] = y[piv]; y[piv] = t; }
		for (r = 0; r < nc; r++)
		{
			double m;
			if (r == j || A[j][j] == 0) continue;
			m = A[r][j] / A[j][j];
			for (k = 0; k < nc; k++) A[r][k] -= m * A[j][k];
			y[r] -= m * y[j];
		}
	}
	for (j = 0; j < NT + 1; j++) wout[j] = 0;
	for (j = 0; j < nc; j++) wout[cols[j] < 0 ? NT : cols[j]] = A[j][j] ? y[j] / A[j][j] : 0;
	ym /= cnt ? cnt : 1;
	for (i = 0; i < nsamp; i++)
	{
		if (!V[i] || E[i]) continue;
		for (k = 0; k < 3; k++)
		{
			double o = O[i * 3 + k], pr = wout[NT];
			if (o >= opt_cap - 4) continue;
			for (j = 0; j < NT; j++) pr += wout[j] * T[((size_t)i * NT + j) * 3 + k];
			if (pr < 0) pr = 0;
			if (pr > opt_cap) pr = opt_cap;
			ss += (o - ym) * (o - ym); se += (pr - o) * (pr - o); sa += fabs(pr - o);
		}
	}
	*r2 = 1 - se / (ss ? ss : 1);
	*mae = sa / (cnt ? cnt : 1);
}

/* ---------------- sun direction from the original lightmap ---------------- */

/* sample position (world, already off the face by 1 unit) of native sample idx of face f */
static void native_pos(const face_t *f, int s, float *p)
{
	int w = f->lmw;
	face_point(f, f->mins[0] + 16.0f * f->scs * (s % w), f->mins[1] + 16.0f * f->sct * (s / w), p);
	p[0] += f->normal[0]; p[1] += f->normal[1]; p[2] += f->normal[2];
}

static float sun_vis(const float *p, const float *n, const float *dir)
{
	float cs = dot3(n, dir), hit[3];
	if (cs <= 0) return 0;
	return ray_face(p, dir, skyplane, 0, hit) >= 0 ? cs : 0;
}

/* residual of a luminance least-squares fit (point, surf, sun, sky, spot, const) on the subset */
static double subset_residual(int ns, const int *sidx, const float *sunv, double *sunw)
{
	double A[6][6], y[6], x[6], se = 0;
	int i, j, r, k, nc = 6;
	memset(A, 0, sizeof(A)); memset(y, 0, sizeof(y));
	for (i = 0; i < ns; i++)
	{
		const float *t = T + (size_t)sidx[i] * NT * 3;
		double o = (O[sidx[i] * 3] + O[sidx[i] * 3 + 1] + O[sidx[i] * 3 + 2]) / 3.0;
		x[0] = (t[0] + t[1] + t[2]) / 3; x[1] = (t[3] + t[4] + t[5]) / 3; x[2] = sunv[i];
		x[3] = (t[9] + t[10] + t[11]) / 3; x[4] = (t[12] + t[13] + t[14]) / 3; x[5] = 1;
		for (j = 0; j < nc; j++) { for (r = 0; r < nc; r++) A[j][r] += x[j] * x[r]; y[j] += x[j] * o; }
	}
	{
		double M[6][7], wv[6];
		for (j = 0; j < nc; j++) { for (r = 0; r < nc; r++) M[j][r] = A[j][r]; M[j][6] = y[j]; M[j][j] += 1e-6 * (A[j][j] + 1); }
		for (j = 0; j < nc; j++)
		{
			int piv = j;
			for (r = j + 1; r < nc; r++) if (fabs(M[r][j]) > fabs(M[piv][j])) piv = r;
			for (k = 0; k < 7; k++) { double t2 = M[j][k]; M[j][k] = M[piv][k]; M[piv][k] = t2; }
			for (r = 0; r < nc; r++)
			{
				double m;
				if (r == j || M[j][j] == 0) continue;
				m = M[r][j] / M[j][j];
				for (k = 0; k < 7; k++) M[r][k] -= m * M[j][k];
			}
		}
		for (j = 0; j < nc; j++) wv[j] = M[j][j] ? M[j][6] / M[j][j] : 0;
		for (i = 0; i < ns; i++)
		{
			const float *t = T + (size_t)sidx[i] * NT * 3;
			double o = (O[sidx[i] * 3] + O[sidx[i] * 3 + 1] + O[sidx[i] * 3 + 2]) / 3.0;
			double pr = wv[0] * (t[0] + t[1] + t[2]) / 3 + wv[1] * (t[3] + t[4] + t[5]) / 3 + wv[2] * sunv[i] +
			            wv[3] * (t[9] + t[10] + t[11]) / 3 + wv[4] * (t[12] + t[13] + t[14]) / 3 + wv[5];
			se += (pr - o) * (pr - o);
		}
		*sunw = wv[2];
	}
	return se;
}

/* search the direction toward the sun that best explains the original lightmap; keeps it in sundir */
static void fit_sun(void)
{
	enum { NS = 4000 };
	static int sidx[NS];
	static float spos[NS][3], snrm[NS][3];
	int ns = 0, i, pass;
	double best = 1e300, base, bw = 0;
	float baz = 0, bel = 90;
	uint32_t s = 12345;
	/* subset: valid samples, clipped ones included (sunlit areas often sit at the clamp) */
	for (i = 0; i < nsamp * 4 && ns < NS; i++)
	{
		int idx = (int)(rnd(&s) * nsamp), fi;
		if (idx >= nsamp || !V[idx] || E[idx]) continue;
		for (fi = 0; fi < nbfaces; fi++)
		{
			face_t *f = &faces[bfaces[fi]];
			if (idx >= f->lofs && idx < f->lofs + f->lmw * f->lmh)
			{
				native_pos(f, idx - f->lofs, spos[ns]);
				memcpy(snrm[ns], f->normal, 12);
				break;
			}
		}
		sidx[ns++] = idx;
	}
	{
		float *zero = xmalloc(sizeof(float) * ns);
		base = subset_residual(ns, sidx, zero, &bw);
		free(zero);
	}
	if (getenv("OMLIGHT_SUNDEBUG"))
	{
		float az = (float)atof(getenv("OMLIGHT_SUNDEBUG")), el = 35, dir[3], *sv = xmalloc(sizeof(float) * ns);
		int k, lit = 0, up = 0;
		double r, w, mlit = 0, mdark = 0;
		dir[0] = cosf(el * 3.14159265f / 180) * cosf(az * 3.14159265f / 180);
		dir[1] = cosf(el * 3.14159265f / 180) * sinf(az * 3.14159265f / 180);
		dir[2] = sinf(el * 3.14159265f / 180);
		for (k = 0; k < ns; k++)
		{
			float hit[3], o = (O[sidx[k] * 3] + O[sidx[k] * 3 + 1] + O[sidx[k] * 3 + 2]) / 3;
			float upd[3] = {0, 0, 1};
			sv[k] = sun_vis(spos[k], snrm[k], dir);
			if (sv[k] > 0) { lit++; mlit += o; } else mdark += o;
			if (ray_face(spos[k], upd, skyplane, 0, hit) >= 0) up++;
		}
		r = subset_residual(ns, sidx, sv, &w);
		printf("debug az %.0f el %.0f: %d of %d lit (mean %.1f vs %.1f), %d see sky straight up; residual %.1f%%, sun weight %.2f\n",
		       az, el, lit, ns, lit ? mlit / lit : 0, (ns - lit) ? mdark / (ns - lit) : 0, up, 100 * r / base, w);
		free(sv);
	}
	for (pass = 0; pass < 2; pass++)
	{
		int naz = pass ? 11 : 36, nel = pass ? 9 : 17, c;
		float az0 = pass ? baz - 10 : 0, el0 = pass ? bel - 8 : 5, daz = pass ? 2.0f : 10.0f, del = pass ? 2.0f : 5.0f;
		float caz = baz, cel = bel;
		(void)caz; (void)cel;
#pragma omp parallel for schedule(dynamic, 1)
		for (c = 0; c < naz * nel; c++)
		{
			float az = az0 + daz * (c % naz), el = el0 + del * (c / naz), dir[3];
			float *sv;
			double r, w;
			int k;
			if (el < 1 || el > 89.9f) continue;
			dir[0] = cosf(el * 3.14159265f / 180) * cosf(az * 3.14159265f / 180);
			dir[1] = cosf(el * 3.14159265f / 180) * sinf(az * 3.14159265f / 180);
			dir[2] = sinf(el * 3.14159265f / 180);
			sv = xmalloc(sizeof(float) * ns);
			for (k = 0; k < ns; k++) sv[k] = sun_vis(spos[k], snrm[k], dir);
			r = subset_residual(ns, sidx, sv, &w);
			free(sv);
#pragma omp critical
			if (w > 0 && r < best) { best = r; baz = az; bel = el; bw = w; }
		}
	}
	if (best < base * 0.97)
	{
		sundir[0] = cosf(bel * 3.14159265f / 180) * cosf(baz * 3.14159265f / 180);
		sundir[1] = cosf(bel * 3.14159265f / 180) * sinf(baz * 3.14159265f / 180);
		sundir[2] = sinf(bel * 3.14159265f / 180);
		printf("sun fit: toward azimuth %.0f elevation %.0f, residual %.1f%% of no-sun\n", fmod(baz + 360, 360), bel, 100 * best / base);
	}
	else
	{
		sundir[0] = sundir[1] = 0; sundir[2] = -1; /* below the horizon: no directional sun */
		printf("sun fit: no directional sun explains the lightmap (best %.1f%% of no-sun)\n", 100 * best / base);
	}
	/* recompute the sun term of every native sample with the fitted direction */
#pragma omp parallel for schedule(dynamic, 8)
	for (i = 0; i < nbfaces; i++)
	{
		face_t *f = &faces[bfaces[i]];
		int sm, k;
		for (sm = 0; sm < f->lmw * f->lmh; sm++)
		{
			int idx = f->lofs + sm;
			float p[3], v;
			if (!V[idx]) continue;
			native_pos(f, sm, p);
			v = sun_vis(p, f->normal, sundir);
			for (k = 0; k < 3; k++) T[((size_t)idx * NT + 2) * 3 + k] = v * suncolor[k];
		}
	}
}

/* clamp keeping the hue: scale all channels down by the largest (as the renderer does at 255) */
static void clamp_rgb(float *c)
{
	float m = c[0] > c[1] ? c[0] : c[1];
	int k;
	if (c[2] > m) m = c[2];
	for (k = 0; k < 3; k++) if (c[k] < 0) c[k] = 0;
	if (m > opt_cap) for (k = 0; k < 3; k++) c[k] *= opt_cap / m;
}

/* ---------------- output ---------------- */

static void write_header(FILE *f, int n) { fwrite("OMLM", 1, 4, f); fwrite(&n, 4, 1, f); }

static void write_face(FILE *f, int fi, int w, int h, float lss, float lst, const float *rgb)
{
	fwrite(&fi, 4, 1, f); fwrite(&w, 4, 1, f); fwrite(&h, 4, 1, f); fwrite(&lss, 4, 1, f); fwrite(&lst, 4, 1, f);
	fwrite(rgb, 4, (size_t)w * h * 3, f);
}

int main(int argc, char **argv)
{
	double w1[NT + 1], w2[NT + 1], r2, mae, t0, t1;
	char path[1024], paks[8192] = "";
	const char *map = NULL, *outfile = NULL, *debug = NULL;
	uint64_t oldhash;
	int i, k, threads = 0;
	FILE *out;

	/* the engine redirects the output into a log file: keep it readable while the bake runs */
	setvbuf(stdout, NULL, _IONBF, 0);

	/* the engine's bake: the original grid, 4x4 supersampled, soft shadows, two bounces */
	opt_ls = -1;
	opt_ss = 4;
	opt_lr = 8;
	opt_sunang = 1.5f;
	opt_sky = 64;
	opt_bounce = 128;
	opt_bounces = 2;

	for (i = 1; i + 1 < argc; i += 2)
	{
		if (!strcmp(argv[i], "-map")) map = argv[i + 1];
		else if (!strcmp(argv[i], "-base"))
		{
			/* repeatable: the later game directory's paks override the earlier ones */
			char more[8192];
			list_paks(argv[i + 1], more, sizeof(more));
			if (more[0])
			{
				if (paks[0]) strncat(paks, ";", sizeof(paks) - strlen(paks) - 1);
				strncat(paks, more, sizeof(paks) - strlen(paks) - 1);
			}
		}
		else if (!strcmp(argv[i], "-out")) outfile = argv[i + 1];
		else if (!strcmp(argv[i], "-debug")) debug = argv[i + 1];
		else if (!strcmp(argv[i], "-threads")) threads = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-ls")) opt_ls = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "-ss")) opt_ss = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-lr")) opt_lr = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "-sunang")) opt_sunang = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "-sky")) opt_sky = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-bounce")) opt_bounce = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-cap")) opt_cap = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "-bounces")) opt_bounces = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-surfmode")) opt_surfmode = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-opaque")) opt_opaque = (int)strtoul(argv[i + 1], NULL, 16);
		else if (!strcmp(argv[i], "-bmodels")) opt_bmodels = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "-conescale")) opt_conescale = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "-outbsp")) opt_outbsp = argv[i + 1];
		else if (!strcmp(argv[i], "-surfk")) opt_surfk = (float)atof(argv[i + 1]);
		else if (!strcmp(argv[i], "-focusscale")) opt_focusscale = (float)atof(argv[i + 1]);
	}

	if (!map || (!outfile && !debug && !opt_outbsp))
	{
		fprintf(stderr, "usage: omlight -map maps/<name>.bsp -base <game base dir> -out <file.oml> [options]\n");
		return 2;
	}

	/* a background job: leave a core to the game */
	if (threads <= 0)
	{
		threads = omp_get_num_procs() - 1;
	}

	omp_set_num_threads(threads < 1 ? 1 : threads);

	t0 = omp_get_wtime();
	load_bsp(map, paks);
	{
		int ll, lo = lump(7, &ll);
		oldhash = fnv64(bspdata + lo, ll);
	}
	t1 = omp_get_wtime();
	printf("loaded: %d faces (%d world), %d point lights (%d switchable skipped), %d surface patches, %d clusters, threads %d  [%.2f s]\n",
	       nfaces, wnum, npoints, nswitch, nsurfs, nclusters, omp_get_max_threads(), t1 - t0);

	t0 = omp_get_wtime();
	bake_native();
	t1 = omp_get_wtime();
	printf("A  native direct: %d samples  [%.2f s]\n", nsamp, t1 - t0);
	t0 = omp_get_wtime();
	fit_sun();
	t1 = omp_get_wtime();
	printf("   sun search  [%.2f s]\n", t1 - t0);
	fit(0x1F, 1, w1, &r2, &mae);
	printf("fit 1  point %.4f surf %.4f sun %.3f sky %.3f spot %.4f const %.3f   R2 %.4f MAE %.2f\n", w1[0], w1[1], w1[2], w1[3], w1[4], w1[NT], r2, mae);

	for (i = 0; i < nsamp; i++)
		for (k = 0; k < 3; k++)
		{
			int j;
			float v = 0;
			for (j = 0; j < ND; j++) v += (float)w1[j] * T[((size_t)i * NT + j) * 3 + k];
			D[i * 3 + k] = v;
		}
	for (i = 0; i < nbfaces; i++)
	{
		face_t *f = &faces[bfaces[i]];
		fill_invalid(D + (size_t)f->lofs * 3, 3, V + f->lofs, f->lmw, f->lmh);
	}
	t0 = omp_get_wtime();
	{
		/* bounce k gathers bounce k-1 (bounce 1 gathers the direct light); the term is their sum */
		float *buf[2], *src = D;
		int bi;
		buf[0] = xmalloc(sizeof(float) * nsamp * 3);
		buf[1] = xmalloc(sizeof(float) * nsamp * 3);
		for (bi = 0; bi < opt_bounces; bi++)
		{
			float *dst = buf[bi & 1];
			bake_bounce(src, dst);
			for (i = 0; i < nbfaces; i++)
			{
				face_t *f = &faces[bfaces[i]];
				fill_invalid(dst + (size_t)f->lofs * 3, 3, V + f->lofs, f->lmw, f->lmh);
			}
			for (i = 0; i < nsamp * 3; i++) T[((size_t)(i / 3) * NT + TB) * 3 + i % 3] += dst[i];
			src = dst;
		}
		free(buf[0]);
		free(buf[1]);
	}
	t1 = omp_get_wtime();
	printf("B  %d bounce(s), %d rays/sample  [%.2f s]\n", opt_bounces, opt_bounce, t1 - t0);
	fit(0x3F, 0, w2, &r2, &mae);
	printf("fit 2  point %.4f surf %.4f sun %.3f sky %.3f spot %.4f bounce %.3f   R2 %.4f MAE %.2f\n", w2[0], w2[1], w2[2], w2[3], w2[4], w2[TB], r2, mae);

	/* raw terms for analysis: nsamp, NT, then per sample face index, valid, original RGB, NT*3 terms */
	if (debug && (snprintf(path, sizeof(path), "%s_terms.bin", debug) > 0) && ((out = fopen(path, "wb")) != NULL))
	{
		int nt = NT;
		fwrite(&nsamp, 4, 1, out); fwrite(&nt, 4, 1, out);
		for (i = 0; i < nbfaces; i++)
		{
			face_t *f = &faces[bfaces[i]];
			int s;
			for (s = 0; s < f->lmw * f->lmh; s++)
			{
				int idx = f->lofs + s, vv = V[idx];
				fwrite(&bfaces[i], 4, 1, out); fwrite(&vv, 4, 1, out);
				fwrite(O + (size_t)idx * 3, 4, 3, out);
				fwrite(T + (size_t)idx * NT * 3, 4, NT * 3, out);
			}
		}
		fclose(out);
	}

	/* native result (analysis) */
	out = NULL;

	if (debug && (snprintf(path, sizeof(path), "%s_native.bin", debug) > 0))
	{
		out = fopen(path, "wb");
	}

	if (out)
	{
		write_header(out, nbfaces);
	}

	for (i = 0; out && (i < nbfaces); i++)
	{
		face_t *f = &faces[bfaces[i]];
		int n = f->lmw * f->lmh, s;
		float *rgb = xmalloc(sizeof(float) * n * 3);
		for (s = 0; s < n; s++)
			for (k = 0; k < 3; k++)
			{
				int j;
				float v = 0;
				for (j = 0; j < NT; j++) v += (float)w2[j] * T[((size_t)(f->lofs + s) * NT + j) * 3 + k];
				rgb[s * 3 + k] = v;
				if (k == 2) clamp_rgb(rgb + s * 3);
			}
		fill_invalid(rgb, 3, V + f->lofs, f->lmw, f->lmh);
		write_face(out, bfaces[i], f->lmw, f->lmh, 16.0f * f->scs, 16.0f * f->sct, rgb);
		free(rgb);
	}

	if (out)
	{
		fclose(out);
	}

	if (opt_ls != 0) /* -ls <units>: high resolution; -ls -1: the original grid, supersampled with soft shadows */
	{
		/* bounce on the native grid, filled, for bilinear lookups */
		float *BN = xmalloc(sizeof(float) * nsamp * 3);
		long long total = 0;
		FILE *hf;
		float **res = xmalloc(sizeof(float *) * nbfaces);
		int *rw = xmalloc(sizeof(int) * nbfaces), *rh = xmalloc(sizeof(int) * nbfaces);
		for (i = 0; i < nsamp; i++) for (k = 0; k < 3; k++) BN[i * 3 + k] = T[((size_t)i * NT + TB) * 3 + k];
		for (i = 0; i < nbfaces; i++)
		{
			face_t *f = &faces[bfaces[i]];
			fill_invalid(BN + (size_t)f->lofs * 3, 3, V + f->lofs, f->lmw, f->lmh);
		}
		t0 = omp_get_wtime();
#pragma omp parallel for schedule(dynamic, 2) reduction(+ : total)
		for (i = 0; i < nbfaces; i++)
		{
			face_t *f = &faces[bfaces[i]];
			float lss, lst, p[3], bn[3];
			int w, h, x, y, a, b, k2;
			unsigned char *vv;
			float *rgb;
			face_grid(f, opt_ls > 0 ? opt_ls : 0, &lss, &lst, &w, &h);
			rgb = xmalloc(sizeof(float) * w * h * 3);
			vv = xmalloc(w * h);
			for (y = 0; y < h; y++)
				for (x = 0; x < w; x++)
				{
					float acc[3] = {0, 0, 0};
					int nv = 0;
					for (a = 0; a < opt_ss; a++)
						for (b = 0; b < opt_ss; b++)
						{
							float t[ND][3], s0 = f->mins[0] + lss * (x + ((b + 0.5f) / opt_ss - 0.5f));
							float tt0 = f->mins[1] + lst * (y + ((a + 0.5f) / opt_ss - 0.5f));
							uint32_t seed = hash32((uint32_t)(bfaces[i] * 7349 + (y * w + x) * 31 + a * opt_ss + b));
							face_point(f, s0, tt0, p);
							if (!direct_terms(p, f->normal, seed, t, 1)) continue;
							sample_native(BN, f, p, bn);
							for (k2 = 0; k2 < 3; k2++)
								acc[k2] += (float)(w2[0] * t[0][k2] + w2[1] * t[1][k2] + w2[2] * t[2][k2] + w2[3] * t[3][k2] +
								                   w2[4] * t[4][k2] + w2[TB] * bn[k2]);
							nv++;
						}
					vv[y * w + x] = nv > 0;
					for (k2 = 0; k2 < 3; k2++) rgb[(y * w + x) * 3 + k2] = nv ? acc[k2] / nv : 0;
					clamp_rgb(rgb + (y * w + x) * 3);
				}
			fill_invalid(rgb, 3, vv, w, h);
			free(vv);
			res[i] = rgb; rw[i] = w; rh[i] = h;
			total += (long long)w * h;
		}
		t1 = omp_get_wtime();
		printf("C  hires %.1f u/sample, %dx%d supersampling, light radius %.1f, sun %.1f deg: %lld samples  [%.2f s]\n",
		       opt_ls, opt_ss, opt_ss, opt_lr, opt_sunang, total, t1 - t0);
		if (debug && (snprintf(path, sizeof(path), "%s_hires.bin", debug) > 0) && ((hf = fopen(path, "wb")) != NULL))
		{
			write_header(hf, nbfaces);
			for (i = 0; i < nbfaces; i++)
			{
				face_t *f = &faces[bfaces[i]];
				float lss = opt_ls > 0 ? opt_ls : 16.0f * f->scs, lst = opt_ls > 0 ? opt_ls : 16.0f * f->sct;
				write_face(hf, bfaces[i], rw[i], rh[i], lss, lst, res[i]);
			}
			fclose(hf);
		}

		/* the new lighting lump: style 0 of every baked world face replaced (same grid only); other styles
		   (switchable lights), lamp faces and brush-model faces keep their original lightmaps */
		if (opt_ls < 0)
		{
			int lo, ll, s, nemis = 0;
			lo = lump(7, &ll);
			for (i = 0; i < nbfaces; i++)
			{
				face_t *f = &faces[bfaces[i]];
				if (rw[i] != f->lmw || rh[i] != f->lmh) continue;
				if (texinfos[f->texinfo].flags & SURF_LIGHT) { nemis++; continue; } /* lamp faces keep their emission colour */
				for (s = 0; s < rw[i] * rh[i] * 3; s++)
				{
					float v = res[i][s] + 0.5f;
					int ofs = f->lightofs + s;
					if (ofs < 0 || ofs >= ll) break;
					bspdata[lo + ofs] = (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
				}
			}

			if (outfile)
			{
				/* the cache file, renamed into place only when complete */
				char tmp[1100];
				int ver = 1;
				uint32_t h[2] = {(uint32_t)oldhash, (uint32_t)(oldhash >> 32)};
				FILE *of;

				snprintf(tmp, sizeof(tmp), "%s.tmp", outfile);
				of = fopen(tmp, "wb");
				if (!of) { fprintf(stderr, "cannot write %s\n", tmp); return 1; }
				fwrite("OMLT", 1, 4, of);
				fwrite(&ver, 4, 1, of);
				fwrite(&ll, 4, 1, of);
				fwrite(h, 4, 2, of);
				fwrite(bspdata + lo, 1, ll, of);
				if (fclose(of) != 0) { fprintf(stderr, "cannot write %s\n", tmp); remove(tmp); return 1; }
				remove(outfile);
				if (rename(tmp, outfile) != 0) { fprintf(stderr, "cannot rename %s\n", tmp); remove(tmp); return 1; }
				printf("wrote %s (%d bytes of lighting, %d lamp faces kept original)\n", outfile, ll, nemis);
			}

			if (opt_outbsp)
			{
				FILE *ob = fopen(opt_outbsp, "wb");
				if (!ob) { fprintf(stderr, "cannot write %s\n", opt_outbsp); return 1; }
				fwrite(bspdata, 1, bsplen, ob);
				fclose(ob);
				printf("wrote %s\n", opt_outbsp);
			}
		}
		for (i = 0; i < nbfaces; i++) free(res[i]);
	}
	return 0;
}
