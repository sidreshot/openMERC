/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: Ghoul model entities of the 3D view (package I4b).
 *
 * An entity with render flag 0x400000 carries a Ghoul instance handle in the
 * low 16 bits of its frame (taken as signed: instances the client creates
 * itself have negative handles). The renderer finds the instance through the
 * Ghoul system it received at start-up (system method 9), fills the 180-byte
 * render-parameter block of SPEC-011 (light colour from the world light
 * point, probe vector, lighting options, shadow and fog parameters, entity
 * matrix, view), heads it with its render-cull object and calls instance
 * method 41 from the sorted translucent pass. The Ghoul system then draws the
 * instance and its bolt-ons with the GL 1.1 entry-point table the renderer
 * gave it (SPEC-010 G): in the SoF module build on a compatibility context the
 * system's own opengl32 functions, otherwise (core profile, GLES 3, or
 * r_om_ghlemu 1) the emulation of gl3_ghoulemu.c. The menu model views of
 * slot 0xb8 get their starting state through the same back end.
 *
 * The Ghoul interfaces are MSVC thiscall in the 32-bit Windows build: a
 * __fastcall function with a dummy second argument has the same calling
 * sequence (this in ECX, arguments on the stack, callee pops). Elsewhere the
 * object pointer is simply the first argument.
 *
 * Implements: SPEC-411 §4, §5, §7 (renderer side of Ghoul entity drawing),
 *             SPEC-011 (render-parameter block, render-cull object, entity
 *             matrix, instance methods 41 and 51), SPEC-010 (system method 9,
 *             light record), SPEC-002 (entity flags 0x400000, 0x1000, bit 28,
 *             light output pointer, refimport 0x5c ray test),
 *             SPEC-AMEND-002 1.11, 4.1, 4.2, SPEC-410 (light sampling and point
 *             fog used for models), SPEC-412 §2 (state for menu model views)
 */

#include "header/local.h"
#include "header/gl3_ghoul.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* ------------------------------------------------------------------
 * Ghoul interface calls
 * ------------------------------------------------------------------ */

#if defined(_MSC_VER) && defined(_M_IX86)
#define GHL_THIS __fastcall
#define GHL_EDX , void *unused_edx
#define GHL_EDXARG , NULL
#else
#define GHL_THIS
#define GHL_EDX
#define GHL_EDXARG
#endif

#define GHL_VT(obj, n) (((void **)(*(void **)(obj)))[n])

typedef void *(GHL_THIS *ghl_findinst_t)(void *self GHL_EDX, int handle);
typedef void (GHL_THIS *ghl_draw_t)(void *self GHL_EDX, void *params);
typedef void (GHL_THIS *ghl_bounds_t)(void *self GHL_EDX, float time, const ghlmatrix_t *root,
	float *mins, float *maxs, unsigned short part, unsigned char exact);

static void *
Ghl_FindInstance(int handle)
{
	if (!gl3_ghoul || !handle)
	{
		return NULL;
	}

	return ((ghl_findinst_t)GHL_VT(gl3_ghoul, 9))(gl3_ghoul GHL_EDXARG, handle);
}

static void
Ghl_Draw(void *inst, ghlparams_t *p)
{
	((ghl_draw_t)GHL_VT(inst, 41))(inst GHL_EDXARG, p);
}

static void
Ghl_Bounds(void *inst, float time, const ghlmatrix_t *root, float *mins, float *maxs)
{
	((ghl_bounds_t)GHL_VT(inst, 51))(inst GHL_EDXARG, time, root, mins, maxs, 0, 0);
}

/* the exact box (the posed vertices): animated characters give no plain box */
static void
Ghl_BoundsExact(void *inst, float time, const ghlmatrix_t *root, float *mins, float *maxs)
{
	((ghl_bounds_t)GHL_VT(inst, 51))(inst GHL_EDXARG, time, root, mins, maxs, 0, 1);
}

/* ------------------------------------------------------------------
 * render-cull objects (SPEC-011 render parameters, entry 0)
 * ------------------------------------------------------------------ */

/* a box is outside when all 8 corners lie behind one of the four frustum side planes (SPEC-411 §5 step 2) */
static qboolean
CornersOutside(const float (*corners)[3])
{
	int i, k;

	for (i = 0; i < 4; i++)
	{
		const cplane_t *pl = &frustum[i];

		for (k = 0; k < 8; k++)
		{
			if (DotProduct(pl->normal, corners[k]) - pl->dist >= 0)
			{
				break;
			}
		}

		if (k == 8)
		{
			return true;
		}
	}

	return false;
}

static unsigned char GHL_THIS
Cull_Frustum(void *self GHL_EDX, const float (*corners)[3])
{
	(void)self;
	return CornersOutside(corners) ? 1 : 0;
}

static unsigned char GHL_THIS
Cull_Never(void *self GHL_EDX, const float (*corners)[3])
{
	(void)self;
	(void)corners;
	return 0;
}

static void *const cullvt_frustum[1] = {(void *)Cull_Frustum};
static void *const cullvt_never[1] = {(void *)Cull_Never};

/* ------------------------------------------------------------------
 * cvars (created by gl3_sofcvar.c; looked up here)
 * ------------------------------------------------------------------ */

static cvar_t *ghl_max_lights, *ghl_fast_lights, *ghl_light_multiply, *ghl_gllight_local_viewer;
static cvar_t *ghl_gllight_directional, *ghl_gllight_attenuation, *ghl_gllight_max_lights;
static cvar_t *ghl_shadows, *ghl_player_shadow, *ghl_shadow_darkness, *ghl_shadow_dist, *ghl_obj_fog;
static cvar_t *ghl_no_render, *ghl_wireframe, *ghl_no_cull, *ghl_pre_cull, *ghl_no_texture_stack;
static cvar_t *r_om_ghlemu, *r_om_ghldebug, *r_om_tess, *r_om_tess_weapon, *r_om_tess_models, *r_om_ppl_models;
static cvar_t *r_om_tess_round;
static byte roundinst[65536]; /* per instance handle: it drew a round model's texture (r_om_tess) */

static void
GetCvars(void)
{
	if (ghl_max_lights)
	{
		return;
	}

	ghl_max_lights = ri.Cvar_Get("ghl_max_lights", "3", CVAR_ARCHIVE);
	ghl_fast_lights = ri.Cvar_Get("ghl_fast_lights", "0", CVAR_ARCHIVE);
	ghl_light_multiply = ri.Cvar_Get("ghl_light_multiply", "1.3", CVAR_ARCHIVE);
	ghl_gllight_local_viewer = ri.Cvar_Get("ghl_gllight_local_viewer", "1", CVAR_ARCHIVE);
	ghl_gllight_directional = ri.Cvar_Get("ghl_gllight_directional", "0", CVAR_ARCHIVE);
	ghl_gllight_attenuation = ri.Cvar_Get("ghl_gllight_attenuation", "1", CVAR_ARCHIVE);
	ghl_gllight_max_lights = ri.Cvar_Get("ghl_gllight_max_lights", "6", CVAR_ARCHIVE);
	ghl_shadows = ri.Cvar_Get("ghl_shadows", "2", CVAR_ARCHIVE);
	ghl_player_shadow = ri.Cvar_Get("ghl_player_shadow", "1", CVAR_ARCHIVE);
	ghl_shadow_darkness = ri.Cvar_Get("ghl_shadow_darkness", ".75", CVAR_ARCHIVE);
	ghl_shadow_dist = ri.Cvar_Get("ghl_shadow_dist", "25", CVAR_ARCHIVE);
	ghl_obj_fog = ri.Cvar_Get("ghl_obj_fog", "1", CVAR_ARCHIVE);
	ghl_no_render = ri.Cvar_Get("ghl_no_render", "0", 0);
	ghl_wireframe = ri.Cvar_Get("ghl_wireframe", "0", 0);
	ghl_no_cull = ri.Cvar_Get("ghl_no_cull", "0", 0);
	ghl_pre_cull = ri.Cvar_Get("ghl_pre_cull", "1", 0);
	ghl_no_texture_stack = ri.Cvar_Get("ghl_no_texture_stack", "0", CVAR_ARCHIVE);
	/* OpenMerc only: 1 = draw through the GL 1.1 emulation even where a compatibility context exists */
	r_om_ghlemu = ri.Cvar_Get("r_om_ghlemu", "0", 0);
	/* OpenMerc only: print the render-parameter block of every Ghoul entity once */
	r_om_ghldebug = ri.Cvar_Get("r_om_ghldebug", "0", 0);
	/* GFX-modern: curved PN tessellation. r_om_tess 0..10 (fractions allowed) is how strongly the surface
	   is curved: 0 off, 10 the full PN surface (rounder but "inflated"), 8 by default (chosen by eye in
	   game: round characters, no visible inflation, no slowdown). r_om_tess_models is
	   the subdivision of the characters (triangles split into level^2, 2..3), r_om_tess_weapon that of
	   the view weapon (0 off: the stock weapons are faceted with split normals and gain nothing) */
	r_om_tess = ri.Cvar_Get("r_om_tess", "8", CVAR_ARCHIVE);
	r_om_tess_weapon = ri.Cvar_Get("r_om_tess_weapon", "0", CVAR_ARCHIVE);
	r_om_tess_models = ri.Cvar_Get("r_om_tess_models", "3", CVAR_ARCHIVE);
	r_om_tess_round = ri.Cvar_Get("r_om_tess_round", "1", CVAR_ARCHIVE);
	/* GFX-modern: the GL lighting of Ghoul models (ghl_light_method 3) per pixel instead of per vertex */
	r_om_ppl_models = ri.Cvar_Get("r_om_ppl_models", "1", CVAR_ARCHIVE);
}

/* ⌊v + 0.5⌋ clamped to lo..hi (SPEC-411 "rounded") */
static int
RoundClamp(float v, int lo, int hi)
{
	int i = (int)floorf(v + 0.5f);

	return (i < lo) ? lo : ((i > hi) ? hi : i);
}

/* ------------------------------------------------------------------
 * world ray test handed to the Ghoul shadow code (render parameters 0x44)
 * ------------------------------------------------------------------ */

/* SoF module: the engine's import 0x5c, set by gl3_sof.c */
float (*gl3_ghoul_trace)(const float *start, const float *end);

static float tracefrac;

static qboolean
TraceNode(mnode_t *node, float f1, float f2, const vec3_t p1, const vec3_t p2)
{
	float t1, t2, frac, fm;
	vec3_t mid;
	int side, i;

	if (node->contents != CONTENTS_NODE)
	{
		if (((mleaf_t *)node)->contents & (CONTENTS_SOLID | CONTENTS_WINDOW))
		{
			tracefrac = f1;
			return true;
		}

		return false;
	}

	t1 = DotProduct(node->plane->normal, p1) - node->plane->dist;
	t2 = DotProduct(node->plane->normal, p2) - node->plane->dist;

	if ((t1 >= 0) && (t2 >= 0))
	{
		return TraceNode(node->children[0], f1, f2, p1, p2);
	}

	if ((t1 < 0) && (t2 < 0))
	{
		return TraceNode(node->children[1], f1, f2, p1, p2);
	}

	frac = t1 / (t1 - t2);
	fm = f1 + (f2 - f1) * frac;

	for (i = 0; i < 3; i++)
	{
		mid[i] = p1[i] + (p2[i] - p1[i]) * frac;
	}

	side = (t1 < 0);

	if (TraceNode(node->children[side], f1, fm, p1, mid))
	{
		return true;
	}

	return TraceNode(node->children[side ^ 1], fm, f2, mid, p2);
}

/* without an engine ray test (OpenMerc-hosted renderer): a point trace through the world BSP;
   fraction 0..1, -1 when the start is inside a solid */
static float
WorldTrace(const float *start, const float *end)
{
	mleaf_t *leaf;

	if (!gl3_worldmodel || !gl3_worldmodel->nodes)
	{
		return 1.0f;
	}

	leaf = Mod_PointInLeaf(start, gl3_worldmodel->nodes);

	if (leaf && (leaf->contents & (CONTENTS_SOLID | CONTENTS_WINDOW)))
	{
		return -1.0f;
	}

	tracefrac = 1.0f;

	if (!TraceNode(gl3_worldmodel->nodes, 0, 1, start, end))
	{
		return 1.0f;
	}

	return tracefrac;
}

/* ------------------------------------------------------------------
 * GL back end: the fixed-function calls the renderer itself makes around
 * instance method 41 (state of SPEC-411 §5 steps 10-12)
 * ------------------------------------------------------------------ */

static ghlff_t ff;
static qboolean ff_ready, ff_emulated;

#ifdef OM_SOFREF
static qboolean
LoadNative(void)
{
	HMODULE gl = GetModuleHandleA("opengl32.dll");

#define NATIVE(field, name) \
	if (!(*(FARPROC *)&ff.field = GetProcAddress(gl, name))) \
	{ \
		Com_Printf("Ghoul drawing: %s missing\n", name); \
		return false; \
	}

	if (!gl)
	{
		return false;
	}

	NATIVE(Enable, "glEnable");
	NATIVE(Disable, "glDisable");
	NATIVE(IsEnabled, "glIsEnabled");
	NATIVE(MatrixMode, "glMatrixMode");
	NATIVE(LoadMatrixf, "glLoadMatrixf");
	NATIVE(LoadIdentity, "glLoadIdentity");
	NATIVE(Color4f, "glColor4f");
	NATIVE(ShadeModel, "glShadeModel");
	NATIVE(AlphaFunc, "glAlphaFunc");
	NATIVE(TexEnvf, "glTexEnvf");
	NATIVE(LightModeli, "glLightModeli");
	NATIVE(Fogf, "glFogf");
	NATIVE(Fogfv, "glFogfv");
	NATIVE(Fogi, "glFogi");
	NATIVE(PolygonMode, "glPolygonMode");
	NATIVE(DepthRange, "glDepthRange");
	NATIVE(CullFace, "glCullFace");
	NATIVE(BlendFunc, "glBlendFunc");
	NATIVE(DepthMask, "glDepthMask");
	NATIVE(GetIntegerv, "glGetIntegerv");
#undef NATIVE
	return true;
}
#endif

/* chooses the back end once per renderer start (the Ghoul system keeps the table it was given) */
static qboolean
BackEnd(void)
{
	if (ff_ready)
	{
		return true;
	}

	GetCvars();

#ifdef OM_SOFREF
	/* a compatibility context has the GL 1.1 functions; a core-profile one (r_om_coreprofile) does not */
	GLint profile = 0;

	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);

	if (!r_om_ghlemu->value && !(profile & GL_CONTEXT_CORE_PROFILE_BIT))
	{
		if (!LoadNative())
		{
			return false;
		}

		ff_emulated = false;
		ff_ready = true;
		return true;
	}
#endif

	if (!GL3_GhoulEmu_FF(&ff))
	{
		return false;
	}

	ff_emulated = true;
	ff_ready = true;
	return true;
}

/* the table handed to the Ghoul system with system method 1 (gl3_sofapi.c) */
void *
GL3_Ghoul_Table(void)
{
	GetCvars();
	ff_ready = false;

	if (!BackEnd())
	{
		return NULL;
	}

#ifdef OM_SOFREF
	if (!ff_emulated)
	{
		return GL3_Ghoul_GLTable();
	}
#endif

	return GL3_GhoulEmu_Table();
}

void
GL3_Ghoul_Shutdown(void)
{
	ff_ready = false;
	GL3_GhoulEmu_Shutdown();
}

/* ------------------------------------------------------------------
 * menu model views (SPEC-412 §2): the state the original's 2D code leaves
 * for the engine's Ghoul drawing, on either back end
 * ------------------------------------------------------------------ */

void
GL3_Ghoul_MenuEnter(qboolean blend, qboolean modulate, const byte color[4])
{
	if (!BackEnd())
	{
		return;
	}

#ifdef OM_SOFREF
	if (!ff_emulated)
	{
		GL3_Ghoul_EnterFixedFunction(blend, modulate, color);
		return;
	}
#endif

	GL3_GhoulEmu_Begin();
	ff.Enable(GL_TEXTURE_2D);
	ff.Enable(GHL_GL_ALPHA_TEST);
	ff.AlphaFunc(GL_GREATER, 0.05f);
	ff.Disable(GHL_GL_FOG);
	ff.Disable(GHL_GL_LIGHTING);
	ff.ShadeModel(GHL_GL_FLAT);
	ff.TexEnvf(GHL_GL_TEXTURE_ENV, GHL_GL_TEXTURE_ENV_MODE, (float)(modulate ? GHL_GL_MODULATE : GHL_GL_REPLACE));
	ff.Color4f(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f);
	ff.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	(blend ? ff.Enable : ff.Disable)(GL_BLEND);
}

void
GL3_Ghoul_MenuProjection(const float *projection)
{
	if (!BackEnd())
	{
		return;
	}

#ifdef OM_SOFREF
	if (!ff_emulated)
	{
		GL3_Ghoul_SetProjection(projection);
		return;
	}
#endif

	ff.MatrixMode(GHL_GL_PROJECTION);
	ff.LoadMatrixf(projection);
	ff.MatrixMode(GHL_GL_MODELVIEW);
	ff.LoadIdentity();
	ff.ShadeModel(GHL_GL_FLAT);
}

/* back to 2D (slot 0xb4 and the end of every 3D view) */
void
GL3_Ghoul_MenuLeave(void)
{
#ifdef OM_SOFREF
	if (!ff_ready || !ff_emulated)
	{
		GL3_Ghoul_LeaveFixedFunction();
		return;
	}
#endif

	if (ff_ready && ff_emulated)
	{
		GL3_GhoulEmu_End();
		glDepthFunc(GL_LEQUAL);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glDepthRange(gl3depthmin, gl3depthmax);
	}
}

/* ------------------------------------------------------------------
 * per-frame and per-entity work
 * ------------------------------------------------------------------ */

extern vec3_t vpn, vright, vup;

/* B(p) = |L(p)|, L = the light point sampled straight down */
static float
Brightness(const vec3_t p)
{
	vec3_t c;

	GL3_SoF_LightPoint(p, c, 255);
	return VectorLength(c);
}

/* SPEC-411 §5 step 8, probe method: the gradient of the floor light around the origin */
static void
ProbeVector(const entity_t *e, vec3_t dir)
{
	vec3_t a, b, f, r, u, D;
	float gx, gy, gz, q;

	VectorCopy(e->origin, a);
	VectorCopy(e->origin, b);
	a[0] += 16;
	b[0] -= 16;
	gx = 4 * (Brightness(a) - Brightness(b));

	VectorCopy(e->origin, a);
	VectorCopy(e->origin, b);
	a[1] += 16;
	b[1] -= 16;
	gy = 4 * (Brightness(a) - Brightness(b));

	q = gx * gx + gy * gy;

	if (q <= 1)
	{
		gz = sqrtf(1 - q);
	}
	else
	{
		q = sqrtf(q);
		gx /= q;
		gy /= q;
		gz = 0;
	}

	D[0] = 500 * gx;
	D[1] = 500 * gy;
	D[2] = 500 * gz;

	/* in the entity's own axes, from its un-negated angles (SPEC-AMEND-002 4.2) */
	AngleVectors(e->angles, f, r, u);
	dir[0] = DotProduct(D, f);
	dir[1] = DotProduct(D, r);
	dir[2] = DotProduct(D, u);
}

/* SPEC-011 matrix: rows forward, -right, up of the Quake angle vectors of (-pitch, yaw, -roll), origin */
static void
EntityMatrix(const entity_t *e, ghlmatrix_t *m)
{
	vec3_t a, f, r, u;
	int i;

	a[0] = -e->angles[0];
	a[1] = e->angles[1];
	a[2] = -e->angles[2];
	AngleVectors(a, f, r, u);

	for (i = 0; i < 3; i++)
	{
		m->m[0][i] = f[i];
		m->m[1][i] = -r[i];
		m->m[2][i] = u[i];
		m->m[3][i] = e->origin[i];
	}

	m->m[0][3] = m->m[1][3] = m->m[2][3] = 0;
	m->m[3][3] = 1;
	m->flags = 0;
}

static void
BoxCorners(const vec3_t mins, const vec3_t maxs, float corners[8][3])
{
	int k;

	for (k = 0; k < 8; k++)
	{
		corners[k][0] = (k & 1) ? maxs[0] : mins[0];
		corners[k][1] = (k & 2) ? maxs[1] : mins[1];
		corners[k][2] = (k & 4) ? maxs[2] : mins[2];
	}
}

static void
DebugPrint(const entity_t *e, const ghlparams_t *p)
{
	Com_Printf("ghoul ent %d flags %08x org %.0f %.0f %.0f ang %.0f %.0f %.0f light %.2f %.2f %.2f dir %.1f %.1f %.1f "
		"opt %d%d%d%d%d%d%d%d%d%d%d shadow %d %.2f fogvis %.2f maxl %d\n",
		(short)(e->frame & 0xFFFF), e->flags, e->origin[0], e->origin[1], e->origin[2], e->angles[0], e->angles[1],
		e->angles[2], p->lightColor[0], p->lightColor[1], p->lightColor[2], p->lightDir[0], p->lightDir[1],
		p->lightDir[2], p->dirValid, p->useGLLights, p->specular, p->fastLights, p->hardwareLights,
		p->glDirectional, p->glAttenuation, p->castShadow, p->shadowOnly, p->noTextureStack, p->noDynamicLights,
		p->shadowMode, p->shadowDarkness, p->fogVisibility, p->maxLights);
}

/*
 * SPEC-411 §5. Called from the sorted translucent pass (gl3_fx.c) for every
 * entity with flag 0x400000; se is the engine's entity (SPEC-002 layout) when
 * the frame came through the SPEC-002 RenderFrame.
 */
/* GFX-modern r_om_mirror: set while the models are drawn into a mirror image (gl3_post.c) */
qboolean gl3_ghoul_mirror;

/* ------------------------------------------------------------------
 * GFX-modern r_om_capsule: every model's box of this frame becomes a
 * capsule (upright: along z; lying: along its longer level axis); the
 * world of the next frame (it is drawn before the models) takes the eight
 * nearest to the eye. The flat ground blob is not drawn for them.
 * ------------------------------------------------------------------ */
#define MAX_CAPS 64
static float capa[2][MAX_CAPS][4], capb[2][MAX_CAPS][4];
static int numcaps[2], capcur;
/* the instance of each capsule, and the ones the world of this frame shades with: only those lose their ground
   blob (a body past the nearest r_om_capsule_max had neither - shadows came and went under the walkers of the
   tsr1 intro as the camera moved) */
static int capid[2][MAX_CAPS], selid[8], nsel, capinst;
static cvar_t *r_om_capsule;

static qboolean
CapsuleOn(void)
{
	if (!r_om_capsule)
	{
#ifdef YQ2_GL3_GLES3
		r_om_capsule = ri.Cvar_Get("r_om_capsule", "0", CVAR_ARCHIVE);
#else
		r_om_capsule = ri.Cvar_Get("r_om_capsule", "1", CVAR_ARCHIVE);
#endif
	}

	return r_om_capsule->value > 0;
}

/* a model's world box: true when it got a capsule */
static qboolean
CapsuleAdd(const vec3_t mins, const vec3_t maxs)
{
	vec3_t size, c;
	float *a, *b, r;
	int big, i;

	VectorSubtract(maxs, mins, size);

	if (r_om_capsule->value >= 3)
	{
		R_Printf(PRINT_ALL, "  box (%.0f %.0f %.0f) size %.0f %.0f %.0f\n", mins[0], mins[1], mins[2], size[0], size[1],
			size[2]);
	}

	/* bodies only: upright 40..110 high and at most 56 wide, or lying (a corpse) 40..110 long, under 40 high. A big
	   prop's box is a poor capsule (a heap of junk on kos1 darkened the floor around it as one 50-unit sphere) */
	{
		float lo = Q_min(size[0], size[1]), hi = Q_max(size[0], size[1]);
		qboolean upright = (size[2] >= 40) && (size[2] <= 110) && (hi <= 56) && (lo >= 8);
		qboolean lying = (hi >= 40) && (hi <= 110) && (lo <= 56) && (size[2] >= 6) && (size[2] < 40);

		if ((!upright && !lying) || (numcaps[capcur] >= MAX_CAPS))
		{
			return false;
		}
	}

	for (i = 0; i < 3; i++)
	{
		c[i] = 0.5f * (mins[i] + maxs[i]);
	}

	a = capa[capcur][numcaps[capcur]];
	b = capb[capcur][numcaps[capcur]];

	if (size[2] >= 0.8f * Q_max(size[0], size[1]))
	{
		/* upright: as wide as the smaller level extent (a body seen from above), from the feet to the head */
		r = 0.4f * Q_min(size[0], size[1]);
		VectorSet(a, c[0], c[1], mins[2] + r);
		VectorSet(b, c[0], c[1], Q_max(maxs[2] - r, mins[2] + r));
	}
	else
	{
		/* lying (a corpse, a crate): along the longer level axis, as thick as it is high */
		big = (size[0] >= size[1]) ? 0 : 1;
		r = 0.5f * Q_min(size[2], size[big ^ 1]);
		VectorCopy(c, a);
		VectorCopy(c, b);
		a[2] = b[2] = mins[2] + r;
		a[big] = c[big] - Q_max(0.5f * size[big] - r, 0.0f);
		b[big] = c[big] + Q_max(0.5f * size[big] - r, 0.0f);
	}

	a[3] = r;
	b[3] = Q_clamp(r_om_capsule->value, 0.0f, 1.0f) * 0.5f;
	capid[capcur][numcaps[capcur]] = capinst;
	numcaps[capcur]++;
	return true;
}

/* before the world of a frame: last frame's capsules, the eight nearest the eye, to the world shaders */
void
GL3_Ghoul_CapsuleFrame(void)
{
	static cvar_t *capmax;
	float a[8][4], b[8][4], dist[8];
	int last = capcur, n = 0, i, j, cmax, ids[8], prevsel[8], nprev = nsel;

	capcur ^= 1;
	numcaps[capcur] = 0;
	memcpy(prevsel, selid, sizeof(prevsel));
	nsel = 0;

	if (!CapsuleOn() || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		GL3_SoF_CapsuleUniforms(0, NULL, NULL);
		return;
	}

	/* r_om_capsule_max: the nearest bodies only (a loop over eight in the world shader cost tsr1's intro 0.5 ms) */
	if (!capmax)
	{
		capmax = ri.Cvar_Get("r_om_capsule_max", "4", CVAR_ARCHIVE);
	}

	cmax = (int)Q_clamp(capmax->value, 1.0f, 8.0f);

	/* test aid r_om_capsule_test N: N upright bodies in a row 100 units ahead of the eye, 30 apart, standing 40 below
	   the eye - a repeatable load for the cost of the world shader's loop */
	{
		static cvar_t *ct;
		vec3_t f, rt;

		if (!ct)
		{
			ct = ri.Cvar_Get("r_om_capsule_test", "0", 0);
		}

		if (ct->value > 0)
		{
			n = (int)Q_clamp(ct->value, 1.0f, (float)cmax);
			AngleVectors(r_newrefdef.viewangles, f, rt, NULL);
			f[2] = 0;
			VectorNormalize(f);

			for (i = 0; i < n; i++)
			{
				float off = (i - 0.5f * (n - 1)) * 30.0f;

				for (j = 0; j < 2; j++)
				{
					a[i][j] = b[i][j] = r_newrefdef.vieworg[j] + f[j] * 100.0f + rt[j] * off;
				}

				a[i][2] = r_newrefdef.vieworg[2] - 40.0f + 11.0f;
				b[i][2] = a[i][2] + 42.0f;
				a[i][3] = 11.0f;
				b[i][3] = 0.85f;
			}

			GL3_SoF_CapsuleUniforms(n, &a[0][0], &b[0][0]);
			return;
		}
	}

	for (i = 0; i < numcaps[last]; i++)
	{
		vec3_t m, dv;
		float dd;

		for (j = 0; j < 3; j++)
		{
			m[j] = 0.5f * (capa[last][i][j] + capb[last][i][j]);
		}

		VectorSubtract(m, r_newrefdef.vieworg, dv);
		dd = DotProduct(dv, dv);

		if (dd > 1500.0f * 1500.0f)
		{
			continue;
		}

		/* a body shaded last frame keeps its place against one barely nearer (no swapping back and forth) */
		for (j = 0; j < nprev; j++)
		{
			if (prevsel[j] == capid[last][i])
			{
				dd *= 0.5f;
				break;
			}
		}

		/* reach outside the view frustum: nothing it shades is seen (the last frame's frustum, a frame old at most) */
		{
			vec3_t h;
			float R;
			int p;

			VectorSubtract(capb[last][i], capa[last][i], h);
			R = 0.5f * VectorLength(h) + 3.5f * capa[last][i][3];

			for (p = 0; p < 4; p++)
			{
				if (DotProduct(frustum[p].normal, m) - frustum[p].dist < -R)
				{
					break;
				}
			}

			if (p < 4)
			{
				continue;
			}
		}

		/* insertion into the eight nearest */
		for (j = n; (j > 0) && (dist[j - 1] > dd); j--)
		{
			if (j < cmax)
			{
				memcpy(a[j], a[j - 1], sizeof(a[j]));
				memcpy(b[j], b[j - 1], sizeof(b[j]));
				dist[j] = dist[j - 1];
				ids[j] = ids[j - 1];
			}
		}

		if (j < cmax)
		{
			memcpy(a[j], capa[last][i], sizeof(a[j]));
			memcpy(b[j], capb[last][i], sizeof(b[j]));
			dist[j] = dd;
			ids[j] = capid[last][i];
			n = Q_min(n + 1, cmax);
		}
	}

	if (r_om_capsule->value >= 2)
	{
		static float next;

		if (r_newrefdef.time >= next)
		{
			next = r_newrefdef.time + 1.0f;
			R_Printf(PRINT_ALL, "OMCAPS %d of %d\n", n, numcaps[last]);

			for (i = 0; (r_om_capsule->value >= 4) && (i < r_newrefdef.num_entities); i++)
			{
				const entity_t *en = &r_newrefdef.entities[i];

				R_Printf(PRINT_ALL, "  ent %d at (%.0f %.0f %.0f) flags %x model %p frame %d\n", i, en->origin[0],
					en->origin[1], en->origin[2], en->flags, (void *)en->model, en->frame);
			}

			for (i = 0; i < n; i++)
			{
				R_Printf(PRINT_ALL, "  cap (%.0f %.0f %.0f)-(%.0f %.0f %.0f) r %.1f\n", a[i][0], a[i][1], a[i][2], b[i][0],
					b[i][1], b[i][2], a[i][3]);
			}
		}
	}

	memcpy(selid, ids, sizeof(selid));
	nsel = n;
	GL3_SoF_CapsuleUniforms(n, &a[0][0], &b[0][0]);
}

void
GL3_Ghoul_DrawEntity(entity_t *e, sofentity_t *se)
{
	ghlparams_t P;
	void *inst;
	qboolean weapon, fullbright, nocull, fog = false, wire, hack, capsuled;
	float m, darkness;
	int flags = e->flags, mode;
	vec3_t color;
	GLint oldcull = GL_FRONT;
	GLboolean wasblend, wascull, wasalpha, wasdepthtest;

	if (!gl3_ghoul)
	{
		return;
	}

	GetCvars();

	/* step 1 */
	if (ghl_no_render->value > 0.5f)
	{
		return;
	}

	/* the low 16 bits as a signed value: server instances have handles 1..32766, instances the
	   client creates itself (effect debris, frame -1 = local id 1 on the negating client system) are
	   negative. Observed: the original draws such an entity (OPEN_QUESTIONS I4b-1) */
	inst = Ghl_FindInstance((short)(e->frame & 0xFFFF));

	if (!inst || !BackEnd())
	{
		return;
	}

	memset(&P, 0, sizeof(P));
	weapon = (flags & SOF_RF_WEAPONMODEL) != 0;
	fullbright = (r_fullbright->value != 0) || (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES);
	nocull = (ghl_no_cull->value != 0);

	/* step 2: the cull object */
	P.vtable = (weapon || nocull) ? cullvt_never : cullvt_frustum;

	/* step 3: orientation */
	EntityMatrix(e, &P.entityToWorld);

	/* GFX-modern r_om_capsule: the box of every model (also one outside the view: its shadow may be in it) */
	capsuled = false;
	capinst = (short)(e->frame & 0xFFFF);

	if (CapsuleOn() && (r_om_capsule->value >= 4))
	{
		R_Printf(PRINT_ALL, "  ghoul at (%.0f %.0f %.0f) flags %x weapon %d mirror %d\n", e->origin[0], e->origin[1],
			e->origin[2], flags, weapon, gl3_ghoul_mirror);
	}

	if (!weapon && !gl3_ghoul_mirror && CapsuleOn())
	{
		vec3_t mins = {0, 0, 0}, maxs = {0, 0, 0}, size;

		/* a character's box spans its whole animation (about 200 x 200 x 104 - as a capsule it darkened the floor all
		   around a standing grunt; the exact box of the pose is not offered): a body is placed by its origin and the
		   floor under it instead - upright 64 high, or lying along its yaw where the origin is down at the floor */
		Ghl_Bounds(inst, GL3_GameTime(), &P.entityToWorld, mins, maxs);
		VectorSubtract(maxs, mins, size);

		if (r_om_capsule->value >= 3)
		{
			R_Printf(PRINT_ALL, "  model at (%.0f %.0f %.0f) box %.0f %.0f %.0f flags %x\n", e->origin[0], e->origin[1],
				e->origin[2], size[0], size[1], size[2], flags);
		}

		if ((maxs[0] > mins[0]) && (Q_max(size[0], size[1]) >= 100) && (size[2] >= 60) && (size[2] <= 160))
		{
			vec3_t start, end, cmin, cmax;
			float f, feet, yaw = e->angles[1] * (float)M_PI / 180.0f;

			VectorCopy(e->origin, start);
			VectorCopy(e->origin, end);
			start[2] += 8;
			end[2] -= 128;
			f = WorldTrace(start, end);
			feet = ((f >= 0) && (f < 1)) ? start[2] + (end[2] - start[2]) * f : mins[2];

			if (e->origin[2] - feet >= 14)
			{
				VectorSet(cmin, e->origin[0] - 14, e->origin[1] - 14, feet);
				VectorSet(cmax, e->origin[0] + 14, e->origin[1] + 14, feet + 64);
			}
			else if (numcaps[capcur] < MAX_CAPS)
			{
				/* lying: the capsule along the body's yaw itself (a box of a body turned 45 degrees is square) */
				float *ca = capa[capcur][numcaps[capcur]], *cb = capb[capcur][numcaps[capcur]];
				float hx = cosf(yaw) * 26, hy = sinf(yaw) * 26;

				VectorSet(ca, e->origin[0] - hx, e->origin[1] - hy, feet + 9);
				VectorSet(cb, e->origin[0] + hx, e->origin[1] + hy, feet + 9);
				ca[3] = 9;
				cb[3] = Q_clamp(r_om_capsule->value, 0.0f, 1.0f) * 0.5f;
				capid[capcur][numcaps[capcur]] = capinst;
				numcaps[capcur]++;
				capsuled = true;
			}

			if (e->origin[2] - feet >= 14)
			{
				capsuled = CapsuleAdd(cmin, cmax);
			}
		}
		/* props keep their blob: a capsule of a flat thing (a fish on the jpn1 kitchen table) smudged the table */

		/* the blob gives way only where the world is shaded by this body's capsule now */
		if (capsuled)
		{
			int k;

			capsuled = false;

			for (k = 0; k < nsel; k++)
			{
				if (selid[k] == capinst)
				{
					capsuled = true;
					break;
				}
			}
		}
	}

	/* step 4: pre-cull with the instance's box at the frame's game clock */
	if ((ghl_pre_cull->value > 0.5f) && !weapon && !nocull)
	{
		vec3_t mins = {0, 0, 0}, maxs = {0, 0, 0}, c;
		float corners[8][3];

		Ghl_Bounds(inst, GL3_GameTime(), &P.entityToWorld, mins, maxs);
		BoxCorners(mins, maxs, corners);

		if (CornersOutside((const float (*)[3])corners))
		{
			GL3_SoF_ModelLightsCulled(inst); /* its lamps still light what is in view */
			return;
		}

		/* GFX-modern r_om_fogcull: wholly inside the opaque fog of the eye's region */
		VectorAdd(mins, maxs, c);
		VectorScale(c, 0.5f, c);
		VectorSubtract(maxs, mins, corners[0]);

		if ((maxs[0] >= mins[0]) && GL3_Fog_CullPoint(c, 0.5f * VectorLength(corners[0])))
		{
			GL3_SoF_ModelLightsCulled(inst);
			return;
		}
	}
	else if (!weapon && !nocull && GL3_Fog_CullPoint(e->origin, 128))
	{
		return; /* r_om_fogcull without the instance box: a generous radius */
	}

	/* step 5: shadow mode */
	mode = (int)floorf(ghl_shadows->value + 0.5f);

	if ((mode < 0) || fullbright)
	{
		mode = 0;
	}

	if (mode > 2)
	{
		mode = 2;
	}

	/* no ground shadows in a mirror image. The capsule (r_om_capsule) no longer takes the ground shadow away: only
	   the nearest bodies get one, and as they changed the long shadows of the others came and went (irq1a) - the
	   capsule is the soft contact under the feet, the ground shadow the shape thrown by the light */
	(void)capsuled;
	if ((ghl_shadow_darkness->value <= 0) || gl3_ghoul_mirror)
	{
		mode = 0;
	}

	if (mode)
	{
		P.shadowDarkness = ghl_shadow_darkness->value;
		P.worldTrace = gl3_ghoul_trace ? gl3_ghoul_trace : WorldTrace;
		P.shadowDist = ghl_shadow_dist->value;
	}

	P.shadowMode = mode;

	/* GFX-modern: with the world trace set the Ghoul system lets a map light reach the model only when nothing solid
	   lies between them (GatherWorldLights); the ground shadow still runs on shadowMode alone. Always on (was
	   r_om_lightocc: with ground shadows on the trace was set anyway, the switch changed nothing) */
	P.worldTrace = gl3_ghoul_trace ? gl3_ghoul_trace : WorldTrace;

	/* step 6: the player's own model is drawn shadow-only, or not at all; a mirror shows all of it */
	if ((flags & SOF_RF_VIEWERMODEL) && !gl3_ghoul_mirror)
	{
		if (!mode || (ghl_player_shadow->value < 0.5f))
		{
			return;
		}

		P.shadowOnly = 1;
	}

	/* step 7: option bytes */
	m = ghl_light_method->value;
	P.time = GL3_GameTime();
	P.fogVisibility = 1.0f;
	VectorCopy(r_newrefdef.vieworg, P.viewOrigin);
	VectorCopy(vpn, P.viewForward);
	VectorCopy(e->origin, P.origin);
	P.specular = (ghl_specular->value > 0.5f) && (m > 0.5f);
	P.fastLights = (ghl_fast_lights->value > 0.5f) && (m < 3);
	P.castShadow = (flags & SOF_RF_CASTSHADOW) ? 1 : 0;
	P.noTextureStack = (ghl_no_texture_stack->value > 0.5f);
	P.noDynamicLights = (gl_dlightintensity->value <= 0.01f);
	P.maxLights = RoundClamp(ghl_max_lights->value, 1, 8);
	P.entity = se;

	/* step 8: light colour and lighting options */
	if ((flags & SOF_RF_FULLBRIGHT) || fullbright)
	{
		VectorSet(color, 1, 1, 1);
	}
	else if ((m == 1.0f) && !(flags & SOF_RF_AMBIENTONLY))
	{
		GL3_SoF_LightPoint(e->origin, color, 255);
		ProbeVector(e, P.lightDir);
		P.dirValid = 1;
	}
	else
	{
		GL3_SoF_LightPoint(e->origin, color, 255);

		if ((flags & SOF_RF_AMBIENTONLY) || (m < 2))
		{
			P.specular = 0;
		}
		else
		{
			float maxl;

			P.useGLLights = 1;

			if (m <= 2.5f)
			{
				maxl = ghl_max_lights->value;
			}
			else
			{
				P.hardwareLights = 1;
				ff.LightModeli(GHL_GL_LIGHT_MODEL_LOCAL_VIEWER, ghl_gllight_local_viewer->value > 0.5f);
				P.glDirectional = (ghl_gllight_directional->value > 0.5f);
				P.glAttenuation = (ghl_gllight_attenuation->value > 0.5f) && !P.glDirectional;
				maxl = ghl_gllight_max_lights->value;
			}

			P.maxLights = RoundClamp(maxl, 1, 8);
		}
	}

	/* step 9: post-processing of the colour */
	{
		float k = ghl_light_multiply->value;

		if (k < 0.25f)
		{
			k = 0.25f;
		}

		if (k > 2)
		{
			k = 2;
		}

		VectorScale(color, k, color);
	}

	if (flags & SOF_RF_VIEWERMODEL)
	{
		P.lightDir[0] = -25 * vup[0];
		P.lightDir[1] = -25 * vup[1];
		P.lightDir[2] = 0;
	}

	if ((flags & SOF_RF_MINLIGHT) && (color[0] <= 0.1f) && (color[1] <= 0.1f) && (color[2] <= 0.1f))
	{
		VectorSet(color, 0.1f, 0.1f, 0.1f);
	}

	if (flags & SOF_RF_GLOW)
	{
		float t = gl3_sofframe.active ? gl3_sofframe.clienttime : r_newrefdef.time;
		float s = 0.1f * sinf(7 * t);
		int i;

		for (i = 0; i < 3; i++)
		{
			float c = color[i] + s;

			color[i] = (c > 0.8f * color[i]) ? c : 0.8f * color[i];
		}
	}

	VectorCopy(color, P.lightColor);

	if (se && se->lightout)
	{
		VectorCopy(color, se->lightout);
	}

	/* GFX-modern r_om_overbright with r_om_overbright_models: the model's light is scaled like the
	   world's lightmaps (drawing only; the light value reported above is unchanged) */
	if (!((flags & SOF_RF_FULLBRIGHT) || fullbright))
	{
		float ob = GL3_Post_OverbrightModels();

		if (ob != 1.0f)
		{
			VectorScale(P.lightColor, ob, P.lightColor);
		}
	}

	/* the GL state of the Ghoul call: fixed function, the view's matrices */
	if (ff_emulated)
	{
		GL3_GhoulEmu_Begin();
	}
#ifdef OM_SOFREF
	else
	{
		static const byte white[4] = {255, 255, 255, 255};

		GL3_Ghoul_EnterFixedFunction(false, true, white);
	}
#endif

	ff.MatrixMode(GHL_GL_PROJECTION);
	ff.LoadMatrixf(&gl3state.projMat3D.Elements[0][0]);
	ff.MatrixMode(GHL_GL_TEXTURE);
	ff.LoadIdentity();
	ff.MatrixMode(GHL_GL_MODELVIEW);
	ff.LoadMatrixf(&gl3state.viewMat3D.Elements[0][0]);

	wasblend = ff.IsEnabled(GL_BLEND);
	wascull = ff.IsEnabled(GL_CULL_FACE);
	wasalpha = ff.IsEnabled(GHL_GL_ALPHA_TEST);
	wasdepthtest = ff.IsEnabled(GL_DEPTH_TEST);
	ff.GetIntegerv(GL_CULL_FACE_MODE, &oldcull);

	/* step 10: fog */
	darkness = P.shadowDarkness;

	/* E19 r_om_fogpix: the model's shader fogs it per pixel (the regions the view crosses), not the whole model by
	   its origin; the view weapon stays clear as before */
	GL3_GhoulEmu_SetRegionFog(!weapon && GL3_Fog_PixOn());

	if (!weapon && !GL3_Fog_PixOn())
	{
		int region = GL3_Fog_RegionOfPoint(e->origin);

		if (region >= 0)
		{
			if (ghl_obj_fog->value <= 0.5f)
			{
				float start, end, fc[4];

				if (GL3_Fog_RegionLinear(region, &start, &end, fc))
				{
					ff.Fogi(GHL_GL_FOG_MODE, GL_LINEAR);
					ff.Fogf(GHL_GL_FOG_START, start);
					ff.Fogf(GHL_GL_FOG_END, end);
					ff.Fogfv(GHL_GL_FOG_COLOR, fc);
					ff.Enable(GHL_GL_FOG);
					fog = true;
				}
			}
			else if (gl_fogmode->value != 3)
			{
				float fc[4], f = GL3_Fog_PointFactor(region, e->origin, fc);

				if (f > 0.01f)
				{
					vec3_t d;
					float depth;

					VectorSubtract(e->origin, r_newrefdef.vieworg, d);
					depth = fabsf(DotProduct(vpn, d));
					fc[3] = 1;
					ff.Fogi(GHL_GL_FOG_MODE, GL_LINEAR);
					ff.Fogf(GHL_GL_FOG_START, 0);
					ff.Fogf(GHL_GL_FOG_END, depth / f);
					ff.Fogfv(GHL_GL_FOG_COLOR, fc);
					ff.Enable(GHL_GL_FOG);
					fog = true;
					P.fogVisibility = 1 - f;
					darkness *= (1 - f);
				}
			}
		}
	}

	/* GFX-modern r_om_lshadow: a character under a lamp that casts its shadow loses the ground blob */
	if (!weapon && !(flags & SOF_RF_VIEWERMODEL))
	{
		darkness *= GL3_LShadow_BlobKeep(e->origin);
	}

	P.shadowDarkness = darkness;

	/* a blob wholly replaced (lamp shadow + r_om_ssao) is not built at all: no second pose walk for its mask */
	if ((darkness <= 0.01f) && !P.shadowOnly)
	{
		P.shadowMode = 0;
	}

	/* step 11: draw */
	ff.ShadeModel(GHL_GL_SMOOTH);
	ff.CullFace(GL_BACK);
	ff.Disable(GHL_GL_ALPHA_TEST);
	ff.Disable(GL_BLEND);
	ff.Enable(GL_DEPTH_TEST);
	ff.DepthMask(GL_TRUE);
	ff.Color4f(1, 1, 1, 1);
	ff.TexEnvf(GHL_GL_TEXTURE_ENV, GHL_GL_TEXTURE_ENV_MODE, (float)GHL_GL_MODULATE);

	if (gl_drawflat->value)
	{
		ff.Disable(GL_TEXTURE_2D);
	}
	else
	{
		ff.Enable(GL_TEXTURE_2D);
	}

	wire = (ghl_wireframe->value > 0.5f);
	hack = (flags & SOF_RF_DEPTHHACK) != 0;

	if (wire)
	{
		ff.PolygonMode(GL_FRONT_AND_BACK, GHL_GL_LINE);
	}

	if (hack)
	{
		ff.DepthRange(gl3depthmin, gl3depthmin + 0.3 * (gl3depthmax - gl3depthmin));
	}

	/* r_om_ghldebug 1: every Ghoul entity, 2: only fogged ones */
	if ((r_om_ghldebug->value == 1) || ((r_om_ghldebug->value == 2) && fog))
	{
		DebugPrint(e, &P);
	}

	/* GFX-modern bloom: RF_FULLBRIGHT / RF_GLOW entities are light sources (not the view weapon) */
	if (ff_emulated && gl3post.bloom && !weapon && (flags & (SOF_RF_FULLBRIGHT | SOF_RF_GLOW)))
	{
		GL3_GhoulEmu_SetEntityGlow(0.35f * GL3_Post_FullbrightScale());
	}

	/* GFX-modern sun shadows: every Ghoul model but the view weapon and the player's own casts; its ground
	   shadow gives way to the sun shadow in the sun (SPEC-AMEND-016 R16.4). The own model keeps its blob. */
	if (ff_emulated)
	{
		qboolean viewer = (flags & SOF_RF_VIEWERMODEL) != 0;

		/* (a mirror image adds no casters: they would be the mirrored bodies) */
		GL3_GhoulEmu_SetCsm((GL3_CSM_Active() || GL3_LShadow_Capturing()) && !weapon && !viewer && !hack && !gl3_ghoul_mirror,
			GL3_CSM_ReplaceGhoul() && !viewer && !weapon);
		GL3_GhoulEmu_SetCaster(!weapon && !viewer && !hack && !gl3_ghoul_mirror); /* shadows in light beams */
		/* characters (the models that cast a ground shadow) and the props that are round in truth (a texture
		   listed in Ghoul/om_round.txt, seen when the instance was last drawn): other vehicles and props carry
		   flat decals that part from a curved body, or are boxes their smoothed normals would inflate */
		{
			float k = Q_clamp(r_om_tess->value, 0.0f, 10.0f) / 10.0f;
			qboolean round = roundinst[(unsigned short)(e->frame & 0xFFFF)] && (r_om_tess_round->value != 0);
			int level = (k <= 0) ? 0 : weapon ? (int)r_om_tess_weapon->value :
				(((flags & SOF_RF_CASTSHADOW) || round) ? (int)Q_clamp(r_om_tess_models->value, 0.0f, 3.0f) : 0);

			/* by size on the screen: the small triangles cost the GPU (MSAA shades the pixels along every edge
			   twice; three characters close by at level 3 took 4.8 ms on a GTX 1050 Ti, at level 2 2.4). A man-high
			   model taller than 600 pixels (an arm's length) keeps the level, from 100 pixels at most 2, smaller
			   ones none - their facets are a pixel or two */
			/* (a round prop is a few dozen triangles - a jpn1 trash can 34: the full level costs nothing and level 2
			   left it a prism) */
			if ((level > 1) && !weapon && !(round && !(flags & SOF_RF_CASTSHADOW)))
			{
				vec3_t d;
				float dist, px;

				VectorSubtract(e->origin, r_newrefdef.vieworg, d);
				dist = Q_max(VectorLength(d), 1.0f);
				px = 64.0f / dist * (r_newrefdef.height * 0.5f) / tanf(r_newrefdef.fov_y * (float)M_PI / 360.0f);
				level = (px > 600.0f) ? level : (px > 100.0f) ? Q_min(level, 2) : 0;
			}

			if (ri.Cvar_Get("r_om_tessdebug", "0", 0)->value >= 2)
			{
				R_Printf(PRINT_ALL, "OMTESSENT inst %d at (%.0f %.0f %.0f) round %d castshadow %d level %d\n",
					(int)(short)(e->frame & 0xFFFF), e->origin[0], e->origin[1], e->origin[2], round,
					(flags & SOF_RF_CASTSHADOW) != 0, level);
			}

			GL3_GhoulEmu_SetTess(level, k);
		}
		GL3_GhoulEmu_SetPPL(r_om_ppl_models->value != 0);

		/* r_om_ambcube: the ambient light from the side the surroundings are lit from (the view weapon: at the eye) */
		{
			float cube[18];
			qboolean lit = !((flags & SOF_RF_FULLBRIGHT) || fullbright);

			GL3_GhoulEmu_SetAmbientCube((lit && GL3_SoF_AmbientCube((weapon || (flags & SOF_RF_VIEWERMODEL)) ?
				r_newrefdef.vieworg : e->origin, cube)) ? cube : NULL);
		}
	}

	GL3_GhoulEmu_TakeRound();
	GL3_CSM_SetCasterKey((short)(e->frame & 0xFFFF), e->origin);
	{
		int mark = GL3_SoF_ModelLightMark();

		Ghl_Draw(inst, &P);
		GL3_SoF_ModelLightsDrawn(inst, mark);
	}
	GL3_CSM_SetCasterKey(0, NULL);

	if (ff_emulated)
	{
		/* a reused handle forgets the old model's answer at its first draw */
		roundinst[(unsigned short)(e->frame & 0xFFFF)] = GL3_GhoulEmu_TakeRound() ? 1 : 0;
		GL3_GhoulEmu_SetEntityGlow(0.0f);
		GL3_GhoulEmu_SetRegionFog(false);
		GL3_GhoulEmu_SetCsm(false, false);
		GL3_GhoulEmu_SetCaster(false);
		GL3_GhoulEmu_SetTess(0, 0.0f);
		GL3_GhoulEmu_SetPPL(false);
		GL3_GhoulEmu_SetAmbientCube(NULL);
	}

	/* step 12: restore */
	if (wire)
	{
		ff.PolygonMode(GL_FRONT_AND_BACK, GHL_GL_FILL);
	}

	if (hack)
	{
		ff.DepthRange(gl3depthmin, gl3depthmax);
	}

	if (fog)
	{
		ff.Disable(GHL_GL_FOG);
	}

	ff.Disable(GHL_GL_LIGHTING);
	ff.Disable(GHL_GL_TEXTURE_GEN_S);
	ff.Disable(GHL_GL_TEXTURE_GEN_T);
	ff.Disable(GL_POLYGON_OFFSET_FILL);
	ff.MatrixMode(GHL_GL_TEXTURE);
	ff.LoadIdentity();
	ff.MatrixMode(GHL_GL_MODELVIEW);
	ff.CullFace(oldcull);
	(wascull ? ff.Enable : ff.Disable)(GL_CULL_FACE);
	(wasblend ? ff.Enable : ff.Disable)(GL_BLEND);
	(wasalpha ? ff.Enable : ff.Disable)(GHL_GL_ALPHA_TEST);
	(wasdepthtest ? ff.Enable : ff.Disable)(GL_DEPTH_TEST);
	ff.Color4f(1, 1, 1, 1);
	ff.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	ff.Enable(GL_TEXTURE_2D);
	ff.AlphaFunc(GL_GREATER, 0.05f);
	ff.ShadeModel(GHL_GL_SMOOTH);

	if (ff_emulated)
	{
		GL3_GhoulEmu_End();
	}
#ifdef OM_SOFREF
	else
	{
		GL3_Ghoul_LeaveFixedFunction();
	}
#endif
}
