/*
 * Copyright (C) 1997-2001 Id Software, Inc.
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
 *  =======================================================================
 *
 * This file implements the camera, e.g the player's view
 *
 * =======================================================================
 *
 * OpenMerc: for Soldier of Fortune the view values come from
 * cl_sofview.c; level registration reports the loading progress and
 * registers the HUD pictures; the crosshair is drawn by the HUD; cl_stats
 * prints the SoF line.
 *
 * Implements: SPEC-310 (sections 3, 9, 10)
 */

#include "header/client.h"
#include "header/sofclient.h"
#include "input/header/input.h"
#include "fx/cl_fx.h" /* OpenMerc (I5) */
#include "../common/header/sof/refapi.h"

/* development tools for weapons */
int gun_frame;
struct model_s *gun_model;

cvar_t *crosshair;
cvar_t *crosshair_3d;
cvar_t *crosshair_3d_glow;

cvar_t *crosshair_scale;
cvar_t *crosshair_color_r;
cvar_t *crosshair_color_g;
cvar_t *crosshair_color_b;
cvar_t *cl_testparticles;
cvar_t *cl_testentities;
cvar_t *cl_testlights;
cvar_t *cl_testblend;
cvar_t *crosshair_3d_glow_r;
cvar_t *crosshair_3d_glow_g;
cvar_t *crosshair_3d_glow_b;

cvar_t *cl_stats;

static int r_numdlights;
static dlight_t r_dlights[MAX_DLIGHTS];

static int r_numentities;
static entity_t r_entities[MAX_ENTITIES];

static int r_numparticles;
static particle_t r_particles[MAX_PARTICLES];

static lightstyle_t r_lightstyles[MAX_LIGHTSTYLES];

char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
int num_cl_weaponmodels;

/*
 * Specifies the model that will be used as the world
 */
void
V_ClearScene(void)
{
	r_numdlights = 0;
	r_numentities = 0;
	r_numparticles = 0;
}

void
V_AddEntity(entity_t *ent)
{
	if (r_numentities >= MAX_ENTITIES)
	{
		return;
	}

	r_entities[r_numentities++] = *ent;
}

void
V_AddParticle(vec3_t org, unsigned int color, float alpha)
{
	particle_t *p;

	if (r_numparticles >= MAX_PARTICLES)
	{
		return;
	}

	p = &r_particles[r_numparticles++];
	VectorCopy(org, p->origin);
	p->color = color;
	p->alpha = alpha;
}

void
V_AddLight(vec3_t org, float intensity, float r, float g, float b)
{
	dlight_t *dl;

	if (r_numdlights >= MAX_DLIGHTS)
	{
		return;
	}

	dl = &r_dlights[r_numdlights++];
	VectorCopy(org, dl->origin);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}

void
V_AddLightStyle(int style, float r, float g, float b)
{
	lightstyle_t *ls;

	if ((style < 0) || (style > MAX_LIGHTSTYLES))
	{
		Com_Error(ERR_DROP, "Bad light style %i", style);
	}

	ls = &r_lightstyles[style];

	ls->white = r + g + b;
	ls->rgb[0] = r;
	ls->rgb[1] = g;
	ls->rgb[2] = b;
}

/*
 *If cl_testparticles is set, create 4096 particles in the view
 */
static void
V_TestParticles(void)
{
	particle_t *p;
	int i, j;
	float d, r, u;

	r_numparticles = MAX_PARTICLES;

	for (i = 0; i < r_numparticles; i++)
	{
		d = i * 0.25f;
		r = 4 * ((i & 7) - 3.5f);
		u = 4 * (((i >> 3) & 7) - 3.5f);
		p = &r_particles[i];

		for (j = 0; j < 3; j++)
		{
			p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * d +
						   cl.v_right[j] * r + cl.v_up[j] * u;
		}

		p->color = 8;
		p->alpha = cl_testparticles->value;
	}
}

/*
 * If cl_testentities is set, create 32 player models
 */
static void
V_TestEntities(void)
{
	int i, j;
	float f, r;
	entity_t *ent;

	r_numentities = 32;
	memset(r_entities, 0, sizeof(r_entities));

	for (i = 0; i < r_numentities; i++)
	{
		ent = &r_entities[i];

		r = 64.0f * ((float)(i % 4) - 1.5f);
		f = (float)(64 * (i / 4) + 128);

		for (j = 0; j < 3; j++)
		{
			ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
							 cl.v_right[j] * r;
		}

		ent->model = cl.baseclientinfo.model;
		ent->skin = cl.baseclientinfo.skin;
	}
}

/*
 * If cl_testlights is set, create 32 lights models
 */
static void
V_TestLights(void)
{
	int i;

	r_numdlights = 32;
	memset(r_dlights, 0, sizeof(r_dlights));

	for (i = 0; i < r_numdlights; i++)
	{
		dlight_t *dl;
		float f, r;
		int j;

		dl = &r_dlights[i];

		r = 64 * ((i % 4) - 1.5f);
		f = 64 * (i / 4.0f) + 128;

		for (j = 0; j < 3; j++)
		{
			dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
							cl.v_right[j] * r;
		}

		dl->color[0] = (float)(((i % 6) + 1) & 1);
		dl->color[1] = (float)((((i % 6) + 1) & 2) >> 1);
		dl->color[2] = (float)((((i % 6) + 1) & 4) >> 2);
		dl->intensity = 200;
	}
}

/*
 * Call before entering a new level, or after changing dlls
 */
void
CL_PrepRefresh(void)
{
	char mapname[MAX_QPATH];
	int i;
	char *name;
	float rotate;
	vec3_t axis;

	if (!cl.configstrings[CS_MODELS + 1][0])
	{
		return;
	}

	SCR_AddDirtyPoint(0, 0);
	SCR_AddDirtyPoint(viddef.width - 1, viddef.height - 1);

	/* let the refresher load the map */
	Q_strlcpy(mapname, cl.configstrings[CS_MODELS + 1] + 5, sizeof(mapname)); /* skip "maps/" */
	mapname[strlen(mapname) - 4] = 0; /* cut off ".bsp" */

	/* register models, pics, and skins */
	Com_Printf("Map: %s\r", mapname);

	if (CL_SoF_Active())
	{
		SCR_SoF_LoadingProgress(0.70f); /* OpenMerc: SPEC-310 section 9 */
	}
	else
	{
		SCR_UpdateScreen();
	}

	R_BeginRegistration (mapname);

	/* precache status bar pics */
	Com_Printf("pics\r");

	if (CL_SoF_Active())
	{
		SCR_SoF_LoadingProgress(0.75f);
		HUD_RegisterMedia(); /* OpenMerc: HUD pictures and positions */
	}
	else
	{
		SCR_UpdateScreen();
		SCR_TouchPics();
	}

	CL_RegisterTEntModels();

	num_cl_weaponmodels = 1;
	strcpy(cl_weaponmodels[0], "weapon.md2");

	Com_Printf("models\r");
	SCR_UpdateScreen();

	for (i = 1; i < MAX_MODELS && cl.configstrings[CS_MODELS + i][0]; i++)
	{
		name = cl.configstrings[CS_MODELS + i];

		if (*name == '#')
		{
			/* special player weapon model */
			if (num_cl_weaponmodels < MAX_CLIENTWEAPONMODELS)
			{
				Q_strlcpy(cl_weaponmodels[num_cl_weaponmodels],
						cl.configstrings[CS_MODELS + i] + 1,
						sizeof(cl_weaponmodels[num_cl_weaponmodels]));

				num_cl_weaponmodels++;
			}
		}
		else
		{
			cl.model_draw[i] = R_RegisterModel(cl.configstrings[CS_MODELS + i]);

			cl.model_clip[i] = (*name == '*') ?
				CM_InlineModel(name) :
				NULL;
		}
	}

	Com_Printf("images\r");
	SCR_UpdateScreen();

	for (i = 1; i < MAX_IMAGES && cl.configstrings[CS_IMAGES + i][0]; i++)
	{
		cl.image_precache[i] = Draw_FindPic(cl.configstrings[CS_IMAGES + i]);
	}

	Com_Printf("clients\r");
	SCR_UpdateScreen();

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (cl.configstrings[CS_PLAYERSKINS + i][0])
		{
			CL_ParseClientinfo(i);
		}
	}

	CL_LoadClientinfo(&cl.baseclientinfo, "unnamed\\male/grunt");

	/* set sky textures and speed */
	Com_Printf("sky\r");
	SCR_UpdateScreen();
	rotate = (float)strtod(cl.configstrings[CS_SKYROTATE], (char **)NULL);
	sscanf(cl.configstrings[CS_SKYAXIS], "%f %f %f", &axis[0], &axis[1], &axis[2]);
	R_SetSky(cl.configstrings[CS_SKY], rotate, axis);

	/* SPEC-002 registration step 6: the terrain set (configstring 14, worldspawn "terrainset"); without it the
	   trn1 landscape outside the train was never built */
	if (CL_SoF_Active())
	{
		extern const sofrefexport_t *HUD_Ref(void);
		const sofrefexport_t *r = HUD_Ref();

		if (r && r->SetTerrain)
		{
			char name[64];

			Q_strlcpy(name, CL_SoF_ConfigString(14), sizeof(name));
			r->SetTerrain(name);
		}
	}

	Com_Printf("                                     \r");

	if (CL_SoF_Active())
	{
		SCR_SoF_LoadingProgress(0.95f);
	}

	/* the renderer can now free unneeded stuff */
	R_EndRegistration();

	if (CL_SoF_Active())
	{
		SCR_SoF_LoadingProgress(1.0f);
	}

	/* clear any lines of console text */
	Con_ClearNotify();

	SCR_UpdateScreen();
	cl.refresh_prepped = true;
	cl.force_refdef = true; /* make sure we have a valid refdef */
}

float
CalcFov(float fov_x, float width, float height)
{
	float a;
	float x;

	if ((fov_x < 1) || (fov_x > 179))
	{
		Com_Error(ERR_DROP, "Bad fov: %f", fov_x);
	}

	x = width / (float)tan(fov_x / 360 * M_PI);

	a = (float)atan(height / x);

	a = a * 360 / M_PI;

	return a;
}

/* gun frame debugging functions */
static void
V_Gun_Next_f(void)
{
	gun_frame++;
	Com_Printf("frame %i\n", gun_frame);
}

static void
V_Gun_Prev_f(void)
{
	gun_frame--;

	if (gun_frame < 0)
	{
		gun_frame = 0;
	}

	Com_Printf("frame %i\n", gun_frame);
}

static void
V_Gun_Model_f(void)
{
	char name[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		gun_model = NULL;
		return;
	}

	Com_sprintf(name, sizeof(name), "models/%s/tris.md2", Cmd_Argv(1));
	gun_model = R_RegisterModel(name);
}

static int
entitycmpfnc(const entity_t *a, const entity_t *b)
{
	/* all other models are sorted by model then skin */
	if (a->model == b->model)
	{
		return (a->skin == b->skin) ? 0 :
			(a->skin > b->skin) ? 1 : -1;
	}
	else
	{
		return (a->model > b->model) ? 1 : -1;
	}
}

static void
V_Render3dCrosshair(void)
{
	trace_t crosshair_trace;
	vec3_t end;

	crosshair_3d = Cvar_Get("crosshair_3d", "0", CVAR_ARCHIVE);
	crosshair_3d_glow = Cvar_Get("crosshair_3d_glow", "0", CVAR_ARCHIVE);


	if(crosshair_3d->value || crosshair_3d_glow->value){
		VectorMA(cl.refdef.vieworg, 8192, cl.v_forward,end);
		crosshair_trace = CL_PMTrace(cl.refdef.vieworg, vec3_origin, vec3_origin, end);

		if(crosshair_3d_glow->value){
			crosshair_3d_glow_r = Cvar_Get("crosshair_3d_glow_r", "5", CVAR_ARCHIVE);
			crosshair_3d_glow_g = Cvar_Get("crosshair_3d_glow_g", "1", CVAR_ARCHIVE);
			crosshair_3d_glow_b = Cvar_Get("crosshair_3d_glow_b", "4", CVAR_ARCHIVE);

			V_AddLight(
				crosshair_trace.endpos,
				crosshair_3d_glow->value,
				crosshair_3d_glow_r->value,
				crosshair_3d_glow_g->value,
				crosshair_3d_glow_b->value
			);
		}

		if(crosshair_3d->value){
			entity_t crosshair_ent = {0};

			crosshair_ent.origin[0] = crosshair_trace.endpos[0];
			crosshair_ent.origin[1] = crosshair_trace.endpos[1];
			crosshair_ent.origin[2] = crosshair_trace.endpos[2];

			crosshair_ent.model = R_RegisterModel("models/crosshair/tris.md2");
			//crosshair_ent.skin = R_RegisterSkin("models/crosshair/skin.pcx");

			AngleVectors2(crosshair_trace.plane.normal, crosshair_ent.angles);
			crosshair_ent.flags = RF_DEPTHHACK | RF_FULLBRIGHT | RF_NOSHADOW;

			V_AddEntity(&crosshair_ent);
		}
	}
}

/*
 * openMERC GFX-modern: thunder for the renderer's lightning (r_om_rain). The game has no thunder sound, so three
 * are synthesised once into <gamedir>/sound/om/: brown-noise rumble with a few random rolls under a slow decay,
 * the third with a sharp crack in front (close strikes). The renderer reports each strike in the cvar
 * r_om_lightning_strike ("number distance"); the thunder follows after distance / 1200 s, quieter when far.
 */
#define THUNDER_RATE 22050
#define THUNDER_SECONDS 5

static float
Thunder_Rand(unsigned *s)
{
	*s = *s * 1664525u + 1013904223u;
	return (float)((*s >> 8) & 0xffff) / 32768.0f - 1.0f;
}

static void
Thunder_Write(const char *path, int variant)
{
	int n = THUNDER_RATE * THUNDER_SECONDS, i, k;
	float *f = malloc(n * sizeof(float)), b1 = 0, b2 = 0, peak = 1e-6f, rolls[6][3];
	short *pcm = malloc(n * sizeof(short));
	unsigned seed = 12345u + 7919u * (unsigned)variant;
	FILE *out;

	if (!f || !pcm)
	{
		free(f);
		free(pcm);
		return;
	}

	for (k = 0; k < 6; k++)
	{
		rolls[k][0] = 0.1f + (Thunder_Rand(&seed) * 0.5f + 0.5f) * 2.8f; /* time */
		rolls[k][1] = 0.15f + (Thunder_Rand(&seed) * 0.5f + 0.5f) * 0.6f; /* width */
		rolls[k][2] = 0.4f + (Thunder_Rand(&seed) * 0.5f + 0.5f) * 0.6f;  /* strength */
	}

	for (i = 0; i < n; i++)
	{
		float t = (float)i / THUNDER_RATE, w = Thunder_Rand(&seed), env, roll = 0.25f, v;

		/* brown noise, low-passed twice: the rumble */
		b1 = b1 * 0.995f + w * 0.06f;
		b2 = b2 * 0.97f + b1 * 0.03f;

		for (k = 0; k < 6; k++)
		{
			float d = (t - rolls[k][0]) / rolls[k][1];
			roll += rolls[k][2] * expf(-d * d);
		}

		env = (1.0f - expf(-t * 12.0f)) * expf(-t / 1.6f);
		v = b2 * roll * env * 12.0f + b1 * env * 0.6f;

		if (variant == 2)
		{
			/* the crack: bright noise with a fast decay */
			v += w * 0.9f * expf(-t / 0.07f) + b1 * 2.5f * expf(-t / 0.35f);
		}

		f[i] = v;
		peak = Q_max(peak, fabsf(v));
	}

	for (i = 0; i < n; i++)
	{
		float s = f[i] / peak * 0.9f;

		/* fade the tail to silence */
		if (i > n - THUNDER_RATE / 2)
		{
			s *= (float)(n - i) / (THUNDER_RATE / 2);
		}

		pcm[i] = (short)(s * 32767.0f);
	}

	out = Q_fopen(path, "wb");

	if (out)
	{
		int datalen = n * 2, riff = 36 + datalen, fmtlen = 16, rate = THUNDER_RATE, brate = THUNDER_RATE * 2;
		short fmt = 1, ch = 1, align = 2, bits = 16;

		fwrite("RIFF", 1, 4, out);
		fwrite(&riff, 4, 1, out);
		fwrite("WAVEfmt ", 1, 8, out);
		fwrite(&fmtlen, 4, 1, out);
		fwrite(&fmt, 2, 1, out);
		fwrite(&ch, 2, 1, out);
		fwrite(&rate, 4, 1, out);
		fwrite(&brate, 4, 1, out);
		fwrite(&align, 2, 1, out);
		fwrite(&bits, 2, 1, out);
		fwrite("data", 1, 4, out);
		fwrite(&datalen, 4, 1, out);
		fwrite(pcm, 2, n, out);
		fclose(out);
	}

	free(f);
	free(pcm);
}

static void
CL_Thunder_Frame(void)
{
	static cvar_t *strike;
	static int lastseq = -1, pending;
	static float due, dist;
	static qboolean made;
	int seq = 0;
	float d = 0;

	if (!strike)
	{
		strike = Cvar_Get("r_om_lightning_strike", "0 0", 0);
	}

	if (sscanf(strike->string, "%d %f", &seq, &d) == 2 && seq != lastseq)
	{
		if (lastseq >= 0 && seq > 0)
		{
			pending = 1;
			dist = d;
			due = cls.realtime * 0.001f + d / 1200.0f;
		}

		lastseq = seq;
	}

	if (!pending || cls.realtime * 0.001f < due)
	{
		return;
	}

	pending = 0;

	if (!made)
	{
		char path[MAX_OSPATH];
		int i;

		for (i = 0; i < 3; i++)
		{
			FILE *t;

			Com_sprintf(path, sizeof(path), "%s/sound/om/thunder%d.wav", FS_Gamedir(), i);
			t = Q_fopen(path, "rb");

			if (t)
			{
				fclose(t);
				continue;
			}

			FS_CreatePath(path);
			Thunder_Write(path, i);
		}

		made = true;
	}

	{
		char name[64];
		struct sfx_s *sfx;
		int v = (dist < 1900) ? 2 : (rand() & 1);
		float vol = Q_clamp(1.4f - dist / 3500.0f, 0.4f, 1.0f);

		Com_sprintf(name, sizeof(name), "om/thunder%d.wav", v);
		sfx = S_RegisterSound(name);

		if (sfx)
		{
			S_StartSound(NULL, cl.playernum + 1, 0, sfx, vol, 0, 0);
		}
	}
}

void
V_RenderView(float stereo_separation)
{
	if (cls.state != ca_active)
	{
		R_EndWorldRenderpass();
		return;
	}

	if (!cl.refresh_prepped)
	{
		R_EndWorldRenderpass();
		return;			// still loading
	}

	CL_Thunder_Frame();

	if (cl_timedemo->value)
	{
		if (!cl.timedemo_start)
		{
			cl.timedemo_start = Sys_Milliseconds();
		}

		cl.timedemo_frames++;
	}

	/* an invalid frame will just use the exact previous refdef
	   we can't use the old frame if the video mode has changed, though... */
	if (cl.frame.valid && (cl.force_refdef || !cl_paused->value))
	{
		cl.force_refdef = false;

		V_ClearScene();

		/* build a refresh entity list and calc cl.sim*
		   this also calls CL_CalcViewValues which loads
		   v_forward, etc. */
		CL_AddEntities();

		// before changing viewport we should trace the crosshair position
		V_Render3dCrosshair();

		if (cl_testparticles->value)
		{
			V_TestParticles();
		}

		if (cl_testentities->value)
		{
			V_TestEntities();
		}

		if (cl_testlights->value)
		{
			V_TestLights();
		}

		if (cl_testblend->value)
		{
			cl.refdef.blend[0] = 1;
			cl.refdef.blend[1] = 0.5;
			cl.refdef.blend[2] = 0.25;
			cl.refdef.blend[3] = 0.5;
		}

		/* offset vieworg appropriately if
		   we're doing stereo separation */

		if (stereo_separation != 0)
		{
			vec3_t tmp;

			VectorScale(cl.v_right, stereo_separation, tmp);
			VectorAdd(cl.refdef.vieworg, tmp, cl.refdef.vieworg);
		}

		/* never let it sit exactly on a node line, because a water plane can
		   dissapear when viewed with the eye exactly on it. the server protocol
		   only specifies to 1/8 pixel, so add 1/16 in each axis */
		cl.refdef.vieworg[0] += 1.0 / 16;
		cl.refdef.vieworg[1] += 1.0 / 16;
		cl.refdef.vieworg[2] += 1.0 / 16;

		cl.refdef.time = cl.time * 0.001f;

		cl.refdef.areabits = cl.frame.areabits;

		if (!cl_add_entities->value)
		{
			r_numentities = 0;
		}

		if (!cl_add_particles->value)
		{
			r_numparticles = 0;
		}

		if (!cl_add_lights->value)
		{
			r_numdlights = 0;
		}

		if (!cl_add_blend->value)
		{
			VectorClear(cl.refdef.blend);
		}

		cl.refdef.num_entities = r_numentities;
		cl.refdef.entities = r_entities;
		cl.refdef.num_particles = r_numparticles;
		cl.refdef.particles = r_particles;
		cl.refdef.num_dlights = r_numdlights;
		cl.refdef.dlights = r_dlights;
		cl.refdef.lightstyles = r_lightstyles;

		cl.refdef.rdflags = cl.frame.playerstate.rdflags;

		/* sort entities for better cache locality */
		qsort(cl.refdef.entities, cl.refdef.num_entities,
				sizeof(cl.refdef.entities[0]), (int (*)(const void *, const void *))
				entitycmpfnc);
	}

	cl.refdef.x = scr_vrect.x;
	cl.refdef.y = scr_vrect.y;
	cl.refdef.width = scr_vrect.width;
	cl.refdef.height = scr_vrect.height;
	cl.refdef.fov_y = CalcFov(cl.refdef.fov_x, (float)cl.refdef.width,
				(float)cl.refdef.height);

	CLFX_RenderFrame(&cl.refdef); /* OpenMerc (I5): SPEC-002 frame with the effects */

	if (cl_stats->value)
	{
		if (CL_SoF_Active())
		{
			/* SPEC-310 section 10 (no alpha quads or quad chains yet) */
			Com_Printf("ent %i lt %i alphaquads %i quadchains %i (quads %i)\n",
					r_numentities, r_numdlights, 0, 0, 0);
		}
		else
		{
			Com_Printf("ent:%i  lt:%i  part:%i\n", r_numentities,
					r_numdlights, r_numparticles);
		}
	}

	if (log_stats->value && (log_stats_file != 0))
	{
		fprintf(log_stats_file, "%i,%i,%i,", r_numentities,
				r_numdlights, r_numparticles);
	}

	SCR_AddDirtyPoint(scr_vrect.x, scr_vrect.y);
	SCR_AddDirtyPoint(scr_vrect.x + scr_vrect.width - 1,
			scr_vrect.y + scr_vrect.height - 1);

	if (!CL_SoF_Active())
	{
		SCR_DrawCrosshair(); /* OpenMerc: the SoF crosshair is part of the HUD */
	}
}

static void
V_Viewpos_f(void)
{
	Com_Printf("position: %i %i %i, angles: %i %i %i\n",
			(int)cl.refdef.vieworg[0], (int)cl.refdef.vieworg[1],
			(int)cl.refdef.vieworg[2],
			(int)cl.refdef.viewangles[PITCH], (int)cl.refdef.viewangles[YAW],
			(int)cl.refdef.viewangles[ROLL]);
}

void
V_Init(void)
{
	Cmd_AddCommand("gun_next", V_Gun_Next_f);
	Cmd_AddCommand("gun_prev", V_Gun_Prev_f);
	Cmd_AddCommand("gun_model", V_Gun_Model_f);

	Cmd_AddCommand("viewpos", V_Viewpos_f);

	crosshair = Cvar_Get("crosshair", "0", CVAR_ARCHIVE);
	crosshair_scale = Cvar_Get("crosshair_scale", "-1", CVAR_ARCHIVE);
	crosshair_color_r = Cvar_Get("crosshair_color_r", "1", CVAR_ARCHIVE);
	crosshair_color_g = Cvar_Get("crosshair_color_g", "1", CVAR_ARCHIVE);
	crosshair_color_b = Cvar_Get("crosshair_color_b", "1", CVAR_ARCHIVE);
	cl_testblend = Cvar_Get("cl_testblend", "0", 0);
	cl_testparticles = Cvar_Get("cl_testparticles", "0", 0);
	cl_testentities = Cvar_Get("cl_testentities", "0", 0);
	cl_testlights = Cvar_Get("cl_testlights", "0", 0);

	cl_stats = Cvar_Get("cl_stats", "0", 0);
}

