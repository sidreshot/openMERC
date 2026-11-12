/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: see-through glass (GFX-modern r_om_glass).
 *
 * The glass of SoF maps is a translucent texture (texinfo 0x800 or trans
 * 33/66) of the glass material (37): a flat dark grey with one alpha over
 * the pane (0.36 .. 0.72), frames and wire mesh solid, cracks as holes. Blended
 * as it is, every window is a pane of smoked film. Here the pane is clear glass
 * instead:
 *  - the scene behind it (a copy of what is drawn so far: the world and the
 *    farther part of the sorted pass) refracted - the long uneven waves of
 *    drawn window glass and the texture's streaks bend it, more the farther
 *    the scene lies behind the pane, a little apart per colour channel; never
 *    from what is in front of the pane;
 *  - tinted by the hue of the texture (smoked blue stays bluish, not dark),
 *    a few percent absorbed;
 *  - reflecting by Fresnel (glass, F0 0.04: little head-on, a mirror at
 *    grazing angles) the reflection probes of r_om_probes (phones: the light
 *    around the pane), and the lamps of the view as sharp highlights;
 *  - smudges and streaks of the texture (lighter than its pane) frost the view
 *    through them; the frame, wire and dirt (alpha over the pane's) stay the
 *    lit texture as before, cracks stay holes.
 * The pane's colour and alpha come from the texture's texels when it loads
 * (GL3_Glass_PaneStats). A pane over another pane already drawn this view
 * copies the scene again (so the farther one shows through), at most
 * GLASS_MAXCOPY times a view.
 *
 * r_om_glass 0: the old blended texture. r_om_glass_refract: strength of the
 * refraction (1). r_om_glass_tint: how much of the texture's hue tints the
 * view (0.35). r_om_glass_reflect: strength of the reflection (1).
 */

#include "header/local.h"

/* copies of the scene a view may make beyond the first (each a full blit, a resolve with MSAA) */
#ifdef YQ2_GL3_GLES3
#define GLASS_MAXCOPY 0
#else
#define GLASS_MAXCOPY 2
#endif
#define GLASS_MAXBOX 64

#define GLASS_MAXVERTS 1024

static cvar_t *r_om_glass, *r_om_glass_refract, *r_om_glass_tint, *r_om_glass_reflect;

typedef struct
{
	float pos[3];
	float st[2];
	float rgba[4];
} glassvtx_t;

static GLuint vao, vbo;
static int vaogen = -1;
static glassvtx_t verts[GLASS_MAXVERTS];

/* this view: the copies made, the screen box (NDC) of the panes drawn since the last copy */
static int copies, ndrawn;
static qboolean copied;
static float have[GLASS_MAXBOX][4]; /* the screen boxes copied this view (each copy covers only its pane) */
static int nhave;
static float drawn[GLASS_MAXBOX][4]; /* each pane on its own: the box of a whole window's grid covered the panes
                                        between, every pane of it copied the scene again (FPS) */
static int lampframe = -1, probeframe = -1;
static GLuint lampprog, probeprog;

/* ------------------------------------------------------------------
 * shaders (after vertexCommon3D / fragmentCommon3D of gl3_shaders.c)
 * ------------------------------------------------------------------ */

const char *gl3_glass_vs =
	"out vec4 passColor;\n"
	"out vec3 passWorld;\n"
	"out float passFogDist;\n"
	"uniform vec4 fogplane;\n"
	"void main()\n"
	"{\n"
	"	vec4 wp = transModel * vec4(position, 1.0);\n"
	"	passTexCoord = texCoord;\n"
	"	passColor = vertColor;\n"
	"	passWorld = wp.xyz;\n"
	"	passFogDist = abs(dot(fogplane.xyz, wp.xyz) + fogplane.w);\n"
	"	gl_Position = transProjView * wp;\n"
	"}\n";

#ifndef YQ2_GL3_GLES3
/* r_om_probes: the reflection probes (gl3_post.c ProbesUniforms), as the model shaders read them */
#define GLASS_PROBES \
	"uniform samplerCube pc0;\n" \
	"uniform samplerCube pc1;\n" \
	"uniform samplerCube pc2;\n" \
	"uniform samplerCube pc3;\n" \
	"uniform vec4 ppos[4];\n" \
	"uniform vec4 pmin[4];\n" \
	"uniform vec4 pmax[4];\n" \
	"uniform float pcount;\n" \
	"vec3 PDir(vec4 o, vec4 bmin, vec4 bmax, vec3 wp, vec3 wr)\n" \
	"{\n" \
	"	vec3 r = wr + vec3(equal(wr, vec3(0.0))) * 1e-5;\n" \
	"	vec3 tf = max((bmax.xyz - wp) / r, (bmin.xyz - wp) / r);\n" \
	"	float t = max(min(min(tf.x, tf.y), tf.z), 0.0);\n" \
	"	return wp + wr * t - o.xyz;\n" \
	"}\n" \
	"float PW(vec4 o, vec4 bmin, vec4 bmax, vec3 wp)\n" \
	"{\n" \
	"	if (o.w <= 0.0) return 0.0;\n" \
	"	vec3 q = max(max(bmin.xyz - wp, wp - bmax.xyz), vec3(0.0));\n" \
	"	vec3 e = wp - o.xyz;\n" \
	"	return o.w * (1.0 - smoothstep(0.0, 96.0, length(q)) + 1e-3 / (1.0 + dot(e, e) * 1e-5));\n" \
	"}\n" \
	"vec3 Probe(vec3 wp, vec3 wr, float lod)\n" \
	"{\n" \
	"	float w0 = PW(ppos[0], pmin[0], pmax[0], wp), w1 = PW(ppos[1], pmin[1], pmax[1], wp);\n" \
	"	float w2 = PW(ppos[2], pmin[2], pmax[2], wp), w3 = PW(ppos[3], pmin[3], pmax[3], wp);\n" \
	"	vec3 c = vec3(0.0);\n" \
	"	if (w0 > 0.0) c += w0 * textureLod(pc0, PDir(ppos[0], pmin[0], pmax[0], wp, wr), lod).rgb;\n" \
	"	if (w1 > 0.0) c += w1 * textureLod(pc1, PDir(ppos[1], pmin[1], pmax[1], wp, wr), lod).rgb;\n" \
	"	if (w2 > 0.0) c += w2 * textureLod(pc2, PDir(ppos[2], pmin[2], pmax[2], wp, wr), lod).rgb;\n" \
	"	if (w3 > 0.0) c += w3 * textureLod(pc3, PDir(ppos[3], pmin[3], pmax[3], wp, wr), lod).rgb;\n" \
	"	float ws = w0 + w1 + w2 + w3;\n" \
	"	return (ws > 1e-4) ? c / ws : vec3(-1.0);\n" \
	"}\n"
#else
#define GLASS_PROBES \
	"uniform float pcount;\n" \
	"vec3 Probe(vec3 wp, vec3 wr, float lod) { return vec3(-1.0); }\n"
#endif

const char *gl3_glass_fs =
	"in vec4 passColor;\n"
	"in vec3 passWorld;\n"
	"in float passFogDist;\n"
	"uniform sampler2D tex;\n"
	"uniform sampler2D gScene;\n"   /* GL_TEXTURE11: the scene under the pane (GL3_Post_GlassCopy) */
	"uniform sampler2D gDepth;\n"   /* GL_TEXTURE7: its depth */
	"uniform vec4 omSoftP;\n"       /* projection C, D; depth range min, max */
	"uniform vec4 omSoftV;\n"       /* 1 / scene target size */
	"uniform vec4 gN;\n"            /* the pane's normal toward the eye */
	"uniform vec4 gEye;\n"
	"uniform vec4 gPane;\n"         /* the clear pane's colour (0..1) and alpha */
	"uniform vec4 gK;\n"            /* refraction, tint, reflection, waviness */
	"uniform vec4 gTexel;\n"        /* 1 / texture size */
	"uniform vec4 gFogC;\n"         /* gl_fogmode 1: colour, on */
	"uniform vec4 gFogR;\n"         /* start, end */
	"uniform vec4 omBL[8];\n"       /* the lamps of the view (gl3_lamps.c): origin, radius */
	"uniform vec4 omBC[8];\n"       /* colour */
	"uniform int omBN;\n"
	GLASS_PROBES
	"float gHash(vec2 p)\n"
	"{\n"
	"	p = fract(p * vec2(123.34, 456.21));\n"
	"	p += dot(p, p + 45.32);\n"
	"	return fract(p.x * p.y);\n"
	"}\n"
	/* the slope of value noise: uneven waves, no rows */
	"vec2 gNoiseG(vec2 p)\n"
	"{\n"
	"	vec2 i = floor(p), f = fract(p);\n"
	"	vec2 u = f * f * (3.0 - 2.0 * f), du = 6.0 * f * (1.0 - f);\n"
	"	float a = gHash(i), b = gHash(i + vec2(1.0, 0.0));\n"
	"	float c = gHash(i + vec2(0.0, 1.0)), d = gHash(i + vec2(1.0, 1.0));\n"
	"	return du * vec2(b - a + (a - b - c + d) * u.y, c - a + (a - b - c + d) * u.x);\n"
	"}\n"
	/* how much of a texel is a smudge or streak: lighter than the pane and not frame, wire or dirt */
	"float gStreak(vec4 t)\n"
	"{\n"
	"	float cover = clamp((t.a - gPane.w - 0.04) / max(1.0 - gPane.w - 0.04, 0.05), 0.0, 1.0);\n"
	"	vec3 lw = vec3(0.299, 0.587, 0.114);\n"
	"	return clamp((dot(t.rgb, lw) - dot(gPane.rgb, lw)) * 4.0 - 0.1, 0.0, 1.0) * (1.0 - cover);\n"
	"}\n"
	"float gEyeDist(float z)\n"
	"{\n"
	"	float ndc = (z - omSoftP.z) / (omSoftP.w - omSoftP.z) * 2.0 - 1.0;\n"
	"	return omSoftP.y / (ndc + omSoftP.x);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	vec4 t = texture(tex, passTexCoord);\n"
	"	if (t.a < 0.03) discard;\n" /* cracks: holes */
	/* the frame, wire and dirt: alpha over the pane's */
	"	float cover = clamp((t.a - gPane.w - 0.04) / max(1.0 - gPane.w - 0.04, 0.05), 0.0, 1.0);\n"
	"	vec3 lw = vec3(0.299, 0.587, 0.114);\n"
	/* smudges and streaks: lighter than the pane */
	"	float smudge = clamp((dot(t.rgb, lw) - dot(gPane.rgb, lw)) * 4.0 - 0.1, 0.0, 1.0) * (1.0 - cover);\n"
	"	vec3 N = normalize(gN.xyz);\n"
	"	vec3 V = normalize(gEye.xyz - passWorld);\n"
	"	vec3 T1 = normalize(cross(N, (abs(N.z) < 0.9) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));\n"
	"	vec3 B1 = cross(N, T1);\n"
	"	vec2 pp = vec2(dot(passWorld, T1), dot(passWorld, B1));\n"
	/* drawn window glass: long uneven waves, mostly across; the texture's streaks bend the light too */
	"	vec2 g = (gNoiseG(pp * vec2(0.018, 0.05)) * vec2(0.35, 1.0) + gNoiseG(pp * 0.08 + 17.0) * 0.3) * gK.w;\n"
	/* only the streaks bend it, not the edges of the wire and the frame: the slope of every colour change of the
	   texture tore the view behind wire glass along the mesh into jagged pieces (issue #13) */
	"	g += vec2(gStreak(texture(tex, passTexCoord + vec2(gTexel.x, 0.0))) - gStreak(texture(tex, passTexCoord - vec2(gTexel.x, 0.0))),\n"
	"		gStreak(texture(tex, passTexCoord + vec2(0.0, gTexel.y))) - gStreak(texture(tex, passTexCoord - vec2(0.0, gTexel.y)))) * 0.2;\n"
	"	vec3 tw = T1 * g.x + B1 * g.y;\n"
	"	vec3 Np = normalize(N + tw * 0.03);\n"
	/* the scene behind: shifted as through an uneven slab - more the farther it lies behind the pane */
	"	vec2 suv = gl_FragCoord.xy * omSoftV.xy;\n"
	"	float surfD = gEyeDist(gl_FragCoord.z);\n"
	"	float behind = max(gEyeDist(texture(gDepth, suv).r) - surfD, 0.0);\n"
	"	vec4 c0 = transProjView * vec4(passWorld, 1.0);\n"
	"	vec4 c1 = transProjView * vec4(passWorld + tw * (0.03 * gK.x * min(behind, 400.0)), 1.0);\n"
	"	vec2 off = clamp((c1.xy / c1.w - c0.xy / c0.w) * 0.5, vec2(-0.012), vec2(0.012));\n"
	"	if (gEyeDist(texture(gDepth, suv + off * 1.1).r) < surfD + 1.0) off = vec2(0.0);\n"
	"	vec3 refr = vec3(texture(gScene, suv + off * 1.08).r, texture(gScene, suv + off).g, texture(gScene, suv + off * 0.92).b);\n"
	/* frosted under the smudges */
	"	if (smudge > 0.05)\n"
	"	{\n"
	"		vec2 r = omSoftV.xy * (1.0 + 3.0 * smudge);\n"
	"		vec3 b = texture(gScene, suv + off + r).rgb + texture(gScene, suv + off - r).rgb +\n"
	"			texture(gScene, suv + off + vec2(r.x, -r.y)).rgb + texture(gScene, suv + off + vec2(-r.x, r.y)).rgb;\n"
	"		refr = mix(refr, b * 0.25, smudge * 0.6);\n"
	"	}\n"
	/* the tint: the texture's hue, not its darkness */
	"	vec3 hue = gPane.rgb / max(max(gPane.r, gPane.g), max(gPane.b, 0.01));\n"
	"	vec3 tint = mix(vec3(1.0), hue, gK.y) * 0.93;\n"
	"	float fm = 1.0 - abs(dot(Np, V));\n"
	"	float F = 0.04 + 0.96 * fm * fm * fm * fm * fm;\n"
	"	vec3 R = reflect(-V, Np);\n"
	"	vec3 lit = passColor.rgb;\n"
	"	vec3 env = Probe(passWorld + N * 2.0, R, 0.4 + smudge * 3.0);\n"
	"	if (env.r < 0.0) env = lit * (0.45 + 0.3 * R.z);\n" /* no probe: the light around the pane */
	/* the lamps: sharp highlights (the traced glints' shape, gl3_post.c), fewer through the smudges */
	"	vec3 spec = vec3(0.0);\n"
	"	float n = mix(2048.0, 96.0, smudge);\n"
	"	for (int i = 0; i < 8; i++)\n"
	"	{\n"
	"		if (i >= omBN) break;\n"
	"		vec3 Lp = omBL[i].xyz - passWorld;\n"
	"		float dl = length(Lp);\n"
	"		float att = 1.0 - dl / (omBL[i].w * 1.5);\n"
	"		if (att <= 0.0) continue;\n"
	"		vec3 L = Lp / dl;\n"
	"		float ndl = dot(N, L);\n"
	"		if (ndl <= 0.0) continue;\n"
	"		spec += omBC[i].rgb * (att * att * ndl * pow(max(dot(Np, normalize(L + V)), 0.0), n) * (n + 8.0) / 64.0);\n"
	"	}\n"
	"	spec = min(spec, vec3(4.0)) * (0.3 + 0.7 * F);\n"
	"	vec3 trans = refr * tint * (1.0 - F);\n"
	"	float hz = smudge * 0.2;\n"
	"	vec3 haze = mix(t.rgb, gPane.rgb, 0.5) * lit * 1.3;\n"
	"	vec3 own = t.rgb * lit * cover + (haze * hz + env * F * gK.z + spec) * (1.0 - cover);\n"
	"	float ownW = cover + hz * (1.0 - cover);\n"
	"	if (gFogC.w > 0.5)\n"
	"	{\n"
	"		float f = clamp((gFogR.y - passFogDist) / max(gFogR.y - gFogR.x, 0.0001), 0.0, 1.0);\n"
	"		own = mix(gFogC.rgb * ownW, own, f);\n"
	"	}\n"
	"	outColor = vec4((trans * (1.0 - cover) * (1.0 - hz) + own) * intensity, 1.0);\n"
	/* premultiplied: the colour replaced; the emissive target (bloom of the lamps behind) dimmed only by the solid
	   parts */
	"	outEmissive = vec4(0.0, 0.0, 0.0, ownW);\n"
	/* the traced reflection (r_om_ssr, after this pass, not refracted) must not show the floor behind the pane: its
	   material cleared, as the old blended glass did */
	"	outMaterial = vec4(0.0, 0.0, 0.0, 1.0);\n"
	"}\n";

/* ------------------------------------------------------------------
 * the pane of a texture
 * ------------------------------------------------------------------ */

/* the most common alpha between the holes and the solid parts (the clear pane), and the mean colour of its texels */
void
GL3_Glass_PaneStats(gl3image_t *image, const byte *rgba, int w, int h)
{
	int hist[256], i, a, n = w * h, step, total = 0, best = -1, bestc = 0;
	double sum[3] = {0, 0, 0};
	int cnt = 0;

	image->omglass[3] = -1;

	if (n <= 0)
	{
		return;
	}

	memset(hist, 0, sizeof(hist));
	step = (n > 16384) ? n / 16384 : 1;

	for (i = 0; i < n; i += step)
	{
		hist[rgba[i * 4 + 3]]++;
		total++;
	}

	for (a = 8; a <= 246; a++)
	{
		int k, c = 0;

		for (k = -4; k <= 4; k++)
		{
			c += hist[Q_clamp(a + k, 0, 255)];
		}

		if (c > bestc)
		{
			bestc = c;
			best = a;
		}
	}

	/* a pane covers a good part of the texture (bars and fences: holes and solid, nothing between) */
	if ((best < 0) || (bestc * 5 < total))
	{
		return;
	}

	for (i = 0; i < n; i += step)
	{
		const byte *p = rgba + i * 4;

		if (abs(p[3] - best) <= 8)
		{
			sum[0] += p[0];
			sum[1] += p[1];
			sum[2] += p[2];
			cnt++;
		}
	}

	if (!cnt)
	{
		return;
	}

	for (i = 0; i < 3; i++)
	{
		image->omglass[i] = (float)(sum[i] / cnt / 255.0);
	}

	/* the pane's alpha: the top of its band (texels a little over it are still the pane) */
	image->omglass[3] = Q_min(best + 4, 255) / 255.0f;
}

/* ------------------------------------------------------------------
 * drawing
 * ------------------------------------------------------------------ */

void
GL3_Glass_BeginFrame(void)
{
	copies = ndrawn = nhave = 0;
	copied = false;
}

static void
InitCvars(void)
{
	if (!r_om_glass)
	{
		r_om_glass = ri.Cvar_Get("r_om_glass", "1", CVAR_ARCHIVE);
		r_om_glass_refract = ri.Cvar_Get("r_om_glass_refract", "1", CVAR_ARCHIVE);
		r_om_glass_tint = ri.Cvar_Get("r_om_glass_tint", "0.35", CVAR_ARCHIVE);
		r_om_glass_reflect = ri.Cvar_Get("r_om_glass_reflect", "1", CVAR_ARCHIVE);
	}
}

/* at the renderer's start: the video page shows the glass rows (requires=r_om_glass) before any glass was drawn */
void
GL3_Glass_Register(void)
{
	InitCvars();
}

static void
MakeVAO(void)
{
	if (vao && (vaogen == gl3_contextgen))
	{
		return;
	}

	glGenVertexArrays(1, &vao);
	GL3_BindVAO(vao);
	glGenBuffers(1, &vbo);
	GL3_BindVBO(vbo);
	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(glassvtx_t), 0);
	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(glassvtx_t), offsetof(glassvtx_t, st));
	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(glassvtx_t), offsetof(glassvtx_t, rgba));
	GL3_BindVAO(0);
	vaogen = gl3_contextgen;
}

/* the face's box on the screen (NDC); the whole screen when a corner is behind the eye */
static void
ScreenBox(const glassvtx_t *vt, int n, float box[4])
{
	const hmm_mat4 *m = &gl3state.uni3DData.transProjViewMat4;

	box[0] = box[1] = 1e9f;
	box[2] = box[3] = -1e9f;

	{
		int i;

		for (i = 0; i < n; i++)
		{
			const float *v = vt[i].pos;
			float x = m->Elements[0][0] * v[0] + m->Elements[1][0] * v[1] + m->Elements[2][0] * v[2] + m->Elements[3][0];
			float y = m->Elements[0][1] * v[0] + m->Elements[1][1] * v[1] + m->Elements[2][1] * v[2] + m->Elements[3][1];
			float w = m->Elements[0][3] * v[0] + m->Elements[1][3] * v[1] + m->Elements[2][3] * v[2] + m->Elements[3][3];

			if (w <= 1.0f)
			{
				box[0] = box[1] = -1;
				box[2] = box[3] = 1;
				return;
			}

			box[0] = Q_min(box[0], x / w);
			box[1] = Q_min(box[1], y / w);
			box[2] = Q_max(box[2], x / w);
			box[3] = Q_max(box[3], y / w);
		}
	}
}

/* a pane of glass drawn as see-through glass: a world face of the sorted pass (model NULL) or a face of an inline
   model (breakable windows; its origin and angles place it); false: not glass (or no post path), drawn as before */
qboolean
GL3_Glass_Draw(msurface_t *surf, gl3image_t *image, const entity_t *model, int fogregion)
{
	GLuint prog = gl3state.siSoFglass.shaderProgram;
	gl3fxstate_t fog;
	float box[4], nrm[3];
	const glpoly_t *p;
	int nv = 0, mat, cls;

	vec3_t fwd = {1, 0, 0}, right = {0, -1, 0}, up = {0, 0, 1};

	InitCvars();

	if (!prog || !r_om_glass->value || !image || (image->omglass[3] <= 0) || (image->omglass[3] >= 0.97f) ||
		!surf->texinfo || (surf->texinfo->flags & (SURF_WARP | SOFSURF_FLOWING)) || GL3_Mirror_InPass())
	{
		return false;
	}

	mat = (surf->texinfo->flags >> 24) & 0xff;
	cls = (int)(-GL3_Mat_SurfClass(surf) + 0.5f);

	if ((mat != 37) && (cls != 4)) /* the glass material or the glass class (gl3_material.c MC_GLASS) */
	{
		return false;
	}

	if (!GL3_Post_DepthCopyPossible())
	{
		return false;
	}

	/* model space to world: yaw, -pitch, -roll (as DrawInlineAlphaFaces) */
	if (model)
	{
		vec3_t a;

		a[0] = -model->angles[0];
		a[1] = model->angles[1];
		a[2] = -model->angles[2];
		AngleVectors(a, fwd, right, up);
	}

	/* the vertices: as the old pass (the light sampled at each) */
	for (p = surf->polys; p; p = p->chain)
	{
		int i;

		for (i = 1; (i + 1 < p->numverts) && (nv + 3 <= GLASS_MAXVERTS); i++)
		{
			int k, idx[3] = {0, i, i + 1};

			for (k = 0; k < 3; k++)
			{
				const float *v = p->vertices[idx[k]].pos;
				glassvtx_t *o = &verts[nv++];
				vec3_t light;
				int j;

				for (j = 0; j < 3; j++)
				{
					o->pos[j] = (model ? model->origin[j] : 0) + v[0] * fwd[j] - v[1] * right[j] + v[2] * up[j];
				}

				o->st[0] = p->vertices[idx[k]].texCoord[0];
				o->st[1] = p->vertices[idx[k]].texCoord[1];
				GL3_SoF_LightPoint(o->pos, light, 255);
				o->rgba[0] = light[0];
				o->rgba[1] = light[1];
				o->rgba[2] = light[2];
				o->rgba[3] = 1;
			}
		}
	}

	if (!nv)
	{
		return true;
	}

	/* the normal toward the eye */
	{
		vec3_t n0;
		int j;

		VectorCopy(surf->plane->normal, n0);

		if (surf->flags & SURF_PLANEBACK)
		{
			VectorNegate(n0, n0);
		}

		for (j = 0; j < 3; j++)
		{
			nrm[j] = n0[0] * fwd[j] - n0[1] * right[j] + n0[2] * up[j];
		}
	}

	if (DotProduct(nrm, r_newrefdef.vieworg) - DotProduct(nrm, verts[0].pos) < 0)
	{
		/* the far side of a glass brush (the back of an aquarium, a window's other face) seen through its near side:
		   the near pane already bends and tints the view - a second layer only cost a copy of the scene and a full
		   glass shader over it (jpn1 club aquarium: 8 panes, 3 copies, ~2.7 ms) */
		return true;
	}

	/* test aid r_om_glass_debug 1: the panes of a view, once a second */
	{
		static cvar_t *dbg;
		static float next;

		if (!dbg)
		{
			dbg = ri.Cvar_Get("r_om_glass_debug", "0", 0);
		}

		if (dbg->value > 0)
		{
			if (r_newrefdef.time >= next)
			{
				next = r_newrefdef.time + 1.0f;
			}

			if (r_newrefdef.time + 1.0f >= next - 0.001f && r_newrefdef.time + 1.0f <= next + 0.001f)
			{
				R_Printf(PRINT_ALL, "OMGLASS %s verts %d copies %d drawn %d\n", image->name, nv, copies, ndrawn);
			}
		}
	}

	GL3_FX_Flush();
	GL3_Draw3DBatchesNow();

	/* the scene under it: copied at the first pane, again for a pane over one drawn since the last copy */
	ScreenBox(verts, nv, box);

	{
		qboolean over = false, inside = false;
		float need[4];
		int i;

		/* over a pane drawn since the last copy (panes side by side only touch: a margin) */
		for (i = 0; (i < ndrawn) && !over && copied && (copies <= GLASS_MAXCOPY); i++)
		{
			over = (box[0] < drawn[i][2] - 0.004f) && (box[2] > drawn[i][0] + 0.004f) &&
				(box[1] < drawn[i][3] - 0.004f) && (box[3] > drawn[i][1] + 0.004f);
		}

		/* the copy reaches as far as the refraction looks aside (0.012 of the screen, the frosting a few texels) */
		need[0] = box[0] - 0.04f;
		need[1] = box[1] - 0.04f;
		need[2] = box[2] + 0.04f;
		need[3] = box[3] + 0.04f;

		for (i = 0; (i < nhave) && !inside; i++)
		{
			inside = (need[0] >= have[i][0]) && (need[1] >= have[i][1]) && (need[2] <= have[i][2]) &&
				(need[3] <= have[i][3]);
		}

		if (!copied || over || !inside)
		{
			if (!GL3_Post_GlassCopy(need))
			{
				return false;
			}

			if (over)
			{
				ndrawn = 0;
				nhave = 0; /* the copies before hold the scene without the pane drawn since */
			}

			if (!copied || over)
			{
				copies++;
			}

			copied = true;

			if (nhave < GLASS_MAXBOX)
			{
				memcpy(have[nhave++], need, sizeof(need));
			}
		}
		else
		{
			GL3_Post_GlassBind();
		}

		if (ndrawn < GLASS_MAXBOX)
		{
			memcpy(drawn[ndrawn++], box, sizeof(drawn[0]));
		}
	}


	GL3_UseProgram(prog);
	MakeVAO();
	GL3_BindVAO(vao);
	GL3_BindVBO(vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nv * sizeof(glassvtx_t), verts, GL_STREAM_DRAW);

	GL3_SelectTMU(GL_TEXTURE0);
	GL3_Bind(image->texnum);
	glUniform1i(GL3_UniformLoc(prog, "tex"), 0);
	glUniform1i(GL3_UniformLoc(prog, "gScene"), 11);
	glUniform1i(GL3_UniformLoc(prog, "gDepth"), 7);
	GL3_Post_SoftUniforms(GL3_UniformLoc(prog, "omSoftP"), GL3_UniformLoc(prog, "omSoftV"), 0);
	glUniform4f(GL3_UniformLoc(prog, "gN"), nrm[0], nrm[1], nrm[2], 0);
	glUniform4f(GL3_UniformLoc(prog, "gEye"), r_newrefdef.vieworg[0], r_newrefdef.vieworg[1],
		r_newrefdef.vieworg[2], r_newrefdef.time);
	glUniform4fv(GL3_UniformLoc(prog, "gPane"), 1, image->omglass);
	glUniform4f(GL3_UniformLoc(prog, "gK"), Q_clamp(r_om_glass_refract->value, 0.0f, 4.0f),
		Q_clamp(r_om_glass_tint->value, 0.0f, 1.0f), Q_clamp(r_om_glass_reflect->value, 0.0f, 4.0f), 1.0f);
	glUniform4f(GL3_UniformLoc(prog, "gTexel"), 1.0f / Q_max(image->width, 1), 1.0f / Q_max(image->height, 1), 0, 0);

	/* gl_fogmode 1: the linear fog of the face's region (on the pane's own colour; the scene behind has its own) */
	GL3_FX_DefaultState(&fog);
	GL3_Fog_SetHardwareFxState(fogregion, &fog);

	if (fog.fogmode)
	{
		vec3_t vf, vr, vu;

		AngleVectors(r_newrefdef.viewangles, vf, vr, vu);
		glUniform4f(GL3_UniformLoc(prog, "fogplane"), vf[0], vf[1], vf[2], -DotProduct(vf, r_newrefdef.vieworg));
		glUniform4f(GL3_UniformLoc(prog, "gFogC"), fog.fogcolor[0], fog.fogcolor[1], fog.fogcolor[2], 1);
		glUniform4f(GL3_UniformLoc(prog, "gFogR"), fog.fogstart, fog.fogend, 0, 0);
	}
	else
	{
		glUniform4f(GL3_UniformLoc(prog, "gFogC"), 0, 0, 0, 0);
	}

	/* the lamps of the view, once a frame (their fades run per call) */
	if ((lampframe != gl3_framecount) || (lampprog != prog))
	{
		lampframe = gl3_framecount;
		lampprog = prog;
		GL3_Lamps_View(prog, r_newrefdef.vieworg);
	}

#ifndef YQ2_GL3_GLES3
	if ((probeframe != gl3_framecount) || (probeprog != prog))
	{
		probeframe = gl3_framecount;
		probeprog = prog;
		GL3_Probes_Uniforms(prog);
	}
#else
	glUniform1f(GL3_UniformLoc(prog, "pcount"), 0);
#endif

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);

	glDrawArrays(GL_TRIANGLES, 0, nv);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	return true;
}
