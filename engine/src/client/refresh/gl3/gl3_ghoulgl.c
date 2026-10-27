/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer, SoF module build: the OpenGL 1.1 entry-point table
 * handed to the engine's Ghoul model system (SPEC-010 interface G, system
 * method 1) and the switch between the renderer's shader state and the
 * fixed-function state the Ghoul system draws with (menu model views,
 * SPEC-412 §2 slot 0xb8; Ghoul entities of the 3D view are package I4b).
 *
 * The context is a compatibility-profile context, so the table holds the
 * system opengl32.dll functions themselves, with the substitutions of
 * SPEC-411 (texture binding through the renderer's cache and gl_nobind,
 * filters following gl_texturemode). The renderer does not defer state
 * changes, so entry 51 has nothing to apply.
 *
 * Implements: SPEC-010 (G: GL entry-point table, method 1), SPEC-411 (GL entry-point
 *             table supplied to the Ghoul system), SPEC-412 §2
 */

#ifdef OM_SOFREF

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "header/local.h"

#define GHL_GL_ENTRIES 52

static void *ghlgl[GHL_GL_ENTRIES];
static qboolean ghlgl_ready;

typedef void (WINAPI *pfnBindTexture)(GLenum, GLuint);
typedef void (WINAPI *pfnTexParameteri)(GLenum, GLenum, GLint);
typedef void (WINAPI *pfnDeleteTextures)(GLsizei, const GLuint *);
typedef void (WINAPI *pfnEnable)(GLenum);
typedef void (WINAPI *pfnMatrixMode)(GLenum);
typedef void (WINAPI *pfnLoadMatrixf)(const GLfloat *);
typedef void (WINAPI *pfnLoadIdentity)(void);
typedef void (WINAPI *pfnShadeModel)(GLenum);

static pfnBindTexture real_BindTexture;
static pfnTexParameteri real_TexParameteri;
static pfnDeleteTextures real_DeleteTextures;
typedef void (WINAPI *pfnMaterialfv)(GLenum, GLenum, const GLfloat *);
static pfnMaterialfv real_Materialfv;

#ifndef GL_TEXTURE_2D_ENABLE
#define GL_FLAT_SHADE 0x1D00
#define GL_MODELVIEW_MODE 0x1700
#define GL_PROJECTION_MODE 0x1701
#endif

/* BindTexture through the renderer's binding cache (gl_nobind applies) */
static void WINAPI
GhlBindTexture(GLenum target, GLuint texture)
{
	extern gl3image_t *draw_chars;

	if (gl_nobind->value && draw_chars)
	{
		texture = draw_chars->texnum;
	}

	if (target == GL_TEXTURE_2D)
	{
		GL3_SelectTMU(GL_TEXTURE0);
		gl3state.currenttexture = texture;
	}

	real_BindTexture(target, texture);
}

/* filters follow gl_texturemode; NEAREST (_NOMIP textures) is kept */
static void WINAPI
GhlTexParameteri(GLenum target, GLenum pname, GLint param)
{
	if (target == GL_TEXTURE_2D)
	{
		if (pname == GL_TEXTURE_MAG_FILTER)
		{
			param = gl_filter_max;
		}
		else if (pname == GL_TEXTURE_MIN_FILTER)
		{
			if (param == GL_LINEAR_MIPMAP_NEAREST)
			{
				param = gl_filter_min;
			}
			else if (param == GL_LINEAR)
			{
				param = gl_filter_max;
			}
		}
	}

	real_TexParameteri(target, pname, param);
}

/* the Ghoul system's bloom source name is not OpenGL's (GFX-modern, gl3_post.c) */
static void WINAPI
GhlMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
	if (pname == 0x7F10) /* GHL_OM_BLOOM_SOURCE */
	{
		return;
	}

	real_Materialfv(face, pname, params);
}

static void WINAPI
GhlDeleteTextures(GLsizei n, const GLuint *textures)
{
	GLsizei i;

	for (i = 0; i < n; i++)
	{
		if (textures[i] == gl3state.currenttexture)
		{
			gl3state.currenttexture = 0;
		}
	}

	real_DeleteTextures(n, textures);
}

/* entry 51: apply deferred state - this renderer sends state at once */
static void WINAPI
GhlApplyState(void)
{
}

static const char *ghlnames[GHL_GL_ENTRIES - 1] = {
	"glAlphaFunc", "glBegin", "glBindTexture", "glBlendFunc", "glColor3f", "glColor4f", "glCullFace",
	"glDepthFunc", "glDepthMask", "glDeleteTextures",
	"glDisable", "glEnable", "glEnd", "glFrontFace", "glGetError", "glGenTextures", "glGetFloatv",
	"glGetIntegerv", "glLoadIdentity", "glLoadMatrixf",
	"glMatrixMode", "glMultMatrixf", "glNormal3f", "glPixelStorei", "glPolygonOffset", "glPopMatrix",
	"glPushMatrix", "glRotatef", "glShadeModel", "glTexCoord2f",
	"glTexEnvf", "glTexImage2D", "glTexParameteri", "glTexGeni", "glVertex3f", "glColor3fv", "glNormal3fv",
	"glTexCoord2fv", "glVertex3fv",
	"glLightModelf", "glLightModelfv", "glLightModeli", "glLightModeliv", "glLightf", "glLightfv",
	"glLighti", "glLightiv", "glMaterialf", "glMaterialfv", "glMateriali", "glMaterialiv"
};

/* ------------------------------------------------------------------
 * debugging: log the Ghoul system's GL calls of one frame (r_om_ghltrace 1)
 * ------------------------------------------------------------------ */

static void *ghlreal[GHL_GL_ENTRIES]; /* the entries while tracing is on */
static FILE *ghltrace;
static int ghlvertices;

#define GT(...) do { if (ghltrace) fprintf(ghltrace, __VA_ARGS__); } while (0)
#define R(i, type) ((type)ghlreal[i])

typedef void (WINAPI *v_e)(GLenum);
typedef void (WINAPI *v_ef)(GLenum, GLfloat);
typedef void (WINAPI *v_ee)(GLenum, GLenum);
typedef void (WINAPI *v_ui)(GLuint);
typedef void (WINAPI *v_b)(GLboolean);
typedef void (WINAPI *v_v)(void);
typedef void (WINAPI *v_fff)(GLfloat, GLfloat, GLfloat);
typedef void (WINAPI *v_ffff)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (WINAPI *v_ff)(GLfloat, GLfloat);
typedef void (WINAPI *v_pf)(const GLfloat *);
typedef void (WINAPI *v_eef)(GLenum, GLenum, GLfloat);
typedef void (WINAPI *v_eei)(GLenum, GLenum, GLint);
typedef void (WINAPI *v_eepf)(GLenum, GLenum, const GLfloat *);
typedef void (WINAPI *v_epf)(GLenum, const GLfloat *);
typedef void (WINAPI *v_ei)(GLenum, GLint);
typedef void (WINAPI *v_teximage)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (WINAPI *v_igen)(GLsizei, GLuint *);
typedef void (WINAPI *v_epi)(GLenum, GLint *);
typedef void (WINAPI *v_epff)(GLenum, GLfloat *);

static void WINAPI t_AlphaFunc(GLenum f, GLfloat r) { GT("AlphaFunc %x %g\n", f, r); R(0, v_ef)(f, r); }
static void WINAPI t_Begin(GLenum m) { GT("Begin %x\n", m); ghlvertices = 0; R(1, v_e)(m); }
static void WINAPI t_BindTexture(GLenum t, GLuint n) { GT("BindTexture %u\n", n); R(2, v_ee)(t, n); }
static void WINAPI t_BlendFunc(GLenum a, GLenum b) { GT("BlendFunc %x %x\n", a, b); R(3, v_ee)(a, b); }
static void WINAPI t_Color3f(GLfloat r, GLfloat g, GLfloat b) { if (ghlvertices < 3) GT("  Color3f %g %g %g\n", r, g, b); R(4, v_fff)(r, g, b); } /* I1c: colours logged (test aid) */
static void WINAPI t_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { if (ghlvertices < 3) GT("  Color4f %g %g %g %g\n", r, g, b, a); R(5, v_ffff)(r, g, b, a); } /* I1c */
static void WINAPI t_CullFace(GLenum m) { GT("CullFace %x\n", m); R(6, v_e)(m); }
static void WINAPI t_DepthFunc(GLenum m) { GT("DepthFunc %x\n", m); R(7, v_e)(m); }
static void WINAPI t_DepthMask(GLboolean m) { GT("DepthMask %d\n", m); R(8, v_b)(m); }
static void WINAPI t_DeleteTextures(GLsizei n, const GLuint *t) { GT("DeleteTextures %d\n", n); ((void (WINAPI *)(GLsizei, const GLuint *))ghlreal[9])(n, t); }
static void WINAPI t_Disable(GLenum c) { GT("Disable %x\n", c); R(10, v_e)(c); }
static void WINAPI t_Enable(GLenum c) { GT("Enable %x\n", c); R(11, v_e)(c); }
static void WINAPI t_End(void) { GT("End (%d vertices)\n", ghlvertices); R(12, v_v)(); }
static void WINAPI t_FrontFace(GLenum m) { GT("FrontFace %x\n", m); R(13, v_e)(m); }
static GLenum WINAPI t_GetError(void) { GLenum e = ((GLenum (WINAPI *)(void))ghlreal[14])(); GT("GetError -> %x\n", e); return e; }
static void WINAPI t_GenTextures(GLsizei n, GLuint *t) { R(15, v_igen)(n, t); GT("GenTextures %d -> %u\n", n, n > 0 ? t[0] : 0); }
static void WINAPI t_GetFloatv(GLenum p, GLfloat *v) { R(16, v_epff)(p, v); GT("GetFloatv %x -> %g %g %g %g\n", p, v[0], v[1], v[2], v[3]); }
static void WINAPI t_GetIntegerv(GLenum p, GLint *v) { R(17, v_epi)(p, v); GT("GetIntegerv %x -> %d\n", p, v[0]); }
static void WINAPI t_LoadIdentity(void) { GT("LoadIdentity\n"); R(18, v_v)(); }
static void WINAPI t_LoadMatrixf(const GLfloat *m) { GT("LoadMatrixf %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]); R(19, v_pf)(m); }
static void WINAPI t_MatrixMode(GLenum m) { GT("MatrixMode %x\n", m); R(20, v_e)(m); }
static void WINAPI t_MultMatrixf(const GLfloat *m) { GT("MultMatrixf %g %g %g %g / %g %g %g %g / %g %g %g %g / %g %g %g %g\n", m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]); R(21, v_pf)(m); }
static void WINAPI t_Normal3f(GLfloat a, GLfloat b, GLfloat c) { R(22, v_fff)(a, b, c); }
static void WINAPI t_PixelStorei(GLenum p, GLint v) { GT("PixelStorei %x %d\n", p, v); R(23, v_ei)(p, v); }
static void WINAPI t_PolygonOffset(GLfloat a, GLfloat b) { GT("PolygonOffset %g %g\n", a, b); R(24, v_ff)(a, b); }
static void WINAPI t_PopMatrix(void) { GT("PopMatrix\n"); R(25, v_v)(); }
static void WINAPI t_PushMatrix(void) { GT("PushMatrix\n"); R(26, v_v)(); }
static void WINAPI t_Rotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z) { GT("Rotatef %g %g %g %g\n", a, x, y, z); R(27, v_ffff)(a, x, y, z); }
static void WINAPI t_ShadeModel(GLenum m) { GT("ShadeModel %x\n", m); R(28, v_e)(m); }
static void WINAPI t_TexCoord2f(GLfloat s, GLfloat t) { R(29, v_ff)(s, t); }
static void WINAPI t_TexEnvf(GLenum a, GLenum b, GLfloat c) { GT("TexEnvf %x %x %g\n", a, b, c); R(30, v_eef)(a, b, c); }
static void WINAPI t_TexImage2D(GLenum t, GLint l, GLint f, GLsizei w, GLsizei h, GLint b, GLenum fm, GLenum ty, const void *p) { GT("TexImage2D level %d fmt %x %dx%d\n", l, f, w, h); R(31, v_teximage)(t, l, f, w, h, b, fm, ty, p); }
static void WINAPI t_TexParameteri(GLenum t, GLenum p, GLint v) { GT("TexParameteri %x %x\n", p, v); R(32, v_eei)(t, p, v); }
static void WINAPI t_TexGeni(GLenum c, GLenum p, GLint v) { GT("TexGeni %x %x %x\n", c, p, v); R(33, v_eei)(c, p, v); }
static void WINAPI t_Vertex3f(GLfloat x, GLfloat y, GLfloat z) { if (ghlvertices++ < 3) GT("  Vertex3f %g %g %g\n", x, y, z); R(34, v_fff)(x, y, z); }
static void WINAPI t_Color3fv(const GLfloat *v) { if (ghlvertices < 3) GT("  Color3fv %g %g %g\n", v[0], v[1], v[2]); R(35, v_pf)(v); } /* I1c */
static void WINAPI t_Normal3fv(const GLfloat *v) { R(36, v_pf)(v); }
static void WINAPI t_TexCoord2fv(const GLfloat *v) { R(37, v_pf)(v); }
static void WINAPI t_Vertex3fv(const GLfloat *v) { if (ghlvertices++ < 3) GT("  Vertex3fv %g %g %g\n", v[0], v[1], v[2]); R(38, v_pf)(v); }
static void WINAPI t_LightModelf(GLenum p, GLfloat v) { GT("LightModelf %x %g\n", p, v); R(39, v_ef)(p, v); }
static void WINAPI t_LightModelfv(GLenum p, const GLfloat *v) { GT("LightModelfv %x\n", p); R(40, v_epf)(p, v); }
static void WINAPI t_LightModeli(GLenum p, GLint v) { GT("LightModeli %x %d\n", p, v); R(41, v_ei)(p, v); }
static void WINAPI t_LightModeliv(GLenum p, const GLint *v) { GT("LightModeliv %x\n", p); ((void (WINAPI *)(GLenum, const GLint *))ghlreal[42])(p, v); }
static void WINAPI t_Lightf(GLenum l, GLenum p, GLfloat v) { GT("Lightf %x %x %g\n", l, p, v); R(43, v_eef)(l, p, v); }
static void WINAPI t_Lightfv(GLenum l, GLenum p, const GLfloat *v) { GT("Lightfv %x %x %g %g %g %g\n", l, p, v[0], v[1], v[2], v[3]); R(44, v_eepf)(l, p, v); }
static void WINAPI t_Lighti(GLenum l, GLenum p, GLint v) { GT("Lighti %x %x %d\n", l, p, v); R(45, v_eei)(l, p, v); }
static void WINAPI t_Lightiv(GLenum l, GLenum p, const GLint *v) { GT("Lightiv %x %x\n", l, p); ((void (WINAPI *)(GLenum, GLenum, const GLint *))ghlreal[46])(l, p, v); }
static void WINAPI t_Materialf(GLenum f, GLenum p, GLfloat v) { GT("Materialf %x %x %g\n", f, p, v); R(47, v_eef)(f, p, v); }
static void WINAPI t_Materialfv(GLenum f, GLenum p, const GLfloat *v) { GT("Materialfv %x %x %g %g %g %g\n", f, p, v[0], v[1], v[2], v[3]); R(48, v_eepf)(f, p, v); }
static void WINAPI t_Materiali(GLenum f, GLenum p, GLint v) { GT("Materiali %x %x %d\n", f, p, v); R(49, v_eei)(f, p, v); }
static void WINAPI t_Materialiv(GLenum f, GLenum p, const GLint *v) { GT("Materialiv %x %x\n", f, p); ((void (WINAPI *)(GLenum, GLenum, const GLint *))ghlreal[50])(f, p, v); }
static void WINAPI t_Apply(void) { GT("ApplyState\n"); R(51, v_v)(); }

static void *ghlwrappers[GHL_GL_ENTRIES] = {
	t_AlphaFunc, t_Begin, t_BindTexture, t_BlendFunc, t_Color3f, t_Color4f, t_CullFace, t_DepthFunc,
	t_DepthMask, t_DeleteTextures, t_Disable, t_Enable, t_End, t_FrontFace, t_GetError, t_GenTextures,
	t_GetFloatv, t_GetIntegerv, t_LoadIdentity, t_LoadMatrixf, t_MatrixMode, t_MultMatrixf, t_Normal3f,
	t_PixelStorei, t_PolygonOffset, t_PopMatrix, t_PushMatrix, t_Rotatef, t_ShadeModel, t_TexCoord2f,
	t_TexEnvf, t_TexImage2D, t_TexParameteri, t_TexGeni, t_Vertex3f, t_Color3fv, t_Normal3fv, t_TexCoord2fv,
	t_Vertex3fv, t_LightModelf, t_LightModelfv, t_LightModeli, t_LightModeliv, t_Lightf, t_Lightfv,
	t_Lighti, t_Lightiv, t_Materialf, t_Materialfv, t_Materiali, t_Materialiv, t_Apply
};

/* swaps the table entries in place (the Ghoul system keeps the table pointer) */
void
GL3_Ghoul_Trace(qboolean on, const char *path)
{
	int i;

	if (!ghlgl_ready)
	{
		return;
	}

	if (on && !ghltrace)
	{
		ghltrace = fopen(path, "w");

		if (!ghltrace)
		{
			return;
		}

		for (i = 0; i < GHL_GL_ENTRIES; i++)
		{
			ghlreal[i] = ghlgl[i];
			ghlgl[i] = ghlwrappers[i];
		}
	}
	else if (!on && ghltrace)
	{
		for (i = 0; i < GHL_GL_ENTRIES; i++)
		{
			ghlgl[i] = ghlreal[i];
		}

		fclose(ghltrace);
		ghltrace = NULL;
	}
}

void
GL3_Ghoul_TraceNote(const char *text)
{
	GT("--- %s\n", text);
}

/* builds the table; returns NULL if a function is missing */
void *
GL3_Ghoul_GLTable(void)
{
	HMODULE gl = GetModuleHandleA("opengl32.dll");
	int i;

	if (ghlgl_ready)
	{
		return ghlgl;
	}

	if (!gl)
	{
		return NULL;
	}

	for (i = 0; i < GHL_GL_ENTRIES - 1; i++)
	{
		ghlgl[i] = (void *)GetProcAddress(gl, ghlnames[i]);

		if (!ghlgl[i])
		{
			Com_Printf("GL3_Ghoul_GLTable: %s missing\n", ghlnames[i]);
			return NULL;
		}
	}

	real_BindTexture = (pfnBindTexture)ghlgl[2];
	real_DeleteTextures = (pfnDeleteTextures)ghlgl[9];
	real_TexParameteri = (pfnTexParameteri)ghlgl[32];
	real_Materialfv = (pfnMaterialfv)ghlgl[48];
	ghlgl[2] = (void *)GhlBindTexture;
	ghlgl[48] = (void *)GhlMaterialfv;
	ghlgl[9] = (void *)GhlDeleteTextures;
	ghlgl[32] = (void *)GhlTexParameteri;
	ghlgl[51] = (void *)GhlApplyState;
	ghlgl_ready = true;
	return ghlgl;
}

void
GL3_Ghoul_ResetGLTable(void)
{
	ghlgl_ready = false;
}

typedef void (WINAPI *pfnTexEnvi)(GLenum, GLenum, GLint);
typedef void (WINAPI *pfnColor4ub)(GLubyte, GLubyte, GLubyte, GLubyte);
typedef void (WINAPI *pfnAlphaFunc)(GLenum, GLfloat);

#define FF_TEXTURE_2D 0x0DE1
#define FF_ALPHA_TEST 0x0BC0
#define FF_LIGHTING 0x0B50
#define FF_FOG 0x0B60
#define FF_TEXTURE_ENV 0x2300
#define FF_TEXTURE_ENV_MODE 0x2200
#define FF_MODULATE 0x2100
#define FF_REPLACE 0x1E01

/*
 * Leaves the renderer's shader state so that GL 1.1 drawing works: no
 * program, no vertex array or buffers, texture unit 0 with 2D texturing, and
 * the fixed-function state the original's 2D calls would have left (SPEC-412
 * §1): blending, texture environment and current colour; alpha test
 * "greater than 0.05" on, flat shading.
 */
void
GL3_Ghoul_EnterFixedFunction(qboolean blend, qboolean modulate, const byte color[4])
{
	HMODULE gl = GetModuleHandleA("opengl32.dll");
	pfnEnable enable = (pfnEnable)GetProcAddress(gl, "glEnable");
	pfnEnable disable = (pfnEnable)GetProcAddress(gl, "glDisable");
	pfnTexEnvi texEnvi = (pfnTexEnvi)GetProcAddress(gl, "glTexEnvi");
	pfnColor4ub color4ub = (pfnColor4ub)GetProcAddress(gl, "glColor4ub");
	pfnAlphaFunc alphaFunc = (pfnAlphaFunc)GetProcAddress(gl, "glAlphaFunc");
	pfnShadeModel shadeModel = (pfnShadeModel)GetProcAddress(gl, "glShadeModel");

	GL3_SoF2D_Flush();
	GL3_FX_Flush();
	GL3_DrawCurrent2Dbatch();
	GL3_Draw3DBatchesNow();

	glUseProgram(0);
	gl3state.currentShaderProgram = 0;
	glBindVertexArray(0);
	gl3state.currentVAO = 0;
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	gl3state.currentVBO = 0;
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	gl3state.currentEBO = 0;
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	gl3state.currentUBO = 0;
	GL3_SelectTMU(GL_TEXTURE0);

	if (!enable || !disable || !texEnvi || !color4ub || !alphaFunc || !shadeModel)
	{
		return;
	}

	enable(FF_TEXTURE_2D);
	enable(FF_ALPHA_TEST);
	alphaFunc(GL_GREATER, 0.05f);
	disable(FF_FOG);
	shadeModel(GL_FLAT_SHADE);
	texEnvi(FF_TEXTURE_ENV, FF_TEXTURE_ENV_MODE, modulate ? FF_MODULATE : FF_REPLACE);
	color4ub(color[0], color[1], color[2], color[3]);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	/* GFX-modern: fixed-function colour would go to every draw buffer; only the scene target here */
	if (gl3post.active)
	{
		static const GLenum scene[1] = {GL_COLOR_ATTACHMENT0};

		glDrawBuffers(1, scene);
	}

	if (blend)
	{
		glEnable(GL_BLEND);
	}
	else
	{
		glDisable(GL_BLEND);
	}
}

/* back to the renderer's shader state after fixed-function drawing */
void
GL3_Ghoul_LeaveFixedFunction(void)
{
	gl3state.currentShaderProgram = 0;
	gl3state.currentVAO = 0;
	gl3state.currentVBO = 0;
	gl3state.currentEBO = 0;
	gl3state.currentUBO = 0;
	gl3state.currenttexture = 0;
	gl3state.currentlightmap = -1;
	glActiveTexture(GL_TEXTURE0);
	gl3state.currenttmu = GL_TEXTURE0;
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_POLYGON_OFFSET_FILL);

	/* fixed-function per-fragment operations still apply to shader output in a
	   compatibility context: the renderer's shaders do their own alpha test.
	   (Entry to fixed-function drawing turns it back on. Lighting and other
	   fixed-function-only state are left alone: they do not affect shaders,
	   and the Ghoul system may rely on what it set before.) */
	glDisable(0x0BC0); /* GL_ALPHA_TEST */
	glDepthRange(gl3depthmin, gl3depthmax);

	if (gl3post.active)
	{
		static const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};

		glDrawBuffers(2, both);
	}

	/* the uniform blocks are bound to binding points, not to GL_UNIFORM_BUFFER; rebinding them is cheap */
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, gl3state.uniCommonUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, gl3state.uni2DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, 2, gl3state.uni3DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, 3, gl3state.uniLightsUBO);
}

/* fixed-function projection and modelview for the menu model view (slot 0xb8 with flag) */
void
GL3_Ghoul_SetProjection(const float *projection)
{
	HMODULE gl = GetModuleHandleA("opengl32.dll");
	pfnMatrixMode matrixMode = (pfnMatrixMode)GetProcAddress(gl, "glMatrixMode");
	pfnLoadMatrixf loadMatrix = (pfnLoadMatrixf)GetProcAddress(gl, "glLoadMatrixf");
	pfnLoadIdentity loadIdentity = (pfnLoadIdentity)GetProcAddress(gl, "glLoadIdentity");
	pfnShadeModel shadeModel = (pfnShadeModel)GetProcAddress(gl, "glShadeModel");

	if (!matrixMode || !loadMatrix || !loadIdentity)
	{
		return;
	}

	matrixMode(GL_PROJECTION_MODE);
	loadMatrix(projection);
	matrixMode(GL_MODELVIEW_MODE);
	loadIdentity();

	if (shadeModel)
	{
		shadeModel(GL_FLAT_SHADE);
	}
}

#endif /* OM_SOFREF */
