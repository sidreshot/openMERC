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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * This file implements the input handling like mouse events and
 * keyboard strokes.
 *
 * =======================================================================
 *
 * OpenMerc: Soldier of Fortune 1.07f button commands (held flag, +altattack,
 * +leanleft/+leanright, +weaponExtra1/2, +use as lean modifier and skip) and
 * the user command: one command per packet frame, movement truncated per
 * button, no run doubling, lean, run / use-skip / any-key bits, diagonal
 * correction, cinematic freeze, r_lightlevel; force-feedback commands
 * without a device. The structure is Quake II's client/cl_input.c.
 *
 * Implements: SPEC-312 (sections 7, 10, 11, 12), SPEC-202 section 6.1,
 *             SPEC-310 section 14 (pending weapon command)
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "input/header/input.h"

/* SoF user command buttons (SPEC-202 6.1) */
#define BUTTON_ALTATTACK 0x04
#define BUTTON_WEAPONEXTRA1 0x08
#define BUTTON_WEAPONEXTRA2 0x10
#define BUTTON_RUN 0x20
#define BUTTON_SKIP 0x40

static cvar_t *cl_nodelta;
static cvar_t *cl_pitchdebug;
static cvar_t *lookspring;
static cvar_t *console_cvar;

static unsigned frame_msec;
static unsigned old_sys_frame_time;

/*
 * KEY BUTTONS
 *
 * Continuous button event tracking is complicated by the fact that two different
 * input sources (say, mouse button 1 and the control key) can both press the
 * same button, but the button should only be released when both of the
 * pressing key have been released.
 *
 * When a key event issues a button command (+forward, +attack, etc), it appends
 * its key number as a parameter to the command so it can be matched up with
 * the release.
 *
 * state bit 0 is the current state of the key
 * state bit 1 is edge triggered on the up to down transition
 * state bit 2 is edge triggered on the down to up transition
 *
 * SoF adds a "held" flag: set when a key presses the button, cleared when
 * its last holding key is released by a key event (SPEC-312 section 7).
 */

typedef struct
{
	kbutton_t k;
	qboolean held;
} button_t;

static button_t in_left, in_right, in_forward, in_back;
static button_t in_lookup, in_lookdown, in_moveleft, in_moveright;
static button_t in_up, in_down;
static button_t in_klook, in_speed, in_use, in_attack, in_strafe_b;
static button_t in_altattack, in_leanleft, in_leanright, in_wextra1, in_wextra2;

/* Quake II name of the strafe button for code that still reads it */
kbutton_t in_strafe;

static qboolean skip_requested; /* cinematic skip (SPEC-312 section 11) */

/* movement produced by mouse / joystick readings since the last command */
static usercmd_t mouse_accum;

static void
KeyDown(button_t *bt)
{
	kbutton_t *b = &bt->k;
	int k;
	char *c;

	c = Cmd_Argv(1);

	if (c[0])
	{
		k = (int)strtol(c, (char **)NULL, 10);
	}
	else
	{
		k = -1; /* typed manually at the console for continuous down */
	}

	if ((k == b->down[0]) || (k == b->down[1]))
	{
		return; /* repeating key */
	}

	if (!b->down[0])
	{
		b->down[0] = k;
	}
	else if (!b->down[1])
	{
		b->down[1] = k;
	}
	else
	{
		Com_Printf("Three keys down for a button!\n");
		return;
	}

	bt->held = true;

	if (b->state & 1)
	{
		return; /* still down */
	}

	/* save timestamp */
	c = Cmd_Argv(2);
	b->downtime = (int)strtol(c, (char **)NULL, 10);

	if (!b->downtime)
	{
		b->downtime = sys_frame_time - 100;
	}

	b->state |= 1 + 2; /* down + impulse down */
}

static void
KeyUp(button_t *bt)
{
	kbutton_t *b = &bt->k;
	int k;
	char *c;
	unsigned uptime;

	c = Cmd_Argv(1);

	if (c[0])
	{
		k = (int)strtol(c, (char **)NULL, 10);
	}
	else
	{
		/* typed manually at the console, assume for unsticking, so clear
		   all; the held flag stays (SPEC-312 section 7) */
		b->down[0] = b->down[1] = 0;
		b->state = 4; /* impulse up */
		return;
	}

	if (b->down[0] == k)
	{
		b->down[0] = 0;
	}
	else if (b->down[1] == k)
	{
		b->down[1] = 0;
	}
	else
	{
		return; /* key up without coresponding down (menu pass through) */
	}

	if (b->down[0] || b->down[1])
	{
		return; /* some other key is still holding it down */
	}

	bt->held = false;

	if (!(b->state & 1))
	{
		return; /* still up (this should not happen) */
	}

	/* save timestamp */
	c = Cmd_Argv(2);
	uptime = (int)strtol(c, (char **)NULL, 10);

	if (uptime)
	{
		b->msec += uptime - b->downtime;
	}
	else
	{
		b->msec += 10;
	}

	b->state &= ~1; /* now up */
	b->state |= 4; /* impulse up */
}

#define BUTTON_FUNCS(name, var) \
	static void IN_##name##Down(void) { KeyDown(&var); } \
	static void IN_##name##Up(void) { KeyUp(&var); }

BUTTON_FUNCS(Up, in_up)
BUTTON_FUNCS(Down, in_down)
BUTTON_FUNCS(Left, in_left)
BUTTON_FUNCS(Right, in_right)
BUTTON_FUNCS(Forward, in_forward)
BUTTON_FUNCS(Back, in_back)
BUTTON_FUNCS(Lookup, in_lookup)
BUTTON_FUNCS(Lookdown, in_lookdown)
BUTTON_FUNCS(Moveleft, in_moveleft)
BUTTON_FUNCS(Moveright, in_moveright)
BUTTON_FUNCS(Speed, in_speed)
BUTTON_FUNCS(Use, in_use)
BUTTON_FUNCS(LeanLeft, in_leanleft)
BUTTON_FUNCS(LeanRight, in_leanright)
BUTTON_FUNCS(WeaponExtra1, in_wextra1)
BUTTON_FUNCS(WeaponExtra2, in_wextra2)

static void
IN_StrafeDown(void)
{
	KeyDown(&in_strafe_b);
	in_strafe = in_strafe_b.k;
}

static void
IN_StrafeUp(void)
{
	KeyUp(&in_strafe_b);
	in_strafe = in_strafe_b.k;
}

static void
IN_KLookDown(void)
{
	KeyDown(&in_klook);
}

static void
IN_KLookUp(void)
{
	KeyUp(&in_klook);

	/* SoF recentres on +klook release too (SPEC-312 section 7) */
	if (!(in_klook.k.state & 1) && !freelook->value && lookspring->value)
	{
		IN_CenterView();
	}
}

static void
IN_AttackDown(void)
{
	KeyDown(&in_attack);
}

static void
IN_AttackUp(void)
{
	KeyUp(&in_attack);
	FF_StopAll(); /* SPEC-312 section 12 */
}

static void
IN_AltAttackDown(void)
{
	KeyDown(&in_altattack);
}

static void
IN_AltAttackUp(void)
{
	KeyUp(&in_altattack);
	FF_StopAll();
}

/*
 * Returns the fraction of the
 * frame that the key was down
 */
static float
CL_KeyState(button_t *bt)
{
	kbutton_t *key = &bt->k;
	float val;
	int msec;

	key->state &= 1; /* clear impulses */

	msec = key->msec;
	key->msec = 0;

	if (key->state)
	{
		/* still down */
		msec += sys_frame_time - key->downtime;
		key->downtime = sys_frame_time;
	}

	val = (float)msec / frame_msec;

	if (val < 0)
	{
		val = 0;
	}

	if (val > 1)
	{
		val = 1;
	}

	return val;
}

#define DOWN(b) (((b).k.state & 1) != 0)
#define DOWN_OR_WENT(b) (((b).k.state & 3) != 0)

qboolean
IN_StrafeIsDown(void)
{
	return DOWN(in_strafe_b);
}

cvar_t *cl_upspeed;
cvar_t *cl_forwardspeed;
cvar_t *cl_sidespeed;
cvar_t *cl_yawspeed;
cvar_t *cl_pitchspeed;
cvar_t *cl_run;
cvar_t *cl_anglespeedkey;

/* ---------------------------------------------------------------------
 * cinematic freeze and skip (SPEC-312 sections 10 A, 11)
 * --------------------------------------------------------------------- */

qboolean
CL_CinematicFreezeActive(void)
{
	if (!CL_SoF_Active() || (cls.state != ca_active))
	{
		return false;
	}

	return CL_SoF_CurrentPS()->cinfreeze != 0;
}

void
CL_RequestCinematicSkip(void)
{
	skip_requested = true;
}

/* SPEC-312 5.1: toggleconsole works only with the console cvar set; with
   game key focus during a cinematic freeze it requests a skip instead */
qboolean
Key_ConsoleToggleAllowed(void)
{
	if (!console_cvar)
	{
		console_cvar = Cvar_Get("console", "1", CVAR_NOSET);
	}

	if (!console_cvar->value)
	{
		return false;
	}

	if ((cls.key_dest == key_game) && CL_CinematicFreezeActive())
	{
		CL_RequestCinematicSkip();
		return false;
	}

	return true;
}

/* ---------------------------------------------------------------------
 * the user command (SPEC-312 section 10)
 * --------------------------------------------------------------------- */

/* B: keyboard movement; every change of a movement field is truncated
   toward zero at once (the fields are shorts) */
static void
CL_KeyboardMove(usercmd_t *cmd)
{
	float t;

	/* 1. turn speed */
	t = cls.nframetime;

	if (DOWN(in_speed))
	{
		t *= cl_anglespeedkey->value;
	}

	/* 2. pitch */
	cl.viewangles[PITCH] -= t * cl_pitchspeed->value * CL_KeyState(&in_lookup);
	cl.viewangles[PITCH] += t * cl_pitchspeed->value * CL_KeyState(&in_lookdown);

	/* 3. left / right (+use +strafe +left/+right leans instead) */
	if (!(DOWN(in_use) && DOWN(in_strafe_b) && (DOWN(in_left) || DOWN(in_right))))
	{
		if (DOWN(in_strafe_b) || (lookstrafe->value && DOWN(in_klook)))
		{
			cmd->sidemove += cl_sidespeed->value * CL_KeyState(&in_right);
			cmd->sidemove -= cl_sidespeed->value * CL_KeyState(&in_left);
		}
		else
		{
			cl.viewangles[YAW] -= t * cl_yawspeed->value * CL_KeyState(&in_right);
			cl.viewangles[YAW] += t * cl_yawspeed->value * CL_KeyState(&in_left);
		}
	}

	/* 4. strafing, only while +use is not down (it leans then) */
	if (!DOWN(in_use))
	{
		cmd->sidemove += cl_sidespeed->value * CL_KeyState(&in_moveright);
		cmd->sidemove -= cl_sidespeed->value * CL_KeyState(&in_moveleft);
	}

	/* 5. up / down */
	cmd->upmove += cl_upspeed->value * CL_KeyState(&in_up);
	cmd->upmove -= cl_upspeed->value * CL_KeyState(&in_down);

	/* 6. forward / back, or pitch with +klook */
	if (DOWN(in_klook))
	{
		cl.viewangles[PITCH] -= t * cl_pitchspeed->value * CL_KeyState(&in_forward);
		cl.viewangles[PITCH] += t * cl_pitchspeed->value * CL_KeyState(&in_back);
	}
	else
	{
		cmd->forwardmove += cl_forwardspeed->value * CL_KeyState(&in_forward);
		cmd->forwardmove -= cl_forwardspeed->value * CL_KeyState(&in_back);
	}
}

static void
CL_ClampPitch(void)
{
	float pitch;

	pitch = SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[PITCH]);

	if (pitch > 180)
	{
		pitch -= 360;
	}

	if (cl.viewangles[PITCH] + pitch < -360)
	{
		cl.viewangles[PITCH] += 360; /* wrapped */
	}

	if (cl.viewangles[PITCH] + pitch > 360)
	{
		cl.viewangles[PITCH] -= 360; /* wrapped */
	}

	if (cl.viewangles[PITCH] + pitch > 89)
	{
		cl.viewangles[PITCH] = 89 - pitch;
	}

	if (cl.viewangles[PITCH] + pitch < -89)
	{
		cl.viewangles[PITCH] = -89 - pitch;
	}

	if (cl_pitchdebug->value)
	{
		Com_Printf("cl.viewangles[PITCH]=%f, delta_angles[PITCH]=%f\n",
				cl.viewangles[PITCH], pitch);
	}
}

/* D 9: lean, later rules override earlier ones */
static int
CL_Lean(void)
{
	int lean = 0;

	/* "+use was down at any time this frame" (the held flags are only used
	   for the strafe side, SPEC-312 10 D 9a) */
	if (DOWN_OR_WENT(in_use))
	{
		if (DOWN_OR_WENT(in_moveright) || in_moveright.held ||
			IN_JoyLeanRight() || (in_right.held && in_strafe_b.held))
		{
			lean = -1;
		}

		if (DOWN_OR_WENT(in_moveleft) || in_moveleft.held ||
			IN_JoyLeanLeft() || (in_left.held && in_strafe_b.held))
		{
			lean = 1;
		}
	}

	if (DOWN_OR_WENT(in_leanright))
	{
		lean = -1;
	}

	if (DOWN_OR_WENT(in_leanleft))
	{
		lean = 1;
	}

	return lean;
}

/* D 11: running diagonally is no faster than running straight */
static void
CL_DiagonalCorrection(usercmd_t *cmd)
{
	double f = cmd->forwardmove, s = cmd->sidemove, len, m;

	if (!cmd->forwardmove || !cmd->sidemove)
	{
		return;
	}

	m = fabs(f) > fabs(s) ? fabs(f) : fabs(s);
	len = sqrt(f * f + s * s);
	cmd->forwardmove = (short)(f / len * m);
	cmd->sidemove = (short)(s / len * m);
}

static void
CL_BuildCmd(usercmd_t *cmd)
{
	const sofps_t *ps = CL_SoF_CurrentPS();
	qboolean frozen = CL_CinematicFreezeActive();
	qboolean weapons;
	int i, ms, lean;

	memset(cmd, 0, sizeof(*cmd));

	/* frame milliseconds since the previous user command, 1..200 */
	frame_msec = sys_frame_time - old_sys_frame_time;

	if ((int)frame_msec < 1)
	{
		frame_msec = 1;
	}
	else if (frame_msec > 200)
	{
		frame_msec = 200;
	}

	old_sys_frame_time = sys_frame_time;

	if (!frozen)
	{
		/* B */
		CL_KeyboardMove(cmd);

		/* C: the mouse is read every input frame (CL_RefreshCmd); its
		   movement share is added here */
		cmd->sidemove += mouse_accum.sidemove;
		cmd->forwardmove += mouse_accum.forwardmove;
		cmd->upmove += mouse_accum.upmove;
	}
	else if (skip_requested)
	{
		cmd->buttons |= BUTTON_SKIP;
	}

	memset(&mouse_accum, 0, sizeof(mouse_accum));
	skip_requested = false;

	/* B 7: run is a button bit; the movement is not doubled */
	if ((DOWN(in_speed) ? 1 : 0) ^ ((int)cl_run->value & 1))
	{
		cmd->buttons |= BUTTON_RUN;
	}

	/* D 1: use/skip */
	if (((ps->pm_type == 3) && (DOWN_OR_WENT(in_use) || DOWN_OR_WENT(in_attack))) ||
		(frozen && DOWN_OR_WENT(in_use)))
	{
		cmd->buttons |= BUTTON_SKIP;
	}

	/* D 2: weapon buttons only without a remote view (or type 1) */
	weapons = (ps->remote_type == 0) || (ps->remote_type == 1);

	if (weapons)
	{
		if (DOWN_OR_WENT(in_attack))
		{
			cmd->buttons |= BUTTON_ATTACK;
		}

		if (DOWN_OR_WENT(in_altattack))
		{
			cmd->buttons |= BUTTON_ALTATTACK;
		}

		if (DOWN_OR_WENT(in_wextra1))
		{
			cmd->buttons |= BUTTON_WEAPONEXTRA1;
		}

		if (DOWN_OR_WENT(in_wextra2))
		{
			cmd->buttons |= BUTTON_WEAPONEXTRA2;
		}
	}

	/* D 3 */
	if (DOWN_OR_WENT(in_use))
	{
		cmd->buttons |= BUTTON_USE;
	}

	/* D 9 (before the "went down" marks are cleared) */
	lean = CL_Lean();

	/* D 4 */
	in_attack.k.state &= ~2;
	in_altattack.k.state &= ~2;
	in_wextra1.k.state &= ~2;
	in_wextra2.k.state &= ~2;
	in_use.k.state &= ~2;
	in_leanleft.k.state &= ~2;
	in_leanright.k.state &= ~2;

	/* D 5 */
	if (anykeydown && (cls.key_dest == key_game))
	{
		cmd->buttons |= BUTTON_ANY;
	}

	/* D 6 */
	ms = (int)(cls.nframetime * 1000.0f);

	if (ms > 250)
	{
		ms = 100;
	}

	cmd->msec = (byte)ms;

	/* D 7, 8 */
	CL_ClampPitch();

	for (i = 0; i < 3; i++)
	{
		cmd->angles[i] = (short)((int)(cl.viewangles[i] * 65536.0f / 360.0f) & 0xFFFF);
	}

	/* D 10 */
	cmd->lightlevel = (byte)(int)cl_lightlevel->value;

	/* D 11 */
	CL_DiagonalCorrection(cmd);

	/* clamping, wire quantisation, lean and fire times (cl_sof.c);
	   no client weapon prediction yet, so the fire times stay 0 */
	CL_SoF_FinalizeCmd(cmd, lean, false, false, false, false);
}

void
IN_CenterView(void)
{
	cl.viewangles[PITCH] = -SHORT2ANGLE(cl.frame.playerstate.pmove.delta_angles[PITCH]);
}

/* ---------------------------------------------------------------------
 * force feedback (SPEC-312 section 12): commands and messages; no device
 * --------------------------------------------------------------------- */

static cvar_t *use_ff;
static cvar_t *cl_ffmult;
static qboolean ff_spring_device;

void
FF_Init(void)
{
	use_ff = Cvar_Get("use_ff", "1", CVAR_ARCHIVE);
	cl_ffmult = Cvar_Get("cl_ffmult", "3.0", CVAR_ARCHIVE);

	Com_Printf("\n\a------- Force Feedback Initialization -------\n");

	if (!use_ff->value)
	{
		Com_Printf("...inhibited, not initializing\n");
		return;
	}

	Com_Printf("Creating feedback device:\n");
	Com_Printf("...no feedback devices found\n");
	ff_spring_device = false;
}

void
FF_Shutdown(void)
{
	Com_Printf("\n\a------- Force Feedback Shutdown -------\n");
}

void
FF_StopAll(void)
{
	/* no feedback device */
}

static void
FF_Restart_f(void)
{
	FF_Shutdown();
	FF_Init();
}

static void
FF_Tension_f(void)
{
	static const int tension[4] = {0, 1000, 5000, 10000};
	int t;

	if ((Cmd_Argc() != 2) || ((t = (int)strtol(Cmd_Argv(1), NULL, 10)) < 0) || (t > 3))
	{
		Com_Printf("ff_tension <tension [0..3]>\n");
		return;
	}

	if (!ff_spring_device)
	{
		Com_Printf("No spring device\n");
		return;
	}

	Com_Printf("Setting tension %d\n", tension[t]);
}

static void
FF_Spring_f(void)
{
	int s;

	if ((Cmd_Argc() != 2) || ((s = (int)strtol(Cmd_Argv(1), NULL, 10)) < 0) || (s > 10000))
	{
		Com_Printf("ff_spring <0..10000>\n");
		return;
	}

	if (!ff_spring_device)
	{
		Com_Printf("No spring device\n");
		return;
	}

	Com_Printf("Setting spring to %d\n", s);
}

/* ---------------------------------------------------------------------
 * init
 * --------------------------------------------------------------------- */

void
CL_InitInput(void)
{
	Cmd_AddCommand("centerview", IN_CenterView);

	Cmd_AddCommand("+moveup", IN_UpDown);
	Cmd_AddCommand("-moveup", IN_UpUp);
	Cmd_AddCommand("+movedown", IN_DownDown);
	Cmd_AddCommand("-movedown", IN_DownUp);
	Cmd_AddCommand("+left", IN_LeftDown);
	Cmd_AddCommand("-left", IN_LeftUp);
	Cmd_AddCommand("+right", IN_RightDown);
	Cmd_AddCommand("-right", IN_RightUp);
	Cmd_AddCommand("+forward", IN_ForwardDown);
	Cmd_AddCommand("-forward", IN_ForwardUp);
	Cmd_AddCommand("+back", IN_BackDown);
	Cmd_AddCommand("-back", IN_BackUp);
	Cmd_AddCommand("+lookup", IN_LookupDown);
	Cmd_AddCommand("-lookup", IN_LookupUp);
	Cmd_AddCommand("+lookdown", IN_LookdownDown);
	Cmd_AddCommand("-lookdown", IN_LookdownUp);
	Cmd_AddCommand("+strafe", IN_StrafeDown);
	Cmd_AddCommand("-strafe", IN_StrafeUp);
	Cmd_AddCommand("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand("+moveright", IN_MoverightDown);
	Cmd_AddCommand("-moveright", IN_MoverightUp);
	Cmd_AddCommand("+speed", IN_SpeedDown);
	Cmd_AddCommand("-speed", IN_SpeedUp);
	Cmd_AddCommand("+leanleft", IN_LeanLeftDown);
	Cmd_AddCommand("-leanleft", IN_LeanLeftUp);
	Cmd_AddCommand("+leanright", IN_LeanRightDown);
	Cmd_AddCommand("-leanright", IN_LeanRightUp);
	Cmd_AddCommand("+attack", IN_AttackDown);
	Cmd_AddCommand("-attack", IN_AttackUp);
	Cmd_AddCommand("+altattack", IN_AltAttackDown);
	Cmd_AddCommand("-altattack", IN_AltAttackUp);
	Cmd_AddCommand("+weaponExtra1", IN_WeaponExtra1Down);
	Cmd_AddCommand("-weaponExtra1", IN_WeaponExtra1Up);
	Cmd_AddCommand("+weaponExtra2", IN_WeaponExtra2Down);
	Cmd_AddCommand("-weaponExtra2", IN_WeaponExtra2Up);
	Cmd_AddCommand("+use", IN_UseDown);
	Cmd_AddCommand("-use", IN_UseUp);
	Cmd_AddCommand("+klook", IN_KLookDown);
	Cmd_AddCommand("-klook", IN_KLookUp);

	Cmd_AddCommand("ff_usemouse", FF_Restart_f);
	Cmd_AddCommand("ff_usejoy", FF_Restart_f);
	Cmd_AddCommand("ff_tension", FF_Tension_f);
	Cmd_AddCommand("ff_spring", FF_Spring_f);

	cl_nodelta = Cvar_Get("cl_nodelta", "0", 0);
	cl_pitchdebug = Cvar_Get("cl_pitchdebug", "0", 0);
	lookspring = Cvar_Get("lookspring", "0", CVAR_ARCHIVE);
	console_cvar = Cvar_Get("console", "1", CVAR_NOSET);

	FF_Init();
}

/* Called every input frame while connected. The SoF user command is built
   once per packet frame in CL_SendCmd (SPEC-312 section 10); the mouse and
   joystick are read here, once per frame as the original reads them, so the
   view turns at the frame rate: angles change at once, movement is kept for
   the next command. */
void
CL_RefreshCmd(void)
{
	if ((cls.key_dest == key_game) && !CL_CinematicFreezeActive() && !CL_Demo_Playing())
	{
		IN_Move(&mouse_accum);
	}
}

void
CL_RefreshMove(void)
{
}

void
CL_SendCmd(void)
{
	sizebuf_t buf;
	byte data[128];
	int i;
	usercmd_t *cmd;

	memset(&buf, 0, sizeof(buf));

	/* demo playback: the client sends nothing (SPEC-311) */
	if (CL_Demo_Playing())
	{
		SZ_Clear(&cls.netchan.message);
		CL_BuildCmd(&cl.cmd);
		return;
	}

	/* build this frame's command and save it off for prediction */
	i = cls.netchan.outgoing_sequence & (CMD_BACKUP - 1);
	cmd = &cl.cmds[i];
	cl.cmd_time[i] = cls.realtime; /* for netgraph ping calculation */

	if (cls.state >= ca_connected)
	{
		CL_BuildCmd(cmd);
	}
	else
	{
		memset(cmd, 0, sizeof(*cmd));
		old_sys_frame_time = sys_frame_time;
	}

	cl.cmd = *cmd;

	if ((cls.state == ca_disconnected) || (cls.state == ca_connecting))
	{
		return;
	}

	if (cls.state == ca_connected)
	{
		if (cls.netchan.message.cursize || (curtime - cls.netchan.last_sent > 1000))
		{
			Netchan_Transmit(&cls.netchan, 0, buf.data);
		}

		return;
	}

	/* send a userinfo update if needed */
	if (userinfo_modified)
	{
		userinfo_modified = false;
		MSG_WriteByte(&cls.netchan.message, clc_userinfo);
		MSG_WriteString(&cls.netchan.message, Cvar_Userinfo());
	}

	SZ_Init(&buf, data, sizeof(data));

	/* bit-packed SoF move message (SPEC-202 section 6) */
	CL_SoF_WriteMove(&buf, cl_nodelta->value != 0);
	Netchan_Transmit(&cls.netchan, buf.cursize, buf.data);

	/* the next slot is not built yet; prediction skips it (msec 0) */
	memset(&cl.cmds[cls.netchan.outgoing_sequence & (CMD_BACKUP - 1)], 0, sizeof(usercmd_t));
}
