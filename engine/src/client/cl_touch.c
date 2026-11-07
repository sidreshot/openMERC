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
 *    without a key, once per approach;
 *  - the item button on the left (touch_leftitem; touch_leftitem 0 + touch_leftfire 1 puts a second fire button
 *    there instead): it uses the item selected in the item tray and shows its name and count;
 *  - with a grenade selected the item button is held to aim - the arc of the throw and
 *    the blast circle where it ends are drawn, the finger keeps turning the view - and release to throw; releasing on
 *    the CANCEL circle (where USE sits) or tapping it with another finger keeps the grenade;
 *  - the crosshair of the touch controls (touch_crosshair): four ticks whose gap follows movement and firing, red over
 *    a hostile character, a hit marker on every hit and a red one on a kill (game STAT_FLAGS 0x1000/0x2000/0x4000);
 *    the turning slows down over a hostile (touch_aimslow) and touch_autofire fires on its own while one is there.
 * Menus get the finger as an absolute pointer (sdl3.c): the touch's mouse events place the menu cursor.
 */

#include "header/client.h"
#include "header/keyboard.h"
#include "header/sofclient.h"

#define TOUCH_FINGERS 10

enum
{
	R_NONE, R_JOY, R_LOOK, R_FIRE, R_ALT, R_JUMP, R_CROUCH, R_RELOAD, R_ITEM, R_WEAPON, R_MENU, R_USE, R_GRENADE,
	R_LFIRE, R_CANCEL, R_INV
};

/* the selected inventory item (game STAT_ITEMTYPE 0, STAT_ITEMCOUNT 7; player_api.h IT_*) */
#define STAT_ITEMTYPE 0
#define STAT_ITEMCOUNT 7
#define IT_FLASHPACK 1
#define IT_GRENADE 6

/* game STAT_FLAGS bits of OpenMerc (game/src/p_hud.cpp) and the grenade count (stats 13) */
#define OMF_HOSTILE 0x1000
#define OMF_HIT 0x2000
#define OMF_KILL 0x4000
#define STAT_OM_GRENADES 13

typedef struct
{
	long long id;
	int role;
	float x0, y0, x, y; /* window pixels */
	float lx, ly;       /* last position the view turned from */
	int t0;             /* cls.realtime at the press */
	qboolean longdone;
	qboolean cancelled; /* a grenade aim given up */
	int item;           /* the item being aimed (IT_GRENADE, IT_FLASHPACK) */
} finger_t;

typedef struct
{
	int role;
	float cx, cy, r; /* window pixels */
	const char *label;
} tbutton_t;

static cvar_t *in_touch, *touch_sens, *touch_joyradius, *touch_alpha, *touch_size, *touch_invert;
static finger_t fingers[TOUCH_FINGERS];
static cvar_t *touch_crosshair, *touch_aimslow, *touch_autofire, *touch_leftfire, *touch_leftitem, *touch_autoaim;
static tbutton_t buttons[12];
static int numbuttons;
static qboolean crouched, firing, autofiring;
static int fireholds;        /* fingers and the automatic fire holding +attack */
static int autofire_last;    /* cls.realtime a hostile was last under the crosshair */

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

/* touch_fake <id> <0 down | 1 move | 2 up> <x> <y>: a finger event from the console (0..1 of the window), for
   scripted tests of the touch controls on a desktop */
static void
Touch_Fake_f(void)
{
	if (Cmd_Argc() < 5)
	{
		Com_Printf("touch_fake <id> <0 down|1 move|2 up> <x 0..1> <y 0..1>\n");
		return;
	}

	Touch_Finger(atoi(Cmd_Argv(1)) + 1000, atoi(Cmd_Argv(2)), (float)atof(Cmd_Argv(3)), (float)atof(Cmd_Argv(4)));
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
	touch_crosshair = Cvar_Get("touch_crosshair", "1", CVAR_ARCHIVE);
	touch_aimslow = Cvar_Get("touch_aimslow", "0.55", CVAR_ARCHIVE); /* turning over a hostile, 1 = no slowdown */
	touch_autofire = Cvar_Get("touch_autofire", "0", CVAR_ARCHIVE);
	touch_leftfire = Cvar_Get("touch_leftfire", "1", CVAR_ARCHIVE);
	/* the left button uses the selected inventory item (1, the default) or fires (0 and touch_leftfire 1) */
	touch_leftitem = Cvar_Get("touch_leftitem", "1", CVAR_ARCHIVE);
	touch_autoaim = Cvar_Get("touch_autoaim", "0.5", CVAR_ARCHIVE); /* auto aim strength, 0 off */
	memset(fingers, 0, sizeof(fingers));
	Cmd_AddCommand("touch_fake", Touch_Fake_f);
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

/* looking through a remote camera (security monitors): the player stands at the terminal */
static qboolean
Remote(void)
{
	const sofps_t *ps = CL_SoF_CurrentPS();

	return ps && ps->remote_type != 0;
}

static int
Stat(int i)
{
	const sofps_t *ps = CL_SoF_CurrentPS();

	return ps ? ps->stats[i] : 0;
}

static qboolean
Held(int role);

static qboolean
GrenadeAiming(void);

/* the item button's label: the selected item (player_api.h IT_*) */
static const char *
ItemLabel(void)
{
	static const char *const names[] = {"ITEM", "FLASH", "C4", "GOGGLES", "MINE", "MEDKIT", "GREN", "FLAG"};
	int t = Stat(STAT_ITEMTYPE);

	return (t > 0 && t < (int)(sizeof(names) / sizeof(names[0]))) ? names[t] : names[0];
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
	/* the use key by hand (talk, a terminal again; automatic use covers doors and switches); in a remote camera view
	   it is the way out */
	/* while a grenade is aimed the USE circle is its CANCEL */
	if (GrenadeAiming())
	{
		*b++ = (tbutton_t){R_CANCEL, w - 0.31f * h, h - 0.62f * h, 0.060f * h * k, "CANCEL"};
	}
	else
	{
		*b++ = (tbutton_t){R_USE, w - 0.31f * h, h - 0.62f * h, 0.050f * h * k, Remote() ? "EXIT" : "USE"};
	}

	/* the left button: the selected inventory item (the item tray at the top selects it) - a grenade is held to aim
	   (the arc of the throw is drawn) and thrown on release, anything else is used at the press; shown while an item
	   is selected or a grenade is being aimed */
	if (touch_leftitem->value)
	{
		if ((Stat(STAT_ITEMTYPE) > 0 && Stat(STAT_ITEMCOUNT) > 0 && !Remote()) || Held(R_GRENADE) || Held(R_INV))
		{
			*b++ = (tbutton_t){R_INV, 0.14f * h, h - 0.62f * h, 0.065f * h * k, ItemLabel()};
		}
	}
	/* or the left fire button: firing while the right thumb aims (the big mobile shooters have one) */
	else if (touch_leftfire->value && !Remote())
	{
		*b++ = (tbutton_t){R_LFIRE, 0.14f * h, h - 0.62f * h, 0.065f * h * k, "FIRE"};
	}
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

/* +attack while any fire finger or the automatic fire holds it */
static void
FireHold(qboolean down)
{
	if (down)
	{
		if (fireholds++ == 0)
		{
			Cbuf_AddText("+attack\n");
		}
	}
	else if (fireholds > 0)
	{
		if (--fireholds == 0)
		{
			Cbuf_AddText("-attack\n");
		}
	}

	firing = fireholds > 0;
}

static void
Press(int role, qboolean down)
{
	switch (role)
	{
		case R_FIRE:
		case R_LFIRE:
			FireHold(down);
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
		case R_USE:
			Cbuf_AddText(down ? "+use\n" : "-use\n");
			break;
		case R_RELOAD:
			if (down)
			{
				Cbuf_AddText("reload\n");
			}
			break;
		case R_INV:
			if (down)
			{
				Cbuf_AddText("itemuse\n");
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

	if (fireholds > 0)
	{
		Cbuf_AddText("-attack\n");
	}

	autofiring = false;
	fireholds = 0;
	firing = false;

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

		/* the item button with a grenade or a flash pack selected: the finger aims the throw */
		if ((f->role == R_INV) && ((Stat(STAT_ITEMTYPE) == IT_GRENADE) || (Stat(STAT_ITEMTYPE) == IT_FLASHPACK)))
		{
			f->role = R_GRENADE;
			f->item = Stat(STAT_ITEMTYPE);
		}

		/* another finger on CANCEL gives the grenade aim up */
		if (f->role == R_CANCEL)
		{
			for (i = 0; i < TOUCH_FINGERS; i++)
			{
				if (fingers[i].role == R_GRENADE)
				{
					fingers[i].cancelled = true;
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

		/* the grenade: thrown where the finger lifts, unless the aim was given up or the finger lifts on CANCEL */
		if (f->role == R_GRENADE)
		{
			float cx = W() - 0.31f * H(), cy = H() - 0.62f * H(), r = 0.075f * H();

			if (!f->cancelled && ((f->x - cx) * (f->x - cx) + (f->y - cy) * (f->y - cy) > r * r))
			{
				if (f->item == IT_FLASHPACK)
				{
					Cbuf_AddText("itemuse fpak\n");
				}
				else if (Stat(STAT_OM_GRENADES) > 0)
				{
					Cbuf_AddText("itemuse grenade\n");
				}
			}
		}

		/* a remote camera view: a short tap anywhere leaves it (the use key at the terminal) */
		if (((f->role == R_LOOK) || (f->role == R_JOY)) && Remote() && (cls.realtime - f->t0 < 350) &&
			(fabsf(f->x - f->x0) + fabsf(f->y - f->y0) < 0.04f * H()))
		{
			Cbuf_AddText("+use\n-use\n");
		}

		Press(f->role, false);
		f->role = R_NONE;
	}
}

/* touch_autoaim (0..1): the auto aim of console and mobile shooters ("rotational assist") - while a finger moves or
   turns the player, the view is turned towards the hostile character the game picked near the crosshair (stats 14,
   game/src/p_hud.cpp OM_AimTarget: om_autoaim_angle degrees around it, in sight), at its chest as it moves now
   (the client's interpolated entity). The pull approaches the target exponentially, 1 = fast; nothing while the
   thumbs rest, so the view never drifts on its own */
static void
AutoAim(void)
{
	static int last;
	int s = Stat(14) & 0xFFFF, num = (s & 0x7FF) - 1, i;
	float dt = Q_clamp((cls.realtime - last) / 1000.0f, 0.0f, 0.1f), k;
	qboolean steering = false;
	const centity_t *c;
	vec3_t pos, d;
	float want[2], diff[2];

	last = cls.realtime;

	if ((num <= 0) || (num >= MAX_EDICTS) || (touch_autoaim->value <= 0) || Remote() || GrenadeAiming())
	{
		return;
	}

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		if ((fingers[i].role == R_LOOK) || (fingers[i].role == R_FIRE) || (fingers[i].role == R_JOY))
		{
			steering = true;
		}
	}

	c = &cl_entities[num];

	if (!steering || (c->serverframe != cl.frame.serverframe))
	{
		return;
	}

	for (i = 0; i < 3; i++)
	{
		pos[i] = c->prev.origin[i] + cl.lerpfrac * (c->current.origin[i] - c->prev.origin[i]);
	}

	pos[2] += ((s >> 11) & 31) * 2.0f - 32.0f;
	VectorSubtract(pos, cl.refdef.vieworg, d);
	want[0] = -atan2f(d[2], sqrtf(d[0] * d[0] + d[1] * d[1])) * (180.0f / (float)M_PI); /* pitch, down positive */
	want[1] = atan2f(d[1], d[0]) * (180.0f / (float)M_PI);
	k = 1.0f - expf(-dt * 10.0f * Q_clamp(touch_autoaim->value, 0.0f, 1.0f));

	for (i = 0; i < 2; i++)
	{
		/* against the view as drawn: cl.viewangles lack the server's delta_angles (a teleport, a spawn) */
		diff[i] = fmodf(want[i] - cl.refdef.viewangles[i] + 540.0f, 360.0f) - 180.0f;
		cl.viewangles[i] += diff[i] * k;
	}
}

/* the view turns by the movement of the look fingers and of the fire button's finger */
static void
Look(void)
{
	/* touch_sens 1: a drag across the whole width turns 180 degrees */
	float k = 180.0f / W() * (touch_sens ? touch_sens->value : 1.0f);
	int i;

	/* aim slowdown over a hostile character (the "friction" of console and mobile shooters): fine corrections stay on
	   the target, a fast swipe still leaves it */
	if (Stat(8) & OMF_HOSTILE)
	{
		k *= Q_clamp(touch_aimslow->value, 0.2f, 1.0f);
	}

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		finger_t *f = &fingers[i];

		if ((f->role != R_LOOK) && (f->role != R_FIRE) && (f->role != R_GRENADE))
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
	AutoAim();

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

	/* automatic fire (touch_autofire): while a hostile is under the crosshair, and 0.15 s after (a burst does not stop
	   on the frame the target twitches out of the line) */
	{
		qboolean want = false;

		if (touch_autofire->value && Stat(6) && !Remote() && !GrenadeAiming())
		{
			if (Stat(8) & OMF_HOSTILE)
			{
				autofire_last = cls.realtime;
			}

			want = autofire_last && (cls.realtime - autofire_last < 150);
		}

		if (want != autofiring)
		{
			autofiring = want;
			FireHold(want);
		}
	}
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
GrenadeAiming(void)
{
	int i;

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		if ((fingers[i].role == R_GRENADE) && !fingers[i].cancelled)
		{
			return true;
		}
	}

	return false;
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

/* a world point on the screen (window pixels); false behind the eye */
static qboolean
Project(const vec3_t p, float *sx, float *sy)
{
	vec3_t d, f, r, u;
	float z, tx, ty;

	AngleVectors(cl.refdef.viewangles, f, r, u);
	VectorSubtract(p, cl.refdef.vieworg, d);
	z = DotProduct(d, f);

	if (z < 4.0f)
	{
		return false;
	}

	tx = tanf(cl.refdef.fov_x * (float)M_PI / 360.0f);
	ty = tanf(cl.refdef.fov_y * (float)M_PI / 360.0f);

	if ((tx <= 0) || (ty <= 0))
	{
		return false;
	}

	*sx = (0.5f + 0.5f * DotProduct(d, r) / z / tx) * W(); /* the view is the whole window in SoF mode */
	*sy = (0.5f - 0.5f * DotProduct(d, u) / z / ty) * H();
	return true;
}

/* the flight of a thrown grenade as the game runs it (game/src/g_weapon.cpp WeaponUseGrenade with om_throwprecise,
   g_phys.cpp SV_Physics_Dan): from 20 units under the eye at 475 forward + 325 up + the player's velocity, gravity
   while airborne, 10 Hz steps, a bounce keeps half the velocity (normal part 1.8), on the ground 42 units/s of
   friction per step; it goes off after 2 s. Fills pts (up to max), returns the count; *rest is where it ends */
#define ARC_MAX 64

static int
GrenadeFlight(vec3_t *pts, int max, int item)
{
	static const vec3_t mins = {-2, -2, -3}, maxs = {2, 2, 3};
	vec3_t f, r, u, pos, vel;
	float g = cl.frame.playerstate.pmove.gravity > 0 ? cl.frame.playerstate.pmove.gravity : 800.0f;
	/* a flash pack (WeaponUseFlashpack): from the eye at 400 forward + 300 up (the middle of 200..400), without the
	   player's velocity, a bounce keeps 0.7, it goes off after 1.5 s */
	qboolean flash = item == IT_FLASHPACK, ground = false;
	float keep = flash ? 0.7f : 0.5f;
	int n = 0, step, i, steps = flash ? 15 : 20;

	AngleVectors(cl.refdef.viewangles, f, r, u);
	VectorCopy(cl.refdef.vieworg, pos);
	pos[2] -= flash ? 0 : 20;

	for (i = 0; i < 3; i++)
	{
		vel[i] = flash ? (f[i] * 400.0f + u[i] * 300.0f) :
			(f[i] * 475.0f + u[i] * 325.0f + cl.frame.playerstate.pmove.velocity[i] * 0.125f);
	}

	VectorCopy(pos, pts[n]);
	n++;

	for (step = 0; step < steps && n < max; step++)
	{
		float left = 0.1f;
		int bump;

		if (!ground)
		{
			vel[2] -= g * 0.1f;
		}

		for (bump = 0; bump < 4 && left > 0.0f; bump++)
		{
			vec3_t end;
			trace_t tr;
			float vn;

			VectorMA(pos, left, vel, end);
			tr = CM_BoxTrace(pos, end, mins, maxs, 0, MASK_SOLID);

			if (tr.allsolid)
			{
				VectorClear(vel);
				break;
			}

			VectorCopy(tr.endpos, pos);

			if (tr.fraction >= 1.0f)
			{
				break;
			}

			if (tr.plane.normal[2] > 0.7f)
			{
				ground = true;
			}

			vn = DotProduct(vel, tr.plane.normal);

			for (i = 0; i < 3; i++)
			{
				vel[i] = keep * (vel[i] - 1.8f * vn * tr.plane.normal[i]);
			}

			if (fabsf(vel[0]) < 1)
			{
				vel[0] = 0;
			}

			if (fabsf(vel[1]) < 1)
			{
				vel[1] = 0;
			}

			if (fabsf(vel[2]) < 100)
			{
				vel[2] = 0;
			}

			left -= left * tr.fraction;

			if (n < max)
			{
				VectorCopy(pos, pts[n]);
				n++;
			}
		}

		/* still on the ground? */
		if (ground)
		{
			vec3_t below;
			trace_t tr;

			VectorCopy(pos, below);
			below[2] -= 0.5f;
			tr = CM_BoxTrace(pos, below, mins, maxs, 0, MASK_SOLID);
			ground = (tr.fraction < 1.0f) && (tr.plane.normal[2] > 0.7f) && (vel[2] <= 0);

			if (ground)
			{
				for (i = 0; i < 3; i++)
				{
					vel[i] = (vel[i] > 0) ? Q_max(0.0f, vel[i] - 42.0f) : Q_min(0.0f, vel[i] + 42.0f);
				}
			}
		}

		if (n < max)
		{
			VectorCopy(pos, pts[n]);
			n++;
		}
	}

	return n;
}

/* while the grenade button is held: the arc (dots fading with the distance) and the blast circle (200 units, the
   grenade's damage radius) on the ground where it goes off */
static void
GrenadeArc(void)
{
	static vec3_t pts[ARC_MAX];
	unsigned char arc[4] = {255, 255, 255, 220}, ring[4] = {255, 120, 60, 120}, core[4] = {255, 60, 40, 255};
	float lw = Q_max(2.0f, H() / 300.0f), px = 0, py = 0;
	qboolean had = false;
	int n, i, item = IT_GRENADE;

	if (!GrenadeAiming())
	{
		return;
	}

	for (i = 0; i < TOUCH_FINGERS; i++)
	{
		if ((fingers[i].role == R_GRENADE) && !fingers[i].cancelled)
		{
			item = fingers[i].item;
		}
	}

	n = GrenadeFlight(pts, ARC_MAX, item);

	for (i = 0; i < n; i++)
	{
		float x, y;

		if (Project(pts[i], &x, &y))
		{
			if (had)
			{
				HUD_DrawLine(px, py, x, y, lw, arc);
			}

			px = x;
			py = y;
			had = true;
		}
		else
		{
			had = false;
		}
	}

	/* the blast circle and its centre, on the ground plane */
	{
		const vec3_t *end = &pts[n - 1];
		float rr[2] = {(item == IT_FLASHPACK) ? 100.0f : 200.0f, 24.0f}; /* the damage radius */
		int k;

		for (k = 0; k < 2; k++)
		{
			const int segs = 32;
			had = false;

			for (i = 0; i <= segs; i++)
			{
				float ang = (float)i / segs * 2.0f * (float)M_PI, x, y;
				vec3_t q;

				q[0] = (*end)[0] + cosf(ang) * rr[k];
				q[1] = (*end)[1] + sinf(ang) * rr[k];
				q[2] = (*end)[2] - 2.0f;

				if (Project(q, &x, &y))
				{
					if (had)
					{
						HUD_DrawLine(px, py, x, y, k ? lw * 1.5f : lw, k ? core : ring);
					}

					px = x;
					py = y;
					had = true;
				}
				else
				{
					had = false;
				}
			}
		}
	}
}

/* the crosshair of the touch controls, in window pixels: four ticks around a dot. The gap opens with the player's
   speed and while firing (the weapon's spread grows the same way), the ticks turn red over a hostile character; a
   hit marker (four diagonal strokes) flashes on a hit, a bigger red one on a kill. Drawn by HUD_DrawCrosshair. */
static float xgap, bloom;
static int hitstart, killstart, lastflags;

/* HUD_DrawCrosshair (cl_hud.c): the touch controls have their own crosshair, drawn with the controls in window
   pixels (DrawCrosshair) - the classic one stays off */
qboolean
Touch_DrawCrosshair(void)
{
	return Touch_Enabled() && touch_crosshair && touch_crosshair->value;
}

static void
DrawCrosshair(void)
{
	const sofps_t *ps = CL_SoF_CurrentPS();
	unsigned char col[4] = {255, 255, 255, 230}, dark[4] = {0, 0, 0, 150}, red[4] = {255, 50, 40, 255};
	float h = H(), cx = W() * 0.5f, cy = H() * 0.5f;
	float speed, target, len, th, dt;
	int flags, i;

	if (!touch_crosshair->value || !ps)
	{
		return;
	}

	flags = ps->stats[8];

	/* no crosshair: scoped, blinded, no weapon, a remote camera view */
	if ((flags & 0x1) || (flags & 0x800) || !ps->stats[6] || ps->remote_type)
	{
		lastflags = flags;
		return;
	}

	/* rising edges of the hit / kill bits */
	if ((flags & OMF_HIT) && !(lastflags & OMF_HIT))
	{
		hitstart = cls.realtime;
	}

	if ((flags & OMF_KILL) && !(lastflags & OMF_KILL))
	{
		killstart = cls.realtime;
	}

	lastflags = flags;

	dt = Q_clamp(cls.rframetime, 0.0f, 0.1f);
	speed = sqrtf((float)cl.frame.playerstate.pmove.velocity[0] * cl.frame.playerstate.pmove.velocity[0] +
		(float)cl.frame.playerstate.pmove.velocity[1] * cl.frame.playerstate.pmove.velocity[1]) * 0.125f;
	bloom = firing ? Q_min(1.0f, bloom + dt * 3.0f) : Q_max(0.0f, bloom - dt * 2.5f);
	target = h * (0.010f + 0.016f * Q_clamp(speed / 300.0f, 0.0f, 1.0f) + 0.018f * bloom);
	xgap += (target - xgap) * Q_min(1.0f, dt * 14.0f);

	if (flags & OMF_HOSTILE)
	{
		memcpy(col, red, 4);
	}

	len = h * 0.016f;
	th = Q_max(2.0f, h / 360.0f);

	/* the knife: the dot alone */
	if (ps->stats[6] != 1)
	{
		static const float dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

		for (i = 0; i < 4; i++)
		{
			float x0 = cx + dir[i][0] * xgap, y0 = cy + dir[i][1] * xgap;
			float x1 = cx + dir[i][0] * (xgap + len), y1 = cy + dir[i][1] * (xgap + len);

			HUD_DrawLine(x0, y0, x1, y1, th + 2.0f, dark);
			HUD_DrawLine(x0, y0, x1, y1, th, col);
		}
	}

	HUD_DrawFill((int)(cx - th * 0.5f - 1), (int)(cy - th * 0.5f - 1), (int)th + 2, (int)th + 2, dark);
	HUD_DrawFill((int)(cx - th * 0.5f), (int)(cy - th * 0.5f), (int)th, (int)th, col);

	/* the hit marker */
	{
		int kill = cls.realtime - killstart < 400, hit = cls.realtime - hitstart < 200;

		if (kill || hit)
		{
			float r0 = h * (kill ? 0.016f : 0.012f), r1 = h * (kill ? 0.036f : 0.026f);
			const unsigned char *mc = kill ? red : (const unsigned char *)"\xff\xff\xff\xff";

			for (i = 0; i < 4; i++)
			{
				float sx = (i & 1) ? 1.0f : -1.0f, sy = (i & 2) ? 1.0f : -1.0f;

				HUD_DrawLine(cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1, th + 2.0f, dark);
				HUD_DrawLine(cx + sx * r0, cy + sy * r0, cx + sx * r1, cy + sy * r1, th * (kill ? 1.5f : 1.0f), mc);
			}
		}
	}
}

/* cl_sofscreen.c: the controls over the view, in window pixels */
void
Touch_Draw(void)
{
	unsigned char line[4] = {255, 255, 255, 0}, fill[4] = {0, 0, 0, 0}, lit[4] = {120, 255, 120, 0};
	unsigned char warn[4] = {255, 90, 70, 230}, warnfill[4] = {120, 0, 0, 110};
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
	DrawCrosshair();

	for (i = 0; i < numbuttons; i++)
	{
		const tbutton_t *b = &buttons[i];
		qboolean on = Held(b->role) || ((b->role == R_CROUCH) && crouched) || ((b->role == R_INV) && Held(R_GRENADE));
		int tw = HUD_TextWidth(b->label, 2), th = HUD_FontHeight(2);

		Disc(b->cx, b->cy, b->r, (b->role == R_CANCEL) ? warnfill : fill);
		Ring(b->cx, b->cy, b->r, Q_max(2.0f, H() / 360.0f), (b->role == R_CANCEL) ? warn : (on ? lit : line));
		HUD_DrawFontText((int)(b->cx - tw / 2), (int)(b->cy - th / 2), b->label,
			(b->role == R_CANCEL) ? warn : (on ? lit : line), 2, 0);

		/* how many of the selected item are left, under the label */
		if (b->role == R_INV)
		{
			char n[8];

			Com_sprintf(n, sizeof(n), "%d", Stat(STAT_ITEMCOUNT));
			HUD_DrawFontText((int)(b->cx - HUD_TextWidth(n, 2) / 2), (int)(b->cy + th * 0.6f), n, on ? lit : line, 2, 0);
		}
	}

	GrenadeArc();

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
