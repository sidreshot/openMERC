/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc MENU-SCRATCH: TrueType menu text (m_font.c).
 */

#ifndef OM_M_FONT_H
#define OM_M_FONT_H

#include "../../../common/header/sof/refapi.h"

void MF_SetRenderer(OM_SetAlphaImage_t upload); /* NULL: no TrueType text */
void MF_NewFrame(void);
qboolean MF_Ready(const char *font);           /* this menu font is drawn by m_font.c */
float MF_WidthPx(const char *text, const char *font, float z);
int MF_LineHeightPx(const char *font, float z);
void MF_Draw(int x, int y, const char *text, unsigned rgba, const char *font, int keep, float z,
	void (*part)(int x, int y, int l, int t, int r, int b, const byte c[4], const char *name));

int MF_ConsolePitch(int cell);
qboolean MF_DrawCell(int x, int y, int ch, int cell, const byte color[4],
	void (*part)(int x, int y, int l, int t, int r, int b, const byte c[4], const char *name));

#endif
