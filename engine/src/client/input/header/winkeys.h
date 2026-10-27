/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I6h. Windows keyboard messages queued for the SDL input backend.
 *
 * Implements: SPEC-312 section 3
 */

#ifndef OM_INPUT_WINKEYS_H
#define OM_INPUT_WINKEYS_H

void IN_WinKeys_Init(void);
void IN_WinKeys_Shutdown(void);
/* non-zero while keyboard events come from the message hook */
int IN_WinKeys_Active(void);
/* next queued event; returns 0 when the queue is empty. altenter = 1 marks
   an Alt+Enter press (no key event); altenter = 2 marks a typed character
   (WM_CHAR), its UTF-16 code unit in *down. */
int IN_WinKeys_Next(int *key, int *down, int *extended, unsigned *age, int *altenter);

#endif
