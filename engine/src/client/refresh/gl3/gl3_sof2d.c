/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: Soldier of Fortune 2D drawing. Every 2D export slot
 * of SPEC-002 with the render state and geometry of SPEC-412: pictures,
 * console characters and strings with colour codes, tile clear, fills, bevel
 * boxes, lines, fades, polygons, 2D clipping, proportional fonts (.fnt pages,
 * SPEC-132), world labels, save-game thumbnails, screenshots and raw shots.
 *
 * The original draws through a deferred fixed-function state cache, and some
 * calls leave blending or the texture mode changed for the next call
 * (SPEC-412 §1). That state is kept here (v2d) and applied per vertex
 * (texture mode, alpha test) or per batch (blending, clipping, depth test).
 *
 * Implements: SPEC-002, SPEC-412, SPEC-132, SPEC-AMEND-002, SPEC-802 (Picture flips of slot 0x54)
 */

#include "header/local.h"
#include "header/gl3_ghoul.h" /* I4b: Ghoul menu-model state (native or emulated GL 1.1) */

/* SPEC-002 text colour table (R, G, B, A) */
const byte sof_textcolors[32][4] = {
	{0x00, 0x00, 0x00, 0x00}, {0xFF, 0xFF, 0xFF, 0xFF}, {0xFF, 0x00, 0x00, 0xFF}, {0x00, 0xFF, 0x00, 0xFF},
	{0xFF, 0xFF, 0x00, 0xFF}, {0x00, 0x00, 0xFF, 0xFF}, {0xFF, 0x00, 0xFF, 0xFF}, {0x00, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0x00, 0xFF}, {0x7F, 0x7F, 0x7F, 0x7F}, {0x00, 0x00, 0x00, 0x00}, {0x7F, 0x00, 0x00, 0x7F},
	{0x00, 0x7F, 0x00, 0x7F}, {0x00, 0x00, 0x00, 0x00}, {0x7F, 0x7F, 0x00, 0x7F}, {0x00, 0x00, 0x7F, 0x7F},
	{0x56, 0x4D, 0x28, 0x7F}, {0x4C, 0x5E, 0x36, 0x7F}, {0x37, 0x6F, 0x65, 0x7F}, {0x00, 0x55, 0x72, 0xFF},
	{0x54, 0x64, 0x7E, 0xFF}, {0x1E, 0x2A, 0x63, 0xBF}, {0x66, 0x09, 0x7B, 0xFF}, {0x70, 0x5E, 0x61, 0xFF},
	{0x98, 0x00, 0x53, 0xFF}, {0x96, 0x00, 0x18, 0xFF}, {0x70, 0x2D, 0x07, 0xFF}, {0x54, 0x49, 0x2A, 0xFF},
	{0x61, 0xA9, 0x97, 0xFF}, {0xCB, 0x8F, 0x39, 0xFF}, {0xCF, 0x83, 0x16, 0xFF}, {0xFF, 0x80, 0x20, 0xFF}
};

/* vertex of the 2D batch: position, texture coordinates, colour, mode (texture mode, alpha test) */
typedef struct
{
	float x, y, s, t;
	float r, g, b, a;
	float texmode, alphatest;
} sof2dvtx_t;

enum
{
	TEXMODE_MODULATE = 0,
	TEXMODE_REPLACE = 1,
	TEXMODE_NONE = 2
};

#define MAX_2D_VERTS 8192

static GLuint vao2d, vbo2d;
static sof2dvtx_t verts[MAX_2D_VERTS];
static int numverts;
static GLuint batchtex;      /* texture of the pending batch (0 = untextured only) */
static int batchblend = -1;  /* blending of the pending batch */
static qboolean batchdepth;  /* depth test of the pending batch */
static qboolean batchpic;    /* MENU2: the batch texture is a pic or font page */
static qboolean emitpic;     /* MENU2: set by Quad for the next Emit */
static float sof2d_scale = 1.0f; /* V1/MENU2: see GL3_SoF2D_SetScale */

/* the original's deferred state (SPEC-412 §1) */
static struct
{
	qboolean blend;
	int texmode;           /* TEXMODE_MODULATE / TEXMODE_REPLACE */
	qboolean texturing;    /* cleared by slot 0x88 */
	byte color[4];         /* current vertex colour */
	qboolean depthtest;
} v2d;

/* 2D clipping (SPEC-412 §4) */
static int clipx, clipy, clipw, cliph;
static qboolean clipenabled;

/* slot 0xb8 / 0xb4 (SPEC-412 §2) */
static qboolean menu3d_saved;
static qboolean menu3d_clipwason;
static hmm_mat4 menu3d_projection;

/* names of missing pictures, reported once per registration cycle */
#define MAX_MISSING 128
static char missingpics[MAX_MISSING][MAX_QPATH];
static int nummissing;
static int missingseq = -1;

/* ------------------------------------------------------------------ */

void
GL3_SoF2D_Init(void)
{
	glGenVertexArrays(1, &vao2d);
	GL3_BindVAO(vao2d);
	glGenBuffers(1, &vbo2d);
	GL3_BindVBO(vbo2d);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 2, GL_FLOAT, GL_FALSE, sizeof(sof2dvtx_t), 0);
	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(sof2dvtx_t), offsetof(sof2dvtx_t, s));
	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(sof2dvtx_t), offsetof(sof2dvtx_t, r));
	glEnableVertexAttribArray(GL3_ATTRIB_LMTEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_LMTEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(sof2dvtx_t), offsetof(sof2dvtx_t, texmode));

	GL3_BindVAO(0);

	numverts = 0;
	batchtex = 0;
	batchblend = -1;
	clipenabled = false;
	clipx = clipy = 0;
	clipw = vid.width;
	cliph = vid.height;
	GL3_SoF2D_ResetState();
}

void
GL3_SoF2D_Shutdown(void)
{
	GL3_SoF2D_ShutdownFonts();

	if (vbo2d)
	{
		glDeleteBuffers(1, &vbo2d);
	}

	if (vao2d)
	{
		glDeleteVertexArrays(1, &vao2d);
	}

	vbo2d = vao2d = 0;
	numverts = 0;
}

/* the state after entering 2D (SPEC-412 §2) */
void
GL3_SoF2D_ResetState(void)
{
	v2d.blend = false;
	v2d.texmode = TEXMODE_REPLACE;
	v2d.texturing = true;
	v2d.color[0] = v2d.color[1] = v2d.color[2] = v2d.color[3] = 255;
	v2d.depthtest = false;
}

/* debugging: r_om_glstate 1 prints the GL state at the next 2D batch */
static void
DumpGLState(void)
{
	static cvar_t *r_om_glstate;
	GLint i[8];
	GLfloat f[4];
	GLboolean cm[4];

	if (!r_om_glstate)
	{
		r_om_glstate = ri.Cvar_Get("r_om_glstate", "0", 0);
	}

	if (!r_om_glstate->value)
	{
		return;
	}

	ri.Cvar_Set("r_om_glstate", "0");
	glGetIntegerv(GL_CURRENT_PROGRAM, &i[0]);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &i[1]);
	glGetIntegerv(GL_VIEWPORT, &i[2]);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &i[6]);
	glGetFloatv(GL_DEPTH_RANGE, f);
	glGetBooleanv(GL_COLOR_WRITEMASK, cm);
	Com_Printf("GLSTATE prog %d (2D %d) vao %d (2D %d) viewport %d %d %d %d fbo %d depthrange %g %g colormask %d%d%d%d\n",
		i[0], gl3state.siSoF2D.shaderProgram, i[1], vao2d, i[2], i[3], i[4], i[5], i[6], f[0], f[1],
		cm[0], cm[1], cm[2], cm[3]);
	Com_Printf("GLSTATE blend %d depth %d cull %d scissor %d stencil %d alphatest %d polyfill-offset %d rasterdiscard %d\n",
		glIsEnabled(GL_BLEND), glIsEnabled(GL_DEPTH_TEST), glIsEnabled(GL_CULL_FACE), glIsEnabled(GL_SCISSOR_TEST),
		glIsEnabled(GL_STENCIL_TEST), glIsEnabled(0x0BC0), glIsEnabled(GL_POLYGON_OFFSET_FILL),
		glIsEnabled(GL_RASTERIZER_DISCARD));
#ifdef YQ2_GL3_GLES3
	i[0] = 0; /* no polygon modes in OpenGL ES */
#else
	glGetIntegerv(GL_POLYGON_MODE, i);
#endif
	glGetIntegerv(GL_ACTIVE_TEXTURE, &i[2]);
	glGetIntegerv(GL_BLEND_SRC_RGB, &i[3]);
	glGetIntegerv(GL_BLEND_DST_RGB, &i[4]);
	Com_Printf("GLSTATE polygonmode %x active texture %x blend %x %x err %x\n", i[0], i[2], i[3], i[4], glGetError());
}

void
GL3_SoF2D_Flush(void)
{
	if (numverts == 0)
	{
		return;
	}

	DumpGLState();

	GL3_UseProgram(gl3state.siSoF2D.shaderProgram);
	GL3_BindVAO(vao2d);
	GL3_BindVBO(vbo2d);
	glBufferData(GL_ARRAY_BUFFER, numverts * sizeof(sof2dvtx_t), verts, GL_STREAM_DRAW);

	if (batchtex)
	{
		GL3_Bind(batchtex);
	}

	/* OpenMerc MENU2: pics and fonts are nearest-sampled (SPEC-413 section 8);
	   at a fractional 2D scale that gives uneven glyph strokes, so such a
	   batch is drawn with linear filtering (whole factors stay nearest) */
	if (batchtex && batchpic && (sof2d_scale != (float)(int)sof2d_scale))
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else
	{
		batchpic = false;
	}

	if (batchblend)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
	{
		glDisable(GL_BLEND);
	}

	if (batchdepth)
	{
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
	}
	else
	{
		glDisable(GL_DEPTH_TEST);
	}

	glDrawArrays(GL_TRIANGLES, 0, numverts);
	++gl3_num2Ddraws;
	++gl3_numBufferVtxData;

	if (batchpic)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		batchpic = false;
	}

	numverts = 0;
	batchtex = 0;
	batchblend = -1;

	if (batchdepth)
	{
		glDisable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
	}
}

/* adds triangles (count a multiple of 3); texnum 0 = untextured */
static void
Emit(GLuint texnum, const sof2dvtx_t *v, int count)
{
	int blend = v2d.blend ? 1 : 0;

	GL3_DrawCurrent2Dbatch(); /* Quake II 2D calls keep their own batch */
	GL3_FX_Flush();

	if ((numverts + count > MAX_2D_VERTS) ||
		(texnum && batchtex && (texnum != batchtex)) ||
		((batchblend >= 0) && (batchblend != blend)) ||
		((numverts > 0) && (batchdepth != v2d.depthtest)))
	{
		GL3_SoF2D_Flush();
	}

	if (texnum)
	{
		batchtex = texnum;
		batchpic = emitpic;
	}

	emitpic = false;
	batchblend = blend;
	batchdepth = v2d.depthtest;
	memcpy(&verts[numverts], v, count * sizeof(*v));
	numverts += count;
}

/*
 * One quad with corners in the order top-left, top-right, bottom-right,
 * bottom-left (SPEC-412 §3), drawn with the current texture mode, colour
 * and blending. image NULL or texturing disabled: untextured.
 */
static void
Quad(gl3image_t *image, const float xy[4][2], const float st[4][2], const byte rgba[4], int texmode)
{
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	sof2dvtx_t v[6];
	GLuint texnum = 0;
	int i;

	if (!image || !v2d.texturing)
	{
		texmode = TEXMODE_NONE;
	}
	else
	{
		texnum = image->texnum;
		emitpic = (image->softype == SOFIMG_PIC) || (image->softype == SOFIMG_FONT);
	}

	for (i = 0; i < 6; i++)
	{
		int k = order[i];

		v[i].x = xy[k][0];
		v[i].y = xy[k][1];
		v[i].s = st ? st[k][0] : 0;
		v[i].t = st ? st[k][1] : 0;
		v[i].r = rgba[0] * (1.0f / 255.0f);
		v[i].g = rgba[1] * (1.0f / 255.0f);
		v[i].b = rgba[2] * (1.0f / 255.0f);
		v[i].a = rgba[3] * (1.0f / 255.0f);
		v[i].texmode = (float)texmode;
		v[i].alphatest = 1.0f; /* alpha test (> 0.05) is on in 2D */
	}

	Emit(texnum, v, 6);
}

/* one textured quad drawn now with the blend function the caller set (night-vision multiply) */
void
GL3_SoF2D_DrawMultiply(GLuint tex, const float xy[4][2], const float st[4][2], const byte rgba[4])
{
	static const int order[6] = {0, 1, 2, 0, 2, 3};
	sof2dvtx_t v[6];
	int i;

	GL3_SoF2D_Flush();

	for (i = 0; i < 6; i++)
	{
		int k = order[i];

		v[i].x = xy[k][0];
		v[i].y = xy[k][1];
		v[i].s = st[k][0];
		v[i].t = st[k][1];
		v[i].r = rgba[0] / 255.0f;
		v[i].g = rgba[1] / 255.0f;
		v[i].b = rgba[2] / 255.0f;
		v[i].a = rgba[3] / 255.0f;
		v[i].texmode = TEXMODE_MODULATE;
		v[i].alphatest = 0; /* alpha test off for the overlay */
	}

	GL3_UseProgram(gl3state.siSoF2D.shaderProgram);
	GL3_BindVAO(vao2d);
	GL3_BindVBO(vbo2d);
	glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
	GL3_Bind(tex);
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	++gl3_num2Ddraws;
}

/* public helper for other parts of the renderer (overlays) */
void
GL3_SoF2D_TexturedQuad(GLuint texnum, const float xy[4][2], const float st[4][2], const byte rgba[4])
{
	gl3image_t fake;

	memset(&fake, 0, sizeof(fake));
	fake.texnum = texnum;
	Quad(texnum ? &fake : NULL, xy, st, rgba, TEXMODE_MODULATE);
}

/* OpenMerc V1: uniform scale of every 2D primitive (HUD scaling, OM_Set2DScale);
   1.0 outside the HUD draws. OpenMerc MENU2: also the menus' UI scale, which
   also applies to the clip rectangles and the menu-model projection. */
void
GL3_SoF2D_SetScale(float scale)
{
	scale = (scale > 0.0f) ? scale : 1.0f;

	if (scale != sof2d_scale)
	{
		GL3_SoF2D_Flush();
	}

	sof2d_scale = scale;
}

static void
RectXY(float xy[4][2], float x, float y, float w, float h)
{
	x *= sof2d_scale;
	y *= sof2d_scale;
	w *= sof2d_scale;
	h *= sof2d_scale;
	xy[0][0] = x;     xy[0][1] = y;
	xy[1][0] = x + w; xy[1][1] = y;
	xy[2][0] = x + w; xy[2][1] = y + h;
	xy[3][0] = x;     xy[3][1] = y + h;
}

static void
RectST(float st[4][2], float s0, float t0, float s1, float t1)
{
	st[0][0] = s0; st[0][1] = t0;
	st[1][0] = s1; st[1][1] = t0;
	st[2][0] = s1; st[2][1] = t1;
	st[3][0] = s0; st[3][1] = t1;
}

static void
UnpackRGBA(unsigned int rgba, byte out[4])
{
	/* RGBA by value: bytes in memory order R, G, B, A (little endian) */
	out[0] = rgba & 0xFF;
	out[1] = (rgba >> 8) & 0xFF;
	out[2] = (rgba >> 16) & 0xFF;
	out[3] = (rgba >> 24) & 0xFF;
}

static void
CopyRGBA(const unsigned char *in, byte out[4])
{
	if (in)
	{
		out[0] = in[0];
		out[1] = in[1];
		out[2] = in[2];
		out[3] = in[3];
	}
	else
	{
		out[0] = out[1] = out[2] = out[3] = 255;
	}
}

static qboolean
IsMissing(const char *name)
{
	int i;

	if (missingseq != registration_sequence)
	{
		missingseq = registration_sequence;
		nummissing = 0;
	}

	for (i = 0; i < nummissing; i++)
	{
		if (!strcmp(missingpics[i], name))
		{
			return true;
		}
	}

	return false;
}

/* a pic by name; prints "Can't find pic" once and returns NULL if missing */
static gl3image_t *
FindPic(const char *name)
{
	gl3image_t *image;

	if (!name || !name[0])
	{
		return NULL;
	}

	image = GL3_SoF_FindImage(name, SOFIMG_PIC, false);

	if (image)
	{
		return image;
	}

	if (IsMissing(name))
	{
		return NULL;
	}

	image = GL3_SoF_FindImage(name, SOFIMG_PIC, true);

	if (!image)
	{
		Com_Printf("Can't find pic: %s\n", name);

		if (nummissing < MAX_MISSING)
		{
			Q_strlcpy(missingpics[nummissing++], name, MAX_QPATH);
		}
	}

	return image;
}

/* ------------------------------------------------------------------
 * 2D mode (SPEC-412 §2)
 * ------------------------------------------------------------------ */

/* entering 2D after the 3D view: end of RenderFrame and slot 0xb4 */
void
GL3_SoF2D_Enter2D(qboolean restore)
{
	GL3_SoF2D_Flush();
	GL3_FX_Flush();
	GL3_DrawCurrent2Dbatch();

	GL3_Ghoul_MenuLeave(); /* I4b: native fixed function (SoF module) or the GL 1.1 emulation */

	if (restore && menu3d_saved)
	{
		menu3d_saved = false;

		if (menu3d_clipwason)
		{
			SoF_ReapplyClipRect();
		}
	}

	glViewport(0, 0, vid.width, vid.height);
	gl3state.uni2DData.transMat4 = HMM_Orthographic(0, vid.width, vid.height, 0, -9999, 9999);
	GL3_UpdateUBO2D();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	/* depth writes are left as they are (SPEC-AMEND-005 I4a.2: Begin frame leaves them on, the end of
	   RenderFrame does not change them; only slot 0xb4 turns them off) */
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GL3_SoF2D_ResetState();
}

/* slot 0xb4 */
void
SoF_Set2D(int restore)
{
	GL3_SoF2D_Enter2D((restore & 0xFF) != 0);
	glDepthMask(GL_FALSE);
}

/* slot 0xb8: prepares 3D drawing inside the 2D screen (menu model views) */
void
SoF_Set3DInMenu(int flag)
{
	GL3_SoF2D_Flush();

	v2d.depthtest = true;
	v2d.texturing = true;
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);

#ifdef OM_SOFREF
	GL3_Ghoul_TraceNote(flag ? "Set3DInMenu 1" : "Set3DInMenu 0");
#endif

	/* the engine draws the menu model through the Ghoul system with GL 1.1 calls (I4b: native or emulated) */
	GL3_Ghoul_MenuEnter(v2d.blend, v2d.texmode == TEXMODE_MODULATE, v2d.color);
	glDepthFunc(GL_LEQUAL);
	glCullFace(GL_BACK);

	if (flag & 0xFF)
	{
		float zn = r_nearclipdist ? r_nearclipdist->value : 4.0f;
		float zf = r_farclipdist ? r_farclipdist->value : 65535.0f;

		menu3d_saved = true;
		menu3d_clipwason = clipenabled;
		menu3d_projection = HMM_Perspective(33.75084377109428f, 1.3333f, zn > 0.1f ? zn : 0.1f, zf);

		if (clipenabled)
		{
			glDisable(GL_SCISSOR_TEST);
			clipenabled = false;
		}

		GL3_Ghoul_MenuProjection(&menu3d_projection.Elements[0][0]); /* I4b */
	}
	else
	{
		/*
		 * The projection stays the 2D one: models are drawn in screen
		 * coordinates. Its depth runs the other way round than glOrtho with
		 * near -9999 / far 9999 would give: more negative z is nearer. Only
		 * that orientation reproduces the original's main menu (frame,
		 * buttons and globe pixel-identical, black-box comparison 2026-10-22).
		 */
		/* MENU2: in the menus' virtual window (2D scale) */
		hmm_mat4 ortho = HMM_Orthographic(0, vid.width / sof2d_scale, vid.height / sof2d_scale, 0,
				9999, -9999);

		GL3_Ghoul_MenuProjection(&ortho.Elements[0][0]); /* I4b */
	}
}

/* ------------------------------------------------------------------
 * 2D primitives (SPEC-412 §3)
 * ------------------------------------------------------------------ */

/* slot 0x44 */
void
SoF_DrawGetPicSize(int *w, int *h, char *name)
{
	gl3image_t *image = FindPic(name);

	if (!image)
	{
		*w = *h = -1;
		return;
	}

	*w = image->width;
	*h = image->height;
}

/* slot 0x48: pic at its level-0 size, leaves blending on */
void
SoF_DrawPic(int x, int y, char *name, unsigned int rgba)
{
	gl3image_t *image = FindPic(name);
	float xy[4][2], st[4][2];
	byte c[4];

	if (!image)
	{
		return;
	}

	UnpackRGBA(rgba, c);
	v2d.texmode = TEXMODE_MODULATE;
	v2d.blend = true;
	RectXY(xy, x, y, image->width, image->height);
	RectST(st, 0, 0, 1, 1);
	Quad(image, xy, st, c, v2d.texmode);
	memcpy(v2d.color, c, 4);
}

/* slot 0x4c: size trunc(w sx) x trunc(h sy); afterwards replace, blending off */
void
SoF_DrawPicScaled(int x, int y, float sx, float sy, const unsigned char *rgba, char *name)
{
	gl3image_t *image = FindPic(name);
	float xy[4][2], st[4][2];
	byte c[4];

	if (!image)
	{
		return;
	}

	CopyRGBA(rgba, c);
	v2d.blend = true;
	RectXY(xy, x, y, (float)(int)(image->width * sx), (float)(int)(image->height * sy));
	RectST(st, 0, 0, 1, 1);
	Quad(image, xy, st, c, TEXMODE_MODULATE);
	memcpy(v2d.color, c, 4);
	v2d.texmode = TEXMODE_REPLACE;
	v2d.blend = false;
}

/* slot 0x50: sub-rectangle of the pic, in pic pixels */
void
SoF_DrawPicPart(int x, int y, int left, int top, int right, int bottom, const unsigned char *rgba, char *name)
{
	gl3image_t *image = FindPic(name);
	float xy[4][2], st[4][2];
	byte c[4];
	int w, h;

	if (!image || (image->width <= 0) || (image->height <= 0))
	{
		return;
	}

	CopyRGBA(rgba, c);
	w = abs(right - left);
	h = abs(bottom - top);
	v2d.blend = true;
	RectXY(xy, x, y, w, h);
	RectST(st, (float)left / image->width, (float)top / image->height,
		(float)right / image->width, (float)bottom / image->height);
	Quad(image, xy, st, c, TEXMODE_MODULATE);
	memcpy(v2d.color, c, 4);
	v2d.texmode = TEXMODE_REPLACE;
	v2d.blend = false;
}

/* slot 0x54: stretched with quarter-turn rotation (0-3, SPEC-002) or flip (4-7, SPEC-802 Picture flips);
   afterwards blending off (modulate stays) */
void
SoF_DrawStretchPic(int x, int y, int w, int h, const unsigned char *rgba, char *name, int rotation)
{
	static const float rot[8][4][2] = {
		{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
		{{1, 0}, {1, 1}, {0, 1}, {0, 0}},
		{{1, 1}, {0, 1}, {0, 0}, {1, 0}},
		{{0, 1}, {0, 0}, {1, 0}, {1, 1}},
		/* I4c: SPEC-802 flips (4 unchanged, 5 left-right, 6 top-bottom, 7 both) */
		{{0, 0}, {1, 0}, {1, 1}, {0, 1}},
		{{1, 0}, {0, 0}, {0, 1}, {1, 1}},
		{{0, 1}, {1, 1}, {1, 0}, {0, 0}},
		{{1, 1}, {0, 1}, {0, 0}, {1, 0}}
	};
	gl3image_t *image = FindPic(name);
	float xy[4][2];
	byte c[4];

	if (!image)
	{
		return;
	}

	CopyRGBA(rgba, c);
	v2d.texmode = TEXMODE_MODULATE;
	v2d.blend = true;
	RectXY(xy, x, y, w, h);
	Quad(image, xy, rot[rotation & 7], c, TEXMODE_MODULATE); /* values >= 8 are not specified */
	memcpy(v2d.color, c, 4);
	v2d.blend = false;
}

static gl3image_t *
Conchars(void)
{
	extern gl3image_t *draw_chars;

	return draw_chars;
}

/* one 8x8 glyph at (x, y); state: modulate, blending unchanged */
static void
Glyph(float x, float y, int c, const byte rgba[4])
{
	float xy[4][2], st[4][2];
	float s, t;

	c &= 255;

	if ((c == 32) || (y <= -8))
	{
		return;
	}

	s = (c & 15) * 0.0625f;
	t = (c >> 4) * 0.0625f;
	RectXY(xy, x, y, 8, 8);
	RectST(st, s, t, s + 0.0625f, t + 0.0625f);
	Quad(Conchars(), xy, st, rgba, TEXMODE_MODULATE);
}

/* slot 0x58 */
void
SoF_DrawChar(int x, int y, int c, const unsigned char *rgba)
{
	byte col[4];

	CopyRGBA(rgba, col);
	v2d.texmode = TEXMODE_MODULATE;
	Glyph(x, y, c, col);
	memcpy(v2d.color, col, 4);
}

/* slot 0x5c: up to maxlen glyphs in one colour; 0 draws nothing, negative = whole string */
void
SoF_DrawString(int x, int y, char *s, unsigned int rgba, int maxlen)
{
	byte col[4];

	if (!s)
	{
		return;
	}

	UnpackRGBA(rgba, col);
	v2d.texmode = TEXMODE_MODULATE;

	while (*s && (maxlen != 0))
	{
		Glyph(x, y, (byte)*s, col);
		x += 8;
		s++;

		if (maxlen > 0)
		{
			maxlen--;
		}
	}

	memcpy(v2d.color, col, 4);
}

/* slots 0x60 / 0x64: every byte 1..31 selects a colour; maxlen counts drawn glyphs */
static void
ConsoleString(int x, int y, const char *s, int maxlen, const byte start[4])
{
	byte col[4];
	int drawn = 0;

	if (!s)
	{
		return;
	}

	memcpy(col, start, 4);
	v2d.texmode = TEXMODE_MODULATE;

	while (*s && (drawn < maxlen))
	{
		byte c = (byte)*s++;

		if ((c >= 1) && (c <= 31))
		{
			memcpy(col, sof_textcolors[c], 4);
			continue;
		}

		Glyph(x, y, c, col);
		x += 8;
		drawn++;
	}

	memcpy(v2d.color, col, 4);
}

void
SoF_DrawConsoleString(int x, int y, char *s, int maxlen)
{
	ConsoleString(x, y, s, maxlen, sof_textcolors[1]);
}

void
SoF_DrawConsoleStringColor(int x, int y, char *s, int maxlen, const unsigned char *rgba)
{
	byte col[4];

	CopyRGBA(rgba, col);
	ConsoleString(x, y, s, maxlen, col);
}

/* slot 0x6c: glyph at a fractional position; the scale is ignored */
void
SoF_DrawCharFloat(float x, float y, float scale, const unsigned char *rgba, int c)
{
	byte col[4];

	(void)scale;
	CopyRGBA(rgba, col);
	v2d.blend = true;
	Glyph(x, y, c, col);
	memcpy(v2d.color, col, 4);
	v2d.blend = false;
	v2d.texmode = TEXMODE_REPLACE;
}

/* slot 0x70: pic repeated every 64 pixels, screen-aligned; colour, texture mode and blending inherited */
void
SoF_DrawTileClear(int x, int y, int w, int h, char *name)
{
	gl3image_t *image = FindPic(name);
	float xy[4][2], st[4][2];

	if (!image)
	{
		return;
	}

	RectXY(xy, x, y, w, h);
	RectST(st, x / 64.0f, y / 64.0f, (x + w) / 64.0f, (y + h) / 64.0f);
	Quad(image, xy, st, v2d.color, v2d.texmode);
}

static void
FillRect(float x, float y, float w, float h, const byte c[4])
{
	float xy[4][2];

	RectXY(xy, x, y, w, h);
	Quad(NULL, xy, NULL, c, TEXMODE_NONE);
}

/* slot 0x74: blended solid rectangle; afterwards blending off */
void
SoF_DrawFill(int x, int y, int w, int h, const unsigned char *rgba)
{
	byte c[4];

	CopyRGBA(rgba, c);
	v2d.blend = true;
	FillRect(x, y, w, h, c);
	memcpy(v2d.color, c, 4);
	v2d.blend = false;
}

/* slot 0x7c: thick line with square caps; afterwards blending off */
static void
Line(float x1, float y1, float x2, float y2, float width, const byte c[4])
{
	float dx, dy, len, hw, nx, ny, xy[4][2];

	x1 *= sof2d_scale;
	y1 *= sof2d_scale;
	x2 *= sof2d_scale;
	y2 *= sof2d_scale;
	width *= sof2d_scale;
	dx = x2 - x1;
	dy = y2 - y1;
	len = sqrtf(dx * dx + dy * dy);
	hw = width * 0.5f;

	if (len <= 0)
	{
		dx = 1;
		dy = 0;
	}
	else
	{
		dx /= len;
		dy /= len;
	}

	nx = -dy;
	ny = dx;

	xy[0][0] = x1 - hw * dx + hw * nx; xy[0][1] = y1 - hw * dy + hw * ny;
	xy[1][0] = x2 + hw * dx + hw * nx; xy[1][1] = y2 + hw * dy + hw * ny;
	xy[2][0] = x2 + hw * dx - hw * nx; xy[2][1] = y2 + hw * dy - hw * ny;
	xy[3][0] = x1 - hw * dx - hw * nx; xy[3][1] = y1 - hw * dy - hw * ny;

	v2d.blend = true;
	Quad(NULL, xy, NULL, c, TEXMODE_NONE);
	v2d.blend = false;
}

void
SoF_DrawLine(float x1, float y1, float x2, float y2, float width, const unsigned char *rgba)
{
	byte c[4];

	CopyRGBA(rgba, c);
	Line(x1, y1, x2, y2, width, c);
	memcpy(v2d.color, c, 4);
}

/* slot 0x78: fill, then right and bottom edges dark (x0.7), top and left light (x1.3) */
void
SoF_DrawBevelBox(int x, int y, int w, int h, const unsigned char *rgba)
{
	byte c[4], dark[4], light[4];
	int i;

	CopyRGBA(rgba, c);

	for (i = 0; i < 3; i++)
	{
		int d = (int)(c[i] * 0.7f);
		int l = (int)(c[i] * 1.3f);

		dark[i] = (byte)(d > 255 ? 255 : d);
		light[i] = (byte)(l > 255 ? 255 : l);
	}

	dark[3] = light[3] = c[3];

	v2d.blend = true;
	FillRect(x, y, w, h, c);
	Line(x + w, y + h, x + w, y, 1, dark);
	Line(x + w, y + h, x, y + h, 1, dark);
	Line(x, y, x + w, y, 1, light);
	Line(x, y, x, y + h, 1, light);
	memcpy(v2d.color, light, 4);
}

/* slot 0x84: full-screen blended rectangle; afterwards blending off */
void
SoF_DrawFadeScreen(const unsigned char *rgba)
{
	byte c[4];

	CopyRGBA(rgba, c);
	v2d.blend = true;
	FillRect(0, 0, vid.width, vid.height, c);
	memcpy(v2d.color, c, 4);
	v2d.blend = false;
}

/* slot 0x88: filled polygon with depth test; leaves texturing disabled */
void
SoF_DrawPolygon(int unused1, int unused2, int n, sofvec3_t *pverts, const unsigned char *rgba)
{
	sof2dvtx_t v[3];
	byte c[4];
	int i, j;

	(void)unused1;
	(void)unused2;

	if ((n < 3) || !pverts)
	{
		return;
	}

	CopyRGBA(rgba, c);
	v2d.blend = true;
	v2d.depthtest = true;

	for (i = 1; i < n - 1; i++)
	{
		const int idx[3] = {0, i, i + 1};

		for (j = 0; j < 3; j++)
		{
			v[j].x = pverts[idx[j]][0];
			v[j].y = pverts[idx[j]][1];
			v[j].s = v[j].t = 0;
			v[j].r = c[0] / 255.0f;
			v[j].g = c[1] / 255.0f;
			v[j].b = c[2] / 255.0f;
			v[j].a = c[3] / 255.0f;
			v[j].texmode = TEXMODE_NONE;
			v[j].alphatest = 1.0f;
		}

		Emit(0, v, 3);
	}

	GL3_SoF2D_Flush();
	v2d.depthtest = false;
	v2d.blend = false;
	v2d.texturing = false;
	memcpy(v2d.color, c, 4);
}

/* ------------------------------------------------------------------
 * 2D clipping (SPEC-412 §4)
 * ------------------------------------------------------------------ */

static void
Scissor(int x0, int x1, int y, int h)
{
	GL3_SoF2D_Flush();

	/* MENU2: clip rectangles are in 2D units like every primitive */
	if (sof2d_scale != 1.0f)
	{
		int sx0 = (int)floorf(x0 * sof2d_scale + 0.5f), sx1 = (int)floorf(x1 * sof2d_scale + 0.5f);
		int sy0 = (int)floorf(y * sof2d_scale + 0.5f), sy1 = (int)floorf((y + h) * sof2d_scale + 0.5f);

		x0 = sx0;
		x1 = sx1;
		y = sy0;
		h = sy1 - sy0;
	}

	glScissor(x0, vid.height - y - h, x1 - x0, h);
}

void
SoF_SetClipRect(int x, int y, int w, int h)
{
	if (gl_scissor_broken && gl_scissor_broken->value)
	{
		return;
	}

	if ((w < 0) || (h < 0))
	{
		return;
	}

	clipx = x;
	clipy = y;
	clipw = w;
	cliph = h;
	Scissor(x, x + w, y, h);
	glEnable(GL_SCISSOR_TEST);
	clipenabled = true;
}

void
SoF_ResetClipRect(void)
{
	if (gl_scissor_broken && gl_scissor_broken->value)
	{
		return;
	}

	clipx = clipy = 0;
	clipw = vid.width;
	cliph = vid.height;
	Scissor(0, vid.width, 0, vid.height);
	glDisable(GL_SCISSOR_TEST);
	clipenabled = false;
}

void
GL3_SoF2D_ScissorReset(void)
{
	GL3_SoF2D_Flush();
	glDisable(GL_SCISSOR_TEST);
	clipenabled = false;
	clipx = clipy = 0;
	clipw = vid.width;
	cliph = vid.height;
}

/* slot 0x94: clip from the remembered left edge up to column x */
void
SoF_ClipRightTo(int x)
{
	if ((gl_scissor_broken && gl_scissor_broken->value) || (x < clipx))
	{
		return;
	}

	Scissor(clipx, x, clipy, cliph);
}

/* slot 0x98: clip from column x to the remembered right edge */
void
SoF_ClipLeftFrom(int x)
{
	if ((gl_scissor_broken && gl_scissor_broken->value) || (x < clipx))
	{
		return;
	}

	Scissor(x, clipx + clipw, clipy, cliph);
}

/* slot 0x9c */
void
SoF_ReapplyClipRect(void)
{
	if (gl_scissor_broken && gl_scissor_broken->value)
	{
		return;
	}

	Scissor(clipx, clipx + clipw, clipy, cliph);
}

/* ------------------------------------------------------------------
 * Proportional fonts (SPEC-412 §6, SPEC-132)
 * ------------------------------------------------------------------ */

typedef struct
{
	float s0, t0, s1, t1;
	int width, height, baseline;
} sofglyph_t;

typedef struct
{
	const char *name;      /* font name used by the engine */
	const char *file;      /* .fnt / .m32 base name */
	int first, end;        /* code range, end exclusive */
	int lineheight, ascent;
	sofglyph_t glyphs[224];
	int numglyphs;
	gl3image_t *page;
	qboolean loaded;
} soffont_t;

static soffont_t fonts[4] = {
	{"small", "pics/menus/fonts/font1", 32, 256, 10, 8},
	{"medium", "pics/menus/fonts/font4", 32, 256, 12, 10},
	{"title", "pics/menus/fonts/font2", 32, 256, 18, 14},
	{"interface", "pics/menus/fonts/inttext", 32, 64, 32, 24}
};

/* colour remembered across slot 0xc8 calls (keep) */
static byte fontcolor[4] = {255, 255, 255, 255};

void
GL3_SoF2D_InitFonts(void)
{
	int i, j;

	fontcolor[0] = fontcolor[1] = fontcolor[2] = fontcolor[3] = 255;

	for (i = 0; i < 4; i++)
	{
		soffont_t *f = &fonts[i];
		char path[MAX_QPATH];
		byte *buf = NULL;
		int len;

		Com_sprintf(path, sizeof(path), "%s.fnt", f->file);
		len = ri.FS_LoadFile(path, (void **)&buf);

		if (!buf)
		{
			/* SPEC-412 §6 / SPEC-AMEND-002 1.10: a missing .fnt is fatal */
			Com_Error(ERR_FATAL, "Unable to find : %s", path);
			return;
		}

		f->numglyphs = len / 28;

		if (f->numglyphs > 224)
		{
			f->numglyphs = 224;
		}

		for (j = 0; j < f->numglyphs; j++)
		{
			const byte *r = buf + 28 * j;

			f->glyphs[j].s0 = LittleFloat(*(const float *)(r + 0));
			f->glyphs[j].t0 = LittleFloat(*(const float *)(r + 4));
			f->glyphs[j].s1 = LittleFloat(*(const float *)(r + 8));
			f->glyphs[j].t1 = LittleFloat(*(const float *)(r + 12));
			f->glyphs[j].width = LittleLong(*(const int *)(r + 16));
			f->glyphs[j].height = LittleLong(*(const int *)(r + 20));
			f->glyphs[j].baseline = LittleLong(*(const int *)(r + 24));
		}

		ri.FS_FreeFile(buf);

		/* the page; a missing page draws with the notexture pic */
		f->page = GL3_SoF_FindImage(f->file, SOFIMG_FONT, true);

		if (!f->page)
		{
			f->page = GL3_SoF_NoTexture();
		}
		else
		{
			f->page->softype = SOFIMG_FONT;
		}

		f->loaded = true;
	}
}

void
GL3_SoF2D_ShutdownFonts(void)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		fonts[i].loaded = false;
		fonts[i].page = NULL;
	}
}

static soffont_t *
FindFont(const om_fontref_t *ref)
{
	int i;

	if (!ref || !ref->chars)
	{
		return NULL;
	}

	for (i = 0; i < 4; i++)
	{
		if (fonts[i].loaded && !strcmp(fonts[i].name, ref->chars))
		{
			return &fonts[i];
		}
	}

	return NULL;
}

static const sofglyph_t *
FontGlyph(const soffont_t *f, int c)
{
	const sofglyph_t *g;

	if ((c < f->first) || (c >= f->end) || (c - 32 >= f->numglyphs))
	{
		return NULL;
	}

	g = &f->glyphs[c - 32];

	if ((g->width == 0) && ('.' - 32 < f->numglyphs))
	{
		g = &f->glyphs['.' - 32];
	}

	return g;
}

/* slot 0xc8 */
void
SoF_DrawFontString(int x, int y, char *text, unsigned int rgba, const om_fontref_t *font, int keep)
{
	soffont_t *f = FindFont(font);
	const byte *s = (const byte *)text;
	int penx = x, base;

	if (!f || !text)
	{
		return;
	}

	if (!(keep & 0xFF))
	{
		UnpackRGBA(rgba, fontcolor);
	}

	v2d.texmode = TEXMODE_MODULATE;
	v2d.blend = true;
	base = y + f->ascent;

	for (; *s; s++)
	{
		int c = *s;
		const sofglyph_t *g;

		if (c == 10)
		{
			base += f->lineheight;
			penx = x;
			continue;
		}

		if (c == 13)
		{
			continue;
		}

		if (c < 32)
		{
			memcpy(fontcolor, sof_textcolors[c], 4);
			continue;
		}

		if (c == 32)
		{
			penx += f->lineheight / 2;
			continue;
		}

		g = FontGlyph(f, c);

		if (!g)
		{
			continue;
		}

		if ((g->width > 0) && (g->height > 0))
		{
			float xy[4][2], st[4][2];

			RectXY(xy, penx, base - g->baseline, g->width, g->height);
			RectST(st, g->s0, g->t0, g->s1, g->t1);
			Quad(f->page, xy, st, fontcolor, TEXMODE_MODULATE);
		}

		penx += g->width;
	}

	memcpy(v2d.color, fontcolor, 4);
}

/* slot 0xcc */
int
SoF_FontStringWidth(char *text, const om_fontref_t *font)
{
	soffont_t *f = FindFont(font);
	const byte *s = (const byte *)text;
	int w = 0;

	if (!f || !text)
	{
		return 0;
	}

	for (; *s; s++)
	{
		const sofglyph_t *g;

		if (*s == 32)
		{
			w += f->lineheight / 2;
			continue;
		}

		if (*s < 32)
		{
			continue;
		}

		g = FontGlyph(f, *s);

		if (g)
		{
			w += g->width;
		}
	}

	return w;
}

/* slot 0xd0 */
int
SoF_FontLineHeight(const om_fontref_t *font)
{
	soffont_t *f = FindFont(font);

	return f ? f->lineheight : 0;
}

/* ------------------------------------------------------------------
 * World labels (SPEC-412 §7)
 * ------------------------------------------------------------------ */

void
SoF_DrawWorldLabel(float *origin, char *text, char *pic, unsigned int rgba)
{
	vec3_t d, forward, right, up;
	float across, upward, depth, k, k2;
	int cx, cy, sx, sy, sy2, h = 0;
	om_fontref_t small;

	if (!origin)
	{
		return;
	}

	AngleVectors(r_newrefdef.viewangles, forward, right, up);
	VectorSubtract(origin, r_newrefdef.vieworg, d);
	across = DotProduct(d, right);
	upward = DotProduct(d, up);
	depth = DotProduct(d, forward);

	if ((depth < 0.01f) || (r_newrefdef.fov_x <= 0) || (r_newrefdef.fov_y <= 0))
	{
		return;
	}

	cx = r_newrefdef.width / 2;
	cy = r_newrefdef.height / 2;
	sx = (int)(cx + across * cx / depth * 95.0f / r_newrefdef.fov_x);
	sy = (int)(cy - upward * cy / depth * 95.0f / r_newrefdef.fov_y);

	k = (depth > 256) ? 0.7f : 1.0f - 0.3f * depth / 256.0f;

	if (sx < 0)
	{
		sx = (int)(sx + 5 * k);
	}
	else if (sx > 0)
	{
		sx = (int)(sx - 5 * k);
	}

	sy2 = (int)(sy - 20 * k);

	memset(&small, 0, sizeof(small));
	small.chars = "small";
	small.length = 5;
	h = SoF_FontLineHeight(&small);

	if (text)
	{
		/* the text is always opaque white (SPEC-AMEND-002 1.5) */
		SoF_DrawFontString(sx - SoF_FontStringWidth(text, &small) / 2, sy2 + h / 2, text,
			0xFFFFFFFFu, &small, 0);
	}

	if (pic)
	{
		int pw, ph, dw, dh;
		byte c[4];

		k2 = (depth > 256) ? 0.4f : 1.0f - 0.6f * depth / 256.0f;
		SoF_DrawGetPicSize(&pw, &ph, pic);
		dw = (int)(pw * k2);
		dh = (int)(ph * k2);
		UnpackRGBA(rgba, c);
		SoF_DrawStretchPic(sx - dw / 2, (int)(sy2 - h / 2 - dh / 2 - 4 * k2), dw, dh, c, pic, 0);
	}
}

/* ------------------------------------------------------------------
 * Thumbnails, screenshots, raw shots (SPEC-412 §12)
 * ------------------------------------------------------------------ */

static byte *
ReadWindowRGB(int *w, int *h)
{
	byte *buf;

	*w = vid.width;
	*h = vid.height;
	buf = malloc((size_t)(*w) * (*h) * 3);

	if (!buf)
	{
		return NULL;
	}

	GL3_SoF2D_Flush();
	GL3_FX_Flush();
	GL3_DrawCurrent2Dbatch();
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, *w, *h, GL_RGB, GL_UNSIGNED_BYTE, buf);
	return buf;
}

/* slot 0xd4: point-sampled w x h RGBA thumbnail, rows bottom to top */
void
SoF_ReadThumbnail(unsigned char *out, int w, int h)
{
	int W, H, i, j, sx, sy;
	byte *buf;

	if (!out || (w <= 0) || (h <= 0))
	{
		return;
	}

	buf = ReadWindowRGB(&W, &H);

	if (!buf)
	{
		return;
	}

	sx = (W * 65536) / w;
	sy = (H * 65536) / h;

	for (j = 0; j < h; j++)
	{
		const byte *row = buf + (size_t)((j * sy) >> 16) * W * 3;

		for (i = 0; i < w; i++)
		{
			const byte *p = row + ((i * sx) >> 16) * 3;
			byte *o = out + ((size_t)j * w + i) * 4;

			o[0] = p[0];
			o[1] = p[1];
			o[2] = p[2];
			o[3] = 255;
		}
	}

	free(buf);
}

extern const char *GL3_UserDir(void);
extern qboolean GL3_NextFreeName(char *path);

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/*
 * screenshot [name]: <user dir>/scrnshot/sofNN.tga (or <name>.tga), uncompressed 24-bit, bottom row
 * first. The command only asks for it; the frame is read at the end of the next frame, before the
 * buffer swap (GL3_SoF_ScreenShotFrame): after the swap the back buffer is undefined with flip-model
 * presentation (DXGI), which gave black screenshots.
 */
static int shot_pending;
static char shot_name[MAX_QPATH];

void
GL3_SoF_ScreenShot_f(void)
{
	shot_pending = 1;
	shot_name[0] = 0;

	if (ri.Cmd_Argc() > 1)
	{
		const char *s = ri.Cmd_Argv(1);
		int i;

		for (i = 0; s[i] && (i < (int)sizeof(shot_name) - 1); i++)
		{
			char c = s[i];

			/* a plain file name only */
			shot_name[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
					(c == '_') || (c == '-')) ? c : '_';
		}

		shot_name[i] = 0;
	}
}

void
GL3_SoF_ScreenShotFrame(void)
{
	char path[MAX_OSPATH];
	byte header[18], *buf;
	int w, h, i;
	FILE *f;

	if (!shot_pending)
	{
		return;
	}

	shot_pending = 0;
	Com_sprintf(path, sizeof(path), "%s/scrnshot", GL3_UserDir());
	MKDIR(path);

	if (shot_name[0])
	{
		Com_sprintf(path, sizeof(path), "%s/scrnshot/%s.tga", GL3_UserDir(), shot_name);
	}
	else
	{
		Com_sprintf(path, sizeof(path), "%s/scrnshot/sof00.tga", GL3_UserDir());

		if (!GL3_NextFreeName(path))
		{
			Com_Printf("SCR_ScreenShot_f: Couldn't create a file\n");
			return;
		}
	}

	buf = ReadWindowRGB(&w, &h);

	if (!buf)
	{
		return;
	}

	for (i = 0; i < w * h; i++)
	{
		byte t = buf[i * 3];

		buf[i * 3] = buf[i * 3 + 2];
		buf[i * 3 + 2] = t;
	}

	memset(header, 0, sizeof(header));
	header[2] = 2;
	header[12] = w & 255;
	header[13] = (w >> 8) & 255;
	header[14] = h & 255;
	header[15] = (h >> 8) & 255;
	header[16] = 24;

	f = Q_fopen(path, "wb");

	if (f && (fwrite(header, 1, 18, f) == 18) && (fwrite(buf, 1, (size_t)w * h * 3, f) == (size_t)w * h * 3))
	{
		fclose(f);
		Com_Printf("Wrote %s\n", path);
	}
	else
	{
		if (f)
		{
			fclose(f);
		}

		Com_Printf("Error writing %s\n", path);
	}

	free(buf);
}

/* rawshot: <user dir>/rawshot/sofNN.raw, a 128 x 128 thumbnail */
void
GL3_SoF_RawShot_f(void)
{
	char path[MAX_OSPATH];
	byte *buf;
	FILE *f;

	Com_sprintf(path, sizeof(path), "%s/rawshot", GL3_UserDir());
	MKDIR(path);
	Com_sprintf(path, sizeof(path), "%s/rawshot/sof00.raw", GL3_UserDir());

	if (!GL3_NextFreeName(path))
	{
		Com_Printf("SCR_RawShot_f: Couldn't create a file\n");
		return;
	}

	buf = malloc(128 * 128 * 4);

	if (!buf)
	{
		Com_Printf("SCR_RawShot_f: Unable to allocate memory\n");
		return;
	}

	SoF_ReadThumbnail(buf, 128, 128);
	f = Q_fopen(path, "wb");

	if (f && (fwrite(buf, 1, 128 * 128 * 4, f) == 128 * 128 * 4))
	{
		fclose(f);
		Com_Printf("Wrote %s\n", path);
	}
	else
	{
		if (f)
		{
			fclose(f);
		}

		Com_Printf("Error writing %s\n", path);
	}

	free(buf);
}
