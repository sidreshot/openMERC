/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc client: the Ghoul client system. The client owns the engine's client Ghoul system (SPEC-010 A)
 * and
 *  - feeds it the server's model registry and instance states: every svc_ghoulreliable payload to system
 *    method 24, every svc_ghoulunreliable payload to method 25 with the number and game time of the svc_frame
 *    of the same packet; methods 23 and 7 when the client has finished loading, method 8 when its state is
 *    cleared (SPEC-010 Lifetimes, SPEC-013 section 5, SPEC-201/203);
 *  - keeps the game clock (SPEC-202 section 5): the estimate of the server's game time that the renderer
 *    receives as its animation clock (SPEC-002 frame record 0x48-0x50) and the Ghoul instances are drawn at;
 *  - hands the system to the renderer through the third argument of the SPEC-002 Init slot of the running
 *    renderer's SoF table (OM_GetSoFRefExports), and detaches it (Init with NULL) before the renderer goes away;
 *  - turns the Ghoul entities of a frame into renderer entities (SPEC-002 entity: flag 0x400000, frame = the
 *    entity state's instance handle) and adds the view weapon of player-state field 1 with the client-side
 *    sway transform (SPEC-310 section 13);
 *  - draws the menu models for the menu runtime (SPEC-803 section 1, Rendering; cl_ghoulsys.cpp).
 *
 * Packet-entity values as SPEC-AMEND-006 I1c.2 states them (also used by cl_sof.c for non-Ghoul entities):
 * angles interpolated, with a secondary model index pitch and roll negated and the yaw plus the yaw offset
 * quantised; auto-rotating ones keep pitch and roll and take the real-time auto-rotate yaw (I1c.7); the
 * viewer's own Ghoul model gets flag 0x2 and the view origin the previous frame left; the view weapon is a
 * zeroed entity with origin = view origin, angles = view angles with pitch and roll negated, frame = handle,
 * flags 0x400015.
 *
 * Implements: SPEC-010, SPEC-013 (client side), SPEC-002 (Init slot, entities, game clock fields),
 * SPEC-202 (section 5), SPEC-310 (section 13), SPEC-803 (section 1), SPEC-AMEND-006 (I1c.1, 2, 6, 7)
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "header/cl_ghoul.h"
#include "../common/header/sof/refapi.h"

extern void *reflib_handle; /* vid.c */

#define CLG_RF_VIEWERMODEL 0x00000002
#define CLG_EF_ROTATE 0x2000
#define CLG_VIEWWEAPON_FLAGS 0x00400015  /* ghoul, minimum light, weapon model, depth hack */

static cvar_t *om_clghoul;           /* OpenMerc test switch: 0 = placeholders instead of Ghoul drawing */
static cvar_t *om_entdump;           /* OpenMerc test aid: print the Ghoul entities of the next frame */
static cvar_t *ghl_specular;
static cvar_t *ghl_light_method;
static cvar_t *ghl_no_texture_stack;

static const sofrefexport_t *clg_re; /* SoF table of the running renderer */
static qboolean clg_attached;        /* the renderer holds the client system */

/* game clock (SPEC-202 section 5, SPEC-AMEND-006 I1c.1) */
static qboolean clg_clockset;
static float clg_clock, clg_oldclock;
static float clg_rec48, clg_rec4c, clg_rec50; /* frame-record values, written when the view is rebuilt */
static qboolean clg_recset;
static cvar_t *clg_freezeworld;
static vec3_t clg_prevview;                   /* view origin the previous rendered frame left (nudged) */
static qboolean clg_prevviewset;

/* svc_frame of the packet being parsed (svc 27 is decoded against it) */
static int clg_frame;
static float clg_frametime;

/* view-weapon sway (SPEC-310 13.5): the instance's own transform and what we stored on it */
static struct
{
	qboolean valid;
	int handle;
	float base[16];
	float last[16];
} clg_vw;

static qboolean clg_dumping;
static cvar_t *ghl_om_maxtexsize;
void OMGhoul_SetMaxTextureSize(int size); /* ghoul_system.cpp (fb5) */

/* ---- start-up --------------------------------------------------------------- */

static void
CL_Ghoul_Info_f(void)
{
	const sofps_t *x = CL_SoF_CurrentPS();

	Com_Printf("Ghoul client system %p, renderer %s, game clock %.4f (svc_frame %d, %.4f)\n", CLG_System(),
			clg_attached ? "attached" : "not attached", clg_clock, clg_frame, clg_frametime);
	Com_Printf("view weapon handle %d: %s\n", x->gun_id,
			x->gun_id ? (CLG_FindInstance(x->gun_id) ? "instance" : "no instance") : "none");
}

void
CL_Ghoul_Init(void)
{
	om_clghoul = Cvar_Get("om_clghoul", "1", 0);
	om_entdump = Cvar_Get("om_entdump", "0", 0);
	/* SPEC-010 Constants: the renderer's Ghoul cvars (same defaults and flags) */
	ghl_specular = Cvar_Get("ghl_specular", "1", CVAR_ARCHIVE);
	ghl_light_method = Cvar_Get("ghl_light_method", "3", CVAR_ARCHIVE);
	ghl_no_texture_stack = Cvar_Get("ghl_no_texture_stack", "0", CVAR_ARCHIVE);
	clg_freezeworld = Cvar_Get("freezeworld", "0", 0);
	/* fb5: Ghoul skin upload cap; 256 = the SoF rule, larger keeps HD-pack skins (sofhd2024 4x) */
	ghl_om_maxtexsize = Cvar_Get("ghl_om_maxtexsize", "2048", CVAR_ARCHIVE);
	Cmd_AddCommand("om_ghoulinfo", CL_Ghoul_Info_f);
	(void)CLG_System(); /* the library is up before the renderer asks for the system */
}

void SV_Ghoul_Shutdown(void); /* sv_ghoul.cpp (I1b): OMGhoul_Shutdown and the server's text cache */

/* process exit (SPEC-010 Lifetimes): every object of the three systems is destroyed and the caches are
   emptied while the engine still runs (the filesystem frees the model files). Leaving it to the static
   destructors at exit() crashed: the library's file cache outlives its decoded-chunk cache there. */
void
CL_Ghoul_Shutdown(void)
{
	clg_attached = false;
	clg_re = NULL;
	SV_Ghoul_Shutdown();
}

void *
CL_Ghoul_System(void)
{
	return CLG_System();
}

int
CL_Ghoul_Active(void)
{
	return clg_attached && om_clghoul && om_clghoul->value;
}

/* ---- renderer hand-off (SPEC-002 Init slot, SPEC-010 Lifetimes) ----------------- */

void
CL_Ghoul_RendererStarted(void)
{
	OM_GetSoFRefExports_t fn;

	clg_re = NULL;
	clg_attached = false;
	clg_vw.valid = false;

	if (!reflib_handle)
	{
		return;
	}

	fn = (OM_GetSoFRefExports_t)Sys_GetProcAddress(reflib_handle, OM_SOFREF_EXPORTS_NAME);
	clg_re = fn ? fn() : NULL;

	if (!clg_re || !clg_re->Init)
	{
		clg_re = NULL;
		return;
	}

	OMGhoul_SetMaxTextureSize(ghl_om_maxtexsize ? (int)ghl_om_maxtexsize->value : 256);

	/* the renderer runs system methods 38, 39, 37, 1, 2 and keeps the pointer */
	if (clg_re->Init(NULL, NULL, CLG_System()) == -1)
	{
		Com_Printf("Ghoul: the renderer refused the client system\n");
		clg_re = NULL;
		return;
	}

	clg_attached = true;
}

void
CL_Ghoul_RendererStopping(void)
{
	/* method 3 (release every Ghoul texture) runs while the GL context still exists */
	if (clg_attached && clg_re && clg_re->Init)
	{
		clg_re->Init(NULL, NULL, NULL);
	}

	clg_attached = false;
	clg_re = NULL;
	clg_vw.valid = false;
}

/* ---- network (SPEC-013 section 5, SPEC-201 svc 26/27, SPEC-203) ---------------- */

void
CL_Ghoul_FrameParsed(int serverframe, float gametime)
{
	clg_frame = serverframe;
	clg_frametime = gametime;

	/* SPEC-202 5, SPEC-AMEND-006 I1c.1 rule 1: for every svc_frame (valid or not), within 0.5 s the
	   estimate moves 5 % toward the received time, else it jumps */
	if (!clg_clockset || (fabsf(clg_clock - gametime) > 0.5f))
	{
		clg_clock = gametime;
		clg_clockset = true;
	}
	else
	{
		clg_clock = 0.95f * clg_clock + 0.05f * gametime;
	}
}

void
CL_Ghoul_ParseReliable(const unsigned char *data, int len)
{
	/* SPEC-AMEND-006 I1c.6: the specular flag the objects are prepared with, both cvars read now and
	   tested > 0 (the game module's test; the renderer's own method-2 call keeps > 0.5) */
	int specular = (ghl_specular->value > 0) && (ghl_light_method->value > 0);

	CLG_ReadReliable(data, len, specular);
}

void
CL_Ghoul_ParseUnreliable(const unsigned char *data, int len)
{
	CLG_ReadUnreliable(clg_frame, data, len, clg_frametime);
}

void
CL_Ghoul_LoadingDone(void)
{
	CLG_StartAccepting(); /* method 23: apply the model data stored so far */
	CLG_Purge();          /* method 7 */
}

void
CL_Ghoul_ClearState(void)
{
	CLG_Reset(); /* method 8: objects, instances, strings, network data */
	clg_clockset = false;
	clg_clock = clg_oldclock = 0;
	clg_recset = false;
	clg_rec48 = clg_rec4c = clg_rec50 = 0;
	clg_frame = 0;
	clg_frametime = 0;
	clg_vw.valid = false;
}

/* ---- game clock -------------------------------------------------------------- */

/* SPEC-AMEND-006 I1c.1 rules 2 and 3: once per client frame the estimate grows by the frame's unclamped
   duration, except while `paused` or `freezeworld` is set or the client is not active */
void
CL_Ghoul_RunFrame(float frametime)
{
	clg_oldclock = clg_clock;

	if (clg_clockset && (frametime > 0) && !cl_paused->value &&
		!(clg_freezeworld && clg_freezeworld->value) && (cls.state == ca_active))
	{
		clg_clock += frametime;
	}

	clg_dumping = om_entdump && om_entdump->value;

	if (clg_dumping)
	{
		Cvar_Set("om_entdump", "0");
		Com_Printf("om_entdump: real %d ms client time %.3f clock %.5f (svc_frame %d, game time %.3f; frame time "
				"%.4f, state %d, paused %g, freezeworld %s)\n", Sys_Milliseconds(), cl.time * 0.001f, clg_clock,
				clg_frame, clg_frametime, frametime, cls.state, cl_paused->value,
				clg_freezeworld ? clg_freezeworld->string : "-");
	}
}

/* SPEC-AMEND-006 I1c.1 rule 4 and I1c.2: called when the view is rebuilt (CL_AddEntities, before the view
   is computed): the frame-record values take the estimate, and the view origin the previous rendered frame
   left (after its 1/16 nudge) is kept for the viewer's own entity */
void
CL_Ghoul_ViewRebuild(void)
{
	float now = CL_Ghoul_GameClock();

	clg_rec48 = clg_recset ? clg_rec50 : now;
	clg_rec50 = now;
	clg_rec4c = clg_rec50 - clg_rec48;
	clg_recset = true;

	VectorCopy(cl.refdef.vieworg, clg_prevview);
	clg_prevviewset = true;

	if (clg_dumping)
	{
		Com_Printf("om_entdump: frame record game %.5f/%.5f/%.5f\n", clg_rec48, clg_rec4c, clg_rec50);
	}
}

float
CL_Ghoul_GameClock(void)
{
	return clg_clockset ? clg_clock : cl.time * 0.001f;
}

/* the frame-record values (SPEC-002 0x48, 0x4c, 0x50) as the last view rebuild set them */
void
CL_Ghoul_FrameRecordClock(float *old, float *delta, float *now)
{
	if (!clg_recset)
	{
		*now = *old = CL_Ghoul_GameClock();
		*delta = 0;
		return;
	}

	*old = clg_rec48;
	*delta = clg_rec4c;
	*now = clg_rec50;
}

/* SPEC-AMEND-006 I1c.7: the auto-rotate yaw - the real-time millisecond clock in centiseconds, quantised to
   1/65536 of a turn (100 degrees per real second, also while paused) */
float
CL_Ghoul_AutoRotateYaw(void)
{
	unsigned c = (unsigned)Sys_Milliseconds() / 10;
	unsigned q = (unsigned)(((unsigned long long)((double)c * 65536.0 / 360.0)) & 65535);

	return q * (360.0f / 65536.0f);
}

/* ---- entities ------------------------------------------------------------------ */

static void
CL_Ghoul_Dump(const char *what, const entity_t *e)
{
	Com_Printf("  %s org(%g %g %g) ang(%g %g %g) frame %d flags %08x alpha %g\n", what, e->origin[0],
			e->origin[1], e->origin[2], e->angles[0], e->angles[1], e->angles[2], e->frame, e->flags,
			e->alpha);
}

/* SPEC-AMEND-006 I1c.2 steps 1, 2 and 5, for every packet entity (cl_sof.c): angles, frame, old frame,
   back-lerp and alpha as the original client fills its entity record */
void
CL_Ghoul_EntityValues(entity_t *ent, const entity_state_t *s)
{
	const centity_t *cent = &cl_entities[s->number];
	int i, f;

	if (s->effects & CLG_EF_ROTATE)
	{
		/* pitch and roll interpolated, not negated; the real-time auto-rotate yaw (I1c.7) */
		ent->angles[0] = LerpAngle(cent->prev.angles[0], cent->current.angles[0], cl.lerpfrac);
		ent->angles[1] = CL_Ghoul_AutoRotateYaw();
		ent->angles[2] = LerpAngle(cent->prev.angles[2], cent->current.angles[2], cl.lerpfrac);
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			ent->angles[i] = LerpAngle(cent->prev.angles[i], cent->current.angles[i], cl.lerpfrac);
		}

		/* with a secondary model index (every spawned game entity): pitch and roll negated, the yaw plus
		   the state's yaw offset (an angle byte, kept raw in modelindex3 by cl_sof.c), quantised to 1/65536 of
		   a turn by truncation toward zero, in [0, 360) */
		if (s->modelindex2)
		{
			double yaw = ent->angles[1] + (signed char)(s->modelindex3 & 0xFF) * (360.0 / 256.0);
			long long q = (long long)(yaw * (65536.0 / 360.0));

			ent->angles[0] = -ent->angles[0];
			ent->angles[1] = (float)((q & 65535) * (360.0 / 65536.0));
			ent->angles[2] = -ent->angles[2];
		}
	}

	/* the frame, or the Quake II auto-animation of effect bits 0x1078 (client time) */
	if (s->effects & 0x1078)
	{
		int a = (int)(2 * (long long)cl.time / 1000);

		if (s->effects & 0x20)
		{
			f = a;
		}
		else if (s->effects & 0x40)
		{
			f = cl.time / 100;
		}
		else
		{
			f = a & 1;

			if (s->effects & 0x10)
			{
				f += 2;
			}

			if (s->effects & 0x1000)
			{
				f /= 2;
			}
		}
	}
	else
	{
		f = s->frame;
	}

	ent->frame = f;
	ent->oldframe = cent->prev.frame;
	ent->backlerp = 1.0f - cl.lerpfrac;
	ent->alpha = 1.0f;
	ent->flags = s->renderfx; /* step 6: the state's render flags (renderer: SPEC-002 flag table) */
}

int
CL_Ghoul_AddEntity(entity_t *ent, const entity_state_t *s, int own)
{
	int i;

	if (!CL_Ghoul_Active() || !s)
	{
		return false;
	}

	/* angles, frame (= the instance handle, SPEC-002 entity 0x34), old frame, back-lerp and alpha were set by
	   CL_Ghoul_EntityValues; model, skin and box are not written for Ghoul entities */
	ent->model = NULL;
	ent->skin = NULL;
	ent->skinnum = 0;
	ent->flags = s->renderfx | SOF_RF_GHOUL; /* I1c.2 step 6: the state's render flags */

	if (own)
	{
		/* the viewer's own model: shadow only, at the view origin the previous rendered frame left (after
		   its 1/16 nudge): the original adds the packet entities before it computes the view (I1c.2 step 8) */
		ent->flags |= CLG_RF_VIEWERMODEL;

		for (i = 0; i < 3; i++)
		{
			ent->origin[i] = clg_prevviewset ? clg_prevview[i] : cl.refdef.vieworg[i] + 1.0f / 16;
		}

		VectorCopy(ent->origin, ent->oldorigin);
	}

	if (clg_dumping)
	{
		CL_Ghoul_Dump(va("ent %d", s->number), ent);
	}

	V_AddEntity(ent);
	return true;
}

/* SPEC-310 13.5: the model's own transform (re-captured whenever something else changed it since the last
   frame) times a translation in model space, stored back on the instance */
static void
CL_Ghoul_ApplySway(void *inst, int handle)
{
	const float *t = CL_SoF_ViewWeaponSway();
	float cur[16], m[16];
	int j;

	CLG_GetTransform(inst, cur);

	if (!clg_vw.valid || (clg_vw.handle != handle) || memcmp(cur, clg_vw.last, sizeof(cur)))
	{
		memcpy(clg_vw.base, cur, sizeof(cur));
		clg_vw.handle = handle;
		clg_vw.valid = true;
	}

	/* row vectors: p' = (p + t) * base, i.e. rows 0-2 stay, row 3 = t * R + base row 3 */
	memcpy(m, clg_vw.base, sizeof(m));

	for (j = 0; j < 3; j++)
	{
		m[12 + j] = clg_vw.base[12 + j] + t[0] * clg_vw.base[j] + t[1] * clg_vw.base[4 + j] +
			t[2] * clg_vw.base[8 + j];
	}

	CLG_SetTransform(inst, m);
	CLG_GetTransform(inst, clg_vw.last);
}

void
CL_Ghoul_AddViewWeapon(void)
{
	const sofps_t *x = CL_SoF_CurrentPS();
	entity_t gun;
	void *inst;

	if (!CL_Ghoul_Active() || !cl_gun->value || x->remote_type || !x->gun_id ||
		(cl.frame.playerstate.fov > 95))
	{
		return;
	}

	inst = CLG_FindInstance(x->gun_id);

	if (!inst)
	{
		clg_vw.valid = false;
		return;
	}

	CL_Ghoul_ApplySway(inst, x->gun_id);

	memset(&gun, 0, sizeof(gun));
	VectorCopy(cl.refdef.vieworg, gun.origin);
	VectorCopy(gun.origin, gun.oldorigin);
	gun.angles[0] = -cl.refdef.viewangles[0];
	gun.angles[1] = cl.refdef.viewangles[1];
	gun.angles[2] = -cl.refdef.viewangles[2];
	gun.frame = x->gun_id;
	gun.flags = CLG_VIEWWEAPON_FLAGS;
	gun.backlerp = 1.0f - cl.lerpfrac;

	if (clg_dumping)
	{
		CL_Ghoul_Dump("view weapon", &gun);
	}

	V_AddEntity(&gun);
}

/* ---- menus ---------------------------------------------------------------------- */

void
CL_Ghoul_DrawMenuModel(void *instance, float x, float y, float time, int camerapart)
{
	if (!instance)
	{
		return;
	}

	if (!ghl_no_texture_stack)
	{
		ghl_no_texture_stack = Cvar_Get("ghl_no_texture_stack", "0", CVAR_ARCHIVE);
	}

	CLG_DrawMenu(instance, x, y, time, camerapart, ghl_no_texture_stack->value > 0.5f);
}

void
CL_Ghoul_DrawMenuModelLit(void *instance, float x, float y, float time, float light)
{
	if (!instance)
	{
		return;
	}

	if (!ghl_no_texture_stack)
	{
		ghl_no_texture_stack = Cvar_Get("ghl_no_texture_stack", "0", CVAR_ARCHIVE);
	}

	CLG_DrawMenuLit(instance, x, y, time, ghl_no_texture_stack->value > 0.5f, light);
}
