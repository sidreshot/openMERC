/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 OpenMerc contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * The header file for the upper level key event processing
 *
 * =======================================================================
 *
 * OpenMerc: key numbers are those of Soldier of Fortune 1.07f: Windows
 * virtual-key codes plus a few engine numbers (mouse buttons, wheel, keypad
 * Enter, right-hand modifiers, double clicks, joystick). The Quake II / Yamagi
 * K_ names that other client code uses are kept as aliases of these numbers.
 * Owner: I6h. Other packages: include, do not edit.
 *
 * Implements: SPEC-312 (section 1 key numbers, section 4 key events)
 */

#ifndef CL_HEADER_KEYBOARD_H
#define CL_HEADER_KEYBOARD_H

#include "../../common/header/shared.h" /* for qboolean etc */

/* console input line: 255 characters after the prompt (SPEC-312 5.2);
   the buffer keeps Quake II's size so other code can format into it */
#define MAXCMDLINE 1024
#define SOF_CMDLINE_CHARS 255

/* number of console command lines saved in history,
 * must be a power of two, because we use & (NUM_KEY_LINES-1)
 * instead of % so -1 wraps to NUM_KEY_LINES-1 */
#define NUM_KEY_LINES 32

/* kept for the gamepad code (no SoF meaning) */
extern qboolean joy_altselector_pressed;

/* SPEC-312 section 1: key numbers 0..255 */
enum QKEYS {
	K_MOUSE1 = 1,
	K_MOUSE2 = 2,
	K_MOUSE3 = 3,
	K_MOUSE4 = 4,
	K_MOUSE5 = 5,
	K_BACKSPACE = 8,
	K_TAB = 9,
	K_KP_5_NOLOCK = 12, /* keypad 5 with Num Lock off, becomes K_KP_5 */
	K_ENTER = 13,
	K_KP_ENTER = 14,
	K_LSHIFT = 16,
	K_LCTRL = 17,
	K_LALT = 18,
	K_PAUSE = 19,
	K_CAPSLOCK = 20,
	K_RSHIFT = 21,
	K_RCTRL = 22,
	K_RALT = 23,
	K_MWHEELDOWN = 24,
	K_MWHEELUP = 25,
	K_ESCAPE = 27,
	K_SPACE = 32,
	K_PGUP = 33,
	K_PGDN = 34,
	K_END = 35,
	K_HOME = 36,
	K_LEFTARROW = 37,
	K_UPARROW = 38,
	K_RIGHTARROW = 39,
	K_DOWNARROW = 40,
	K_INS = 45,
	K_DEL = 46,
	/* 48..57 digits, 65..90 letters (their ASCII codes, upper case) */
	K_KP_INS = 96,
	K_KP_END = 97,
	K_KP_DOWNARROW = 98,
	K_KP_PGDN = 99,
	K_KP_LEFTARROW = 100,
	K_KP_5 = 101,
	K_KP_RIGHTARROW = 102,
	K_KP_HOME = 103,
	K_KP_UPARROW = 104,
	K_KP_PGUP = 105,
	K_KP_STAR = 106,
	K_KP_PLUS = 107,
	K_KP_MINUS = 109,
	K_KP_DEL = 110,
	K_KP_SLASH = 111,
	K_F1 = 112,
	K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11,
	K_F12 = 123,
	K_MOUSE1DBL = 128,
	K_MOUSE2DBL, K_MOUSE3DBL, K_MOUSE4DBL,
	K_MOUSE5DBL = 132,
	K_KP_NUMLOCK = 144,
	K_SCROLLOCK = 145,
	K_SEMICOLON = 186,
	K_EQUALS = 187,
	K_COMMA = 188,
	K_MINUS = 189,
	K_PERIOD = 190,
	K_SLASH = 191,
	K_GRAVE = 192,
	K_JOY1 = 193,
	K_JOY2, K_JOY3,
	K_JOY4 = 196,
	K_LBRACKET = 219,
	K_BACKSLASH = 220,
	K_RBRACKET = 221,
	K_APOSTROPHE = 222,
	K_EXCLAM = 223,
	K_AUX1 = 224,
	K_AUX2, K_AUX3, K_AUX4, K_AUX5, K_AUX6, K_AUX7, K_AUX8, K_AUX9, K_AUX10,
	K_AUX11, K_AUX12, K_AUX13, K_AUX14, K_AUX15, K_AUX16, K_AUX17, K_AUX18,
	K_AUX19, K_AUX20, K_AUX21, K_AUX22, K_AUX23, K_AUX24, K_AUX25, K_AUX26,
	K_AUX27, K_AUX28, K_AUX29, K_AUX30, K_AUX31,
	K_AUX32 = 255,

	K_LAST = 256,

	/* Quake II / Yamagi names used by other client code */
	K_SHIFT = K_LSHIFT,
	K_CTRL = K_LCTRL,
	K_ALT = K_LALT,

	/* gamepad buttons (SDL order) as SoF joystick keys: buttons 1-4 are
	   JOY1-JOY4, the d-pad is the hat (AUX29 up, AUX30 right, AUX31 down,
	   AUX32 left), the others AUX5 upwards (SPEC-312 section 3) */
	K_BTN_SOUTH = K_JOY1,
	K_JOY_FIRST_BTN = K_BTN_SOUTH,
	K_BTN_EAST = K_JOY2,
	K_BTN_WEST = K_JOY3,
	K_BTN_NORTH = K_JOY4,
	K_BTN_BACK = K_AUX5,
	K_BTN_GUIDE = K_AUX6,
	K_BTN_START = K_AUX7,
	K_STICK_LEFT = K_AUX8,
	K_STICK_RIGHT = K_AUX9,
	K_SHOULDER_LEFT = K_AUX10,
	K_SHOULDER_RIGHT = K_AUX11,
	K_DPAD_UP = K_AUX29,
	K_DPAD_DOWN = K_AUX31,
	K_DPAD_LEFT = K_AUX32,
	K_DPAD_RIGHT = K_AUX30,
	K_BTN_MISC1 = K_AUX12,
	K_PADDLE_RIGHT_1 = K_AUX13,
	K_PADDLE_LEFT_1 = K_AUX14,
	K_PADDLE_RIGHT_2 = K_AUX15,
	K_PADDLE_LEFT_2 = K_AUX16,
	K_TOUCHPAD = K_AUX17,
	K_BTN_MISC2 = K_AUX18,
	K_BTN_MISC3 = K_AUX19,
	K_BTN_MISC4 = K_AUX20,
	K_BTN_MISC5 = K_AUX21,
	K_BTN_MISC6 = K_AUX22,
	K_TRIG_LEFT = K_AUX23,
	K_TRIG_RIGHT = K_AUX24,
	/* SoF has no "alternate" gamepad layer */
	K_JOY_FIRST_BTN_ALT = K_LAST
};

/* key flags of the SPEC-312 key table */
#define KEYF_MENU 0x01  /* M: runs its binding while a menu has key focus */
#define KEYF_TEXT 0x02  /* C: edits the console line in console / unconnected */
#define KEYF_CLICK 0x04 /* T: typing click in menu input fields */

extern char		*keybindings[K_LAST];
extern int		key_repeats[K_LAST];
extern qboolean	keydown[K_LAST];
extern int		anykeydown;
extern char		chat_buffer[];
extern int		chat_bufferlen;
extern int		chat_cursorpos;
extern qboolean	chat_team;

qboolean IN_NumpadIsOn();
void Char_Event(int key);
void Key_TextInput(const char *utf8);
void Key_TextInputCodepoint(unsigned cp);
int Key_UnicodeToFont(unsigned cp);
/* key event; 'special' is ignored (Yamagi signature kept). The key
   number is a SoF key number, events without the Windows extended-key
   flag go through Key_EventEx with extended = false. */
void Key_Event(int key, qboolean down, qboolean special);
void Key_EventEx(int key, qboolean down, qboolean extended, unsigned time);
void Key_Init(void);
void Key_Shutdown(void);
void Key_WriteBindings(FILE *f);
void Key_ReadConsoleHistory();
void Key_WriteConsoleHistory();
void Key_SetBinding(int keynum, char *binding);
void Key_MarkAllUp(void);
void Controller_Rumble(const char *name, vec3_t source, qboolean from_player,
				unsigned int duration, unsigned short int volume);
void Haptic_Feedback(const char *name, int effect_volume, int effect_duration,
				int effect_delay, int effect_attack, int effect_fade,
				int effect_x, int effect_y, int effect_z, float effect_distance);
int Key_GetMenuKey(int key);

/* SPEC-312 section 1/2 lookups (for menus and HUD) */
int Key_StringToKeynum(const char *str);       /* -1 when not a key name */
int Key_Flags(int key);                         /* KEYF_* */
int Key_Char(int key, qboolean shifted);        /* layout character, 0 = none */
int Key_ShiftDown(void);
const char *Key_LocalisedName(int key);         /* KEY_<name> string, "" if none */
extern qboolean key_bindings_changed;           /* menus re-read bindings */

/* the pending weapon command of SPEC-310 section 14 / SPEC-312 section 4 */
const char *Key_PendingWeaponCommand(void);
void Key_ClearPendingWeaponCommand(void);

/* button held flags and "any key held" for the user command (cl_input.c) */
void Key_ReleaseAll(void);

#endif
