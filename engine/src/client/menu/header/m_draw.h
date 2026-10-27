/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: 2D drawing used by the menu runtime. Every function is one
 * slot of the SoF renderer interface (SPEC-002) with the drawing rules of
 * SPEC-412. Until the renderer exports these slots to the engine (package
 * I4a), m_draw.c implements them in software into one RGBA layer that is
 * presented with the renderer's raw-image call.
 *
 * Implements: SPEC-002 (2D slots), SPEC-412 (sections 2-6), SPEC-132
 */

#ifndef OM_M_DRAW_H
#define OM_M_DRAW_H

#include "../../../common/header/shared.h"

qboolean MR_Virtual(void); /* MENU2: virtual-window layout active (renderer table, scr_aspect 1) */
float MR_Scale(void);    /* MENU2: 2D factor of the menus (1 with the software layer) */
qboolean MR_RailWanted(void); /* MENU-SCRATCH: menu_rail on and aspect at least 3:2 */
qboolean MR_RailShown(void);  /* MENU-SCRATCH: rail wanted and the composition fits */
qboolean MR_WideLayout(void); /* MENU-SCRATCH: menu_layout (openMERC wide layout) */
#define MENU_COMP_RAIL 200     /* MENU-SCRATCH: rail width + gap, menu units */
#define MENU_RAIL_W 190
#define MENU_RAIL_MARGIN 8     /* least window edge left of the rail, both sides together */
int MR_VirtualW(void);   /* window size in menu units: W / MR_Scale() */
int MR_VirtualH(void);
void MR_BeginFrame(void);
void MR_EndFrame(void);
void MR_Shutdown(void);
void MR_ReleasePics(void);                                         /* menu system closed */

void MR_GetPicSize(int *w, int *h, const char *name);              /* 0x44 */
void MR_DrawPic(int x, int y, const char *name, unsigned rgba);     /* 0x48 */
void MR_DrawPicPart(int x, int y, int l, int t, int r, int b, unsigned rgba,
		const char *name);                                          /* 0x50 */
void MR_StretchPic(int x, int y, int w, int h, unsigned rgba,
		const char *name, int rotation);                            /* 0x54 (+ flips 4-7, SPEC-802) */
void MR_Fill(int x, int y, int w, int h, unsigned rgba);            /* 0x74 */
/* MENU-SCRATCH: own flat chrome on whole window pixels (colours 0xAABBGGRR) */
void MR_FlatBox(int x, int y, int w, int h, unsigned fill, unsigned border, unsigned accent,
		qboolean openbottom);
void MR_FlatGradient(int x, int y, int w, int h, unsigned top, unsigned bottom);
void MR_FlatArrow(int x, int y, int w, int h, qboolean down, unsigned rgba);
void MR_DrawOwnCursor(int mx, int my);
void MR_Bevel(int x, int y, int w, int h, unsigned rgba);           /* 0x78 */
void MR_Line(float x1, float y1, float x2, float y2, float width,
		unsigned rgba);                                             /* 0x7c */
void MR_SetClip(int x, int y, int w, int h);                        /* 0x8c */
void MR_ResetClip(void);                                            /* 0x90 */
void MR_ClipRight(int x);                                           /* 0x94 */
void MR_ClipLeft(int x);                                            /* 0x98 */
void MR_Reclip(void);                                               /* 0x9c */
void MR_RawPic(const char *name, int w, int h, const byte *rgba);   /* 0xa0 */
void MR_FreePic(const char *name);                                  /* 0x24 */
void MR_ColorCorrection(float a, float b, float c, float d);        /* 0xc0 */
void MR_DrawString(int x, int y, const char *text, unsigned rgba,
		const char *font, int keep);                                /* 0xc8 */
int MR_StringWidth(const char *text, const char *font);             /* 0xcc */
int MR_FontHeight(const char *font);                                /* 0xd0 */
qboolean MR_Capture(const char *path);
/* I1c: menu-model mode (slots 0xb8 / 0xb4) around one Ghoul model; false = not drawable (software 2D) */
qboolean MR_ModelBegin(qboolean forcedcamera);
void MR_ModelEnd(qboolean forcedcamera);
qboolean MR_Thumbnail(byte *out128x128);                           /* 0xd4 */                             /* test capture */
void MR_ZoomBegin(int dx, int dy, float z);  /* MENU-SCRATCH page zoom */
void MR_ZoomEnd(void);

#endif
