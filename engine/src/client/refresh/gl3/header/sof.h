/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: declarations of the Soldier of Fortune parts
 * (image semantics, 2D and fonts, sky, fog regions, detail pass, alpha quads
 * and quad chains, sorted translucent pass, screen overlays, renderer cvars,
 * the SPEC-002 export table).
 *
 * Implements: SPEC-002, SPEC-410, SPEC-412, SPEC-413, SPEC-AMEND-002
 */

#ifndef SRC_CLIENT_REFRESH_GL3_HEADER_SOF_H_
#define SRC_CLIENT_REFRESH_GL3_HEADER_SOF_H_

#include "../../../../common/header/sof/refapi.h"

/* image types of SPEC-002 (gl3image_t.softype) */
enum
{
	SOFIMG_SKIN = 0,
	SOFIMG_SPRITE = 1,
	SOFIMG_WALL = 2,
	SOFIMG_PIC = 3,
	SOFIMG_SKY = 4,
	SOFIMG_DETAIL = 5,
	SOFIMG_RAW = 6,
	SOFIMG_FONT = 7,
	SOFIMG_ENV = 8,
	SOFIMG_INTERNAL = 9 /* renderer-made images (not in SPEC-002) */
};

/* .m32 header fields used by the renderer (SPEC-100 offsets) */
#define M32_OFS_ALTNAME 0x084
#define M32_OFS_DAMAGENAME 0x184
#define M32_OFS_WIDTHS 0x204
#define M32_OFS_HEIGHTS 0x244
#define M32_OFS_OFFSETS 0x284
#define M32_OFS_FLAGS 0x2C4
#define M32_OFS_VALUE 0x2CC
#define M32_OFS_PICTIPSKIP 0x2D8
#define M32_OFS_DETAILNAME 0x2DC
#define M32_OFS_DETAILSCALE 0x35C
#define M32_OFS_DETAILSHIFT 0x364
#define M32_OFS_KIND 0x378
#define M32_HEADER_SIZE 0x3C8

/* texinfo flags interpreted by the SoF renderer (SPEC-410) */
#define SOFSURF_SKY 0x4
#define SOFSURF_WARP 0x8
#define SOFSURF_FLOWING 0x40
#define SOFSURF_NODETAIL 0x400
#define SOFSURF_ALPHA 0x800
#define SOFSURF_ANIMRATE 0x1000
#define SOFSURF_WAVES 0x2000
#define SOFSURF_FOGBOUNDARY 0x10000
#define SOFSURF_SPHEREMAP 0x20000

/* per-frame data of the SPEC-002 frame record that Quake II's refdef lacks */
typedef struct
{
	qboolean active;          /* this frame came through the SPEC-002 RenderFrame */
	float gametime;           /* refdef 0x50, stops while paused */
	float gametime_delta;     /* refdef 0x4c */
	float clienttime;         /* refdef 0x44 */
	sofrefdef_t *fd;
	int num_entities;
	sofentity_t **entities;   /* parallel to r_newrefdef.entities */
	int num_quadchains;
	sofquadchain_t **quadchains;
	int num_alphaquads;
	sofalphaquad_t **alphaquads;
} gl3sofframe_t;

extern gl3sofframe_t gl3_sofframe;

/* game clock of the current frame (refdef 0x50; Quake II refdefs: time) */
float GL3_GameTime(void);

/* ---------------- gl3_image.c (SPEC-413 §8, §9) ---------------- */
extern byte gl3_colortable[256];
void GL3_SoF_BuildColorTable(float gamma, float brightness, float contrast, float scale);
/* lightmap colour table for the lightmapped world shaders (GL_TEXTURE5), once per 3D view */
void GL3_SoF_BindLightTable(qboolean fullbright);
/* I4c: gl_monolightmap mode of the lightmapped world shaders (gl3_shaders.c), once per 3D view */
void GL3_SoF_ApplyMonoLightmap(qboolean fullbright);
struct image_s *GL3_SoF_FindImage(const char *name, int softype, qboolean create);
void GL3_SoF_FreeImageByName(const char *name, int softype);
void GL3_SoF_SetRawImage(const char *name, int w, int h, const byte *rgba);
void GL3_SoF_SetAlphaImage(const char *name, int w, int h, const byte *rgba); /* MENU-SCRATCH */
qboolean GL3_SoF_ImageLoaded(const char *name);
void GL3_SoF_BeginRegistrationImages(void);
void GL3_SoF_FreeUnusedImages(void);
void GL3_SoF_TouchAllImages(void);
void GL3_SoF_UpdateDetailScroll(float delta);
void GL3_SoF_ApplyTextureFilters(void);
void GL3_SoF_InitImages(void);
struct image_s *GL3_SoF_NoTexture(void);
qboolean GL3_SoF_CheckFormatCvar(cvar_t *cvar, const char *def);

/* ---------------- gl3_sof2d.c (SPEC-412 §2-§7, §12) ---------------- */
void GL3_SoF2D_Init(void);
void GL3_SoF2D_Shutdown(void);
void GL3_SoF2D_Flush(void);
void GL3_SoF2D_Enter2D(qboolean restore);
void GL3_SoF2D_Set3DInMenu(int flag);
void GL3_SoF2D_ResetState(void);
void GL3_SoF2D_InitFonts(void);
void GL3_SoF2D_ShutdownFonts(void);
void GL3_SoF2D_SetScale(float scale); /* OpenMerc V1: HUD scaling */
void GL3_SoF2D_ScissorReset(void);
void GL3_SoF2D_TexturedQuad(GLuint texnum, const float xy[4][2], const float st[4][2], const byte rgba[4]);
void GL3_SoF_ScreenShot_f(void);
void GL3_SoF_ScreenShotFrame(void);
void GL3_SoF_RawShot_f(void);

void SoF_DrawGetPicSize(int *w, int *h, char *name);
void SoF_DrawPic(int x, int y, char *name, unsigned int rgba);
void SoF_DrawPicScaled(int x, int y, float sx, float sy, const unsigned char *rgba, char *name);
void SoF_DrawPicPart(int x, int y, int left, int top, int right, int bottom, const unsigned char *rgba, char *name);
void SoF_DrawStretchPic(int x, int y, int w, int h, const unsigned char *rgba, char *name, int rotation);
void SoF_DrawChar(int x, int y, int c, const unsigned char *rgba);
void SoF_DrawString(int x, int y, char *s, unsigned int rgba, int maxlen);
void SoF_DrawConsoleString(int x, int y, char *s, int maxlen);
void SoF_DrawConsoleStringColor(int x, int y, char *s, int maxlen, const unsigned char *rgba);
void SoF_DrawCharFloat(float x, float y, float scale, const unsigned char *rgba, int c);
void SoF_DrawTileClear(int x, int y, int w, int h, char *name);
void SoF_DrawFill(int x, int y, int w, int h, const unsigned char *rgba);
void SoF_DrawBevelBox(int x, int y, int w, int h, const unsigned char *rgba);
void SoF_DrawLine(float x1, float y1, float x2, float y2, float width, const unsigned char *rgba);
void SoF_DrawFadeScreen(const unsigned char *rgba);
void SoF_DrawPolygon(int unused1, int unused2, int n, sofvec3_t *verts, const unsigned char *rgba);
void SoF_SetClipRect(int x, int y, int w, int h);
void SoF_ResetClipRect(void);
void SoF_ClipRightTo(int x);
void SoF_ClipLeftFrom(int x);
void SoF_ReapplyClipRect(void);
void SoF_DrawWorldLabel(float *origin, char *text, char *pic, unsigned int rgba);
void SoF_Set2D(int restore);
void SoF_Set3DInMenu(int flag);
void SoF_DrawFontString(int x, int y, char *text, unsigned int rgba, const om_fontref_t *font, int keep);
int SoF_FontStringWidth(char *text, const om_fontref_t *font);
int SoF_FontLineHeight(const om_fontref_t *font);
void SoF_ReadThumbnail(unsigned char *out, int w, int h);

/* the 32 text colours of SPEC-002 */
extern const byte sof_textcolors[32][4];

/* ---------------- gl3_fx.c (SPEC-412 §8-§11, SPEC-410 light sampling) ---------------- */
void GL3_FX_Init(void);
void GL3_FX_Shutdown(void);
void GL3_FX_Flush(void);
void GL3_FX_BeginFrame3D(void);
void GL3_FX_SortedPass(void);
void GL3_FX_ScreenOverlays(void);
void GL3_FX_AddAlphaSurface(msurface_t *surf, entity_t *ent);
void GL3_SoF_LightPoint(const vec3_t p, vec3_t color, int dir);
void GL3_SoF_ClearLightCache(void);
void GL3_SoF_SetDirTable(const float (*table)[3], int count);

/* the 3D effect vertex: position, texture coordinates, colour */
typedef struct
{
	float pos[3];
	float st[2];
	float rgba[4];
} gl3fxvtx_t;

/* render state of an effect batch */
typedef struct
{
	GLuint texnum;       /* 0 = untextured */
	int blend;           /* 0 off, 1 normal, 2 additive, 3 darkening, 4 multiply, 5 detail (dst,src) */
	qboolean alphatest;  /* > 0.05 */
	qboolean depthtest;
	qboolean depthwrite;
	qboolean polyoffset;
	int texmode;         /* 0 modulate, 1 replace */
	int fogmode;         /* 0 none, 1 linear fog (fogstart/fogend/fogcolor) */
	float fogstart, fogend;
	float fogcolor[4];
	qboolean culling;
	int soft;            /* GFX-modern soft particles: 0 off, 1 fade alpha, 2 fade colour */
	/* GFX-modern r_om_ppfog: fog region overlay evaluated per fragment (gl3_fog.c); vertex alpha = shimmer */
	int ppfog;
	float pf0[4];        /* eye xyz, 1 */
	float pf1[4];        /* start, range, max alpha (0..255), height falloff */
	float pf2[4];        /* maxs z, size z, alpha scale, number of planes */
	float pfpl[5][4];    /* boundary planes still shortening: normal, dist */
} gl3fxstate_t;

void GL3_FX_SetState(const gl3fxstate_t *st);
void GL3_FX_DefaultState(gl3fxstate_t *st);
void GL3_FX_Add(const gl3fxvtx_t *v, int numverts, GLenum mode);

/* ---------------- gl3_fog.c (SPEC-410 fog regions) ---------------- */
void GL3_Fog_LoadRegions(gl3model_t *mod, const byte *mod_base, const lump_t *lfaces,
	const lump_t *lpolys, const lump_t *lregions, const lump_t *lleafs);
void GL3_Fog_FreeRegions(void);
void GL3_Fog_BeginFrame(void);
void GL3_Fog_QueueSurface(msurface_t *surf, entity_t *ent);
void GL3_Fog_QueueModelSurface(msurface_t *surf, int region, const vec3_t origin, const vec3_t angles);
void GL3_Fog_DrawPass(qboolean first);
/* I4c: alpha quads that passed the collection test this frame (r_speeds 1 "qp") */
extern int gl3_c_alphaquads;
/* I4c: fog-region polygons drawn this frame (r_speeds 1 "fp") */
extern int gl3_c_fogpolys;
/* I4c: overlay of one translucent world face right after the face (sorted pass) */
void GL3_Fog_DrawSurfaceNow(msurface_t *surf);
void GL3_Fog_MarkBoundaryPlane(msurface_t *surf);
int GL3_Fog_RegionOfPoint(const vec3_t p);
float GL3_Fog_PointFactor(int region, const vec3_t p, float fogcolor_out[4]);
int GL3_Fog_NumRegions(void);
void GL3_Fog_Regions_f(void);
void GL3_Fog_ViewTest(refdef_t *rd);
int GL3_Fog_VolumeRegions(float *out, int max);
float GL3_VolFog_OverlayDim(void);
void GL3_Post_VolFog(void);
int GL3_Fog_SurfaceRegion(msurface_t *surf);
/* I4c: gl_fogmode 1 (OpenGL linear fog on interior region faces while drawn) */
int GL3_Fog_HardwareRegion(msurface_t *surf);
int GL3_Fog_HardwareModelRegion(msurface_t *surf, int modelregion);
void GL3_Fog_HardwareParams(int region, float *start, float *end, float color[3]);
qboolean GL3_Fog_SetHardwareFog(int region, gl3Uni3D_t *u);
void GL3_Fog_SetHardwareFxState(int region, gl3fxstate_t *st);

/* ---------------- gl3_warp.c (SPEC-410 sky) ---------------- */
void GL3_SoF_SetSky(const char *name, float rotate, vec3_t axis);

/* ---------------- gl3_surf.c (SPEC-410 detail pass) ---------------- */
void GL3_Detail_Queue(msurface_t *surf, gl3image_t *image, entity_t *ent);
void GL3_Detail_DrawPass(void);
gl3image_t *GL3_SoF_TextureAnimation(const mtexinfo_t *tex, int entframe);

/* ---------------- gl3_avi.c (SPEC-412 §13; package I4c) ---------------- */
void GL3_Avi_f(void);
void GL3_Avi_Frame(void);
void GL3_Avi_Shutdown(void);

/* ---------------- gl3_dynlight.c (SPEC-410 dynamic lights on lightmaps; package I4c) ---------------- */
void GL3_DynLight_Free(void);
void GL3_DynLight_BindPage(int page);
void GL3_DynLight_BeginFrame(void);
void GL3_DynLight_Face(msurface_t *surf);
/* GFX-modern r_om_pdlight: SoF dlights per pixel instead of per lightmap sample this view */
qboolean GL3_DynLight_PerPixel(void);
unsigned GL3_DynLight_PixelFlags(msurface_t *surf);
void GL3_DynLight_SetupView(gl3Uni3D_t *u);
void GL3_DynLight_TestLight(refdef_t *rd);

/* ---------------- gl3_liquid.c (SPEC-410 Liquids; package I4c) ---------------- */
void GL3_Liquid_Init(void);
void GL3_Liquid_Draw(msurface_t *surf, entity_t *ent, qboolean translucent, int fogregion);

/* ---------------- gl3_terrain.c (SPEC-410 Terrain sets, SPEC-144; package I4c) ---------------- */
void GL3_Terrain_Select(const char *name);
void GL3_Terrain_Draw(void);
void GL3_Terrain_Free(void);
extern int gl3_c_terrainpolys;
/* the engine's flrand (SPEC-002 import 0x60), set by the SoF module; NULL = local random numbers */
extern float (*gl3_flrand_fn)(float min, float max);

/* ---------------- gl3_sofcvar.c (SPEC-413) ---------------- */
void GL3_SoF_RegisterCvars(void);
extern cvar_t *gl_picmip, *gl_pictip, *gl_skymip, *r_isf, *r_iaf, *vid_brightness, *vid_contrast;
extern cvar_t *gl_modulate, *gl_monolightmap, *gl_textureclamp, *fs_createpak;
extern cvar_t *gl_ext_anisotropic, *gl_scissor_broken, *gl_gentextures_broken;
extern cvar_t *r_nearclipdist, *r_farclipdist, *gl_clear, *gl_ztrick, *gl_drawmode, *gl_drawflat;
extern cvar_t *gl_dynamic, *gl_saturatelighting, *gl_dlightintensity, *gl_detailtexturing;
extern cvar_t *gl_detailtexturingdistance, *gl_offsetfactor, *gl_offsetunits, *gl_effectfill;
extern cvar_t *gl_showeffectfill, *gl_fogmode, *gl_fogverts, *gl_fogcolor, *gl_fogdensity, *gl_fogstart;
extern cvar_t *gl_fogend, *gl_fogtime, *gl_fogdensityrange, *r_warnings, *gl_swapinterval;
extern cvar_t *r_nocull, *gl_driver, *gl_mode, *gl_bitdepth, *gl_displayrefresh, *gl_allow_cds;
extern cvar_t *gl_allow_software, *vid_card, *cpu_memory_level, *cpu_speed_level, *cpu_memory_using;
extern cvar_t *gl_ext_multitexture, *gl_s3_s3tc, *gl_ext_swapinterval, *s3tc_avail;
extern cvar_t *ghl_mip, *ghl_poly_fact, *ghl_poly_unit, *ghl_specular, *ghl_light_method;
extern float gl3_modulate_latched;

/* a renderer-private cvar (not registered with the engine) in the SoF module build,
   an engine cvar otherwise: for Yamagi renderer settings that SoF does not have */
cvar_t *GL3_LocalCvar(const char *name, const char *value, int flags);

/* ---------------- gl3_sof.c / gl3_sofapi.c (SPEC-002) ---------------- */
const sofrefexport_t *GL3_SoF_ExportTable(qboolean module);
void GL3_SoF_RenderFrame(sofrefdef_t *fd);
void GL3_SoF_LightPointExport(float *point, float *color_out, int dir);

/* the Ghoul system object handed to Init (SPEC-010), or NULL */
extern void *gl3_ghoul;

/* world loaded: lump 20 records to the Ghoul world-light set (SPEC-410 Model lights) */
void GL3_SoF_WorldLights(const byte *lights, int len);
void GL3_SoF_GhoulStartup(void);
void GL3_SoF_GhoulShutdown(void);
struct model_s *GL3_SoF_RegisterModel(const char *name);

/* SoF module: the GL 1.1 table for the Ghoul system and the fixed-function switch (gl3_ghoulgl.c) */
void *GL3_Ghoul_GLTable(void);
void GL3_Ghoul_ResetGLTable(void);
void GL3_Ghoul_EnterFixedFunction(qboolean blend, qboolean modulate, const byte color[4]);
void GL3_Ghoul_LeaveFixedFunction(void);
void GL3_Ghoul_SetProjection(const float *projection);
void GL3_Ghoul_Trace(qboolean on, const char *path);
void GL3_Ghoul_TraceNote(const char *text);

/* SoF module: ask the engine for a renderer restart (vid_ref modified) */
void GL3_SoF_RequestRestart(void);

/* Quake II refdef entry of the core (gl3_main.c) */
void GL3_RenderFrameInternal(refdef_t *fd);

/* user directory and free file name (SPEC-002 imports 0x38 / 0x3c, set by the SoF module) */
extern const char *(*gl3_userdir_fn)(void);
extern qboolean (*gl3_nextfreename_fn)(char *path);
const char *GL3_UserDir(void);
qboolean GL3_NextFreeName(char *path);

/* extra mip levels to drop for images loaded now (sky) */
extern int gl3_sof_extramip;

/* 2D quad drawn with the blend function set by the caller (night-vision multiply) */
void GL3_SoF2D_DrawMultiply(GLuint tex, const float xy[4][2], const float st[4][2], const byte rgba[4]);

/* ---------------- gl3_post.c (GFX-modern post path: bloom from emissive sources, soft particles) ---------------- */
typedef struct
{
	qboolean active; /* the 3D view is drawn into the post framebuffer */
	qboolean bloom;  /* emissive values are wanted this view */
	qboolean soft;   /* soft particles this view */
	qboolean volfog; /* volumetric fog in the fog regions this view */
} gl3post_t;

extern gl3post_t gl3post;

void GL3_Post_Init(void);
void GL3_Post_Shutdown(void);
qboolean GL3_Post_BeginView(int x, int y, int w, int h);
void GL3_Post_DepthCopy(void);
qboolean GL3_Post_DepthReady(void);
void GL3_Post_EndView(void);
char *GL3_Post_WrapFragment(const char *src, const char *src2);
void GL3_Post_BindOutputs(GLuint prog);
float GL3_Post_SurfEmissive(const msurface_t *surf);
void GL3_Post_EmisForBlend(GLenum src, GLenum dst, qboolean blend, float glow, float out[4]);
void GL3_Post_FxEmis(int blend, float out[4]);
float GL3_Post_GhoulMin(void);
float GL3_Post_FullbrightScale(void);
int GL3_Post_SoftMode(int blend, qboolean billboard, qboolean depthtest);
void GL3_Post_SoftUniforms(GLint uparams, GLint uview, int mode);
void GL3_Post_GpuTimer(qboolean begin);
float GL3_Post_Overbright(void);
float GL3_Post_OverbrightModels(void);

/* GFX-modern: cascaded sun shadows (gl3_csm.c) */
void GL3_CSM_Init(void);
void GL3_CSM_Shutdown(void);
void GL3_CSM_LoadSun(const char *ents, int len);
void GL3_CSM_Frame(void);
qboolean GL3_CSM_Active(void);
qboolean GL3_CSM_ReplaceGhoul(void);
const char *GL3_CSM_Glsl(void);
char *GL3_CSM_Prepend(const char *src);
void GL3_CSM_BindProgram(GLuint prog);
const float *GL3_CSM_InvPV(void);
void GL3_CSM_AddCasterTris(const float *clip, int numverts, int stride);


#endif
