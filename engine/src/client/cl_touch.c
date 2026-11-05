/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * OpenMerc: touch controls (phones, tablets; in_touch, on by default on Android).
 *
 * In the game (not in menus or the console):
 *  - left half of the screen: a joystick that appears where the thumb lands - walking and strafing, the
 *    farther from the anchor the faster (full deflection runs);
 *  - right half: dragging turns the view; the round buttons there are fire, alt fire, jump, crouch (a
 *    toggle) and reload - fire keeps turning the view while the finger moves on it;
 *  - the HUD trays sit at the top (cl_hud.c): a tap on the item tray selects the next item (the ] key), a
 *    long press uses it; a tap on the weapon tray selects the next weapon, a long press the previous one;
 *  - a small button between the trays opens the menu (Escape);
 *  - automatic use is the game's (om_autouse, game/src/g_utils.cpp): doors, buttons and switches in front are used
 *    without a key, once per approach.
 * Menus get the finger as an absolute pointer (sdl3.c): the touch's mouse events place the menu cursor.
 */

#include "header/client.h"
#include "header/keyboard.h"
#include "header/sofclient.h"

#define TOUCH_FINGERS 10

enum
{
	R_NONE, R_JOY, R_LOOK, R_FIRE, R_ALT, R_JUMP, R_CROUCH, R_RELOAD, R_ITEM, R_WEAPON, R_MENU
};

typedef struct
{
	long long id;
	int role;
	float x0, y0, x, y; /* window pixels */
	float lx, ly;       /* last position the view turned from */
	int t0;             /* cls.realtime at the press */
	qboolean longdone;
} finger_t;

typedef struct
{
	int role;
	float cx, cy, r; /* window pixels */
	const char *label;
} tbutton_t;

static cvar_t *in_touch, *touch_sens, *touch_joyradius, *touch_alpha, *touch_size, *touch_invert;
static finger_t fingers[TOUCH_FINGERS];
static tbutton_t buttons[8];
static int numbuttons;
static qboolean crouched, firing;

static float
W(void)
{
	return (float)viddef.width;
}

static float
H(void)
{
	return (float)viddef.height;
}

void
Touch_Init(void)
{
#ifdef __ANDROID__
	in_touch = Cvar_Get("in_touch", "1", CVAR_ARCHIVE);
#else
	in_touch = Cvar_Get("in_touch", "0", CVAR_ARCHIVE);
#endif
	touch_sens = Cvar_Get("touch_sens", "1", CVAR_ARCHIVE);           /* 1 = half a turn across the screen */
	touch_joyradius = Cvar_Get("touch_joyradius", "0.13", CVAR_ARCHIVE); /* of the screen height */
	touch_alpha = Cvar_Get("touch_alpha", "0.35", CVAR_ARCHIVE);
	touch_size = Cvar_Get("touch_size", "1", CVAR_ARCHIVE);
	touch_invert = Cvar_Get("touch_invert", "0", CVAR_ARCHIVE);
	memset(fingers, 0, sizeof(fingers));
}

/* the touch controls are on and take the fingers: the game is running and has the keys */
qboolean
Touch_Enabled(void)
{
	return in_touch && in_touch->value;
}

qboolean
Touch_Active(void)
{
	return Touch_Enabled() && (cls.state == ca_active) && (cls.key_dest == key_game);
}

/* cl_hud.c: the trays go to the top of the screen while touch controls are on (the thumbs hold the corners) */
qboolean
Touch_HudTop(void)
{
	return Touch_Enabled();
}

static void
Layout(void)
{
	/* after the big mobile shooters (Call of Duty Mobile, PUBG Mobile): the buttons sit in a tight cluster at the right
	   edge and the bottom corner, the middle of the right half is free for turning the view - a look drag that
	   started there never lands on a button. Sizes in screen heights, touch_size scales them. */
	float h = H(), w = W(), k = Q_clamp(touch_size->value, 0.7f, 1.5f);
	tbutton_t *b = buttons;

	*b++ = (tbutton_t){R_FIRE, w - 0.15f * h, h - 0.40f * h, 0.090f * h * k, "FIRE"};
	*b++ = (tbutton_t){R_ALT, w - 0.10f * h, h - 0.64f * h, 0.055f * h * k, "ALT"};
	*b++ = (tbutton_t){R_JUMP, w - 0.10f * h, h - 0.12f * h, 0.065f * h * k, "JUMP"};
	*b++ = (tbutton_t){R_CROUCH, w - 0.29f * h, h - 0.09f * h, 0.060f * h * k, "DUCK"};
	*b++ = (tbutton_t){R_RELOAD, w - 0.46f * h, h - 0.08f * h, 0.050f * h * k, "R"};
	/* the menu between the item tray and the health bar */
	*b++ = (tbutton_t){R_MENU, w * 0.5f - 0.36f * h, 0.06f * h, 0.045f * h, "II"};
	numbuttons = (int)(b - buttons);
}

/* window pixel rectangle of a HUD tray (cl_hud.c), 0 = item, 1 = weapon */
qboolean HUD_TrayRect(int which, float *x, float *y, float *w, float *h);

static int
RoleAt(float x, float y)
{
	int i;
	float tx, ty, tw, th;

	for (i = 0; i < numbuttons; i++)
	{
		float dx = x - buttons[i].cx, dy = y - buttons[i].cy, r = buttons[i].r; /* the drawn circle exactly */

		if (dx * dx + dy * dy <= r * r)
		{
			return buttons[i].role;
		}
	}

	for (i = 0; i < 2; i++)
	{
		if (HUD_TrayRect(i, &tx, &ty, &tw, &th) && (x >= tx) && (x < tx + tw) && (y >= ty) && (y < ty + th))
		{
			return i ? R_WEAPON : R_ITEM;
		}
	}

	return (x < W() * 0.5f) ? R_JOY : R_LOOK;
}

static void
Press(int role, qboolean down)
{
	switch (role)
	{
		case R_FIRE:
			firing = down;
			Cbuf_AddText(down ? "+attack\n" : "-attack\n");
			break;
		case R_ALT:
			Cbuf_AddText(down ? "+altattack\n" : "-altattack\n");
			break;
		case R_JUMP:
			Cbuf_AddText(down ? "+moveup\n" : "-moveup\n");
			break;
		case R_CROUCH:
			if (down)
			{
				crouched = !crouched;
				Cbuf_AddText(crouched ? "+movedown\n" : "-movedown\n");
			}
			break;
		case R_RELOAD:
			if (down)
			{
				Cbuf_AddText("reload\n");
			}
			break;
		case R_MENU:
			if (down) /* press and release at once: the menu takes the keys before the finger lifts */
			{
				Key_Event(K_ESCAPE, true, true);
				Key_Event(K_ESCAPE, false, true);
			}
			break;
		default:
			break;
	}
}

/* all held buttons up (the controls lose the fingers: menu, console, disconnect) */
static void
ReleaseAll(void)
{
	int i;

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		if (fingers[i].role && (fingers[i].role != R_MENU))
		{
			Press(fingers[i].role, false);
		}

		fingers[i].role = R_NONE;
	}

	if (crouched)
	{
		crouched = false;
		Cbuf_AddText("-movedown\n");
	}

}

/* sdl3.c: a finger event, x / y normalized 0..1; type 0 down, 1 motion, 2 up */
void
Touch_Finger(long long id, int type, float nx, float ny)
{
	float x = nx * W(), y = ny * H();
	finger_t *f = NULL;
	int i;

	if (!Touch_Active())
	{
		return;
	}

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		if (fingers[i].role && (fingers[i].id == id))
		{
			f = &fingers[i];
			break;
		}
	}

	if (type == 0)
	{
		if (f)
		{
			return;
		}

		for (i = 0; i < TOUCH_FINGERS && fingers[i].role; i++)
		{
		}

		if (i == TOUCH_FINGERS)
		{
			return;
		}

		Layout();
		f = &fingers[i];
		memset(f, 0, sizeof(*f));
		f->id = id;
		f->x0 = f->x = f->lx = x;
		f->y0 = f->y = f->ly = y;
		f->t0 = cls.realtime;
		f->role = RoleAt(x, y);

		/* one joystick: a second finger on the left half turns the view */
		if (f->role == R_JOY)
		{
			for (i = 0; i < TOUCH_FINGERS; i++)
			{
				if ((&fingers[i] != f) && (fingers[i].role == R_JOY))
				{
					f->role = R_LOOK;
				}
			}
		}

		Press(f->role, true);
		return;
	}

	if (!f)
	{
		return;
	}

	f->x = x;
	f->y = y;

	if (type == 2)
	{
		/* the trays: a tap selects, a long press (handled while held) uses / goes back */
		if (!f->longdone && (f->role == R_ITEM))
		{
			Cbuf_AddText("itemnext\n");
		}
		else if (!f->longdone && (f->role == R_WEAPON))
		{
			Cbuf_AddText("weapnext\n");
		}

		Press(f->role, false);
		f->role = R_NONE;
	}
}

/* the view turns by the movement of the look fingers and of the fire button's finger */
static void
Look(void)
{
	/* touch_sens 1: a drag across the whole width turns 180 degrees */
	float k = 180.0f / W() * (touch_sens ? touch_sens->value : 1.0f);
	int i;

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		finger_t *f = &fingers[i];

		if ((f->role != R_LOOK) && (f->role != R_FIRE))
		{
			continue;
		}

		cl.viewangles[YAW] -= (f->x - f->lx) * k;
		cl.viewangles[PITCH] += (f->y - f->ly) * k * ((touch_invert && touch_invert->value) ? -1.0f : 1.0f);
		f->lx = f->x;
		f->ly = f->y;
	}

	if (cl.viewangles[PITCH] > 89)
	{
		cl.viewangles[PITCH] = 89;
	}
	else if (cl.viewangles[PITCH] < -89)
	{
		cl.viewangles[PITCH] = -89;
	}
}

/* in_main / sdl3.c IN_Move: the joystick moves, the view turns */
void
Touch_Move(struct usercmd_s *cmd)
{
	int i;
	static qboolean was;

	if (!Touch_Active())
	{
		if (was)
		{
			ReleaseAll();
			was = false;
		}

		return;
	}

	was = true;
	Layout();
	Look();

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		finger_t *f = &fingers[i];

		if (f->role == R_JOY)
		{
			float r = touch_joyradius->value * H();
			float dx = (f->x - f->x0) / r, dy = (f->y - f->y0) / r, len = sqrtf(dx * dx + dy * dy);

			if (len > 1.0f)
			{
				/* the anchor follows the thumb past the rim, so turning back reacts at once */
				f->x0 = f->x - dx / len * r;
				f->y0 = f->y - dy / len * r;
				dx /= len;
				dy /= len;
			}

			if (len > 0.12f) /* dead zone */
			{
				cmd->forwardmove -= (short)(dy * cl_forwardspeed->value * 2.0f);
				cmd->sidemove += (short)(dx * cl_sidespeed->value * 2.0f);
			}
		}
		else if (((f->role == R_ITEM) || (f->role == R_WEAPON)) && !f->longdone && (cls.realtime - f->t0 > 450))
		{
			f->longdone = true;
			Cbuf_AddText((f->role == R_ITEM) ? "itemuse\n" : "weapprev\n");
		}
	}

	/* automatic use: the game does it (om_autouse, game/src/g_utils.cpp G_AutoUse) with the use key's own rules */
}

static void
Ring(float cx, float cy, float r, float width, const unsigned char *c)
{
	int i;
	const int n = 28;

	for (i = 0; i < n; i++)
	{
		float a0 = (float)i / n * 2.0f * (float)M_PI, a1 = (float)(i + 1) / n * 2.0f * (float)M_PI;

		HUD_DrawLine(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, width, c);
	}
}

static void
Disc(float cx, float cy, float r, const unsigned char *c)
{
	float y;

	for (y = -r; y < r; y += 2.0f)
	{
		float hw = sqrtf(r * r - (y + 1.0f) * (y + 1.0f));

		HUD_DrawFill((int)(cx - hw), (int)(cy + y), (int)(2.0f * hw), 2, c);
	}
}

static qboolean
Held(int role)
{
	int i;

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		if (fingers[i].role == role)
		{
			return true;
		}
	}

	return false;
}

/* cl_sofscreen.c: the controls over the view, in window pixels */
void
Touch_Draw(void)
{
	unsigned char line[4] = {255, 255, 255, 0}, fill[4] = {0, 0, 0, 0}, lit[4] = {120, 255, 120, 0};
	float a;
	int i;

	if (!Touch_Active())
	{
		return;
	}

	a = Q_clamp(touch_alpha->value, 0.05f, 1.0f);
	line[3] = (byte)(a * 255);
	fill[3] = (byte)(a * 0.5f * 255);
	lit[3] = (byte)(Q_min(1.0f, a * 1.6f) * 255);
	Layout();

	for (i = 0; i < numbuttons; i++)
	{
		const tbutton_t *b = &buttons[i];
		qboolean on = Held(b->role) || ((b->role == R_CROUCH) && crouched);
		int tw = HUD_TextWidth(b->label, 2), th = HUD_FontHeight(2);

		Disc(b->cx, b->cy, b->r, fill);
		Ring(b->cx, b->cy, b->r, Q_max(2.0f, H() / 360.0f), on ? lit : line);
		HUD_DrawFontText((int)(b->cx - tw / 2), (int)(b->cy - th / 2), b->label, on ? lit : line, 2, 0);
	}

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		const finger_t *f = &fingers[i];

		if (f->role == R_JOY)
		{
			float r = touch_joyradius->value * H();

			Ring(f->x0, f->y0, r, Q_max(2.0f, H() / 360.0f), line);
			Disc(f->x, f->y, r * 0.35f, lit);
		}
	}
}
