/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GFX-modern r_om_ssr: what a world face is made of and how it
 * reflects. SoF has no material data beyond a surface sound id, so it is
 * put together from three sources:
 *
 *  - the material table (r_om_materials, default materials/om_materials.txt):
 *    the parameters of each class and the class of every texture of the game,
 *    sorted offline from the texture name, the SoF surface material, the
 *    artist's detail texture (d_stonerough, d_metalsmooth, d_tile, env maps)
 *    and the texels (share of rust colours); "tex <pattern> <class>" lines
 *    with '*' cover families of textures. A loose file in the game folder
 *    overrides the one in the pak; r_om_materials_reload reads it again in
 *    the game, r_om_material prints what the face under the crosshair is.
 *  - for textures missing from the table (mods, replacements): the same
 *    rules in short (name words, SoF material, detail texture, rust share).
 *  - a material map per world texture, made from its texels when it loads
 *    (GL_TEXTURE14, RGBA8 with mipmaps): R gloss (dark grout, cracks and
 *    grime spots darker than their surroundings and gritty patches are dull,
 *    the clean surface keeps the class's shine), G height from the relative
 *    brightness (rain water gathers in the low parts), B rust and dirt
 *    colours (stains on metal), A the panes of painted windows (glass shines
 *    only there, not on its frame, sill or the wall around it).
 *
 * Class parameters go to the world shaders as two vec4 arrays (omMatA:
 * floor reflectivity, roughness, wall and ceiling factors; omMatB: weight of
 * the gloss map, weight of the stains, natural dampness, how much rain
 * water the surface holds). The class index travels in the emissive slot of
 * the face as a negative number (gl3_surf.c); 5 pictures, 7 liquids and 11
 * mirrors are handled specially by the shaders and the mirror pass.
 */

#include "header/local.h"

#ifdef _MSC_VER
#include <intrin.h> /* _InterlockedCompareExchange */
#include <direct.h> /* _mkdir */
#include <io.h>     /* _findfirst: the old cache's files */
#else
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h> /* rmdir */
#endif
#include "../../../common/unzip/miniz/miniz.h"

#define MAT_MAXCLASS 24
#define MAT_UNIT 14
#define MAT_HASH 2048

extern vec3_t vpn; /* gl3_main.c, the view of this frame */

enum
{
	MC_UNKNOWN, MC_STONE, MC_WOOD, MC_METAL, MC_GLASS, MC_PICTURE, MC_GROUND, MC_LIQUID, MC_POLISHED, MC_CHROME,
	MC_RUSTY, MC_MIRROR, MC_DAMP, MC_SMOOTHCON, MC_VARNISH, MC_TILES, MC_ICE, MC_FABRIC, MC_PLASTIC, MC_SKY,
	MC_BUILTIN
};

typedef struct
{
	char name[16];
	float a[4];   /* floor reflectivity, roughness, wall factor, ceiling factor */
	float b[4];   /* gloss map weight, stain weight, natural dampness, rain water held */
} matclass_t;

/* the defaults when the table is missing; the table usually sets the same */
static const matclass_t builtin[MC_BUILTIN] = {
	{"unknown",   {0.00f, 0.70f, 0.30f, 0.15f}, {1.0f, 0.5f, 0.0f, 1.0f}},
	{"stone",     {0.00f, 0.75f, 0.30f, 0.15f}, {1.0f, 0.5f, 0.0f, 1.0f}},
	{"wood",      {0.00f, 0.65f, 0.30f, 0.15f}, {1.0f, 0.5f, 0.0f, 0.6f}},
	{"metal",     {0.14f, 0.35f, 0.60f, 0.30f}, {1.0f, 1.0f, 0.0f, 0.8f}}, /* visible on walls too: SSR replaces the env shine */
	{"glass",     {0.45f, 0.03f, 1.00f, 1.00f}, {1.0f, 0.0f, 0.0f, 0.0f}},
	{"picture",   {0.00f, 0.60f, 0.30f, 0.15f}, {0.0f, 0.0f, 0.0f, 0.0f}},
	{"ground",    {0.00f, 0.85f, 0.30f, 0.15f}, {1.0f, 0.0f, 0.0f, 0.35f}},
	{"liquid",    {0.45f, 0.09f, 1.00f, 1.00f}, {0.0f, 0.0f, 0.0f, 0.0f}},
	{"polished",  {0.28f, 0.06f, 0.35f, 0.15f}, {1.0f, 0.6f, 0.0f, 1.0f}},
	{"chrome",    {0.30f, 0.12f, 0.60f, 0.30f}, {1.0f, 1.0f, 0.0f, 0.8f}},
	{"rusty",     {0.00f, 0.80f, 0.30f, 0.15f}, {1.0f, 0.0f, 0.0f, 0.7f}},
	{"mirror",    {0.90f, 0.00f, 1.00f, 1.00f}, {0.0f, 0.0f, 0.0f, 0.0f}},
	{"damp",      {0.00f, 0.50f, 0.30f, 0.15f}, {1.0f, 0.5f, 1.0f, 1.0f}},
	{"smoothcon", {0.04f, 0.45f, 0.30f, 0.15f}, {1.0f, 0.5f, 0.0f, 1.0f}},
	{"varnish",   {0.10f, 0.30f, 0.40f, 0.15f}, {1.0f, 0.5f, 0.0f, 0.8f}},
	{"tiles",     {0.15f, 0.14f, 0.35f, 0.15f}, {1.0f, 0.6f, 0.0f, 1.0f}},
	{"ice",       {0.22f, 0.08f, 0.60f, 0.30f}, {0.6f, 0.0f, 0.0f, 0.5f}},
	{"fabric",    {0.00f, 0.90f, 0.30f, 0.15f}, {0.0f, 0.0f, 0.0f, 0.0f}},
	{"plastic",   {0.10f, 0.25f, 0.50f, 0.30f}, {1.0f, 0.5f, 0.0f, 0.8f}},
	{"sky",       {0.00f, 1.00f, 0.00f, 0.00f}, {0.0f, 0.0f, 0.0f, 0.0f}},
};

typedef struct mattex_s
{
	struct mattex_s *next;
	int cls;
	char name[1];
} mattex_t;

typedef struct
{
	char pat[MAX_QPATH];
	int cls;
} matpat_t;

static struct
{
	matclass_t cls[MAT_MAXCLASS];
	int numcls;
	mattex_t *hash[MAT_HASH];
	matpat_t *pats;
	int numpats, cappats, numtex;
	int gen;
	qboolean loaded;

	GLuint *maps;   /* material map by colour texture number */
	float *rust;    /* share of rust-coloured texels by colour texture number (-1 = not measured) */
	int count;
	GLuint flat;
} M;

static cvar_t *r_om_materials;
static qboolean matcmds;

/* ------------------------------------------------------------------
 * the table
 * ------------------------------------------------------------------ */

/* "textures/iraq/sand.m32" -> "iraq/sand" */
static void
MatKey(const char *path, char *out, size_t size)
{
	char *dot, *slash;

	if (!Q_strncasecmp(path, "textures/", 9))
	{
		path += 9;
	}

	Q_strlcpy(out, path, size);
	Q_strlwr(out);
	dot = strrchr(out, '.');
	slash = strrchr(out, '/');

	if (dot && (!slash || dot > slash))
	{
		*dot = 0;
	}
}

static unsigned
MatHash(const char *s)
{
	unsigned h = 5381;

	while (*s)
	{
		h = h * 33 + (unsigned char)*s++;
	}

	return h & (MAT_HASH - 1);
}

/* '*' matches any run of characters */
static qboolean
MatGlob(const char *p, const char *s)
{
	for (; *p; p++, s++)
	{
		if (*p == '*')
		{
			for (p++; ; s++)
			{
				if (MatGlob(p, s))
				{
					return true;
				}

				if (!*s)
				{
					return false;
				}
			}
		}

		if (*s != *p)
		{
			return false;
		}
	}

	return !*s;
}

static int
MatClassByName(const char *name)
{
	int i;

	for (i = 0; i < M.numcls; i++)
	{
		if (!Q_stricmp(M.cls[i].name, name))
		{
			return i;
		}
	}

	return -1;
}

static void
MatClear(void)
{
	int i;

	for (i = 0; i < MAT_HASH; i++)
	{
		while (M.hash[i])
		{
			mattex_t *t = M.hash[i];

			M.hash[i] = t->next;
			free(t);
		}
	}

	free(M.pats);
	M.pats = NULL;
	M.numpats = M.cappats = M.numtex = 0;
	memcpy(M.cls, builtin, sizeof(builtin));
	M.numcls = MC_BUILTIN;
}

static void
MatAddTex(const char *name, int cls)
{
	char key[MAX_QPATH];

	MatKey(name, key, sizeof(key));

	if (strchr(key, '*'))
	{
		if (M.numpats == M.cappats)
		{
			matpat_t *p = realloc(M.pats, sizeof(matpat_t) * (M.cappats + 64));

			if (!p)
			{
				return;
			}

			M.pats = p;
			M.cappats += 64;
		}

		Q_strlcpy(M.pats[M.numpats].pat, key, sizeof(M.pats[M.numpats].pat));
		M.pats[M.numpats].cls = cls;
		M.numpats++;
	}
	else
	{
		unsigned h = MatHash(key);
		mattex_t *t;

		for (t = M.hash[h]; t; t = t->next)
		{
			if (!strcmp(t->name, key))
			{
				t->cls = cls; /* a later line wins */
				return;
			}
		}

		t = malloc(sizeof(mattex_t) + strlen(key));

		if (!t)
		{
			return;
		}

		strcpy(t->name, key);
		t->cls = cls;
		t->next = M.hash[h];
		M.hash[h] = t;
		M.numtex++;
	}
}

/* one line: "class <name> refl rough walls ceil detail stain damp soak" or "tex <name|pattern> <class>" */
static void
MatLine(char *line, int lineno)
{
	char *tok[12];
	int n = 0;
	char *p = line, *c = strstr(line, "//");

	if (c)
	{
		*c = 0;
	}

	if ((c = strchr(line, '#')))
	{
		*c = 0;
	}

	while (n < 12)
	{
		while (*p == ' ' || *p == '\t' || *p == '\r')
		{
			p++;
		}

		if (!*p)
		{
			break;
		}

		tok[n++] = p;

		while (*p && *p != ' ' && *p != '\t' && *p != '\r')
		{
			p++;
		}

		if (*p)
		{
			*p++ = 0;
		}
	}

	if (!n)
	{
		return;
	}

	if (!Q_stricmp(tok[0], "class") && n == 10)
	{
		int k = MatClassByName(tok[1]), j;

		if (k < 0)
		{
			if (M.numcls >= MAT_MAXCLASS)
			{
				R_Printf(PRINT_ALL, "r_om_materials: line %d: too many classes\n", lineno);
				return;
			}

			k = M.numcls++;
			Q_strlcpy(M.cls[k].name, tok[1], sizeof(M.cls[k].name));
		}

		for (j = 0; j < 4; j++)
		{
			M.cls[k].a[j] = (float)atof(tok[2 + j]);
			M.cls[k].b[j] = (float)atof(tok[6 + j]);
		}
	}
	else if (!Q_stricmp(tok[0], "tex") && n == 3)
	{
		int k = MatClassByName(tok[2]);

		if (k < 0)
		{
			R_Printf(PRINT_ALL, "r_om_materials: line %d: unknown class %s\n", lineno, tok[2]);
			return;
		}

		MatAddTex(tok[1], k);
	}
	else
	{
		R_Printf(PRINT_ALL, "r_om_materials: line %d not understood\n", lineno);
	}
}

static void
MatLoad(void)
{
	char *buf = NULL, *text, *line;
	int len, lineno = 0;

	MatClear();
	M.loaded = true;
	M.gen++;

	len = ri.FS_LoadFile(r_om_materials->string, (void **)&buf);

	if (len <= 0 || !buf)
	{
		R_Printf(PRINT_ALL, "r_om_materials: %s not found, built-in classes only\n", r_om_materials->string);
		return;
	}

	text = malloc(len + 1);

	if (!text)
	{
		ri.FS_FreeFile(buf);
		return;
	}

	memcpy(text, buf, len);
	text[len] = 0;
	ri.FS_FreeFile(buf);

	for (line = text; line && *line; )
	{
		char *nl = strchr(line, '\n');

		if (nl)
		{
			*nl = 0;
		}

		MatLine(line, ++lineno);
		line = nl ? nl + 1 : NULL;
	}

	free(text);
	R_Printf(PRINT_DEVELOPER, "r_om_materials: %d classes, %d textures, %d patterns\n", M.numcls, M.numtex, M.numpats);
}

/* class from the table: exact name, then the patterns in file order; -1 = not listed */
static int
MatLookup(const char *key)
{
	mattex_t *t;
	int i;

	for (t = M.hash[MatHash(key)]; t; t = t->next)
	{
		if (!strcmp(t->name, key))
		{
			return t->cls;
		}
	}

	for (i = 0; i < M.numpats; i++)
	{
		if (MatGlob(M.pats[i].pat, key))
		{
			return M.pats[i].cls;
		}
	}

	return -1;
}

/* ------------------------------------------------------------------
 * textures missing from the table: the offline rules in short
 * ------------------------------------------------------------------ */

static qboolean
AnyWord(const char *s, const char *const *words)
{
	for (; *words; words++)
	{
		if (strstr(s, *words))
		{
			return true;
		}
	}

	return false;
}

static int
MatFallback(const gl3image_t *img, const char *key, int m)
{
	static const char *const liquid[] = {"water", "slime", "lava", "molten", "sewage", "acid", NULL};
	static const char *const glass[] = {"glass", "window", "wndw", NULL};
	/* not "icewall"/"ice_wall": the SoF ice walls are snow-covered rock and frost (sib1 icewall_c), sorted as
	   ground below; "ice_" alone also caught office_ / police_ */
	static const char *const ice[] = {"ice_tube", "icicle", "_ice", "frozen", NULL};
	static const char *const fabric[] = {"carpet", "rug", "rubber", "tire", "cloth", "tarp", "canvas", "curtain",
		"towel", "mattress", "paper", "cardboard", "sandbag", "cork", "wallpaper", "couch", "sofa", NULL};
	static const char *const ground[] = {"snow", "grass", "sand", "dirt", "mud", "gravel", "hay", "straw", "terrain",
		"soil", "leaves", "rubble", "caverock", "cavefloor", "moss", "icewall", "ice_wall", NULL};
	static const char *const damp[] = {"sewer", "sewcon", "bsew", "mildew", "gutter", "drain", "slimy", "wet", NULL};
	static const char *const rusty[] = {"rust", "dirty", "burn", "grime", "grate", "grating", "rivet", "metalrib",
		"damage", "scorch", "chainlink", "fence", "barbwire", "catwalk", "girder", "ibeam", NULL};
	static const char *const chrome[] = {"chrome", "brushed", "metal_clean", "metal_smooth", "stainless", "_sink",
		"faucet", "urinal", "toilet", NULL};
	static const char *const polished[] = {"marb", "polish", "granite", "lobby_floor", "pooltile", "checkfloor",
		"clean_floor", "kitchfloor", "hallfloor", "floordesign", "decfloor", "tilefloor", "terrazzo", "mosaic", NULL};
	static const char *const tiles[] = {"tile", "bathroom", "bath_", "linol", "vinyl", NULL};
	static const char *const varnish[] = {"parquet", "varnish", "woodfloor", "hardwood", "tablewood", "deskwood",
		"desktop", "counter", NULL};
	static const char *const smoothcon[] = {"cementslab", "cementplain", "epoxy", "ramp_floor", "parking", NULL};
	const char *base = strrchr(key, '/');
	const char *det = (img->detail && img->detail->name[0]) ? img->detail->name : "";
	qboolean metalmat = (m == 1) || (m >= 28 && m <= 31) || (m == 42);
	float rust = ((int)img->texnum < M.count && M.rust) ? M.rust[img->texnum] : -1.0f;
	int c;

	base = base ? base + 1 : key;

	if (m == 10 || m == 11 || m == 12 || m == 27 || m == 36 || AnyWord(base, liquid))
	{
		return MC_LIQUID;
	}

	if (m == 37 || AnyWord(base, glass))
	{
		return strstr(base, "mirror") && m == 37 ? MC_MIRROR : MC_GLASS;
	}

	if (m == 32 || m == 38 || m == 39 || m == 40)
	{
		return MC_PICTURE;
	}

	if (AnyWord(base, ice))
	{
		return MC_ICE;
	}

	if (m == 41 || AnyWord(base, fabric))
	{
		return MC_FABRIC;
	}

	if ((m >= 2 && m <= 9) || m == 13 || m == 25 || m == 26 || AnyWord(base, ground))
	{
		return MC_GROUND;
	}

	if (AnyWord(base, damp))
	{
		return MC_DAMP;
	}

	if (AnyWord(base, rusty))
	{
		return MC_RUSTY;
	}

	if (AnyWord(base, chrome))
	{
		return MC_CHROME;
	}

	c = AnyWord(base, polished) ? MC_POLISHED : AnyWord(base, tiles) ? MC_TILES : AnyWord(base, varnish) ? MC_VARNISH :
		AnyWord(base, smoothcon) ? MC_SMOOTHCON : -1;

	if (c >= 0)
	{
		/* smooth words on a metal face: panels, grids, shelves; a rough detail texture keeps stone honest */
		if (metalmat)
		{
			return MC_METAL;
		}

		if ((c == MC_POLISHED || c == MC_TILES) && (strstr(det, "rough") || strstr(det, "cracked") ||
			strstr(det, "holey") || strstr(det, "stucco")))
		{
			return MC_STONE;
		}

		if (c == MC_VARNISH && (strstr(det, "rough") || strstr(det, "ribbed") || strstr(det, "metal") || strstr(det, "stone")))
		{
			return MC_WOOD;
		}

		return c;
	}

	if (m >= 14 && m <= 16)
	{
		return MC_WOOD;
	}

	if (metalmat)
	{
		if (strstr(det, "metalrough") || strstr(det, "metalribbed") || rust > 0.45f)
		{
			return MC_RUSTY;
		}

		return strstr(det, "env1c") ? MC_CHROME : MC_METAL;
	}

	return strstr(det, "d_tile") ? MC_TILES : MC_STONE;
}

/* ------------------------------------------------------------------
 * the class of a face
 * ------------------------------------------------------------------ */

/* the class of a face (texture + SoF surface material, high byte of the texinfo flags, SPEC-401) as the negative
   value the world shader takes in the emissive slot of non-light faces; 0 = unknown */
float
GL3_Mat_SurfClass(const msurface_t *surf)
{
	gl3image_t *img;
	int m, c;

	if (!surf->texinfo || (surf->texinfo->flags & SURF_LIGHT))
	{
		return 0.0f;
	}

	if (!M.loaded)
	{
		GL3_Mat_Init();
		MatLoad();
	}

	m = (surf->texinfo->flags >> 24) & 0xff;
	img = surf->texinfo->image;

	if (!img)
	{
		return -(float)MC_STONE;
	}

	if (!img->omgloss || img->omglossgen != M.gen)
	{
		char key[MAX_QPATH];

		MatKey(img->name, key, sizeof(key));
		c = MatLookup(key);

		if (c < 0)
		{
			c = MatFallback(img, key, m);
		}

		img->omgloss = c + 1;
		img->omglossgen = M.gen;
	}

	c = img->omgloss - 1;

	/* a mirror only where the surface is glass (the mirror pass takes the faces of class 11) */
	if (c == MC_MIRROR && m != 37)
	{
		c = MC_CHROME;
	}

	/* the artist's environment map shine (enamel signs, news stands, tech walls): with r_om_ssr it is replaced by the
	   traced reflection (gl3_detail.c), so the face reflects at least like gloss plastic */
	if (img->detail && (img->detail->softype == SOFIMG_ENV) && (c < MC_BUILTIN) &&
		(((c < M.numcls) ? M.cls[c].a[0] : builtin[c].a[0]) < 0.1f))
	{
		c = MC_PLASTIC;
	}

	/* computers and screens, lit glass panels, vending machines, paintings: flat, no shine */
	if (m == 32 || m == 38 || m == 39 || m == 40)
	{
		c = MC_PICTURE;
	}

	return -(float)c;
}

/* the reflectivity r_om_ssr gives a face with world normal n: its class's floor value, scaled for walls and ceilings
   as the world shaders do */
float
GL3_Mat_SurfReflect(const msurface_t *surf, const vec3_t n)
{
	int c = (int)(-GL3_Mat_SurfClass(surf) + 0.5f);
	const matclass_t *mc;

	if (c <= 0 || c >= MAT_MAXCLASS)
	{
		return 0.0f;
	}

	mc = (c < M.numcls) ? &M.cls[c] : (c < MC_BUILTIN) ? &builtin[c] : NULL;

	if (!mc)
	{
		return 0.0f;
	}

	return mc->a[0] * ((n[2] > 0.7f) ? 1.0f : (n[2] < -0.7f) ? mc->a[3] : mc->a[2]);
}

/* ------------------------------------------------------------------
 * material maps
 * ------------------------------------------------------------------ */

/* r_om_loadtime: where the material maps' time goes (gl3_model.c clock) */
double gl3_matt[5], gl3_matx[6];
#define MatClock GL3_LoadClockMs

/* index i wrapped into 0..n-1 (textures tile; r may exceed a short side) */
static inline int
MatWrap(int i, int n)
{
	while (i < 0)
	{
		i += n;
	}

	while (i >= n)
	{
		i -= n;
	}

	return i;
}

/* box blur with wrap-around (textures tile), twice for a tent shape. Running sums; the wrap is a compare, not a
   modulo per texel (two divisions per texel and pass made the material maps 8 s of a 16 s map load with HD textures) */
/* the scratch memory of one measuring job: every array of MatCompute and MatBlur (about twenty of up to 256 KB a
   texture) is cut from it, reset per texture, instead of the heap (no lock, no fresh pages per texture) */
#define MAT_ARENA (12 * 1024 * 1024)
#define MAT_ARENAS 8

typedef struct
{
	volatile long busy;
	char *base;
	size_t used;
} matarena_t;

static matarena_t matarenas[MAT_ARENAS];

#if defined(_MSC_VER)
static __declspec(thread) matarena_t *matcur;
#else
static __thread matarena_t *matcur;
#endif

static void *
MatAlloc(size_t n)
{
	matarena_t *a = matcur;

	n = (n + 63) & ~(size_t)63;

	if (a && a->base && (a->used + n <= MAT_ARENA))
	{
		void *p = a->base + a->used;

		a->used += n;
		return p;
	}

	return malloc(n); /* a texture larger than the arena: the heap as before */
}

static void
MatFree(void *p)
{
	matarena_t *a = matcur;

	if (!p || (a && a->base && ((char *)p >= a->base) && ((char *)p < a->base + MAT_ARENA)))
	{
		return; /* the arena is reset with the next texture */
	}

	free(p);
}

/* an arena for this thread's job (one per worker; the main thread outside a batch takes one too) */
static void
MatArenaBegin(void)
{
	int i;

	matcur = NULL;

	for (i = 0; i < MAT_ARENAS; i++)
	{
#ifdef _MSC_VER
		if (_InterlockedCompareExchange(&matarenas[i].busy, 1, 0) == 0)
#else
		if (__sync_bool_compare_and_swap(&matarenas[i].busy, 0, 1))
#endif
		{
			if (!matarenas[i].base)
			{
				matarenas[i].base = malloc(MAT_ARENA);
			}

			matarenas[i].used = 0;
			matcur = &matarenas[i];
			return;
		}
	}
}

static void
MatArenaEnd(void)
{
	if (matcur)
	{
		matcur->busy = 0;
		matcur = NULL;
	}
}

/* the arenas go at the end of a registration */
static void
MatArenasFree(void)
{
	int i;

	for (i = 0; i < MAT_ARENAS; i++)
	{
		free(matarenas[i].base);
		matarenas[i].base = NULL;
		matarenas[i].busy = 0;
	}
}

static float *
MatBlur(const float *src, int w, int h, int r)
{
	float *a = MatAlloc(sizeof(float) * w * h), *b = MatAlloc(sizeof(float) * w * h), *colsum = MatAlloc(sizeof(float) * w);
	float inv = 1.0f / (2 * r + 1);
	int pass, x, y, k;
	double tb = MatClock();

	if (!a || !b || !colsum)
	{
		MatFree(a);
		MatFree(b);
		MatFree(colsum);
		return NULL;
	}

	memcpy(a, src, sizeof(float) * w * h);

	for (pass = 0; pass < 2; pass++)
	{
		for (y = 0; y < h; y++)
		{
			const float *row = a + (size_t)y * w;
			float *dst = b + (size_t)y * w;
			float s = 0;
			int add = MatWrap(r + 1, w), sub = MatWrap(-r, w);

			for (k = -r; k <= r; k++)
			{
				s += row[MatWrap(k, w)];
			}

			for (x = 0; x < w; x++)
			{
				dst[x] = s * inv;
				s += row[add] - row[sub];

				if (++add == w)
				{
					add = 0;
				}

				if (++sub == w)
				{
					sub = 0;
				}
			}
		}

		/* the columns row by row: a running sum per column (memory in order, the same sums as column by column) */
		{
			int add = MatWrap(r + 1, h), sub = MatWrap(-r, h);

			for (x = 0; x < w; x++)
			{
				colsum[x] = 0;
			}

			for (k = -r; k <= r; k++)
			{
				const float *row = b + (size_t)MatWrap(k, h) * w;

				for (x = 0; x < w; x++)
				{
					colsum[x] += row[x];
				}
			}

			for (y = 0; y < h; y++)
			{
				const float *ra = b + (size_t)add * w, *rs = b + (size_t)sub * w;
				float *dst = a + (size_t)y * w;

				for (x = 0; x < w; x++)
				{
					dst[x] = colsum[x] * inv;
					colsum[x] += ra[x] - rs[x];
				}

				if (++add == h)
				{
					add = 0;
				}

				if (++sub == h)
				{
					sub = 0;
				}
			}
		}
	}

	MatFree(b);
	MatFree(colsum);
	gl3_matt[1] += MatClock() - tb;
	return a;
}

static float
SStep(float e0, float e1, float x)
{
	float t = (x - e0) / (e1 - e0);

	t = t < 0 ? 0 : (t > 1 ? 1 : t);
	return t * t * (3 - 2 * t);
}

static void
MatStore(GLuint texnum, GLuint map, float rust)
{
	if ((int)texnum >= M.count)
	{
		int n = texnum + 1024, i;
		GLuint *p = realloc(M.maps, sizeof(GLuint) * n);
		float *r;

		if (!p)
		{
			glDeleteTextures(1, &map);
			return;
		}

		M.maps = p;
		r = realloc(M.rust, sizeof(float) * n);

		if (!r)
		{
			glDeleteTextures(1, &map);
			return;
		}

		M.rust = r;

		for (i = M.count; i < n; i++)
		{
			M.maps[i] = 0;
			M.rust[i] = -1.0f;
		}

		M.count = n;
	}

	if (M.maps[texnum])
	{
		glDeleteTextures(1, &M.maps[texnum]);
	}

	M.maps[texnum] = map;
	M.rust[texnum] = rust;
}

/*
 * The material map of a world texture (rgba: w x h, 4 bytes per texel). The map is measured at most 256 wide; the
 * texels are averaged down at once (cheap), the measuring itself runs as a job: during a registration (map load)
 * the jobs of 64 textures at a time run on all cores (GL3_ParallelFor) and the maps are uploaded on the main thread
 * (before: 8 s of a 16 s HD map load on one core). The same floats in the same order - the maps are unchanged.
 */
typedef struct
{
	GLuint texnum;
	int w, h;
	float *rgb;   /* averaged colour 0..1, 3 floats per texel */
	byte *out;    /* the map (MatCompute) */
	float rust;
} matjob_t;

#define MAT_BATCH 64

static matjob_t matjobs[MAT_BATCH];
static int nmatjobs;
static qboolean matbatch;

static void
MatCompute(matjob_t *j)
{
	float *L = NULL, *rm = NULL, *M1 = NULL, *M3 = NULL, *V1 = NULL, *V3 = NULL, *hf = NULL, *G = NULL, *st = NULL;
	float *tmp = NULL, *pane = NULL, *wm = NULL, hfmean = 0, rust = 0, scale;
	byte *out = NULL;
	int x, y, i, n, md, hist[256] = {0}, acc, panew = 1, panek = 1;
	int w = j->w, h = j->h;

	n = w * h;
	md = w > h ? w : h;
	L = MatAlloc(sizeof(float) * n);
	rm = MatAlloc(sizeof(float) * n);
	tmp = MatAlloc(sizeof(float) * n);
	wm = MatAlloc(sizeof(float) * n);
	out = malloc((size_t)n * 4);

	if (!L || !rm || !tmp || !wm || !out)
	{
		goto done;
	}

	for (y = 0; y < h; y++)
	{
		for (x = 0; x < w; x++)
		{
			const float *c3 = j->rgb + ((size_t)y * w + x) * 3;
			float r = c3[0], g = c3[1], b = c3[2], mx, mn, sat;

			L[y * w + x] = 0.299f * r + 0.587f * g + 0.114f * b;
			mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
			mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
			sat = mx > 0.03f ? (mx - mn) / mx : 0;
			/* brick and wooden window frames: red-brown and saturated (never a pane) */
			wm[y * w + x] = SStep(0.03f, 0.08f, r - g) * SStep(0.25f, 0.4f, sat);
			/* rust and dirt: red the largest, blue the smallest, saturated, neither black nor bright */
			rm[y * w + x] = (r >= g && g >= b && (g - b) <= (r - b) * 0.75f && sat > 0.3f && mx > 0.06f && mx < 0.85f)
				? 1.0f : 0.0f;
			rust += rm[y * w + x];
		}
	}

	rust /= n;


	/* surroundings at two scales: the wide one (stains, blotches), the narrow one (grout, cracks) */
	M1 = MatBlur(L, w, h, md / 16 > 2 ? md / 16 : 2);
	M3 = MatBlur(L, w, h, md / 48 > 1 ? md / 48 : 1);

	if (!M1 || !M3)
	{
		goto done;
	}

	for (i = 0; i < n; i++)
	{
		tmp[i] = (L[i] - M1[i]) * (L[i] - M1[i]);
	}

	V1 = MatBlur(tmp, w, h, md / 16 > 2 ? md / 16 : 2);

	for (i = 0; i < n; i++)
	{
		tmp[i] = (L[i] - M3[i]) * (L[i] - M3[i]);
	}

	V3 = MatBlur(tmp, w, h, md / 48 > 1 ? md / 48 : 1);

	/* grit: fine texel-to-texel change, compared with the texture's own mean */
	{
		float *b1 = MatBlur(L, w, h, 1);

		if (!b1)
		{
			goto done;
		}

		for (i = 0; i < n; i++)
		{
			tmp[i] = fabsf(L[i] - b1[i]);
		}

		MatFree(b1);
	}

	hf = MatBlur(tmp, w, h, 2);
	st = MatBlur(rm, w, h, 1);

	if (!V1 || !V3 || !hf || !st)
	{
		goto done;
	}

	for (i = 0; i < n; i++)
	{
		hfmean += hf[i];
	}

	hfmean = hfmean / n + 1.0e-3f;
	G = tmp; /* gloss before normalising */

	for (i = 0; i < n; i++)
	{
		float s1 = sqrtf(V1[i]), s3 = sqrtf(V3[i]);
		float dk1 = (M1[i] - L[i]) / fmaxf(1.5f * s1, 0.025f), dk3 = (M3[i] - L[i]) / fmaxf(1.5f * s3, 0.02f);
		float crev = fmaxf(SStep(0.35f, 1.4f, dk1), SStep(0.35f, 1.4f, dk3));
		float grit = SStep(1.1f, 2.5f, hf[i] / hfmean);

		G[i] = (1.0f - crev) * (1.0f - 0.5f * grit);
	}

	/* the gloss only at the scale of stains and wear (radius 1/16 of the texture): per texel it followed the
	   texture's light and dark pattern - reflections and lamp glints on the bright texels, matte outlines - an
	   embossed, cel-shaded look on doors and panels (owner, twice) */
	{
		float *gb = MatBlur(G, w, h, md / 16 > 2 ? md / 16 : 2);

		if (gb)
		{
			memcpy(G, gb, sizeof(float) * n);
			MatFree(gb);
		}
	}

	for (i = 0; i < n; i++)
	{
		hist[(int)(fminf(fmaxf(G[i], 0.0f), 1.0f) * 255.0f + 0.5f)]++;
	}

	/* the panes of painted windows (A): texels with no strong step to a neighbour within one texel and little
	   detail around them (frames, bars and sills of the 64-texel SoF windows are one or two texels wide), and no
	   red-brown brick or wood next to them. Measured at most 128 wide, so replacement textures give the same mask
	   as the originals (the thresholds are per texel) */
	{
		int k = 1, pw, ph, x2, y2, dx, dy;
		float *Lp, *Wp, *pb;

		while (md / k > 128)
		{
			k *= 2;
		}

		pw = w / k > 1 ? w / k : 1;
		ph = h / k > 1 ? h / k : 1;
		Lp = MatAlloc(sizeof(float) * pw * ph);
		Wp = MatAlloc(sizeof(float) * pw * ph);
		pane = MatAlloc(sizeof(float) * pw * ph);

		if (!Lp || !Wp || !pane)
		{
			MatFree(Lp);
			MatFree(Wp);
			goto done;
		}

		for (y2 = 0; y2 < ph; y2++)
		{
			for (x2 = 0; x2 < pw; x2++)
			{
				float s = 0, ww = 0;
				int a, c;

				for (c = 0; c < k; c++)
				{
					for (a = 0; a < k; a++)
					{
						s += L[(y2 * k + c) * w + x2 * k + a];
						ww += wm[(y2 * k + c) * w + x2 * k + a];
					}
				}

				Lp[y2 * pw + x2] = s / (k * k);
				Wp[y2 * pw + x2] = ww / (k * k);
			}
		}

		for (y2 = 0; y2 < ph; y2++)
		{
			for (x2 = 0; x2 < pw; x2++)
			{
				float near = 0, lo = 1e9f, hi = -1e9f, warm = 0;

				for (dy = -1; dy <= 1; dy++)
				{
					for (dx = -1; dx <= 1; dx++)
					{
						/* wrapped neighbours without a modulo per tap (the modulos were most of the material maps'
						   load time) */
						int xx = MatWrap(x2 + dx, pw), yy = MatWrap(y2 + dy, ph);
						int xn = (xx + 1 == pw) ? 0 : xx + 1, yn = (yy + 1 == ph) ? 0 : yy + 1;
						float v = Lp[yy * pw + xx];
						float s1 = fabsf(v - Lp[yy * pw + xn]), s2 = fabsf(v - Lp[yn * pw + xx]);

						near = fmaxf(near, fmaxf(s1, s2));
						lo = fminf(lo, v);
						hi = fmaxf(hi, v);
						warm = fmaxf(warm, Wp[yy * pw + xx]);
					}
				}

				pane[y2 * pw + x2] = (1.0f - SStep(0.035f, 0.08f, near)) * (1.0f - SStep(0.06f, 0.14f, hi - lo)) *
					(1.0f - warm);
			}
		}

		MatFree(Lp);
		MatFree(Wp);
		pb = MatBlur(pane, pw, ph, 1);

		if (!pb)
		{
			goto done;
		}

		for (i = 0; i < pw * ph; i++)
		{
			pane[i] = SStep(0.35f, 0.7f, pb[i]);
		}

		MatFree(pb);
		panew = pw;
		panek = k;
	}
	/* the cleanest 15 % keep the full shine of the class */	for (i = 255, acc = 0; i > 0 && acc < n * 15 / 100; i--)
	{
		acc += hist[i];
	}

	scale = 255.0f / (float)(i > 32 ? i : 32);

	for (i = 0; i < n; i++)
	{
		float s3 = sqrtf(V3[i]);
		float g = G[i] * scale, ht = 0.5f + 0.5f * (L[i] - M3[i]) / fmaxf(2.5f * s3, 0.03f);
		byte *o = out + (size_t)i * 4;

		o[0] = (byte)(fminf(g, 1.0f) * 255.0f + 0.5f);
		o[1] = (byte)(fminf(fmaxf(ht, 0.0f), 1.0f) * 255.0f + 0.5f);
		o[2] = (byte)(fminf(st[i], 1.0f) * 255.0f + 0.5f);
		{
			int px = (i % w) / panek, py = (i / w) / panek;

			px = px < panew ? px : panew - 1;
			py = py < (h / panek > 1 ? h / panek : 1) ? py : (h / panek > 1 ? h / panek : 1) - 1;
			o[3] = (byte)(pane[py * panew + px] * 255.0f + 0.5f);
		}
	}


	j->out = out; /* uploaded by the main thread (MatUpload) */
	out = NULL;
	j->rust = rust;


done:
	MatFree(L);
	MatFree(rm);
	MatFree(tmp);
	MatFree(out);
	MatFree(M1);
	MatFree(M3);
	MatFree(V1);
	MatFree(V3);
	MatFree(hf);
	MatFree(st);
	MatFree(pane);
	MatFree(wm);
}


static void
MatUpload(matjob_t *j)
{
	GLuint map;

	if (j->out && j->texnum)
	{
		glGenTextures(1, &map);
		GL3_SelectTMU(GL_TEXTURE0 + MAT_UNIT);
		glBindTexture(GL_TEXTURE_2D, map);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, j->w, j->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, j->out);
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		GL3_SelectTMU(GL_TEXTURE0);
		MatStore(j->texnum, map, j->rust);
	}

	free(j->rgb);
	free(j->out);
	memset(j, 0, sizeof(*j));
}


/* ------------------------------------------------------------------
 * the material maps on disk (r_om_matcache 1): a map depends only on the
 * texels it is measured from, so it is kept in <user dir>/matcache<version>/ under a
 * 64-bit hash of those texels, their size and MAT_CACHE_VERSION, deflated
 * (miniz). A later load reads it instead of measuring (1.2 s of a PC load of
 * tsr1 with the HD pack). The jobs read and write the files themselves.
 * ------------------------------------------------------------------ */

#define MAT_CACHE_VERSION 2   /* raise it with any change of MatCompute (2: MurmurHash3 keys) */

static char matcachedir[MAX_OSPATH]; /* "" = no cache this batch */

static unsigned
MatRotl(unsigned x, int r)
{
	return (x << r) | (x >> (32 - r));
}

static unsigned
MatFmix(unsigned h)
{
	h ^= h >> 16;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h;
}

/* MurmurHash3 x86_128 (Austin Appleby, public domain), the low 64 bits. OpenMerc: the key was FNV-1a over every
   byte with a 64-bit multiply - a library call per byte on the 32-bit build, 12 per texel; this one works on 32-bit
   words in four lanes. */
static unsigned long long
MatMurmur(const unsigned char *data, size_t len, unsigned seed)
{
	const unsigned c1 = 0x239b961bu, c2 = 0xab0e9789u, c3 = 0x38b34ae5u, c4 = 0xa1e38b93u;
	unsigned h1 = seed, h2 = seed, h3 = seed, h4 = seed, k1, k2, k3, k4;
	size_t i, nblocks = len / 16;
	const unsigned char *tail = data + nblocks * 16;

	for (i = 0; i < nblocks; i++)
	{
		unsigned b[4];

		memcpy(b, data + i * 16, 16);
		k1 = b[0];
		k2 = b[1];
		k3 = b[2];
		k4 = b[3];

		k1 *= c1; k1 = MatRotl(k1, 15); k1 *= c2; h1 ^= k1;
		h1 = MatRotl(h1, 19); h1 += h2; h1 = h1 * 5 + 0x561ccd1bu;
		k2 *= c2; k2 = MatRotl(k2, 16); k2 *= c3; h2 ^= k2;
		h2 = MatRotl(h2, 17); h2 += h3; h2 = h2 * 5 + 0x0bcaa747u;
		k3 *= c3; k3 = MatRotl(k3, 17); k3 *= c4; h3 ^= k3;
		h3 = MatRotl(h3, 15); h3 += h4; h3 = h3 * 5 + 0x96cd1c35u;
		k4 *= c4; k4 = MatRotl(k4, 18); k4 *= c1; h4 ^= k4;
		h4 = MatRotl(h4, 13); h4 += h1; h4 = h4 * 5 + 0x32ac3b17u;
	}

	k1 = k2 = k3 = k4 = 0;

	switch (len & 15)
	{
		case 15: k4 ^= (unsigned)tail[14] << 16; /* fall through */
		case 14: k4 ^= (unsigned)tail[13] << 8; /* fall through */
		case 13: k4 ^= tail[12]; k4 *= c4; k4 = MatRotl(k4, 18); k4 *= c1; h4 ^= k4; /* fall through */
		case 12: k3 ^= (unsigned)tail[11] << 24; /* fall through */
		case 11: k3 ^= (unsigned)tail[10] << 16; /* fall through */
		case 10: k3 ^= (unsigned)tail[9] << 8; /* fall through */
		case 9: k3 ^= tail[8]; k3 *= c3; k3 = MatRotl(k3, 17); k3 *= c4; h3 ^= k3; /* fall through */
		case 8: k2 ^= (unsigned)tail[7] << 24; /* fall through */
		case 7: k2 ^= (unsigned)tail[6] << 16; /* fall through */
		case 6: k2 ^= (unsigned)tail[5] << 8; /* fall through */
		case 5: k2 ^= tail[4]; k2 *= c2; k2 = MatRotl(k2, 16); k2 *= c3; h2 ^= k2; /* fall through */
		case 4: k1 ^= (unsigned)tail[3] << 24; /* fall through */
		case 3: k1 ^= (unsigned)tail[2] << 16; /* fall through */
		case 2: k1 ^= (unsigned)tail[1] << 8; /* fall through */
		case 1: k1 ^= tail[0]; k1 *= c1; k1 = MatRotl(k1, 15); k1 *= c2; h1 ^= k1;
	}

	h1 ^= (unsigned)len;
	h2 ^= (unsigned)len;
	h3 ^= (unsigned)len;
	h4 ^= (unsigned)len;
	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;
	h1 = MatFmix(h1);
	h2 = MatFmix(h2);
	h3 = MatFmix(h3);
	h4 = MatFmix(h4);
	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1;

	return ((unsigned long long)h2 << 32) | h1;
}

static unsigned long long
MatCacheKey(const matjob_t *j)
{
	/* the texels, and their size and the version in the seed */
	unsigned seed = (unsigned)MAT_CACHE_VERSION * 0x9e3779b1u ^ (unsigned)j->w * 0x85ebca77u ^ (unsigned)j->h * 0xc2b2ae3du;

	return MatMurmur((const unsigned char *)j->rgb, sizeof(float) * 3 * (size_t)j->w * j->h, seed);
}

static void
MatCachePath(char *path, size_t size, unsigned long long key)
{
	Com_sprintf(path, (int)size, "%s/%08x%08x.omm", matcachedir, (unsigned)(key >> 32), (unsigned)key);
}

/* the file: "OMM1", width, height, rust (float), deflated length, the deflated map */
static qboolean
MatCacheRead(matjob_t *j, unsigned long long key)
{
	char path[MAX_OSPATH];
	FILE *f;
	int head[5];
	unsigned char *z;
	mz_ulong len = (mz_ulong)j->w * j->h * 4;
	qboolean ok = false;

	MatCachePath(path, sizeof(path), key);

	if (!(f = fopen(path, "rb")))
	{
		return false;
	}

	if ((fread(head, sizeof(int), 5, f) == 5) && (head[0] == 0x314D4D4F) && (head[1] == j->w) && (head[2] == j->h) &&
		(head[4] > 0) && (head[4] < 64 * 1024 * 1024) && (z = malloc(head[4])) != NULL)
	{
		if ((fread(z, 1, head[4], f) == (size_t)head[4]) && (j->out = malloc(len)) != NULL)
		{
			mz_ulong got = len;

			if ((mz_uncompress(j->out, &got, z, (mz_ulong)head[4]) == MZ_OK) && (got == len))
			{
				memcpy(&j->rust, &head[3], sizeof(float));
				ok = true;
			}
			else
			{
				free(j->out);
				j->out = NULL;
			}
		}

		free(z);
	}

	fclose(f);
	return ok;
}

static void
MatCacheWrite(const matjob_t *j, unsigned long long key)
{
	char path[MAX_OSPATH], tmp[MAX_OSPATH + 8];
	mz_ulong len = (mz_ulong)j->w * j->h * 4, zl = mz_compressBound(len);
	unsigned char *z = malloc(zl);
	int head[5];
	FILE *f;

	if (!z)
	{
		return;
	}

	if (mz_compress2(z, &zl, j->out, len, 1) == MZ_OK)
	{
		MatCachePath(path, sizeof(path), key);
		Com_sprintf(tmp, sizeof(tmp), "%s.tmp", path);
		head[0] = 0x314D4D4F; /* "OMM1" */
		head[1] = j->w;
		head[2] = j->h;
		memcpy(&head[3], &j->rust, sizeof(float));
		head[4] = (int)zl;

		/* written whole, then renamed: a load never reads a half-written map */
		if ((f = fopen(tmp, "wb")) != NULL)
		{
			qboolean ok = (fwrite(head, sizeof(int), 5, f) == 5) && (fwrite(z, 1, zl, f) == zl);

			fclose(f);
			remove(path);

			if (!ok || rename(tmp, path))
			{
				remove(tmp);
			}
		}
	}

	free(z);
}

/* a job: the map from the cache, or measured and stored */
static void
MatComputeCached(matjob_t *j)
{
	unsigned long long key = 0;

	if (matcachedir[0])
	{
		key = MatCacheKey(j);

		if (MatCacheRead(j, key))
		{
			return;
		}
	}

	MatArenaBegin();
	MatCompute(j);
	MatArenaEnd();

	if (matcachedir[0] && j->out)
	{
		MatCacheWrite(j, key);
	}
}

/* the maps of an older MAT_CACHE_VERSION (their own directory, "matcache" for version 1): deleted once per run */
static void
MatCachePurgeOld(void)
{
	static qboolean done;
	char dir[MAX_OSPATH], path[MAX_OSPATH * 2];
	int v;

	if (done)
	{
		return;
	}

	done = true;

	for (v = 1; v < MAT_CACHE_VERSION; v++)
	{
		if (v == 1)
		{
			Com_sprintf(dir, sizeof(dir), "%s/matcache", ri.FS_Gamedir());
		}
		else
		{
			Com_sprintf(dir, sizeof(dir), "%s/matcache%d", ri.FS_Gamedir(), v);
		}

#ifdef _MSC_VER
		{
			struct _finddata_t fd;
			intptr_t h;

			Com_sprintf(path, sizeof(path), "%s/*.omm", dir);

			if ((h = _findfirst(path, &fd)) != -1)
			{
				do
				{
					Com_sprintf(path, sizeof(path), "%s/%s", dir, fd.name);
					remove(path);
				}
				while (_findnext(h, &fd) == 0);

				_findclose(h);
			}

			_rmdir(dir);
		}
#else
		{
			DIR *d = opendir(dir);
			struct dirent *e;

			if (d)
			{
				while ((e = readdir(d)) != NULL)
				{
					size_t n = strlen(e->d_name);

					if ((n > 4) && !strcmp(e->d_name + n - 4, ".omm"))
					{
						Com_sprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
						remove(path);
					}
				}

				closedir(d);
				rmdir(dir);
			}
		}
#endif
	}
}

/* the cache directory of this registration (main thread: the engine's imports) */
static void
MatCacheBegin(void)
{
	static cvar_t *on;

	if (!on)
	{
		on = ri.Cvar_Get("r_om_matcache", "1", CVAR_ARCHIVE);
	}

	matcachedir[0] = 0;

	if (!on->value || !ri.FS_Gamedir())
	{
		return;
	}

	MatCachePurgeOld();
	Com_sprintf(matcachedir, sizeof(matcachedir), "%s/matcache%d", ri.FS_Gamedir(), MAT_CACHE_VERSION);
#ifdef _WIN32
	_mkdir(matcachedir);
#else
	mkdir(matcachedir, 0755);
#endif
}

static void
MatJob(int i, void *ctx)
{
	MatComputeCached(&((matjob_t *)ctx)[i]);
}

/* the pending maps: measured on all cores, uploaded here */
void
GL3_Mat_Flush(void)
{
	double t0 = MatClock();
	int i;

	if (!nmatjobs)
	{
		return;
	}

	GL3_ParallelFor(nmatjobs, MatJob, matjobs);
	gl3_matt[2] += MatClock() - t0; /* r_om_loadtime "rest": the measuring on all cores */

	{
		double t1 = MatClock();

		for (i = 0; i < nmatjobs; i++)
		{
			MatUpload(&matjobs[i]);
		}

		gl3_matt[3] += MatClock() - t1;
	}

	nmatjobs = 0;
	gl3_matt[4] += MatClock() - t0;
}

/* registration (gl3_model.c): the maps are batched from Begin to End */
void
GL3_Mat_BatchBegin(void)
{
	MatCacheBegin();
	matbatch = true;
}

void
GL3_Mat_BatchEnd(void)
{
	GL3_Mat_Flush();
	MatArenasFree();
	matbatch = false;
}

/* OpenMerc: the maps have two readers - the reflections (r_om_ssr; never on the GLES build) and the seam strips,
   which compare the material classes (their rust measure). Without both a map load measured every world texture
   for nothing (1.5 s of a PC load of tsr1; the mobile preset has neither). Skipped textures are measured once one of
   them is turned on: GL3_Mat_Watch restarts the renderer, which loads the textures again. */
static qboolean mat_skipped;

qboolean
GL3_Mat_Wanted(void)
{
	static cvar_t *ssr, *seams;

	if (!ssr)
	{
		ssr = ri.Cvar_Get("r_om_ssr", "1", CVAR_ARCHIVE);
		seams = ri.Cvar_Get("r_om_seams", "1", CVAR_ARCHIVE);
	}

#ifdef YQ2_GL3_GLES3
	return seams->value != 0;
#else
	return (ssr->value > 0) || (seams->value != 0);
#endif
}

/* once a frame: a reader turned on after textures were loaded without their maps */
void
GL3_Mat_Watch(void)
{
	if (mat_skipped && GL3_Mat_Wanted())
	{
		mat_skipped = false;
		R_Printf(PRINT_ALL, "material maps: the textures are loaded again for the reflections / seams\n");
		ri.Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
	}
}

void
GL3_Mat_Make(GLuint texnum, const char *name, const byte *rgba, int w, int h)
{
	matjob_t j;
	int x, y, step = 1;
	double t0 = MatClock();

	(void)name;

	if (!texnum || !rgba || w < 4 || h < 4)
	{
		return;
	}

	if (!GL3_Mat_Wanted())
	{
		mat_skipped = true;
		return;
	}

	/* measured at most 256 wide: the map only modulates the class */
	while ((w / step > 256) || (h / step > 256))
	{
		step *= 2;
	}

	memset(&j, 0, sizeof(j));
	j.texnum = texnum;
	j.w = w / step;
	j.h = h / step;
	j.rgb = malloc(sizeof(float) * 3 * j.w * j.h);

	if (!j.rgb)
	{
		return;
	}

	for (y = 0; y < j.h; y++)
	{
		for (x = 0; x < j.w; x++)
		{
			float r = 0, g = 0, b = 0, *o = j.rgb + ((size_t)y * j.w + x) * 3;
			int a, c;

			for (c = 0; c < step; c++)
			{
				for (a = 0; a < step; a++)
				{
					const byte *p = rgba + ((size_t)(y * step + c) * j.w * step + (x * step + a)) * 4;

					r += p[0];
					g += p[1];
					b += p[2];
				}
			}

			o[0] = r / (255.0f * step * step);
			o[1] = g / (255.0f * step * step);
			o[2] = b / (255.0f * step * step);
		}
	}

	gl3_matt[0] += MatClock() - t0;

	if (!matbatch)
	{
		if (!matcachedir[0])
		{
			MatCacheBegin();
		}

		MatComputeCached(&j);
		MatUpload(&j);
		return;
	}

	matjobs[nmatjobs++] = j;

	if (nmatjobs == MAT_BATCH)
	{
		GL3_Mat_Flush();
	}
}

/* the colour texture is deleted: its material map goes too */
void
GL3_Mat_Free(GLuint texnum)
{
	int i;

	for (i = 0; i < nmatjobs; i++)
	{
		if (matjobs[i].texnum == texnum)
		{
			matjobs[i].texnum = 0; /* computed, not stored */
		}
	}

	if ((int)texnum < M.count && M.maps[texnum])
	{
		glDeleteTextures(1, &M.maps[texnum]);
		M.maps[texnum] = 0;
		M.rust[texnum] = -1.0f;
	}
}

/* binds the material map of a colour texture (or a neutral one: full gloss, mid height, no stains) */
void
GL3_Mat_Bind(GLuint texnum)
{
	if (!M.flat)
	{
		static const byte flat[4] = {255, 128, 0, 255};

		glGenTextures(1, &M.flat);
		GL3_SelectTMU(GL_TEXTURE0 + MAT_UNIT);
		glBindTexture(GL_TEXTURE_2D, M.flat);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	GL3_SelectTMU(GL_TEXTURE0 + MAT_UNIT);
	glBindTexture(GL_TEXTURE_2D, ((int)texnum < M.count && M.maps[texnum]) ? M.maps[texnum] : M.flat);
	GL3_SelectTMU(GL_TEXTURE0);
}

/* the class parameters for the world shaders, once a view */
void
GL3_Mat_Frame(void)
{
	float a[MAT_MAXCLASS * 4], b[MAT_MAXCLASS * 4];
	int i;

	if (!M.loaded)
	{
		GL3_Mat_Init();
		MatLoad();
	}

	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));

	for (i = 0; i < M.numcls; i++)
	{
		memcpy(a + i * 4, M.cls[i].a, sizeof(M.cls[i].a));
		memcpy(b + i * 4, M.cls[i].b, sizeof(M.cls[i].b));
	}

	GL3_SoF_MaterialUniforms(a, b, MAT_MAXCLASS);
}

/* ------------------------------------------------------------------
 * commands
 * ------------------------------------------------------------------ */

static void
MatReload_f(void)
{
	MatLoad();
	R_Printf(PRINT_ALL, "r_om_materials: %d classes, %d textures, %d patterns\n", M.numcls, M.numtex, M.numpats);
}

/* the first face along the view direction */
static const msurface_t *
MatTrace(mnode_t *node, const vec3_t start, const vec3_t end)
{
	float front, back, frac;
	int side, i;
	vec3_t mid;
	const msurface_t *surf, *r;

	if (node->contents != CONTENTS_NODE)
	{
		return NULL;
	}

	front = DotProduct(start, node->plane->normal) - node->plane->dist;
	back = DotProduct(end, node->plane->normal) - node->plane->dist;
	side = front < 0;

	if ((back < 0) == side)
	{
		return MatTrace(node->children[side], start, end);
	}

	frac = front / (front - back);
	VectorLerp(start, end, frac, mid);

	if ((r = MatTrace(node->children[side], start, mid)))
	{
		return r;
	}

	surf = gl3_worldmodel->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		const mtexinfo_t *tex = surf->texinfo;
		float s, t;

		if (!tex || (surf->flags & SURF_DRAWSKY))
		{
			continue;
		}

		s = DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3];

		if (s >= surf->texturemins[0] && t >= surf->texturemins[1] && s - surf->texturemins[0] <= surf->extents[0] &&
			t - surf->texturemins[1] <= surf->extents[1])
		{
			return surf;
		}
	}

	return MatTrace(node->children[!side], mid, end);
}

/* r_om_material: what the face under the crosshair is made of */
static void
MatProbe_f(void)
{
	vec3_t end;
	const msurface_t *s;
	char key[MAX_QPATH];
	int c, listed;

	if (!gl3_worldmodel)
	{
		return;
	}

	VectorMA(r_newrefdef.vieworg, 8192, vpn, end);
	s = MatTrace(gl3_worldmodel->nodes, r_newrefdef.vieworg, end);

	if (!s || !s->texinfo || !s->texinfo->image)
	{
		R_Printf(PRINT_ALL, "r_om_material: no face\n");
		return;
	}

	MatKey(s->texinfo->image->name, key, sizeof(key));
	listed = MatLookup(key);
	c = (int)(-GL3_Mat_SurfClass(s) + 0.5f);
	c = c < 0 ? 0 : (c >= M.numcls ? 0 : c);
	R_Printf(PRINT_ALL, "%s  material %d  detail %s  rust %.2f\n  class %s (%s): refl %.2f rough %.2f walls %.2f "
		"ceil %.2f detail %.2f stain %.2f damp %.2f soak %.2f\n", key, (s->texinfo->flags >> 24) & 0xff,
		(s->texinfo->image->detail ? s->texinfo->image->detail->name : "-"),
		((int)s->texinfo->image->texnum < M.count && M.rust) ? M.rust[s->texinfo->image->texnum] : -1.0f,
		M.cls[c].name, listed >= 0 ? "table" : "rules", M.cls[c].a[0], M.cls[c].a[1], M.cls[c].a[2], M.cls[c].a[3],
		M.cls[c].b[0], M.cls[c].b[1], M.cls[c].b[2], M.cls[c].b[3]);
}

/* OpenMerc: built at the end of a map's registration, under the loading plaque, instead of in the first frame of
   the map (a 0.2 s hitch when the view appeared) */
void
GL3_Mat_Prewarm(void)
{
	if (!M.loaded)
	{
		GL3_Mat_Init();
		MatLoad();
	}
}

void
GL3_Mat_Init(void)
{
	r_om_materials = ri.Cvar_Get("r_om_materials", "materials/om_materials.txt", CVAR_ARCHIVE);

	if (!matcmds)
	{
		ri.Cmd_AddCommand("r_om_materials_reload", MatReload_f);
		ri.Cmd_AddCommand("r_om_material", MatProbe_f);
		matcmds = true;
	}
}

void
GL3_Mat_Shutdown(void)
{
	int i;

	for (i = 0; i < M.count; i++)
	{
		if (M.maps[i])
		{
			glDeleteTextures(1, &M.maps[i]);
		}
	}

	if (M.flat)
	{
		glDeleteTextures(1, &M.flat);
	}

	MatClear();
	free(M.maps);
	free(M.rust);
	M.maps = NULL;
	M.rust = NULL;
	M.count = 0;
	M.flat = 0;
	M.loaded = false;
	ri.Cmd_RemoveCommand("r_om_materials_reload");
	ri.Cmd_RemoveCommand("r_om_material");
	matcmds = false;
}
