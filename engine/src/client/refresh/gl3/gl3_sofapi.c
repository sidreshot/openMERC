/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: the SPEC-002 export table (renderer -> engine) on
 * top of the GL3 core, shared by both front ends: the SoF module
 * (ref_omgl.dll, gl3_sof.c) and the Yamagi-hosted renderer, which offers the
 * same table through OM_GetSoFRefExports() for engine code drawing through
 * the SoF slots. Also the scene conversion of SPEC-002 RenderFrame and the
 * renderer side of the Ghoul light exchange (SPEC-010 system methods).
 *
 * Implements: SPEC-002, SPEC-410 (Model lights, damaged surfaces, terrain-set
 *             selection messages), SPEC-411 §1-§3, SPEC-412, SPEC-413
 */

#include "header/local.h"

gl3sofframe_t gl3_sofframe;
void *gl3_ghoul;

/* user directory and free-name search (SPEC-002 imports 0x38 / 0x3c in the module) */
const char *(*gl3_userdir_fn)(void);
qboolean (*gl3_nextfreename_fn)(char *path);

const char *
GL3_UserDir(void)
{
	if (gl3_userdir_fn)
	{
		return gl3_userdir_fn();
	}

	return ri.FS_Gamedir();
}

/* the two characters 6 and 5 places before the end of path become 00..99 */
qboolean
GL3_NextFreeName(char *path)
{
	size_t len = strlen(path);
	int i;

	if (gl3_nextfreename_fn)
	{
		return gl3_nextfreename_fn(path);
	}

	if (len < 6)
	{
		return false;
	}

	for (i = 0; i < 100; i++)
	{
		FILE *f;

		path[len - 6] = '0' + i / 10;
		path[len - 5] = '0' + i % 10;
		f = fopen(path, "rb");

		if (!f)
		{
			return true;
		}

		fclose(f);
	}

	return false;
}

/* ------------------------------------------------------------------
 * Ghoul system calls (SPEC-010 interface A, MSVC x86 thiscall)
 * ------------------------------------------------------------------ */

#if defined(_MSC_VER) && defined(_M_IX86)
/* thiscall through fastcall: this in ECX, a dummy in EDX, the rest on the stack, callee pops */
#define GHL_METHOD(n) (((void **)(*(void **)gl3_ghoul))[n])
typedef int (__fastcall *ghl_int0_t)(void *self, void *edx);
typedef void (__fastcall *ghl_void0_t)(void *self, void *edx);
typedef void (__fastcall *ghl_ptr_t)(void *self, void *edx, void *p);
typedef void (__fastcall *ghl_intptr_t)(void *self, void *edx, int i, void *p);
typedef void (__fastcall *ghl_int1_t)(void *self, void *edx, int i);
typedef void (__fastcall *ghl_float2_t)(void *self, void *edx, float a, float b);

static int GhlInt0(int n) { return ((ghl_int0_t)GHL_METHOD(n))(gl3_ghoul, NULL); }
static void GhlVoid0(int n) { ((ghl_void0_t)GHL_METHOD(n))(gl3_ghoul, NULL); }
static void GhlPtr(int n, void *p) { ((ghl_ptr_t)GHL_METHOD(n))(gl3_ghoul, NULL, p); }
static void GhlIntPtr(int n, int i, void *p) { ((ghl_intptr_t)GHL_METHOD(n))(gl3_ghoul, NULL, i, p); }
static void GhlInt1(int n, int i) { ((ghl_int1_t)GHL_METHOD(n))(gl3_ghoul, NULL, i); }
static void GhlFloat2(int n, float a, float b) { ((ghl_float2_t)GHL_METHOD(n))(gl3_ghoul, NULL, a, b); }
#define HAVE_GHOUL_CALLS 1
#else
/* I4b: elsewhere (x64, GCC/Clang) a C++ method is a plain function whose first argument is the object
   (the OpenMerc Ghoul library behind ghoul_api.h; int stands for the 16-bit ids and bools of the methods) */
#define GHL_METHOD(n) (((void **)(*(void **)gl3_ghoul))[n])
typedef int (*ghl_int0_t)(void *self);
typedef void (*ghl_void0_t)(void *self);
typedef void (*ghl_ptr_t)(void *self, void *p);
typedef void (*ghl_intptr_t)(void *self, int i, void *p);
typedef void (*ghl_int1_t)(void *self, int i);
typedef void (*ghl_float2_t)(void *self, float a, float b);

static int GhlInt0(int n) { return ((ghl_int0_t)GHL_METHOD(n))(gl3_ghoul); }
static void GhlVoid0(int n) { ((ghl_void0_t)GHL_METHOD(n))(gl3_ghoul); }
static void GhlPtr(int n, void *p) { ((ghl_ptr_t)GHL_METHOD(n))(gl3_ghoul, p); }
static void GhlIntPtr(int n, int i, void *p) { ((ghl_intptr_t)GHL_METHOD(n))(gl3_ghoul, i, p); }
static void GhlInt1(int n, int i) { ((ghl_int1_t)GHL_METHOD(n))(gl3_ghoul, i); }
static void GhlFloat2(int n, float a, float b) { ((ghl_float2_t)GHL_METHOD(n))(gl3_ghoul, a, b); }
#define HAVE_GHOUL_CALLS 1
#endif

typedef struct
{
	float origin[3];
	float radius;
	float color[3];
} ghllight_t;

/* SPEC-411 §1: start-up of the Ghoul link (the GL table, method 1, is package I4b) */
void
GL3_SoF_GhoulStartup(void)
{
	if (!gl3_ghoul)
	{
		return;
	}

	GhlInt1(38, (int)floorf(ghl_mip->value + 0.5f));
	GhlFloat2(39, ghl_poly_fact->value, ghl_poly_unit->value);
	GhlPtr(37, gl3_colortable);

	{
		/* I4b: the native GL 1.1 table (SoF module) or its emulation on the core profile (gl3_ghoul.c) */
		extern void *GL3_Ghoul_Table(void);
		void *table = GL3_Ghoul_Table();

		if (table)
		{
			GhlPtr(1, table);
			GhlInt1(2, (ghl_specular->value > 0.5f) && (ghl_light_method->value > 0.5f));
		}
	}
}

void
GL3_SoF_GhoulShutdown(void)
{
	extern void GL3_Ghoul_Shutdown(void); /* I4b */

	if (gl3_ghoul)
	{
		GhlVoid0(3);
	}

	GL3_Ghoul_Shutdown(); /* I4b */
}

/* SPEC-410 Model lights / SPEC-411 §2: the static world-light set from lump 20 */
void
GL3_SoF_WorldLights(const byte *lights, int len)
{
	int i, n = len / 36;

	if (!gl3_ghoul)
	{
		return;
	}

	GhlVoid0(36);

	for (i = 0; i < n; i++)
	{
		const byte *r = lights + 36 * i;
		ghllight_t l;
		float intensity = LittleFloat(*(const float *)(r + 24));
		int type = LittleLong(*(const int *)(r + 28));
		int extra = LittleLong(*(const int *)(r + 32));
		float radius;

		if ((intensity <= 0) || (type == 3))
		{
			continue;
		}

		radius = ((type == 0) || (extra >= 2)) ? sqrtf(intensity) / 2 : intensity;

		if (radius > 1500)
		{
			continue;
		}

		l.origin[0] = LittleFloat(*(const float *)(r + 0));
		l.origin[1] = LittleFloat(*(const float *)(r + 4));
		l.origin[2] = LittleFloat(*(const float *)(r + 8));
		l.radius = radius;
		l.color[0] = LittleFloat(*(const float *)(r + 12));
		l.color[1] = LittleFloat(*(const float *)(r + 16));
		l.color[2] = LittleFloat(*(const float *)(r + 20));
		GhlPtr(34, &l);
	}

	GhlVoid0(35);
}

/* SPEC-002 / SPEC-411 §3: light exchange with the Ghoul system before the frame.
   I4b: returns the frame's light count; the engine's record keeps its own count (the original works
   on a copy of the frame record, so a scene drawn again unchanged, e.g. while paused, does not
   accumulate the appended lights) */
static int
GhoulLightExchange(sofrefdef_t *fd)
{
	float k;
	int i, n, num = (fd->num_dlights > SOF_MAX_DLIGHTS) ? SOF_MAX_DLIGHTS : fd->num_dlights;

	if (num < 0)
	{
		num = 0;
	}

	if (!gl3_ghoul || !fd->dlights)
	{
		return num;
	}

	if (gl_dlightintensity->value > 4)
	{
		ri.Cvar_Set("gl_dlightintensity", "4");
	}

	k = gl_dlightintensity->value;

	if (k == 0)
	{
		return num;
	}

	n = GhlInt0(28);

	for (i = 0; (i < n) && (num < SOF_MAX_DLIGHTS); i++)
	{
		ghllight_t l;
		sofdlight_t *d = &fd->dlights[num];

		GhlIntPtr(29, i, &l);
		VectorCopy(l.origin, d->origin);
		VectorCopy(l.color, d->color);
		d->intensity = l.radius * k;
		num++;
	}

	GhlVoid0(30);

	if (ghl_light_method->value > 0.5f)
	{
		for (i = 0; i < num; i++)
		{
			ghllight_t l;

			VectorCopy(fd->dlights[i].origin, l.origin);
			VectorCopy(fd->dlights[i].color, l.color);
			l.radius = fd->dlights[i].intensity * 0.5f;
			GhlPtr(31, &l);
		}
	}

	return num;
}

/* ------------------------------------------------------------------
 * scene conversion (SPEC-002 frame record)
 * ------------------------------------------------------------------ */

extern void GL3_RenderFrameInternal(refdef_t *fd);

static entity_t sofents[SOF_MAX_ENTITIES];

void
GL3_SoF_RenderFrame(sofrefdef_t *fd)
{
	refdef_t rd;
	int i, n, numdlights;

	if (!fd)
	{
		return;
	}

	numdlights = GhoulLightExchange(fd); /* I4b */

	if (gl3_ghoul)
	{
		GhlVoid0(48); /* Ghoul statistics reset */
	}

	memset(&rd, 0, sizeof(rd));
	rd.x = fd->x;
	rd.y = fd->y;
	rd.width = fd->width;
	rd.height = fd->height;
	rd.fov_x = fd->fov_x;
	rd.fov_y = fd->fov_y;
	VectorCopy(fd->vieworg, rd.vieworg);
	VectorCopy(fd->viewangles, rd.viewangles);
	memcpy(rd.blend, fd->blend, sizeof(rd.blend));
	rd.time = fd->time;
	rd.rdflags = fd->rdflags & (SOF_RDF_NOWORLDMODEL | SOF_RDF_IRGOGGLES); /* 0x1: no renderer effect */
	rd.areabits = fd->areabits;
	rd.lightstyles = (lightstyle_t *)fd->lightstyles;
	rd.num_dlights = numdlights; /* I4b */
	rd.dlights = (dlight_t *)fd->dlights;

	n = (fd->num_entities > SOF_MAX_ENTITIES) ? SOF_MAX_ENTITIES : fd->num_entities;

	for (i = 0; i < n; i++)
	{
		const sofentity_t *se = fd->entities ? fd->entities[i] : NULL;
		entity_t *e = &sofents[i];

		memset(e, 0, sizeof(*e));

		if (!se)
		{
			continue;
		}

		e->model = (struct model_s *)se->model;
		VectorCopy(se->angles, e->angles);
		VectorCopy(se->origin, e->origin);
		VectorCopy(se->origin, e->oldorigin);
		e->frame = se->frame;
		e->oldframe = se->oldframe;
		e->backlerp = se->backlerp;
		e->skinnum = se->skinnum;
		e->alpha = se->alpha;
		e->skin = (struct image_s *)se->skin;
		e->flags = se->flags & ~0x20; /* no Quake II translucency bit in SoF */
	}

	rd.num_entities = n;
	rd.entities = sofents;

	gl3_sofframe.active = true;
	gl3_sofframe.gametime = fd->gametime;
	gl3_sofframe.gametime_delta = fd->gametime_delta;
	gl3_sofframe.clienttime = fd->time;
	gl3_sofframe.fd = fd;
	gl3_sofframe.num_entities = n;
	gl3_sofframe.entities = fd->entities;
	gl3_sofframe.num_quadchains = (fd->num_quadchains > SOF_MAX_QUADCHAINS) ? SOF_MAX_QUADCHAINS : fd->num_quadchains;
	gl3_sofframe.quadchains = fd->quadchains;
	gl3_sofframe.num_alphaquads = (fd->num_alphaquads > SOF_MAX_ALPHAQUADS) ? SOF_MAX_ALPHAQUADS : fd->num_alphaquads;
	gl3_sofframe.alphaquads = fd->alphaquads;

	if (!gl3_sofframe.quadchains)
	{
		gl3_sofframe.num_quadchains = 0;
	}

	if (!gl3_sofframe.alphaquads)
	{
		gl3_sofframe.num_alphaquads = 0;
	}

	GL3_RenderFrameInternal(&rd);

	fd->scratch = NULL;

	if (gl3_ghoul)
	{
		GhlVoid0(32); /* end of the renderer frame */
	}

	/* keep the frame data for 2D calls of this frame (world labels), but no SoF lists */
	gl3_sofframe.num_alphaquads = 0;
	gl3_sofframe.num_quadchains = 0;
	gl3_sofframe.active = false;
}

/* ------------------------------------------------------------------
 * export slots
 * ------------------------------------------------------------------ */

static void
SoF_TouchAllImages(void)
{
	GL3_SoF_TouchAllImages();
}

static void
SoF_BeginRegistration(char *mapname)
{
	GL3_BeginRegistration(mapname);
}

extern struct model_s *GL3_SoF_RegisterModel(const char *name);

static void *
SoF_RegisterModel(char *name)
{
	return GL3_SoF_RegisterModel(name);
}

static void *
SoF_RegisterSkin(char *name)
{
	return GL3_SoF_FindImage(name, SOFIMG_SKIN, true);
}

static void
SoF_UnregisterSkin(char *name)
{
	GL3_SoF_FreeImageByName(name, SOFIMG_SKIN);
}

static void *
SoF_RegisterPic(char *name)
{
	return GL3_SoF_FindImage(name, SOFIMG_PIC, true);
}

static void
SoF_UnregisterPic(char *name)
{
	GL3_SoF_FreeImageByName(name, SOFIMG_PIC);
}

static void *
SoF_RegisterSprite(char *name)
{
	return GL3_SoF_FindImage(name, SOFIMG_SPRITE, true);
}

static void
SoF_UnregisterSprite(char *name)
{
	GL3_SoF_FreeImageByName(name, SOFIMG_SPRITE);
}

static void
SoF_SetSky(char *name, float rotate, float *axis)
{
	vec3_t a = {0, 0, 1};

	if (axis)
	{
		VectorCopy(axis, a);
	}

	GL3_SoF_SetSky(name ? name : "", rotate, a);
}

/* slot 0x34: no effect in 1.07f */
static void
SoF_SetCloud(float type, float density, float angle, float speed, float red, float green, float blue)
{
	(void)type; (void)density; (void)angle; (void)speed; (void)red; (void)green; (void)blue;
}

/* slot 0x38: terrain sets (SPEC-410 Terrain sets; I4c: gl3_terrain.c) */
static void
SoF_SetTerrain(char *name)
{
	GL3_Terrain_Select(name);
}

static void
SoF_EndRegistration(void)
{
	GL3_EndRegistration();
}

static void
SoF_RenderFrame(sofrefdef_t *fd)
{
	GL3_SoF_RenderFrame(fd);
}

void
GL3_SoF_LightPointExport(float *point, float *color_out, int dir)
{
	if (!point || !color_out)
	{
		return;
	}

	if (!gl3_worldmodel)
	{
		color_out[0] = color_out[1] = color_out[2] = 1;
		return;
	}

	GL3_SoF_LightPoint(point, color_out, dir & 0xFF);
}

/* slot 0xa8: the texinfo switches to its image's damaged variant and stops animating */
static void
SoF_DamageTexture(int texinfo, int unused)
{
	short ti = (short)(texinfo & 0xFFFF);
	mtexinfo_t *tex;

	(void)unused;

	if (!gl3_worldmodel || (ti < 0) || (ti >= gl3_worldmodel->numtexinfo))
	{
		return;
	}

	tex = &gl3_worldmodel->texinfo[ti];

	if (tex->image && tex->image->damage)
	{
		tex->image = tex->image->damage;
		tex->next = NULL;
		tex->numframes = 1;
	}
}

/* slot 0xc0: colour-correction table for images loaded from now on */
static void
SoF_SetColorCorrection(float gamma, float brightness, float contrast, float scale)
{
	GL3_SoF_BuildColorTable(gamma, brightness, contrast, scale);

	if (gl3_ghoul)
	{
		GhlPtr(37, gl3_colortable);
	}
}

/* module-only slots when hosted through the Yamagi interface */
static int
SoF_NoInit(void *a, void *b, void *c)
{
	(void)a;
	(void)b;

	/* I4b: an OpenMerc engine hands its client Ghoul system through the SPEC-002 Init slot
	   (the window and context already exist); NULL detaches it */
	if (c != gl3_ghoul)
	{
		GL3_SoF_GhoulShutdown();
		gl3_ghoul = c;
		GL3_SoF_GhoulStartup();
	}

	return 1;
}
static void SoF_Nop(void) { }
static void SoF_NopInt(int a) { (void)a; }

static sofrefexport_t sofexports;

const sofrefexport_t *
GL3_SoF_ExportTable(qboolean module)
{
	sofrefexport_t *re = &sofexports;

	memset(re, 0, sizeof(*re));
	re->api_version = SOF_REF_API_VERSION;
	re->Init = SoF_NoInit;
	re->Shutdown = SoF_Nop;
	re->TouchAllImages = SoF_TouchAllImages;
	re->BeginRegistration = SoF_BeginRegistration;
	re->RegisterModel = SoF_RegisterModel;
	re->RegisterSkin = SoF_RegisterSkin;
	re->UnregisterSkin = SoF_UnregisterSkin;
	re->RegisterPic = SoF_RegisterPic;
	re->UnregisterPic = SoF_UnregisterPic;
	re->RegisterSprite = SoF_RegisterSprite;
	re->UnregisterSprite = SoF_UnregisterSprite;
	re->SetSky = SoF_SetSky;
	re->SetCloud = SoF_SetCloud;
	re->SetTerrain = SoF_SetTerrain;
	re->EndRegistration = SoF_EndRegistration;
	re->RenderFrame = SoF_RenderFrame;
	re->DrawGetPicSize = SoF_DrawGetPicSize;
	re->DrawPic = SoF_DrawPic;
	re->DrawPicScaled = SoF_DrawPicScaled;
	re->DrawPicPart = SoF_DrawPicPart;
	re->DrawStretchPic = SoF_DrawStretchPic;
	re->DrawChar = SoF_DrawChar;
	re->DrawString = SoF_DrawString;
	re->DrawConsoleString = SoF_DrawConsoleString;
	re->DrawConsoleStringColor = SoF_DrawConsoleStringColor;
	re->unused68 = NULL;
	re->DrawCharFloat = SoF_DrawCharFloat;
	re->DrawTileClear = SoF_DrawTileClear;
	re->DrawFill = SoF_DrawFill;
	re->DrawBevelBox = SoF_DrawBevelBox;
	re->DrawLine = SoF_DrawLine;
	re->unused80 = NULL;
	re->DrawFadeScreen = SoF_DrawFadeScreen;
	re->DrawPolygon = SoF_DrawPolygon;
	re->SetClipRect = SoF_SetClipRect;
	re->ResetClipRect = SoF_ResetClipRect;
	re->ClipRightTo = SoF_ClipRightTo;
	re->ClipLeftFrom = SoF_ClipLeftFrom;
	re->ReapplyClipRect = SoF_ReapplyClipRect;
	re->SetRawImage = (void (*)(char *, int, int, unsigned char *))GL3_SoF_SetRawImage;
	re->LightPoint = GL3_SoF_LightPointExport;
	re->DamageTexture = SoF_DamageTexture;
	re->DrawWorldLabel = SoF_DrawWorldLabel;
	re->BeginFrame = SoF_Nop;
	re->Set2D = SoF_Set2D;
	re->Set3DInMenu = SoF_Set3DInMenu;
	re->EndFrame = SoF_Nop;
	re->SetColorCorrection = SoF_SetColorCorrection;
	re->AppActivate = SoF_NopInt;
	re->DrawFontString = SoF_DrawFontString;
	re->FontStringWidth = SoF_FontStringWidth;
	re->FontLineHeight = SoF_FontLineHeight;
	re->ReadThumbnail = SoF_ReadThumbnail;
	re->A3DExportGeometry = NULL; /* A3D geometry export: not implemented */

	(void)module;
	return re;
}

#ifndef OM_SOFREF
/* OpenMerc V1: uniform scale of the SoF 2D slots for the HUD (scr_hudscale); not part of the SoF ABI table */
Q2_DLL_EXPORTED void
OM_Set2DScale(float scale)
{
	GL3_SoF2D_SetScale(scale);
}

/* OpenMerc MENU-SCRATCH: RGBA image with alpha (menu TrueType glyph atlases); returns whether the
   name is loaded when rgba is NULL */
Q2_DLL_EXPORTED int
OM_SetAlphaImage(const char *name, int w, int h, const unsigned char *rgba)
{
	if (!rgba)
	{
		return GL3_SoF_ImageLoaded(name);
	}

	GL3_SoF_SetAlphaImage(name, w, h, rgba);
	return GL3_SoF_ImageLoaded(name);
}

/* Yamagi-hosted renderer: the SPEC-002 table for engine code that draws through the SoF slots */
Q2_DLL_EXPORTED const sofrefexport_t *
OM_GetSoFRefExports(void)
{
	return GL3_SoF_ExportTable(false);
}

/* OpenMerc (I5): the Yamagi interface has no SPEC-002 import table, so an OpenMerc engine hands over the data
   of import 0x68 here: the direction table of SPEC-201 Appendix A (250 entries, SPEC-AMEND-005 I4a.4) that
   slot 0xa4 and lit alpha quads sample along */
Q2_DLL_EXPORTED void
OM_SetSoFDirTable(const float (*table)[3], int count)
{
	GL3_SoF_SetDirTable(table, count);
}
#endif
