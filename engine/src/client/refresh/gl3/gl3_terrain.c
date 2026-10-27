/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: terrain sets of Soldier of Fortune (the scrolling
 * landscape and the drifting clouds outside the train of trn1). Selection of
 * env/<name>.trn through SPEC-002 slot 0x38, the .trn layout as the renderer
 * reads it, the ring of rows regenerated in front of the viewer, the per-frame
 * scroll, fade and drawing inside the world pass, and the clouds.
 *
 * Implements: SPEC-410 (Terrain sets), SPEC-144 (.trn), SPEC-002 (slot 0x38),
 *             SPEC-413 (r_speeds tp field)
 */

#include "header/local.h"

#define TRN_HEADER_SIZE 88
#define TRN_GROUP_SIZE 52
#define TRN_COLUMN_SIZE 68
#define TRN_MAX_ROWS 1024
#define TRN_MAX_COLUMNS 256
#define TRN_NUM_CLOUDS 32
#define TRN_CLOUD_EXTENT 16383.0f
#define TRN_CLOUD_WRAP 32766.0f

typedef struct
{
	gl3image_t *image;
	float threshold;    /* cumulative selection threshold */
	float sdiv, tdiv;   /* units per texture repeat */
	float fixs, fixt;   /* fixed repeat counts (non-zero overrides) */
	byte whole;         /* whole-repeat flag */
} trnentry_t;

typedef struct
{
	int numentries;
	trnentry_t *entries;
} trnslot_t;

typedef struct
{
	int slot;
	float ymin, ymax, zmin, zmax;
	int flags;          /* 0x2: mirror the texture */
	float lw[4];        /* lateral wave f1, f2, a1, a2 */
	float hw[4];        /* height wave f1, f2, a1, a2 */
} trncolumn_t;

typedef struct
{
	vec3_t pos;
	float s0, s1, t0, t1;
	trnentry_t *entry;
	float bright;
	qboolean behind;
} trnvert_t;

typedef struct
{
	vec3_t centre;
	vec3_t corner[4];
	vec3_t vel;
} trncloud_t;

static struct
{
	qboolean loaded;
	qboolean active;    /* header byte 1 */
	qboolean built;

	int numrows;        /* N */
	int numcols;        /* M */
	float speed;
	float fogstart, fogend;
	float fogcolor[3];
	byte clearflag;

	int spacing;        /* S of group 0 */
	float latoffset, hgtoffset;
	trncolumn_t *columns;  /* group 0 */

	int numslots;
	trnslot_t *slots;

	trnvert_t *verts;   /* numrows x numcols, ring of rows */
	int rear;           /* index of the rearmost row */
	float prevclock;

	trncloud_t clouds[TRN_NUM_CLOUDS];
	gl3image_t *cloudimage;
} trn;

/* the last game clock seen by a 3D frame (selection happens between frames) */
static float trn_lastclock;

/* terrain quads plus clouds drawn in the last frame with a terrain (r_speeds 1 "tp") */
int gl3_c_terrainpolys;

/* the engine's flrand (SPEC-002 import 0x60) in the SoF module; a local one otherwise */
float (*gl3_flrand_fn)(float min, float max);

static float
FlRand(float lo, float hi)
{
	if (gl3_flrand_fn)
	{
		return gl3_flrand_fn(lo, hi);
	}

	return lo + (hi - lo) * ((float)rand() / (float)RAND_MAX);
}

static float
Clock(void)
{
	return gl3_sofframe.active ? gl3_sofframe.gametime : trn_lastclock;
}

void
GL3_Terrain_Free(void)
{
	int i;

	if (trn.slots)
	{
		for (i = 0; i < trn.numslots; i++)
		{
			free(trn.slots[i].entries);
		}
	}

	free(trn.slots);
	free(trn.columns);
	free(trn.verts);
	memset(&trn, 0, sizeof(trn));
}

/* ------------------------------------------------------------------
 * file
 * ------------------------------------------------------------------ */

typedef struct
{
	const byte *p, *end;
	qboolean bad;
} trnreader_t;

static int
RdInt(trnreader_t *r)
{
	int v;

	if (r->p + 4 > r->end)
	{
		r->bad = true;
		return 0;
	}

	v = LittleLong(*(const int *)r->p);
	r->p += 4;
	return v;
}

static float
RdFloat(trnreader_t *r)
{
	float v;

	if (r->p + 4 > r->end)
	{
		r->bad = true;
		return 0;
	}

	v = LittleFloat(*(const float *)r->p);
	r->p += 4;
	return v;
}

static byte
RdByte(trnreader_t *r)
{
	if (r->p + 1 > r->end)
	{
		r->bad = true;
		return 0;
	}

	return *r->p++;
}

static void
RdString(trnreader_t *r, char *out, size_t size)
{
	size_t n = 0;

	while ((r->p < r->end) && *r->p)
	{
		if (n + 1 < size)
		{
			out[n++] = *r->p;
		}

		r->p++;
	}

	out[n] = 0;

	if (r->p >= r->end)
	{
		r->bad = true;
		return;
	}

	r->p++; /* NUL */
}

/* the texture name of an entry: a trailing .m32 (any case) is removed; wall image or "notexture" */
static gl3image_t *
TerrainImage(const char *name)
{
	char base[MAX_QPATH];
	size_t len;
	gl3image_t *image;

	Q_strlcpy(base, name, sizeof(base));
	len = strlen(base);

	if ((len > 4) && !Q_stricmp(base + len - 4, ".m32"))
	{
		base[len - 4] = 0;
	}

	image = GL3_SoF_FindImage(base, SOFIMG_WALL, true);
	return image ? image : GL3_SoF_NoTexture();
}

static qboolean
Parse(const byte *buf, int len)
{
	trnreader_t r;
	int i, g, numgroups, numsprites;

	r.p = buf;
	r.end = buf + len;
	r.bad = false;

	if (len < TRN_HEADER_SIZE)
	{
		return false;
	}

	trn.active = buf[1] != 0;
	r.p = buf + 0x06;
	trn.numrows = RdInt(&r);
	trn.numcols = RdInt(&r);
	numgroups = RdInt(&r);
	numsprites = RdInt(&r);
	trn.numslots = RdInt(&r);
	trn.speed = RdFloat(&r);
	trn.fogstart = RdFloat(&r);
	trn.fogend = RdFloat(&r);
	trn.fogcolor[0] = RdFloat(&r);
	trn.fogcolor[1] = RdFloat(&r);
	trn.fogcolor[2] = RdFloat(&r);
	trn.clearflag = RdByte(&r);
	/* 0x33..0x57: six floats, the "sun" byte and vector, read without effect */
	r.p = buf + TRN_HEADER_SIZE;

	if ((trn.numrows < 2) || (trn.numrows > TRN_MAX_ROWS) || (trn.numcols < 2) ||
		(trn.numcols > TRN_MAX_COLUMNS) || (numgroups < 1) || (numgroups > 64) ||
		(numsprites < 0) || (numsprites > 1024) || (trn.numslots < 1) || (trn.numslots > 1024))
	{
		return false;
	}

	trn.columns = calloc(trn.numcols, sizeof(trncolumn_t));

	/* every group is parsed, only group 0 is used */
	for (g = 0; g < numgroups; g++)
	{
		const byte *grp = r.p;
		int spacing = RdInt(&r);
		float lat = RdFloat(&r);
		float hgt = RdFloat(&r);

		r.p = grp + TRN_GROUP_SIZE;

		if (g == 0)
		{
			trn.spacing = spacing;
			trn.latoffset = lat;
			trn.hgtoffset = hgt;
		}

		for (i = 0; i < trn.numcols; i++)
		{
			const byte *col = r.p;
			trncolumn_t c;
			int k;

			c.slot = RdInt(&r);
			RdInt(&r);
			c.ymin = RdFloat(&r);
			c.ymax = RdFloat(&r);
			c.zmin = RdFloat(&r);
			c.zmax = RdFloat(&r);
			RdFloat(&r);
			RdFloat(&r);
			c.flags = RdInt(&r);

			for (k = 0; k < 4; k++)
			{
				c.lw[k] = RdFloat(&r);
			}

			for (k = 0; k < 4; k++)
			{
				c.hw[k] = RdFloat(&r);
			}

			r.p = col + TRN_COLUMN_SIZE;

			if (g == 0)
			{
				trn.columns[i] = c;
			}
		}

		if (r.bad || (r.p > r.end))
		{
			return false;
		}
	}

	/* sprite section: parsed and discarded (terrain sprites are never drawn) */
	r.p += 4;

	for (i = 0; i < numsprites && !r.bad; i++)
	{
		int n = RdInt(&r), k;

		for (k = 0; (k < n) && !r.bad; k++)
		{
			char name[256];

			RdString(&r, name, sizeof(name));
			r.p += 20;
		}
	}

	/* texture section */
	r.p += 4;
	trn.slots = calloc(trn.numslots, sizeof(trnslot_t));

	for (i = 0; (i < trn.numslots) && !r.bad; i++)
	{
		int n = RdInt(&r), k;

		if ((n < 1) || (n > 256))
		{
			return false;
		}

		trn.slots[i].numentries = n;
		trn.slots[i].entries = calloc(n, sizeof(trnentry_t));

		for (k = 0; (k < n) && !r.bad; k++)
		{
			trnentry_t *e = &trn.slots[i].entries[k];
			char name[256];

			RdString(&r, name, sizeof(name));
			e->threshold = RdFloat(&r);
			e->sdiv = RdFloat(&r);
			e->tdiv = RdFloat(&r);
			e->fixs = RdFloat(&r);
			e->fixt = RdFloat(&r);
			e->whole = RdByte(&r);

			if (!r.bad)
			{
				e->image = TerrainImage(name);
			}
		}
	}

	return !r.bad;
}

/* ------------------------------------------------------------------
 * landscape
 * ------------------------------------------------------------------ */

static trnvert_t *
Vert(int row, int col)
{
	return &trn.verts[row * trn.numcols + col];
}

static float
Dist(const vec3_t a, const vec3_t b)
{
	vec3_t d;

	VectorSubtract(a, b, d);
	return sqrtf(DotProduct(d, d));
}

/* one texture range (s or t) by the rules of SPEC-410 Texture coordinates */
static void
Range(float fixed, byte whole, float divisor, float length, float start, float *r0, float *r1)
{
	float q = (divisor != 0) ? length / divisor : 0;

	if (fixed != 0)
	{
		*r0 = 0;
		*r1 = fixed;
	}
	else if (whole)
	{
		*r0 = 0;
		*r1 = (float)(int)q; /* truncated toward zero */
	}
	else
	{
		*r0 = start;
		*r1 = start + q;
	}
}

/* generates row `row` at x with the game clock t; prev = the row 1 S behind it (-1: none) */
static void
GenerateRow(int row, float x, int prev, float t)
{
	int c;

	for (c = 0; c < trn.numcols; c++)
	{
		const trncolumn_t *col = &trn.columns[c];
		trnvert_t *v = Vert(row, c);
		float rnd;
		int k;

		v->pos[0] = x;
		v->pos[1] = FlRand(col->ymin, col->ymax) + col->lw[2] * sinf(col->lw[0] * t) +
			col->lw[3] * sinf(col->lw[1] * t) - trn.latoffset;
		v->pos[2] = FlRand(col->zmin, col->zmax) + col->hw[2] * sinf(col->hw[0] * t) +
			col->hw[3] * sinf(col->hw[1] * t) - trn.hgtoffset;

		/* texture: the first entry of the column's slot whose threshold is >= r */
		rnd = FlRand(0, 1);
		v->entry = NULL;

		if ((col->slot >= 0) && (col->slot < trn.numslots))
		{
			const trnslot_t *s = &trn.slots[col->slot];

			for (k = 0; k < s->numentries; k++)
			{
				if (s->entries[k].threshold >= rnd)
				{
					v->entry = &s->entries[k];
					break;
				}
			}
		}

		v->bright = 0;
		v->behind = false;
	}

	/* texture ranges, once per generated row (they need the neighbours) */
	for (c = 0; c < trn.numcols; c++)
	{
		const trncolumn_t *col = &trn.columns[c];
		trnvert_t *v = Vert(row, c);
		const trnentry_t *e = v->entry;
		float ls, lt;

		v->s0 = v->s1 = v->t0 = v->t1 = 0;

		if (!e)
		{
			continue;
		}

		/* the last column has no next vertex; its s range is never used */
		ls = (c + 1 < trn.numcols) ? Dist(v->pos, Vert(row, c + 1)->pos) : 0;
		Range(e->fixs, e->whole, e->sdiv, ls, v->pos[1], &v->s0, &v->s1);

		/* the row built first has no neighbour behind it: its t range only matters with fixed or
		   whole repeats until it is regenerated (it is the rearmost row) */
		lt = (prev >= 0) ? Dist(v->pos, Vert(prev, c)->pos) : 0;
		Range(e->fixt, e->whole, e->tdiv, lt, t, &v->t0, &v->t1);

		if (col->flags & 0x2)
		{
			float tmp = v->s0;

			v->s0 = v->s1;
			v->s1 = tmp;
			tmp = v->t0;
			v->t0 = v->t1;
			v->t1 = tmp;
		}
	}
}

static void
PlaceClouds(void)
{
	int i, j;

	for (i = 0; i < TRN_NUM_CLOUDS; i++)
	{
		trncloud_t *cl = &trn.clouds[i];
		float h, len;
		vec3_t d, p;
		int k;

		for (;;)
		{
			int near2000 = 0;
			qboolean reject = false;

			cl->centre[0] = FlRand(-TRN_CLOUD_EXTENT, TRN_CLOUD_EXTENT);
			cl->centre[1] = FlRand(-TRN_CLOUD_EXTENT, TRN_CLOUD_EXTENT);

			for (j = 0; j < i; j++)
			{
				float dx = trn.clouds[j].centre[0] - cl->centre[0];
				float dy = trn.clouds[j].centre[1] - cl->centre[1];
				float dd = sqrtf(dx * dx + dy * dy);

				if (dd < 800)
				{
					reject = true;
					break;
				}

				if ((dd < 2000) && (++near2000 >= 3))
				{
					reject = true;
					break;
				}
			}

			/* the lateral value is read from the first cloud of the list (SPEC-410 Clouds) */
			if (!reject && (fabsf(trn.clouds[0].centre[1]) > 8191) && (FlRand(0, 1) < 2.0f / 3.0f))
			{
				reject = true;
			}

			if (!reject)
			{
				break;
			}
		}

		cl->centre[2] = FlRand(3000, 5000);
		h = 2 * FlRand(1200, 1800);

		/* horizontal unit direction from two uniform values (third component taken as 0) */
		d[0] = FlRand(-1, 1);
		d[1] = FlRand(-1, 1);
		d[2] = 0;
		len = sqrtf(d[0] * d[0] + d[1] * d[1]);

		if (len > 0)
		{
			d[0] /= len;
			d[1] /= len;
		}
		else
		{
			d[0] = 1;
		}

		p[0] = d[1];
		p[1] = -d[0];
		p[2] = 0;

		for (k = 0; k < 4; k++)
		{
			static const float ab[4][2] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
			int a;

			for (a = 0; a < 3; a++)
			{
				cl->corner[k][a] = cl->centre[a] + h * (ab[k][0] * d[a] + ab[k][1] * p[a]);
			}
		}

		cl->vel[0] = -400;
		cl->vel[1] = FlRand(-40, 40);
		cl->vel[2] = 0;
	}
}

static void
Build(void)
{
	int i;
	float t = Clock();

	trn.verts = calloc(trn.numrows * trn.numcols, sizeof(trnvert_t));
	PlaceClouds();

	for (i = 0; i < trn.numrows; i++)
	{
		GenerateRow(i, (float)(i - trn.numrows / 2) * trn.spacing, i - 1, t);
	}

	trn.rear = 0;
	trn.cloudimage = GL3_SoF_FindImage("textures/uganda/cloud", SOFIMG_WALL, true);

	if (!trn.cloudimage)
	{
		trn.cloudimage = GL3_SoF_NoTexture();
	}

	trn.built = true;
}

/* SPEC-002 slot 0x38 (and console "terrain" through the engine) */
void
GL3_Terrain_Select(const char *name)
{
	char path[MAX_QPATH];
	byte *buf = NULL;
	int len;

	GL3_Terrain_Free();

	if (!name || !name[0] || !strcmp(name, "(null)"))
	{
		return;
	}

	Com_sprintf(path, sizeof(path), "env/%s.trn", name);
	len = ri.FS_LoadFile(path, (void **)&buf);

	if (!buf)
	{
		Com_Printf("%s isn't a valid terrain set name\n", name);
		return;
	}

	if ((len < 1) || (buf[0] != '"'))
	{
		Com_Printf("Invalid version for Terrain Set %s\n", name);
		ri.FS_FreeFile(buf);
		return;
	}

	if (!Parse(buf, len))
	{
		/* OpenMerc: the original trusts the counts; a short or inconsistent file is refused here */
		Com_DPrintf("Terrain set %s: bad layout\n", name);
		ri.FS_FreeFile(buf);
		GL3_Terrain_Free();
		return;
	}

	ri.FS_FreeFile(buf);
	trn.loaded = true;
	trn.prevclock = Clock();

	if (trn.active)
	{
		Build();
	}
}

/* ------------------------------------------------------------------
 * per frame
 * ------------------------------------------------------------------ */

static gl3image_t *
Usable(gl3image_t *image)
{
	/* an image freed since the selection (only if the set was not selected again) */
	if (!image || !image->registration_sequence || !image->texnum)
	{
		return GL3_SoF_NoTexture();
	}

	return image;
}

static void
SetCorner(gl3fxvtx_t *o, const trnvert_t *v, float s, float t)
{
	float b = v->bright;

	VectorCopy(v->pos, o->pos);
	o->st[0] = s;
	o->st[1] = t;
	o->rgba[0] = 0.46f * b;
	o->rgba[1] = 0.89f * b;
	o->rgba[2] = b;
	o->rgba[3] = 1;
}

/* draws the landscape and the clouds; called by the world pass after the sky box */
void
GL3_Terrain_Draw(void)
{
	vec3_t forward, right, up;
	float clock, dt, vx, vy;
	int i, c, row, n;
	gl3fxstate_t st;
	qboolean fog;
	gl3image_t *drawn[64];
	int numdrawn = 0;

	trn_lastclock = Clock();

	if (!trn.loaded || !trn.active || !trn.built)
	{
		return;
	}

	/* r_speeds "tp": the count of the last frame that drew a terrain (black box: the original keeps
	   printing the old count after the set is switched off) */
	gl3_c_terrainpolys = 0;

	n = trn.numrows;
	clock = Clock();
	dt = clock - trn.prevclock;
	trn.prevclock = clock;

	if (dt < 0)
	{
		dt = 0;
	}
	else if (dt > 0.1f)
	{
		dt = 0.1f;
	}

	/* 2. scroll */
	if (dt != 0)
	{
		for (i = 0; i < n * trn.numcols; i++)
		{
			trn.verts[i].pos[0] -= trn.speed * dt;
		}
	}

	/* 3., 4. brightness and the behind test */
	vx = r_newrefdef.vieworg[0];
	vy = r_newrefdef.vieworg[1];
	AngleVectors(r_newrefdef.viewangles, forward, right, up);

	for (i = 0; i < n * trn.numcols; i++)
	{
		trnvert_t *v = &trn.verts[i];
		vec3_t d;

		if (r_fullbright->value)
		{
			v->bright = 1;
		}
		else
		{
			float m = 10000 - (fabsf(v->pos[0] - vx) + fabsf(v->pos[1] - vy));

			v->bright = 0.5f * ((m > 0) ? m * 0.0001f : 0);
		}

		VectorSubtract(v->pos, r_newrefdef.vieworg, d);
		v->behind = DotProduct(d, forward) < 0;
	}

	/* 5. recycling: at most one row per frame */
	if (Vert(trn.rear, 0)->pos[0] < vx - (float)n * trn.spacing / 2)
	{
		int front = (trn.rear + n - 1) % n;
		int old = trn.rear;

		trn.rear = (trn.rear + 1) % n;
		GenerateRow(old, Vert(old, 0)->pos[0] + (float)n * trn.spacing, front, clock);
	}

	/* 6. quads */
	GL3_Draw3DBatchesNow();
	fog = (trn.fogend >= 1.0f);

	if (fog && trn.clearflag)
	{
		GL3_FX_Flush();
		glDepthMask(GL_TRUE);
		glClearColor(trn.fogcolor[0], trn.fogcolor[1], trn.fogcolor[2], 0);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glClearColor(1, 0, 0.5, 0.5);
	}

	GL3_FX_DefaultState(&st);
	st.blend = 1;
	st.alphatest = true;
	st.depthtest = true;
	st.depthwrite = true;
	st.culling = false;

	if (fog)
	{
		st.fogmode = 1;
		st.fogstart = trn.fogstart;
		st.fogend = trn.fogend;
		st.fogcolor[0] = trn.fogcolor[0];
		st.fogcolor[1] = trn.fogcolor[1];
		st.fogcolor[2] = trn.fogcolor[2];
		st.fogcolor[3] = 0;
	}

	/* the quads of one image at a time (the batch changes texture only between images) */
	for (;;)
	{
		gl3image_t *image = NULL;
		int k;

		for (row = 0; (row < n) && !image; row++)
		{
			for (c = 0; c + 1 < trn.numcols; c++)
			{
				const trnvert_t *v = Vert(row, c);
				gl3image_t *im;

				if (!v->entry)
				{
					continue;
				}

				im = Usable(v->entry->image);

				for (k = 0; k < numdrawn; k++)
				{
					if (drawn[k] == im)
					{
						break;
					}
				}

				if (k == numdrawn)
				{
					image = im;
					break;
				}
			}
		}

		if (!image || (numdrawn >= (int)ARRLEN(drawn)))
		{
			break;
		}

		drawn[numdrawn++] = image;
		st.texnum = image->texnum;
		GL3_FX_SetState(&st);

		for (row = 0; row < n; row++)
		{
			int ahead = (row + 1) % n;

			if (ahead == trn.rear)
			{
				continue; /* no quad across the seam between the frontmost and the rearmost row */
			}

			for (c = 0; c + 1 < trn.numcols; c++)
			{
				const trnvert_t *v = Vert(row, c);
				const trnvert_t *v2 = Vert(row, c + 1);
				const trnvert_t *v3 = Vert(ahead, c + 1);
				const trnvert_t *v4 = Vert(ahead, c);
				gl3fxvtx_t q[4];

				if (!v->entry || (Usable(v->entry->image) != image))
				{
					continue;
				}

				if (v->behind && v2->behind && v3->behind && v4->behind)
				{
					continue;
				}

				SetCorner(&q[0], v, v->s0, v->t0);
				SetCorner(&q[1], v2, v->s0, v->t1);
				SetCorner(&q[2], v3, v->s1, v->t1);
				SetCorner(&q[3], v4, v->s1, v->t0);
				GL3_FX_Add(q, 4, GL_TRIANGLE_FAN);
				gl3_c_terrainpolys++;
			}
		}
	}

	GL3_FX_Flush();

	if (fog)
	{
		/* fog ends with the quads; the clear flag also clears depth (and colour with gl_clear) */
		if (trn.clearflag)
		{
			glDepthMask(GL_TRUE);
			glClear(gl_clear->value ? (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) : GL_DEPTH_BUFFER_BIT);
		}
	}

	/* 7. clouds: move, wrap, draw (never fogged, no depth writes) */
	for (i = 0; i < TRN_NUM_CLOUDS; i++)
	{
		trncloud_t *cl = &trn.clouds[i];
		int k, a;

		for (k = 0; k < 4; k++)
		{
			VectorMA(cl->corner[k], dt, cl->vel, cl->corner[k]);
			cl->corner[k][2] = 2500 + 1000 * (TRN_CLOUD_EXTENT - fabsf(cl->corner[k][0])) / TRN_CLOUD_EXTENT;
		}

		VectorMA(cl->centre, dt, cl->vel, cl->centre);

		for (a = 0; a < 2; a++)
		{
			float shift = 0;

			if (cl->centre[a] > TRN_CLOUD_EXTENT)
			{
				shift = -TRN_CLOUD_WRAP;
			}
			else if (cl->centre[a] < -TRN_CLOUD_EXTENT)
			{
				shift = TRN_CLOUD_WRAP;
			}

			if (shift != 0)
			{
				cl->centre[a] += shift;

				for (k = 0; k < 4; k++)
				{
					cl->corner[k][a] += shift;
				}
			}
		}
	}

	GL3_FX_DefaultState(&st);
	st.texnum = Usable(trn.cloudimage)->texnum;
	st.blend = 1;
	st.alphatest = true;
	st.depthtest = true;
	st.depthwrite = false;
	st.culling = false;
	GL3_FX_SetState(&st);

	for (i = 0; i < TRN_NUM_CLOUDS; i++)
	{
		static const float cst[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
		gl3fxvtx_t q[4];
		int k;

		for (k = 0; k < 4; k++)
		{
			VectorCopy(trn.clouds[i].corner[k], q[k].pos);
			q[k].st[0] = cst[k][0];
			q[k].st[1] = cst[k][1];
			q[k].rgba[0] = q[k].rgba[1] = q[k].rgba[2] = q[k].rgba[3] = 1;
		}

		GL3_FX_Add(q, 4, GL_TRIANGLE_FAN);
		gl3_c_terrainpolys++;
	}

	GL3_FX_Flush();
}
