/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f HUD: the interface layer (sniper scope, item and
 * weapon panels, health/armor/meter, countdown, mission messages, deathmatch
 * rank, CTF and control flags), the crosshair, overhead player labels, the
 * scoreboard layout language, remote-view overlays, and the client commands
 * ignore / unignore / listignore / addfave / remfave / terrain / cloud /
 * cl_touch.
 *
 * Drawing goes through the SPEC-002 2D slots of the renderer
 * (OM_GetSoFRefExports of the OpenMerc GL3 renderer, refapi.h). Without
 * them a reduced fallback through the Quake II renderer interface is used.
 *
 * Implements: SPEC-310 (sections 2, 3, 4, 5, 8, 15), SPEC-201 section 33; SPEC-AMEND-005 I6h.2
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "../common/header/sof/refapi.h"
#include "../common/header/sof/engine.h"

extern void *reflib_handle;

/* ---------------------------------------------------------------------
 * 2D layer (SPEC-002 slots)
 * --------------------------------------------------------------------- */

typedef unsigned char rgba_t[4];

static const sofrefexport_t *sre;
static OM_Set2DScale_t sre_setscale; /* OpenMerc V1: HUD scaling export of the renderer */
static void *sre_handle;
static qboolean sre_tried;

static om_fontref_t font_small = {0, {0}, "small", 5, 15};
static om_fontref_t font_medium = {0, {0}, "medium", 6, 15};
static om_fontref_t font_title = {0, {0}, "title", 5, 15};

static const rgba_t c_hudgreen = {0, 220, 0, 255};
static const rgba_t c_white = {255, 255, 255, 255};
static const rgba_t c_grey = {175, 175, 175, 255};
static const rgba_t c_red = {255, 0, 0, 255};
static const rgba_t c_green = {0, 255, 0, 255};
static const rgba_t c_blue = {0, 0, 255, 255};
static const rgba_t c_black = {0, 0, 0, 255};

static unsigned
RGBA32(const unsigned char *c)
{
	/* RGBA by value: bytes R, G, B, A in memory order */
	return (unsigned)c[0] | ((unsigned)c[1] << 8) | ((unsigned)c[2] << 16) | ((unsigned)c[3] << 24);
}

const sofrefexport_t *
HUD_Ref(void)
{
	if (reflib_handle != sre_handle)
	{
		sre_handle = reflib_handle;
		sre = NULL;
		sre_setscale = NULL;
		sre_tried = false;
	}

	if (!sre_tried && reflib_handle)
	{
		OM_GetSoFRefExports_t f;

		sre_tried = true;
		f = (OM_GetSoFRefExports_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_EXPORTS_NAME);
		sre = f ? f() : NULL;
		sre_setscale = sre ? (OM_Set2DScale_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_SETSCALE_NAME) : NULL;

		if (!sre)
		{
			Com_DPrintf("HUD: renderer without the SoF 2D slots, using the reduced fallback\n");
		}
	}

	return sre;
}

/* SPEC-002 slot 0xb4(0): the engine switches to 2D right after RenderFrame */
void
HUD_Set2D(void)
{
	if (HUD_Ref() && sre->Set2D)
	{
		sre->Set2D(0);
	}
}

static void
D_PicPart(int x, int y, int l, int t, int r, int b, const unsigned char *c, const char *name)
{
	if (HUD_Ref() && sre->DrawPicPart)
	{
		sre->DrawPicPart(x, y, l, t, r, b, c, (char *)name);
		return;
	}

	Draw_StretchPic(x, y, abs(r - l), abs(b - t), name);
}

static void
D_Stretch(int x, int y, int w, int h, const unsigned char *c, const char *name, int rot)
{
	if (HUD_Ref() && sre->DrawStretchPic)
	{
		sre->DrawStretchPic(x, y, w, h, c, (char *)name, rot);
		return;
	}

	if ((w > 0) && (h > 0))
	{
		Draw_StretchPic(x, y, w, h, name);
	}
	else
	{
		Draw_StretchPic((w < 0) ? x + w : x, (h < 0) ? y + h : y, abs(w), abs(h), name);
	}
}

static void
D_ScaledPic(int x, int y, const unsigned char *c, const char *name)
{
	if (HUD_Ref() && sre->DrawPicScaled)
	{
		sre->DrawPicScaled(x, y, 1.0f, 1.0f, c, (char *)name);
		return;
	}

	Draw_PicScaled(x, y, name, 1.0f);
}

static void
D_Fill(int x, int y, int w, int h, const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawFill)
	{
		sre->DrawFill(x, y, w, h, c);
		return;
	}

	Draw_Fill(x, y, w, h, (c[0] + c[1] + c[2] == 0) ? 0 : 15);
}

static void
D_GetPicSize(int *w, int *h, const char *name)
{
	if (HUD_Ref() && sre->DrawGetPicSize)
	{
		sre->DrawGetPicSize(w, h, (char *)name);
		return;
	}

	Draw_GetPicSize(w, h, name);

	if ((*w <= 0) || (*h <= 0))
	{
		*w = *h = -1;
	}
}

static int
D_FontHeight(const om_fontref_t *f)
{
	if (HUD_Ref() && sre->FontLineHeight)
	{
		return sre->FontLineHeight(f);
	}

	return (f == &font_title) ? 18 : (f == &font_medium) ? 12 : 10;
}

static int
D_TextWidth(const char *s, const om_fontref_t *f)
{
	if (HUD_Ref() && sre->FontStringWidth)
	{
		return sre->FontStringWidth((char *)s, f);
	}

	return 8 * (int)strlen(s);
}

void
HUD_DrawFontText(int x, int y, const char *s, const unsigned char *c, int font, int keep)
{
	const om_fontref_t *f = (font == 2) ? &font_title : (font == 1) ? &font_medium : &font_small;

	if (HUD_Ref() && sre->DrawFontString)
	{
		sre->DrawFontString(x, y, (char *)s, RGBA32(c), f, keep);
		return;
	}

	for ( ; *s; s++, x += 8)
	{
		Draw_CharScaled(x, y, (byte)*s, 1.0f);
	}
}

int
HUD_FontHeight(int font)
{
	return D_FontHeight((font == 2) ? &font_title : (font == 1) ? &font_medium : &font_small);
}

int
HUD_TextWidth(const char *s, int font)
{
	return D_TextWidth(s, (font == 2) ? &font_title : (font == 1) ? &font_medium : &font_small);
}

static void
D_Text(int x, int y, const char *s, const unsigned char *c, const om_fontref_t *f)
{
	HUD_DrawFontText(x, y, s, c, (f == &font_title) ? 2 : (f == &font_medium) ? 1 : 0, 0);
}

void
HUD_DrawColouredString(int x, int y, const char *s, const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawConsoleStringColor)
	{
		sre->DrawConsoleStringColor(x, y, (char *)s, 1024, c);
		return;
	}

	for ( ; *s; s++)
	{
		if ((byte)*s >= 32)
		{
			Draw_CharScaled(x, y, (byte)*s, 1.0f);
			x += 8;
		}
	}
}

static void
D_Char(int x, int y, int ch, const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawChar)
	{
		sre->DrawChar(x, y, ch, c);
		return;
	}

	Draw_CharScaled(x, y, ch, 1.0f);
}

void
HUD_DrawCharFloat(float x, float y, int ch, const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawCharFloat)
	{
		sre->DrawCharFloat(x, y, 1.0f, c, ch);
		return;
	}

	Draw_CharScaled((int)x, (int)y, ch, 1.0f);
}

void
HUD_DrawFade(const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawFadeScreen)
	{
		sre->DrawFadeScreen(c);
		return;
	}

	Draw_FadeScreen();
}

void
HUD_DrawFill(int x, int y, int w, int h, const unsigned char *c)
{
	D_Fill(x, y, w, h, c);
}

void
HUD_DrawLine(float x1, float y1, float x2, float y2, float width, const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawLine)
	{
		sre->DrawLine(x1, y1, x2, y2, width, c);
		return;
	}

	Draw_Fill((int)x1, (int)(y1 < y2 ? y1 : y2), (int)width, (int)fabsf(y2 - y1) + 1, 15);
}

void
HUD_DrawPic(int x, int y, const char *name, const unsigned char *c)
{
	if (HUD_Ref() && sre->DrawPic)
	{
		sre->DrawPic(x, y, (char *)name, RGBA32(c));
		return;
	}

	Draw_PicScaled(x, y, name, 1.0f);
}

void
HUD_GetPicSize(int *w, int *h, const char *name)
{
	D_GetPicSize(w, h, name);
}

static void
D_RegisterPic(const char *name)
{
	if (HUD_Ref() && sre->RegisterPic)
	{
		sre->RegisterPic((char *)name);
		return;
	}

	Draw_FindPic(name);
}

/* OpenMerc V1 HUD scaling (docs/DEVIATIONS.md "HUD scale"): every HUD element
   is drawn with one uniform factor f - scr_hudscale 0 (default) = the UI
   scale (SCR_UIScale, MENU2; was s = H/480) (1 with scr_aspect 0), 1 = 1:1 as the original, any other
   value = that factor. The HUD code lays out in a virtual window of
   HUD_W() x HUD_H() = W/f x H/f pixels and the renderer's SoF 2D slots multiply
   every coordinate and size by f (OM_Set2DScale), so pics, pic parts, fonts,
   fills, lines, positions and anchors scale together. */
static cvar_t *scr_hudscale;

float
HUD_Scale(void)
{
	float f;

	if (!scr_hudscale)
	{
		scr_hudscale = Cvar_Get("scr_hudscale", "0", CVAR_ARCHIVE);
	}

	f = scr_hudscale->value;

	if (f <= 0.0f)
	{
		f = SCR_AspectCorrect() ? SCR_UIScale() : 1.0f; /* MENU2: same factor as the menus */
	}

	return (f < 0.25f) ? 0.25f : f;
}

int
HUD_W(void)
{
	return (int)((float)viddef.width / HUD_Scale());
}

int
HUD_H(void)
{
	return (int)((float)viddef.height / HUD_Scale());
}

/* OpenMerc (text size): the centre prints and subtitles are drawn with the
   HUD factor times scr_textscale (SCR_TextScale). From a HUD factor of 2 the
   result is rounded to a whole factor, so the bitmap fonts stay sharp
   (nearest sampled) instead of being filtered; below 2 it is exact. Between
   HUD_BeginText and HUD_EndText (inside HUD_BeginScaled) the text lays out in
   a virtual window of HUD_TextW() x HUD_TextH(). */
float
HUD_TextFactor(void)
{
	float f = HUD_Scale(), t = f * SCR_TextScale();

	if (SCR_TextScale() == 1.0f)
	{
		return f; /* exactly the HUD factor, as before */
	}

	if (f >= 2.0f)
	{
		t = floorf(t + 0.5f);
	}

	return (t < 0.25f) ? 0.25f : t;
}

int
HUD_TextW(void)
{
	return (int)((float)viddef.width / HUD_TextFactor());
}

int
HUD_TextH(void)
{
	return (int)((float)viddef.height / HUD_TextFactor());
}

void
HUD_BeginText(void)
{
	if (HUD_Ref() && sre_setscale)
	{
		sre_setscale(HUD_TextFactor());
	}
}

void
HUD_EndText(void)
{
	if (HUD_Ref() && sre_setscale)
	{
		sre_setscale(HUD_Scale());
	}
}

void
HUD_BeginScaled(void)
{
	if (HUD_Ref() && sre_setscale)
	{
		sre_setscale(HUD_Scale());
	}
}

void
HUD_EndScaled(void)
{
	if (sre_setscale)
	{
		sre_setscale(1.0f);
	}
}

/* SPEC-310's 640-based positions in the virtual window */
static int
ScaleX(int v)
{
	return (int)((float)v * viddef.width / 640.0f / HUD_Scale());
}

static int
ScaleY(int v)
{
	return (int)((float)v * viddef.height / 480.0f / HUD_Scale());
}

/* OpenMerc V1 (scr_aspect 1, docs/DEVIATIONS.md): the 640-based x values of
   SPEC-310 are anchored instead of stretched - a size or an offset from the
   screen centre scales by s = height / 480 (SX), a left-anchored x by s (XL),
   a right-anchored x keeps its distance to the right edge (XR). With
   scr_aspect 0 all three are the original's width / 640 scale. */
static int
SX(int v)
{
	return SCR_AspectCorrect() ? (int)((float)v * SCR_CanvasScale() / HUD_Scale()) : ScaleX(v);
}

static int
XL(int v)
{
	return SX(v);
}

static int
XR(int v)
{
	return SCR_AspectCorrect() ? HUD_W() - (int)((float)(640 - v) * SCR_CanvasScale() / HUD_Scale()) : ScaleX(v);
}

/* ---------------------------------------------------------------------
 * state shared by the elements
 * --------------------------------------------------------------------- */

static cvar_t *scr_interface;
static cvar_t *crosshair_cvar;
static cvar_t *shownames;
static cvar_t *spectator_cvar;

static int hud_countdown;
static char hud_layout[1024];
static int name_cols[32]; /* svc_playernamecols values per slot */

typedef struct
{
	int w, h, px, py;
	int mask;
	int x, y; /* computed at registration */
} hudelem_t;

enum
{
	E_SCOPE, E_ITEM, E_WEAPON, E_HEALTH, E_COUNTDOWN, E_TARGET, E_MISSION, E_RANK, E_CTF,
	E_CONTROL, E_COUNT
};

static hudelem_t elems[E_COUNT] = {
	{32, 32, 0, 0, 16},
	{130, 95, 0, 100, 0},
	{130, 95, 100, 100, 0},
	{160, 52, 50, 100, 0},
	{32, 32, 0, 0, 2},
	{320, 32, 100, 100, 0},
	{0, 0, 0, 0, 0},
	{48, 32, 0, 0, 2},
	{48, 48, 0, 0, 0},
	{48, 48, 0, 0, 0},
};

typedef struct
{
	qboolean seen;
	int last;
	float phase, speed, target, hold;
} tray_t;

static tray_t trays[2];
static char crosshair_pic[MAX_QPATH];

static const sofps_t *
PS(void)
{
	return CL_SoF_CurrentPS();
}

static qboolean
HUD_Active(void)
{
	return (cls.state == ca_active) && cl.refresh_prepped && CL_SoF_Active();
}

/* the local player's info fields: name\team\model\N */
static void
HUD_PlayerField(int slot, int field, char *out, size_t size)
{
	const char *s;
	int f = 0;
	size_t n = 0;

	out[0] = 0;

	if ((slot < 0) || (slot >= 32))
	{
		return;
	}

	s = CL_SoF_ConfigString(1415 + slot);

	for ( ; *s; s++)
	{
		if (*s == '\\')
		{
			f++;
			continue;
		}

		if ((f == field) && (n + 1 < size))
		{
			out[n++] = *s;
		}
	}

	out[n] = 0;
}

/* last field of the player info (team number - 1) */
static void
HUD_TeamDigit(int slot, char *out, size_t size)
{
	char buf[64];
	const char *s, *p;

	out[0] = 0;

	if ((slot < 0) || (slot >= 32))
	{
		return;
	}

	Q_strlcpy(buf, CL_SoF_ConfigString(1415 + slot), sizeof(buf));
	p = strrchr(buf, '\\');
	s = p ? p + 1 : "";
	Q_strlcpy(out, s, size);
}

static const unsigned char *
HUD_TeamColour(int slot)
{
	char d[16];

	HUD_TeamDigit(slot, d, sizeof(d));
	return !strcmp(d, "0") ? c_blue : c_red;
}

static qboolean
HUD_TeamMode(void)
{
	int dm = CL_SoF_ServerDeathmatch();

	return (dm == 4) || (dm == 6) || ((dm == 7) && atoi(CL_SoF_ConfigString(10)));
}

/* team icon picture of a slot, "" when none */
static void
HUD_TeamIcon(int slot, char *out, size_t size)
{
	char team[64], model[64];
	int w, h;

	HUD_PlayerField(slot, 1, team, sizeof(team));
	HUD_PlayerField(slot, 2, model, sizeof(model));
	out[0] = 0;

	if (!team[0])
	{
		return;
	}

	if (!Q_stricmp(team, model))
	{
		Com_sprintf(out, size, "ghoul/pmodels/teamicons/noteam");
	}
	else
	{
		Com_sprintf(out, size, "ghoul/pmodels/teamicons/%s", team);
	}

	D_GetPicSize(&w, &h, out);

	if ((w <= 0) || (h <= 0))
	{
		out[0] = 0;
	}
}

/* ---------------------------------------------------------------------
 * registration
 * --------------------------------------------------------------------- */

static const char *hud_pics[] = {
	"pics/interface2/frame_top", "pics/interface2/frame_top2", "pics/interface2/frame_lip",
	"pics/interface2/frame_bottom", "pics/interface2/frame_health", "pics/interface2/health",
	"pics/interface2/armor", "pics/interface2/frame_stealth", "pics/interface2/item_flash",
	"pics/interface2/item_c4", "pics/interface2/item_nightvision", "pics/interface2/item_claymore",
	"pics/interface2/item_medkit", "pics/interface2/item_h_grenade", "pics/interface2/item_flag",
	"pics/interface2/item_neuro", "pics/interface2/eye", "pics/interface2/ctfb_hudflag",
	"pics/interface2/ctfr_hudflag", "pics/interface2/ctfb_hudflag_d", "pics/interface2/ctfb_hudflag_r",
	"pics/interface2/ctfr_hudflag_d", "pics/interface2/ctfr_hudflag_r", "pics/interface2/control_flag",
	"pics/interface2/no_blank", "pics/scope/reticle", "pics/scope/vinette", "pics/scope/bullet",
	"pics/scope/arrows128_1", "pics/scope/arrows128_2", "pics/scope/arrows128_3",
	"pics/scope/arrows128_4", "pics/scope/big_arrow", "pics/scope/gauge", "pics/monitor/monitor",
	"pics/menus/status/ch1", "pics/menus/status/ch2", "pics/menus/status/ch3", "pics/menus/status/ch4",
	"pics/console/net", NULL
};

void
HUD_RegisterMedia(void)
{
	int i;
	char name[MAX_QPATH];

	CL_Strings_RegisterMedia();

	for (i = 0; hud_pics[i]; i++)
	{
		D_RegisterPic(hud_pics[i]);
	}

	for (i = 0; i < 10; i++)
	{
		Com_sprintf(name, sizeof(name), "pics/interface2/no_%d", i);
		D_RegisterPic(name);
	}

	/* positions with the video size of this moment (SPEC-310 2) */
	for (i = 0; i < E_COUNT; i++)
	{
		elems[i].x = (int)((HUD_W() - elems[i].w) * elems[i].px / 100);
		elems[i].y = (int)((HUD_H() - elems[i].h) * elems[i].py / 100);
	}

	memset(trays, 0, sizeof(trays));
}

/* ---------------------------------------------------------------------
 * section 2.1: big digits
 * --------------------------------------------------------------------- */

static void
HUD_BigNumberRight(int x, int y, int value, int width)
{
	char buf[32], name[MAX_QPATH];
	int i;

	Com_sprintf(buf, sizeof(buf), "%*d", width, value);

	for (i = (int)strlen(buf) - 1; i >= 0; i--)
	{
		if ((buf[i] >= '0') && (buf[i] <= '9'))
		{
			x -= 18;
			Com_sprintf(name, sizeof(name), "pics/interface2/no_%c", buf[i]);
			D_ScaledPic(x, y, c_hudgreen, name);
		}
		else if (buf[i] == ' ')
		{
			x -= 18;
			D_ScaledPic(x, y, c_hudgreen, "pics/interface2/no_blank");
		}
		else
		{
			x -= 16;
		}
	}
}

/* ---------------------------------------------------------------------
 * section 2.3: sniper scope
 * --------------------------------------------------------------------- */

static void
HUD_DrawScope(int bullets, float fov)
{
	int W = HUD_W(), H = HUD_H();
	int cx = W / 2, cy = H / 2;
	int A = (int)(0.625f * W), B = (int)(0.6667f * H), C = (int)(0.2f * W), D = (int)(0.2667f * H);
	int E = (int)(0.1953f * W), F = (int)(0.2604f * H), G = (int)(0.003125f * W);
	int J = (int)(0.004167f * H), K = (int)(0.008333f * H);
	float z = (fov - 5) / 55.0f;
	char name[MAX_QPATH], txt[32];
	int k, m;

	if (SCR_AspectCorrect())
	{
		/* the same fractions of a 640x480 screen, with one scale (the vignette quadrants stay full-screen) */
		A = cx + SX(80);
		C = SX(128);
		E = SX(125);
		G = SX(2);
	}

	D_Stretch(cx - C, cy - 8, C, 16, c_hudgreen, "pics/scope/reticle", 0);
	D_Stretch(cx + C, cy - 8, -C, 16, c_hudgreen, "pics/scope/reticle", 0);
	D_Stretch(cx - 8, cy, 16, -D, c_hudgreen, "pics/scope/reticle", 1);
	D_Stretch(cx - 8, cy, 16, D, c_hudgreen, "pics/scope/reticle", 1);
	D_Fill(cx, cy, 1, 1, c_hudgreen);

	D_Stretch(0, 0, cx, cy, c_hudgreen, "pics/scope/vinette", 0);
	D_Stretch(W, 0, -cx, cy, c_hudgreen, "pics/scope/vinette", 0);
	D_Stretch(0, H, cx, -cy, c_hudgreen, "pics/scope/vinette", 0);
	D_Stretch(W, H, -cx, -cy, c_hudgreen, "pics/scope/vinette", 0);

	for (k = 0; k < bullets; k++)
	{
		D_Stretch(A + SX(160), B + ScaleY((2 - bullets) * 20 + 20 * k), SX(64), ScaleY(16),
				c_hudgreen, "pics/scope/bullet", 0);
	}

	m = 1 + (((int)(z * 100)) % 4 + 4) % 4;
	Com_sprintf(name, sizeof(name), "pics/scope/arrows128_%d", m);
	D_Stretch(cx - E, cy - F, C, D, c_hudgreen, name, 0);
	D_Stretch(cx - E, cy - K, C, D, c_hudgreen, name, 1);
	D_Stretch(cx, cy - J, E, F, c_hudgreen, name, 2);
	D_Stretch(cx - G, cy - F, C, D, c_hudgreen, name, 3);

	D_Stretch(cx - SX(7), cy - ScaleY(135), SX(16), ScaleY(32), c_hudgreen, "pics/scope/big_arrow", 0);
	D_Stretch(cx - SX(8), cy + ScaleY(102), SX(16), ScaleY(32), c_hudgreen, "pics/scope/big_arrow", 2);
	D_Stretch(cx - SX(134), cy - ScaleY(8), SX(32), ScaleY(16), c_hudgreen, "pics/scope/big_arrow", 1);
	D_Stretch(cx + SX(103), cy - ScaleY(7), SX(32), ScaleY(16), c_hudgreen, "pics/scope/big_arrow", 3);

	Com_sprintf(txt, sizeof(txt), "x %0.2d", (int)((1 - z) * 15 + 6));
	D_Text(cx - SX(300), cy - ScaleY(70) - 8, txt, c_hudgreen, &font_small);

	D_Stretch(cx - SX(290), cy + (int)((110 * z - 62) * H / 480), SX(26), ScaleY(12),
			c_hudgreen, "pics/scope/big_arrow", 3);
	D_Stretch(cx - SX(300), cy - ScaleY(64), SX(16), ScaleY(128), c_hudgreen, "pics/scope/gauge", 0);
}

/* ---------------------------------------------------------------------
 * section 2.4: item and weapon panels
 * --------------------------------------------------------------------- */

static void
HUD_UpdateTray(tray_t *t, int selection)
{
	float ft = cls.rframetime;

	if (!t->seen || (selection != t->last))
	{
		if (t->phase <= 0)
		{
			t->speed = 3;
			t->target = (float)M_PI / 2;
			t->hold = 2.5f;
		}

		t->seen = true;
		t->last = selection;
	}

	t->phase += ft * t->speed;

	if ((t->speed > 0) && (t->phase >= t->target))
	{
		t->phase = t->target;
		t->speed = 0;
	}
	else if ((t->speed < 0) && (t->phase <= t->target))
	{
		t->phase = t->target;
		t->speed = 0;
	}

	t->hold -= ft;

	if ((t->hold <= 0) && (t->speed >= 0) && (t->phase > 0))
	{
		t->speed = -3;
		t->target = 0;
	}
}

static void
HUD_DrawPanel(int which, int x, int y)
{
	static const int item_labels[8] = {5, 1, 0, 3, 4, 53, 54, 57};
	static const char *item_icons[8] = {NULL, "flash", "c4", "nightvision", "claymore", "medkit",
		"h_grenade", "flag"};
	static const int weapon_labels[13] = {12, 0, 2, 1, 7, 5, 4, 8, 3, 6, 10, 11, 9};
	const sofps_t *ps = PS();
	tray_t *t = &trays[which];
	int sel, count, l0, r0, labelx, iconx, countx, o;
	rgba_t closed = {255, 255, 255, 175};
	const unsigned char *countcol = c_hudgreen;
	const char *label;
	char buf[32], name[MAX_QPATH];

	if (which == 0)
	{
		sel = ps->stats[0];
		count = ps->stats[7];
		l0 = 128;
		r0 = 0;
		labelx = 17;
		iconx = 28;
		countx = 76;
		label = CL_StringById((3 << 8) | item_labels[((sel >= 1) && (sel <= 7)) ? sel : 0]);
	}
	else
	{
		sel = ps->stats[6];
		count = ps->stats[3];
		l0 = 0;
		r0 = 128;
		labelx = 49;
		iconx = 62;
		countx = 31;
		label = CL_StringById((2 << 8) | weapon_labels[((sel >= 1) && (sel <= 12)) ? sel : 0]);

		if ((count < 2) && ((cls.realtime % 1024) <= 512))
		{
			countcol = c_red;
		}
	}

	HUD_UpdateTray(t, sel);

	if ((t->phase == 0) && (t->speed == 0))
	{
		D_PicPart(x, y + 27, l0, 0, r0, 32, closed, "pics/interface2/frame_top2");
		D_PicPart(x, y + 28, l0, 0, r0, 64, closed, "pics/interface2/frame_bottom");
	}
	else
	{
		o = (int)(cosf(t->phase) * 27);
		D_PicPart(x, y + 27, l0, 0, r0, 32, c_white, "pics/interface2/frame_lip");
		D_PicPart(x, y + o, l0, 0, r0, 64, c_white, "pics/interface2/frame_top");

		{
			/* OpenMerc: a name longer than the tray's window (localized names, e.g. Russian) is moved
			   left so that it ends 2 units inside the screen instead of running off the right edge */
			int lx = x + labelx, w = HUD_TextWidth(label, 0);

			if ((which == 1) && (lx + w > HUD_W() - 2))
			{
				lx = HUD_W() - 2 - w;
			}

			D_Text(lx, y + o + 21, label, c_hudgreen, &font_small);
		}
		D_PicPart(x, y + 28, l0, 0, r0, 64, c_white, "pics/interface2/frame_bottom");
	}

	if (which == 0)
	{
		if ((sel >= 1) && (sel <= 7))
		{
			Com_sprintf(name, sizeof(name), "pics/interface2/item_%s", item_icons[sel]);
			D_PicPart(x + iconx + 1, y + 44, 0, 0, 39, 32, c_hudgreen, name);
		}
	}
	else
	{
		HUD_BigNumberRight(x + iconx + 39, y + 42, ps->stats[2], 2);
	}

	Com_sprintf(buf, sizeof(buf), "%d", count);
	D_Text(x + countx, y + 63, buf, countcol, &font_small);
}

/* ---------------------------------------------------------------------
 * section 2.5: health, armor, meter
 * --------------------------------------------------------------------- */

static int
HUD_BlinkAlpha(void)
{
	float f = (cls.realtime % 800) * 0.00375f;

	if (f < 1)
	{
		return 255;
	}

	if (f < 2)
	{
		return (int)(sinf((2 - f) * (float)M_PI / 2) * 255);
	}

	return -1;
}

static void
HUD_DrawHealth(int x, int y)
{
	const sofps_t *ps = PS();
	float h = ps->stats[1] / 100.0f, a = ps->stats[5] / 100.0f, m;
	rgba_t blink = {255, 255, 255, 150};
	int al;

	h = (h < 0) ? 0 : (h > 1) ? 1 : h;
	a = (a < 0) ? 0 : (a > 1) ? 1 : a;

	D_PicPart(x, y + 10, 3, 1, 158, 28, c_white, "pics/interface2/frame_health");
	D_PicPart(x + 26, y + 21, 2, 3, (int)(124 * h + 2), 12, c_white, "pics/interface2/health");

	if (a > 0)
	{
		D_PicPart(x + 16, y + 10, 57, 1, (int)(141 * a + 57), 13, c_white, "pics/interface2/armor");
	}

	if (h < 0.25f)
	{
		al = HUD_BlinkAlpha();

		if (al >= 0)
		{
			blink[3] = (byte)al;
			D_PicPart(x + 12, y + 19, 181, 10, 188, 17, blink, "pics/interface2/frame_health");
		}
	}

	if (ps->stats[8] & 0x200)
	{
		D_PicPart(x, y + 37, 3, 0, 155, 10, c_white, "pics/interface2/frame_stealth");
		m = ps->stats[11] / 100.0f;
		m = (m < 0) ? 0 : (m > 1) ? 1 : m;
		D_PicPart(x + 21, y + 40, 24, 19, (int)(115 * m + 24), 23, c_white, "pics/interface2/frame_stealth");

		if (ps->stats[11] > 99)
		{
			al = HUD_BlinkAlpha();

			if (al >= 0)
			{
				blink[3] = (byte)al;
				D_PicPart(x + 137, y + 40, 140, 19, 152, 24, blink, "pics/interface2/frame_stealth");
			}
		}
	}
}

/* ---------------------------------------------------------------------
 * sections 2.6 - 2.11
 * --------------------------------------------------------------------- */

void
HUD_SetCountdown(int seconds)
{
	hud_countdown = seconds;
}

static void
HUD_DrawCountdown(void)
{
	char buf[64];

	if (!hud_countdown)
	{
		return;
	}

	if (hud_countdown < 60)
	{
		Com_sprintf(buf, sizeof(buf), "Countdown : %d", hud_countdown);
	}
	else
	{
		Com_sprintf(buf, sizeof(buf), "Countdown : %d:%02d", hud_countdown / 60, hud_countdown % 60);
	}

	D_Text(HUD_W() / 2 - 300, HUD_H() / 2 - 78, buf, c_hudgreen, &font_title);
}

static void
HUD_CentredText(int y, const char *s, const unsigned char *c, const om_fontref_t *f)
{
	D_Text((HUD_W() - D_TextWidth(s, f)) / 2, y, s, c, f);
}

static void
HUD_DrawMission(void)
{
	int s8 = PS()->stats[8], L, H = HUD_H();

	if (s8 & 0x0008)
	{
		L = D_FontHeight(&font_title);
		HUD_CentredText((H - L) / 2 - 18, CL_StringById((6 << 8) | 6), c_red, &font_title);
		HUD_CentredText((H - L) / 2 + 22, CL_StringById((6 << 8) | 7), c_red, &font_title);
	}
	else if (s8 & 0x0080)
	{
		L = D_FontHeight(&font_title);
		HUD_CentredText((H - L) / 2 - 8, CL_StringById((6 << 8) | 8), c_red, &font_title);
	}
	else if (s8 & 0x0100)
	{
		L = D_FontHeight(&font_title);
		HUD_CentredText((H - L) / 2 - 8, CL_StringById((6 << 8) | 10), c_red, &font_title);
	}
	else if (s8 & 0x0040)
	{
		L = D_FontHeight(&font_title);
		HUD_CentredText((H - L) / 2 - 18, CL_StringById((6 << 8) | 6), c_red, &font_title);
	}
	else if (s8 & 0x0400)
	{
		rgba_t c = {255, 0, 0, 255};

		if (!((cls.realtime % 1024) / 512 > 1))
		{
			c[3] = 0;
		}

		L = D_FontHeight(&font_medium);
		HUD_CentredText(5 * L - 8, CL_StringById((6 << 8) | 9), c, &font_medium);
	}
}

static void
HUD_DrawRank(void)
{
	const sofps_t *ps = PS();
	int ax = XR(600), y = ScaleY(20), w, h, local = cl.playernum;
	qboolean icons = atoi(CL_SoF_ConfigString(33)) != 0;
	char icon[MAX_QPATH], buf[64];
	rgba_t col;

	if (!(ps->stats[9] & 0x2))
	{
		return;
	}

	memcpy(col, HUD_TeamMode() ? HUD_TeamColour(local) : c_hudgreen, 4);

	if ((ps->pm_type == 2) || (ps->pm_type == 7))
	{
		memcpy(col, c_hudgreen, 4);
		D_GetPicSize(&w, &h, "pics/interface2/eye");

		if (w > 0)
		{
			D_ScaledPic(ax - w / 2, y, c_hudgreen, "pics/interface2/eye");
			y += h + 6;
		}

		if (ps->spectated)
		{
			int slot = ps->spectated - 1;
			char name[64];

			if (icons)
			{
				HUD_TeamIcon(slot, icon, sizeof(icon));

				if (icon[0])
				{
					D_GetPicSize(&w, &h, icon);
					D_ScaledPic(ax - w / 2, y, HUD_TeamMode() ? HUD_TeamColour(local) : c_hudgreen, icon);
					y += h + 6;
				}
			}

			HUD_PlayerField(slot, 0, name, sizeof(name));
			D_Text(ax - D_TextWidth(name, &font_small) / 2, y - 8, name, c_hudgreen, &font_small);
		}

		return;
	}

	if (icons)
	{
		HUD_TeamIcon(local, icon, sizeof(icon));

		if (icon[0])
		{
			D_GetPicSize(&w, &h, icon);
			D_ScaledPic(ax - w / 2, y, col, icon);
			y += h + 6;
		}
	}

	Com_sprintf(buf, sizeof(buf), "%d", ps->stats[10]);
	col[3] = 215;
	D_Text(ax - D_TextWidth(buf, &font_title) / 2, y - 8, buf, col, &font_title);

	Com_sprintf(buf, sizeof(buf), "%d/%d", ps->rank, ps->rankcount);
	col[3] = 255;
	D_Text(ax - D_TextWidth(buf, &font_title) / 2, y - 8 + 18, buf, col, &font_title);
}

static void
HUD_DrawCTF(void)
{
	const char *cs8 = CL_SoF_ConfigString(8), *cs9 = CL_SoF_ConfigString(9);
	int x1 = XL(20), y1 = ScaleY(20), y2 = ScaleY(65);
	char d[16];
	qboolean blue;

	HUD_TeamDigit(cl.playernum, d, sizeof(d));
	blue = !strcmp(d, "0");

	if (blue)
	{
		if ((cs8[0] == 'd') || (cs8[0] == 'D'))
		{
			D_Stretch(x1, y1, 48, 48, c_white, "pics/interface2/ctfb_hudflag_d", 0);
		}
		else if (cs8[0] == 'r')
		{
			D_Stretch(x1, y1, 48, 48, c_white, "pics/interface2/ctfb_hudflag_r", 0);
		}

		if ((cs9[0] == 'r') && ((byte)cs9[1] == cl.playernum + 1))
		{
			D_Stretch(x1, y2, 48, 48, c_white, "pics/interface2/ctfr_hudflag", 0);
		}
	}
	else
	{
		if ((cs9[0] == 'd') || (cs9[0] == 'D'))
		{
			D_Stretch(x1, y1, 48, 48, c_white, "pics/interface2/ctfr_hudflag_d", 0);
		}
		else if (cs9[0] == 'r')
		{
			D_Stretch(x1, y1, 48, 48, c_white, "pics/interface2/ctfr_hudflag_r", 0);
		}

		if ((cs8[0] == 'r') && ((byte)cs8[1] == cl.playernum + 1))
		{
			D_Stretch(x1, y2, 48, 48, c_white, "pics/interface2/ctfb_hudflag", 0);
		}
	}
}

static void
HUD_DrawControl(void)
{
	int bx = XL(10), by = ScaleY(328), rx = XR(590), lx = XL(10), ly = ScaleY(40);
	int i;

	D_Stretch(bx, by, 48, 48, c_white, "pics/interface2/ctfb_hudflag", 0);
	HUD_DrawColouredString(bx + 12, by + 48, CL_SoF_ConfigString(8), c_white);
	D_Stretch(rx, by, 48, 48, c_white, "pics/interface2/ctfr_hudflag", 0);
	HUD_DrawColouredString(rx + 12, by + 48, CL_SoF_ConfigString(9), c_white);

	for (i = 3545; i <= 3549; i++)
	{
		const char *s = CL_SoF_ConfigString(i), *pic = NULL;

		if (!s[0])
		{
			continue;
		}

		switch (s[0])
		{
			case 'G': pic = "pics/interface2/control_flag"; break;
			case 'R': pic = "pics/interface2/ctfr_hudflag"; break;
			case 'B': pic = "pics/interface2/ctfb_hudflag"; break;
		}

		if (pic)
		{
			D_Stretch(lx, ly, 48, 48, c_white, pic, 0);
		}

		if (s[1])
		{
			HUD_DrawColouredString(lx, ly + 48, s + 1, c_white);
		}

		ly += 60;
	}
}

/* ---------------------------------------------------------------------
 * section 2: the interface layer
 * --------------------------------------------------------------------- */

static qboolean
HUD_StandardTest(const hudelem_t *e)
{
	const sofps_t *ps = PS();

	if ((ps->pm_type == 2) || (ps->pm_type == 7))
	{
		return false;
	}

	if (!ps->stats[6] && !ps->stats[12])
	{
		return false;
	}

	return (ps->remote_type == 0) || (ps->remote_type & e->mask);
}

void
HUD_DrawInterface(void)
{
	const sofps_t *ps;
	qboolean scope;
	int dm, i;

	if (!HUD_Active() || !scr_interface->value)
	{
		return;
	}

	ps = PS();
	scope = (ps->stats[8] & 0x1) != 0;
	dm = CL_SoF_ServerDeathmatch();

	/* OpenMerc V1: positions follow the virtual window of the moment (SPEC-310 2 computes
	   them at registration; the HUD scale can change between registrations) */
	for (i = 0; i < E_COUNT; i++)
	{
		elems[i].x = (int)((HUD_W() - elems[i].w) * elems[i].px / 100);
		elems[i].y = (int)((HUD_H() - elems[i].h) * elems[i].py / 100);
	}

	/* 1 sniper scope */
	if (HUD_StandardTest(&elems[E_SCOPE]))
	{
		if (ps->remote_type == 16)
		{
			HUD_DrawScope(6, cl.frame.playerstate.fov);
		}
		else if (scope)
		{
			HUD_DrawScope(ps->stats[2], cl.frame.playerstate.fov);
		}
	}

	/* 2, 3 panels; 4 health */
	if (!scope)
	{
		if (HUD_StandardTest(&elems[E_ITEM]))
		{
			HUD_DrawPanel(0, elems[E_ITEM].x, elems[E_ITEM].y);
		}

		if (HUD_StandardTest(&elems[E_WEAPON]))
		{
			HUD_DrawPanel(1, elems[E_WEAPON].x, elems[E_WEAPON].y);
		}

		if (HUD_StandardTest(&elems[E_HEALTH]))
		{
			HUD_DrawHealth(elems[E_HEALTH].x, elems[E_HEALTH].y);
		}
	}

	/* 5 countdown */
	if (HUD_StandardTest(&elems[E_COUNTDOWN]))
	{
		HUD_DrawCountdown();
	}

	/* 6 debug target text */
	if (HUD_StandardTest(&elems[E_TARGET]) && (ps->stats[8] & 0x2) && !scope)
	{
		D_Text(elems[E_TARGET].x, elems[E_TARGET].y - 448, "Current target:", c_white, &font_small);
	}

	/* 7 mission messages (always) */
	HUD_DrawMission();

	/* 8 rank / spectator: remote mask only */
	if ((ps->remote_type == 0) || (ps->remote_type & elems[E_RANK].mask))
	{
		HUD_DrawRank();
	}

	/* 9, 10 flags */
	if ((ps->stats[6] || ps->stats[12]) && ((ps->remote_type == 0) || (ps->remote_type & elems[E_CTF].mask)))
	{
		if (dm == 4)
		{
			HUD_DrawCTF();
		}
		else if ((dm == 6) && (ps->remote_type != 16))
		{
			HUD_DrawControl();
		}
	}
}

/* ---------------------------------------------------------------------
 * section 3: crosshair
 * --------------------------------------------------------------------- */

void
HUD_DrawCrosshair(void)
{
	const sofps_t *ps;
	int w, h, n;
	const unsigned char *tint = c_white;

	if (!HUD_Active())
	{
		return;
	}

	/* SPEC-AMEND-005 I6h.2: when the cvar was modified since the last check (also once after its creation) and its
	   value is non-zero, a value above 4 or below 0 becomes 4 - the NUMERIC value only, the string is never
	   changed -, the mark is cleared and pics/menus/status/ch<trunc(value)> is looked up (missing: no
	   crosshair). A value of 0 leaves the mark set until a non-zero value is seen. */
	if (crosshair_cvar->modified && (crosshair_cvar->value != 0))
	{
		if ((crosshair_cvar->value > 4) || (crosshair_cvar->value < 0))
		{
			crosshair_cvar->value = 4;
		}

		crosshair_cvar->modified = false;
		n = (int)crosshair_cvar->value;
		Com_sprintf(crosshair_pic, sizeof(crosshair_pic), "pics/menus/status/ch%d", n);
		D_GetPicSize(&w, &h, crosshair_pic);

		if ((w <= 0) || (h <= 0))
		{
			crosshair_pic[0] = 0;
		}
	}

	ps = PS();

	if (!crosshair_pic[0] || (crosshair_cvar->value == 0) || (ps->stats[8] & 0x800) || !ps->stats[6] ||
		(ps->remote_type == 2) || (ps->remote_type == 4) || (ps->remote_type == 16))
	{
		return;
	}

	if (ps->stats[8] & 0x10)
	{
		tint = (ps->stats[8] & 0x20) ? c_green : c_red;
	}

	D_GetPicSize(&w, &h, crosshair_pic);
	/* OpenMerc V1: the view is the whole window in SoF mode; centred in the virtual window */
	HUD_DrawPic((HUD_W() - w) / 2, (HUD_H() - h) / 2, crosshair_pic, tint);
}

/* ---------------------------------------------------------------------
 * section 4: overhead player labels
 * --------------------------------------------------------------------- */

void
HUD_SetNameCols(int first, int last, int value)
{
	int i;

	for (i = first; (i <= last) && (i < 32); i++)
	{
		if (i >= 0)
		{
			name_cols[i] = value;
		}
	}
}

void
HUD_DrawLabels(void)
{
	int i, maxclients, local = cl.playernum, dm = CL_SoF_ServerDeathmatch();
	qboolean icons = atoi(CL_SoF_ConfigString(33)) != 0;
	qboolean names = shownames->value || atoi(CL_SoF_ConfigString(32));
	qboolean spect = (PS()->pm_type == 2) || (PS()->pm_type == 7);
	float cosfov = cosf(cl.refdef.fov_x * 0.5f * (float)M_PI / 180.0f);
	unsigned last = RGBA32(c_white);
	char myteam[64], team[64];

	if (!HUD_Active() || (PS()->stats[8] & 0x800) || !HUD_Ref() || !sre->DrawWorldLabel)
	{
		return;
	}

	maxclients = atoi(CL_SoF_ConfigString(31));

	if (maxclients > 32)
	{
		maxclients = 32;
	}

	HUD_PlayerField(local, 1, myteam, sizeof(myteam));

	for (i = 0; i < maxclients; i++)
	{
		centity_t *cent;
		vec3_t p, d, mins, maxs;
		float dist;
		char name[64], icon[MAX_QPATH];
		const char *pic = NULL;
		trace_t tr;

		if ((i == local) || !CL_SoF_ConfigString(1415 + i)[0])
		{
			continue;
		}

		if ((i + 1 >= cl_numentities) || !cl_entities)
		{
			continue;
		}

		cent = &cl_entities[i + 1];

		if (cent->serverframe != cl.frame.serverframe)
		{
			continue;
		}

		CL_SoF_SolidBox(cent->current.solid, mins, maxs);
		VectorCopy(cent->current.origin, p);
		p[2] += (maxs[2] + mins[2]) * 0.5f;

		/* 1 range, 2 cone */
		VectorSubtract(p, cl.refdef.vieworg, d);
		dist = VectorNormalize(d);

		if ((dist > 1024) || (DotProduct(d, cl.v_forward) < cosfov))
		{
			continue;
		}

		/* 5 visibility (against the world; entity occlusion not tested) */
		tr = CM_BoxTrace(cl.refdef.vieworg, p, vec3_origin, vec3_origin, 0, MASK_SOLID);

		if (tr.fraction < 1.0f)
		{
			continue;
		}

		HUD_PlayerField(i, 0, name, sizeof(name));
		HUD_PlayerField(i, 1, team, sizeof(team));

		if (!spect)
		{
			if (icons)
			{
				HUD_TeamIcon(i, icon, sizeof(icon));

				if (icon[0])
				{
					pic = icon;

					if (HUD_TeamMode() || (dm == 4) || (dm == 6))
					{
						last = RGBA32(HUD_TeamColour(i));
					}
					else
					{
						last = RGBA32(!Q_stricmp(team, myteam) ? c_green : c_red);
					}
				}
			}

			if (!pic && (name_cols[i] >= 1))
			{
				const char *n = CL_SoF_ConfigString(1447 + name_cols[i]);

				if (n[0])
				{
					Com_sprintf(icon, sizeof(icon), "pics/dmicons/%s", n);
					pic = icon;
				}
			}
		}

		p[2] = cent->current.origin[2] + maxs[2] + 2;
		sre->DrawWorldLabel(p, names ? name : NULL, (char *)pic, last);
	}
}

/* ---------------------------------------------------------------------
 * section 5: scoreboard layout language
 * --------------------------------------------------------------------- */

static const unsigned char layout_colours[16][4] = {
	{0, 0, 0, 255}, {0, 255, 0, 255}, {0, 170, 0, 255}, {0, 102, 0, 255}, {240, 240, 0, 255},
	{255, 0, 0, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
	{255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
	{255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}
};

void
HUD_ParseLayout(const char *s)
{
	Q_strlcpy(hud_layout, s, sizeof(hud_layout));
}

void
HUD_LayoutText(const char *s)
{
	if (s[0] == '*')
	{
		Q_strlcpy(hud_layout, s + 1, sizeof(hud_layout));
		return;
	}

	if (hud_layout[0])
	{
		Q_strlcat(hud_layout, " ", sizeof(hud_layout));
	}

	Q_strlcat(hud_layout, s, sizeof(hud_layout));
}

static void
HUD_Portrait(int slot, int x, int y)
{
	char model[64], pic[MAX_QPATH];
	int w, h;

	HUD_PlayerField(slot, 2, model, sizeof(model));
	Com_sprintf(pic, sizeof(pic), "ghoul/pmodels/portraits/%s", model[0] ? model : "mullins");
	D_GetPicSize(&w, &h, pic);

	if ((w <= 0) || (h <= 0))
	{
		Q_strlcpy(pic, "ghoul/pmodels/portraits/mullins", sizeof(pic));
	}

	HUD_DrawPic(x, y, pic, c_white);
}

static int
HUD_LayoutInt(const char **s)
{
	return atoi(COM_Parse((char **)s));
}

static void
HUD_RowName(int slot, int X, int Y, int kind)
{
	char name[64], team[64], myteam[64], d[16], myd[16], icon[MAX_QPATH];
	const unsigned char *col = c_red;
	qboolean icons = atoi(CL_SoF_ConfigString(33)) != 0;

	HUD_PlayerField(slot, 0, name, sizeof(name));
	HUD_PlayerField(slot, 1, team, sizeof(team));
	HUD_PlayerField(cl.playernum, 1, myteam, sizeof(myteam));
	HUD_TeamDigit(slot, d, sizeof(d));
	HUD_TeamDigit(cl.playernum, myd, sizeof(myd));

	if ((slot == cl.playernum) || (spectator_cvar && spectator_cvar->value))
	{
		col = c_green;
	}
	else if (kind == 1) /* client_ctf_sb */
	{
		col = !strcmp(d, myd) ? c_green : c_red;
	}
	else if ((CL_SoF_ServerDeathmatch() == 1) && icons)
	{
		HUD_TeamIcon(slot, icon, sizeof(icon));
		col = (icon[0] && !Q_stricmp(team, myteam)) ? c_green : c_red;
	}
	else if (icons && !strcmp(d, myd))
	{
		col = c_green;
	}

	HUD_DrawColouredString(X + 32, Y, name, col);
}

static void
HUD_TeamFlagPic(int team, int X, int Y)
{
	D_Stretch(X, Y, 32, 32, c_white, team ? "pics/interface2/ctfr_hudflag" : "pics/interface2/ctfb_hudflag", 0);
}

void
HUD_DrawLayout(void)
{
	const char *s = hud_layout;
	char *tok;
	int x = 0, y = 0, W = HUD_W(), H = HUD_H();
	const unsigned char *tc = c_white, *ac = c_grey;
	char buf[128];

	if (!HUD_Active() || !(PS()->stats[9] & 0x1) || !s[0])
	{
		return;
	}

	while (s)
	{
		tok = COM_Parse((char **)&s);

		if (!s || !tok[0])
		{
			break;
		}

		if (!strcmp(tok, "xl"))
		{
			x = HUD_LayoutInt(&s);
		}
		else if (!strcmp(tok, "xr"))
		{
			x = W + HUD_LayoutInt(&s);
		}
		else if (!strcmp(tok, "xv"))
		{
			x = W / 2 - 160 + HUD_LayoutInt(&s);
		}
		else if (!strcmp(tok, "yt"))
		{
			y = HUD_LayoutInt(&s);
		}
		else if (!strcmp(tok, "yb"))
		{
			y = H + HUD_LayoutInt(&s);
		}
		else if (!strcmp(tok, "yv"))
		{
			y = H / 2 - 120 + HUD_LayoutInt(&s);
		}
		else if (!strcmp(tok, "picn"))
		{
			HUD_DrawPic(x, y, COM_Parse((char **)&s), c_white);
		}
		else if (!strcmp(tok, "string"))
		{
			HUD_DrawColouredString(x, y, COM_Parse((char **)&s), tc);
		}
		else if (!strcmp(tok, "altstring"))
		{
			const char *t = COM_Parse((char **)&s);
			int i;

			for (i = 0; t[i]; i++)
			{
				D_Char(x + 8 * i, y, (byte)t[i], ac); /* OpenMerc: colour, not the Q2 high-bit charset */
			}
		}
		else if (!strcmp(tok, "tc"))
		{
			tc = layout_colours[HUD_LayoutInt(&s) & 15];
		}
		else if (!strcmp(tok, "ac"))
		{
			ac = layout_colours[HUD_LayoutInt(&s) & 15];
		}
		else if (!strcmp(tok, "client_sb") || !strcmp(tok, "client_ctf_sb"))
		{
			qboolean ctf = !strcmp(tok, "client_ctf_sb");
			int X, Y, cn, score, ping, time, flag = 0;

			x = HUD_LayoutInt(&s);
			y = HUD_LayoutInt(&s);
			X = W / 2 - 160 + x;
			Y = H / 2 - 120 + y;
			x = X;
			y = Y;
			cn = HUD_LayoutInt(&s);
			score = HUD_LayoutInt(&s);
			ping = HUD_LayoutInt(&s);
			time = HUD_LayoutInt(&s);

			if (ctf)
			{
				flag = HUD_LayoutInt(&s);
			}

			if ((cn < 0) || (cn >= 32))
			{
				Com_Error(ERR_DROP, "client >= MAX_CLIENTS");
				return;
			}

			HUD_RowName(cn, X, Y, ctf ? 1 : 0);
			HUD_DrawColouredString(X + 32, Y + 8, CL_StringById(62), c_grey);
			Com_sprintf(buf, sizeof(buf), " %i", score);
			HUD_DrawColouredString(X + 88, Y + 8, buf, c_white);
			Com_sprintf(buf, sizeof(buf), "%s%i", CL_StringById(63), ping);
			HUD_DrawColouredString(X + 32, Y + 16, buf, c_grey);
			Com_sprintf(buf, sizeof(buf), "%s%i", CL_StringById(64), time);
			HUD_DrawColouredString(X + 32, Y + 24, buf, c_grey);
			HUD_Portrait(cn, X, Y);

			if (ctf && (flag == 1))
			{
				D_Stretch(X - 32, Y, 32, 32, c_white, "pics/interface2/ctfb_hudflag", 0);
			}
			else if (ctf && flag)
			{
				D_Stretch(X - 35, Y, 32, 32, c_white, "pics/interface2/ctfr_hudflag", 0);
			}
		}
		else if (!strcmp(tok, "spect_sb"))
		{
			int X, Y, cn, ping, time;
			char name[64];

			x = HUD_LayoutInt(&s);
			y = HUD_LayoutInt(&s);
			X = x = W / 2 - 160 + x;
			Y = y = H / 2 - 120 + y;
			cn = HUD_LayoutInt(&s);
			ping = HUD_LayoutInt(&s);
			time = HUD_LayoutInt(&s);

			if ((cn < 0) || (cn >= 32))
			{
				Com_Error(ERR_DROP, "client >= MAX_CLIENTS");
				return;
			}

			HUD_PlayerField(cn, 0, name, sizeof(name));
			HUD_DrawColouredString(X + 32, Y, name, c_green);
			HUD_DrawColouredString(X + 32, Y + 8, CL_StringById(65), c_white);
			Com_sprintf(buf, sizeof(buf), "%s%i", CL_StringById(63), ping);
			HUD_DrawColouredString(X + 32, Y + 16, buf, c_grey);
			Com_sprintf(buf, sizeof(buf), "%s%i", CL_StringById(64), time);
			HUD_DrawColouredString(X + 32, Y + 24, buf, c_grey);
			HUD_Portrait(cn, X, Y);
		}
		else if (!strcmp(tok, "team_sb"))
		{
			int X, Y, cn, score;
			char icon[MAX_QPATH], team[64], myteam[64];

			x = HUD_LayoutInt(&s);
			y = HUD_LayoutInt(&s);
			X = x = W / 2 - 160 + x;
			Y = y = H / 2 - 120 + y;
			cn = HUD_LayoutInt(&s);
			score = HUD_LayoutInt(&s);

			if ((cn < 0) || (cn >= 32))
			{
				Com_Error(ERR_DROP, "client >= MAX_CLIENTS");
				return;
			}

			HUD_DrawColouredString(X + 32, Y + 12, CL_StringById(62), c_grey);
			Com_sprintf(buf, sizeof(buf), "%i", score);
			HUD_DrawColouredString(X + 88, Y + 12, buf, c_white);
			HUD_TeamIcon(cn, icon, sizeof(icon));
			HUD_PlayerField(cn, 1, team, sizeof(team));
			HUD_PlayerField(cl.playernum, 1, myteam, sizeof(myteam));

			if (icon[0])
			{
				HUD_DrawPic(X, Y, icon, !Q_stricmp(team, myteam) ? c_green : c_red);
			}
			else
			{
				HUD_Portrait(cn, X, Y);
			}
		}
		else if (!strcmp(tok, "client_conq_sb") || !strcmp(tok, "ctf_team_sb") ||
				!strcmp(tok, "control_team_sb"))
		{
			int X, Y, team, score, extra = 0;
			qboolean ctf = !strcmp(tok, "ctf_team_sb"), control = !strcmp(tok, "control_team_sb");

			x = HUD_LayoutInt(&s);
			y = HUD_LayoutInt(&s);
			X = x = W / 2 - 160 + x;
			Y = y = H / 2 - 120 + y;
			team = HUD_LayoutInt(&s);
			score = HUD_LayoutInt(&s);

			if (ctf || control)
			{
				extra = HUD_LayoutInt(&s);
			}

			HUD_TeamFlagPic(team, X, Y);

			if (control)
			{
				HUD_DrawColouredString(X + 32, Y + 22, CL_StringById(62), c_grey);
				Com_sprintf(buf, sizeof(buf), "%i", score);
				HUD_DrawColouredString(X + 88, Y + 22, buf, c_white);
				HUD_DrawColouredString(X + 32, Y + 12, CL_StringById(67), c_grey);
				Com_sprintf(buf, sizeof(buf), "%i", extra);
				HUD_DrawColouredString(X + 150, Y + 12, buf, c_white);
			}
			else
			{
				HUD_DrawColouredString(X + 32, Y + 12, CL_StringById(62), c_grey);
				Com_sprintf(buf, sizeof(buf), "%i", score);
				HUD_DrawColouredString(X + 88, Y + 12, buf, c_white);

				if (ctf)
				{
					HUD_DrawColouredString(X + 32, Y + 22, CL_StringById(66), c_grey);
					Com_sprintf(buf, sizeof(buf), "%i", extra);
					HUD_DrawColouredString(X + 150, Y + 22, buf, c_white);
				}
			}
		}
		/* unknown tokens are skipped */
	}
}

/* ---------------------------------------------------------------------
 * section 8: remote-view overlays
 * --------------------------------------------------------------------- */

void
HUD_DrawRemoteOverlay(void)
{
	const sofps_t *ps;
	int W = HUD_W(), H = HUD_H(), bar;

	if (!HUD_Active())
	{
		return;
	}

	ps = PS();

	if (ps->remote_type == 2)
	{
		bar = (int)(48.0f * H / 480);
		D_Fill(0, 0, W, bar, c_black);
		D_Fill(0, H - bar, W, bar, c_black);
	}
	else if (ps->remote_type == 4)
	{
		D_Stretch(0, 0, W / 2, H / 2, c_white, "pics/monitor/monitor", 0);
		D_Stretch(W, 0, -(W / 2), H / 2, c_white, "pics/monitor/monitor", 0);
		D_Stretch(0, H, W / 2, -(H / 2), c_white, "pics/monitor/monitor", 0);
		D_Stretch(W, H, -(W / 2), -(H / 2), c_white, "pics/monitor/monitor", 0);
	}
}

/* ---------------------------------------------------------------------
 * section 15: client commands
 * --------------------------------------------------------------------- */

static qboolean ignored[32];

qboolean
HUD_IsIgnored(int slot)
{
	return (slot >= 0) && (slot < 32) && ignored[slot];
}

static int
HUD_FindClient(const char *arg)
{
	int i, n;
	char name[64];

	if ((arg[0] >= '0') && (arg[0] <= '9'))
	{
		n = atoi(arg);

		if ((n < 0) || (n >= 32))
		{
			Com_Printf("Bad client slot: %i\n", n);
			return -1;
		}

		if (!CL_SoF_ConfigString(1415 + n)[0])
		{
			Com_Printf("Client %i is not active\n", n);
			return -1;
		}

		return n;
	}

	for (i = 0; i < 32; i++)
	{
		HUD_PlayerField(i, 0, name, sizeof(name));

		if (name[0] && !Q_stricmp(name, arg))
		{
			return i;
		}
	}

	return -1;
}

static void
HUD_Ignore(qboolean on)
{
	int n;
	char name[64];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("ignore <client name> or <client id number>\n");
		return;
	}

	n = HUD_FindClient(Cmd_Argv(1));

	if (n < 0)
	{
		return;
	}

	ignored[n] = on;
	HUD_PlayerField(n, 0, name, sizeof(name));
	Com_Printf(on ? "Client %i named %s is now ignored\n" :
			"Client %i named %s is now free to message you\n", n, name);
}

static void
HUD_Ignore_f(void)
{
	HUD_Ignore(true);
}

static void
HUD_Unignore_f(void)
{
	HUD_Ignore(false);
}

static void
HUD_ListIgnore_f(void)
{
	int i, n = 0;
	char name[64];

	for (i = 0; i < 32; i++)
	{
		if (!ignored[i])
		{
			continue;
		}

		if (!CL_SoF_ConfigString(1415 + i)[0])
		{
			ignored[i] = false; /* entries of empty slots are dropped */
			continue;
		}

		HUD_PlayerField(i, 0, name, sizeof(name));
		Com_Printf("Client id %i named %s is ignored.\n", i, name);
		n++;
	}

	if (!n)
	{
		Com_Printf("No one is Ignored\n");
	}
}

void
HUD_Ignore_Init(void)
{
	memset(ignored, 0, sizeof(ignored));
	memset(name_cols, 0, sizeof(name_cols));
	hud_countdown = 0;
	hud_layout[0] = 0;
}

static void
HUD_AddFave_f(void)
{
	char name[16];
	int i, freeslot = -1;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: addfave <ip>\n");
		return;
	}

	for (i = 0; i < 20; i++)
	{
		const char *v;

		Com_sprintf(name, sizeof(name), "adr%d", i);
		v = Cvar_Get(name, "", CVAR_ARCHIVE)->string;

		if (!Q_stricmp(v, Cmd_Argv(1)))
		{
			Com_Printf("Favourite already in list.\n");
			return;
		}

		if (!v[0] && (freeslot < 0))
		{
			freeslot = i;
		}
	}

	if (freeslot < 0)
	{
		Com_Printf("No free slots to add favourite %s.\n", Cmd_Argv(1));
		return;
	}

	Com_sprintf(name, sizeof(name), "adr%d", freeslot);
	Cvar_Set(name, Cmd_Argv(1));
	Com_Printf("Server at %s has been added to list.\n", Cmd_Argv(1));
}

static void
HUD_RemFave_f(void)
{
	char name[16];
	int i;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: remfave <ip>\n");
		return;
	}

	for (i = 0; i < 20; i++)
	{
		Com_sprintf(name, sizeof(name), "adr%d", i);

		if (!Q_stricmp(Cvar_Get(name, "", CVAR_ARCHIVE)->string, Cmd_Argv(1)))
		{
			Cvar_Set(name, "");
			Com_Printf("Favourite %s removed from list.\n", Cmd_Argv(1));
			return;
		}
	}

	Com_Printf("Could not find %s to remove.\n", Cmd_Argv(1));
}

static void
HUD_Terrain_f(void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: terrain <terrainFileName>\n");
		return;
	}

	if (HUD_Ref() && sre->SetTerrain)
	{
		sre->SetTerrain(Cmd_Argv(1));
	}
}

static void
HUD_Cloud_f(void)
{
	float v[7];
	int i;

	if (Cmd_Argc() < 3)
	{
		Com_Printf("Usage: cloud <type> <density> <angle> <speed> <r> <g> <b>\n");
	}

	for (i = 0; i < 7; i++)
	{
		v[i] = (float)atof(Cmd_Argv(i + 1));
	}

	if (HUD_Ref() && sre->SetCloud)
	{
		sre->SetCloud(v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
	}
}

static void
HUD_Touch_f(void)
{
	if (HUD_Ref() && sre->TouchAllImages)
	{
		sre->TouchAllImages();
	}
}

/* note: the original never changes the string of an out-of-range `crosshair`; HUD_DrawCrosshair forces only
   its numeric value to 4 (SPEC-AMEND-005 I6h.2) */
void
HUD_Init(void)
{
	int i;
	char name[16];

	scr_interface = Cvar_Get("scr_interface", "1", CVAR_ARCHIVE);
	crosshair_cvar = Cvar_Get("crosshair", "0", CVAR_ARCHIVE);
	shownames = Cvar_Get("shownames", "0", CVAR_ARCHIVE);
	spectator_cvar = Cvar_Get("spectator", "0", CVAR_USERINFO);
	Cvar_Get("cl_subtitles", "1", CVAR_ARCHIVE);

	for (i = 0; i < 20; i++)
	{
		Com_sprintf(name, sizeof(name), "adr%d", i);
		Cvar_Get(name, "", CVAR_ARCHIVE);
	}

	Cmd_AddCommand("ignore", HUD_Ignore_f);
	Cmd_AddCommand("unignore", HUD_Unignore_f);
	Cmd_AddCommand("listignore", HUD_ListIgnore_f);
	Cmd_AddCommand("addfave", HUD_AddFave_f);
	Cmd_AddCommand("remfave", HUD_RemFave_f);
	Cmd_AddCommand("terrain", HUD_Terrain_f);
	Cmd_AddCommand("cloud", HUD_Cloud_f);
	Cmd_AddCommand("cl_touch", HUD_Touch_f);

	HUD_Ignore_Init();
}
