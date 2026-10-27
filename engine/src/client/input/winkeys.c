/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Windows keyboard messages as Soldier of Fortune key events. SoF key numbers
 * are Windows virtual-key codes (with the extended-key flag carried along),
 * so on Windows the keyboard is read from the window messages themselves,
 * through SDL's message hook, before SDL translates them. The SDL input
 * backend drains the queue every frame.
 *
 * Implements: SPEC-312 section 3 (Windows messages -> key events)
 */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL3/SDL.h>

#include "header/winkeys.h"

#define QUEUE_SIZE 256

typedef struct
{
	int key;
	int down;
	int extended;
	unsigned age; /* milliseconds between the message time and its arrival */
	int altenter;
} winkey_t;

static winkey_t queue[QUEUE_SIZE];
static unsigned qhead, qtail;
static int active;

static void
Push(int key, int down, int extended, unsigned age, int altenter)
{
	winkey_t *k;

	if (qhead - qtail >= QUEUE_SIZE)
	{
		return; /* full: drop */
	}

	k = &queue[qhead++ % QUEUE_SIZE];
	k->key = key;
	k->down = down;
	k->extended = extended;
	k->age = age;
	k->altenter = altenter;
}

static bool SDLCALL
Hook(void *userdata, MSG *msg)
{
	int vk, ext, sc, down, key;
	unsigned age;

	(void)userdata;

	switch (msg->message)
	{
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
			break;

		case WM_CHAR:
			/* the layout's character for the key just queued (TranslateMessage);
			   read here so it does not depend on SDL's keyboard focus. Characters
			   outside the Basic Multilingual Plane are dropped. */
			if ((msg->wParam < 0xD800) || (msg->wParam > 0xDFFF))
			{
				Push(0, (int)(msg->wParam & 0xFFFF), 0, 0, 2);
			}
			return true;

		default:
			return true;
	}

	vk = (int)(msg->wParam & 0xFF);
	ext = (int)((msg->lParam >> 24) & 1);
	sc = (int)((msg->lParam >> 16) & 0xFF);
	down = (msg->message == WM_KEYDOWN) || (msg->message == WM_SYSKEYDOWN);
	age = GetTickCount() - (unsigned)msg->time;

	if (age > 10000)
	{
		age = 0;
	}

	/* Alt+Enter is not a key event (SPEC-312 section 3) */
	if ((msg->message == WM_SYSKEYDOWN) && (vk == VK_RETURN) && (msg->lParam & (1 << 29)))
	{
		Push(0, 1, 0, age, 1);
		return true;
	}

	if ((vk == VK_RETURN) && ext)
	{
		key = 14; /* KP_ENTER */
	}
	else if (vk == VK_SHIFT)
	{
		key = (sc == 0x36) ? 21 : 16; /* RSHIFT / LSHIFT */
	}
	else if (vk == VK_CONTROL)
	{
		key = ext ? 22 : 17; /* RCTRL / LCTRL */
	}
	else if (vk == VK_MENU)
	{
		key = ext ? 23 : 18; /* RALT / LALT */
	}
	else
	{
		key = vk;
	}

	Push(key, down, ext, age, 0);
	return true;
}

void
IN_WinKeys_Init(void)
{
	qhead = qtail = 0;
	SDL_SetWindowsMessageHook(Hook, NULL);
	active = 1;
}

void
IN_WinKeys_Shutdown(void)
{
	SDL_SetWindowsMessageHook(NULL, NULL);
	active = 0;
}

int
IN_WinKeys_Active(void)
{
	return active;
}

int
IN_WinKeys_Next(int *key, int *down, int *extended, unsigned *age, int *altenter)
{
	winkey_t *k;

	if (qtail == qhead)
	{
		return 0;
	}

	k = &queue[qtail++ % QUEUE_SIZE];
	*key = k->key;
	*down = k->down;
	*extended = k->extended;
	*age = k->age;
	*altenter = k->altenter;
	return 1;
}

#else

#include "header/winkeys.h"

void IN_WinKeys_Init(void) {}
void IN_WinKeys_Shutdown(void) {}
int IN_WinKeys_Active(void) { return 0; }

int
IN_WinKeys_Next(int *key, int *down, int *extended, unsigned *age, int *altenter)
{
	(void)key; (void)down; (void)extended; (void)age; (void)altenter;
	return 0;
}

#endif
