/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GFX-modern r_om_look: the colour of the picture, graded in float
 * at the end of the view instead of SoF's colour table.
 *
 * SoF corrects colour by an 8-bit table built from vid_gamma, vid_brightness
 * and vid_contrast and baked into every texture and lightmap when they load
 * (SPEC-413 §9): banding in dark gradients, a renderer restart for every
 * change. With r_om_look 1 that table is neutral and the composite pass
 * (gl3_post.c) does it all on the linear HDR scene, as a film camera (the
 * MGS5 / Fox Engine way: linear light, exposure, a filmic curve, a grade):
 *
 *   auto exposure (r_om_hdr) -> grade exposure + menu brightness -> white
 *   balance (luminance kept) -> saturation -> contrast around mid grey ->
 *   ACES filmic curve (RRT+ODT fit, mid grey stays 0.18) -> menu gamma ->
 *   menu contrast (S curve) -> optional 3D LUT -> vignette, film grain and
 *   dither.
 *
 * r_om_camera 0..1 (the menu's "Film Camera") blends from the plain picture
 * (the shoulder curve of r_om_hdr, original mid tones) to the full camera.
 *
 * Grades come from looks/om_looks.txt (a loose copy in the game folder wins):
 *   grade <name> <ev> <temperature> <tint> <saturation> <contrast> <vignette> <grain> [lut]
 *   map <map name, '*' allowed> <grade>
 * The first matching map line chooses the grade; r_om_look_preset forces one.
 * r_om_looks_reload re-reads the file in the game. The LUT is a strip of N
 * slices of N x N (blue across the slices, red across a slice, green down),
 * e.g. 1024 x 32: grade looks/neutral_lut.png over a screenshot in any image
 * editor and name it in the grade.
 */

#include "header/local.h"

extern unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *comp, int req);
extern void stbi_image_free(void *p);

#define LOOK_MAX 32
#define LOOK_MAPS 64
#define LOOK_UNIT 4

typedef struct
{
	char name[32];
	float ev, temp, tint, sat, contrast;
	float vignette, grain;
	char lut[MAX_QPATH];
} look_t;

static const look_t neutral = {"neutral", 0, 0, 0, 1.0f, 1.0f, 0, 0, ""};

static struct
{
	look_t looks[LOOK_MAX];
	int numlooks;
	struct
	{
		char pat[MAX_QPATH];
		int look;
	} maps[LOOK_MAPS];
	int nummaps;
	qboolean loaded;
	char map[MAX_QPATH];
	int current;
	GLuint luttex;
	int lutsize;
	char lutname[MAX_QPATH];
} LK;

static cvar_t *r_om_look, *r_om_camera, *r_om_look_preset, *r_om_looks;
static qboolean lookcmds;

static qboolean
Glob(const char *p, const char *s)
{
	for (; *p; p++, s++)
	{
		if (*p == '*')
		{
			for (p++; ; s++)
			{
				if (Glob(p, s))
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
LookByName(const char *name)
{
	int i;

	for (i = 0; i < LK.numlooks; i++)
	{
		if (!Q_stricmp(LK.looks[i].name, name))
		{
			return i;
		}
	}

	return -1;
}

static void
LookLine(char *line, int lineno)
{
	char *tok[20];
	int n = 0;
	char *p = line, *c;

	if ((c = strchr(line, '#')))
	{
		*c = 0;
	}

	while (n < 20)
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

	if (!Q_stricmp(tok[0], "grade") && (n == 9 || n == 10))
	{
		int k = LookByName(tok[1]);
		look_t *l;

		if (k < 0)
		{
			if (LK.numlooks >= LOOK_MAX)
			{
				return;
			}

			k = LK.numlooks++;
		}

		l = &LK.looks[k];
		memset(l, 0, sizeof(*l));
		Q_strlcpy(l->name, tok[1], sizeof(l->name));
		l->ev = (float)atof(tok[2]);
		l->temp = (float)atof(tok[3]);
		l->tint = (float)atof(tok[4]);
		l->sat = (float)atof(tok[5]);
		l->contrast = (float)atof(tok[6]);
		l->vignette = (float)atof(tok[7]);
		l->grain = (float)atof(tok[8]);

		if (n == 10)
		{
			Q_strlcpy(l->lut, tok[9], sizeof(l->lut));
		}
	}
	else if (!Q_stricmp(tok[0], "map") && n == 3)
	{
		int k = LookByName(tok[2]);

		if (k < 0 || LK.nummaps >= LOOK_MAPS)
		{
			R_Printf(PRINT_ALL, "r_om_looks: line %d: unknown look %s\n", lineno, tok[2]);
			return;
		}

		Q_strlcpy(LK.maps[LK.nummaps].pat, tok[1], sizeof(LK.maps[0].pat));
		Q_strlwr(LK.maps[LK.nummaps].pat);
		LK.maps[LK.nummaps].look = k;
		LK.nummaps++;
	}
	else
	{
		R_Printf(PRINT_ALL, "r_om_looks: line %d not understood\n", lineno);
	}
}

static void
LookLoad(void)
{
	char *buf = NULL, *text, *line;
	int len, lineno = 0;

	LK.numlooks = 1;
	LK.looks[0] = neutral;
	LK.nummaps = 0;
	LK.loaded = true;

	len = ri.FS_LoadFile(r_om_looks->string, (void **)&buf);

	if (len <= 0 || !buf)
	{
		R_Printf(PRINT_ALL, "r_om_looks: %s not found, neutral look only\n", r_om_looks->string);
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

		LookLine(line, ++lineno);
		line = nl ? nl + 1 : NULL;
	}

	free(text);
}

/* the look of this map: r_om_look_preset, else the first map line that matches, else neutral */
static int
LookChoose(void)
{
	int i, k;

	if (r_om_look_preset->string[0] && (k = LookByName(r_om_look_preset->string)) >= 0)
	{
		return k;
	}

	for (i = 0; i < LK.nummaps; i++)
	{
		if (Glob(LK.maps[i].pat, LK.map))
		{
			return LK.maps[i].look;
		}
	}

	return 0;
}

/* the LUT strip of the look (N slices of N x N), uploaded as a 2D texture; false without one */
static qboolean
LookLut(const char *name)
{
	byte *raw = NULL, *px;
	int len, w, h, comp;

	if (!name[0])
	{
		return false;
	}

	if (LK.luttex && !strcmp(LK.lutname, name))
	{
		return LK.lutsize > 0;
	}

	Q_strlcpy(LK.lutname, name, sizeof(LK.lutname));
	LK.lutsize = 0;
	len = ri.FS_LoadFile(name, (void **)&raw);

	if (len <= 0 || !raw)
	{
		R_Printf(PRINT_ALL, "r_om_look: LUT %s not found\n", name);
		return false;
	}

	px = stbi_load_from_memory(raw, len, &w, &h, &comp, 4);
	ri.FS_FreeFile(raw);

	if (!px || h < 2 || w != h * h)
	{
		R_Printf(PRINT_ALL, "r_om_look: %s is no LUT strip (N*N x N)\n", name);

		if (px)
		{
			stbi_image_free(px);
		}

		return false;
	}

	if (!LK.luttex)
	{
		glGenTextures(1, &LK.luttex);
	}

	GL3_SelectTMU(GL_TEXTURE0 + LOOK_UNIT);
	glBindTexture(GL_TEXTURE_2D, LK.luttex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GL3_SelectTMU(GL_TEXTURE0);
	stbi_image_free(px);
	LK.lutsize = h;
	return true;
}

static void
LookReload_f(void)
{
	LookLoad();
	LK.lutname[0] = 0;
	R_Printf(PRINT_ALL, "r_om_looks: %d looks, %d map lines; this map: %s\n", LK.numlooks, LK.nummaps,
		LK.looks[LookChoose()].name);
}

void
GL3_Look_Init(void)
{
	r_om_look = ri.Cvar_Get("r_om_look", "1", CVAR_ARCHIVE);
	r_om_camera = ri.Cvar_Get("r_om_camera", "1", CVAR_ARCHIVE);
	r_om_look_preset = ri.Cvar_Get("r_om_look_preset", "", CVAR_ARCHIVE);
	r_om_looks = ri.Cvar_Get("r_om_looks", "looks/om_looks.txt", CVAR_ARCHIVE);

	if (!lookcmds)
	{
		ri.Cmd_AddCommand("r_om_looks_reload", LookReload_f);
		lookcmds = true;
	}
}

void
GL3_Look_Shutdown(void)
{
	if (LK.luttex)
	{
		glDeleteTextures(1, &LK.luttex);
	}

	LK.luttex = 0;
	LK.lutsize = 0;
	LK.lutname[0] = 0;
	LK.loaded = false;

	if (lookcmds)
	{
		ri.Cmd_RemoveCommand("r_om_looks_reload");
		lookcmds = false;
	}
}

/* the colour table (SPEC-413 §9) is neutral while the look grades the picture */
qboolean
GL3_Look_Active(void)
{
	if (!r_om_look)
	{
		GL3_Look_Init();
	}

	return r_om_look->value != 0;
}

/* a world is loading ("maps/tsr1.bsp") */
void
GL3_Look_MapLoad(const char *name)
{
	const char *b = strrchr(name, '/');
	char *dot;

	Q_strlcpy(LK.map, b ? b + 1 : name, sizeof(LK.map));
	Q_strlwr(LK.map);

	if ((dot = strrchr(LK.map, '.')))
	{
		*dot = 0;
	}
}

/* the uniforms of the composite program for this view (look0..4, lut); the menu's gamma, brightness and contrast
   (vid_gamma, vid_brightness, vid_contrast: 0, 0.5, 0.5 are neutral) take effect here */
void
GL3_Look_Uniforms(GLuint prog, float time, float aspect)
{
	extern cvar_t *vid_gamma, *vid_brightness, *vid_contrast;
	const look_t *l;
	float k, ev, wb[3], y, lutk = 0, basesat;
	int i;

	if (!LK.loaded)
	{
		GL3_Look_Init();
		LookLoad();
	}

	l = &LK.looks[LookChoose()];
	k = Q_clamp(r_om_camera->value, 0.0f, 1.0f);

	/* white balance: warm (temperature > 0) lifts red and lowers blue, tint > 0 towards magenta; luminance kept */
	wb[0] = 1.0f + 0.12f * l->temp + 0.04f * l->tint;
	wb[1] = 1.0f - 0.08f * l->tint;
	wb[2] = 1.0f - 0.12f * l->temp + 0.04f * l->tint;
	y = 0.2126f * wb[0] + 0.7152f * wb[1] + 0.0722f * wb[2];

	for (i = 0; i < 3; i++)
	{
		wb[i] = 1.0f + (wb[i] / y - 1.0f) * k;
	}

	/* the HDR picture settings carry over: r_om_hdr_brightness as exposure, r_om_hdr_saturation as the base */
	/* the menu's brightness: +0.3 EV per 0.1 above 0.5, and the lift of the lit dark tones below */
	ev = l->ev * k + (vid_brightness->value - 0.5f) * 3.0f + log2f(Q_max(ri.Cvar_Get("r_om_hdr_brightness", "1.1", CVAR_ARCHIVE)->value, 0.1f));
	basesat = ri.Cvar_Get("r_om_hdr_saturation", "1.1", CVAR_ARCHIVE)->value;

	if (LookLut(l->lut))
	{
		lutk = k;
		GL3_SelectTMU(GL_TEXTURE0 + LOOK_UNIT);
		glBindTexture(GL_TEXTURE_2D, LK.luttex);
		GL3_SelectTMU(GL_TEXTURE0);
	}

	glUniform1i(GL3_UniformLoc(prog, "look"), 1);
	glUniform4f(GL3_UniformLoc(prog, "look0"), ev, Q_clamp(l->contrast, 0.5f, 1.5f), basesat * l->sat, k);
	/* the menu's contrast and gamma as SoF's colour table (SPEC-413 §9): S power k, gamma power 1 - g */
	{
		float c = vid_contrast->value;

		glUniform4f(GL3_UniformLoc(prog, "look1"), wb[0], wb[1], wb[2], (c < 0.5f) ? powf(1.5f - c, 3.0f) : sqrtf(1.5f - c));
	}
	glUniform4f(GL3_UniformLoc(prog, "look2"), basesat, 0, 0, 1.0f - Q_clamp(vid_gamma->value, -1.0f, 0.95f));
	glUniform4f(GL3_UniformLoc(prog, "look3"), l->vignette * k, l->grain * k, fmodf(time, 100.0f), lutk);
	/* the menu's brightness: SoF's offset (160 b - 80) / 255 */
	glUniform4f(GL3_UniformLoc(prog, "look4"), aspect, (float)LK.lutsize, 0, (160.0f * vid_brightness->value - 80.0f) / 255.0f);
	glUniform1i(GL3_UniformLoc(prog, "lut"), LOOK_UNIT);
}
