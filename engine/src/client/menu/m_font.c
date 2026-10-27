/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc MENU-SCRATCH: TrueType menu text (docs/impl/MENU2.md "Modern
 * scaling and fonts"). The menu font names of the RMF files (small, medium,
 * title, interface, plus the openMERC "logo") are drawn with OpenMerc's own
 * OFL fonts (fonts/om/, packed into zz_openmerc.pak, not game data),
 * rasterized by stb_truetype at the pixel size the window needs: a share of
 * the window height (menu_textsize scales it). Each size has a glyph atlas
 * of the 224 printable CP1251 characters, uploaded with its alpha through the
 * renderer's OM_SetAlphaImage export and drawn 1:1 at whole pixels, so the
 * text is sharp at any menu scale. Without the export or the font files the
 * stock bitmap fonts are drawn (MF_Ready false).
 */

#include "../header/client.h"
#include "header/m_font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "header/stb_truetype.h"

/* CP1251 0x80-0xFF -> Unicode (0 = not drawn) */
static const unsigned short mf_cp1251[128] = {
	0x0402, 0x0403, 0x201a, 0x0453, 0x201e, 0x2026, 0x2020, 0x2021,
	0x20ac, 0x2030, 0x0409, 0x2039, 0x040a, 0x040c, 0x040b, 0x040f,
	0x0452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
	0x0000, 0x2122, 0x0459, 0x203a, 0x045a, 0x045c, 0x045b, 0x045f,
	0x00a0, 0x040e, 0x045e, 0x0408, 0x00a4, 0x0490, 0x00a6, 0x00a7,
	0x0401, 0x00a9, 0x0404, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x0407,
	0x00b0, 0x00b1, 0x0406, 0x0456, 0x0491, 0x00b5, 0x00b6, 0x00b7,
	0x0451, 0x2116, 0x0454, 0x00bb, 0x0458, 0x0405, 0x0455, 0x0457,
	0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
	0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
	0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
	0x0428, 0x0429, 0x042a, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f,
	0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
	0x0438, 0x0439, 0x043a, 0x043b, 0x043c, 0x043d, 0x043e, 0x043f,
	0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
	0x0448, 0x0449, 0x044a, 0x044b, 0x044c, 0x044d, 0x044e, 0x044f
};

/* the colours of the control characters 1-31 (as the renderer's font slot) */
static const byte mf_textcolors[32][4] = {
	{0x00, 0x00, 0x00, 0x00}, {0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0x00, 0x00, 0xFF}, {0x00, 0xFF, 0x00, 0xFF},
	{0xFF, 0xFF, 0x00, 0xFF}, {0x00, 0x00, 0xFF, 0xFF}, {0xFF, 0x00, 0xFF, 0xFF}, {0x00, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x00, 0xFF}, {0x7F, 0x7F, 0x7F, 0x7F}, {0x00, 0x00, 0x00, 0x00}, {0x7F, 0x00, 0x00, 0x7F},
	{0x00, 0x7F, 0x00, 0x7F}, {0x00, 0x00, 0x00, 0x00}, {0x7F, 0x7F, 0x00, 0x7F}, {0x00, 0x00, 0x7F, 0x7F},
	{0x56, 0x4D, 0x28, 0x7F}, {0x4C, 0x5E, 0x36, 0x7F}, {0x37, 0x6F, 0x65, 0x7F}, {0x00, 0x55, 0x72, 0xFF},
	{0x54, 0x64, 0x7E, 0xFF}, {0x1E, 0x2A, 0x63, 0xBF}, {0x66, 0x09, 0x7B, 0xFF}, {0x70, 0x5E, 0x61, 0xFF},
	{0x98, 0x00, 0x53, 0xFF}, {0x96, 0x00, 0x18, 0xFF}, {0x70, 0x2D, 0x07, 0xFF}, {0x54, 0x49, 0x2A, 0xFF},
	{0x61, 0xA9, 0x97, 0xFF}, {0xCB, 0x8F, 0x39, 0xFF}, {0xCF, 0x83, 0x16, 0xFF}, {0xFF, 0x80, 0x20, 0xFF}
};

typedef struct
{
	const char *file;
	byte *data;
	stbtt_fontinfo info;
	qboolean tried, ok;
} mfface_t;

static mfface_t mf_faces[] = {
	{"fonts/om/Play-Regular.ttf"},
	{"fonts/om/Play-Bold.ttf"},
	{"fonts/om/RussoOne-Regular.ttf"},
	{"fonts/om/JetBrainsMono-Regular.ttf"}
};

/* a menu font name: face, em size as a share of the window height, extra
   letter spacing in em (the SoF menu fonts are widely spaced) */
typedef struct
{
	const char *name;
	int face;
	float share;
	float track;
} mfstyle_t;

static const mfstyle_t mf_styles[] = {
	{"small", 0, 0.0150f, 0.02f},
	{"medium", 0, 0.0172f, 0.03f},
	{"title", 1, 0.0190f, 0.03f},
	{"interface", 1, 0.0280f, 0.02f},
	{"logo", 2, 0.0560f, 0.06f},
	{"logosmall", 2, 0.0230f, 0.10f},
	{"console", 3, 0.0f, 0.0f}      /* console cells (MF_DrawCell), sized by the cell */
};
#define MF_CONSOLESTYLE 6
#define MF_NUMSTYLES ((int)(sizeof(mf_styles) / sizeof(mf_styles[0])))

#define MF_FIRST 32
#define MF_COUNT 224
#define MF_MAXATLAS 16

typedef struct
{
	int style, px;          /* key */
	char name[32];          /* picture name in the renderer */
	int w, h;
	stbtt_packedchar pc[MF_COUNT];
	float ascent, descent;  /* pixels, descent negative */
	int lineheight;         /* pixels */
	float track;            /* pixels */
	float scale;            /* stbtt scale */
	int checked;            /* frame of the last renderer check */
	unsigned lastuse;
	byte *rgba;             /* kept for a re-upload after a renderer restart */
} mfatlas_t;

static mfatlas_t mf_atlas[MF_MAXATLAS];
static unsigned mf_use;
static int mf_frame = 1;
static OM_SetAlphaImage_t mf_upload;
static cvar_t *mf_cvar, *mf_size;

void
MF_SetRenderer(OM_SetAlphaImage_t upload)
{
	if (upload != mf_upload)
	{
		int i;

		/* a different renderer: every atlas is uploaded again on use */
		for (i = 0; i < MF_MAXATLAS; i++)
		{
			mf_atlas[i].checked = 0;
		}
	}

	mf_upload = upload;
}

void
MF_NewFrame(void)
{
	mf_frame++;
}

static const mfstyle_t *
MF_Style(const char *font, int *index)
{
	int i;

	if (!font)
	{
		return NULL;
	}

	for (i = 0; i < MF_NUMSTYLES; i++)
	{
		if (!Q_stricmp(mf_styles[i].name, font))
		{
			*index = i;
			return &mf_styles[i];
		}
	}

	return NULL;
}

static mfface_t *
MF_Face(int i)
{
	mfface_t *f = &mf_faces[i];

	if (!f->tried)
	{
		byte *buf = NULL;
		int len;

		f->tried = true;
		len = FS_LoadFile((char *)f->file, (void **)&buf);

		if (buf && len > 0)
		{
			f->data = Z_Malloc(len);
			memcpy(f->data, buf, len);
			f->ok = stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0)) != 0;
		}

		if (buf)
		{
			FS_FreeFile(buf);
		}

		if (!f->ok)
		{
			Com_Printf("menu: font %s not found, the stock menu fonts are used\n", f->file);
		}
	}

	return f->ok ? f : NULL;
}

qboolean
MF_Ready(const char *font)
{
	int si;
	const mfstyle_t *st;

	if (!mf_cvar)
	{
		mf_cvar = Cvar_Get("menu_ttf", "1", CVAR_ARCHIVE);
		mf_size = Cvar_Get("menu_textsize", "1", CVAR_ARCHIVE);
	}

	if (!mf_cvar->value || !mf_upload)
	{
		return false;
	}

	st = MF_Style(font, &si);
	return st && MF_Face(st->face) != NULL;
}

/* the em size in pixels of a menu font at magnification z */
static int
MF_Px(const mfstyle_t *st, float z)
{
	float k = mf_size->value;
	int px;

	k = (k < 0.6f) ? 0.6f : ((k > 1.6f) ? 1.6f : k);
	px = (int)(st->share * (float)viddef.height * k * z + 0.5f);
	return (px < 8) ? 8 : ((px > 160) ? 160 : px);
}

static int
MF_Codepoint(int c)
{
	return (c < 128) ? c : mf_cp1251[c - 128];
}

static qboolean
MF_Build(mfatlas_t *a, int si, int px)
{
	const mfstyle_t *st = &mf_styles[si];
	mfface_t *f = MF_Face(st->face);
	int cps[MF_COUNT], i, w = 512, h = 256, asc, desc, gap;
	stbtt_pack_range r;
	byte *alpha = NULL;

	if (!f)
	{
		return false;
	}

	for (i = 0; i < MF_COUNT; i++)
	{
		int cp = MF_Codepoint(MF_FIRST + i);

		/* not in CP1251 or not in the font: a space-sized gap */
		cps[i] = (cp && stbtt_FindGlyphIndex(&f->info, cp)) ? cp : ' ';
	}

	memset(&r, 0, sizeof(r));
	r.font_size = STBTT_POINT_SIZE((float)px);
	r.array_of_unicode_codepoints = cps;
	r.num_chars = MF_COUNT;
	r.chardata_for_range = a->pc;

	for (;;)
	{
		stbtt_pack_context pk;
		qboolean ok;

		alpha = Z_Malloc(w * h);

		if (!stbtt_PackBegin(&pk, alpha, w, h, 0, 1, NULL))
		{
			Z_Free(alpha);
			return false;
		}

		stbtt_PackSetOversampling(&pk, 1, 1);
		ok = stbtt_PackFontRanges(&pk, f->data, 0, &r, 1) != 0;
		stbtt_PackEnd(&pk);

		if (ok)
		{
			break;
		}

		Z_Free(alpha);

		if (h < w)
		{
			h *= 2;
		}
		else if (w < 2048)
		{
			w *= 2;
		}
		else
		{
			return false;
		}
	}

	if (a->rgba)
	{
		Z_Free(a->rgba);
	}

	a->rgba = Z_Malloc(w * h * 4);

	for (i = 0; i < w * h; i++)
	{
		a->rgba[i * 4 + 0] = 255;
		a->rgba[i * 4 + 1] = 255;
		a->rgba[i * 4 + 2] = 255;
		a->rgba[i * 4 + 3] = alpha[i];
	}

	Z_Free(alpha);

	a->style = si;
	a->px = px;
	a->w = w;
	a->h = h;
	a->scale = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
	stbtt_GetFontVMetrics(&f->info, &asc, &desc, &gap);
	a->ascent = asc * a->scale;
	a->descent = desc * a->scale;
	a->lineheight = (int)ceilf(a->ascent - a->descent + 0.15f * px);
	a->track = st->track * px;
	Com_sprintf(a->name, sizeof(a->name), "*omfont%d_%d", si, px);
	a->checked = 0;
	return true;
}

static mfatlas_t *MF_AtlasPx(int si, int px, qboolean draw);

static mfatlas_t *
MF_Atlas(const char *font, float z, qboolean draw)
{
	const mfstyle_t *st;
	int si;

	st = MF_Style(font, &si);

	if (!st)
	{
		return NULL;
	}

	return MF_AtlasPx(si, MF_Px(st, z), draw);
}

static mfatlas_t *
MF_AtlasPx(int si, int px, qboolean draw)
{
	mfatlas_t *a, *lru = &mf_atlas[0];
	int i;

	for (i = 0, a = mf_atlas; i < MF_MAXATLAS; i++, a++)
	{
		if (a->rgba && a->style == si && a->px == px)
		{
			break;
		}

		if (!a->rgba || a->lastuse < lru->lastuse)
		{
			lru = a;
		}
	}

	if (i == MF_MAXATLAS)
	{
		a = lru;

		if (!MF_Build(a, si, px))
		{
			return NULL;
		}
	}

	a->lastuse = ++mf_use;

	/* uploaded once, again after a renderer restart (checked once a frame) */
	if (draw && a->checked != mf_frame)
	{
		if (!a->checked || !mf_upload(a->name, 0, 0, NULL))
		{
			mf_upload(a->name, a->w, a->h, a->rgba);
		}

		a->checked = mf_frame;
	}

	return a;
}

static float
MF_Kern(const mfatlas_t *a, int c1, int c2)
{
	const mfface_t *f = &mf_faces[mf_styles[a->style].face];

	if (!c1 || c2 < MF_FIRST || c1 < MF_FIRST)
	{
		return 0.0f;
	}

	return stbtt_GetCodepointKernAdvance(&f->info, MF_Codepoint(c1), MF_Codepoint(c2)) * a->scale;
}

/* width in pixels at magnification z (the widest line) */
float
MF_WidthPx(const char *text, const char *font, float z)
{
	mfatlas_t *a = MF_Atlas(font, z, false);
	const byte *s = (const byte *)text;
	float pen = 0.0f, best = 0.0f;
	int prev = 0;

	if (!a || !text)
	{
		return 0.0f;
	}

	for (; *s; s++)
	{
		int c = *s;

		if (c == '\n')
		{
			best = pen > best ? pen : best;
			pen = 0.0f;
			prev = 0;
			continue;
		}

		if (c < MF_FIRST)
		{
			continue;
		}

		pen += MF_Kern(a, prev, c);
		pen = floorf(pen + 0.5f);
		pen += a->pc[c - MF_FIRST].xadvance + a->track;
		prev = c;
	}

	best = pen > best ? pen : best;
	return best > 0.0f ? best - a->track : 0.0f;
}

int
MF_LineHeightPx(const char *font, float z)
{
	mfatlas_t *a = MF_Atlas(font, z, false);

	return a ? a->lineheight : 0;
}

/*
 * Draws text with its top-left corner at window pixel x, y. part draws one
 * atlas rectangle at pixel size (the renderer at 2D scale 1).
 */
void
MF_Draw(int x, int y, const char *text, unsigned rgba, const char *font, int keep, float z,
	void (*part)(int x, int y, int l, int t, int r, int b, const byte c[4], const char *name))
{
	static byte color[4] = {255, 255, 255, 255};
	mfatlas_t *a = MF_Atlas(font, z, true);
	const byte *s = (const byte *)text;
	float pen = (float)x;
	int base, prev = 0;

	if (!a || !text)
	{
		return;
	}

	if (!(keep & 0xFF))
	{
		color[0] = rgba & 0xff;
		color[1] = (rgba >> 8) & 0xff;
		color[2] = (rgba >> 16) & 0xff;
		color[3] = (rgba >> 24) & 0xff;
	}

	base = y + (int)floorf((a->lineheight - (a->ascent - a->descent)) * 0.5f + a->ascent + 0.5f);

	for (; *s; s++)
	{
		int c = *s;
		const stbtt_packedchar *g;

		if (c == '\n')
		{
			base += a->lineheight;
			pen = (float)x;
			prev = 0;
			continue;
		}

		if (c == '\r')
		{
			continue;
		}

		if (c < MF_FIRST)
		{
			memcpy(color, mf_textcolors[c], 4);
			continue;
		}

		pen += MF_Kern(a, prev, c);
		pen = floorf(pen + 0.5f);
		g = &a->pc[c - MF_FIRST];

		if (g->x1 > g->x0 && g->y1 > g->y0)
		{
			part((int)floorf(pen + g->xoff + 0.5f), base + (int)floorf(g->yoff + 0.5f),
				g->x0, g->y0, g->x1, g->y1, color, a->name);
		}

		pen += g->xadvance + a->track;
		prev = c;
	}
}

/*
 * MENU-SCRATCH (owner: the menu fonts brief): one console character in a
 * fixed cell of cell x cell window pixels (the console grid of the stock
 * 8x8 font), drawn with the monospaced OFL face (JetBrains Mono) centred in
 * the cell. False: not drawn (con_ttf 0, no font, no renderer export, a
 * control character), the caller draws the bitmap character.
 */
/* ink extent of the console text glyphs (printable ASCII and the CP1251
   letters A..ya), pixels from the baseline: *top negative (above), *bot
   positive (below) */
static void
MF_CellInk(const mfatlas_t *a, float *top, float *bot)
{
	int ch;

	*top = 0;
	*bot = 0;

	for (ch = 33; ch < MF_FIRST + MF_COUNT; ch++)
	{
		const stbtt_packedchar *g;

		if (ch == 127)
		{
			ch = 0xBF; /* skip to 0xC0: CP1251 letters */
			continue;
		}

		g = &a->pc[ch - MF_FIRST];

		if (g->y1 <= g->y0)
		{
			continue;
		}

		if (g->yoff < *top)
		{
			*top = g->yoff;
		}

		if (g->yoff + (g->y1 - g->y0) > *bot)
		{
			*bot = g->yoff + (g->y1 - g->y0);
		}
	}
}

/* the console draws with the OFL face: con_ttf 1, a renderer export and
   the face present */
static qboolean
MF_ConsoleTTF(void)
{
	static cvar_t *con_ttf;

	if (!con_ttf)
	{
		con_ttf = Cvar_Get("con_ttf", "1", CVAR_ARCHIVE);
	}

	return con_ttf->value && mf_upload && MF_Face(mf_styles[MF_CONSOLESTYLE].face);
}

/* layout of the last console cell size: em in pixels, baseline below the
   row top, row pitch in pixels */
static int lay_cell = -1, lay_px, lay_base, lay_pitch;

/* the em of the face is the cell (the advance of 0.6 em leaves a small gap
   in the cell); the console rows step by the ink of the text glyphs, the
   shadow and a line gap of 1 pixel (2 from cells of 16), never less than
   the cell, so rows never touch (owner, int5 review: with 1.2 em in the
   bare 8x8 grid the rows ran into each other) */
static qboolean
MF_CellLayout(int cell)
{
	int gap, sh, h;
	float top, bot;
	mfatlas_t *a;

	if (cell == lay_cell)
	{
		return true;
	}

	gap = cell >= 16 ? 2 : 1;
	sh = cell >= 12 ? 1 + cell / 24 : 0;
	a = MF_AtlasPx(MF_CONSOLESTYLE, cell, true);

	if (!a)
	{
		return false;
	}

	MF_CellInk(a, &top, &bot);
	h = (int)ceilf(bot) - (int)floorf(top);
	lay_pitch = h + sh + gap > cell ? h + sh + gap : cell;
	/* the ink starts half the gap (and half any spare room) below the row top */
	lay_base = gap / 2 + (lay_pitch - gap - sh - h) / 2 - (int)floorf(top);
	lay_px = cell;
	lay_cell = cell;
	return true;
}

/* pixels from one console text row to the next for this cell: the cell
   for the bitmap font, more for the OFL face (MF_CellLayout) */
int
MF_ConsolePitch(int cell)
{
	if (cell < 6 || !MF_ConsoleTTF() || !MF_CellLayout(cell))
	{
		return cell;
	}

	return lay_pitch;
}

qboolean
MF_DrawCell(int x, int y, int ch, int cell, const byte color[4],
	void (*part)(int x, int y, int l, int t, int r, int b, const byte c[4], const char *name))
{
	const stbtt_packedchar *g;
	mfatlas_t *a;
	int px, base;
	float pen;

	ch &= 0xff;

	if (ch < MF_FIRST || ch == 127 || cell < 6 || !MF_ConsoleTTF())
	{
		return false;
	}

	if (ch == ' ')
	{
		return true;
	}

	if (!MF_CellLayout(cell))
	{
		return false;
	}

	px = lay_px;
	a = MF_AtlasPx(MF_CONSOLESTYLE, px, true);

	if (!a)
	{
		return false;
	}

	g = &a->pc[ch - MF_FIRST];

	if (!MF_Codepoint(ch))
	{
		return false;
	}

	base = y + lay_base;
	pen = x + (cell - g->xadvance) * 0.5f;

	if (g->x1 > g->x0 && g->y1 > g->y0)
	{
		/* a one-pixel dark shadow keeps the thin strokes readable over the
		   console backdrop */
		static const byte shadow[4] = {0, 0, 0, 0xC0};
		int sh = cell >= 12 ? 1 + cell / 24 : 0;

		if (sh)
		{
			part((int)floorf(pen + g->xoff + 0.5f) + sh, base + (int)floorf(g->yoff + 0.5f) + sh,
				g->x0, g->y0, g->x1, g->y1, shadow, a->name);
		}

		part((int)floorf(pen + g->xoff + 0.5f), base + (int)floorf(g->yoff + 0.5f),
			g->x0, g->y0, g->x1, g->y1, color, a->name);
	}

	return true;
}
