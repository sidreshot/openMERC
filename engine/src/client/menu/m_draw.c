/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: software implementation of the SoF renderer's 2D slots for the
 * menu runtime (local shim until the renderer package exports them to the
 * engine). Everything the menus draw in one frame is composited into one
 * RGBA layer with the blending, alpha test, clipping, picture rotation/flip
 * and proportional-font rules of SPEC-412; SW_EndFrame presents the layer
 * with the renderer's raw-image call. Pixels the menus did not cover stay
 * transparent (the 2D shader discards them), so the 3D view shows through.
 *
 * Pictures are read from the .m32 files (SPEC-100, level 0), fonts from the
 * .fnt glyph tables (SPEC-132) with the renderer-side metrics of SPEC-412 6.
 *
 * Implements: SPEC-002 (2D slots 0x24, 0x44-0x54, 0x74-0x7c, 0x8c-0x9c,
 *             0xa0, 0xc0, 0xc8-0xd0), SPEC-412 (sections 2-6), SPEC-100,
 *             SPEC-132, SPEC-802 (picture flips 4-7)
 */

#include <math.h>

#include "../header/client.h"
#include "header/m_draw.h"
#include "../../common/header/sof/refapi.h"
#include "header/m_font.h"

typedef struct mrimage_s
{
	struct mrimage_s *next;
	char name[MAX_QPATH];
	int w, h;               /* -1 = missing */
	byte *pix;              /* RGBA, rows top to bottom */
	qboolean raw;
} mrimage_t;

typedef struct
{
	float s0, t0, s1, t1;
	int width, height, baseline;
} mrglyph_t;

typedef struct
{
	const char *name;
	const char *file;
	int first, end;
	int lineh, ascent;
	qboolean loaded;
	mrglyph_t *glyphs;
	int count;
	mrimage_t *page;
} mrfont_t;

/* SPEC-412 font table */
static mrfont_t mr_fonts[4] = {
	{"small", "pics/menus/fonts/font1", 32, 256, 10, 8},
	{"medium", "pics/menus/fonts/font4", 32, 256, 12, 10},
	{"title", "pics/menus/fonts/font2", 32, 256, 18, 14},
	{"interface", "pics/menus/fonts/inttext", 32, 64, 32, 24},
};

/* SPEC-002 text colour table, bytes R G B A */
static const byte mr_textcolors[32][4] = {
	{0x00, 0x00, 0x00, 0x00}, {0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0x00, 0x00, 0xFF}, {0x00, 0xFF, 0x00, 0xFF},
	{0xFF, 0xFF, 0x00, 0xFF}, {0x00, 0x00, 0xFF, 0xFF}, {0xFF, 0x00, 0xFF, 0xFF}, {0x00, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x00, 0xFF}, {0x7F, 0x7F, 0x7F, 0x7F}, {0x00, 0x00, 0x00, 0x00}, {0x7F, 0x00, 0x00, 0x7F},
	{0x00, 0x7F, 0x00, 0x7F}, {0x00, 0x00, 0x00, 0x00}, {0x7F, 0x7F, 0x00, 0x7F}, {0x00, 0x00, 0x7F, 0x7F},
	{0x56, 0x4D, 0x28, 0x7F}, {0x4C, 0x5E, 0x36, 0x7F}, {0x37, 0x6F, 0x65, 0x7F}, {0x00, 0x55, 0x72, 0xFF},
	{0x54, 0x64, 0x7E, 0xFF}, {0x1E, 0x2A, 0x63, 0xBF}, {0x66, 0x09, 0x7B, 0xFF}, {0x70, 0x5E, 0x61, 0xFF},
	{0x98, 0x00, 0x53, 0xFF}, {0x96, 0x00, 0x18, 0xFF}, {0x70, 0x2D, 0x07, 0xFF}, {0x54, 0x49, 0x2A, 0xFF},
	{0x61, 0xA9, 0x97, 0xFF}, {0xCB, 0x8F, 0x39, 0xFF}, {0xCF, 0x83, 0x16, 0xFF}, {0xFF, 0x80, 0x20, 0xFF},
};


static void SW_ResetClip(void);
static void SW_FreePic(const char *name);
static void SW_Fill(int x, int y, int w, int h, unsigned rgba);
static void SW_Line(float x1, float y1, float x2, float y2, float width, unsigned rgba);

static mrimage_t *mr_images;
static byte *mr_fb;
static int mr_w, mr_h;
static qboolean mr_inframe;
static qboolean mr_drawn;

/* clipping (SPEC-412 section 4) */
static qboolean mr_clip;
static int mr_rx, mr_ry, mr_rw, mr_rh;  /* remembered rectangle */
static int mr_hx0, mr_hy0, mr_hx1, mr_hy1; /* hardware clip, exclusive right/bottom */

static unsigned mr_textcolor = 0xFFFFFFFF;

/* colour correction applied to pictures when they are loaded (slot 0xc0) */
static byte mr_gammatable[256];
static qboolean mr_gammaset;

/* ------------------------------------------------------------------ */

static void
MR_FreeImage(mrimage_t *img)
{
	if (img->pix)
	{
		free(img->pix);
	}

	free(img);
}

static mrimage_t *
MR_LoadM32(const char *name)
{
	char path[MAX_QPATH + 8];
	byte *buf;
	int len;
	mrimage_t *img;

	img = calloc(1, sizeof(*img));
	Q_strlcpy(img->name, name, sizeof(img->name));
	img->w = img->h = -1;

	if (strlen(name) > 4 && !Q_stricmp(name + strlen(name) - 4, ".m32"))
	{
		Q_strlcpy(path, name, sizeof(path));
	}
	else
	{
		Com_sprintf(path, sizeof(path), "%s.m32", name);
	}

	len = FS_LoadFile(path, (void **)&buf);

	if (len >= 968 && buf)
	{
		int version = LittleLong(*(int *)buf);
		int w = LittleLong(*(int *)(buf + 0x204));
		int h = LittleLong(*(int *)(buf + 0x244));
		int ofs = LittleLong(*(int *)(buf + 0x284));

		if (version == 4 && w > 0 && h > 0 && w <= 4096 && h <= 4096 &&
			ofs >= 968 && ofs + w * h * 4 <= len)
		{
			img->w = w;
			img->h = h;
			img->pix = malloc(w * h * 4);
			memcpy(img->pix, buf + ofs, w * h * 4);

			if (mr_gammaset)
			{
				int i;

				for (i = 0; i < w * h * 4; i++)
				{
					if ((i & 3) != 3)
					{
						img->pix[i] = mr_gammatable[img->pix[i]];
					}
				}
			}
		}
	}

	if (buf)
	{
		FS_FreeFile(buf);
	}

	if (img->w < 0)
	{
		Com_DPrintf("Can't find pic: %s\n", name);
	}

	return img;
}

static mrimage_t *
MR_FindImage(const char *name)
{
	mrimage_t *img;

	if (!name || !name[0])
	{
		return NULL;
	}

	for (img = mr_images; img; img = img->next)
	{
		if (!Q_stricmp(img->name, name))
		{
			return img->w > 0 ? img : NULL;
		}
	}

	img = MR_LoadM32(name);
	img->next = mr_images;
	mr_images = img;

	return img->w > 0 ? img : NULL;
}

static void
SW_FreePic(const char *name)
{
	mrimage_t **pp, *img;

	for (pp = &mr_images; *pp; pp = &(*pp)->next)
	{
		img = *pp;

		if (!Q_stricmp(img->name, name))
		{
			*pp = img->next;
			MR_FreeImage(img);
			return;
		}
	}
}

static void
SW_ReleasePics(void)
{
	/* pictures registered by the menus are released when the menu system
	   closes (SPEC-801 section 1); fonts stay loaded */
	while (mr_images)
	{
		mrimage_t *img = mr_images;
		mr_images = img->next;
		MR_FreeImage(img);
	}

	for (int i = 0; i < 4; i++)
	{
		mr_fonts[i].page = NULL;
	}
}

static void
SW_RawPic(const char *name, int w, int h, const byte *rgba)
{
	mrimage_t *img;

	SW_FreePic(name);

	img = calloc(1, sizeof(*img));
	Q_strlcpy(img->name, name, sizeof(img->name));
	img->w = w;
	img->h = h;
	img->raw = true;
	img->pix = malloc(w * h * 4);
	memcpy(img->pix, rgba, w * h * 4);
	img->next = mr_images;
	mr_images = img;
}

static void
SW_ColorCorrection(float gamma, float brightness, float contrast, float modulate)
{
	/* SPEC-002 open question: the parameter meanings and the formula are not
	   specified. The engine calls it with the pending menu_* settings
	   (modulate x 0.5) before reloading the video preview picture and then
	   with the vid_* settings and 1.0. OpenMerc's renderer applies vid_gamma
	   itself, so the second call means "no correction"; the first one is
	   approximated by the gamma ratio and the modulate factor. */
	int i;
	qboolean identity = true;
	float ref = Cvar_VariableValue("vid_gamma");

	if (modulate == 1.0f || ref <= 0.01f)
	{
		mr_gammaset = false;
		return;
	}

	for (i = 0; i < 256; i++)
	{
		float v = i / 255.0f;

		if (gamma > 0.01f)
		{
			v = (float)pow(v, ref / gamma);
		}

		v = v * modulate * 2.0f;

		if (v < 0)
		{
			v = 0;
		}

		if (v > 1)
		{
			v = 1;
		}

		mr_gammatable[i] = (byte)(v * 255.0f + 0.5f);

		if (mr_gammatable[i] != i)
		{
			identity = false;
		}
	}

	(void)brightness;
	(void)contrast;
	mr_gammaset = !identity;
}

static void
SW_GetPicSize(int *w, int *h, const char *name)
{
	mrimage_t *img = MR_FindImage(name);

	if (!img)
	{
		*w = *h = -1;
		return;
	}

	*w = img->w;
	*h = img->h;
}

/* ------------------------------------------------------------------ */
/* raster helpers                                                      */

static void
MR_ClipBounds(int *x0, int *y0, int *x1, int *y1)
{
	int cx0 = 0, cy0 = 0, cx1 = mr_w, cy1 = mr_h;

	if (mr_clip)
	{
		cx0 = mr_hx0 > 0 ? mr_hx0 : 0;
		cy0 = mr_hy0 > 0 ? mr_hy0 : 0;
		cx1 = mr_hx1 < mr_w ? mr_hx1 : mr_w;
		cy1 = mr_hy1 < mr_h ? mr_hy1 : mr_h;
	}

	if (*x0 < cx0) *x0 = cx0;
	if (*y0 < cy0) *y0 = cy0;
	if (*x1 > cx1) *x1 = cx1;
	if (*y1 > cy1) *y1 = cy1;
}

/* blend normal with alpha test > 0.05 (SPEC-412 sections 1-3) */
static inline void
MR_Blend(byte *d, int r, int g, int b, int a)
{
	int ia;

	if (a <= 12)
	{
		return;
	}

	mr_drawn = true;

	if (a >= 255)
	{
		d[0] = (byte)r;
		d[1] = (byte)g;
		d[2] = (byte)b;
		d[3] = 255;
		return;
	}

	ia = 255 - a;
	d[0] = (byte)((r * a + d[0] * ia + 127) / 255);
	d[1] = (byte)((g * a + d[1] * ia + 127) / 255);
	d[2] = (byte)((b * a + d[2] * ia + 127) / 255);
	d[3] = (byte)(a + (d[3] * ia + 127) / 255);
}

static inline int
MR_Mul(int a, int b)
{
	return (a * b + 127) / 255;
}

/* textured or untextured axis-aligned quad; tc = texture coordinates of the
   corners (x, y), (x+w, y), (x+w, y+h), (x, y+h) */
static void
MR_Quad(float x, float y, float w, float h, float tc[4][2], const mrimage_t *img, unsigned rgba)
{
	int px0, py0, px1, py1, px, py;
	int cr = rgba & 0xff, cg = (rgba >> 8) & 0xff, cb = (rgba >> 16) & 0xff, ca = (rgba >> 24) & 0xff;
	float t[4][2];

	if (!mr_fb || w == 0 || h == 0)
	{
		return;
	}

	memcpy(t, tc, sizeof(t));

	if (w < 0)
	{
		/* mirror: swap left and right corners */
		float s[2];
		x += w;
		w = -w;
		memcpy(s, t[0], sizeof(s)); memcpy(t[0], t[1], sizeof(s)); memcpy(t[1], s, sizeof(s));
		memcpy(s, t[3], sizeof(s)); memcpy(t[3], t[2], sizeof(s)); memcpy(t[2], s, sizeof(s));
	}

	if (h < 0)
	{
		float s[2];
		y += h;
		h = -h;
		memcpy(s, t[0], sizeof(s)); memcpy(t[0], t[3], sizeof(s)); memcpy(t[3], s, sizeof(s));
		memcpy(s, t[1], sizeof(s)); memcpy(t[1], t[2], sizeof(s)); memcpy(t[2], s, sizeof(s));
	}

	/* pixels whose centre lies inside [x, x+w) x [y, y+h) */
	px0 = (int)ceilf(x - 0.5f);
	py0 = (int)ceilf(y - 0.5f);
	px1 = (int)ceilf(x + w - 0.5f);
	py1 = (int)ceilf(y + h - 0.5f);
	MR_ClipBounds(&px0, &py0, &px1, &py1);

	/* fast path: a picture at its own size, unrotated, at whole pixels (tiles) */
	if (img && (int)w == img->w && (int)h == img->h && x == (float)(int)x && y == (float)(int)y &&
		t[0][0] == 0 && t[0][1] == 0 && t[1][0] == 1 && t[1][1] == 0 &&
		t[2][0] == 1 && t[2][1] == 1 && t[3][0] == 0 && t[3][1] == 1)
	{
		qboolean white = (rgba == 0xFFFFFFFF);

		for (py = py0; py < py1; py++)
		{
			const byte *texel = img->pix + ((py - (int)y) * img->w + (px0 - (int)x)) * 4;
			byte *d = mr_fb + (py * mr_w + px0) * 4;

			for (px = px0; px < px1; px++, d += 4, texel += 4)
			{
				if (white)
				{
					if (texel[3] == 255)
					{
						d[0] = texel[0];
						d[1] = texel[1];
						d[2] = texel[2];
						d[3] = 255;
						mr_drawn = true;
					}
					else
					{
						MR_Blend(d, texel[0], texel[1], texel[2], texel[3]);
					}
				}
				else
				{
					MR_Blend(d, MR_Mul(texel[0], cr), MR_Mul(texel[1], cg), MR_Mul(texel[2], cb),
							MR_Mul(texel[3], ca));
				}
			}
		}

		return;
	}

	for (py = py0; py < py1; py++)
	{
		float v = (py + 0.5f - y) / h;
		byte *d = mr_fb + (py * mr_w + px0) * 4;

		for (px = px0; px < px1; px++, d += 4)
		{
			if (img)
			{
				float u = (px + 0.5f - x) / w;
				float s = t[0][0] * (1 - u) * (1 - v) + t[1][0] * u * (1 - v) + t[2][0] * u * v + t[3][0] * (1 - u) * v;
				float tt = t[0][1] * (1 - u) * (1 - v) + t[1][1] * u * (1 - v) + t[2][1] * u * v + t[3][1] * (1 - u) * v;
				int tx = (int)floorf(s * img->w);
				int ty = (int)floorf(tt * img->h);
				const byte *texel;

				if (tx < 0) tx = 0;
				if (tx >= img->w) tx = img->w - 1;
				if (ty < 0) ty = 0;
				if (ty >= img->h) ty = img->h - 1;

				texel = img->pix + (ty * img->w + tx) * 4;
				MR_Blend(d, MR_Mul(texel[0], cr), MR_Mul(texel[1], cg), MR_Mul(texel[2], cb),
						MR_Mul(texel[3], ca));
			}
			else
			{
				MR_Blend(d, cr, cg, cb, ca);
			}
		}
	}
}

/* convex polygon (4 corners) with pixel-centre sampling */
static void
MR_Poly4(const float c[4][2], unsigned rgba)
{
	float minx = c[0][0], maxx = c[0][0], miny = c[0][1], maxy = c[0][1];
	int px0, py0, px1, py1, px, py, i;
	float area = 0;
	int cr = rgba & 0xff, cg = (rgba >> 8) & 0xff, cb = (rgba >> 16) & 0xff, ca = (rgba >> 24) & 0xff;

	if (!mr_fb)
	{
		return;
	}

	for (i = 1; i < 4; i++)
	{
		if (c[i][0] < minx) minx = c[i][0];
		if (c[i][0] > maxx) maxx = c[i][0];
		if (c[i][1] < miny) miny = c[i][1];
		if (c[i][1] > maxy) maxy = c[i][1];
	}

	for (i = 0; i < 4; i++)
	{
		const float *a = c[i], *b = c[(i + 1) & 3];
		area += a[0] * b[1] - b[0] * a[1];
	}

	if (area == 0)
	{
		return;
	}

	px0 = (int)floorf(minx);
	py0 = (int)floorf(miny);
	px1 = (int)ceilf(maxx) + 1;
	py1 = (int)ceilf(maxy) + 1;
	MR_ClipBounds(&px0, &py0, &px1, &py1);

	for (py = py0; py < py1; py++)
	{
		for (px = px0; px < px1; px++)
		{
			float fx = px + 0.5f, fy = py + 0.5f;
			qboolean inside = true;

			for (i = 0; i < 4 && inside; i++)
			{
				const float *a = c[i], *b = c[(i + 1) & 3];
				float cross = (b[0] - a[0]) * (fy - a[1]) - (b[1] - a[1]) * (fx - a[0]);

				if ((area > 0 && cross < 0) || (area < 0 && cross > 0))
				{
					inside = false;
				}
			}

			if (inside)
			{
				MR_Blend(mr_fb + (py * mr_w + px) * 4, cr, cg, cb, ca);
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* slots                                                               */

static void
SW_BeginFrame(void)
{
	int w = viddef.width, h = viddef.height;

	if (w <= 0 || h <= 0)
	{
		return;
	}

	if (w != mr_w || h != mr_h || !mr_fb)
	{
		free(mr_fb);
		mr_fb = malloc(w * h * 4);
		mr_w = w;
		mr_h = h;
	}

	memset(mr_fb, 0, mr_w * mr_h * 4);
	mr_inframe = true;
	mr_drawn = false;
	SW_ResetClip();
}

static void
SW_EndFrame(void)
{
	if (!mr_inframe)
	{
		return;
	}

	mr_inframe = false;

	if (mr_drawn)
	{
		Draw_StretchRaw(0, 0, mr_w, mr_h, mr_w, mr_h, mr_fb, 32);
	}
}

static void
SW_Shutdown(void)
{
	SW_ReleasePics();
	free(mr_fb);
	mr_fb = NULL;
	mr_w = mr_h = 0;

	for (int i = 0; i < 4; i++)
	{
		free(mr_fonts[i].glyphs);
		mr_fonts[i].glyphs = NULL;
		mr_fonts[i].loaded = false;
	}
}

/* SPEC-002 rotation table (0-3) and SPEC-802 flips (4-7) */
static const float mr_rot[8][4][2] = {
	{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
	{{1, 0}, {1, 1}, {0, 1}, {0, 0}},
	{{1, 1}, {0, 1}, {0, 0}, {1, 0}},
	{{0, 1}, {0, 0}, {1, 0}, {1, 1}},
	{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
	{{1, 0}, {0, 0}, {0, 1}, {1, 1}},
	{{0, 1}, {1, 1}, {1, 0}, {0, 0}},
	{{1, 1}, {0, 1}, {0, 0}, {1, 0}},
};

static void
SW_DrawPic(int x, int y, const char *name, unsigned rgba)
{
	mrimage_t *img = MR_FindImage(name);
	float tc[4][2];

	if (!img)
	{
		return;
	}

	memcpy(tc, mr_rot[0], sizeof(tc));
	MR_Quad((float)x, (float)y, (float)img->w, (float)img->h, tc, img, rgba);
}

static void
SW_StretchPic(int x, int y, int w, int h, unsigned rgba, const char *name, int rotation)
{
	mrimage_t *img = MR_FindImage(name);
	float tc[4][2];

	if (!img)
	{
		return;
	}

	memcpy(tc, mr_rot[(rotation >= 0 && rotation < 8) ? rotation : 0], sizeof(tc));
	MR_Quad((float)x, (float)y, (float)w, (float)h, tc, img, rgba);
}

static void
SW_Fill(int x, int y, int w, int h, unsigned rgba)
{
	float tc[4][2] = {{0}};
	MR_Quad((float)x, (float)y, (float)w, (float)h, tc, NULL, rgba);
}

static void
SW_Line(float x1, float y1, float x2, float y2, float width, unsigned rgba)
{
	float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy), hw = width * 0.5f;
	float nx, ny, c[4][2];

	if (len <= 0)
	{
		return;
	}

	dx /= len;
	dy /= len;
	nx = -dy;
	ny = dx;

	/* square caps: extended by width/2 beyond both ends (SPEC-412 0x7c) */
	c[0][0] = x1 - hw * dx + hw * nx; c[0][1] = y1 - hw * dy + hw * ny;
	c[1][0] = x2 + hw * dx + hw * nx; c[1][1] = y2 + hw * dy + hw * ny;
	c[2][0] = x2 + hw * dx - hw * nx; c[2][1] = y2 + hw * dy - hw * ny;
	c[3][0] = x1 - hw * dx - hw * nx; c[3][1] = y1 - hw * dy - hw * ny;

	if (dx == 0 || dy == 0)
	{
		/* axis aligned: same coverage rule as a filled rectangle */
		float minx = c[0][0], maxx = c[0][0], miny = c[0][1], maxy = c[0][1];

		for (int i = 1; i < 4; i++)
		{
			if (c[i][0] < minx) minx = c[i][0];
			if (c[i][0] > maxx) maxx = c[i][0];
			if (c[i][1] < miny) miny = c[i][1];
			if (c[i][1] > maxy) maxy = c[i][1];
		}

		{
			float tc[4][2] = {{0}};
			MR_Quad(minx, miny, maxx - minx, maxy - miny, tc, NULL, rgba);
		}
		return;
	}

	MR_Poly4((const float (*)[2])c, rgba);
}

static void
SW_Bevel(int x, int y, int w, int h, unsigned rgba)
{
	int r = rgba & 0xff, g = (rgba >> 8) & 0xff, b = (rgba >> 16) & 0xff;
	unsigned a = rgba & 0xff000000;
	int dr = (int)(r * 0.7f), dg = (int)(g * 0.7f), db = (int)(b * 0.7f);
	int lr = (int)(r * 1.3f), lg = (int)(g * 1.3f), lb = (int)(b * 1.3f);
	unsigned dark, light;

	if (dr > 255) dr = 255;
	if (dg > 255) dg = 255;
	if (db > 255) db = 255;
	if (lr > 255) lr = 255;
	if (lg > 255) lg = 255;
	if (lb > 255) lb = 255;

	dark = a | (db << 16) | (dg << 8) | dr;
	light = a | (lb << 16) | (lg << 8) | lr;

	SW_Fill(x, y, w, h, rgba);
	SW_Line((float)(x + w), (float)(y + h), (float)(x + w), (float)y, 1, dark);
	SW_Line((float)(x + w), (float)(y + h), (float)x, (float)(y + h), 1, dark);
	SW_Line((float)x, (float)y, (float)(x + w), (float)y, 1, light);
	SW_Line((float)x, (float)y, (float)x, (float)(y + h), 1, light);
}

static void
SW_SetClip(int x, int y, int w, int h)
{
	if (w < 0 || h < 0)
	{
		return;
	}

	mr_rx = x;
	mr_ry = y;
	mr_rw = w;
	mr_rh = h;
	mr_hx0 = x;
	mr_hy0 = y;
	mr_hx1 = x + w;
	mr_hy1 = y + h;
	mr_clip = true;
}

static void
SW_ResetClip(void)
{
	mr_rx = mr_ry = 0;
	mr_rw = mr_w;
	mr_rh = mr_h;
	mr_hx0 = mr_hy0 = 0;
	mr_hx1 = mr_w;
	mr_hy1 = mr_h;
	mr_clip = false;
}

static void
SW_ClipRight(int x)
{
	if (x >= mr_rx)
	{
		mr_hx0 = mr_rx;
		mr_hx1 = x;
	}
}

static void
SW_ClipLeft(int x)
{
	if (x >= mr_rx)
	{
		mr_hx0 = x;
		mr_hx1 = mr_rx + mr_rw;
	}
}

static void
SW_Reclip(void)
{
	mr_hx0 = mr_rx;
	mr_hy0 = mr_ry;
	mr_hx1 = mr_rx + mr_rw;
	mr_hy1 = mr_ry + mr_rh;
}

/* ------------------------------------------------------------------ */
/* proportional fonts (SPEC-412 section 6)                             */

static mrfont_t *
MR_Font(const char *name)
{
	int i;

	if (!name)
	{
		return NULL;
	}

	for (i = 0; i < 4; i++)
	{
		if (!strcmp(mr_fonts[i].name, name))
		{
			mrfont_t *f = &mr_fonts[i];

			if (!f->loaded)
			{
				byte *buf;
				char path[MAX_QPATH];
				int len, n, j;

				f->loaded = true;
				Com_sprintf(path, sizeof(path), "%s.fnt", f->file);
				len = FS_LoadFile(path, (void **)&buf);

				if (len < 0 || !buf)
				{
					Com_Printf("Unable to find : %s\n", path);
					f->count = 0;
				}
				else
				{
					n = len / 28;
					f->glyphs = calloc(n > 0 ? n : 1, sizeof(mrglyph_t));
					f->count = n;

					for (j = 0; j < n; j++)
					{
						const byte *r = buf + j * 28;
						mrglyph_t *g = &f->glyphs[j];
						g->s0 = LittleFloat(*(float *)(r + 0));
						g->t0 = LittleFloat(*(float *)(r + 4));
						g->s1 = LittleFloat(*(float *)(r + 8));
						g->t1 = LittleFloat(*(float *)(r + 12));
						g->width = LittleLong(*(int *)(r + 16));
						g->height = LittleLong(*(int *)(r + 20));
						g->baseline = LittleLong(*(int *)(r + 24));
					}

					FS_FreeFile(buf);
				}
			}

			if (!f->page)
			{
				f->page = MR_FindImage(f->file);
			}

			return f;
		}
	}

	return NULL;
}

static const mrglyph_t *
MR_Glyph(const mrfont_t *f, int c)
{
	static const mrglyph_t empty;
	const mrglyph_t *g;
	int i = c - 32;

	if (i < 0 || i >= f->count)
	{
		g = &empty;
	}
	else
	{
		g = &f->glyphs[i];
	}

	if (g->width == 0)
	{
		i = '.' - 32;
		g = (i < f->count) ? &f->glyphs[i] : &empty;
	}

	return g;
}

static int
SW_FontHeight(const char *font)
{
	mrfont_t *f = MR_Font(font);
	return f ? f->lineh : 0;
}

static int
SW_StringWidth(const char *text, const char *font)
{
	mrfont_t *f = MR_Font(font);
	const byte *s;
	int w = 0;

	if (!f || !text)
	{
		return 0;
	}

	for (s = (const byte *)text; *s; s++)
	{
		int c = *s;

		if (c == 32)
		{
			w += f->lineh / 2;
		}
		else if (c >= f->first && c < f->end)
		{
			w += MR_Glyph(f, c)->width;
		}
	}

	return w;
}

static void
SW_DrawString(int x, int y, const char *text, unsigned rgba, const char *font, int keep)
{
	mrfont_t *f = MR_Font(font);
	const byte *s;
	int penx = x, base;
	unsigned color;

	if (!f || !text)
	{
		return;
	}

	if (!keep)
	{
		mr_textcolor = rgba;
	}

	color = mr_textcolor;
	base = y + f->ascent;

	for (s = (const byte *)text; *s; s++)
	{
		int c = *s;

		if (c < 32)
		{
			if (c == 10)
			{
				base += f->lineh;
				penx = x;
			}
			else if (c != 13)
			{
				const byte *tc = mr_textcolors[c];
				color = tc[0] | (tc[1] << 8) | (tc[2] << 16) | ((unsigned)tc[3] << 24);
				mr_textcolor = color;
			}
		}
		else if (c == 32)
		{
			penx += f->lineh / 2;
		}
		else if (c >= f->first && c < f->end)
		{
			const mrglyph_t *g = MR_Glyph(f, c);

			if (g->width > 0 && g->height > 0 && f->page)
			{
				float tc[4][2];

				tc[0][0] = g->s0; tc[0][1] = g->t0;
				tc[1][0] = g->s1; tc[1][1] = g->t0;
				tc[2][0] = g->s1; tc[2][1] = g->t1;
				tc[3][0] = g->s0; tc[3][1] = g->t1;
				MR_Quad((float)penx, (float)(base - g->baseline), (float)g->width, (float)g->height,
						tc, f->page, color);
			}

			penx += g->width;
		}
	}
}

/* ------------------------------------------------------------------ */
/* dispatch: the renderer's SPEC-002 table when it offers one          */

extern void *reflib_handle; /* vid.c */

static void *mr_relib;
static const sofrefexport_t *mr_re;
static OM_Set2DScale_t mr_setscale; /* MENU2: the renderer's 2D scale export (cl_hud.c's) */
static OM_SetAlphaImage_t mr_alphaimg; /* RGBA pictures that keep their alpha (own cursor) */
static float mr_zoom = 1.0f;        /* MENU-SCRATCH page zoom: extra factor and translation */
static int mr_zdx, mr_zdy;
static cvar_t *om_menusoft;
static om_fontref_t mr_fontrefs[8];
static char mr_fontnames[8][16];

static const sofrefexport_t *
MR_Re(void)
{
	if (!om_menusoft)
	{
		/* 0 (default since I1c): the renderer's SPEC-002 table, which also draws the menu models
		   (I4c verified its pictures, docs/impl/I4c.md); 1: software 2D layer, without models */
	om_menusoft = Cvar_Get("om_menusoft", "0", 0);
	}

	if (om_menusoft->value)
	{
		return NULL;
	}

	if (reflib_handle != mr_relib)
	{
		OM_GetSoFRefExports_t fn;

		mr_relib = reflib_handle;
		mr_re = NULL;
		mr_setscale = NULL;
		mr_alphaimg = NULL;
		MF_SetRenderer(NULL);

		if (reflib_handle)
		{
			fn = (OM_GetSoFRefExports_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_EXPORTS_NAME);

			if (fn)
			{
				mr_re = fn();
				mr_setscale = (OM_Set2DScale_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_SETSCALE_NAME);
				/* MENU-SCRATCH: TrueType menu text needs pictures with alpha */
				mr_alphaimg = (OM_SetAlphaImage_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_ALPHAIMAGE_NAME);
				MF_SetRenderer(mr_alphaimg);
			}
		}
	}

	return mr_re;
}

static const om_fontref_t *
MR_FontRef(const char *font)
{
	int i;

	/* MENU-SCRATCH: the TrueType-only styles of the landing page (m_font.c)
	   fall back to stock bitmap fonts */
	if (!Q_stricmp(font, "logo"))
	{
		font = "title";
	}
	else if (!Q_stricmp(font, "logosmall"))
	{
		font = "medium";
	}

	for (i = 0; i < 8; i++)
	{
		if (mr_fontrefs[i].chars && !strcmp(mr_fontnames[i], font))
		{
			return &mr_fontrefs[i];
		}
	}

	for (i = 0; i < 8; i++)
	{
		if (!mr_fontrefs[i].chars)
		{
			Q_strlcpy(mr_fontnames[i], font, sizeof(mr_fontnames[i]));
			mr_fontrefs[i].chars = mr_fontnames[i];
			mr_fontrefs[i].length = strlen(mr_fontnames[i]);
			mr_fontrefs[i].capacity = mr_fontrefs[i].length;
			return &mr_fontrefs[i];
		}
	}

	/* more than 8 distinct names: reuse the last slot */
	Q_strlcpy(mr_fontnames[7], font, sizeof(mr_fontnames[7]));
	mr_fontrefs[7].length = strlen(mr_fontnames[7]);
	return &mr_fontrefs[7];
}

static void
MR_Bytes(unsigned rgba, unsigned char out[4])
{
	out[0] = rgba & 0xff;
	out[1] = (rgba >> 8) & 0xff;
	out[2] = (rgba >> 16) & 0xff;
	out[3] = (rgba >> 24) & 0xff;
}

/*
 * OpenMerc MENU2 (docs/DEVIATIONS.md "UI scale"): with the renderer's table
 * the menus are laid out at scale 1 in a virtual window of W/f x H/f pixels
 * (f = MR_Scale) and the renderer multiplies every 2D coordinate, size,
 * clip rectangle and the menu-model projection by f. The software layer
 * (om_menusoft 1) and scr_aspect 0 keep f = 1 and the original's layout.
 */
qboolean
MR_Virtual(void)
{
	return MR_Re() && mr_setscale && SCR_AspectCorrect();
}

/*
 * OpenMerc MENU-SCRATCH (docs/impl/MENU2.md "Widescreen composition"): the
 * menu factor is the menus' own (the HUD and the console keep SCR_UIScale).
 * The composition is the 640x480 SoF panel plus, from aspect 3:2 on, the
 * navigation rail at its left (MENU_COMP_RAIL units). Auto (ui_scale 0):
 * f0 = min(H / 540, W / (composition + 32)) leaves a margin of the tile
 * backdrop around the panel; a whole factor within reach of f0 that still
 * fits (H / 480, W / composition) is taken instead, so pics and fonts stay
 * nearest-sampled and crisp at the usual sizes (1080p 2, 2160p 4, 2560x1080 2,
 * 2560x1600 3); 1440p keeps its own 2.67 (3 would leave no margin). ui_scale > 0 = that factor, at most the fit.
 * The rail part of the composition includes MENU_RAIL_MARGIN, so the factor
 * the fit allows always leaves room to show the rail (2560x1440: 853 >= 848).
 */
static cvar_t *mr_uiscale, *mr_menurail, *mr_menulayout;

/* MENU-SCRATCH: menu_layout 1 = openMERC wide layout (m_wide.c) */
qboolean
MR_WideLayout(void)
{
	if (!mr_menulayout)
	{
		mr_menulayout = Cvar_Get("menu_layout", "1", CVAR_ARCHIVE);
	}

	return mr_menulayout->value != 0;
}

qboolean
MR_RailWanted(void)
{
	if (!mr_menurail)
	{
		mr_menurail = Cvar_Get("menu_rail", "1", CVAR_ARCHIVE);
	}

	return mr_menurail->value != 0 && viddef.width * 2 >= viddef.height * 3;
}

static float
MR_ScaleFor(float comp)
{
	float W = (float)viddef.width, H = (float)viddef.height;
	float fit, f;

	fit = H / 480.0f;

	if (W / comp < fit)
	{
		fit = W / comp;
	}

	if (fit < 0.5f)
	{
		fit = 0.5f;
	}

	if (mr_uiscale->value > 0.0f)
	{
		f = mr_uiscale->value;
		return (f > fit) ? fit : ((f < 0.5f) ? 0.5f : f);
	}

	f = H / 540.0f;

	if (W / (comp + 32.0f) < f)
	{
		f = W / (comp + 32.0f);
	}

	if (f >= 1.5f)
	{
		float c = ceilf(f), fl = floorf(f);

		/* MENU-SCRATCH: rounded up only while the 480-unit panel keeps a
		   margin of 10 units above and below; 1440p at 3 put the main
		   menu's art edge to edge and squeezed the nav panel out */
		if (c <= fit + 0.001f && c - f <= 0.34f && c * 500.0f <= H + 0.5f)
		{
			return c;
		}

		if (f - fl <= 0.25f)
		{
			return fl;
		}
	}

	return (f > fit) ? fit : ((f < 0.5f) ? 0.5f : f);
}

static float
MR_MenuScale(void)
{
	float f, alone;

	if (!mr_uiscale)
	{
		mr_uiscale = Cvar_Get("ui_scale", "0", CVAR_ARCHIVE);
	}

	/* MENU-SCRATCH modern scaling (owner t91u: whole-factor blow-ups of the
	   640x480 menus were too large): the openMERC wide layout is laid out on
	   a window 720 units tall at any resolution (1080p 1.5, 1440p 2, 2160p 3),
	   kept at least 640x480 units; its text is TrueType at a share of the
	   window height (m_font.c), so a fractional factor stays sharp. */
	if (MR_WideLayout() && mr_uiscale->value <= 0.0f)
	{
		f = (float)viddef.height / 720.0f;

		if (f > (float)viddef.width / 640.0f)
		{
			f = (float)viddef.width / 640.0f;
		}

		return (f < 0.5f) ? 0.5f : f;
	}

	/* the openMERC wide layout fills the window at the panel's own factor;
	   the nav panel is shown where it fits (MWD_NavFits) */
	if (!MR_RailWanted() || MR_WideLayout())
	{
		return MR_ScaleFor(640.0f);
	}

	f = MR_ScaleFor(640.0f + (float)(MENU_COMP_RAIL + MENU_RAIL_MARGIN));

	/* a whole factor for the panel alone beats a fractional one with the
	   rail (1680x1050: 2 without the rail, 1.91 with it); the rail is then
	   not shown because it does not fit (MR_RailShown) */
	if (mr_uiscale->value <= 0.0f && f != floorf(f))
	{
		alone = MR_ScaleFor(640.0f);

		if (alone == floorf(alone) && alone > f)
		{
			return alone;
		}
	}

	return f;
}

float
MR_Scale(void)
{
	return MR_Virtual() ? MR_MenuScale() : 1.0f;
}

/* the rail is shown when it is wanted and the composition fits the window */
qboolean
MR_RailShown(void)
{
	if (MR_Virtual() && MR_WideLayout())
	{
		return MR_RailWanted() && MR_VirtualW() >= 2 * 10 + 204 + 8 + 600; /* MWD_NavFits */
	}

	return MR_Virtual() && MR_RailWanted() && MR_VirtualW() >= 640 + MENU_COMP_RAIL + MENU_RAIL_MARGIN;
}

int
MR_VirtualW(void)
{
	return (int)((float)viddef.width / MR_Scale() + 0.5f);
}

int
MR_VirtualH(void)
{
	return (int)((float)viddef.height / MR_Scale() + 0.5f);
}

void
MR_BeginFrame(void)
{
	const sofrefexport_t *re = MR_Re();

	MF_NewFrame();

	if (re)
	{
		if (mr_setscale)
		{
			mr_setscale(MR_Scale());
		}

		re->Set2D(0);
		re->ResetClipRect();
		return;
	}

	SW_BeginFrame();
}

void
MR_EndFrame(void)
{
	const sofrefexport_t *re = MR_Re();

	mr_zoom = 1.0f;
	mr_zdx = mr_zdy = 0;

	if (re)
	{
		re->ResetClipRect();

		if (mr_setscale)
		{
			mr_setscale(1.0f);
		}

		return;
	}

	SW_EndFrame();
}

/*
 * MENU-SCRATCH page zoom: until MR_ZoomEnd every coordinate is translated by
 * dx, dy and the renderer scale is MR_Scale() * z, so a point P of the page
 * lands on (P + d) * z virtual units. Only with the renderer's scale export.
 */
void
MR_ZoomBegin(int dx, int dy, float z)
{
	if (!MR_Virtual() || z <= 1.0f)
	{
		return;
	}

	mr_zoom = z;
	mr_zdx = dx;
	mr_zdy = dy;
	mr_setscale(MR_Scale() * z);
}

void
MR_ZoomEnd(void)
{
	if (mr_zoom == 1.0f)
	{
		return;
	}

	mr_zoom = 1.0f;
	mr_zdx = mr_zdy = 0;

	if (mr_setscale)
	{
		mr_setscale(MR_Scale());
	}
}

/* I1c: the renderer's menu-model mode around one Ghoul model (SPEC-803 section 1 Rendering steps 1 and 3,
   SPEC-002 slots 0xb8 / 0xb4); false with the software layer, which draws no models */
qboolean
MR_ModelBegin(qboolean forcedcamera)
{
	const sofrefexport_t *re = MR_Re();

	if (!re || !re->Set3DInMenu || !re->Set2D)
	{
		return false;
	}

	re->Set3DInMenu(forcedcamera ? 1 : 0);
	return true;
}

void
MR_ModelEnd(qboolean forcedcamera)
{
	const sofrefexport_t *re = MR_Re();

	if (re && re->Set2D)
	{
		re->Set2D(forcedcamera ? 1 : 0);
	}
}

void
MR_Shutdown(void)
{
	SW_Shutdown();
	mr_relib = NULL;
	mr_re = NULL;
}

void
MR_ReleasePics(void)
{
	SW_ReleasePics();
}

void
MR_GetPicSize(int *w, int *h, const char *name)
{
	const sofrefexport_t *re = MR_Re();

	if (re)
	{
		char nb[MAX_QPATH];

		/* own copy: callers pass va() buffers the renderer may reuse */
		Q_strlcpy(nb, name, sizeof(nb));
		re->DrawGetPicSize(w, h, nb);
		return;
	}

	SW_GetPicSize(w, h, name);
}

void
MR_DrawPic(int x, int y, const char *name, unsigned rgba)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (re)
	{
		char nb[MAX_QPATH];

		/* own copy: callers pass va() buffers the renderer may reuse */
		Q_strlcpy(nb, name, sizeof(nb));
		re->DrawPic(x, y, nb, rgba);
		return;
	}

	SW_DrawPic(x, y, name, rgba);
}

/* MENU-SCRATCH: sub-rectangle l..r x t..b of the pic at its pixel size (the
   9-slice chrome of the wide layout); nothing with the software layer */
void
MR_DrawPicPart(int x, int y, int l, int t, int r, int b, unsigned rgba, const char *name)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (re && re->DrawPicPart && r > l && b > t)
	{
		char nb[MAX_QPATH];
		unsigned char c[4];

		Q_strlcpy(nb, name, sizeof(nb));
		MR_Bytes(rgba, c);
		re->DrawPicPart(x, y, l, t, r, b, c, nb);
	}
}

void
MR_StretchPic(int x, int y, int w, int h, unsigned rgba, const char *name, int rotation)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (re)
	{
		char nb[MAX_QPATH];

		/* own copy: callers pass va() buffers the renderer may reuse */
		Q_strlcpy(nb, name, sizeof(nb));
		unsigned char c[4];

		MR_Bytes(rgba, c);
		re->DrawStretchPic(x, y, w, h, c, nb, rotation);
		return;
	}

	SW_StretchPic(x, y, w, h, rgba, name, rotation);
}

void
MR_Fill(int x, int y, int w, int h, unsigned rgba)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (re)
	{
		unsigned char c[4];

		MR_Bytes(rgba, c);
		re->DrawFill(x, y, w, h, c);
		return;
	}

	SW_Fill(x, y, w, h, rgba);
}

/* MENU-SCRATCH (owner t95u): the own flat chrome of the wide layout: a box
   with an optional 1-2 pixel border and a top accent bar, every edge on a
   whole window pixel at any scale (no pictures) */
static void
MR_PxRect(const sofrefexport_t *re, int x0, int y0, int x1, int y1, unsigned rgba)
{
	unsigned char c[4];

	if (x1 <= x0 || y1 <= y0 || !(rgba >> 24))
	{
		return;
	}

	MR_Bytes(rgba, c);
	re->DrawFill(x0, y0, x1 - x0, y1 - y0, c);
}

void
MR_FlatBox(int x, int y, int w, int h, unsigned fill, unsigned border, unsigned accent,
		qboolean openbottom)
{
	const sofrefexport_t *re = MR_Re();
	float s;
	int x0, y0, x1, y1, t, a;

	if (w <= 0 || h <= 0)
	{
		return;
	}

	if (!re || !mr_setscale)
	{
		if (fill >> 24)
		{
			MR_Fill(x, y, w, h, fill);
		}

		if (border >> 24)
		{
			MR_Fill(x, y, w, 1, border);
			MR_Fill(x, y, 1, h, border);
			MR_Fill(x + w - 1, y, 1, h, border);

			if (!openbottom)
			{
				MR_Fill(x, y + h - 1, w, 1, border);
			}
		}

		if (accent >> 24)
		{
			MR_Fill(x, y, w, 2, accent);
		}

		return;
	}

	s = MR_Scale() * mr_zoom;
	x += mr_zdx;
	y += mr_zdy;
	x0 = (int)floorf(x * s + 0.5f);
	y0 = (int)floorf(y * s + 0.5f);
	x1 = (int)floorf((x + w) * s + 0.5f);
	y1 = (int)floorf((y + h) * s + 0.5f);
	t = s >= 2.5f ? 2 : 1;
	a = (int)floorf(2 * s + 0.5f);
	a = a < 2 ? 2 : a;

	mr_setscale(1.0f);
	MR_PxRect(re, x0, y0, x1, y1, fill);
	MR_PxRect(re, x0, y0, x1, y0 + t, border);
	MR_PxRect(re, x0, y0, x0 + t, y1, border);
	MR_PxRect(re, x1 - t, y0, x1, y1, border);

	if (!openbottom)
	{
		MR_PxRect(re, x0, y1 - t, x1, y1, border);
	}

	MR_PxRect(re, x0, y0, x1, y0 + a, accent);
	mr_setscale(s);
}

/* an arrow of whole pixels pointing up or down (own scroll bar) */
void
MR_FlatArrow(int x, int y, int w, int h, qboolean down, unsigned rgba)
{
	const sofrefexport_t *re = MR_Re();
	float s;
	int x0, y0, x1, y1, n, i;

	if (!re || !mr_setscale)
	{
		MR_Fill(x + w / 4, y + h / 4, w / 2, h / 2, rgba);
		return;
	}

	s = MR_Scale() * mr_zoom;
	x += mr_zdx;
	y += mr_zdy;
	x0 = (int)floorf(x * s + 0.5f);
	y0 = (int)floorf(y * s + 0.5f);
	x1 = (int)floorf((x + w) * s + 0.5f);
	y1 = (int)floorf((y + h) * s + 0.5f);
	n = (x1 - x0) / 3;          /* rows: half the arrow width */
	mr_setscale(1.0f);

	for (i = 0; i < n; i++)
	{
		int cx = (x0 + x1) / 2, half = i, ry;

		ry = down ? (y0 + y1 - n) / 2 + n - 1 - i : (y0 + y1 - n) / 2 + i;
		MR_PxRect(re, cx - half - 1, ry, cx + half + 1, ry + 1, rgba);
	}

	mr_setscale(s);
}

/* MENU-SCRATCH (owner t95u): the own mouse pointer, an arrow rendered by
   code at the window pixel size (no stock cursor picture) */
static int mr_cursorpx;

static float
MR_SegDist(float px, float py, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);

	t = t < 0 ? 0 : (t > 1 ? 1 : t);
	dx = ax + t * dx - px;
	dy = ay + t * dy - py;
	return sqrtf(dx * dx + dy * dy);
}

void
MR_DrawOwnCursor(int mx, int my)
{
	static const float poly[][2] = {{0, 0}, {0, 21}, {5.5f, 16}, {9.5f, 25}, {13, 23.5f},
		{9, 15}, {16, 15}};
	const int np = sizeof(poly) / sizeof(poly[0]);
	const sofrefexport_t *re = MR_Re();
	float s = MR_Scale() * mr_zoom;
	int px = (int)floorf(26 * s * 0.8f + 0.5f), x0, y0;

	px = px < 16 ? 16 : (px > 128 ? 128 : px);

	if (px == mr_cursorpx)
	{
		int cw = -1, ch = -1;

		/* the renderer may have dropped it (vid_restart) */
		MR_GetPicSize(&cw, &ch, "pics/menus/om/cursor");
		mr_cursorpx = cw > 0 ? mr_cursorpx : 0;
	}

	if (px != mr_cursorpx)
	{
		static byte img[128 * 128 * 4];
		float k = 26.0f / px; /* design units per pixel */
		int i, j, a, b, e;

		for (j = 0; j < px; j++)
		{
			for (i = 0; i < px; i++)
			{
				int fillc = 0, edgec = 0;

				for (b = 0; b < 4; b++)
				{
					for (a = 0; a < 4; a++)
					{
						float x = (i + (a + 0.5f) / 4) * k - 0.8f, y = (j + (b + 0.5f) / 4) * k - 0.8f;
						qboolean in = false;
						float d = 1e9f;

						for (e = 0; e < np; e++)
						{
							const float *p = poly[e], *q = poly[(e + 1) % np];
							float dd = MR_SegDist(x, y, p[0], p[1], q[0], q[1]);

							d = dd < d ? dd : d;

							if ((p[1] > y) != (q[1] > y) &&
								x < (q[0] - p[0]) * (y - p[1]) / (q[1] - p[1]) + p[0])
							{
								in = !in;
							}
						}

						if (in && d > 1.3f)
						{
							fillc++;
						}
						else if (in || d < 0.8f)
						{
							edgec++;
						}
					}
				}

				{
					byte *o = img + (j * px + i) * 4;
					int t = fillc + edgec;

					/* light grey body, near-black outline */
					o[0] = (byte)(t ? (fillc * 236 + edgec * 14) / t : 0);
					o[1] = (byte)(t ? (fillc * 238 + edgec * 12) / t : 0);
					o[2] = (byte)(t ? (fillc * 232 + edgec * 10) / t : 0);
					o[3] = (byte)(t * 255 / 16);
				}
			}
		}

		if (mr_alphaimg)
		{
			mr_alphaimg("pics/menus/om/cursor", px, px, img);
		}
		else
		{
			MR_RawPic("pics/menus/om/cursor", px, px, img);
		}
		mr_cursorpx = px;
	}

	if (!re || !mr_setscale)
	{
		MR_StretchPic(mx, my, 20, 20, 0xFFFFFFFF, "pics/menus/om/cursor", 0);
		return;
	}

	x0 = (int)floorf((mx + mr_zdx) * s + 0.5f);
	y0 = (int)floorf((my + mr_zdy) * s + 0.5f);
	mr_setscale(1.0f);
	{
		unsigned char c[4] = {255, 255, 255, 255};
		char nb[MAX_QPATH];

		Q_strlcpy(nb, "pics/menus/om/cursor", sizeof(nb));
		re->DrawStretchPic(x0 - 1, y0 - 1, px, px, c, nb, 0);
	}
	mr_setscale(s);
}

/* a vertical gradient of whole-pixel bands (backdrop of the wide layout) */
void
MR_FlatGradient(int x, int y, int w, int h, unsigned top, unsigned bottom)
{
	const sofrefexport_t *re = MR_Re();
	float s;
	int x0, y0, x1, y1, i, n = 48;

	if (!re || !mr_setscale)
	{
		MR_Fill(x, y, w, h, top);
		return;
	}

	s = MR_Scale() * mr_zoom;
	x += mr_zdx;
	y += mr_zdy;
	x0 = (int)floorf(x * s + 0.5f);
	y0 = (int)floorf(y * s + 0.5f);
	x1 = (int)floorf((x + w) * s + 0.5f);
	y1 = (int)floorf((y + h) * s + 0.5f);
	mr_setscale(1.0f);

	for (i = 0; i < n; i++)
	{
		int a = y0 + (y1 - y0) * i / n, b = y0 + (y1 - y0) * (i + 1) / n, k;
		unsigned c = 0;

		for (k = 0; k < 32; k += 8)
		{
			int p = (top >> k) & 255, q = (bottom >> k) & 255;
			c |= (unsigned)(p + (q - p) * i / (n - 1)) << k;
		}

		MR_PxRect(re, x0, a, x1, b, c);
	}

	mr_setscale(s);
}

void
MR_Bevel(int x, int y, int w, int h, unsigned rgba)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (re)
	{
		unsigned char c[4];

		MR_Bytes(rgba, c);
		re->DrawBevelBox(x, y, w, h, c);
		return;
	}

	SW_Bevel(x, y, w, h, rgba);
}

void
MR_Line(float x1, float y1, float x2, float y2, float width, unsigned rgba)
{
	const sofrefexport_t *re = MR_Re();

	x1 += mr_zdx;
	x2 += mr_zdx;
	y1 += mr_zdy;
	y2 += mr_zdy;

	if (re)
	{
		unsigned char c[4];

		MR_Bytes(rgba, c);
		re->DrawLine(x1, y1, x2, y2, width, c);
		return;
	}

	SW_Line(x1, y1, x2, y2, width, rgba);
}

void
MR_SetClip(int x, int y, int w, int h)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (re)
	{
		re->SetClipRect(x, y, w, h);
		return;
	}

	SW_SetClip(x, y, w, h);
}

void
MR_ResetClip(void)
{
	const sofrefexport_t *re = MR_Re();

	if (re)
	{
		re->ResetClipRect();
		return;
	}

	SW_ResetClip();
}

void
MR_ClipRight(int x)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;

	if (re)
	{
		re->ClipRightTo(x);
		return;
	}

	SW_ClipRight(x);
}

void
MR_ClipLeft(int x)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;

	if (re)
	{
		re->ClipLeftFrom(x);
		return;
	}

	SW_ClipLeft(x);
}

void
MR_Reclip(void)
{
	const sofrefexport_t *re = MR_Re();

	if (re)
	{
		re->ReapplyClipRect();
		return;
	}

	SW_Reclip();
}

void
MR_RawPic(const char *name, int w, int h, const byte *rgba)
{
	const sofrefexport_t *re = MR_Re();

	if (re)
	{
		char nb[MAX_QPATH];

		/* own copy: callers pass va() buffers the renderer may reuse */
		Q_strlcpy(nb, name, sizeof(nb));
		re->SetRawImage(nb, w, h, (unsigned char *)rgba);
		return;
	}

	SW_RawPic(name, w, h, rgba);
}

void
MR_FreePic(const char *name)
{
	const sofrefexport_t *re = MR_Re();

	if (re)
	{
		char nb[MAX_QPATH];

		/* own copy: callers pass va() buffers the renderer may reuse */
		Q_strlcpy(nb, name, sizeof(nb));
		re->UnregisterPic(nb);
		return;
	}

	SW_FreePic(name);
}

void
MR_ColorCorrection(float a, float b, float c, float d)
{
	const sofrefexport_t *re = MR_Re();

	if (re)
	{
		re->SetColorCorrection(a, b, c, d);
		return;
	}

	SW_ColorCorrection(a, b, c, d);
}

/* MENU-SCRATCH: one glyph of a TrueType atlas at pixel size (2D scale 1) */
static void
MR_FontPart(int x, int y, int l, int t, int r, int b, const byte c[4], const char *name)
{
	char nb[MAX_QPATH];
	unsigned char cc[4];

	Q_strlcpy(nb, name, sizeof(nb));
	memcpy(cc, c, 4);
	mr_re->DrawPicPart(x, y, l, t, r, b, cc, nb);
}

/* MENU-SCRATCH (fonts brief): one console character in the monospaced OFL
   face (m_font.c MF_DrawCell); false: the caller draws the bitmap one */
qboolean
MR_ConsoleChar(int x, int y, int ch, int cell)
{
	static const byte color[4] = {0xE6, 0xE6, 0xE6, 0xFF};
	const sofrefexport_t *re = MR_Re();

	if (!re || !re->DrawPicPart)
	{
		return false;
	}

	return MF_DrawCell(x, y, ch, cell, color, MR_FontPart);
}

/* pixels between console text rows for this cell (m_font.c MF_ConsolePitch);
   the cell when the bitmap font draws */
int
MR_ConsolePitch(int cell)
{
	const sofrefexport_t *re = MR_Re();

	if (!re || !re->DrawPicPart)
	{
		return cell;
	}

	return MF_ConsolePitch(cell);
}

/* MENU-SCRATCH: the TrueType path (m_font.c) is taken with the renderer's
   table, its scale export and a font of that name */
static qboolean
MR_TrueType(const char *font)
{
	const sofrefexport_t *re = MR_Re();

	return re && re->DrawPicPart && mr_setscale && MF_Ready(font);
}

void
MR_DrawString(int x, int y, const char *text, unsigned rgba, const char *font, int keep)
{
	const sofrefexport_t *re = MR_Re();

	x += mr_zdx;
	y += mr_zdy;

	if (MR_TrueType(font))
	{
		float s = MR_Scale() * mr_zoom;

		/* text is laid out in menu units and drawn at whole window pixels */
		mr_setscale(1.0f);
		MF_Draw((int)floorf(x * s + 0.5f), (int)floorf(y * s + 0.5f), text, rgba, font, keep, mr_zoom,
			MR_FontPart);
		mr_setscale(s);
		return;
	}

	/* the bitmap fonts have no CP1251 Ё/ё (0xA8/0xB8): draw Е/е */
	{
		static char nb[1024];
		size_t i;

		for (i = 0; text[i] && i < sizeof(nb) - 1; i++)
		{
			unsigned char c = (unsigned char)text[i];
			nb[i] = (char)(c == 0xA8 ? 0xC5 : c == 0xB8 ? 0xE5 : c);
		}

		nb[i] = 0;
		text = nb;
	}

	if (re)
	{
		re->DrawFontString(x, y, (char *)text, rgba, MR_FontRef(font), keep);
		return;
	}

	SW_DrawString(x, y, text, rgba, font, keep);
}

int
MR_StringWidth(const char *text, const char *font)
{
	const sofrefexport_t *re = MR_Re();

	if (MR_TrueType(font))
	{
		return (int)ceilf(MF_WidthPx(text, font, 1.0f) / MR_Scale() - 0.01f);
	}

	if (re)
	{
		return re->FontStringWidth((char *)text, MR_FontRef(font));
	}

	return SW_StringWidth(text, font);
}

int
MR_FontHeight(const char *font)
{
	const sofrefexport_t *re = MR_Re();

	if (MR_TrueType(font))
	{
		return (int)ceilf(MF_LineHeightPx(font, 1.0f) / MR_Scale() - 0.01f);
	}

	if (re)
	{
		return re->FontLineHeight(MR_FontRef(font));
	}

	return SW_FontHeight(font);
}

/* test capture of the frame being drawn (before the buffer swap): an
   uncompressed 32-bit TGA, bottom row first (om_menushot, m_test.c) */
qboolean
MR_Capture(const char *path)
{
	const sofrefexport_t *re = MR_Re();
	int w = viddef.width, h = viddef.height, i;
	byte *buf, header[18];
	FILE *f;

	if (w <= 0 || h <= 0)
	{
		return false;
	}

	buf = malloc((size_t)w * h * 4);

	if (!buf)
	{
		return false;
	}

	if (!re && Cvar_VariableValue("om_menushotgl") && reflib_handle)
	{
		/* test: read the presented software layer back through the renderer */
		OM_GetSoFRefExports_t fn = (OM_GetSoFRefExports_t)Sys_GetProcAddress(reflib_handle,
				OM_SOFREF_EXPORTS_NAME);

		re = fn ? fn() : NULL;
	}

	if (re && re->ReadThumbnail)
	{
		re->ReadThumbnail(buf, w, h); /* 1:1 sampling, rows bottom to top */
	}
	else if (mr_fb && mr_w == w && mr_h == h)
	{
		for (i = 0; i < h; i++)
		{
			memcpy(buf + (size_t)i * w * 4, mr_fb + (size_t)(h - 1 - i) * w * 4, (size_t)w * 4);
		}
	}
	else
	{
		free(buf);
		return false;
	}

	for (i = 0; i < w * h; i++)
	{
		byte t = buf[i * 4];

		buf[i * 4] = buf[i * 4 + 2];
		buf[i * 4 + 2] = t;
	}

	memset(header, 0, sizeof(header));
	header[2] = 2;
	header[12] = w & 255;
	header[13] = (w >> 8) & 255;
	header[14] = h & 255;
	header[15] = (h >> 8) & 255;
	header[16] = 32;
	f = Q_fopen(path, "wb");

	if (!f)
	{
		free(buf);
		return false;
	}

	fwrite(header, 1, 18, f);
	fwrite(buf, 1, (size_t)w * h * 4, f);
	fclose(f);
	free(buf);
	return true;
}

/* savegame thumbnail (SPEC-801 section 1 step 2): renderer slot 0xd4, 128 x 128
   RGBA, rows bottom to top; read through the renderer even when the menus draw
   in software */
qboolean
MR_Thumbnail(byte *out)
{
	OM_GetSoFRefExports_t fn;
	const sofrefexport_t *re;

	if (!reflib_handle)
	{
		return false;
	}

	fn = (OM_GetSoFRefExports_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_EXPORTS_NAME);
	re = fn ? fn() : NULL;

	if (!re || !re->ReadThumbnail)
	{
		return false;
	}

	re->ReadThumbnail(out, 128, 128);
	return true;
}
