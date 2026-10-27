/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: the GL 1.1 entry-point table of the Ghoul system
 * (SPEC-010 G, 52 entries) emulated on the GL 3 core profile (and GLES 3),
 * for renderers without a compatibility context: the OpenMerc-hosted
 * ref_gl3/ref_gles3 and, for comparison, the SoF module with r_om_ghlemu 1.
 *
 * The fixed-function vertex stage is done on the CPU when a vertex is
 * issued (modelview / projection / texture matrix stacks, OpenGL lighting
 * with up to 8 lights, sphere-map and linear texture coordinate generation,
 * the fog coordinate), primitives are turned into triangles (flat shading
 * by the provoking vertex) and every Begin/End pair is drawn with one small
 * shader that does the fragment stage: texture environment (MODULATE,
 * REPLACE, DECAL, BLEND, ADD) for the GL 1.1 base formats, fog, alpha test.
 * Blending, depth, face culling, polygon offset and polygon mode are real
 * GL 3 state and go straight to OpenGL. Texture names, images and
 * parameters are real GL 3 textures (GL_CLAMP becomes GL_CLAMP_TO_EDGE,
 * filters follow gl_texturemode as SPEC-411 requires of the table).
 *
 * Implements: SPEC-010 G (GL entry-point table), SPEC-411 (GL entry-point
 *             table supplied to the Ghoul system), SPEC-012 §8 (the drawing
 *             state the Ghoul system uses)
 */

#include "header/local.h"
#include "header/gl3_ghoul.h"

/* ------------------------------------------------------------------
 * state
 * ------------------------------------------------------------------ */

#define MV_DEPTH 32
#define OTHER_DEPTH 8
#define NUM_LIGHTS 8

typedef struct
{
	float pos[4];   /* clip space */
	float rgba[4];
	float st[2];
	float fog;      /* 1 = no fog */
} emuvtx_t;

typedef struct
{
	float ambient[4], diffuse[4], specular[4];
	float position[4];   /* eye space */
	float spotdir[3];    /* eye space */
	float spotexp, spotcut;
	float k[3];          /* constant, linear, quadratic attenuation */
	qboolean on;
} emulight_t;

typedef struct
{
	float ambient[4], diffuse[4], specular[4], emission[4];
	float shininess;
} emumaterial_t;

/* texel swizzles of the GL 1.1 base formats (shader uniform "texswz") */
enum
{
	SWZ_RGBA, SWZ_RGB, SWZ_A_FROM_R, SWZ_A_FROM_A, SWZ_L, SWZ_LA, SWZ_I, SWZ_A_FROM_G, SWZ_L_A_FROM_A
};

static struct
{
	qboolean ready;

	float mv[MV_DEPTH][16];
	float proj[OTHER_DEPTH][16];
	float tex[OTHER_DEPTH][16];
	int mvdepth, projdepth, texdepth;
	GLenum matrixmode;
	float normalmat[9];
	qboolean normaldirty;

	float color[4], normal[3], st[2];

	qboolean alphatest, fog, lighting, texture2d, gens, gent, normalize, rescale, colormaterial;
	GLenum alphafunc;
	float alpharef;
	GLenum shademodel, texenv, genmode_s, genmode_t;

	GLenum fogmode;
	float fogdensity, fogstart, fogend, fogcolor[4];

	float lmambient[4];
	qboolean localviewer, twoside;
	emulight_t light[NUM_LIGHTS];
	emumaterial_t mat;

	GLuint boundtex;
	byte *texswz;     /* per texture name */
	int numtexswz;

	/* the primitive being built */
	qboolean inbegin;
	GLenum prim;
	emuvtx_t *verts;
	int numverts, maxverts;
	emuvtx_t *tris;
	int maxtris;

	/* GL objects */
	GLuint prog, vao, vbo;
	GLint u_tex, u_texenv, u_texswz, u_alphafunc, u_alpharef, u_fog, u_fogcolor;

	/* GFX-modern bloom (gl3_post.c): blending as set, the Ghoul system's source weight of the next
	   primitive and the glow of the entity being drawn */
	qboolean blendon;
	GLenum bsrc, bdst;
	float bloomsource, entityglow;
	GLint u_emis;

	/* GFX-modern sun shadows (gl3_csm.c): the depth mask as set, whether the entity casts and whether its
	   ground shadow gives way to the sun shadow */
	qboolean depthmask, csmcapture, csmrepl;
	GLint u_invpv, u_csmrepl;
} E;

static ghlff_t emuff;
static void *emutable[52];

/* ------------------------------------------------------------------
 * matrices (column-major, as OpenGL)
 * ------------------------------------------------------------------ */

static void
MatIdentity(float *m)
{
	int i;

	for (i = 0; i < 16; i++)
	{
		m[i] = (i % 5) ? 0.0f : 1.0f;
	}
}

/* out = a * b */
static void
MatMul(float *out, const float *a, const float *b)
{
	float r[16];
	int c, k;

	for (c = 0; c < 4; c++)
	{
		for (k = 0; k < 4; k++)
		{
			r[c * 4 + k] = a[0 * 4 + k] * b[c * 4 + 0] + a[1 * 4 + k] * b[c * 4 + 1] + a[2 * 4 + k] * b[c * 4 + 2] +
				a[3 * 4 + k] * b[c * 4 + 3];
		}
	}

	memcpy(out, r, sizeof(r));
}

static void
MatVec(const float *m, const float *v, float *out)
{
	int k;

	for (k = 0; k < 4; k++)
	{
		out[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2] + m[12 + k] * v[3];
	}
}

static float *
CurrentMatrix(void)
{
	switch (E.matrixmode)
	{
		case GHL_GL_PROJECTION:
			return E.proj[E.projdepth];
		case GHL_GL_TEXTURE:
			return E.tex[E.texdepth];
		default:
			return E.mv[E.mvdepth];
	}
}

static void
MatrixChanged(void)
{
	if ((E.matrixmode != GHL_GL_PROJECTION) && (E.matrixmode != GHL_GL_TEXTURE))
	{
		E.normaldirty = true;
	}
}

/* inverse transpose of the modelview's upper 3x3 (row-major 3x3 result) */
static void
UpdateNormalMatrix(void)
{
	const float *m = E.mv[E.mvdepth];
	float a = m[0], b = m[4], c = m[8];
	float d = m[1], e = m[5], f = m[9];
	float g = m[2], h = m[6], i = m[10];
	float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
	float det = a * A + b * B + c * C;
	float s;

	E.normaldirty = false;

	if (fabsf(det) < 1e-12f)
	{
		memset(E.normalmat, 0, sizeof(E.normalmat));
		E.normalmat[0] = E.normalmat[4] = E.normalmat[8] = 1;
		return;
	}

	s = 1.0f / det;
	/* inverse transpose = cofactor matrix / det */
	E.normalmat[0] = A * s;
	E.normalmat[1] = B * s;
	E.normalmat[2] = C * s;
	E.normalmat[3] = -(b * i - c * h) * s;
	E.normalmat[4] = (a * i - c * g) * s;
	E.normalmat[5] = -(a * h - b * g) * s;
	E.normalmat[6] = (b * f - c * e) * s;
	E.normalmat[7] = -(a * f - c * d) * s;
	E.normalmat[8] = (a * e - b * d) * s;
}

/* n_eye = (M^-1)^T n: row r of the result uses row r of normalmat */
static void
EyeNormal(const float *n, float *out)
{
	const float *q = E.normalmat;

	if (E.normaldirty)
	{
		UpdateNormalMatrix();
	}

	out[0] = q[0] * n[0] + q[1] * n[1] + q[2] * n[2];
	out[1] = q[3] * n[0] + q[4] * n[1] + q[5] * n[2];
	out[2] = q[6] * n[0] + q[7] * n[1] + q[8] * n[2];

	if (E.normalize || E.rescale)
	{
		float l = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);

		if (l > 0)
		{
			out[0] /= l;
			out[1] /= l;
			out[2] /= l;
		}
	}
}

/* ------------------------------------------------------------------
 * the vertex stage
 * ------------------------------------------------------------------ */

static float
Clamp01(float v)
{
	return (v < 0) ? 0 : ((v > 1) ? 1 : v);
}

static void
Lighting(const float *eye, float *rgba)
{
	float n[3], c[3], ma[4], md[4];
	int i, k;

	EyeNormal(E.normal, n);

	if (E.colormaterial)
	{
		memcpy(ma, E.color, sizeof(ma));
		memcpy(md, E.color, sizeof(md));
	}
	else
	{
		memcpy(ma, E.mat.ambient, sizeof(ma));
		memcpy(md, E.mat.diffuse, sizeof(md));
	}

	for (k = 0; k < 3; k++)
	{
		c[k] = E.mat.emission[k] + ma[k] * E.lmambient[k];
	}

	for (i = 0; i < NUM_LIGHTS; i++)
	{
		const emulight_t *l = &E.light[i];
		float L[3], d = 0, att = 1, spot = 1, ndotl, h[3], hl, spec = 0;

		if (!l->on)
		{
			continue;
		}

		if (l->position[3] != 0)
		{
			for (k = 0; k < 3; k++)
			{
				L[k] = l->position[k] / l->position[3] - eye[k];
			}

			d = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);

			if (d > 0)
			{
				L[0] /= d;
				L[1] /= d;
				L[2] /= d;
			}

			att = l->k[0] + l->k[1] * d + l->k[2] * d * d;
			att = (att > 0) ? 1.0f / att : 1.0f;
		}
		else
		{
			float len = sqrtf(l->position[0] * l->position[0] + l->position[1] * l->position[1] +
				l->position[2] * l->position[2]);

			for (k = 0; k < 3; k++)
			{
				L[k] = (len > 0) ? l->position[k] / len : 0;
			}
		}

		if (l->spotcut != 180)
		{
			float sl = sqrtf(l->spotdir[0] * l->spotdir[0] + l->spotdir[1] * l->spotdir[1] +
				l->spotdir[2] * l->spotdir[2]);
			float cs = (sl > 0) ? -(L[0] * l->spotdir[0] + L[1] * l->spotdir[1] + L[2] * l->spotdir[2]) / sl : 0;

			spot = (cs < cosf(l->spotcut * (float)M_PI / 180.0f)) ? 0 : powf(cs > 0 ? cs : 0, l->spotexp);
		}

		ndotl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];

		if (ndotl < 0)
		{
			ndotl = 0;
		}

		if (ndotl > 0)
		{
			if (E.localviewer)
			{
				float el = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);

				for (k = 0; k < 3; k++)
				{
					h[k] = L[k] - ((el > 0) ? eye[k] / el : 0);
				}
			}
			else
			{
				h[0] = L[0];
				h[1] = L[1];
				h[2] = L[2] + 1;
			}

			hl = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);

			if (hl > 0)
			{
				float nh = (n[0] * h[0] + n[1] * h[1] + n[2] * h[2]) / hl;

				spec = (nh > 0) ? ((E.mat.shininess > 0) ? powf(nh, E.mat.shininess) : 1) : 0;
			}
		}

		for (k = 0; k < 3; k++)
		{
			c[k] += att * spot * (l->ambient[k] * ma[k] + ndotl * l->diffuse[k] * md[k] +
				spec * l->specular[k] * E.mat.specular[k]);
		}
	}

	rgba[0] = Clamp01(c[0]);
	rgba[1] = Clamp01(c[1]);
	rgba[2] = Clamp01(c[2]);
	rgba[3] = Clamp01(md[3]);
}

static float
GenCoord(GLenum mode, int axis, const float *obj, const float *eye, const float *sphere)
{
	/* default planes: S = (1, 0, 0, 0), T = (0, 1, 0, 0) (object and eye) */
	switch (mode)
	{
		case GHL_GL_SPHERE_MAP:
			return sphere[axis];
		case GHL_GL_OBJECT_LINEAR:
			return obj[axis];
		default:
			return eye[axis];
	}
}

static void
Emit(float x, float y, float z)
{
	float obj[4] = {x, y, z, 1}, eye[4], st[4] = {0, 0, 0, 1}, out[4];
	emuvtx_t *v;

	if (!E.inbegin)
	{
		return;
	}

	if (E.numverts == E.maxverts)
	{
		int n = E.maxverts ? E.maxverts * 2 : 1024;
		emuvtx_t *nv = realloc(E.verts, n * sizeof(*nv));

		if (!nv)
		{
			return;
		}

		E.verts = nv;
		E.maxverts = n;
	}

	v = &E.verts[E.numverts++];
	MatVec(E.mv[E.mvdepth], obj, eye);
	MatVec(E.proj[E.projdepth], eye, v->pos);

	if (E.lighting)
	{
		Lighting(eye, v->rgba);
	}
	else
	{
		/* GL clamps the vertex colour to 0..1 before it is interpolated */
		int k;

		for (k = 0; k < 4; k++)
		{
			v->rgba[k] = Clamp01(E.color[k]);
		}
	}

	st[0] = E.st[0];
	st[1] = E.st[1];

	if (E.gens || E.gent)
	{
		float sphere[2] = {0, 0};

		if ((E.gens && (E.genmode_s == GHL_GL_SPHERE_MAP)) || (E.gent && (E.genmode_t == GHL_GL_SPHERE_MAP)))
		{
			float u[3], n[3], r[3], l, nu, m;
			int k;

			l = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);

			for (k = 0; k < 3; k++)
			{
				u[k] = (l > 0) ? eye[k] / l : 0;
			}

			EyeNormal(E.normal, n);
			nu = n[0] * u[0] + n[1] * u[1] + n[2] * u[2];

			for (k = 0; k < 3; k++)
			{
				r[k] = u[k] - 2 * n[k] * nu;
			}

			m = 2 * sqrtf(r[0] * r[0] + r[1] * r[1] + (r[2] + 1) * (r[2] + 1));
			sphere[0] = (m > 0) ? r[0] / m + 0.5f : 0.5f;
			sphere[1] = (m > 0) ? r[1] / m + 0.5f : 0.5f;
		}

		if (E.gens)
		{
			st[0] = GenCoord(E.genmode_s, 0, obj, eye, sphere);
		}

		if (E.gent)
		{
			st[1] = GenCoord(E.genmode_t, 1, obj, eye, sphere);
		}
	}

	MatVec(E.tex[E.texdepth], st, out);
	v->st[0] = (out[3] != 0) ? out[0] / out[3] : out[0];
	v->st[1] = (out[3] != 0) ? out[1] / out[3] : out[1];

	if (E.fog)
	{
		float c = fabsf(eye[2] / ((eye[3] != 0) ? eye[3] : 1)), f;

		switch (E.fogmode)
		{
			case GL_LINEAR:
				f = (E.fogend != E.fogstart) ? (E.fogend - c) / (E.fogend - E.fogstart) : 1;
				break;
			case 0x0801: /* GL_EXP2 */
				f = expf(-(E.fogdensity * c) * (E.fogdensity * c));
				break;
			default:     /* GL_EXP */
				f = expf(-E.fogdensity * c);
				break;
		}

		v->fog = Clamp01(f);
	}
	else
	{
		v->fog = 1;
	}
}

/* ------------------------------------------------------------------
 * shader and drawing
 * ------------------------------------------------------------------ */

static const char *emu_vs =
	"in vec4 position;\n"
	"in vec2 texCoord;\n"
	"in vec4 vertColor;\n"
	"in float fogFactor;\n"
	"out vec4 vColor;\n"
	"out vec2 vTex;\n"
	"out float vFog;\n"
	"out vec4 vPos;\n"
	"void main()\n"
	"{\n"
	"	vPos = position;\n"
	"	vColor = vertColor;\n"
	"	vTex = texCoord;\n"
	"	vFog = fogFactor;\n"
	"	gl_Position = position;\n"
	"}\n";

static const char *emu_fs =
	"uniform sampler2D tex;\n"
	"uniform int texenv;\n"   /* 0 none, 1 modulate, 2 replace, 3 decal, 4 blend, 5 add */
	"uniform int texswz;\n"
	"uniform int alphafunc;\n" /* 0 never ... 7 always */
	"uniform float alpharef;\n"
	"uniform int fogon;\n"
	"uniform vec4 fogcolor;\n"
	"uniform vec4 omEmis;\n"   /* GFX-modern: (x, y, z, mode) as in the effect shader */
	"in vec4 vColor;\n"
	"in vec2 vTex;\n"
	"in float vFog;\n"
	"in vec4 vPos;\n"
	"uniform mat4 omInvPV;\n"
	"uniform int omCsmRepl;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec4 c = vColor;\n"
	"	if (texenv != 0)\n"
	"	{\n"
	"		vec4 r = texture(tex, vTex);\n"
	"		vec4 t;\n"
	"		bool hasColor = true, hasAlpha = true;\n"
	"		if (texswz == 0) t = r;\n"
	"		else if (texswz == 1) { t = vec4(r.rgb, 1.0); hasAlpha = false; }\n"
	"		else if (texswz == 2) { t = vec4(1.0, 1.0, 1.0, r.r); hasColor = false; }\n"
	"		else if (texswz == 3) { t = vec4(1.0, 1.0, 1.0, r.a); hasColor = false; }\n"
	"		else if (texswz == 4) { t = vec4(r.rrr, 1.0); hasAlpha = false; }\n"
	"		else if (texswz == 5) t = vec4(r.rrr, r.g);\n"
	"		else if (texswz == 6) t = r.rrrr;\n"
	"		else if (texswz == 7) { t = vec4(1.0, 1.0, 1.0, r.g); hasColor = false; }\n"
	"		else t = vec4(r.rrr, r.a);\n"
	"		if (texenv == 2)\n"
	"		{\n"
	"			c = vec4(hasColor ? t.rgb : c.rgb, hasAlpha ? t.a : c.a);\n"
	"		}\n"
	"		else if (texenv == 3)\n"
	"		{\n"
	"			if (!hasAlpha) c.rgb = t.rgb;\n"
	"			else if (hasColor) c.rgb = mix(c.rgb, t.rgb, t.a);\n"
	"			else c *= t;\n"
	"		}\n"
	"		else if (texenv == 4)\n"
	"		{\n"
	"			c.rgb = c.rgb * (vec3(1.0) - t.rgb);\n"
	"			c.a = c.a * t.a;\n"
	"		}\n"
	"		else if (texenv == 5)\n"
	"		{\n"
	"			c.rgb = min(c.rgb + t.rgb, vec3(1.0));\n"
	"			c.a = c.a * t.a;\n"
	"		}\n"
	"		else c *= t;\n"
	"	}\n"
	"	vec3 omPre = (fogon != 0) ? c.rgb * vFog : c.rgb;\n"
	"	if (fogon != 0) c.rgb = mix(fogcolor.rgb, c.rgb, vFog);\n"
	"	if (omCsmRepl != 0)\n"
	"	{\n"
	"		vec4 w = omInvPV * vPos;\n"
	"		c.a *= omCsmGhoulShadowKeep(w.xyz / w.w);\n"
	"	}\n"
	"	bool pass = true;\n"
	"	if (alphafunc == 0) pass = false;\n"
	"	else if (alphafunc == 1) pass = c.a < alpharef;\n"
	"	else if (alphafunc == 2) pass = c.a == alpharef;\n"
	"	else if (alphafunc == 3) pass = c.a <= alpharef;\n"
	"	else if (alphafunc == 4) pass = c.a > alpharef;\n"
	"	else if (alphafunc == 5) pass = c.a != alpharef;\n"
	"	else if (alphafunc == 6) pass = c.a >= alpharef;\n"
	"	if (!pass) discard;\n"
	"	outColor = c;\n"
	"	if (omEmis.w > 1.5) outEmissive = vec4(omEmis.xyz, c.a);\n"
	"	else if (omEmis.w > 0.5) outEmissive = vec4(omPre * omEmis.x, c.a);\n"
	"}\n";

static GLuint
Compile(GLenum type, const char *src)
{
#ifdef YQ2_GL3_GLES3
	const char *version = "#version 300 es\nprecision highp float;\n";
#else
	const char *version = "#version 150\n";
#endif
	const char *srcs[2] = {version, src};
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	/* GFX-modern: the emissive output of the post path */
	char *wrapped = (type == GL_FRAGMENT_SHADER) ? GL3_Post_WrapFragment(src, NULL) : NULL;

	if (wrapped)
	{
		srcs[1] = wrapped;
	}

	glShaderSource(s, 2, srcs, NULL);
	glCompileShader(s);
	free(wrapped);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		Com_Printf("Ghoul GL emulation: shader compile failed: %s\n", log);
		glDeleteShader(s);
		return 0;
	}

	return s;
}

static qboolean
CreateObjects(void)
{
	GLuint vs, fs;
	GLint ok = 0;

	if (E.prog)
	{
		return true;
	}

	vs = Compile(GL_VERTEX_SHADER, emu_vs);
	{
		/* GFX-modern: the sun shadow functions in front (gl3_csm.c) */
		char *f = GL3_CSM_Prepend(emu_fs);

		fs = Compile(GL_FRAGMENT_SHADER, f ? f : emu_fs);
		free(f);
	}

	if (!vs || !fs)
	{
		return false;
	}

	E.prog = glCreateProgram();
	glAttachShader(E.prog, vs);
	glAttachShader(E.prog, fs);
	glBindAttribLocation(E.prog, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(E.prog, GL3_ATTRIB_TEXCOORD, "texCoord");
	glBindAttribLocation(E.prog, GL3_ATTRIB_LMTEXCOORD, "fogFactor");
	glBindAttribLocation(E.prog, GL3_ATTRIB_COLOR, "vertColor");
	GL3_Post_BindOutputs(E.prog);
	glLinkProgram(E.prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	glGetProgramiv(E.prog, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(E.prog, sizeof(log), NULL, log);
		Com_Printf("Ghoul GL emulation: link failed: %s\n", log);
		glDeleteProgram(E.prog);
		E.prog = 0;
		return false;
	}

	E.u_tex = glGetUniformLocation(E.prog, "tex");
	E.u_texenv = glGetUniformLocation(E.prog, "texenv");
	E.u_texswz = glGetUniformLocation(E.prog, "texswz");
	E.u_alphafunc = glGetUniformLocation(E.prog, "alphafunc");
	E.u_alpharef = glGetUniformLocation(E.prog, "alpharef");
	E.u_fog = glGetUniformLocation(E.prog, "fogon");
	E.u_fogcolor = glGetUniformLocation(E.prog, "fogcolor");
	E.u_emis = glGetUniformLocation(E.prog, "omEmis");
	E.u_invpv = glGetUniformLocation(E.prog, "omInvPV");
	E.u_csmrepl = glGetUniformLocation(E.prog, "omCsmRepl");
	GL3_UseProgram(E.prog);
	GL3_CSM_BindProgram(E.prog);

	glGenVertexArrays(1, &E.vao);
	GL3_BindVAO(E.vao);
	glGenBuffers(1, &E.vbo);
	GL3_BindVBO(E.vbo);
	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(emuvtx_t), offsetof(emuvtx_t, pos));
	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(emuvtx_t), offsetof(emuvtx_t, rgba));
	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(emuvtx_t), offsetof(emuvtx_t, st));
	glEnableVertexAttribArray(GL3_ATTRIB_LMTEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_LMTEXCOORD, 1, GL_FLOAT, GL_FALSE, sizeof(emuvtx_t), offsetof(emuvtx_t, fog));
	GL3_BindVAO(0);
	return true;
}

static int
TexSwizzle(GLuint tex)
{
	return (E.texswz && (tex < (GLuint)E.numtexswz)) ? E.texswz[tex] : SWZ_RGBA;
}

static void
SetTexSwizzle(GLuint tex, int swz)
{
	if (tex >= (GLuint)E.numtexswz)
	{
		int n = E.numtexswz ? E.numtexswz : 1024;
		byte *p;

		while ((GLuint)n <= tex)
		{
			n *= 2;
		}

		if (n > (1 << 20))
		{
			return;
		}

		p = realloc(E.texswz, n);

		if (!p)
		{
			return;
		}

		memset(p + E.numtexswz, SWZ_RGBA, n - E.numtexswz);
		E.texswz = p;
		E.numtexswz = n;
	}

	E.texswz[tex] = (byte)swz;
}

static int
AlphaFuncIndex(GLenum f)
{
	return ((f >= GL_NEVER) && (f <= GL_ALWAYS)) ? (int)(f - GL_NEVER) : 7;
}

static int
TexEnvIndex(void)
{
	if (!E.texture2d || !E.boundtex)
	{
		return 0;
	}

	switch (E.texenv)
	{
		case GHL_GL_REPLACE:
			return 2;
		case GHL_GL_DECAL:
			return 3;
		case 0x0BE2: /* GL_BLEND */
			return 4;
		case 0x0104: /* GL_ADD */
			return 5;
		default:
			return 1;
	}
}

/* the provoking vertex of triangle (a, b, c) in primitive mode prim: the last one, but the first for polygons */
static void
AddTri(emuvtx_t *out, int *n, int a, int b, int c, int provoking)
{
	out[*n] = E.verts[a];
	out[*n + 1] = E.verts[b];
	out[*n + 2] = E.verts[c];

	if (E.shademodel == GHL_GL_FLAT)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			memcpy(out[*n + k].rgba, E.verts[provoking].rgba, sizeof(out[0].rgba));
		}
	}

	*n += 3;
}

static void
Flush(void)
{
	GLenum mode = GL_TRIANGLES;
	emuvtx_t *out;
	int n = 0, i, count = E.numverts;

	if (!count || !CreateObjects())
	{
		return;
	}

	/* every primitive becomes at most 3 * count vertices */
	if (E.maxtris < 3 * count)
	{
		emuvtx_t *p = realloc(E.tris, 3 * count * sizeof(*p));

		if (!p)
		{
			return;
		}

		E.tris = p;
		E.maxtris = 3 * count;
	}

	out = E.tris;

	switch (E.prim)
	{
		case GL_POINTS:
			mode = GL_POINTS;
			memcpy(out, E.verts, count * sizeof(*out));
			n = count;
			break;
		case GL_LINES:
			mode = GL_LINES;
			n = count & ~1;
			memcpy(out, E.verts, n * sizeof(*out));
			break;
		case GL_LINE_STRIP:
		case GL_LINE_LOOP:
			mode = GL_LINES;

			for (i = 0; i + 1 < count; i++)
			{
				out[n++] = E.verts[i];
				out[n++] = E.verts[i + 1];
			}

			if ((E.prim == GL_LINE_LOOP) && (count > 2))
			{
				out[n++] = E.verts[count - 1];
				out[n++] = E.verts[0];
			}

			break;
		case GL_TRIANGLES:
			for (i = 0; i + 2 < count; i += 3)
			{
				AddTri(out, &n, i, i + 1, i + 2, i + 2);
			}

			break;
		case GL_TRIANGLE_STRIP:
			for (i = 0; i + 2 < count; i++)
			{
				if (i & 1)
				{
					AddTri(out, &n, i + 1, i, i + 2, i + 2);
				}
				else
				{
					AddTri(out, &n, i, i + 1, i + 2, i + 2);
				}
			}

			break;
		case GL_TRIANGLE_FAN:
			for (i = 1; i + 1 < count; i++)
			{
				AddTri(out, &n, 0, i, i + 1, i + 1);
			}

			break;
		case GHL_GL_QUADS:
			for (i = 0; i + 3 < count; i += 4)
			{
				AddTri(out, &n, i, i + 1, i + 2, i + 3);
				AddTri(out, &n, i, i + 2, i + 3, i + 3);
			}

			break;
		case GHL_GL_QUAD_STRIP:
			for (i = 0; i + 3 < count; i += 2)
			{
				AddTri(out, &n, i, i + 1, i + 3, i + 3);
				AddTri(out, &n, i, i + 3, i + 2, i + 3);
			}

			break;
		case GHL_GL_POLYGON:
			for (i = 1; i + 1 < count; i++)
			{
				AddTri(out, &n, 0, i, i + 1, 0);
			}

			break;
		default:
			break;
	}

	if (!n)
	{
		return;
	}

	GL3_UseProgram(E.prog);
	GL3_BindVAO(E.vao);
	GL3_BindVBO(E.vbo);
	glBufferData(GL_ARRAY_BUFFER, n * sizeof(emuvtx_t), out, GL_STREAM_DRAW);

	GL3_SelectTMU(GL_TEXTURE0);

	if (TexEnvIndex())
	{
		glBindTexture(GL_TEXTURE_2D, E.boundtex);
		gl3state.currenttexture = E.boundtex;
	}

	glUniform1i(E.u_tex, 0);
	glUniform1i(E.u_texenv, TexEnvIndex());
	glUniform1i(E.u_texswz, TexSwizzle(E.boundtex));
	glUniform1i(E.u_alphafunc, E.alphatest ? AlphaFuncIndex(E.alphafunc) : 7);
	glUniform1f(E.u_alpharef, Clamp01(E.alpharef));
	glUniform1i(E.u_fog, E.fog ? 1 : 0);
	glUniform4f(E.u_fogcolor, E.fogcolor[0], E.fogcolor[1], E.fogcolor[2], E.fogcolor[3]);

	/* GFX-modern bloom: Ghoul self-illumination / additive materials as drawn (SPEC-AMEND-016 R16.7),
	   RF_FULLBRIGHT / RF_GLOW entities; multiplying blends keep what is behind */
	{
		float emis[4], glow = 0.0f;

		if (gl3post.bloom)
		{
			if (E.bloomsource >= GL3_Post_GhoulMin())
			{
				glow = Q_min(E.bloomsource, 1.0f);
			}

			glow = Q_max(glow, E.entityglow);
		}

		GL3_Post_EmisForBlend(E.bsrc, E.bdst, E.blendon, glow, emis);

		/* an additive pass that is no light source (a specular pass) adds nothing to the glow */
		if (E.blendon && (E.bdst == GL_ONE) && (glow <= 0.0f))
		{
			emis[0] = emis[1] = emis[2] = 0.0f;
			emis[3] = 2.0f;
		}

		glUniform4fv(E.u_emis, 1, emis);
	}

	/* GFX-modern sun shadows: the ground shadow pass (blend 0, 1 - src alpha, ghoul_draw) gives way to the
	   sun shadow where the ground is in the sun; opaque depth-writing triangles are casters */
	{
		int repl = (E.csmrepl && E.blendon && (E.bsrc == GL_ZERO) && (E.bdst == GL_ONE_MINUS_SRC_ALPHA)) ? 1 : 0;

		glUniform1i(E.u_csmrepl, repl);

		if (repl)
		{
			glUniformMatrix4fv(E.u_invpv, 1, GL_FALSE, GL3_CSM_InvPV());
		}

		if (E.csmcapture && (mode == GL_TRIANGLES) && E.depthmask && !E.blendon && !E.alphatest)
		{
			GL3_CSM_AddCasterTris(out[0].pos, n, sizeof(emuvtx_t) / sizeof(float));
		}
	}

	glDrawArrays(mode, 0, n);
}

/* ------------------------------------------------------------------
 * the 52 entries
 * ------------------------------------------------------------------ */

static qboolean
EmulatedCap(GLenum cap, qboolean on, qboolean set, qboolean *result)
{
	qboolean *flag = NULL;

	switch (cap)
	{
		case GHL_GL_ALPHA_TEST:
			flag = &E.alphatest;
			break;
		case GHL_GL_FOG:
			flag = &E.fog;
			break;
		case GHL_GL_LIGHTING:
			flag = &E.lighting;
			break;
		case GL_TEXTURE_2D:
			flag = &E.texture2d;
			break;
		case GHL_GL_TEXTURE_GEN_S:
			flag = &E.gens;
			break;
		case GHL_GL_TEXTURE_GEN_T:
			flag = &E.gent;
			break;
		case GHL_GL_NORMALIZE:
			flag = &E.normalize;
			break;
		case 0x803A: /* GL_RESCALE_NORMAL */
			flag = &E.rescale;
			break;
		case GHL_GL_COLOR_MATERIAL:
			flag = &E.colormaterial;
			break;
		default:
			if ((cap >= GHL_GL_LIGHT0) && (cap < GHL_GL_LIGHT0 + NUM_LIGHTS))
			{
				flag = &E.light[cap - GHL_GL_LIGHT0].on;
				break;
			}

			/* other fixed-function capabilities have no effect here */
			if ((cap == 0x0B10) || (cap == 0x0B20) || (cap == 0x0B41) || (cap == 0x0C62) || (cap == 0x0C63) ||
				(cap == 0x0BF1) || (cap == 0x0BF2) || ((cap >= 0x3000) && (cap < 0x3006)) ||
				(cap == 0x0DE0) || (cap == 0x0B42) || (cap == 0x0B24) || (cap == 0x0B60 + 1))
			{
				if (result)
				{
					*result = false;
				}

				return true;
			}

			return false;
	}

	if (set)
	{
		*flag = on;
	}

	if (result)
	{
		*result = *flag;
	}

	return true;
}

static void GHLAPI
Emu_Enable(GLenum cap)
{
	if (cap == GL_BLEND)
	{
		E.blendon = true;
	}

	if (!EmulatedCap(cap, true, true, NULL))
	{
		glEnable(cap);
	}
}

static void GHLAPI
Emu_Disable(GLenum cap)
{
	if (cap == GL_BLEND)
	{
		E.blendon = false;
	}

	if (!EmulatedCap(cap, false, true, NULL))
	{
		glDisable(cap);
	}
}

static GLboolean GHLAPI
Emu_IsEnabled(GLenum cap)
{
	qboolean r = false;

	if (EmulatedCap(cap, false, false, &r))
	{
		return r ? GL_TRUE : GL_FALSE;
	}

	return glIsEnabled(cap);
}

static void GHLAPI
Emu_AlphaFunc(GLenum func, GLfloat ref)
{
	E.alphafunc = func;
	E.alpharef = ref;
}

static void GHLAPI
Emu_Begin(GLenum mode)
{
	E.inbegin = true;
	E.prim = mode;
	E.numverts = 0;
}

static void GHLAPI
Emu_End(void)
{
	if (E.inbegin)
	{
		Flush();
	}

	E.inbegin = false;
	E.numverts = 0;
	E.bloomsource = 0.0f; /* it applies to one primitive */
}

static void GHLAPI
Emu_BindTexture(GLenum target, GLuint texture)
{
	extern gl3image_t *draw_chars;

	if (target != GL_TEXTURE_2D)
	{
		glBindTexture(target, texture);
		return;
	}

	/* the renderer's binding cache: gl_nobind applies to models too (SPEC-411) */
	if (gl_nobind->value && draw_chars)
	{
		texture = draw_chars->texnum;
	}

	E.boundtex = texture;
	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	gl3state.currenttexture = texture;
}

static void GHLAPI
Emu_BlendFunc(GLenum s, GLenum d)
{
	E.bsrc = s;
	E.bdst = d;
	glBlendFunc(s, d);
}

static void GHLAPI
Emu_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	E.color[0] = r;
	E.color[1] = g;
	E.color[2] = b;
	E.color[3] = a;
}

static void GHLAPI
Emu_Color3f(GLfloat r, GLfloat g, GLfloat b)
{
	Emu_Color4f(r, g, b, 1);
}

static void GHLAPI
Emu_Color3fv(const GLfloat *v)
{
	Emu_Color4f(v[0], v[1], v[2], 1);
}

static void GHLAPI
Emu_CullFace(GLenum mode)
{
	glCullFace(mode);
}

static void GHLAPI
Emu_DepthFunc(GLenum f)
{
	glDepthFunc(f);
}

static void GHLAPI
Emu_DepthMask(GLboolean flag)
{
	E.depthmask = (flag != 0);
	glDepthMask(flag);
}

static void GHLAPI
Emu_DeleteTextures(GLsizei n, const GLuint *textures)
{
	GLsizei i;

	for (i = 0; i < n; i++)
	{
		if (textures[i] == gl3state.currenttexture)
		{
			gl3state.currenttexture = 0;
		}

		if (textures[i] == E.boundtex)
		{
			E.boundtex = 0;
		}

		SetTexSwizzle(textures[i], SWZ_RGBA);
	}

	glDeleteTextures(n, textures);
}

static void GHLAPI
Emu_FrontFace(GLenum mode)
{
	glFrontFace(mode);
}

static GLenum GHLAPI
Emu_GetError(void)
{
	return glGetError();
}

static void GHLAPI
Emu_GenTextures(GLsizei n, GLuint *textures)
{
	glGenTextures(n, textures);
}

static qboolean
GetEmulated(GLenum pname, float *out)
{
	switch (pname)
	{
		case GHL_GL_MODELVIEW_MATRIX:
			memcpy(out, E.mv[E.mvdepth], 16 * sizeof(float));
			return true;
		case GHL_GL_PROJECTION_MATRIX:
			memcpy(out, E.proj[E.projdepth], 16 * sizeof(float));
			return true;
		case GHL_GL_TEXTURE_MATRIX:
			memcpy(out, E.tex[E.texdepth], 16 * sizeof(float));
			return true;
		case GHL_GL_CURRENT_COLOR:
			memcpy(out, E.color, 4 * sizeof(float));
			return true;
		case 0x0BA0: /* GL_MATRIX_MODE */
			out[0] = (float)E.matrixmode;
			return true;
		case 0x0B54: /* GL_SHADE_MODEL */
			out[0] = (float)E.shademodel;
			return true;
		case 0x0BC1: /* GL_ALPHA_TEST_FUNC */
			out[0] = (float)E.alphafunc;
			return true;
		case 0x0BC2: /* GL_ALPHA_TEST_REF */
			out[0] = E.alpharef;
			return true;
		case 0x0D31: /* GL_MAX_LIGHTS */
			out[0] = NUM_LIGHTS;
			return true;
		case 0x0D36: /* GL_MAX_MODELVIEW_STACK_DEPTH */
			out[0] = MV_DEPTH;
			return true;
		case 0x0D38: /* GL_MAX_PROJECTION_STACK_DEPTH */
		case 0x0D39: /* GL_MAX_TEXTURE_STACK_DEPTH */
			out[0] = OTHER_DEPTH;
			return true;
		default:
			return false;
	}
}

static void GHLAPI
Emu_GetFloatv(GLenum pname, GLfloat *params)
{
	if (!GetEmulated(pname, params))
	{
		glGetFloatv(pname, params);
	}
}

static void GHLAPI
Emu_GetIntegerv(GLenum pname, GLint *params)
{
	float f[16];
	int i, n = ((pname == GHL_GL_MODELVIEW_MATRIX) || (pname == GHL_GL_PROJECTION_MATRIX) ||
		(pname == GHL_GL_TEXTURE_MATRIX)) ? 16 : ((pname == GHL_GL_CURRENT_COLOR) ? 4 : 1);

	if (!GetEmulated(pname, f))
	{
		glGetIntegerv(pname, params);
		return;
	}

	for (i = 0; i < n; i++)
	{
		params[i] = (GLint)f[i];
	}
}

static void GHLAPI
Emu_LoadIdentity(void)
{
	MatIdentity(CurrentMatrix());
	MatrixChanged();
}

static void GHLAPI
Emu_LoadMatrixf(const GLfloat *m)
{
	memcpy(CurrentMatrix(), m, 16 * sizeof(float));
	MatrixChanged();
}

static void GHLAPI
Emu_MatrixMode(GLenum mode)
{
	E.matrixmode = mode;
}

static void GHLAPI
Emu_MultMatrixf(const GLfloat *m)
{
	float *c = CurrentMatrix();

	MatMul(c, c, m);
	MatrixChanged();
}

static void GHLAPI
Emu_Normal3f(GLfloat x, GLfloat y, GLfloat z)
{
	E.normal[0] = x;
	E.normal[1] = y;
	E.normal[2] = z;
}

static void GHLAPI
Emu_Normal3fv(const GLfloat *v)
{
	Emu_Normal3f(v[0], v[1], v[2]);
}

static void GHLAPI
Emu_PixelStorei(GLenum pname, GLint param)
{
	glPixelStorei(pname, param);
}

static void GHLAPI
Emu_PolygonOffset(GLfloat factor, GLfloat units)
{
	glPolygonOffset(factor, units);
}

static void GHLAPI
Emu_PushMatrix(void)
{
	switch (E.matrixmode)
	{
		case GHL_GL_PROJECTION:
			if (E.projdepth + 1 < OTHER_DEPTH)
			{
				memcpy(E.proj[E.projdepth + 1], E.proj[E.projdepth], sizeof(E.proj[0]));
				E.projdepth++;
			}

			break;
		case GHL_GL_TEXTURE:
			if (E.texdepth + 1 < OTHER_DEPTH)
			{
				memcpy(E.tex[E.texdepth + 1], E.tex[E.texdepth], sizeof(E.tex[0]));
				E.texdepth++;
			}

			break;
		default:
			if (E.mvdepth + 1 < MV_DEPTH)
			{
				memcpy(E.mv[E.mvdepth + 1], E.mv[E.mvdepth], sizeof(E.mv[0]));
				E.mvdepth++;
			}

			break;
	}
}

static void GHLAPI
Emu_PopMatrix(void)
{
	switch (E.matrixmode)
	{
		case GHL_GL_PROJECTION:
			if (E.projdepth > 0)
			{
				E.projdepth--;
			}

			break;
		case GHL_GL_TEXTURE:
			if (E.texdepth > 0)
			{
				E.texdepth--;
			}

			break;
		default:
			if (E.mvdepth > 0)
			{
				E.mvdepth--;
			}

			E.normaldirty = true;
			break;
	}
}

static void GHLAPI
Emu_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	float r[16], l = sqrtf(x * x + y * y + z * z), c, s, t;

	if (l == 0)
	{
		return;
	}

	x /= l;
	y /= l;
	z /= l;
	c = cosf(angle * (float)M_PI / 180.0f);
	s = sinf(angle * (float)M_PI / 180.0f);
	t = 1 - c;

	/* column-major */
	r[0] = x * x * t + c;
	r[1] = y * x * t + z * s;
	r[2] = x * z * t - y * s;
	r[3] = 0;
	r[4] = x * y * t - z * s;
	r[5] = y * y * t + c;
	r[6] = y * z * t + x * s;
	r[7] = 0;
	r[8] = x * z * t + y * s;
	r[9] = y * z * t - x * s;
	r[10] = z * z * t + c;
	r[11] = 0;
	r[12] = r[13] = r[14] = 0;
	r[15] = 1;
	Emu_MultMatrixf(r);
}

static void GHLAPI
Emu_ShadeModel(GLenum mode)
{
	E.shademodel = mode;
}

static void GHLAPI
Emu_TexCoord2f(GLfloat s, GLfloat t)
{
	E.st[0] = s;
	E.st[1] = t;
}

static void GHLAPI
Emu_TexCoord2fv(const GLfloat *v)
{
	Emu_TexCoord2f(v[0], v[1]);
}

static void GHLAPI
Emu_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	if ((target == GHL_GL_TEXTURE_ENV) && (pname == GHL_GL_TEXTURE_ENV_MODE))
	{
		E.texenv = (GLenum)param;
	}
}

/* GL 1.1 formats -> GL 3 storage, and the swizzle the fragment stage applies */
static void GHLAPI
Emu_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
	GLenum format, GLenum type, const void *pixels)
{
	GLenum fmt;
	GLint ifmt;
	int channels, swz;

	(void)border;

	switch (format)
	{
		case GHL_GL_ALPHA:
		case GHL_GL_LUMINANCE:
		case GL_RED:
			channels = 1;
			fmt = GL_RED;
			ifmt = GL_R8;
			break;
		case GHL_GL_LUMINANCE_ALPHA:
		case GL_RG:
			channels = 2;
			fmt = GL_RG;
			ifmt = GL_RG8;
			break;
		case GL_RGB:
		case 0x80E0: /* GL_BGR */
			channels = 3;
			fmt = format;
			ifmt = GL_RGB8;
			break;
		default:
			channels = 4;
			fmt = format;
			ifmt = GL_RGBA8;
			break;
	}

	/* base internal format */
	switch (internalformat)
	{
		case GHL_GL_ALPHA:
		case GHL_GL_ALPHA8:
		case 0x803B: /* GL_ALPHA4 */
		case 0x803D:
		case 0x803E:
			swz = (channels == 1) ? SWZ_A_FROM_R : ((channels == 2) ? SWZ_A_FROM_G : ((channels == 4) ? SWZ_A_FROM_A : SWZ_RGB));
			break;
		case 1:
		case GHL_GL_LUMINANCE:
		case GHL_GL_LUMINANCE8:
		case 0x803F: /* GL_LUMINANCE4 */
			swz = SWZ_L;
			break;
		case 2:
		case GHL_GL_LUMINANCE_ALPHA:
		case 0x8045: /* GL_LUMINANCE8_ALPHA8 */
			swz = (channels == 2) ? SWZ_LA : ((channels == 4) ? SWZ_L_A_FROM_A : SWZ_L);
			break;
		case GHL_GL_INTENSITY:
		case GHL_GL_INTENSITY8:
			swz = SWZ_I;
			break;
		case 3:
		case GL_RGB:
		case GL_RGB8:
		case 0x8050: /* GL_RGB5 */
		case 0x804F: /* GL_RGB4 */
		case 0x2A10: /* GL_R3_G3_B2 */
			swz = (channels == 1) ? ((format == GHL_GL_ALPHA) ? SWZ_RGB : SWZ_L) : SWZ_RGB;
			break;
		default:
			if (channels == 1)
			{
				swz = (format == GHL_GL_ALPHA) ? SWZ_A_FROM_R : SWZ_L;
			}
			else if (channels == 2)
			{
				swz = SWZ_LA;
			}
			else if (channels == 3)
			{
				swz = SWZ_RGB;
			}
			else
			{
				swz = SWZ_RGBA;
			}

			break;
	}

	if ((target == GL_TEXTURE_2D) && (level == 0))
	{
		SetTexSwizzle(E.boundtex, swz);
	}

	glTexImage2D(target, level, ifmt, width, height, 0, fmt, type, pixels);
}

static void GHLAPI
Emu_TexParameteri(GLenum target, GLenum pname, GLint param)
{
	if ((pname == GL_TEXTURE_WRAP_S) || (pname == GL_TEXTURE_WRAP_T))
	{
		if (param == GHL_GL_CLAMP)
		{
			param = GL_CLAMP_TO_EDGE;
		}
	}
	else if ((target == GL_TEXTURE_2D) && (pname == GL_TEXTURE_MAG_FILTER))
	{
		param = gl_filter_max;
	}
	else if ((target == GL_TEXTURE_2D) && (pname == GL_TEXTURE_MIN_FILTER))
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
	else if (pname == 0x8191) /* GL_GENERATE_MIPMAP */
	{
		return;
	}

	glTexParameteri(target, pname, param);
}

static void GHLAPI
Emu_TexGeni(GLenum coord, GLenum pname, GLint param)
{
	if (pname != GHL_GL_TEXTURE_GEN_MODE)
	{
		return;
	}

	if (coord == GHL_GL_S)
	{
		E.genmode_s = param;
	}
	else if (coord == GHL_GL_T)
	{
		E.genmode_t = param;
	}
}

static void GHLAPI
Emu_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	Emit(x, y, z);
}

static void GHLAPI
Emu_Vertex3fv(const GLfloat *v)
{
	Emit(v[0], v[1], v[2]);
}

static void
LightModel(GLenum pname, const GLfloat *v)
{
	switch (pname)
	{
		case GHL_GL_LIGHT_MODEL_AMBIENT:
			memcpy(E.lmambient, v, 4 * sizeof(float));
			break;
		case GHL_GL_LIGHT_MODEL_LOCAL_VIEWER:
			E.localviewer = (v[0] != 0);
			break;
		case GHL_GL_LIGHT_MODEL_TWO_SIDE:
			E.twoside = (v[0] != 0);
			break;
		default:
			break;
	}
}

static void GHLAPI
Emu_LightModelf(GLenum pname, GLfloat param)
{
	float v[4] = {param, param, param, param};

	LightModel(pname, v);
}

static void GHLAPI
Emu_LightModelfv(GLenum pname, const GLfloat *params)
{
	LightModel(pname, params);
}

static void GHLAPI
Emu_LightModeli(GLenum pname, GLint param)
{
	Emu_LightModelf(pname, (float)param);
}

static void GHLAPI
Emu_LightModeliv(GLenum pname, const GLint *params)
{
	/* integer colours map the full integer range to -1..1 */
	float v[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		v[i] = (pname == GHL_GL_LIGHT_MODEL_AMBIENT) ? params[i] / 2147483647.0f : (float)params[i];
	}

	LightModel(pname, v);
}

static void
Light(GLenum light, GLenum pname, const GLfloat *v)
{
	emulight_t *l;

	if ((light < GHL_GL_LIGHT0) || (light >= GHL_GL_LIGHT0 + NUM_LIGHTS))
	{
		return;
	}

	l = &E.light[light - GHL_GL_LIGHT0];

	switch (pname)
	{
		case GHL_GL_AMBIENT:
			memcpy(l->ambient, v, 4 * sizeof(float));
			break;
		case GHL_GL_DIFFUSE:
			memcpy(l->diffuse, v, 4 * sizeof(float));
			break;
		case GHL_GL_SPECULAR:
			memcpy(l->specular, v, 4 * sizeof(float));
			break;
		case GHL_GL_POSITION:
			MatVec(E.mv[E.mvdepth], v, l->position);
			break;
		case GHL_GL_SPOT_DIRECTION:
		{
			const float *m = E.mv[E.mvdepth];
			int k;

			for (k = 0; k < 3; k++)
			{
				l->spotdir[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2];
			}

			break;
		}
		case GHL_GL_SPOT_EXPONENT:
			l->spotexp = v[0];
			break;
		case GHL_GL_SPOT_CUTOFF:
			l->spotcut = v[0];
			break;
		case GHL_GL_CONSTANT_ATTENUATION:
			l->k[0] = v[0];
			break;
		case GHL_GL_LINEAR_ATTENUATION:
			l->k[1] = v[0];
			break;
		case GHL_GL_QUADRATIC_ATTENUATION:
			l->k[2] = v[0];
			break;
		default:
			break;
	}
}

static void GHLAPI
Emu_Lightf(GLenum light, GLenum pname, GLfloat param)
{
	float v[4] = {param, param, param, param};

	Light(light, pname, v);
}

static void GHLAPI
Emu_Lightfv(GLenum light, GLenum pname, const GLfloat *params)
{
	Light(light, pname, params);
}

static void GHLAPI
Emu_Lighti(GLenum light, GLenum pname, GLint param)
{
	Emu_Lightf(light, pname, (float)param);
}

static void GHLAPI
Emu_Lightiv(GLenum light, GLenum pname, const GLint *params)
{
	float v[4];
	qboolean colour = (pname == GHL_GL_AMBIENT) || (pname == GHL_GL_DIFFUSE) || (pname == GHL_GL_SPECULAR);
	int i, n = ((pname == GHL_GL_POSITION) || colour) ? 4 : ((pname == GHL_GL_SPOT_DIRECTION) ? 3 : 1);

	for (i = 0; i < 4; i++)
	{
		v[i] = (i < n) ? (colour ? params[i] / 2147483647.0f : (float)params[i]) : 0;
	}

	Light(light, pname, v);
}

static void
Material(GLenum face, GLenum pname, const GLfloat *v)
{
	if (face == GL_BACK)
	{
		return; /* one-sided lighting: only the front material is used */
	}

	switch (pname)
	{
		case GHL_GL_AMBIENT:
			memcpy(E.mat.ambient, v, 4 * sizeof(float));
			break;
		case GHL_GL_DIFFUSE:
			memcpy(E.mat.diffuse, v, 4 * sizeof(float));
			break;
		case GHL_GL_AMBIENT_AND_DIFFUSE:
			memcpy(E.mat.ambient, v, 4 * sizeof(float));
			memcpy(E.mat.diffuse, v, 4 * sizeof(float));
			break;
		case GHL_GL_SPECULAR:
			memcpy(E.mat.specular, v, 4 * sizeof(float));
			break;
		case GHL_GL_EMISSION:
			memcpy(E.mat.emission, v, 4 * sizeof(float));
			break;
		case GHL_OM_BLOOM_SOURCE:
			E.bloomsource = v[0];
			break;
		case GHL_GL_SHININESS:
			E.mat.shininess = v[0];
			break;
		default:
			break;
	}
}

static void GHLAPI
Emu_Materialf(GLenum face, GLenum pname, GLfloat param)
{
	float v[4] = {param, param, param, param};

	Material(face, pname, v);
}

static void GHLAPI
Emu_Materialfv(GLenum face, GLenum pname, const GLfloat *params)
{
	Material(face, pname, params);
}

static void GHLAPI
Emu_Materiali(GLenum face, GLenum pname, GLint param)
{
	Emu_Materialf(face, pname, (float)param);
}

static void GHLAPI
Emu_Materialiv(GLenum face, GLenum pname, const GLint *params)
{
	float v[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		v[i] = (pname == GHL_GL_SHININESS) ? (float)params[i] : params[i] / 2147483647.0f;
	}

	Material(face, pname, v);
}

/* entry 51: state is applied as it is set */
static void GHLAPI
Emu_FlushState(void)
{
}

/* the renderer's own calls (ghlff_t) that the table does not have */
static void GHLAPI
Emu_Fogf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
		case GHL_GL_FOG_MODE:
			E.fogmode = (GLenum)param;
			break;
		case GHL_GL_FOG_DENSITY:
			E.fogdensity = param;
			break;
		case GHL_GL_FOG_START:
			E.fogstart = param;
			break;
		case GHL_GL_FOG_END:
			E.fogend = param;
			break;
		default:
			break;
	}
}

static void GHLAPI
Emu_Fogi(GLenum pname, GLint param)
{
	Emu_Fogf(pname, (float)param);
}

static void GHLAPI
Emu_Fogfv(GLenum pname, const GLfloat *params)
{
	if (pname == GHL_GL_FOG_COLOR)
	{
		memcpy(E.fogcolor, params, 4 * sizeof(float));
	}
	else
	{
		Emu_Fogf(pname, params[0]);
	}
}

static void GHLAPI
Emu_PolygonMode(GLenum face, GLenum mode)
{
#ifdef YQ2_GL3_GLES3
	(void)face;
	(void)mode;
#else
	glPolygonMode(face, mode);
#endif
}

static void GHLAPI
Emu_DepthRange(GLdouble n, GLdouble f)
{
	glDepthRange(n, f);
}

/* ------------------------------------------------------------------
 * set-up
 * ------------------------------------------------------------------ */

static void
ResetState(void)
{
	int i;

	E.mvdepth = E.projdepth = E.texdepth = 0;
	MatIdentity(E.mv[0]);
	MatIdentity(E.proj[0]);
	MatIdentity(E.tex[0]);
	E.matrixmode = GHL_GL_MODELVIEW;
	E.normaldirty = true;

	E.color[0] = E.color[1] = E.color[2] = E.color[3] = 1;
	E.normal[0] = E.normal[1] = 0;
	E.normal[2] = 1;
	E.st[0] = E.st[1] = 0;

	E.alphatest = E.fog = E.lighting = E.texture2d = E.gens = E.gent = false;
	E.normalize = E.rescale = E.colormaterial = false;
	E.alphafunc = GL_ALWAYS;
	E.alpharef = 0;
	E.shademodel = GHL_GL_SMOOTH;
	E.texenv = GHL_GL_MODULATE;
	E.genmode_s = E.genmode_t = GHL_GL_EYE_LINEAR;

	E.fogmode = 0x0800; /* GL_EXP */
	E.fogdensity = 1;
	E.fogstart = 0;
	E.fogend = 1;
	E.fogcolor[0] = E.fogcolor[1] = E.fogcolor[2] = E.fogcolor[3] = 0;

	E.lmambient[0] = E.lmambient[1] = E.lmambient[2] = 0.2f;
	E.lmambient[3] = 1;
	E.localviewer = E.twoside = false;

	for (i = 0; i < NUM_LIGHTS; i++)
	{
		emulight_t *l = &E.light[i];
		float v = (i == 0) ? 1.0f : 0.0f;

		memset(l, 0, sizeof(*l));
		l->ambient[3] = 1;
		l->diffuse[0] = l->diffuse[1] = l->diffuse[2] = v;
		l->diffuse[3] = 1;
		l->specular[0] = l->specular[1] = l->specular[2] = v;
		l->specular[3] = 1;
		l->position[2] = 1;
		l->spotdir[2] = -1;
		l->spotcut = 180;
		l->k[0] = 1;
	}

	E.mat.ambient[0] = E.mat.ambient[1] = E.mat.ambient[2] = 0.2f;
	E.mat.ambient[3] = 1;
	E.mat.diffuse[0] = E.mat.diffuse[1] = E.mat.diffuse[2] = 0.8f;
	E.mat.diffuse[3] = 1;
	memset(E.mat.specular, 0, sizeof(E.mat.specular));
	E.mat.specular[3] = 1;
	memset(E.mat.emission, 0, sizeof(E.mat.emission));
	E.mat.emission[3] = 1;
	E.mat.shininess = 0;

	E.boundtex = 0;
	E.inbegin = false;
	E.numverts = 0;
}

static void
BuildTable(void)
{
	void **t = emutable;

	t[0] = (void *)Emu_AlphaFunc;
	t[1] = (void *)Emu_Begin;
	t[2] = (void *)Emu_BindTexture;
	t[3] = (void *)Emu_BlendFunc;
	t[4] = (void *)Emu_Color3f;
	t[5] = (void *)Emu_Color4f;
	t[6] = (void *)Emu_CullFace;
	t[7] = (void *)Emu_DepthFunc;
	t[8] = (void *)Emu_DepthMask;
	t[9] = (void *)Emu_DeleteTextures;
	t[10] = (void *)Emu_Disable;
	t[11] = (void *)Emu_Enable;
	t[12] = (void *)Emu_End;
	t[13] = (void *)Emu_FrontFace;
	t[14] = (void *)Emu_GetError;
	t[15] = (void *)Emu_GenTextures;
	t[16] = (void *)Emu_GetFloatv;
	t[17] = (void *)Emu_GetIntegerv;
	t[18] = (void *)Emu_LoadIdentity;
	t[19] = (void *)Emu_LoadMatrixf;
	t[20] = (void *)Emu_MatrixMode;
	t[21] = (void *)Emu_MultMatrixf;
	t[22] = (void *)Emu_Normal3f;
	t[23] = (void *)Emu_PixelStorei;
	t[24] = (void *)Emu_PolygonOffset;
	t[25] = (void *)Emu_PopMatrix;
	t[26] = (void *)Emu_PushMatrix;
	t[27] = (void *)Emu_Rotatef;
	t[28] = (void *)Emu_ShadeModel;
	t[29] = (void *)Emu_TexCoord2f;
	t[30] = (void *)Emu_TexEnvf;
	t[31] = (void *)Emu_TexImage2D;
	t[32] = (void *)Emu_TexParameteri;
	t[33] = (void *)Emu_TexGeni;
	t[34] = (void *)Emu_Vertex3f;
	t[35] = (void *)Emu_Color3fv;
	t[36] = (void *)Emu_Normal3fv;
	t[37] = (void *)Emu_TexCoord2fv;
	t[38] = (void *)Emu_Vertex3fv;
	t[39] = (void *)Emu_LightModelf;
	t[40] = (void *)Emu_LightModelfv;
	t[41] = (void *)Emu_LightModeli;
	t[42] = (void *)Emu_LightModeliv;
	t[43] = (void *)Emu_Lightf;
	t[44] = (void *)Emu_Lightfv;
	t[45] = (void *)Emu_Lighti;
	t[46] = (void *)Emu_Lightiv;
	t[47] = (void *)Emu_Materialf;
	t[48] = (void *)Emu_Materialfv;
	t[49] = (void *)Emu_Materiali;
	t[50] = (void *)Emu_Materialiv;
	t[51] = (void *)Emu_FlushState;

	emuff.Enable = Emu_Enable;
	emuff.Disable = Emu_Disable;
	emuff.IsEnabled = Emu_IsEnabled;
	emuff.MatrixMode = Emu_MatrixMode;
	emuff.LoadMatrixf = Emu_LoadMatrixf;
	emuff.LoadIdentity = Emu_LoadIdentity;
	emuff.Color4f = Emu_Color4f;
	emuff.ShadeModel = Emu_ShadeModel;
	emuff.AlphaFunc = Emu_AlphaFunc;
	emuff.TexEnvf = Emu_TexEnvf;
	emuff.LightModeli = Emu_LightModeli;
	emuff.Fogf = Emu_Fogf;
	emuff.Fogfv = Emu_Fogfv;
	emuff.Fogi = Emu_Fogi;
	emuff.PolygonMode = Emu_PolygonMode;
	emuff.DepthRange = Emu_DepthRange;
	emuff.CullFace = Emu_CullFace;
	emuff.BlendFunc = Emu_BlendFunc;
	emuff.DepthMask = Emu_DepthMask;
	emuff.GetIntegerv = Emu_GetIntegerv;
}

static void
Ready(void)
{
	if (!E.ready)
	{
		BuildTable();
		ResetState();
		E.ready = true;
	}
}

qboolean
GL3_GhoulEmu_FF(ghlff_t *out)
{
	Ready();
	*out = emuff;
	return true;
}

void *
GL3_GhoulEmu_Table(void)
{
	Ready();
	return emutable;
}

/* around a Ghoul draw: the renderer's pending batches go out first */
void
GL3_GhoulEmu_Begin(void)
{
	Ready();

	/* a renderer restart without GL3_GhoulEmu_Shutdown left names of the old context behind */
	if (E.prog && !glIsProgram(E.prog))
	{
		E.prog = E.vao = E.vbo = 0;
	}

	GL3_SoF2D_Flush();
	GL3_FX_Flush();
	GL3_Draw3DBatchesNow();
	E.boundtex = 0;

	/* the blending the caller leaves is what OpenGL has now */
	E.blendon = glIsEnabled(GL_BLEND);
	{
		GLint s = GL_SRC_ALPHA, d = GL_ONE_MINUS_SRC_ALPHA;

		glGetIntegerv(GL_BLEND_SRC_RGB, &s);
		glGetIntegerv(GL_BLEND_DST_RGB, &d);
		E.bsrc = (GLenum)s;
		E.bdst = (GLenum)d;
	}
	E.bloomsource = 0.0f;
	{
		GLboolean dm = GL_TRUE;

		glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
		E.depthmask = (dm != 0);
	}
	E.csmcapture = E.csmrepl = false;
}

/* GFX-modern: whether the entity being drawn casts a sun shadow and lets its ground shadow give way to it */
void
GL3_GhoulEmu_SetCsm(qboolean capture, qboolean replace)
{
	E.csmcapture = capture;
	E.csmrepl = replace;
}

/* GFX-modern: bloom weight of the whole entity being drawn (RF_FULLBRIGHT / RF_GLOW), 0 after it */
void
GL3_GhoulEmu_SetEntityGlow(float glow)
{
	E.entityglow = glow;
}

void
GL3_GhoulEmu_End(void)
{
	E.inbegin = false;
	E.numverts = 0;
}

/* the GL objects die with the context (renderer shutdown); the state is kept like GL's would not be */
void
GL3_GhoulEmu_Shutdown(void)
{
	if (E.prog)
	{
		glDeleteProgram(E.prog);
	}

	if (E.vbo)
	{
		glDeleteBuffers(1, &E.vbo);
	}

	if (E.vao)
	{
		glDeleteVertexArrays(1, &E.vao);
	}

	if (gl3state.currentShaderProgram == E.prog)
	{
		gl3state.currentShaderProgram = 0;
	}

	if (gl3state.currentVAO == E.vao)
	{
		gl3state.currentVAO = 0;
	}

	if (gl3state.currentVBO == E.vbo)
	{
		gl3state.currentVBO = 0;
	}

	E.prog = E.vao = E.vbo = 0;
	free(E.texswz);
	E.texswz = NULL;
	E.numtexswz = 0;
	E.ready = false;
}
