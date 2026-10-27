/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: shared helpers - random numbers, traces, axes, direction
 * bytes, message reading, the client sound and sprite lists, entity lookup.
 *
 * Implements: SPEC-400 (Terms: rand / irand / ranges), SPEC-401 (client sound
 * list, client sprite list, crand), SPEC-313 s.9 (generator formula)
 */

#include <stdarg.h>

#include "fx_local.h"

cvar_t *fx_sim_time, *cl_max_quads, *cl_max_debris, *fx_maxdebrisonscreen;
cvar_t *cl_freezequads, *freezeworld, *fx_noflash, *fx_edit, *fx_editsegment;
cvar_t *fx_editdir, *fx_run, *fx_partadd, *fx_partsub, *fx_partadjust;
cvar_t *fx_graphic, *fx_sound, *fx_collide, *fx_trail, *fx_trace;
cvar_t *sc_framerate;

/* ---- random numbers ------------------------------------------------------ */

/* The SoF engine shares one generator between all modules (SPEC-001,
   SPEC-313 s.9). Draw order is not normative (SPEC-400 Terms), so the
   effects keep their own state with the same formula. */
static unsigned fx_seed = 0x2F6B3A1Du;
static unsigned fx_cseed = 1;

static int
FX_Next15(void)
{
	fx_seed = fx_seed * 214013u + 2531011u;
	return (int)(fx_seed >> 17);
}

float
FX_flrand(float min, float max)
{
	return min + (max - min) * (float)FX_Next15() / 32768.0f;
}

int
FX_irand(int min, int max)
{
	return min + (int)(((long long)(max - min + 1) * FX_Next15()) >> 15);
}

int
FX_crand(void)
{
	/* the C runtime rand() of the original toolchain: 0..32767 */
	fx_cseed = fx_cseed * 214013u + 2531011u;
	return (int)((fx_cseed >> 16) & 0x7FFF);
}

float
FX_Range(const fx_range_t *r)
{
	if (r->min == r->max)
	{
		return r->min;
	}

	return FX_flrand(r->min, r->max);
}

qboolean
FX_Frozen(void)
{
	return (cl_freezequads && cl_freezequads->value) || (freezeworld && freezeworld->value);
}

/* ---- traces ------------------------------------------------------------- */

static void
FX_ClipEntities(const vec3_t start, const vec3_t mins, const vec3_t maxs,
		const vec3_t end, int kind, trace_t *tr, int *entnum)
{
	int i, num, headnode;
	entity_state_t *ent;
	cmodel_t *cmodel;
	trace_t t;
	vec3_t bmins, bmaxs;
	const float *angles;

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		num = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		ent = &cl_parse_entities[num];

		if (!ent->solid)
		{
			continue;
		}

		if (ent->solid == 31)
		{
			if ((ent->modelindex <= 0) || (ent->modelindex >= MAX_MODELS))
			{
				continue;
			}

			cmodel = cl.model_clip[ent->modelindex];

			if (!cmodel)
			{
				continue;
			}

			headnode = cmodel->headnode;
			angles = ent->angles;
		}
		else
		{
			if (kind != FXTR_SOLID)
			{
				continue;
			}

			CL_SoF_SolidBox(ent->solid, bmins, bmaxs);
			headnode = CM_HeadnodeForBox(bmins, bmaxs);
			angles = vec3_origin;
		}

		t = CM_TransformedBoxTrace(start, end, mins, maxs, headnode,
				MASK_SHOT, ent->origin, angles);

		if (t.allsolid || t.startsolid || (t.fraction < tr->fraction))
		{
			*tr = t;
			*entnum = ent->number;
		}
	}
}

/* Box trace for effects. entnum: 0 = world (or nothing), > 0 = entity hit */
void
FX_Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
		int kind, trace_t *tr, int *entnum)
{
	static vec3_t zero = {0, 0, 0};

	if (!mins)
	{
		mins = zero;
	}

	if (!maxs)
	{
		maxs = zero;
	}

	*entnum = 0;
	*tr = CM_BoxTrace(start, end, mins, maxs, 0, MASK_SHOT);

	if (kind != FXTR_WORLD)
	{
		FX_ClipEntities(start, mins, maxs, end, kind, tr, entnum);
	}
}

int
FX_TraceMaterial(const trace_t *tr)
{
	if (!tr->surface)
	{
		return 0;
	}

	return ((unsigned)tr->surface->flags >> 24) & 0xFF;
}

/* ---- axes ----------------------------------------------------------------- */

/* pitch and yaw of a direction (Quake II convention: pitch up is negative) */
void
FX_VecToAngles(const vec3_t v, vec3_t angles)
{
	float yaw, pitch, fwd;

	if ((v[1] == 0) && (v[0] == 0))
	{
		yaw = 0;
		pitch = (v[2] > 0) ? 90.0f : 270.0f;
	}
	else
	{
		yaw = FX_RAD2DEG(atan2f(v[1], v[0]));

		if (yaw < 0)
		{
			yaw += 360;
		}

		fwd = sqrtf(v[0] * v[0] + v[1] * v[1]);
		pitch = FX_RAD2DEG(atan2f(v[2], fwd));

		if (pitch < 0)
		{
			pitch += 360;
		}
	}

	angles[PITCH] = -pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0;
}

/* forward = dir, right/up derived from its pitch and yaw with roll 0 */
void
FX_AxesFromDir(const vec3_t dir, vec3_t fwd, vec3_t right, vec3_t up)
{
	vec3_t angles, d;

	VectorCopy(dir, d);

	if (VectorNormalize(d) == 0)
	{
		d[0] = 1;
		d[1] = d[2] = 0;
	}

	FX_VecToAngles(d, angles);
	angles[2] = 0;
	AngleVectors(angles, NULL, right, up);
	VectorCopy(d, fwd);
}

void
FX_Perpendicular(const vec3_t n, vec3_t out)
{
	vec3_t src;

	VectorCopy(n, src);

	if (VectorNormalize(src) == 0)
	{
		VectorSet(out, 1, 0, 0);
		return;
	}

	PerpendicularVector(out, src);
}

/* ---- direction bytes (SPEC-201 dir, table in cl_sof.c) -------------------- */

int
FX_DirIndex(const vec3_t v)
{
	return CL_SoF_DirIndex(v);
}

void
FX_DirVector(int index, vec3_t out)
{
	CL_SoF_DirVector(index, out);
}

void
FX_ReadPos(vec3_t out)
{
	MSG_ReadPos(&net_message, out);
}

void
FX_ReadDir(vec3_t out)
{
	int d = MSG_ReadByte(&net_message);

	if ((d < 0) || (d >= 250))
	{
		Com_Error(ERR_DROP, "MSF_ReadDir: out of range");
		VectorClear(out);
		return;
	}

	FX_DirVector(d, out);
}

void
FX_ReadScaledPos(vec3_t out, float scale)
{
	MSG_ReadPos(&net_message, out);
	VectorScale(out, scale, out);
}

/* ---- client sound list (SPEC-401) ----------------------------------------- */

static const char *fx_sound_names[FX_NUM_SOUNDS] = {
	"Ambient/Gen/Electric/spark1", "Ambient/Gen/Electric/spark2",
	"player/foot/metal/metal1", "player/foot/metal/metal2", "player/foot/metal/metal3",
	"player/foot/metal/metal4",
	"player/foot/snow/snow1", "player/foot/snow/snow2", "player/foot/snow/snow3",
	"player/foot/snow/snow4",
	"player/foot/water/water1", "player/foot/water/water2", "player/foot/water/water3",
	"player/foot/water/water4",
	"player/foot/grass/grass1", "player/foot/grass/grass2", "player/foot/grass/grass3",
	"player/foot/grass/grass4",
	"player/foot/gravel/gravel1", "player/foot/gravel/gravel2", "player/foot/gravel/gravel3",
	"player/foot/gravel/gravel4",
	"player/foot/sand/sand1", "player/foot/sand/sand2", "player/foot/sand/sand3",
	"player/foot/sand/sand4",
	"player/foot/wood/wood1", "player/foot/wood/wood2", "player/foot/wood/wood3",
	"player/foot/wood/wood4",
	"player/foot/stone/stone1", "player/foot/stone/stone2", "player/foot/stone/stone3",
	"player/foot/stone/stone4",
	"Weapons/FX/Rics/ric1", "Weapons/FX/Rics/ric2", "Weapons/FX/Rics/ric3",
	"Impact/Surfs/stone", "Impact/Surfs/water", "Impact/Surfs/wood", "Impact/Surfs/grass",
	"Impact/Surfs/gravel", "Impact/Surfs/sand", "Impact/Surfs/snow",
	NULL, /* 44: placeholder, never loaded (metal impacts use 79..81) */
	"Impact/Gore/impact1", "Impact/Gore/impact2", "Impact/Gore/impact3",
	"Weapons/FX/FlyBy/FlyBy1", "Weapons/FX/FlyBy/FlyBy2", "Weapons/FX/FlyBy/FlyBy3",
	"Weapons/FX/FlyBy/FlyBy4",
	"Weapons/Glock/fire",
	"ambient/Nature/weather/thnder1",
	"weapons/FlshGren/FlshExp",
	"Weapons/Autoshot/fire",
	"Enemy/MGun/trnslow", "Enemy/MGun/trnstop",
	"weapons/C4/C4Exp",
	"Impact/Chair/Squeak1", "Impact/Chair/Squeak2", "Impact/Chair/Squeak3",
	"Ambient/Locs/Subway/Train/Brake", "Ambient/Locs/Subway/Train/Horn2",
	"Ambient/Locs/Subway/Train/Impact", "Ambient/Locs/Subway/Train/Move",
	"Weapons/Knife/Swing1", "Weapons/Knife/Swing2", "Weapons/Knife/Swing3",
	"Weapons/Knife/Impact",
	"Weapons/Knife/Throw",
	"Player/Land/Grass", "Player/Land/Gravel", "Player/Land/Metal", "Player/Land/Sand",
	"Player/Land/Snow", "Player/Land/Stone", "Player/Land/Water", "Player/Land/Wood",
	"impact/surfs/metal1", "impact/surfs/metal2", "impact/surfs/metal3",
	"Impact/GlassBreak/GBBig", "Impact/GlassBreak/GBMed", "Impact/GlassBreak/GBSmall",
	"Impact/Break/Metal", "Impact/Break/Stone", "Impact/Break/Wood",
	"Ambient/Gen/Electric/buzz",
	"Impact/Explosion/Big", "Impact/Explosion/Med", "Impact/Explosion/Small",
	"Ambient/Models/Phone/Hit",
	"Ambient/Models/TrashCan/LidSpin",
	"weapons/FX/Tink/Metal1", "weapons/FX/Tink/Metal2", "weapons/FX/Tink/Metal3",
	"weapons/FX/Tink/Gen1", "weapons/FX/Tink/Gen2",
	"Impact/Gore/Fall1", "Impact/Gore/Fall2", "Impact/Gore/Fall3",
	"weapons/FX/Crack/Crack1", "weapons/FX/Crack/Crack2", "weapons/FX/Crack/Crack3",
	"enemy/Helicopter/Fire1", "enemy/Helicopter/Fire2", "enemy/Helicopter/Fire3",
	"weapons/FX/Tink/shot1", "weapons/FX/Tink/shot2", "weapons/FX/Tink/shot3",
	"weapons/FX/Tink/hvy1", "weapons/FX/Tink/hvy2", "weapons/FX/Tink/hvy3"
};

static struct sfx_s *fx_sound_cache[FX_NUM_SOUNDS];
static qboolean fx_sound_tried[FX_NUM_SOUNDS];

void
FX_ClearSoundList(void)
{
	memset(fx_sound_cache, 0, sizeof(fx_sound_cache));
	memset(fx_sound_tried, 0, sizeof(fx_sound_tried));
}

/* sounds are registered on first use (SPEC-401) */
struct sfx_s *
FX_ListSound(int index)
{
	char name[MAX_QPATH];

	if ((index < 0) || (index >= FX_NUM_SOUNDS) || !fx_sound_names[index])
	{
		return NULL;
	}

	if (!fx_sound_tried[index])
	{
		fx_sound_tried[index] = true;
		Com_sprintf(name, sizeof(name), "%s.wav", fx_sound_names[index]);
		fx_sound_cache[index] = S_RegisterSound(name);
	}

	return fx_sound_cache[index];
}

void
FX_PlaySfx(struct sfx_s *sfx, const vec3_t org, float vol, float atten, int ent,
		int chan, float timeofs)
{
	vec3_t o;

	if (!sfx)
	{
		return;
	}

	if (vol > 1)
	{
		vol = 1;
	}

	if (vol <= 0)
	{
		return;
	}

	if (org)
	{
		VectorCopy(org, o);
		S_StartSound(o, ent, chan, sfx, vol, atten, timeofs);
	}
	else
	{
		S_StartSound(NULL, ent, chan, sfx, vol, atten, timeofs);
	}
}

void
FX_PlaySound(int index, const vec3_t org, float vol, float atten, int ent, int chan,
		float timeofs)
{
	FX_TraceLog("  sound %d %s vol %.3f atten %.2f\n", index,
			(index >= 0 && index < FX_NUM_SOUNDS && fx_sound_names[index]) ?
			fx_sound_names[index] : "?", vol, atten);
	FX_PlaySfx(FX_ListSound(index), org, vol, atten, ent, chan, timeofs);
}

/* ---- client sprite list (SPEC-401) ----------------------------------------- */

static const char *fx_sprite_names[FX_NUM_SPRITES] = {
	"smoke2", "bldglob4", "bldglob5", "bldglob6", "blood_pool2", "footstep", "footstp2",
	"lit", "ring1", "dirt", "sparkpart", "water", "powder", "bhole2", "lightning",
	"sparkpartblue", "flare", "waterdrop", "dent", "rain", "mflash", "shock2", "shock3",
	"shock4", "boom3", "boom5", "scorch", "toxic_ooze", "explode1", "scorchwht", "snow",
	"bubble", "pipeleft", "bhole_glass3", "slash", "ring2", "beam", "flash1", "mflash3",
	"mflash4", "flare2", "smoke3", "bhole_mtl", "bhole_wd", "minimiflash1",
	"minimiflash2", "minimiflash3", "expfire1", "expfire2", "expfire3", "expfire4",
	"smkgrn", "smkgry", "smkwht", "firestreak", "whitestreak", "puddle"
};

static char fx_sprite_paths[FX_NUM_SPRITES][MAX_QPATH];

const char *
FX_SpriteName(int index)
{
	if ((index < 0) || (index >= FX_NUM_SPRITES))
	{
		return NULL;
	}

	if (!fx_sprite_paths[index][0])
	{
		Com_sprintf(fx_sprite_paths[index], MAX_QPATH, "textures/sprites/%s",
				fx_sprite_names[index]);
	}

	return fx_sprite_paths[index];
}

/* Sprite registration goes through renderer slot 0x28 (SPEC-002, fx_scene.c);
   the renderer also registers alpha-quad textures itself on first use
   (SPEC-412 s.8). */
void
FX_RegisterSpriteName(const char *name)
{
	FX_RegisterSpriteImage(name);
}

void
FX_RegisterSprites(void)
{
	int i;

	for (i = 0; i < FX_NUM_SPRITES; i++)
	{
		FX_RegisterSpriteName(FX_SpriteName(i));
	}
}

/* ---- entities --------------------------------------------------------------- */

qboolean
FX_EntityInFrame(int num)
{
	if ((num <= 0) || (num >= cl_numentities))
	{
		return false;
	}

	return cl_entities[num].serverframe == cl.frame.serverframe;
}

const entity_state_t *
FX_EntityCurrent(int num)
{
	int i, n;
	entity_state_t *s;

	for (i = 0; i < cl.frame.num_entities; i++)
	{
		n = (cl.frame.parse_entities + i) & (MAX_PARSE_ENTITIES - 1);
		s = &cl_parse_entities[n];

		if (s->number == num)
		{
			return s;
		}
	}

	return NULL;
}

qboolean
FX_IsLocalPlayer(int num)
{
	return num == cl.playernum + 1;
}

void
FX_EntityLerpOrigin(int num, vec3_t out)
{
	centity_t *c;
	int i;

	if ((num <= 0) || (num >= cl_numentities))
	{
		VectorClear(out);
		return;
	}

	c = &cl_entities[num];

	for (i = 0; i < 3; i++)
	{
		out[i] = c->prev.origin[i] + cl.lerpfrac * (c->current.origin[i] - c->prev.origin[i]);
	}
}

void
FX_EntityLerpAngles(int num, vec3_t out)
{
	centity_t *c;
	int i;

	if ((num <= 0) || (num >= cl_numentities))
	{
		VectorClear(out);
		return;
	}

	c = &cl_entities[num];

	for (i = 0; i < 3; i++)
	{
		out[i] = LerpAngle(c->prev.angles[i], c->current.angles[i], cl.lerpfrac);
	}
}

/* ---- verification log (cvar om_fxtrace) -------------------------------------- */

void
FX_TraceLog(const char *fmt, ...)
{
	va_list ap;
	char text[1024];

	if (!fx_trace || !fx_trace->value)
	{
		return;
	}

	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	Com_Printf("%s", text);
}

/* ---- test captures (OpenMerc, not in 1.07f) ------------------------------------
   om_fxshot "<list>" (temp-entity numbers, "e" = any svc_effect, "*" = both, separated by
   spaces): such a message starts a burst of `omshot` captures (cl_sofscreen.c, at most one
   per rendered frame) named fx<burst>_t<type>_<ms after it> (svc_effect:
   fx<burst>_e<effect index>_<ms>), every om_fxshotstep ms for om_fxshotdur ms; at most
   om_fxshotmax bursts while the cvar keeps its value. A message during a burst is ignored.
   Used to pair OpenMerc frames with frames of the original client at the same moment of an
   effect (test/fxpairs.py). */

static cvar_t *fx_shot, *fx_shotdur, *fx_shotstep, *fx_shotmax;
static int fx_shot_bursts, fx_shot_start = -1, fx_shot_last, fx_shot_type;
static char fx_shot_kind = 't';
static char fx_shot_list[256];

void
FX_ShotInit(void)
{
	fx_shot = Cvar_Get("om_fxshot", "", 0);
	fx_shotdur = Cvar_Get("om_fxshotdur", "1000", 0);
	fx_shotstep = Cvar_Get("om_fxshotstep", "30", 0);
	fx_shotmax = Cvar_Get("om_fxshotmax", "8", 0);
}

static qboolean
FX_ShotWanted(char kind, int type)
{
	const char *s = fx_shot->string;

	while (*s)
	{
		while (*s == ' ' || *s == ',')
		{
			s++;
		}

		if (*s == '*')
		{
			return true;
		}

		if ((kind == 't') && (*s >= '0') && (*s <= '9') && (atoi(s) == type))
		{
			return true;
		}

		if ((kind == 'e') && (*s == 'e'))
		{
			return true;
		}

		while (*s && *s != ' ' && *s != ',')
		{
			s++;
		}
	}

	return false;
}

void
FX_ShotEvent(char kind, int type)
{
	if (!fx_shot || !fx_shot->string[0])
	{
		return;
	}

	if (strcmp(fx_shot_list, fx_shot->string))
	{
		Q_strlcpy(fx_shot_list, fx_shot->string, sizeof(fx_shot_list));
		fx_shot_bursts = 0;
		fx_shot_start = -1;
	}

	if ((fx_shot_start >= 0) || (fx_shot_bursts >= (int)fx_shotmax->value) || !FX_ShotWanted(kind, type))
	{
		return;
	}

	fx_shot_bursts++;
	fx_shot_start = cls.realtime;
	fx_shot_last = -100000;
	fx_shot_type = type;
	fx_shot_kind = kind;
	Com_Printf("OMFXSHOT burst %d %c%d\n", fx_shot_bursts, kind, type);
}

void
FX_ShotFrame(void)
{
	int ms;

	if ((fx_shot_start < 0) || !fx_shot)
	{
		return;
	}

	ms = cls.realtime - fx_shot_start;

	if ((ms > (int)fx_shotdur->value) || (cls.state != ca_active))
	{
		fx_shot_start = -1;
		return;
	}

	if (ms - fx_shot_last < (int)fx_shotstep->value)
	{
		return;
	}

	fx_shot_last = ms;
	/* in front of the command buffer (it may hold a long chain of waits), so it runs at the
	   next command execution, before the next wait; not executed here, because tokenising a
	   command in the middle of the frame frees the arguments of the last tokenised line */
	Cbuf_InsertText(va("omshot fx%02d_%c%d_%04d\n", fx_shot_bursts, fx_shot_kind, fx_shot_type, ms));
}
