/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I4b (Ghoul model drawing). OpenMerc GL3 renderer: the data blocks
 * passed to the Ghoul system when drawing (SPEC-011 matrix and
 * render-parameter block), the GL 1.1 calls the renderer makes around a
 * Ghoul draw, and the GL 1.1 emulation used where no compatibility context
 * exists (gl3_ghoulemu.c).
 *
 * Implements: SPEC-011 (matrix, render parameters), SPEC-010 G (GL table),
 *             SPEC-411 §5
 */

#ifndef SRC_CLIENT_REFRESH_GL3_HEADER_GL3_GHOUL_H_
#define SRC_CLIENT_REFRESH_GL3_HEADER_GL3_GHOUL_H_

#include "../../../../common/header/sof/refapi.h"

/* SPEC-011 matrix, 68 bytes: rows = images of X (forward), Y (left), Z (up), translation */
typedef struct
{
	float m[4][4];
	unsigned int flags; /* bit 0: identity */
} ghlmatrix_t;

/* SPEC-011 render parameters, 180 bytes in the 32-bit build; the first word is the
   render-cull method table (one entry: bool BoxOutside(const float corners[8][3])) */
typedef struct
{
	void *const *vtable;            /* 0x00 */
	float time;                     /* 0x04 refdef game clock */
	float lightColor[3];            /* 0x08 */
	float lightDir[3];              /* 0x14 */
	float origin[3];                /* 0x20 */
	unsigned char reserved2c;       /* 0x2c */
	unsigned char dirValid;         /* 0x2d */
	unsigned char useGLLights;      /* 0x2e */
	unsigned char specular;         /* 0x2f */
	unsigned char fastLights;       /* 0x30 */
	unsigned char hardwareLights;   /* 0x31 */
	unsigned char glDirectional;    /* 0x32 */
	unsigned char glAttenuation;    /* 0x33 */
	unsigned char castShadow;       /* 0x34 entity flag 0x1000 */
	unsigned char shadowOnly;       /* 0x35 RF_VIEWERMODEL */
	unsigned char noTextureStack;   /* 0x36 */
	unsigned char unused37;         /* 0x37 */
	float shadowDarkness;           /* 0x38 */
	float fogVisibility;            /* 0x3c */
	float shadowDist;               /* 0x40 */
	float (*worldTrace)(const float *start, const float *end); /* 0x44 */
	int shadowMode;                 /* 0x48 */
	int maxLights;                  /* 0x4c */
	ghlmatrix_t entityToWorld;      /* 0x50 */
	float viewOrigin[3];            /* 0x94 */
	float viewForward[3];           /* 0xa0 */
	unsigned char noDynamicLights;  /* 0xac */
	unsigned char padAD[3];
	void *entity;                   /* 0xb0 SPEC-002 entity */
} ghlparams_t;

#if defined(_WIN32) && !defined(_WIN64)
typedef char ghl_params_size_check[(sizeof(ghlparams_t) == 180) ? 1 : -1];
typedef char ghl_matrix_size_check[(sizeof(ghlmatrix_t) == 68) ? 1 : -1];
#define GHLAPI __stdcall
#else
#define GHLAPI
#endif

/* GL 1.1 enumerants the GL 3 headers do not have */
#define GHL_GL_MODELVIEW 0x1700
#define GHL_GL_PROJECTION 0x1701
#define GHL_GL_TEXTURE 0x1702
#define GHL_GL_FLAT 0x1D00
#define GHL_GL_SMOOTH 0x1D01
#define GHL_GL_ALPHA_TEST 0x0BC0
#define GHL_GL_FOG 0x0B60
#define GHL_GL_FOG_DENSITY 0x0B62
#define GHL_GL_FOG_START 0x0B63
#define GHL_GL_FOG_END 0x0B64
#define GHL_GL_FOG_MODE 0x0B65
#define GHL_GL_FOG_COLOR 0x0B66
#define GHL_GL_LIGHTING 0x0B50
#define GHL_GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GHL_GL_LIGHT_MODEL_TWO_SIDE 0x0B52
#define GHL_GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GHL_GL_COLOR_MATERIAL 0x0B57
#define GHL_GL_NORMALIZE 0x0BA1
#define GHL_GL_TEXTURE_GEN_S 0x0C60
#define GHL_GL_TEXTURE_GEN_T 0x0C61
#define GHL_GL_TEXTURE_ENV 0x2300
#define GHL_GL_TEXTURE_ENV_MODE 0x2200
#define GHL_GL_MODULATE 0x2100
#define GHL_GL_DECAL 0x2101
#define GHL_GL_REPLACE 0x1E01
#define GHL_GL_S 0x2000
#define GHL_GL_T 0x2001
#define GHL_GL_TEXTURE_GEN_MODE 0x2500
#define GHL_GL_OBJECT_LINEAR 0x2401
#define GHL_GL_EYE_LINEAR 0x2400
#define GHL_GL_SPHERE_MAP 0x2402
#define GHL_GL_LIGHT0 0x4000
#define GHL_GL_AMBIENT 0x1200
#define GHL_GL_DIFFUSE 0x1201
#define GHL_GL_SPECULAR 0x1202
#define GHL_GL_POSITION 0x1203
#define GHL_GL_SPOT_DIRECTION 0x1204
#define GHL_GL_SPOT_EXPONENT 0x1205
#define GHL_GL_SPOT_CUTOFF 0x1206
#define GHL_GL_CONSTANT_ATTENUATION 0x1207
#define GHL_GL_LINEAR_ATTENUATION 0x1208
#define GHL_GL_QUADRATIC_ATTENUATION 0x1209
#define GHL_GL_EMISSION 0x1600
/* OpenMerc GFX-modern: Materialfv name of the Ghoul system's bloom source (ghoul_gl.h OM_BLOOM_SOURCE), never passed to OpenGL */
#define GHL_OM_BLOOM_SOURCE 0x7F10
#define GHL_GL_SHININESS 0x1601
#define GHL_GL_AMBIENT_AND_DIFFUSE 0x1602
#define GHL_GL_MODELVIEW_MATRIX 0x0BA6
#define GHL_GL_PROJECTION_MATRIX 0x0BA7
#define GHL_GL_TEXTURE_MATRIX 0x0BA8
#define GHL_GL_CURRENT_COLOR 0x0B00
#define GHL_GL_CLAMP 0x2900
#define GHL_GL_POINT 0x1B00
#define GHL_GL_LINE 0x1B01
#define GHL_GL_FILL 0x1B02
#define GHL_GL_QUADS 0x0007
#define GHL_GL_QUAD_STRIP 0x0008
#define GHL_GL_POLYGON 0x0009
#define GHL_GL_ALPHA 0x1906
#define GHL_GL_LUMINANCE 0x1909
#define GHL_GL_LUMINANCE_ALPHA 0x190A
#define GHL_GL_INTENSITY 0x8049
#define GHL_GL_ALPHA8 0x803C
#define GHL_GL_LUMINANCE8 0x8040
#define GHL_GL_INTENSITY8 0x804B

/* the fixed-function calls made by the renderer around instance method 41 */
typedef struct
{
	void (GHLAPI *Enable)(GLenum cap);
	void (GHLAPI *Disable)(GLenum cap);
	GLboolean (GHLAPI *IsEnabled)(GLenum cap);
	void (GHLAPI *MatrixMode)(GLenum mode);
	void (GHLAPI *LoadMatrixf)(const GLfloat *m);
	void (GHLAPI *LoadIdentity)(void);
	void (GHLAPI *Color4f)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
	void (GHLAPI *ShadeModel)(GLenum mode);
	void (GHLAPI *AlphaFunc)(GLenum func, GLfloat ref);
	void (GHLAPI *TexEnvf)(GLenum target, GLenum pname, GLfloat param);
	void (GHLAPI *LightModeli)(GLenum pname, GLint param);
	void (GHLAPI *Fogf)(GLenum pname, GLfloat param);
	void (GHLAPI *Fogfv)(GLenum pname, const GLfloat *params);
	void (GHLAPI *Fogi)(GLenum pname, GLint param);
	void (GHLAPI *PolygonMode)(GLenum face, GLenum mode);
	void (GHLAPI *DepthRange)(GLdouble n, GLdouble f);
	void (GHLAPI *CullFace)(GLenum mode);
	void (GHLAPI *BlendFunc)(GLenum s, GLenum d);
	void (GHLAPI *DepthMask)(GLboolean flag);
	void (GHLAPI *GetIntegerv)(GLenum pname, GLint *data);
} ghlff_t;

/* gl3_ghoul.c */
void GL3_Ghoul_DrawEntity(entity_t *e, sofentity_t *se);
void *GL3_Ghoul_Table(void);
void GL3_Ghoul_Shutdown(void);

/* menu model views (SPEC-412 §2, slot 0xb8 ... 0xb4): the fixed-function state the engine's Ghoul drawing
   starts from, on the native GL 1.1 functions (SoF module) or the emulation */
void GL3_Ghoul_MenuEnter(qboolean blend, qboolean modulate, const byte color[4]);
void GL3_Ghoul_MenuProjection(const float *projection);
void GL3_Ghoul_MenuLeave(void);
extern float (*gl3_ghoul_trace)(const float *start, const float *end);

/* gl3_ghoulemu.c: GL 1.1 for the Ghoul system on the GL 3 core profile */
qboolean GL3_GhoulEmu_FF(ghlff_t *out);
void *GL3_GhoulEmu_Table(void);
void GL3_GhoulEmu_Begin(void);
void GL3_GhoulEmu_SetEntityGlow(float glow);
void GL3_GhoulEmu_SetCsm(qboolean capture, qboolean replace);
void GL3_GhoulEmu_End(void);
void GL3_GhoulEmu_Shutdown(void);

/* gl3_fog.c (accessor added for I4b): hardware-fog parameters of a region (ghl_obj_fog 0) */
qboolean GL3_Fog_RegionLinear(int region, float *start, float *end, float color[4]);

#endif
