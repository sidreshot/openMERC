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
	/* GFX-modern tessellation (r_om_tess_weapon): eye-space position and unit normal, not uploaded */
	float eye[3];
	float en[3];
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

/* what the vertex lighting needs of one enabled light, worked out once per glBegin (lights cannot change inside a
   primitive; materials and colours can): the per-vertex loop no longer takes square roots, a cosine and the spot
   axis length of every light at every vertex */
typedef struct
{
	const emulight_t *l;
	float pos[3];        /* positional: the eye-space position; directional: the unit direction */
	float spotdir[3];    /* unit spot axis */
	float spotcos;       /* cosine of the cut-off */
	qboolean positional, spot, specular;
} emulightc_t;

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
	qboolean lcdirty;   /* E.lc is worked out again before the next lit vertex */
	emulight_t light[NUM_LIGHTS];
	emulightc_t lc[NUM_LIGHTS];
	int numlc;
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
	GLint u_tex, u_texenv, u_texswz, u_alphafunc, u_alpharef, u_fog, u_fogcolor, u_a2c;

	/* GFX-modern bloom (gl3_post.c): blending as set, the Ghoul system's source weight of the next
	   primitive and the glow of the entity being drawn */
	qboolean blendon;
	GLenum bsrc, bdst;
	float bloomsource, entityglow;
	GLint u_emis;

	/* GFX-modern sun shadows (gl3_csm.c): the depth mask as set, whether the entity casts and whether its
	   ground shadow gives way to the sun shadow */
	qboolean depthmask, csmcapture, csmrepl;
	GLint u_invpv, u_csmrepl, u_beamn, u_beam, u_ppl, u_pplL, u_pplM;

	/* GFX-modern light beams (gl3_post.c): whether the entity being drawn casts a shadow into beams */
	qboolean casts;

	/* GFX-modern tessellation: subdivision level of the entity being drawn (curved PN triangles), < 2 off;
	   the depth function as set, to recognise a second pass over the same triangles */
	int tess;
	float tessk;    /* how far toward the PN surface (r_om_tess / 10) */
	qboolean ppl;   /* per-pixel lighting of the entity being drawn (r_om_ppl_models) */
	/* GFX-modern r_om_ambcube: the ambient factors of the entity along +x -x +y -y +z -z (world), on; the eye to
	   world rotation of the view (column-major 3x3) */
	float amb[18];
	qboolean ambon;
	float e2w[9];
	GLint u_ambon, u_amb, u_e2w;
	GLenum depthfunc;
	emuvtx_t *tessbuf;
	int maxtess;
	/* the same on the GPU: the corners in a texture buffer, a vertex array without attributes */
	GLuint tessvao, tbo, tbotex;
	GLint u_tessL, u_tessK, u_tessProj, u_tessTab;
	int tesstabL;
	int context; /* gl3_contextgen the GL objects were made in */
} E;

#define EMU_TBO_UNIT 20 /* GL_TEXTURE20: the texture buffer of the tessellated triangles */

static ghlff_t emuff;
static cvar_t *r_om_tessdebug; /* print what the weapon tessellation does (test aid) */
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

/* GFX-modern r_om_ambcube: the ambient factor for an eye-space normal (squared world components weigh the axes) */
static void
AmbFactor(const float *n, float *af)
{
	float w[3];
	int i, k;

	for (i = 0; i < 3; i++)
	{
		w[i] = E.e2w[i] * n[0] + E.e2w[3 + i] * n[1] + E.e2w[6 + i] * n[2];
	}

	for (k = 0; k < 3; k++)
	{
		af[k] = 0;

		for (i = 0; i < 3; i++)
		{
			af[k] += w[i] * w[i] * E.amb[(i * 2 + (w[i] < 0)) * 3 + k];
		}
	}
}

static void
LightCache(void)
{
	int i, k;

	E.numlc = 0;
	E.lcdirty = false;

	for (i = 0; i < NUM_LIGHTS; i++)
	{
		const emulight_t *l = &E.light[i];
		emulightc_t *lc;

		if (!l->on)
		{
			continue;
		}

		lc = &E.lc[E.numlc++];
		lc->l = l;
		lc->positional = (l->position[3] != 0);

		if (lc->positional)
		{
			for (k = 0; k < 3; k++)
			{
				lc->pos[k] = l->position[k] / l->position[3];
			}
		}
		else
		{
			float len = sqrtf(l->position[0] * l->position[0] + l->position[1] * l->position[1] +
				l->position[2] * l->position[2]);

			for (k = 0; k < 3; k++)
			{
				lc->pos[k] = (len > 0) ? l->position[k] / len : 0;
			}
		}

		lc->spot = (l->spotcut != 180);

		if (lc->spot)
		{
			float sl = sqrtf(l->spotdir[0] * l->spotdir[0] + l->spotdir[1] * l->spotdir[1] +
				l->spotdir[2] * l->spotdir[2]);

			for (k = 0; k < 3; k++)
			{
				lc->spotdir[k] = (sl > 0) ? l->spotdir[k] / sl : 0;
			}

			lc->spotcos = cosf(l->spotcut * (float)M_PI / 180.0f);
		}

		lc->specular = (l->specular[0] != 0) || (l->specular[1] != 0) || (l->specular[2] != 0);
	}
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

	{
		float af[3] = {1, 1, 1};

		if (E.ambon)
		{
			AmbFactor(n, af);
		}

		for (k = 0; k < 3; k++)
		{
			c[k] = E.mat.emission[k] + ma[k] * E.lmambient[k] * af[k];
		}
	}

	if (E.lcdirty)
	{
		LightCache();
	}

	for (i = 0; i < E.numlc; i++)
	{
		const emulightc_t *lc = &E.lc[i];
		const emulight_t *l = lc->l;
		float L[3], d = 0, att = 1, spot = 1, ndotl, h[3], hl, spec = 0;

		if (lc->positional)
		{
			for (k = 0; k < 3; k++)
			{
				L[k] = lc->pos[k] - eye[k];
			}

			d = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);

			if (d > 0)
			{
				float id = 1.0f / d;

				L[0] *= id;
				L[1] *= id;
				L[2] *= id;
			}

			att = l->k[0] + l->k[1] * d + l->k[2] * d * d;
			att = (att > 0) ? 1.0f / att : 1.0f;
		}
		else
		{
			L[0] = lc->pos[0];
			L[1] = lc->pos[1];
			L[2] = lc->pos[2];
		}

		if (lc->spot)
		{
			float cs = -(L[0] * lc->spotdir[0] + L[1] * lc->spotdir[1] + L[2] * lc->spotdir[2]);

			spot = (cs < lc->spotcos) ? 0 : powf(cs > 0 ? cs : 0, l->spotexp);

			if (spot == 0)
			{
				continue; /* every term of this light is scaled by spot */
			}
		}

		ndotl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];

		if (ndotl < 0)
		{
			ndotl = 0;
		}

		/* the half vector and its power only where they can add something */
		if ((ndotl > 0) && lc->specular && (E.mat.specular[0] != 0 || E.mat.specular[1] != 0 || E.mat.specular[2] != 0))
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

	if (E.lighting && E.ppl)
	{
		/* lit per fragment: the vertex carries the colour-material colour only */
		int k;

		for (k = 0; k < 4; k++)
		{
			v->rgba[k] = Clamp01(E.color[k]);
		}
	}
	else if (E.lighting)
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

	if ((E.tess > 1) || (E.lighting && E.ppl))
	{
		float n[3], l, w = (eye[3] != 0) ? eye[3] : 1;

		EyeNormal(E.normal, n);
		l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		v->eye[0] = eye[0] / w;
		v->eye[1] = eye[1] / w;
		v->eye[2] = eye[2] / w;
		v->en[0] = (l > 0) ? n[0] / l : 0;
		v->en[1] = (l > 0) ? n[1] / l : 0;
		v->en[2] = (l > 0) ? n[2] / l : 0;
	}
}

/* ------------------------------------------------------------------
 * shader and drawing
 * ------------------------------------------------------------------ */

static const char *emu_vs =
#ifndef YQ2_GL3_GLES3
	"#define OM_TESS 1\n"
#endif
	"in vec4 position;\n"
	"in vec2 texCoord;\n"
	"in vec4 vertColor;\n"
	"in float fogFactor;\n"
	"in vec3 eyeNormal;\n"   /* GFX-modern per-pixel lighting: eye-space normal and position */
	"in vec3 eyePos;\n"
	"out vec4 vColor;\n"
	"out vec2 vTex;\n"
	"out float vFog;\n"
	"out vec4 vPos;\n"
	"out vec3 vEN;\n"
	"out vec3 vEP;\n"
	/* GFX-modern tessellation on the GPU: with omTessL > 1 the draw has no attributes; vertex k of the
	   triangle list is corner omTessTab[k % (3 L^2)] of a sub-triangle of original triangle k / (3 L^2), whose
	   three corners (emuvtx_t, 17 floats each) are read from omTessBuf; the point on the curved PN triangle
	   (Vlachos et al.) of their eye-space positions and normals, omTessK of the way from the flat one */
	"#ifdef OM_TESS\n"
	"uniform int omTessL;\n"
	"uniform float omTessK;\n"
	"uniform mat4 omTessProj;\n"
	"uniform ivec2 omTessTab[48];\n"
	"uniform samplerBuffer omTessBuf;\n"
	"float omTF(int i) { return texelFetch(omTessBuf, i).r; }\n"
	"vec3 omT3(int i) { return vec3(omTF(i), omTF(i + 1), omTF(i + 2)); }\n"
	"vec3 omPNc(vec3 Pi, vec3 Pj, vec3 Ni) { return (2.0 * Pi + Pj - dot(Pj - Pi, Ni) * Ni) / 3.0; }\n"
	"#endif\n"
	"void main()\n"
	"{\n"
	"#ifdef OM_TESS\n"
	"	if (omTessL > 1)\n"
	"	{\n"
	"		int per = omTessL * omTessL * 3, t = gl_VertexID / per;\n"
	"		ivec2 ij = omTessTab[gl_VertexID - t * per];\n"
	"		float u = float(ij.x) / float(omTessL), v = float(ij.y) / float(omTessL), w = 1.0 - u - v;\n"
	"		int a = t * 51, b = a + 17, c = b + 17;\n"
	"		vec3 P1 = omT3(a + 11), P2 = omT3(b + 11), P3 = omT3(c + 11);\n"
	"		vec3 N1 = omT3(a + 14), N2 = omT3(b + 14), N3 = omT3(c + 14);\n"
	"		vec3 b210 = omPNc(P1, P2, N1), b120 = omPNc(P2, P1, N2), b021 = omPNc(P2, P3, N2);\n"
	"		vec3 b012 = omPNc(P3, P2, N3), b102 = omPNc(P3, P1, N3), b201 = omPNc(P1, P3, N1);\n"
	"		vec3 Ev = (b210 + b120 + b021 + b012 + b102 + b201) / 6.0, V = (P1 + P2 + P3) / 3.0;\n"
	"		vec3 b111 = Ev + (Ev - V) / 2.0;\n"
	"		vec3 lin = P1 * u + P2 * v + P3 * w;\n"
	"		vec3 pn = P1 * u * u * u + P2 * v * v * v + P3 * w * w * w + 3.0 * (b210 * u * u * v + b120 * u * v * v +\n"
	"			b201 * u * u * w + b021 * v * v * w + b102 * u * w * w + b012 * v * w * w) + 6.0 * b111 * u * v * w;\n"
	"		vec3 p = lin + omTessK * (pn - lin);\n"
	"		vColor = vec4(omTF(a + 4), omTF(a + 5), omTF(a + 6), omTF(a + 7)) * u +\n"
	"			vec4(omTF(b + 4), omTF(b + 5), omTF(b + 6), omTF(b + 7)) * v +\n"
	"			vec4(omTF(c + 4), omTF(c + 5), omTF(c + 6), omTF(c + 7)) * w;\n"
	"		vTex = vec2(omTF(a + 8), omTF(a + 9)) * u + vec2(omTF(b + 8), omTF(b + 9)) * v + vec2(omTF(c + 8), omTF(c + 9)) * w;\n"
	"		vFog = omTF(a + 10) * u + omTF(b + 10) * v + omTF(c + 10) * w;\n"
	"		vec3 n = N1 * u + N2 * v + N3 * w;\n"
	"		vEN = dot(n, n) > 0.0 ? normalize(n) : n;\n"
	"		vEP = p;\n"
	"		gl_Position = omTessProj * vec4(p, 1.0);\n"
	"		vPos = gl_Position;\n"
	"		return;\n"
	"	}\n"
	"#endif\n"
	"	vPos = position;\n"
	"	vColor = vertColor;\n"
	"	vTex = texCoord;\n"
	"	vFog = fogFactor;\n"
	"	vEN = eyeNormal;\n"
	"	vEP = eyePos;\n"
	"	gl_Position = position;\n"
	"}\n";

static const char *emu_fs =
	"uniform sampler2D tex;\n"
	"uniform int texenv;\n"   /* 0 none, 1 modulate, 2 replace, 3 decal, 4 blend, 5 add */
	"uniform int texswz;\n"
	"uniform int alphafunc;\n" /* 0 never ... 7 always */
	"uniform float alpharef;\n"
	"uniform int omA2C;\n"
	"uniform int fogon;\n"
	"uniform vec4 fogcolor;\n"
	"uniform vec4 omEmis;\n"   /* GFX-modern: (x, y, z, mode) as in the effect shader */
	"in vec4 vColor;\n"
	"in vec2 vTex;\n"
	"in float vFog;\n"
	"in vec4 vPos;\n"
	"uniform mat4 omInvPV;\n"
	"uniform int omCsmRepl;\n"
	/* GFX-modern per-pixel lighting (r_om_ppl_models): the GL 1.1 lighting of Lighting() in gl3_ghoulemu.c
	   per fragment, on the interpolated, renormalised normal. Per light 6 vec4: eye position; ambient + on;
	   diffuse; specular; spot direction + cos(cutoff) (-2 = no spot); attenuation k0 k1 k2 + spot exponent.
	   Material: ambient, diffuse, specular, emission, (shininess, colour material, local viewer), model ambient */
	"in vec3 vEN;\n"
	"in vec3 vEP;\n"
	"uniform int omPPL;\n"
	"uniform vec4 omL[48];\n"
	"uniform vec4 omM[6];\n"
	/* r_om_ambcube: the ambient factors along +x -x +y -y +z -z of the world and the eye to world rotation */
	"uniform int omAmbOn;\n"
	"uniform vec3 omAmb[6];\n"
	"uniform mat3 omE2W;\n"
	"vec3 omAmbK(vec3 n)\n"
	"{\n"
	"	if (omAmbOn == 0) return vec3(1.0);\n"
	"	vec3 w = omE2W * n;\n"
	"	vec3 q = w * w;\n"
	"	return q.x * (w.x >= 0.0 ? omAmb[0] : omAmb[1]) + q.y * (w.y >= 0.0 ? omAmb[2] : omAmb[3]) +\n"
	"		q.z * (w.z >= 0.0 ? omAmb[4] : omAmb[5]);\n"
	"}\n"
	"vec4 omLit(vec4 vc)\n"
	"{\n"
	"	vec3 n = dot(vEN, vEN) > 1e-12 ? normalize(vEN) : vec3(0.0, 0.0, 1.0);\n"
	"	bool cm = omM[4].y > 0.5;\n"
	"	vec4 ma = cm ? vc : omM[0];\n"
	"	vec4 md = cm ? vc : omM[1];\n"
	"	vec3 c = omM[3].rgb + ma.rgb * omM[5].rgb * omAmbK(n);\n"
	"	for (int i = 0; i < 8; i++)\n"
	"	{\n"
	"		if (omL[i * 6 + 1].w < 0.5) continue;\n"
	"		vec4 P = omL[i * 6];\n"
	"		vec3 L;\n"
	"		float att = 1.0, spot = 1.0, spec = 0.0;\n"
	"		if (P.w != 0.0)\n"
	"		{\n"
	"			L = P.xyz / P.w - vEP;\n"
	"			float d = length(L);\n"
	"			if (d > 0.0) L /= d;\n"
	"			vec3 k = omL[i * 6 + 5].xyz;\n"
	"			float a = k.x + k.y * d + k.z * d * d;\n"
	"			att = (a > 0.0) ? 1.0 / a : 1.0;\n"
	"		}\n"
	"		else L = normalize(P.xyz);\n"
	"		vec4 sd = omL[i * 6 + 4];\n"
	"		if (sd.w > -1.5)\n"
	"		{\n"
	"			float sl = length(sd.xyz);\n"
	"			float cs = (sl > 0.0) ? -dot(L, sd.xyz) / sl : 0.0;\n"
	"			spot = (cs < sd.w) ? 0.0 : pow(max(cs, 0.0), omL[i * 6 + 5].w);\n"
	"		}\n"
	"		float ndl = max(dot(n, L), 0.0);\n"
	"		if (ndl > 0.0)\n"
	"		{\n"
	"			vec3 h = (omM[4].z > 0.5) ? L - normalize(vEP) : L + vec3(0.0, 0.0, 1.0);\n"
	"			float hl = length(h);\n"
	"			if (hl > 0.0)\n"
	"			{\n"
	"				float nh = dot(n, h) / hl;\n"
	"				spec = (nh > 0.0) ? ((omM[4].x > 0.0) ? pow(nh, omM[4].x) : 1.0) : 0.0;\n"
	"			}\n"
	"		}\n"
	"		c += att * spot * (omL[i * 6 + 1].rgb * ma.rgb + ndl * omL[i * 6 + 2].rgb * md.rgb + spec * omL[i * 6 + 3].rgb * omM[2].rgb);\n"
	"	}\n"
	"	return vec4(clamp(c, 0.0, 1.0), clamp(md.a, 0.0, 1.0));\n"
	"}\n"
	/* GFX-modern light beams (gl3_post.c): the lamp pools of the last view; a ground shadow in one gives
	   way to the lamp's own shadow of the character */
	"uniform int omBeamN;\n"
	"uniform vec4 omBeam[16];\n"
	"float omBeamKeep(vec3 wp)\n"
	"{\n"
	"	float pool = 0.0;\n"
	"	for (int i = 0; i < 4; i++)\n"
	"	{\n"
	"		if (i >= omBeamN) break;\n"
	"		vec3 q = wp - omBeam[i * 4].xyz;\n"
	"		vec3 ax = omBeam[i * 4 + 1].xyz;\n"
	"		float h = dot(q, ax), hn = h / omBeam[i * 4].w;\n"
	"		if (hn < omBeam[i * 4 + 3].x || hn > 1.3) continue;\n"
	"		float R = max(mix(omBeam[i * 4 + 1].w, omBeam[i * 4 + 2].w, hn), 0.5);\n"
	"		pool = max(pool, (1.0 - smoothstep(0.6, 1.0, length(q - h * ax) / R)) * smoothstep(omBeam[i * 4 + 3].x, omBeam[i * 4 + 3].x + 0.1, hn));\n"
	"	}\n"
	"	return 1.0 - 0.85 * pool;\n"
	"}\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec4 c = (omPPL != 0) ? omLit(vColor) : vColor;\n"
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
	"	if (omCsmRepl != 0 || omBeamN > 0)\n"
	"	{\n"
	"		vec4 w = omInvPV * vPos;\n"
	"		if (omCsmRepl != 0) c.a *= omCsmGhoulShadowKeep(w.xyz / w.w);\n"
	"		if (omBeamN > 0) c.a *= omBeamKeep(w.xyz / w.w);\n"
	"	}\n"
	"	bool pass = true;\n"
	"	if (alphafunc == 0) pass = false;\n"
	"	else if (alphafunc == 1) pass = c.a < alpharef;\n"
	"	else if (alphafunc == 2) pass = c.a == alpharef;\n"
	"	else if (alphafunc == 3) pass = c.a <= alpharef;\n"
	"	else if (alphafunc == 4) pass = c.a > alpharef;\n"
	"	else if (alphafunc == 5) pass = c.a != alpharef;\n"
	"	else if (alphafunc == 6) pass = c.a >= alpharef;\n"
	"	float omW = fwidth(c.a);\n"
	"	if (omA2C != 0)\n" /* r_om_a2c: greater / greater-or-equal widened to about a pixel, as coverage */
	"	{\n"
	"		pass = true;\n"
	"		c.a = clamp((c.a - alpharef) / max(omW, 0.0001) + 0.5, 0.0, 1.0);\n"
	"		if (c.a <= 0.0) discard;\n"
	"	}\n"
	"	if (!pass) discard;\n"
	"	outColor = c;\n"
	"	if (omEmis.w > 1.5) outEmissive = vec4(omEmis.xyz, c.a);\n"
	"	else if (omEmis.w > 0.5) outEmissive = vec4(omPre * omEmis.x, c.a);\n"
	"}\n";

/* the per-primitive uniforms of E.prog are sent only when they change: every glBegin/glEnd primitive of a Ghoul
   model is a draw of its own and set ~20 uniforms, among them the 8 lights x 6 vec4 of the per-pixel lighting,
   mostly the same values as for the previous primitive of the same model */
typedef struct
{
	unsigned gen;
	int bytes, bigcap;
	byte small[64];
	byte *big;
} emuucache_t;

static emuucache_t ucache[256];
static unsigned ucgen = 1;
static int uccontext = -1;

static qboolean
USame(GLint loc, const void *p, int bytes)
{
	emuucache_t *e;
	byte *buf;

	if (loc < 0)
	{
		return true; /* not in the program: glUniform would do nothing */
	}

	if (uccontext != gl3_contextgen)
	{
		uccontext = gl3_contextgen;
		ucgen++;
	}

	if (loc >= (GLint)(sizeof(ucache) / sizeof(ucache[0])))
	{
		return false;
	}

	e = &ucache[loc];

	if (bytes <= (int)sizeof(e->small))
	{
		buf = e->small;
	}
	else
	{
		if (e->bigcap < bytes)
		{
			byte *b = realloc(e->big, bytes);

			if (!b)
			{
				e->gen = 0;
				return false;
			}

			e->big = b;
			e->bigcap = bytes;
		}

		buf = e->big;
	}

	if ((e->gen == ucgen) && (e->bytes == bytes) && !memcmp(buf, p, bytes))
	{
		return true;
	}

	memcpy(buf, p, bytes);
	e->gen = ucgen;
	e->bytes = bytes;
	return false;
}

static void U1i(GLint l, int v) { if (!USame(l, &v, sizeof(v))) glUniform1i(l, v); }
static void U1f(GLint l, float v) { if (!USame(l, &v, sizeof(v))) glUniform1f(l, v); }
static void U4f(GLint l, float a, float b, float c, float d)
{
	float v[4] = {a, b, c, d};

	if (!USame(l, v, sizeof(v)))
	{
		glUniform4fv(l, 1, v);
	}
}
static void U3fv(GLint l, int n, const float *v) { if (!USame(l, v, n * 3 * (int)sizeof(float))) glUniform3fv(l, n, v); }
static void U4fv(GLint l, int n, const float *v) { if (!USame(l, v, n * 4 * (int)sizeof(float))) glUniform4fv(l, n, v); }
static void UM3(GLint l, const float *v) { if (!USame(l, v, 9 * (int)sizeof(float))) glUniformMatrix3fv(l, 1, GL_FALSE, v); }
static void UM4(GLint l, const float *v) { if (!USame(l, v, 16 * (int)sizeof(float))) glUniformMatrix4fv(l, 1, GL_FALSE, v); }

static GLuint
Compile(GLenum type, const char *src)
{
#ifdef YQ2_GL3_GLES3
	const char *version = GL3_GLES_HEADER;
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
	ucgen++; /* a new program: no uniform value is known */
	glAttachShader(E.prog, vs);
	glAttachShader(E.prog, fs);
	glBindAttribLocation(E.prog, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(E.prog, GL3_ATTRIB_TEXCOORD, "texCoord");
	glBindAttribLocation(E.prog, GL3_ATTRIB_LMTEXCOORD, "fogFactor");
	glBindAttribLocation(E.prog, GL3_ATTRIB_COLOR, "vertColor");
	glBindAttribLocation(E.prog, GL3_ATTRIB_NORMAL, "eyeNormal");
	glBindAttribLocation(E.prog, GL3_ATTRIB_LIGHTFLAGS, "eyePos");
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
	E.u_a2c = glGetUniformLocation(E.prog, "omA2C");
	E.u_fog = glGetUniformLocation(E.prog, "fogon");
	E.u_fogcolor = glGetUniformLocation(E.prog, "fogcolor");
	E.u_emis = glGetUniformLocation(E.prog, "omEmis");
	E.u_invpv = glGetUniformLocation(E.prog, "omInvPV");
	E.u_csmrepl = glGetUniformLocation(E.prog, "omCsmRepl");
	E.u_ppl = glGetUniformLocation(E.prog, "omPPL");
	E.u_pplL = glGetUniformLocation(E.prog, "omL");
	E.u_pplM = glGetUniformLocation(E.prog, "omM");
	E.u_ambon = glGetUniformLocation(E.prog, "omAmbOn");
	E.u_amb = glGetUniformLocation(E.prog, "omAmb");
	E.u_e2w = glGetUniformLocation(E.prog, "omE2W");
	E.u_beamn = glGetUniformLocation(E.prog, "omBeamN");
	E.u_beam = glGetUniformLocation(E.prog, "omBeam");
	E.u_tessL = glGetUniformLocation(E.prog, "omTessL");
	E.u_tessK = glGetUniformLocation(E.prog, "omTessK");
	E.u_tessProj = glGetUniformLocation(E.prog, "omTessProj");
	E.u_tessTab = glGetUniformLocation(E.prog, "omTessTab");
	E.tesstabL = 0;
	E.context = gl3_contextgen;
	GL3_UseProgram(E.prog);
	GL3_CSM_BindProgram(E.prog);
#ifndef YQ2_GL3_GLES3
	if (E.u_tessL != -1)
	{
		glUniform1i(glGetUniformLocation(E.prog, "omTessBuf"), EMU_TBO_UNIT);
		glGenVertexArrays(1, &E.tessvao);
		glGenBuffers(1, &E.tbo);
		glBindBuffer(GL_TEXTURE_BUFFER, E.tbo);
		glBufferData(GL_TEXTURE_BUFFER, 4096, NULL, GL_STREAM_DRAW);
		glGenTextures(1, &E.tbotex);
		GL3_SelectTMU(GL_TEXTURE0 + EMU_TBO_UNIT);
		glBindTexture(GL_TEXTURE_BUFFER, E.tbotex);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, E.tbo);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);
		GL3_SelectTMU(GL_TEXTURE0);
	}
#endif

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
	glEnableVertexAttribArray(GL3_ATTRIB_NORMAL);
	qglVertexAttribPointer(GL3_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(emuvtx_t), offsetof(emuvtx_t, en));
	glEnableVertexAttribArray(GL3_ATTRIB_LIGHTFLAGS);
	qglVertexAttribPointer(GL3_ATTRIB_LIGHTFLAGS, 3, GL_FLOAT, GL_FALSE, sizeof(emuvtx_t), offsetof(emuvtx_t, eye));
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

/*
 * GFX-modern tessellation: each triangle becomes tess^2 triangles on the curved PN triangle (Vlachos et al.)
 * of its eye-space corners and normals; colour, texture coordinates and fog are interpolated linearly
 * (the lighting stays per original vertex). An edge whose end normals stand perpendicular to it - a hard
 * edge with split normals, a flat face - stays straight, so neighbours keep meeting without cracks.
 */
static emuvtx_t *
Tessellate(emuvtx_t *in, int *n)
{
	int L = E.tess, per = L * L * 3, t, nt = *n / 3, o = 0;

	if (E.maxtess < nt * per)
	{
		emuvtx_t *p = realloc(E.tessbuf, nt * per * sizeof(*p));

		if (!p)
		{
			return in;
		}

		E.tessbuf = p;
		E.maxtess = nt * per;
	}

	for (t = 0; t < nt; t++)
	{
		const emuvtx_t *a = &in[t * 3], *b = &in[t * 3 + 1], *c = &in[t * 3 + 2];
		const float *P1 = a->eye, *P2 = b->eye, *P3 = c->eye;
		const float *N1 = a->en, *N2 = b->en, *N3 = c->en;
		float cp[10][3], Ev[3], V[3];
		int i, j, k;

#define PN_W(Pi, Pj, Ni) (((Pj)[0] - (Pi)[0]) * (Ni)[0] + ((Pj)[1] - (Pi)[1]) * (Ni)[1] + ((Pj)[2] - (Pi)[2]) * (Ni)[2])
		/* control points: 0 b210, 1 b120, 2 b021, 3 b012, 4 b102, 5 b201, 6 b111 */
		for (k = 0; k < 3; k++)
		{
			cp[0][k] = (2 * P1[k] + P2[k] - PN_W(P1, P2, N1) * N1[k]) / 3;
			cp[1][k] = (2 * P2[k] + P1[k] - PN_W(P2, P1, N2) * N2[k]) / 3;
			cp[2][k] = (2 * P2[k] + P3[k] - PN_W(P2, P3, N2) * N2[k]) / 3;
			cp[3][k] = (2 * P3[k] + P2[k] - PN_W(P3, P2, N3) * N3[k]) / 3;
			cp[4][k] = (2 * P3[k] + P1[k] - PN_W(P3, P1, N3) * N3[k]) / 3;
			cp[5][k] = (2 * P1[k] + P3[k] - PN_W(P1, P3, N1) * N1[k]) / 3;
			Ev[k] = (cp[0][k] + cp[1][k] + cp[2][k] + cp[3][k] + cp[4][k] + cp[5][k]) / 6;
			V[k] = (P1[k] + P2[k] + P3[k]) / 3;
			cp[6][k] = Ev[k] + (Ev[k] - V[k]) / 2;
		}
#undef PN_W

		for (i = 0; i < L; i++)
		{
			for (j = 0; i + j < L; j++)
			{
				/* (i, j): u = i / L weights corner 1, v = j / L corner 2; same winding as the original */
				int tri[2][3][2] = {{{i, j}, {i + 1, j}, {i, j + 1}}, {{i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
				int q, nq = (i + j < L - 1) ? 2 : 1;

				for (q = 0; q < nq; q++)
				{
					int m;

					for (m = 0; m < 3; m++)
					{
						emuvtx_t *v = &E.tessbuf[o++];
						float u = (float)tri[q][m][0] / L, w2 = (float)tri[q][m][1] / L, w = 1.0f - u - w2;
						float p[4], *proj = E.proj[E.projdepth];
						int r;

						for (r = 0; r < 3; r++)
						{
							float lin = P1[r] * u + P2[r] * w2 + P3[r] * w;
							float pn = P1[r] * u * u * u + P2[r] * w2 * w2 * w2 + P3[r] * w * w * w +
								3 * (cp[0][r] * u * u * w2 + cp[1][r] * u * w2 * w2 + cp[5][r] * u * u * w +
								cp[2][r] * w2 * w2 * w + cp[4][r] * u * w * w + cp[3][r] * w2 * w * w) +
								6 * cp[6][r] * u * w2 * w;

							/* part of the way from the flat triangle to the PN surface: rounder silhouettes
							   with less of the PN "inflation" */
							p[r] = lin + E.tessk * (pn - lin);
						}

						p[3] = 1;
						MatVec(proj, p, v->pos);

						for (r = 0; r < 4; r++)
						{
							v->rgba[r] = a->rgba[r] * u + b->rgba[r] * w2 + c->rgba[r] * w;
						}

						v->st[0] = a->st[0] * u + b->st[0] * w2 + c->st[0] * w;
						v->st[1] = a->st[1] * u + b->st[1] * w2 + c->st[1] * w;
						v->fog = a->fog * u + b->fog * w2 + c->fog * w;
						memcpy(v->eye, p, sizeof(v->eye));

						/* the normal for per-pixel lighting, interpolated across the original triangle */
						{
							float nl;

							for (r = 0; r < 3; r++)
							{
								v->en[r] = N1[r] * u + N2[r] * w2 + N3[r] * w;
							}

							nl = sqrtf(v->en[0] * v->en[0] + v->en[1] * v->en[1] + v->en[2] * v->en[2]);

							if (nl > 0)
							{
								v->en[0] /= nl;
								v->en[1] /= nl;
								v->en[2] /= nl;
							}
						}
					}
				}
			}
		}
	}

	*n = o;

	if (r_om_tessdebug && r_om_tessdebug->value)
	{
		R_Printf(PRINT_ALL, "OMTESS level %d: %d -> %d triangles, first corner eye %.1f %.1f %.1f n %.2f %.2f %.2f\n", L, nt,
			o / 3, in[0].eye[0], in[0].eye[1], in[0].eye[2], in[0].en[0], in[0].en[1], in[0].en[2]);
	}

	return E.tessbuf;
}

/* r_om_coronas: the painted halos of lamps (Ghoul textures "glow*", marked by the Ghoul system). With 0 they are
   drawn into the bloom source only: the bloom makes the halo around the lamp instead of the flat sprite */
#define GLOW_MAX 64
static GLuint glowtex[GLOW_MAX];
static int numglow;
static cvar_t *r_om_coronas;

static void
GlowMark(GLuint tex, qboolean on)
{
	int i;

	for (i = 0; i < numglow; i++)
	{
		if (glowtex[i] == tex)
		{
			if (!on)
			{
				glowtex[i] = glowtex[--numglow];
			}

			return;
		}
	}

	if (on && tex && numglow < GLOW_MAX)
	{
		glowtex[numglow++] = tex;
	}
}

static qboolean
IsGlow(GLuint tex)
{
	int i;

	for (i = 0; i < numglow; i++)
	{
		if (glowtex[i] == tex)
		{
			return true;
		}
	}

	return false;
}

/* r_om_tess: the textures of round models (Ghoul/om_round.txt, marked by the Ghoul system); an entity that draws
   one is remembered (gl3_ghoul.c) and drawn curved as a whole from its next frame */
#define ROUND_MAX 256
static GLuint roundtex[ROUND_MAX];
static int numround;
static qboolean sawround;

static void
RoundMark(GLuint tex)
{
	int i;

	for (i = 0; i < numround; i++)
	{
		if (roundtex[i] == tex)
		{
			return;
		}
	}

	if (tex && numround < ROUND_MAX)
	{
		roundtex[numround++] = tex;
	}
}

static qboolean
IsRound(GLuint tex)
{
	int i;

	for (i = 0; i < numround; i++)
	{
		if (roundtex[i] == tex)
		{
			return true;
		}
	}

	return false;
}

/* whether the entity drawn since the last call drew a round model's texture */
qboolean
GL3_GhoulEmu_TakeRound(void)
{
	qboolean r = sawround;

	sawround = false;
	return r;
}

/* the vertex ring of Flush (whole vertices, so a draw can start at any of them) */
#define EMU_RING ((GLsizeiptr)sizeof(emuvtx_t) * 65536)
static GLuint emuringvbo;
static int emuringgen;
static GLsizeiptr emuringoff;

static void
Flush(void)
{
	GLint first = 0; /* first vertex of this primitive in the vertex ring */
	GLenum mode = GL_TRIANGLES;
	emuvtx_t *out, *flat;
	int n = 0, nflat, i, count = E.numverts, gputess = 0;
	qboolean halo;

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

	sawround |= E.texture2d && IsRound(E.boundtex);

	/* the shadow passes take the triangles as modelled: curved silhouettes are lost in a shadow map's texels,
	   the tessellated ones cost the cube and cascade passes many times over */
	flat = out;
	nflat = n;

	/* GFX-modern tessellation of the entity being drawn: every pass over its surfaces gets the same curved
	   triangles (a decal or glass pass drawn over a tessellated body must not z-fight with it), but not the
	   ground shadow quad nor a painted light cone */
	if ((mode == GL_TRIANGLES) && (E.tess > 1) &&
		!(E.blendon && (E.bsrc == GL_ZERO) && (E.bdst == GL_ONE_MINUS_SRC_ALPHA)) && !GL3_Beam_IsBeam(E.boundtex))
	{
#ifndef YQ2_GL3_GLES3
		/* on the GPU (the vertex shader): only the original triangles are uploaded (r_om_tess_cpu 1: the CPU
		   path, a test aid for comparing) */
		static cvar_t *r_om_tess_cpu;

		if (!r_om_tess_cpu)
		{
			r_om_tess_cpu = ri.Cvar_Get("r_om_tess_cpu", "0", 0);
		}

		if (E.tbotex && (E.u_tessL != -1) && !r_om_tess_cpu->value)
		{
			gputess = E.tess;
		}
		else
#endif
		{
			out = Tessellate(out, &n);
		}
	}

	/* a painted light cone in a mirror image: the beam pass draws the real one only (no second beam) */
	{
		extern qboolean gl3_ghoul_mirror;

		if (gl3_ghoul_mirror && GL3_Beam_IsBeam(E.boundtex))
		{
			return;
		}
	}

	/* GFX-modern light beams: a painted light cone is taken over by the volumetric beam pass */
	if ((mode == GL_TRIANGLES) && GL3_Beam_IsBeam(E.boundtex) &&
		GL3_Beam_Capture(out[0].pos, out[0].st, n, sizeof(emuvtx_t) / sizeof(float), E.boundtex))
	{
		return;
	}

	/* r_om_coronas 0: a lamp's painted halo only feeds the bloom (nothing at all without bloom) */
	if (!r_om_coronas)
	{
		r_om_coronas = ri.Cvar_Get("r_om_coronas", "0", CVAR_ARCHIVE);
	}

	halo = !r_om_coronas->value && IsGlow(E.boundtex);

#ifdef YQ2_GL3_GLES3
	if (halo)
	{
		return;
	}
#else
	if (halo && !gl3post.bloom)
	{
		return;
	}
#endif

	GL3_UseProgram(E.prog);
	first = 0;
#ifndef YQ2_GL3_GLES3
	if (gputess)
	{
		/* the corners into the texture buffer, the draw without attributes: 3 L^2 vertices per triangle */
		glBindBuffer(GL_TEXTURE_BUFFER, E.tbo);
		glBufferData(GL_TEXTURE_BUFFER, n * sizeof(emuvtx_t), out, GL_STREAM_DRAW);
		glBindBuffer(GL_TEXTURE_BUFFER, 0);
		GL3_SelectTMU(GL_TEXTURE0 + EMU_TBO_UNIT);
		glBindTexture(GL_TEXTURE_BUFFER, E.tbotex);
		GL3_BindVAO(E.tessvao);

		if (E.tesstabL != gputess)
		{
			int tab[48 * 2], i, j, k = 0;

			memset(tab, 0, sizeof(tab));
			/* the same sub-triangles and winding as Tessellate */
			for (i = 0; i < gputess; i++)
			{
				for (j = 0; i + j < gputess; j++)
				{
					int tri[2][3][2] = {{{i, j}, {i + 1, j}, {i, j + 1}}, {{i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
					int q, m, nq = (i + j < gputess - 1) ? 2 : 1;

					for (q = 0; q < nq; q++)
					{
						for (m = 0; m < 3; m++, k++)
						{
							tab[k * 2] = tri[q][m][0];
							tab[k * 2 + 1] = tri[q][m][1];
						}
					}
				}
			}

			glUniform2iv(E.u_tessTab, 48, tab);
			E.tesstabL = gputess;
		}

		U1i(E.u_tessL, gputess);
		U1f(E.u_tessK, E.tessk);
		UM4(E.u_tessProj, E.proj[E.projdepth]);
		n = (n / 3) * gputess * gputess * 3;
	}
	else
#endif
	{
		if (E.u_tessL != -1)
		{
			U1i(E.u_tessL, 0);
		}

		GL3_BindVAO(E.vao);
		GL3_BindVBO(E.vbo);
		/* a ring of vertices in one big buffer: each primitive is appended unsynchronised and drawn from its first
		   vertex; the store is orphaned once per lap (before: glBufferData per glBegin/glEnd primitive, hundreds
		   to thousands of re-specifications of one buffer a frame) */
		{
			GLsizeiptr bytes = (GLsizeiptr)n * (GLsizeiptr)sizeof(emuvtx_t);
			void *p = NULL;

			if ((emuringvbo != E.vbo) || (emuringgen != gl3_contextgen) || (bytes > EMU_RING / 4))
			{
				emuringvbo = E.vbo;
				emuringgen = gl3_contextgen;
				emuringoff = EMU_RING; /* allocate below */
			}

			if ((bytes > EMU_RING / 4) || !GL3_RingsOn())
			{
				/* a huge primitive: its own store, the ring starts again next time */
				glBufferData(GL_ARRAY_BUFFER, bytes, out, GL_STREAM_DRAW);
				++gl3_numBufferVtxData;
				emuringvbo = 0;
			}
			else
			{
				if (emuringoff + bytes > EMU_RING)
				{
					glBufferData(GL_ARRAY_BUFFER, EMU_RING, NULL, GL_STREAM_DRAW);
					emuringoff = 0;
				}

				p = glMapBufferRange(GL_ARRAY_BUFFER, emuringoff, bytes,
					GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);

				if (p)
				{
					memcpy(p, out, bytes);
					glUnmapBuffer(GL_ARRAY_BUFFER);
				}
				else
				{
					glBufferSubData(GL_ARRAY_BUFFER, emuringoff, bytes, out);
				}

				first = (GLint)(emuringoff / (GLsizeiptr)sizeof(emuvtx_t));
				emuringoff += bytes;
			}
		}
	}

	GL3_SelectTMU(GL_TEXTURE0);

	if (TexEnvIndex())
	{
		glBindTexture(GL_TEXTURE_2D, E.boundtex);
		gl3state.currenttexture = E.boundtex;
	}

	U1i(E.u_tex, 0);
	U1i(E.u_texenv, TexEnvIndex());
	U1i(E.u_texswz, TexSwizzle(E.boundtex));
	U1i(E.u_alphafunc, E.alphatest ? AlphaFuncIndex(E.alphafunc) : 7);
	U1f(E.u_alpharef, Clamp01(E.alpharef));
	U1i(E.u_fog, E.fog ? 1 : 0);
	U4f(E.u_fogcolor, E.fogcolor[0], E.fogcolor[1], E.fogcolor[2], E.fogcolor[3]);

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

			if (halo)
			{
				glow = 0.8f;
			}
		}

		GL3_Post_EmisForBlend(E.bsrc, E.bdst, E.blendon, glow, emis);

		/* an additive pass that is no light source (a specular pass) adds nothing to the glow */
		if (E.blendon && (E.bdst == GL_ONE) && (glow <= 0.0f))
		{
			emis[0] = emis[1] = emis[2] = 0.0f;
			emis[3] = 2.0f;
		}

		U4fv(E.u_emis, 1, emis);
	}

	/* GFX-modern sun shadows: the ground shadow pass (blend 0, 1 - src alpha, ghoul_draw) gives way to the
	   sun shadow where the ground is in the sun; opaque depth-writing triangles are casters */
	{
		qboolean ground = E.blendon && (E.bsrc == GL_ZERO) && (E.bdst == GL_ONE_MINUS_SRC_ALPHA);
		int repl = (E.csmrepl && ground) ? 1 : 0, nbeam = 0;

		/* light beams: inside a lamp's pool the lamp's shadow of the character replaces this one */
		if (E.casts && ground)
		{
			const float *pools;

			nbeam = GL3_Beam_Pools(&pools);

			if (nbeam > 0)
			{
				U4fv(E.u_beam, nbeam * 4, pools);
			}
		}

		U1i(E.u_csmrepl, repl);
		U1i(E.u_beamn, nbeam);

		if (repl || nbeam)
		{
			UM4(E.u_invpv, GL3_CSM_InvPV());
		}

		if (E.csmcapture && (mode == GL_TRIANGLES) && E.depthmask && !E.blendon && !E.alphatest)
		{
			GL3_CSM_AddCasterTris(flat[0].pos, nflat, sizeof(emuvtx_t) / sizeof(float));
		}

		if (E.casts && (mode == GL_TRIANGLES) && E.depthmask && !E.blendon && !E.alphatest)
		{
			GL3_Beam_AddCasterTris(flat[0].pos, nflat, sizeof(emuvtx_t) / sizeof(float));
		}
	}

	/* GFX-modern per-pixel lighting: this primitive's light and material state for omLit() */
	{
		int ppl = (E.lighting && E.ppl) ? 1 : 0;

		U1i(E.u_ppl, ppl);

		if (ppl)
		{
			float L[NUM_LIGHTS * 6 * 4], M[6 * 4];
			int i;

			for (i = 0; i < NUM_LIGHTS; i++)
			{
				const emulight_t *l = &E.light[i];
				float *q = L + i * 24;

				memcpy(q, l->position, 4 * sizeof(float));
				memcpy(q + 4, l->ambient, 3 * sizeof(float));
				q[7] = l->on ? 1.0f : 0.0f;
				memcpy(q + 8, l->diffuse, 4 * sizeof(float));
				memcpy(q + 12, l->specular, 4 * sizeof(float));
				memcpy(q + 16, l->spotdir, 3 * sizeof(float));
				q[19] = (l->spotcut == 180) ? -2.0f : cosf(l->spotcut * (float)M_PI / 180.0f);
				memcpy(q + 20, l->k, 3 * sizeof(float));
				q[23] = l->spotexp;
			}

			memcpy(M, E.mat.ambient, 4 * sizeof(float));
			memcpy(M + 4, E.mat.diffuse, 4 * sizeof(float));
			memcpy(M + 8, E.mat.specular, 4 * sizeof(float));
			memcpy(M + 12, E.mat.emission, 4 * sizeof(float));
			M[16] = E.mat.shininess;
			M[17] = E.colormaterial ? 1.0f : 0.0f;
			M[18] = E.localviewer ? 1.0f : 0.0f;
			M[19] = 0;
			memcpy(M + 20, E.lmambient, 4 * sizeof(float));
			U4fv(E.u_pplL, NUM_LIGHTS * 6, L);
			U4fv(E.u_pplM, 6, M);
			U1i(E.u_ambon, E.ambon ? 1 : 0);

			if (E.ambon)
			{
				U3fv(E.u_amb, 6, E.amb);
				UM3(E.u_e2w, E.e2w);
			}
		}
	}

#ifndef YQ2_GL3_GLES3
	if (halo)
	{
		/* the bloom source only: not the scene colour, not the material of the reflections */
		glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(2, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		++gl3_num3Ddraws;
		glDrawArrays(mode, first, n);
		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		GL3_Post_MaterialWrites(true);
		return;
	}
#endif

	/* GFX-modern r_om_a2c: an opaque alpha-tested primitive (greater / greater-or-equal) in a multisampled target
	   resolves its edge as sample coverage, as the world faces do (gl3_main.c) */
	{
		static cvar_t *r_om_a2c;
		int a2c = 0;

		if (!r_om_a2c)
		{
			r_om_a2c = ri.Cvar_Get("r_om_a2c", "1", CVAR_ARCHIVE);
		}

		if (E.alphatest && !E.blendon && r_om_a2c->value)
		{
			int f = AlphaFuncIndex(E.alphafunc);
			if (f == 4 || f == 6)
			{
				a2c = (gl3_drawsamples > 0); /* gl3_post.c: no driver query per primitive */
			}
		}

		U1i(E.u_a2c, a2c);

		if (a2c)
		{
			glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
#ifndef YQ2_GL3_GLES3
			glEnable(GL_SAMPLE_ALPHA_TO_ONE); /* not in GLES */
#endif
			++gl3_num3Ddraws;
			glDrawArrays(mode, first, n);
			glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
#ifndef YQ2_GL3_GLES3
			glDisable(GL_SAMPLE_ALPHA_TO_ONE);
#endif
			return;
		}
	}

	++gl3_num3Ddraws;
	glDrawArrays(mode, first, n);
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
				E.lcdirty = true;
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
	E.lcdirty = true;
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
	E.depthfunc = f;
	glDepthFunc(f);
}

static void GHLAPI
Emu_DepthMask(GLboolean flag)
{
	E.depthmask = (flag != 0);
	glDepthMask(flag);
}

/* the level-0 image last given to each texture by the Ghoul library (Emu_TexImage2D): the library re-specifies
   some of its textures every frame - on tsr1 ~290 glTexImage2D a second, 33 alpha 64x64 images in rotation -
   so an identical image is skipped and a new image of the same size and format goes through glTexSubImage2D
   instead of reallocating the storage */
typedef struct
{
	unsigned long long hash;
	int w, h, valid;
	GLint ifmt;
	GLenum fmt, type;
} emutexrec_t;

static emutexrec_t *texrecs;
static unsigned numtexrecs;
static unsigned texstat[3]; /* full, sub, skipped (r_om_ghltexlog) */

static emutexrec_t *
TexRec(GLuint t)
{
	if (t >= numtexrecs)
	{
		unsigned n = numtexrecs ? numtexrecs : 1024;
		emutexrec_t *r;

		while (n <= t)
		{
			n *= 2;
		}

		if ((n > (1u << 20)) || !(r = realloc(texrecs, n * sizeof(*r))))
		{
			return NULL;
		}

		memset(r + numtexrecs, 0, (n - numtexrecs) * sizeof(*r));
		texrecs = r;
		numtexrecs = n;
	}

	return &texrecs[t];
}

static void
TexRecForget(GLuint t)
{
	if (t < numtexrecs)
	{
		texrecs[t].valid = 0;
	}
}

static unsigned long long
HashBytes(const byte *p, size_t n)
{
	unsigned long long h = 1469598103934665603ULL;
	size_t i;

	for (i = 0; i < n; i++)
	{
		h = (h ^ p[i]) * 1099511628211ULL;
	}

	return h;
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
		TexRecForget(textures[i]);
		GL3_Beam_ForgetTexture(textures[i]);
		GlowMark(textures[i], false);

		{
			int k;

			for (k = 0; k < numround; k++)
			{
				if (roundtex[k] == textures[i])
				{
					roundtex[k--] = roundtex[--numround];
				}
			}
		}
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
	GLsizei i;

	glGenTextures(n, textures);

	for (i = 0; i < n; i++)
	{
		TexRecForget(textures[i]); /* a name freed outside the library (vid_restart) holds no image */
	}
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

	{
		/* r_om_ghltexlog 1: every texture the Ghoul library uploads, with the time: uploads during play are hitches */
		static cvar_t *log;

		if (!log)
		{
			log = ri.Cvar_Get("r_om_ghltexlog", "0", 0);
		}

		if (log->value && (level == 0))
		{
			R_Printf(PRINT_ALL, "GHLTEX %.0f ms tex %u %dx%d fmt 0x%x (full %u sub %u skipped %u)\n", GL3_LoadClockMs(),
				E.boundtex, width, height, format, texstat[0], texstat[1], texstat[2]);
		}
	}

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

	/* a repeated image: skipped, or the same storage refilled (TexRec) */
	{
		int c = 0;
		emutexrec_t *rec = NULL;

		switch (fmt)
		{
			case GL_RED: case GHL_GL_ALPHA: case GHL_GL_LUMINANCE: c = 1; break;
			case GL_RG: c = 2; break;
			case GL_RGB: c = 3; break;
			case GL_RGBA: c = 4; break;
			default: c = 0; break; /* BGR(A) and the rest: always the full upload */
		}

		if ((target == GL_TEXTURE_2D) && (level == 0) && pixels && (type == GL_UNSIGNED_BYTE) && c && E.boundtex &&
			(width > 0) && (height > 0) && (rec = TexRec(E.boundtex)))
		{
			GLint align = 4;
			size_t row, bytes;
			unsigned long long hash;

			glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
			row = (((size_t)width * c) + (size_t)align - 1) / (size_t)align * (size_t)align;
			bytes = row * (size_t)(height - 1) + (size_t)width * c;
			hash = HashBytes((const byte *)pixels, bytes);

			if (rec->valid && (rec->w == width) && (rec->h == height) && (rec->ifmt == ifmt) && (rec->fmt == fmt) &&
				(rec->type == type))
			{
				if (rec->hash != hash)
				{
					glTexSubImage2D(target, 0, 0, 0, width, height, fmt, type, pixels);
					rec->hash = hash;
					texstat[1]++;
				}
				else
				{
					texstat[2]++;
				}

				return;
			}

			glTexImage2D(target, level, ifmt, width, height, 0, fmt, type, pixels);
			texstat[0]++;
			rec->valid = 1;
			rec->w = width;
			rec->h = height;
			rec->ifmt = ifmt;
			rec->fmt = fmt;
			rec->type = type;
			rec->hash = hash;
			return;
		}

		if ((target == GL_TEXTURE_2D) && (level == 0))
		{
			TexRecForget(E.boundtex);
		}
	}

#ifdef YQ2_GL3_GLES3
	/* GLES has no BGR / BGRA client formats: swap red and blue on the CPU */
	if (((fmt == 0x80E0) || (fmt == 0x80E1)) && pixels && (type == GL_UNSIGNED_BYTE) && (width > 0) && (height > 0))
	{
		int c = (fmt == 0x80E0) ? 3 : 4;
		size_t i, n = (size_t)width * height;
		byte *sw = malloc(n * c);

		if (sw)
		{
			const byte *src = (const byte *)pixels;

			for (i = 0; i < n; i++)
			{
				sw[i * c] = src[i * c + 2];
				sw[i * c + 1] = src[i * c + 1];
				sw[i * c + 2] = src[i * c];

				if (c == 4)
				{
					sw[i * c + 3] = src[i * c + 3];
				}
			}

			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexImage2D(target, level, ifmt, width, height, 0, (c == 3) ? GL_RGB : GL_RGBA, type, sw);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			free(sw);
		}
	}
	else
#endif
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
	else if (pname == OM_GHL_BEAM_HINT) /* openMERC: the bound texture is a painted light cone (1) or halo (2) */
	{
		if (param == 3)
		{
			RoundMark(E.boundtex);
		}
		else if (param == 2)
		{
			GlowMark(E.boundtex, true);
		}
		else if (param)
		{
			GL3_Beam_MarkTexture(E.boundtex);
		}

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
	E.lcdirty = true;

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

	/* a renderer restart without GL3_GhoulEmu_Shutdown left names of the old context behind. Known by the context's
	   number (glIsProgram for every model waited for the driver to finish all queued work: milliseconds a frame) */
	if (E.prog && (E.context != gl3_contextgen))
	{
		E.prog = E.vao = E.vbo = 0;
		E.tessvao = E.tbo = E.tbotex = 0;
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
	E.casts = false;
}

/* GFX-modern: whether the entity being drawn casts a sun shadow and lets its ground shadow give way to it */
void
GL3_GhoulEmu_SetCsm(qboolean capture, qboolean replace)
{
	E.csmcapture = capture;
	E.csmrepl = replace;
}

/* GFX-modern: per-pixel lighting of the entity being drawn */
void
GL3_GhoulEmu_SetPPL(qboolean on)
{
	E.ppl = on;
}

/* GFX-modern r_om_ambcube: the ambient factors of the entity being drawn (18 floats, +x -x +y -y +z -z), NULL off;
   the eye space is the 3D view's (gl3state.viewMat3D, the modelview the Ghoul call starts from) */
void
GL3_GhoulEmu_SetAmbientCube(const float *cube)
{
	int i, j;

	E.ambon = (cube != NULL);

	if (!cube)
	{
		return;
	}

	memcpy(E.amb, cube, sizeof(E.amb));

	/* eye = R world, so world = R^T eye: the matrix that maps eye to world has R's rows as its columns */
	for (j = 0; j < 3; j++)
	{
		for (i = 0; i < 3; i++)
		{
			E.e2w[j * 3 + i] = gl3state.viewMat3D.Elements[i][j];
		}
	}
}

/* GFX-modern: PN tessellation of the entity being drawn - subdivision level (0 or 1 off) and how far the
   new vertices go from the flat triangle toward the PN surface (0..1) */
void
GL3_GhoulEmu_SetTess(int level, float strength)
{
	if (!r_om_tessdebug)
	{
		r_om_tessdebug = ri.Cvar_Get("r_om_tessdebug", "0", 0);
	}

	E.tess = (level > 4) ? 4 : level;
	E.tessk = Q_clamp(strength, 0.0f, 1.0f);
}

/* GFX-modern: whether the entity being drawn casts a shadow into light beams (independent of the sun) */
void
GL3_GhoulEmu_SetCaster(qboolean casts)
{
	E.casts = casts;
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
	/* the glow / round texture names belong to the old context: a new skin given an old glow name drew only into
	   the bloom source (invisible), an old round name tessellated an unrelated model */
	numglow = numround = 0;
	sawround = false;

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

	if (E.tessvao)
	{
		glDeleteVertexArrays(1, &E.tessvao);
		glDeleteBuffers(1, &E.tbo);
		glDeleteTextures(1, &E.tbotex);
	}

	if (gl3state.currentVAO == E.tessvao)
	{
		gl3state.currentVAO = 0;
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
	E.tessvao = E.tbo = E.tbotex = 0;
	free(E.texswz);
	E.texswz = NULL;
	E.numtexswz = 0;
	E.ready = false;
}
