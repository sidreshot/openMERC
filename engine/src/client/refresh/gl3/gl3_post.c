/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: the modern post path (docs/impl/GFX-modern.md,
 * SPEC-AMEND-016). When bloom (r_om_bloom) or soft particles
 * (r_om_softparticles) are on, the 3D view of a world frame is drawn into a
 * window-sized framebuffer with two colour targets: the scene and an
 * "emissive" target that only real light sources write (SURF_LIGHT faces,
 * additive effects, self-illuminated Ghoul pieces as drawn, RF_FULLBRIGHT and
 * RF_GLOW entities; never a brightness threshold of the scene). Every 3D
 * fragment shader is wrapped at compile time so that it writes (0, 0, 0, a)
 * to the emissive target unless it sets its own value; blending then lets
 * translucent and opaque surfaces in front of a light hide its glow.
 *
 * At the end of the view the targets are resolved (MSAA), the emissive target
 * is blurred by a down/up-sample chain and the result is added to the scene
 * while it is copied into the view rectangle of the default framebuffer. The
 * HUD, menus and screen tints are drawn afterwards and are not affected.
 *
 * Soft particles: after the opaque world the depth buffer is copied into a
 * texture that the effect shader reads to fade billboards near geometry.
 */

#include "header/local.h"

#define POST_MAXLEVELS 7

gl3post_t gl3post;

static cvar_t *r_om_bloom, *r_om_bloom_threshold, *r_om_bloom_intensity, *r_om_bloom_radius;
static cvar_t *r_om_bloom_surfscale, *r_om_bloom_ghoulmin, *r_om_bloom_fullbright, *r_om_bloom_fx;
static cvar_t *r_om_bloom_fromscene, *r_om_softparticles, *r_om_softparticles_dist;
static cvar_t *r_om_softparticles_add, *r_om_post_debug, *r_om_gputime;
static cvar_t *r_om_overbright, *r_om_overbright_scale, *r_om_overbright_models;
static cvar_t *r_om_hdr, *r_om_hdr_key, *r_om_hdr_adapt, *r_om_hdr_min, *r_om_hdr_max, *r_om_hdr_saturation;
static cvar_t *r_om_hdr_overbright, *r_om_hdr_debug, *r_om_hdr_adapt_strength;

static qboolean VolFogWanted(void);
static void VolFogShutdown(void);
static qboolean BeamsWanted(void);
static void BeamsBeginView(void);
static void BeamShutdown(void);
static qboolean SSAOWanted(void);
static void SSAOShutdown(void);
static qboolean ssaomark; /* r_om_ssao runs this view: the weapon is marked in the stencil */

static struct
{
	qboolean failed;
	int w, h, samples;
	GLuint fbo, resolvefbo[2], depthfbo;
	GLuint rbcolor, rbemis, rbdepth;
	GLuint texcolor, texemis, texdepthcopy;

	int nlevels, lw[POST_MAXLEVELS], lh[POST_MAXLEVELS];
	int chainw, chainh;
	GLuint levelfbo[POST_MAXLEVELS], leveltex[POST_MAXLEVELS];

	GLuint vao;
	GLuint progdown, progup, progcomp;
	GLint down_tex, down_rect, down_half, down_mode, down_knee, down_clamp;
	GLint up_tex, up_rect, up_half;
	GLint comp_scene, comp_bloom, comp_emis, comp_rect, comp_intensity, comp_mode;
	GLint comp_hdr, comp_exp, comp_sat;

	/* r_om_hdr: float scene, log luminance with mipmaps, exposure adapted over time (1x1 ping-pong) */
	qboolean hdr;           /* the targets were made with a float scene */
	GLuint lumtex, lumfbo, exptex[2], expfbo[2];
	int expcur;
	float exptime;
	GLuint proglum, progexp;
	GLint lum_tex, lum_rect, exp_lum, exp_prev, exp_params, exp_rect, exp_neutral;

	qboolean bound, bloomthis, depthcopied;
	int vx, vy, vw, vh;
} P;

/* ------------------------------------------------------------------
 * measurement: r_om_gputime 1 prints the mean GPU time of the frames
 * (BeginFrame to the buffer swap) every 100 frames, "OMGPU <ms>", as the
 * frame rate is capped (vid_maxfps 999). Timer queries (GL 3.3 /
 * ARB_timer_query) are looked up at first use; desktop Windows only.
 * ------------------------------------------------------------------ */

#if defined(_WIN32) && !defined(YQ2_GL3_GLES3)
#include <windows.h>

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif

typedef void (APIENTRY *pfnGetQueryObjectui64v)(GLuint, GLenum, GLuint64 *);

#define GPUQ 4

static struct
{
	qboolean tried, inquery;
	pfnGetQueryObjectui64v getui64;
	GLuint q[GPUQ];
	qboolean issued[GPUQ];
	int qi, count;
	double sum;
} T;

void
GL3_Post_GpuTimer(qboolean begin)
{
	if (!r_om_gputime || !r_om_gputime->value)
	{
		if (T.inquery)
		{
			glEndQuery(GL_TIME_ELAPSED);
			T.inquery = false;
		}

		return;
	}

	if (!T.tried)
	{
		T.tried = true;
		{
			/* opengl32.dll is loaded with the context; this module does not link it */
			typedef PROC (WINAPI *pfnWglGetProcAddress)(LPCSTR);
			HMODULE gl = GetModuleHandleA("opengl32.dll");
			pfnWglGetProcAddress wgp = gl ? (pfnWglGetProcAddress)(void *)GetProcAddress(gl, "wglGetProcAddress") : NULL;

			T.getui64 = wgp ? (pfnGetQueryObjectui64v)(void *)wgp("glGetQueryObjectui64v") : NULL;
		}

		if (T.getui64)
		{
			glGenQueries(GPUQ, T.q);
		}
		else
		{
			R_Printf(PRINT_ALL, "r_om_gputime: no timer queries\n");
		}
	}

	if (!T.getui64)
	{
		return;
	}

	if (begin && !T.inquery)
	{
		/* the oldest query's result (GPUQ frames ago) */
		if (T.issued[T.qi])
		{
			GLuint64 ns = 0;

			T.getui64(T.q[T.qi], GL_QUERY_RESULT, &ns);
			T.sum += (double)ns / 1.0e6;
			T.issued[T.qi] = false;

			if (++T.count >= 100)
			{
				R_Printf(PRINT_ALL, "OMGPU %.3f\n", T.sum / T.count);
				T.sum = 0;
				T.count = 0;
			}
		}

		glBeginQuery(GL_TIME_ELAPSED, T.q[T.qi]);
		T.inquery = true;
	}
	else if (!begin && T.inquery)
	{
		glEndQuery(GL_TIME_ELAPSED);
		T.inquery = false;
		T.issued[T.qi] = true;
		T.qi = (T.qi + 1) % GPUQ;
	}
}
#else
void
GL3_Post_GpuTimer(qboolean begin)
{
	(void)begin;
}
#endif

/* ------------------------------------------------------------------
 * fragment shader wrapper (used by gl3_shaders.c and gl3_ghoulemu.c)
 * ------------------------------------------------------------------ */

static void
Append(char **buf, size_t *len, size_t *cap, const char *s)
{
	size_t n = strlen(s);

	if (*len + n + 1 > *cap)
	{
		size_t c = (*cap ? *cap : 4096);
		char *p;

		while (*len + n + 1 > c)
		{
			c *= 2;
		}

		p = realloc(*buf, c);

		if (!p)
		{
			return;
		}

		*buf = p;
		*cap = c;
	}

	memcpy(*buf + *len, s, n + 1);
	*len += n;
}

/*
 * Returns a malloc'd fragment source (without the version line): the
 * emissive output declaration, the shader with "void main()" renamed and a new
 * main that gives the emissive output its default. NULL when the shader has
 * not exactly one main (then the caller compiles it unchanged).
 */
char *
GL3_Post_WrapFragment(const char *src, const char *src2)
{
	char *joined = NULL, *out = NULL, *m;
	size_t jl = 0, jc = 0, ol = 0, oc = 0;
	const char *decl;

	Append(&joined, &jl, &jc, src ? src : "");
	Append(&joined, &jl, &jc, src2 ? src2 : "");

	if (!joined)
	{
		return NULL;
	}

	m = strstr(joined, "void main()");

	if (!m || strstr(m + 1, "void main()") || !strstr(joined, "outColor"))
	{
		free(joined);
		return NULL;
	}

	memcpy(m, "void omMn()", 11); /* same length: "void main()" */

#ifdef YQ2_GL3_GLES3
	{
		/* two outputs need explicit locations in GLSL ES */
		char *oc0 = strstr(joined, "out vec4 outColor;");

		if (!oc0)
		{
			free(joined);
			return NULL;
		}

		*oc0 = '\0';
		Append(&out, &ol, &oc, "layout(location = 1) out vec4 outEmissive;\n");
		Append(&out, &ol, &oc, joined);
		Append(&out, &ol, &oc, "layout(location = 0) out vec4 outColor;");
		Append(&out, &ol, &oc, oc0 + strlen("out vec4 outColor;"));
	}
	(void)decl;
#else
	decl = "out vec4 outEmissive;\n";
	Append(&out, &ol, &oc, decl);
	Append(&out, &ol, &oc, joined);
#endif

	Append(&out, &ol, &oc,
		"\nvoid main()\n{\n"
		"\toutEmissive = vec4(0.0, 0.0, 0.0, -1.0);\n"
		"\tomMn();\n"
		/* r_om_hdr: a float scene target does not clamp what the shader writes; alpha out of 0..1 or negative
		   colour would break blending (an 8-bit target clamped them); colour above 1 is the HDR headroom */
		"\toutColor = vec4(clamp(outColor.rgb, 0.0, 64.0), clamp(outColor.a, 0.0, 1.0));\n"
		"\tif (outEmissive.a < 0.0) outEmissive = vec4(0.0, 0.0, 0.0, outColor.a);\n"
		"}\n");

	free(joined);
	return out;
}

/* desktop GL: output locations before linking (GLSL ES uses layout qualifiers) */
void
GL3_Post_BindOutputs(GLuint prog)
{
#ifndef YQ2_GL3_GLES3
	glBindFragDataLocation(prog, 0, "outColor");
	glBindFragDataLocation(prog, 1, "outEmissive");
#else
	(void)prog;
#endif
}

/* ------------------------------------------------------------------
 * emissive sources
 * ------------------------------------------------------------------ */

/* SURF_LIGHT world faces: the emissive weight of the face (0 = none) */
float
GL3_Post_SurfEmissive(const msurface_t *surf)
{
	float v;

	if (!gl3post.bloom || !surf->texinfo || !(surf->texinfo->flags & SURF_LIGHT))
	{
		return 0.0f;
	}

	/* the light value of the face, 1000 being a strong source */
	v = surf->texinfo->value / 1000.0f;
	v = Q_max(0.35f, Q_min(v, 1.0f));

	return v * r_om_bloom_surfscale->value;
}

/*
 * omEmis of the effect shader and the Ghoul emulation shader:
 * (x, y, z, mode); mode 0 = (0, 0, 0, a), 1 = colour * x, 2 = constant xyz.
 * Multiplying blends get the neutral constant of their blend function.
 */
void
GL3_Post_EmisForBlend(GLenum src, GLenum dst, qboolean blend, float glow, float out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0.0f;

	if (!gl3post.bloom || !blend)
	{
		if (gl3post.bloom && (glow > 0.0f))
		{
			out[0] = glow;
			out[3] = 1.0f;
		}

		return;
	}

	if ((dst == GL_ONE) && ((src == GL_ONE) || (src == GL_SRC_ALPHA)))
	{
		/* additive: a light by its nature */
		out[0] = (glow > 0.0f) ? glow : r_om_bloom_fx->value;
		out[3] = 1.0f;
	}
	else if (src == GL_DST_COLOR)
	{
		/* dst * src (ZERO) or 2 dst * src (SRC_COLOR) */
		out[0] = out[1] = out[2] = (dst == GL_SRC_COLOR) ? 0.5f : 1.0f;
		out[3] = 2.0f;
	}
	else if ((src == GL_ZERO) && (dst == GL_SRC_COLOR))
	{
		out[0] = out[1] = out[2] = 1.0f;
		out[3] = 2.0f;
	}
	else if (src == GL_ZERO)
	{
		/* dst * (1 - src): 0 keeps the glow */
		out[3] = 2.0f;
	}
	else if (glow > 0.0f)
	{
		out[0] = glow;
		out[3] = 1.0f;
	}
}

void
GL3_Post_FxEmis(int blend, float out[4])
{
	switch (blend)
	{
		case 0:
			GL3_Post_EmisForBlend(GL_ONE, GL_ZERO, false, 0, out);
			break;
		case 2:
			GL3_Post_EmisForBlend(GL_ONE, GL_ONE, true, 0, out);
			break;
		case 3:
			GL3_Post_EmisForBlend(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, true, 0, out);
			break;
		case 4:
			GL3_Post_EmisForBlend(GL_DST_COLOR, GL_ZERO, true, 0, out);
			break;
		case 5:
			GL3_Post_EmisForBlend(GL_DST_COLOR, GL_SRC_COLOR, true, 0, out);
			break;
		default:
			GL3_Post_EmisForBlend(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, true, 0, out);
			break;
	}
}

/*
 * SoF Buddy-style overbright (GFX-modern.md section 11, non-original option): the lightmap light is
 * multiplied after the colour table (1.0 = authentic, SPEC-410); the frame clamps the result
 */
/* r_om_hdr: float scene target, exposure and filmic curve (not in GLES3 builds) */
static qboolean
HDRWanted(void)
{
#ifdef YQ2_GL3_GLES3
	return false;
#else
	return r_om_hdr && (r_om_hdr->value != 0);
#endif
}

float
GL3_Post_Overbright(void)
{
	float s;

	if (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES)
	{
		return 1.0f;
	}

	/* r_om_hdr: the lightmaps get headroom above white, the filmic curve compresses it instead of clipping */
	if (HDRWanted() && !P.failed && (r_fullbright->value == 0))
	{
		s = r_om_hdr_overbright->value;
		return (s < 1.0f) ? 1.0f : ((s > 4.0f) ? 4.0f : s);
	}

	if (!r_om_overbright || !r_om_overbright->value)
	{
		return 1.0f;
	}

	s = r_om_overbright_scale->value;
	return (s < 1.0f) ? 1.0f : ((s > 4.0f) ? 4.0f : s);
}

float
GL3_Post_OverbrightModels(void)
{
	return (r_om_overbright_models && r_om_overbright_models->value) ? GL3_Post_Overbright() : 1.0f;
}

float
GL3_Post_GhoulMin(void)
{
	return r_om_bloom_ghoulmin ? r_om_bloom_ghoulmin->value : 0.5f;
}

float
GL3_Post_FullbrightScale(void)
{
	return r_om_bloom_fullbright ? r_om_bloom_fullbright->value : 0.0f;
}

/* soft particle fade for an effect batch: 0 none, 1 alpha, 2 colour (additive / darkening blends) */
int
GL3_Post_SoftMode(int blend, qboolean billboard, qboolean depthtest)
{
	if (!gl3post.soft || !billboard || !depthtest)
	{
		return 0;
	}

	if (blend == 1)
	{
		return 1;
	}

	if (((blend == 2) || (blend == 3)) && r_om_softparticles_add->value)
	{
		return 2;
	}

	return 0;
}

void
GL3_Post_SoftUniforms(GLint uparams, GLint uview, int mode)
{
	float d = Q_max(r_om_softparticles_dist->value, 1.0f);

	/* depth of the window coordinate z -> eye distance D / (ndc + C) */
	glUniform4f(uparams, gl3state.projMat3D.Elements[2][2], gl3state.projMat3D.Elements[3][2],
		gl3depthmin, gl3depthmax);
	glUniform4f(uview, 1.0f / (float)P.w, 1.0f / (float)P.h, d, (float)mode);
}

/* ------------------------------------------------------------------
 * GL objects
 * ------------------------------------------------------------------ */

static const char *post_vs =
	"uniform vec4 rect;\n"
	"out vec2 uv;\n"
	"out vec2 vuv;\n"
	"void main()\n"
	"{\n"
	"	vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
	"	vuv = p * 0.5 + 0.5;\n"
	"	uv = rect.xy + vuv * rect.zw;\n"
	"	gl_Position = vec4(p, 0.0, 1.0);\n"
	"}\n";

static const char *post_down_fs =
	"uniform sampler2D tex;\n"
	"uniform vec2 halfpx;\n"
	"uniform int mode;\n"   /* 0 plain, 1 emissive knee, 2 scene threshold (debug) */
	"uniform float knee;\n"
	"uniform vec4 clampuv;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"vec3 S(vec2 p) { return texture(tex, clamp(p, clampuv.xy, clampuv.zw)).rgb; }\n"
	"void main()\n"
	"{\n"
	"	vec3 c = S(uv) * 4.0 + S(uv - halfpx) + S(uv + halfpx) + S(uv + vec2(halfpx.x, -halfpx.y)) +\n"
	"		S(uv - vec2(halfpx.x, -halfpx.y));\n"
	"	c *= 0.125;\n"
	"	if (mode == 1) c = max(c - vec3(knee), vec3(0.0)) / max(1.0 - knee, 0.05);\n"
	"	else if (mode == 2) { float l = max(c.r, max(c.g, c.b)); c *= clamp((l - 0.8) * 5.0, 0.0, 1.0); }\n"
	"	outColor = vec4(c, 1.0);\n"
	"}\n";

static const char *post_up_fs =
	"uniform sampler2D tex;\n"
	"uniform vec2 halfpx;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec3 c = texture(tex, uv + vec2(-halfpx.x * 2.0, 0.0)).rgb;\n"
	"	c += texture(tex, uv + vec2(-halfpx.x, halfpx.y)).rgb * 2.0;\n"
	"	c += texture(tex, uv + vec2(0.0, halfpx.y * 2.0)).rgb;\n"
	"	c += texture(tex, uv + vec2(halfpx.x, halfpx.y)).rgb * 2.0;\n"
	"	c += texture(tex, uv + vec2(halfpx.x * 2.0, 0.0)).rgb;\n"
	"	c += texture(tex, uv + vec2(halfpx.x, -halfpx.y)).rgb * 2.0;\n"
	"	c += texture(tex, uv + vec2(0.0, -halfpx.y * 2.0)).rgb;\n"
	"	c += texture(tex, uv + vec2(-halfpx.x, -halfpx.y)).rgb * 2.0;\n"
	"	outColor = vec4(c / 12.0, 1.0);\n"
	"}\n";

static const char *post_comp_fs =
	"uniform sampler2D scene;\n"
	"uniform sampler2D bloom;\n"
	"uniform sampler2D emis;\n"
	"uniform sampler2D exptex;\n"
	"uniform float intensity;\n"
	"uniform int mode;\n"    /* 0 scene, 1 scene + bloom, 2 emissive target, 3 bloom only */
	"uniform int hdr;\n"     /* r_om_hdr: exposure and the filmic curve */
	"uniform float saturation;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	/* ACES filmic curve (Narkowicz fit): soft shoulder for the highlights, a little more contrast in the mids */
	"vec3 Aces(vec3 x) { return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n"
	"void main()\n"
	"{\n"
	"	vec3 c;\n"
	"	if (mode == 2) c = texture(emis, uv).rgb;\n"
	"	else if (mode == 3) c = texture(bloom, vuv).rgb * intensity;\n"
	"	else\n"
	"	{\n"
	"		c = texture(scene, uv).rgb;\n"
	"		if (mode == 1) c += texture(bloom, vuv).rgb * intensity;\n"
	"		if (hdr != 0)\n"
	"		{\n"
	/* the scene is display-encoded (the shaders applied vid_gamma): to linear, expose, curve, back */
	"			vec3 l = pow(max(c, vec3(0.0)), vec3(2.2)) * texture(exptex, vec2(0.5)).r;\n"
	"			l = Aces(l);\n"
	"			float y = dot(l, vec3(0.2126, 0.7152, 0.0722));\n"
	"			l = max(mix(vec3(y), l, saturation), vec3(0.0));\n"
	"			c = pow(l, vec3(1.0 / 2.2));\n"
	"		}\n"
	"	}\n"
	"	outColor = vec4(c, 1.0);\n"
	"}\n";

/* r_om_hdr: log luminance of the view (linear), averaged by the mipmaps */
static const char *post_lum_fs =
	"uniform sampler2D tex;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec3 l = pow(max(texture(tex, uv).rgb, vec3(0.0)), vec3(2.2));\n"
	/* the centre counts more (what one looks at), the edges less */
	"	vec2 d = vuv - 0.5;\n"
	"	float w = 1.0 - dot(d, d) * 1.6;\n"
	"	outColor = vec4(log(max(dot(l, vec3(0.2126, 0.7152, 0.0722)), 1e-4)) * w, w, 0.0, 1.0);\n"
	"}\n";

/* r_om_hdr: the exposure this frame from the mean log luminance, eased towards over time */
static const char *post_exp_fs =
	"uniform sampler2D lum;\n"
	"uniform sampler2D prev;\n"
	"uniform vec4 params;\n"   /* key, ease factor (0..1), min, max of the adaptation */
	"uniform vec2 neutral;\n"  /* exposure that keeps the original mid tones, adaptation strength */
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec2 m = textureLod(lum, vec2(0.5), 16.0).rg;\n"
	"	float avg = exp(m.x / max(m.y, 1e-4)) * neutral.x;\n"
	/* gentle: dark places are lifted a little, bright ones brought down a little, the map's mood stays */
	"	float target = neutral.x * clamp(pow(params.x / max(avg, 1e-5), neutral.y), params.z, params.w);\n"
	"	float p = texture(prev, vec2(0.5)).r;\n"
	"	outColor = vec4(p <= 0.0 ? target : mix(p, target, params.y), 0.0, 0.0, 1.0);\n"
	"}\n";

static GLuint
CompilePost(GLenum type, const char *src)
{
#ifdef YQ2_GL3_GLES3
	const char *version = "#version 300 es\nprecision highp float;\n";
#else
	const char *version = "#version 150\n";
#endif
	const char *srcs[2] = {version, src};
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 2, srcs, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "GL3 post: shader compile failed: %s\n", log);
		glDeleteShader(s);
		return 0;
	}

	return s;
}

static GLuint
LinkPost(const char *fs)
{
	GLuint v = CompilePost(GL_VERTEX_SHADER, post_vs), f = CompilePost(GL_FRAGMENT_SHADER, fs), p;
	GLint ok = 0;

	if (!v || !f)
	{
		return 0;
	}

	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
#ifndef YQ2_GL3_GLES3
	glBindFragDataLocation(p, 0, "outColor");
#endif
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "GL3 post: link failed: %s\n", log);
		glDeleteProgram(p);
		return 0;
	}

	return p;
}

static qboolean
CreatePrograms(void)
{
	if (P.progcomp)
	{
		return true;
	}

	P.progdown = LinkPost(post_down_fs);
	P.progup = LinkPost(post_up_fs);
	P.progcomp = LinkPost(post_comp_fs);
	P.proglum = LinkPost(post_lum_fs);
	P.progexp = LinkPost(post_exp_fs);

	if (!P.progdown || !P.progup || !P.progcomp || !P.proglum || !P.progexp)
	{
		return false;
	}

	P.comp_hdr = glGetUniformLocation(P.progcomp, "hdr");
	P.comp_exp = glGetUniformLocation(P.progcomp, "exptex");
	P.comp_sat = glGetUniformLocation(P.progcomp, "saturation");
	P.lum_tex = glGetUniformLocation(P.proglum, "tex");
	P.lum_rect = glGetUniformLocation(P.proglum, "rect");
	P.exp_lum = glGetUniformLocation(P.progexp, "lum");
	P.exp_prev = glGetUniformLocation(P.progexp, "prev");
	P.exp_params = glGetUniformLocation(P.progexp, "params");
	P.exp_rect = glGetUniformLocation(P.progexp, "rect");
	P.exp_neutral = glGetUniformLocation(P.progexp, "neutral");

	P.down_tex = glGetUniformLocation(P.progdown, "tex");
	P.down_rect = glGetUniformLocation(P.progdown, "rect");
	P.down_half = glGetUniformLocation(P.progdown, "halfpx");
	P.down_mode = glGetUniformLocation(P.progdown, "mode");
	P.down_knee = glGetUniformLocation(P.progdown, "knee");
	P.down_clamp = glGetUniformLocation(P.progdown, "clampuv");
	P.up_tex = glGetUniformLocation(P.progup, "tex");
	P.up_rect = glGetUniformLocation(P.progup, "rect");
	P.up_half = glGetUniformLocation(P.progup, "halfpx");
	P.comp_scene = glGetUniformLocation(P.progcomp, "scene");
	P.comp_bloom = glGetUniformLocation(P.progcomp, "bloom");
	P.comp_emis = glGetUniformLocation(P.progcomp, "emis");
	P.comp_rect = glGetUniformLocation(P.progcomp, "rect");
	P.comp_intensity = glGetUniformLocation(P.progcomp, "intensity");
	P.comp_mode = glGetUniformLocation(P.progcomp, "mode");

	glGenVertexArrays(1, &P.vao);
	return true;
}

static void
DeleteTargets(void)
{
	int i;

	if (P.fbo) glDeleteFramebuffers(1, &P.fbo);
	if (P.resolvefbo[0]) glDeleteFramebuffers(2, P.resolvefbo);
	if (P.depthfbo) glDeleteFramebuffers(1, &P.depthfbo);
	if (P.rbcolor) glDeleteRenderbuffers(1, &P.rbcolor);
	if (P.rbemis) glDeleteRenderbuffers(1, &P.rbemis);
	if (P.rbdepth) glDeleteRenderbuffers(1, &P.rbdepth);
	if (P.texcolor) glDeleteTextures(1, &P.texcolor);
	if (P.texemis) glDeleteTextures(1, &P.texemis);
	if (P.texdepthcopy) glDeleteTextures(1, &P.texdepthcopy);
	if (P.lumfbo) glDeleteFramebuffers(1, &P.lumfbo);
	if (P.lumtex) glDeleteTextures(1, &P.lumtex);
	if (P.expfbo[0]) glDeleteFramebuffers(2, P.expfbo);
	if (P.exptex[0]) glDeleteTextures(2, P.exptex);
	P.lumfbo = P.lumtex = P.expfbo[0] = P.expfbo[1] = P.exptex[0] = P.exptex[1] = 0;

	for (i = 0; i < POST_MAXLEVELS; i++)
	{
		if (P.levelfbo[i]) glDeleteFramebuffers(1, &P.levelfbo[i]);
		if (P.leveltex[i]) glDeleteTextures(1, &P.leveltex[i]);
		P.levelfbo[i] = P.leveltex[i] = 0;
	}

	P.fbo = P.resolvefbo[0] = P.resolvefbo[1] = P.depthfbo = 0;
	P.rbcolor = P.rbemis = P.rbdepth = 0;
	P.texcolor = P.texemis = P.texdepthcopy = 0;
	P.w = P.h = 0;
	P.nlevels = 0;
	P.chainw = P.chainh = 0;
}

static GLuint
NewTexture(GLenum ifmt, GLenum fmt, GLenum type, int w, int h, GLenum filter)
{
	GLuint t;

	glGenTextures(1, &t);
	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, type, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl3state.currenttexture = t;
	return t;
}

static GLuint
NewRenderbuffer(GLenum fmt, int w, int h, int samples)
{
	GLuint r;

	glGenRenderbuffers(1, &r);
	glBindRenderbuffer(GL_RENDERBUFFER, r);

	if (samples > 0)
	{
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, fmt, w, h);
	}
	else
	{
		glRenderbufferStorage(GL_RENDERBUFFER, fmt, w, h);
	}

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	return r;
}

static qboolean
Complete(const char *what)
{
	GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (st != GL_FRAMEBUFFER_COMPLETE)
	{
		R_Printf(PRINT_ALL, "GL3 post: %s framebuffer incomplete (0x%x), post effects off\n", what, st);
		return false;
	}

	return true;
}

static qboolean
CreateTargets(int w, int h)
{
	static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	static const GLenum none[1] = {GL_NONE};
	GLint samples = 0;

	DeleteTargets();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glGetIntegerv(GL_SAMPLES, &samples);
	P.samples = (samples > 1) ? samples : 0;
	P.w = w;
	P.h = h;

	P.hdr = HDRWanted();

	if (P.hdr)
	{
		P.texcolor = NewTexture(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, w, h, GL_LINEAR);
	}
	else
	{
		P.texcolor = NewTexture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h, GL_LINEAR);
	}

	P.texemis = NewTexture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h, GL_LINEAR);
	P.rbdepth = NewRenderbuffer(GL_DEPTH24_STENCIL8, w, h, P.samples);

	glGenFramebuffers(1, &P.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);

	if (P.samples)
	{
		P.rbcolor = NewRenderbuffer(P.hdr ? GL_RGBA16F : GL_RGBA8, w, h, P.samples);
		P.rbemis = NewRenderbuffer(GL_RGBA8, w, h, P.samples);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, P.rbcolor);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, P.rbemis);
	}
	else
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.texcolor, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, P.texemis, 0);
	}

	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, P.rbdepth);
	glDrawBuffers(2, bufs);

	if (!Complete("scene"))
	{
		return false;
	}

	glClearColor(0, 0, 0, 0);
	glDepthMask(GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glClearColor(1, 0, 0.5, 0.5); /* SPEC-413 default clear colour */

	if (P.samples)
	{
		int i;

		glGenFramebuffers(2, P.resolvefbo);

		for (i = 0; i < 2; i++)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, P.resolvefbo[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
				i ? P.texemis : P.texcolor, 0);
			glDrawBuffers(1, one);

			if (!Complete("resolve"))
			{
				return false;
			}
		}
	}

	/* r_om_hdr: 64x64 log luminance with mipmaps, two 1x1 exposures */
	if (P.hdr)
	{
		static const float zero[4] = {0, 0, 0, 0};
		int i;

		P.lumtex = NewTexture(GL_RG16F, GL_RG, GL_HALF_FLOAT, 64, 64, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glGenerateMipmap(GL_TEXTURE_2D);
		glGenFramebuffers(1, &P.lumfbo);
		glBindFramebuffer(GL_FRAMEBUFFER, P.lumfbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.lumtex, 0);
		glDrawBuffers(1, one);

		if (!Complete("hdr luminance"))
		{
			return false;
		}

		for (i = 0; i < 2; i++)
		{
			P.exptex[i] = NewTexture(GL_R32F, GL_RED, GL_FLOAT, 1, 1, GL_NEAREST);
			glGenFramebuffers(1, &P.expfbo[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, P.expfbo[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.exptex[i], 0);
			glDrawBuffers(1, one);

			if (!Complete("hdr exposure"))
			{
				return false;
			}

			glClearBufferfv(GL_COLOR, 0, zero); /* 0 = take the first measurement as it is */
		}

		P.expcur = 0;
	}

	/* soft particles: depth copy */
	glGenTextures(1, &P.texdepthcopy);
	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl3state.currenttexture = P.texdepthcopy;

	glGenFramebuffers(1, &P.depthfbo);
	glBindFramebuffer(GL_FRAMEBUFFER, P.depthfbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, P.texdepthcopy, 0);
	glDrawBuffers(1, none);
	glReadBuffer(GL_NONE);

	if (!Complete("depth copy"))
	{
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return true;
}

static qboolean
CreateChain(int vw, int vh)
{
	int i, w = vw / 2, h = vh / 2;
	int want = (int)(4.0f + (r_om_bloom_radius->value > 0 ? r_om_bloom_radius->value : 0.0f) + 0.5f);
#ifdef YQ2_GL3_GLES3
	GLenum ifmt = GL_RGBA8, type = GL_UNSIGNED_BYTE;
#else
	GLenum ifmt = GL_RGBA16F, type = GL_HALF_FLOAT;
#endif

	for (i = 0; i < POST_MAXLEVELS; i++)
	{
		if (P.levelfbo[i]) glDeleteFramebuffers(1, &P.levelfbo[i]);
		if (P.leveltex[i]) glDeleteTextures(1, &P.leveltex[i]);
		P.levelfbo[i] = P.leveltex[i] = 0;
	}

	want = Q_max(2, Q_min(want, POST_MAXLEVELS));
	P.nlevels = 0;

	for (i = 0; i < want; i++)
	{
		static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};

		if ((w < 4) || (h < 4))
		{
			break;
		}

		P.lw[i] = w;
		P.lh[i] = h;
		P.leveltex[i] = NewTexture(ifmt, GL_RGBA, type, w, h, GL_LINEAR);
		glGenFramebuffers(1, &P.levelfbo[i]);
		glBindFramebuffer(GL_FRAMEBUFFER, P.levelfbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.leveltex[i], 0);
		glDrawBuffers(1, one);

		if (!Complete("bloom"))
		{
			return false;
		}

		P.nlevels++;
		w /= 2;
		h /= 2;
	}

	P.chainw = vw;
	P.chainh = vh;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return P.nlevels > 0;
}

/* ------------------------------------------------------------------
 * frame
 * ------------------------------------------------------------------ */

void
GL3_Post_Init(void)
{
	r_om_bloom = ri.Cvar_Get("r_om_bloom", "1", CVAR_ARCHIVE);
	r_om_bloom_threshold = ri.Cvar_Get("r_om_bloom_threshold", "0", CVAR_ARCHIVE);
	r_om_bloom_intensity = ri.Cvar_Get("r_om_bloom_intensity", "0.45", CVAR_ARCHIVE);
	r_om_bloom_radius = ri.Cvar_Get("r_om_bloom_radius", "1", CVAR_ARCHIVE);
	r_om_bloom_surfscale = ri.Cvar_Get("r_om_bloom_surfscale", "1", CVAR_ARCHIVE);
	r_om_bloom_ghoulmin = ri.Cvar_Get("r_om_bloom_ghoulmin", "0.5", CVAR_ARCHIVE);
	r_om_bloom_fullbright = ri.Cvar_Get("r_om_bloom_fullbright", "1", CVAR_ARCHIVE);
	r_om_bloom_fx = ri.Cvar_Get("r_om_bloom_fx", "1", CVAR_ARCHIVE);
	r_om_bloom_fromscene = ri.Cvar_Get("r_om_bloom_fromscene", "0", CVAR_ARCHIVE);
	r_om_softparticles = ri.Cvar_Get("r_om_softparticles", "0", CVAR_ARCHIVE);
	r_om_softparticles_dist = ri.Cvar_Get("r_om_softparticles_dist", "12", CVAR_ARCHIVE);
	r_om_softparticles_add = ri.Cvar_Get("r_om_softparticles_add", "1", CVAR_ARCHIVE);
	r_om_post_debug = ri.Cvar_Get("r_om_post_debug", "0", 0);
	r_om_gputime = ri.Cvar_Get("r_om_gputime", "0", 0);
	r_om_overbright = ri.Cvar_Get("r_om_overbright", "0", CVAR_ARCHIVE);
	r_om_overbright_scale = ri.Cvar_Get("r_om_overbright_scale", "2", CVAR_ARCHIVE);
	r_om_overbright_models = ri.Cvar_Get("r_om_overbright_models", "1", CVAR_ARCHIVE);
	r_om_hdr = ri.Cvar_Get("r_om_hdr", "1", CVAR_ARCHIVE);
	r_om_hdr_key = ri.Cvar_Get("r_om_hdr_key", "0.1", CVAR_ARCHIVE);
	r_om_hdr_adapt = ri.Cvar_Get("r_om_hdr_adapt", "1.5", CVAR_ARCHIVE);
	r_om_hdr_adapt_strength = ri.Cvar_Get("r_om_hdr_adapt_strength", "0.35", CVAR_ARCHIVE);
	r_om_hdr_min = ri.Cvar_Get("r_om_hdr_min", "0.7", CVAR_ARCHIVE);
	r_om_hdr_max = ri.Cvar_Get("r_om_hdr_max", "1.6", CVAR_ARCHIVE);
	r_om_hdr_saturation = ri.Cvar_Get("r_om_hdr_saturation", "1.1", CVAR_ARCHIVE);
	r_om_hdr_overbright = ri.Cvar_Get("r_om_hdr_overbright", "1", CVAR_ARCHIVE);
	r_om_hdr_debug = ri.Cvar_Get("r_om_hdr_debug", "0", 0);

	memset(&P, 0, sizeof(P));
	memset(&gl3post, 0, sizeof(gl3post));
}

void
GL3_Post_Shutdown(void)
{
	DeleteTargets();

	if (P.progdown) glDeleteProgram(P.progdown);
	if (P.progup) glDeleteProgram(P.progup);
	if (P.progcomp) glDeleteProgram(P.progcomp);
	if (P.proglum) glDeleteProgram(P.proglum);
	if (P.progexp) glDeleteProgram(P.progexp);
	if (P.vao) glDeleteVertexArrays(1, &P.vao);
	VolFogShutdown();
	BeamShutdown();
	SSAOShutdown();

	if ((gl3state.currentShaderProgram == P.progdown) || (gl3state.currentShaderProgram == P.progup) ||
		(gl3state.currentShaderProgram == P.progcomp))
	{
		gl3state.currentShaderProgram = 0;
	}

	if (gl3state.currentVAO == P.vao)
	{
		gl3state.currentVAO = 0;
	}

	memset(&P, 0, sizeof(P));
	memset(&gl3post, 0, sizeof(gl3post));
}

/* the view rectangle (window coordinates, GL origin bottom left); true when the post path draws this view */
qboolean
GL3_Post_BeginView(int x, int y, int w, int h)
{
	static const float zero[4] = {0, 0, 0, 0};
	qboolean nv = (r_fullbright->value != 0) || (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES);
	qboolean bloom, soft, volfog, beams, ssao;

	gl3post.active = gl3post.bloom = gl3post.soft = gl3post.volfog = false;
	P.bound = P.depthcopied = false;
	ssaomark = false;
	BeamsBeginView();

	if (P.failed || !r_om_bloom || (r_newrefdef.rdflags & RDF_NOWORLDMODEL) || !gl3_worldmodel)
	{
		return false;
	}

	bloom = (r_om_bloom->value != 0) && !nv;
	soft = (r_om_softparticles->value != 0);
	volfog = VolFogWanted() && !nv;
	beams = BeamsWanted() && !nv;
	ssao = SSAOWanted() && !nv;

	if (!bloom && !soft && !volfog && !beams && !ssao && !HDRWanted() && !r_om_post_debug->value)
	{
		return false;
	}

	if ((w <= 0) || (h <= 0))
	{
		return false;
	}

	if (!CreatePrograms())
	{
		P.failed = true;
		return false;
	}

	if ((P.w != vid.width) || (P.h != vid.height) || !P.fbo || (P.hdr != HDRWanted()))
	{
		if (!CreateTargets(vid.width, vid.height))
		{
			DeleteTargets();
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			P.failed = true;
			return false;
		}
	}

	if (bloom && ((P.chainw != w) || (P.chainh != h) || !P.nlevels))
	{
		if (!CreateChain(w, h))
		{
			bloom = false;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	glViewport(x, y, w, h);
	glDepthMask(GL_TRUE);

	if (gl_clear->value)
	{
		float c[4] = {0, 0, 0, 0};

		if (gl_ztrick->value)
		{
			c[0] = 1;
			c[2] = c[3] = 0.5f;
		}

		glClearBufferfv(GL_COLOR, 0, c);
	}

	/* gl_ztrick does not clear the default framebuffer; frames without this path would leave
	   stale depth here, so it is cleared to the far value of this frame's depth range */
	{
		float zfar = (gl3depthmin > gl3depthmax) ? 0.0f : 1.0f;

		/* stencil too: r_om_ssao marks the view weapon there */
		glStencilMask(0xff);
		glClearBufferfi(GL_DEPTH_STENCIL, 0, zfar, 0);
	}

	glClearBufferfv(GL_COLOR, 1, zero);

	P.bound = true;
	P.bloomthis = bloom;
	P.vx = x;
	P.vy = y;
	P.vw = w;
	P.vh = h;
	gl3post.active = true;
	gl3post.bloom = bloom || (r_om_post_debug->value != 0);
	gl3post.soft = soft;
	gl3post.volfog = volfog;
	ssaomark = ssao;
	return true;
}

/* after the opaque world: the depth buffer for soft particles (texture unit 7) */
void
GL3_Post_DepthCopy(void)
{
	if (!P.bound || !(gl3post.soft || gl3post.volfog) || P.depthcopied)
	{
		return;
	}

	GL3_Draw3DBatchesNow();
	glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.depthfbo);
	glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
		GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);

	GL3_SelectTMU(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);
	GL3_SelectTMU(GL_TEXTURE0);
	P.depthcopied = true;
}

qboolean
GL3_Post_DepthReady(void)
{
	return P.bound && P.depthcopied;
}

static void
DrawQuad(void)
{
	GL3_BindVAO(P.vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void
BindTex(GLenum unit, GLuint tex)
{
	GL3_SelectTMU(unit);
	glBindTexture(GL_TEXTURE_2D, tex);

	if (unit == GL_TEXTURE0)
	{
		gl3state.currenttexture = tex;
	}
}

static void
Bloom(void)
{
	int i;
	float fw = (float)P.w, fh = (float)P.h;

	/* first level: the emissive target (or the scene for the debug threshold) inside the view */
	GL3_UseProgram(P.progdown);
	glUniform1i(P.down_tex, 0);
	glDisable(GL_BLEND);

	for (i = 0; i < P.nlevels; i++)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, P.levelfbo[i]);
		glViewport(0, 0, P.lw[i], P.lh[i]);

		if (i == 0)
		{
			qboolean scene = (r_om_bloom_fromscene->value != 0);

			BindTex(GL_TEXTURE0, scene ? P.texcolor : P.texemis);
			glUniform4f(P.down_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
			glUniform2f(P.down_half, 1.0f / fw, 1.0f / fh);
			glUniform1i(P.down_mode, scene ? 2 : 1);
			glUniform1f(P.down_knee, Q_max(0.0f, Q_min(r_om_bloom_threshold->value, 0.95f)));
			glUniform4f(P.down_clamp, (P.vx + 0.5f) / fw, (P.vy + 0.5f) / fh, (P.vx + P.vw - 0.5f) / fw,
				(P.vy + P.vh - 0.5f) / fh);
		}
		else
		{
			BindTex(GL_TEXTURE0, P.leveltex[i - 1]);
			glUniform4f(P.down_rect, 0, 0, 1, 1);
			glUniform2f(P.down_half, 1.0f / P.lw[i - 1], 1.0f / P.lh[i - 1]);
			glUniform1i(P.down_mode, 0);
			glUniform4f(P.down_clamp, 0.5f / P.lw[i - 1], 0.5f / P.lh[i - 1], 1.0f - 0.5f / P.lw[i - 1],
				1.0f - 0.5f / P.lh[i - 1]);
		}

		DrawQuad();
	}

	/* up the chain, each level adds the blurred smaller one */
	GL3_UseProgram(P.progup);
	glUniform1i(P.up_tex, 0);
	glUniform4f(P.up_rect, 0, 0, 1, 1);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	for (i = P.nlevels - 2; i >= 0; i--)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, P.levelfbo[i]);
		glViewport(0, 0, P.lw[i], P.lh[i]);
		BindTex(GL_TEXTURE0, P.leveltex[i + 1]);
		glUniform2f(P.up_half, 0.5f / P.lw[i + 1], 0.5f / P.lh[i + 1]);
		DrawQuad();
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
}

/* r_om_hdr: mean log luminance of the view, then the exposure eased towards key / mean */
static void
HDRExposure(void)
{
	float fw = (float)P.w, fh = (float)P.h, now = r_newrefdef.time, dt, ease;
	int next = P.expcur ^ 1;

	dt = now - P.exptime;
	if ((dt < 0) || (dt > 0.5f))
	{
		dt = 0.5f;
	}
	P.exptime = now;
	ease = 1.0f - expf(-dt * Q_max(r_om_hdr_adapt->value, 0.01f));

	glDisable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, P.lumfbo);
	glViewport(0, 0, 64, 64);
	GL3_UseProgram(P.proglum);
	glUniform1i(P.lum_tex, 0);
	glUniform4f(P.lum_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	BindTex(GL_TEXTURE0, P.texcolor);
	DrawQuad();
	BindTex(GL_TEXTURE0, P.lumtex);
	glGenerateMipmap(GL_TEXTURE_2D);

	glBindFramebuffer(GL_FRAMEBUFFER, P.expfbo[next]);
	glViewport(0, 0, 1, 1);
	GL3_UseProgram(P.progexp);
	glUniform1i(P.exp_lum, 0);
	glUniform1i(P.exp_prev, 1);
	glUniform4f(P.exp_rect, 0, 0, 1, 1);
	glUniform4f(P.exp_params, Q_max(r_om_hdr_key->value, 0.01f), ease, Q_max(r_om_hdr_min->value, 0.05f),
		Q_max(r_om_hdr_max->value, r_om_hdr_min->value));
	/* ACES(0.13) ~ 0.18: 0.72 keeps a mid grey; the lightmap headroom (overbright, display-encoded) is taken back */
	glUniform2f(P.exp_neutral, 0.72f / powf(GL3_Post_Overbright(), 2.2f), Q_max(r_om_hdr_adapt_strength->value, 0.0f));
	BindTex(GL_TEXTURE1, P.exptex[P.expcur]);
	BindTex(GL_TEXTURE0, P.lumtex);
	DrawQuad();
	P.expcur = next;

	if (r_om_hdr_debug->value)
	{
		static int frames;

		if ((++frames % 30) == 0)
		{
			float e = 0;

			glReadPixels(0, 0, 1, 1, GL_RED, GL_FLOAT, &e);
			R_Printf(PRINT_ALL, "OMHDR exposure %.3f\n", e);
		}
	}
}

/* end of the 3D view: resolve, bloom and composite into the default framebuffer */
void
GL3_Post_EndView(void)
{
	float fw, fh;
	int mode, dbg;
	qboolean hdr;

	if (!P.bound)
	{
		return;
	}

	GL3_Draw3DBatchesNow();
	GL3_FX_Flush();
	P.bound = false;

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_FALSE);

	dbg = (int)r_om_post_debug->value;

	if (P.samples)
	{
		int i;

		for (i = 0; i < ((P.bloomthis || dbg) ? 2 : 1); i++)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
			glReadBuffer(i ? GL_COLOR_ATTACHMENT1 : GL_COLOR_ATTACHMENT0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.resolvefbo[i]);
			glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
				GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}

		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	}

	if (P.bloomthis)
	{
		Bloom();
	}

	hdr = P.hdr && P.lumfbo && !(r_newrefdef.rdflags & SOF_RDF_IRGOGGLES) && (r_fullbright->value == 0);

	if (hdr)
	{
		HDRExposure();
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(P.vx, P.vy, P.vw, P.vh);

	fw = (float)P.w;
	fh = (float)P.h;
	mode = P.bloomthis ? 1 : 0;

	if (dbg == 1)
	{
		mode = 2;
	}
	else if ((dbg == 2) && P.bloomthis)
	{
		mode = 3;
	}

	GL3_UseProgram(P.progcomp);
	glUniform1i(P.comp_scene, 0);
	glUniform1i(P.comp_bloom, 1);
	glUniform1i(P.comp_emis, 2);
	glUniform4f(P.comp_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	glUniform1f(P.comp_intensity, r_om_bloom_intensity->value * 2.0f / (float)Q_max(P.nlevels, 1));
	glUniform1i(P.comp_mode, mode);
	glUniform1i(P.comp_hdr, hdr ? 1 : 0);
	glUniform1i(P.comp_exp, 3);
	glUniform1f(P.comp_sat, r_om_hdr_saturation->value);
	if (hdr)
	{
		BindTex(GL_TEXTURE3, P.exptex[P.expcur]);
	}
	BindTex(GL_TEXTURE2, P.texemis);
	BindTex(GL_TEXTURE1, P.nlevels ? P.leveltex[0] : P.texemis);
	BindTex(GL_TEXTURE0, P.texcolor);
	DrawQuad();

	/* lightmap units were used: let the next lightmapped draw bind its pages again */
	gl3state.currentlightmap = -1;
	GL3_SelectTMU(GL_TEXTURE0);

	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	gl3post.active = gl3post.bloom = gl3post.soft = gl3post.volfog = false;
}
/* ------------------------------------------------------------------
 * GFX-modern r_om_volfog (docs/impl/GFX-modern.md 3): volumetric fog in
 * the SoF fog regions. After the opaque world and its overlay pass, a
 * full-view pass reconstructs each pixel's world position from the depth
 * copy, clips the eye ray against the boxes and boundary half-spaces of the
 * regions seen this frame and marches a few jittered steps through each:
 * height falloff as the overlay, a slow value noise (r_om_volfog_noise) and,
 * with a sun, light shafts from the static cascade map (lit steps brighten
 * the fog colour when looking towards the sun). The region overlay is kept
 * and scaled by r_om_volfog_overlaydim, so the map's own fog look stays.
 * Ghoul models and translucent effects come later in the frame and are not
 * in the volume (they keep their SoF fog).
 * ------------------------------------------------------------------ */

#define VOLFOG_MAXREGIONS 16

static cvar_t *r_om_volfog, *r_om_volfog_quality, *r_om_volfog_intensity, *r_om_volfog_overlaydim;
static cvar_t *r_om_volfog_shafts, *r_om_volfog_noise;

static struct
{
	qboolean tried;
	GLuint prog;
	GLint u_depth, u_invpv, u_eye, u_prm, u_prm2, u_vr, u_rect;
	float vr[VOLFOG_MAXREGIONS * 32];
} V;

static const char *volfog_fs =
	"uniform sampler2D depthtex;\n"
	"uniform mat4 invpv;\n"
	"uniform vec4 eye;\n"    /* xyz, time */
	"uniform vec4 prm;\n"    /* depth range min, max; intensity; shafts */
	"uniform vec4 prm2;\n"   /* regions, steps, noise, sun */
	"uniform vec4 vr[128];\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmissive;\n"
	"float h3(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }\n"
	"float vnoise(vec3 x)\n"
	"{\n"
	"	vec3 i = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f);\n"
	"	return mix(mix(mix(h3(i), h3(i + vec3(1.0, 0.0, 0.0)), f.x), mix(h3(i + vec3(0.0, 1.0, 0.0)), h3(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),\n"
	"		mix(mix(h3(i + vec3(0.0, 0.0, 1.0)), h3(i + vec3(1.0, 0.0, 1.0)), f.x), mix(h3(i + vec3(0.0, 1.0, 1.0)), h3(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	float z = texture(depthtex, uv).r;\n"
	"	float nz = (z - prm.x) / (prm.y - prm.x) * 2.0 - 1.0;\n"
	"	vec4 w = invpv * vec4(vuv * 2.0 - 1.0, nz, 1.0);\n"
	"	vec3 ro = eye.xyz;\n"
	"	vec3 dv = w.xyz / w.w - ro;\n"
	"	float tmax = length(dv);\n"
	"	if (w.w <= 0.0) tmax = 1e6;\n"
	"	vec3 rd = dv / max(tmax, 0.001);\n"
	"	vec3 inv = 1.0 / (rd + sign(rd + 1e-9) * 1e-6);\n"
	"	float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
	"	int n = int(prm2.x + 0.5);\n"
	"	int steps = int(prm2.y + 0.5);\n"
	"	float ph = (prm2.w > 0.5) ? pow(max(dot(rd, omCsmSun.xyz), 0.0), 4.0) : 0.0;\n"
	"	vec3 acc = vec3(0.0);\n"
	"	float T = 1.0;\n"
	"	for (int i = 0; i < 16; i++)\n"
	"	{\n"
	"		if (i >= n) break;\n"
	"		vec4 a0 = vr[i * 8];\n"
	"		vec4 a1 = vr[i * 8 + 1];\n"
	"		vec4 a2 = vr[i * 8 + 2];\n"
	"		vec3 ta = (a0.xyz - ro) * inv;\n"
	"		vec3 tb = (a1.xyz - ro) * inv;\n"
	"		vec3 tn = min(ta, tb);\n"
	"		vec3 tf = max(ta, tb);\n"
	"		float t0 = max(max(tn.x, tn.y), max(tn.z, 0.0));\n"
	"		float t1 = min(min(tf.x, tf.y), min(tf.z, tmax));\n"
	"		for (int k = 0; k < 5; k++)\n"
	"		{\n"
	"			vec4 pl = vr[i * 8 + 3 + k];\n"
	"			float a = dot(pl.xyz, ro) - pl.w;\n"
	"			float b = dot(pl.xyz, rd);\n"
	"			if (abs(b) < 1e-5) { if (a < 0.0) t1 = -1.0; }\n"
	"			else if (b > 0.0) t0 = max(t0, -a / b);\n"
	"			else t1 = min(t1, -a / b);\n"
	"		}\n"
	"		if (t1 <= t0 || a2.w <= 0.0) continue;\n"
	"		float len = t1 - t0;\n"
	"		float ds = len / float(steps);\n"
	"		float dens = 0.0, lit = 0.0;\n"
	"		for (int s = 0; s < 64; s++)\n"
	"		{\n"
	"			if (s >= steps) break;\n"
	"			vec3 p = ro + rd * (t0 + (float(s) + jit) * ds);\n"
	"			float d = 1.0;\n"
	"			if (a1.w > 0.5) { float h = clamp((a1.z - p.z) / max(a1.z - a0.z, 1.0), 0.0, 1.0); d = h * h; }\n"
	"			if (prm2.z > 0.0) d *= 1.0 + prm2.z * (vnoise(p * 0.011 + vec3(0.0, eye.w * 0.07, eye.w * 0.11)) * 2.0 - 1.0);\n"
	"			dens += d;\n"
	"			if (ph > 0.001)\n"
	"			{\n"
	"				float fade;\n"
	"				int c = omCsmCascade(p, fade);\n"
	"				float l = (c < 0) ? 0.5 : mix(0.5, omCsmStatic(c, p, vec3(0.0)), fade);\n"
	"				lit += l * d;\n"
	"			}\n"
	"		}\n"
	"		dens /= float(steps);\n"
	"		float al = clamp(a2.w * prm.z, 0.0, 1.0) * (1.0 - exp(-a0.w * dens * len));\n"
	"		vec3 col = a2.rgb;\n"
	"		if (ph > 0.001) col *= 1.0 + prm.w * 0.5 * ph * (lit / float(steps)) / max(dens, 0.001);\n"
	"		acc += T * al * col;\n"
	"		T *= 1.0 - al;\n"
	"	}\n"
	"	outColor = vec4(acc, T);\n"
	"	outEmissive = vec4(0.0, 0.0, 0.0, T);\n"
	"}\n";

static void
VolFogCvars(void)
{
	if (!r_om_volfog)
	{
		r_om_volfog = ri.Cvar_Get("r_om_volfog", "1", CVAR_ARCHIVE);
		r_om_volfog_quality = ri.Cvar_Get("r_om_volfog_quality", "2", CVAR_ARCHIVE);
		r_om_volfog_intensity = ri.Cvar_Get("r_om_volfog_intensity", "0.4", CVAR_ARCHIVE);
		r_om_volfog_overlaydim = ri.Cvar_Get("r_om_volfog_overlaydim", "0.85", CVAR_ARCHIVE);
		r_om_volfog_shafts = ri.Cvar_Get("r_om_volfog_shafts", "1", CVAR_ARCHIVE);
		r_om_volfog_noise = ri.Cvar_Get("r_om_volfog_noise", "0.25", CVAR_ARCHIVE);
	}
}

static qboolean
VolFogWanted(void)
{
	VolFogCvars();
	return (r_om_volfog->value != 0) && (r_om_volfog_intensity->value > 0) && (GL3_Fog_NumRegions() > 0) &&
		((int)gl_fogmode->value != 2) && ((int)gl_fogmode->value != 3);
}

float
GL3_VolFog_OverlayDim(void)
{
	if (!gl3post.volfog)
	{
		return 1.0f;
	}

	return Q_max(0.0f, Q_min(r_om_volfog_overlaydim->value, 1.0f));
}

static qboolean
VolFogProgram(void)
{
	char *fs;
	GLuint v, f, p;
	GLint ok = 0;

	if (V.tried)
	{
		return V.prog != 0;
	}

	V.tried = true;
	fs = GL3_CSM_Prepend(volfog_fs);

	if (!fs)
	{
		return false;
	}

	v = CompilePost(GL_VERTEX_SHADER, post_vs);
	f = CompilePost(GL_FRAGMENT_SHADER, fs);
	free(fs);

	if (!v || !f)
	{
		if (v) glDeleteShader(v);
		if (f) glDeleteShader(f);
		R_Printf(PRINT_ALL, "r_om_volfog: shader failed, volumetric fog off\n");
		return false;
	}

	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	GL3_Post_BindOutputs(p);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "r_om_volfog: link failed: %s\n", log);
		glDeleteProgram(p);
		return false;
	}

	V.prog = p;
	V.u_depth = glGetUniformLocation(p, "depthtex");
	V.u_invpv = glGetUniformLocation(p, "invpv");
	V.u_eye = glGetUniformLocation(p, "eye");
	V.u_prm = glGetUniformLocation(p, "prm");
	V.u_prm2 = glGetUniformLocation(p, "prm2");
	V.u_vr = glGetUniformLocation(p, "vr");
	V.u_rect = glGetUniformLocation(p, "rect");
	GL3_UseProgram(p);
	GL3_CSM_BindProgram(p);
	glUniform1i(V.u_depth, 7);
	return true;
}

/* after the overlay pass of the opaque world */
void
GL3_Post_VolFog(void)
{
	static const int stepsof[4] = {6, 10, 16, 28};
	int n, q;
	float fw, fh;

	if (!P.bound || !gl3post.volfog || !P.depthcopied)
	{
		return;
	}

	n = GL3_Fog_VolumeRegions(V.vr, VOLFOG_MAXREGIONS);

	if ((n <= 0) || !VolFogProgram())
	{
		return;
	}

	GL3_Draw3DBatchesNow();
	GL3_FX_Flush();

	q = (int)r_om_volfog_quality->value;
	q = (q < 0) ? 0 : ((q > 3) ? 3 : q);
	fw = (float)P.w;
	fh = (float)P.h;

	GL3_UseProgram(V.prog);
	glUniform4f(V.u_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	glUniformMatrix4fv(V.u_invpv, 1, GL_FALSE, GL3_CSM_InvPV());
	glUniform4f(V.u_eye, r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2], r_newrefdef.time);
	glUniform4f(V.u_prm, gl3depthmin, gl3depthmax, r_om_volfog_intensity->value, Q_max(0.0f, r_om_volfog_shafts->value));
	glUniform4f(V.u_prm2, (float)n, (float)stepsof[q], Q_max(0.0f, Q_min(r_om_volfog_noise->value, 0.9f)),
		(GL3_CSM_Active() && (r_om_volfog_shafts->value > 0)) ? 1.0f : 0.0f);
	glUniform4fv(V.u_vr, n * 8, V.vr);

	GL3_SelectTMU(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);
	GL3_SelectTMU(GL_TEXTURE0);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_SRC_ALPHA);
	DrawQuad();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

static void
VolFogShutdown(void)
{
	if (V.prog)
	{
		if (gl3state.currentShaderProgram == V.prog)
		{
			gl3state.currentShaderProgram = 0;
		}

		glDeleteProgram(V.prog);
	}

	memset(&V, 0, sizeof(V));
}

/* ------------------------------------------------------------------
 * GFX-modern r_om_beams: volumetric light beams. Some Ghoul light models
 * carry a painted, additive light cone (street lamps, search lights: a part
 * textured ".../BEAM"). The Ghoul system marks such textures
 * (OM_GHL_BEAM_HINT); the emulation hands their triangles here instead of
 * drawing them. At the end of the view each captured cone is fitted (axis,
 * narrow and wide radius), gets a perspective shadow map from its narrow end
 * (static world and this frame's Ghoul casters) and is ray-marched over the
 * full depth of the view: soft edges, slow dust and the shadows of the
 * characters inside the beam. A shot lamp loses its beam part (its _OFF
 * model) and the beam is gone; a moving search light moves its beam.
 * ------------------------------------------------------------------ */

#define BEAM_MAXTEX 64
#define BEAM_MAXCAP 16
#define BEAM_DRAW 4
#define BEAM_SHADOW 512
#define BEAM_UNIT 9         /* GL_TEXTURE9 */
#define BEAM_MAXDYN (3 * 1024 * 1024)
#define BEAM_PROF 32        /* brightness of the painted cone texture along t */

#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif

static cvar_t *r_om_beams, *r_om_beams_intensity, *r_om_beams_shadows, *r_om_beams_noise, *r_om_beams_debug;
static cvar_t *r_om_beams_motes, *r_om_beams_groundshadow;

#define BEAM_MOTES 220      /* dust motes per beam at r_om_beams_motes 1 */

typedef struct
{
	GLuint tex;
	float *v;       /* world-space vertices, 3 floats each */
	int nv, cap;    /* floats */
	float *t;       /* texture coordinate t of each vertex */
	int tcap;
	vec3_t mins, maxs;
} beamcap_t;

typedef struct
{
	vec3_t apex, axis, color;
	float len, r0, r1, dist;
	float hs, he;   /* where the painted light starts and ends, as fractions of len */
} beam_t;

static struct
{
	GLuint marked[BEAM_MAXTEX];
	float markcol[BEAM_MAXTEX][3];
	float markprof[BEAM_MAXTEX][BEAM_PROF];
	int nmarked;

	beamcap_t cap[BEAM_MAXCAP];
	int ncap;

	float *dyn, *flt;      /* this view's Ghoul casters (world space); per-beam filtered copy */
	int numdyn, maxdyn, maxflt;
	float *tmp;
	int maxtmp;

	qboolean tried;
	GLuint prog, shadowtex, shadowfbo;
	GLint u_depth, u_shadow, u_invpv, u_eye, u_prm, u_prm2, u_b0, u_b1, u_b2, u_b3, u_bvp, u_rect;

	/* dust motes */
	qboolean mtried;
	GLuint mprog;
	GLint m_pv, m_eye, m_prm, m_b0, m_b1, m_b2, m_b3, m_bvp, m_layer, m_shadow;

	/* character shadows on lit surfaces */
	qboolean stried;
	GLuint sprog;
	GLint s_depth, s_shadow, s_invpv, s_eye, s_prm, s_b0, s_b1, s_b2, s_b3, s_bvp, s_rect;

	/* the beams of the last view, for the Ghoul ground shadows drawn before this view's beams are known */
	float pools[BEAM_DRAW * 16];
	int npools;
} B;

/* character shadows in a lamp's pool: a surface point inside a beam that the static world leaves lit (the
   lightmap holds the lamp there) but a Ghoul caster blocks loses part of its light; multiplied into the
   scene before the beams are added. Layers 0..3 hold world + casters, layers 4..7 the world alone. */
static const char *beamshade_fs =
	"uniform sampler2D depthtex;\n"
	"uniform sampler2DArrayShadow beamshadow;\n"
	"uniform mat4 invpv;\n"
	"uniform vec4 eye;\n"     /* xyz; mode: 0 darken (the lamp's character shadows), 1 lighten (the lamp fills
	                             the sun's character shadow inside its pool; output = extra factor - 1) */
	"uniform vec4 prm;\n"     /* depth range min, max; strength; beams */
	"uniform vec4 b0[4];\n"
	"uniform vec4 b1[4];\n"
	"uniform vec4 b2[4];\n"
	"uniform vec4 b3[4];\n"
	"uniform mat4 bvp[4];\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmissive;\n"
	"void main()\n"
	"{\n"
	"	float z = texture(depthtex, uv).r;\n"
	"	float nz = (z - prm.x) / (prm.y - prm.x) * 2.0 - 1.0;\n"
	"	vec4 w = invpv * vec4(vuv * 2.0 - 1.0, nz, 1.0);\n"
	"	vec3 p = w.xyz / w.w;\n"
	"	float f = 1.0, fill = 0.0;\n"
	"	int n = int(prm.w + 0.5);\n"
	"	for (int i = 0; i < 4; i++)\n"
	"	{\n"
	"		if (i >= n) break;\n"
	"		vec3 q = p - b0[i].xyz;\n"
	"		float h = dot(q, b1[i].xyz);\n"
	"		float hn = h / b0[i].w;\n"
	/* the pool lies on the ground at and a little beyond the painted cone's wide end */
	"		if (hn < b3[i].x || hn > 1.3) continue;\n"
	"		float R = max(mix(b1[i].w, b2[i].w, hn), 0.5);\n"
	"		float rad = 1.0 - smoothstep(0.6, 1.0, length(q - h * b1[i].xyz) / R);\n"
	"		if (rad <= 0.0) continue;\n"
	"		vec4 sp = bvp[i] * vec4(p, 1.0);\n"
	"		vec3 sc = sp.xyz / sp.w * 0.5 + 0.5;\n"
	"		if (any(lessThan(sc, vec3(0.0))) || any(greaterThan(sc, vec3(1.0)))) continue;\n"
	"		float lall = texture(beamshadow, vec4(sc.xy, float(i), sc.z - 0.004));\n"
	"		float lworld = texture(beamshadow, vec4(sc.xy, float(i + 4), sc.z - 0.004));\n"
	/* only points on the static surface the lamp sees (the lightmap holds the pool there); a model in
	   front of it (the character itself, the lamp housing) lies nearer than that surface and is left */
	"		float onworld = 1.0 - texture(beamshadow, vec4(sc.xy, float(i + 4), sc.z + 0.008));\n"
	"		float pool = lworld * onworld * rad * smoothstep(b3[i].x, b3[i].x + 0.1, hn);\n"
	"		f *= 1.0 - prm.z * pool * (1.0 - lall);\n"
	"		fill = max(fill, pool * lall);\n"
	"	}\n"
	"	if (eye.w < 0.5)\n"
	"	{\n"
	"		outColor = vec4(f, f, f, 1.0);\n"
	"		outEmissive = vec4(1.0);\n"
	"		return;\n"
	"	}\n"
	/* where the lamp reaches, it fills most of the sun's (moon's) character shadow that the world shaders
	   took from the lightmap (gl3_csm.c omCsmApply), so a character does not throw two hard shadows */
	"	float g = 0.0;\n"
	"	if (fill > 0.0 && omCsmSun.w > 0.5)\n"
	"	{\n"
	"		vec3 nr = normalize(cross(dFdx(p), dFdy(p)));\n"
	"		if (dot(nr, eye.xyz - p) < 0.0) nr = -nr;\n"
	"		float ndl = dot(nr, omCsmSun.xyz), fade;\n"
	"		int c = omCsmCascade(p, fade);\n"
	"		if (c >= 0 && ndl > 0.0)\n"
	"		{\n"
	"			float k = omCsmParams.x * omCsmStatic(c, p, nr) * omCsmDynamic(c, p, nr) * fade * smoothstep(0.0, 0.3, ndl);\n"
	"			g = (1.0 / max(1.0 - k, 0.5) - 1.0) * 0.85 * fill;\n"
	"		}\n"
	"	}\n"
	"	outColor = vec4(g, g, g, 0.0);\n"
	"	outEmissive = vec4(0.0);\n"
	"}\n";

/* dust motes: points placed and moved from their index and the clock alone, lit by the beam profile and
   its shadow map, depth-tested against the scene */
static const char *mote_vs =
	"uniform mat4 pv;\n"
	"uniform vec4 eye;\n"     /* xyz, time */
	"uniform vec4 prm;\n"     /* brightness, pixels per unit at distance 1, shadows */
	"uniform vec4 b0, b1, b2, b3;\n"
	"uniform mat4 bvp;\n"
	"uniform float layer;\n"
	"uniform sampler2DArrayShadow beamshadow;\n"
	"out vec3 vcol;\n"
	"float hash(float n) { return fract(sin(n) * 43758.5453); }\n"
	"void main()\n"
	"{\n"
	"	float s = float(gl_VertexID) + layer * 1013.0;\n"
	"	float h1 = hash(s * 1.13 + 0.1), h2 = hash(s * 2.71 + 0.3), h3 = hash(s * 3.37 + 0.7), h4 = hash(s * 5.19 + 0.9);\n"
	/* slow settling along the beam, wrapping from its end back to its start (faded at both) */
	"	float fall = fract(h1 + eye.w * (0.004 + 0.006 * h4));\n"
	"	float hn = mix(b3.x, b3.y, fall);\n"
	"	float R = mix(b1.w, b2.w, hn);\n"
	"	vec3 ax = b1.xyz;\n"
	"	vec3 up = abs(ax.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
	"	vec3 sx = normalize(cross(ax, up)), sy = cross(ax, sx);\n"
	"	float ang = 6.2831853 * h2 + eye.w * 0.05 * (h3 - 0.5);\n"
	"	float rr = sqrt(h3) * R * 0.85;\n"
	"	vec3 p = b0.xyz + ax * (hn * b0.w) + (sx * cos(ang) + sy * sin(ang)) * rr;\n"
	"	p += vec3(sin(eye.w * 0.37 + h4 * 40.0), sin(eye.w * 0.29 + h2 * 40.0), sin(eye.w * 0.33 + h1 * 40.0)) * 1.5;\n"
	"	float a = (1.0 - smoothstep(0.45, 1.0, rr / max(R, 0.5))) * smoothstep(0.0, 0.12, fall) * pow(1.0 - fall, 1.3);\n"
	"	a *= 0.55 + 0.45 * sin(eye.w * (1.5 + 2.0 * h4) + h1 * 60.0);\n"   /* a glint as it turns */
	"	if (prm.z > 0.5)\n"
	"	{\n"
	"		vec4 sp = bvp * vec4(p, 1.0);\n"
	"		vec3 sc = sp.xyz / sp.w * 0.5 + 0.5;\n"
	"		a *= texture(beamshadow, vec4(sc.xy, layer, sc.z - 0.0015));\n"
	"	}\n"
	"	vec4 cp = pv * vec4(p, 1.0);\n"
	"	gl_Position = cp;\n"
	"	gl_PointSize = clamp(prm.y * (0.5 + 0.5 * h2) / max(cp.w, 1.0), 1.0, 5.0);\n"
	"	vcol = b2.rgb * prm.x * max(a, 0.0);\n"
	"}\n";

static const char *mote_fs =
	"in vec3 vcol;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmissive;\n"
	"void main()\n"
	"{\n"
	"	float f = 1.0 - smoothstep(0.15, 0.5, length(gl_PointCoord - vec2(0.5)));\n"
	"	outColor = vec4(vcol * f, 0.0);\n"
	"	outEmissive = vec4(0.0);\n"
	"}\n";

static const char *beam_fs =
	"uniform sampler2D depthtex;\n"
	"uniform sampler2DArrayShadow beamshadow;\n"
	"uniform mat4 invpv;\n"
	"uniform vec4 eye;\n"     /* xyz, time */
	"uniform vec4 prm;\n"     /* depth range min, max; intensity; noise */
	"uniform vec4 prm2;\n"    /* beams, steps, shadows */
	"uniform vec4 b0[4];\n"   /* narrow end, length */
	"uniform vec4 b1[4];\n"   /* axis (narrow to wide), narrow radius */
	"uniform vec4 b2[4];\n"   /* colour, wide radius */
	"uniform vec4 b3[4];\n"   /* light start, end (fractions of the length, from the painted texture) */
	"uniform mat4 bvp[4];\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmissive;\n"
	"float h3(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }\n"
	"float vnoise(vec3 x)\n"
	"{\n"
	"	vec3 i = floor(x); vec3 f = fract(x); f = f * f * (3.0 - 2.0 * f);\n"
	"	return mix(mix(mix(h3(i), h3(i + vec3(1.0, 0.0, 0.0)), f.x), mix(h3(i + vec3(0.0, 1.0, 0.0)), h3(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),\n"
	"		mix(mix(h3(i + vec3(0.0, 0.0, 1.0)), h3(i + vec3(1.0, 0.0, 1.0)), f.x), mix(h3(i + vec3(0.0, 1.0, 1.0)), h3(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	float z = texture(depthtex, uv).r;\n"
	"	float nz = (z - prm.x) / (prm.y - prm.x) * 2.0 - 1.0;\n"
	"	vec4 w = invpv * vec4(vuv * 2.0 - 1.0, nz, 1.0);\n"
	"	vec3 ro = eye.xyz;\n"
	"	vec3 dv = w.xyz / w.w - ro;\n"
	"	float tmax = length(dv);\n"
	"	if (w.w <= 0.0) tmax = 1e6;\n"
	"	vec3 rd = dv / max(tmax, 0.001);\n"
	"	float jit = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
	"	int n = int(prm2.x + 0.5);\n"
	"	int steps = int(prm2.y + 0.5);\n"
	"	vec3 acc = vec3(0.0);\n"
	"	for (int i = 0; i < 4; i++)\n"
	"	{\n"
	"		if (i >= n) break;\n"
	"		vec3 A = b0[i].xyz; float L = b0[i].w;\n"
	"		vec3 ax = b1[i].xyz; float r0 = b1[i].w; float r1 = b2[i].w;\n"
	/* the eye ray against the enclosing cylinder (radius r1, 0 <= h <= L) */
	"		vec3 oc = ro - A;\n"
	"		float ho = dot(oc, ax), hd = dot(rd, ax);\n"
	"		vec3 o2 = oc - ho * ax, d2 = rd - hd * ax;\n"
	"		float qa = dot(d2, d2), qb = 2.0 * dot(o2, d2), qc = dot(o2, o2) - r1 * r1;\n"
	"		float t0 = 0.0, t1 = tmax;\n"
	"		if (qa < 1e-6) { if (qc > 0.0) continue; }\n"
	"		else\n"
	"		{\n"
	"			float disc = qb * qb - 4.0 * qa * qc;\n"
	"			if (disc <= 0.0) continue;\n"
	"			float sq = sqrt(disc);\n"
	"			t0 = max(t0, (-qb - sq) / (2.0 * qa));\n"
	"			t1 = min(t1, (-qb + sq) / (2.0 * qa));\n"
	"		}\n"
	"		if (abs(hd) < 1e-6) { if (ho < 0.0 || ho > L) continue; }\n"
	"		else\n"
	"		{\n"
	"			float ta = -ho / hd, tb = (L - ho) / hd;\n"
	"			t0 = max(t0, min(ta, tb));\n"
	"			t1 = min(t1, max(ta, tb));\n"
	"		}\n"
	"		if (t1 <= t0) continue;\n"
	"		float ds = (t1 - t0) / float(steps);\n"
	"		float sum = 0.0;\n"
	"		for (int s = 0; s < 48; s++)\n"
	"		{\n"
	"			if (s >= steps) break;\n"
	"			vec3 p = ro + rd * (t0 + (float(s) + jit) * ds);\n"
	"			vec3 q = p - A;\n"
	"			float h = dot(q, ax);\n"
	"			float hn = clamp(h / L, 0.0, 1.0);\n"
	"			float R = max(mix(r0, r1, hn), 0.5);\n"
	"			float d = 1.0 - smoothstep(0.45, 1.0, length(q - h * ax) / R);\n"
	"			float fall = (hn - b3[i].x) / max(b3[i].y - b3[i].x, 0.05);\n"
	"			if (d <= 0.0 || fall <= 0.0 || fall >= 1.0) continue;\n"
	"			d *= smoothstep(0.0, 0.12, fall) * pow(1.0 - fall, 1.3);\n"
	"			if (prm.w > 0.0) d *= 1.0 + prm.w * (vnoise(p * 0.045 + vec3(eye.w * 0.11, eye.w * 0.07, -eye.w * 0.15)) * 2.0 - 1.0);\n"
	"			if (prm2.z > 0.5)\n"
	"			{\n"
	"				vec4 sp = bvp[i] * vec4(p, 1.0);\n"
	"				vec3 sc = sp.xyz / sp.w * 0.5 + 0.5;\n"
	"				d *= texture(beamshadow, vec4(sc.xy, float(i), sc.z - 0.0015));\n"
	"			}\n"
	"			sum += d * ds / (2.0 * max(R, 1.0));\n"
	"		}\n"
	"		acc += b2[i].rgb * sum;\n"
	"	}\n"
	"	outColor = vec4(acc * prm.z, 0.0);\n"
	"	outEmissive = vec4(0.0);\n"
	"}\n";

static void
BeamCvars(void)
{
	if (!r_om_beams)
	{
		r_om_beams = ri.Cvar_Get("r_om_beams", "1", CVAR_ARCHIVE);
		r_om_beams_intensity = ri.Cvar_Get("r_om_beams_intensity", "1.5", CVAR_ARCHIVE);
		r_om_beams_shadows = ri.Cvar_Get("r_om_beams_shadows", "1", CVAR_ARCHIVE);
		r_om_beams_noise = ri.Cvar_Get("r_om_beams_noise", "0.35", CVAR_ARCHIVE);
		r_om_beams_debug = ri.Cvar_Get("r_om_beams_debug", "0", 0); /* 1: also draw the painted cone, 2: only it */
		r_om_beams_motes = ri.Cvar_Get("r_om_beams_motes", "1", CVAR_ARCHIVE); /* dust motes, 0 off */
		/* how much a character's shadow takes from the lamp's pool on the ground, 0 off */
		r_om_beams_groundshadow = ri.Cvar_Get("r_om_beams_groundshadow", "0.55", CVAR_ARCHIVE);
	}
}

static qboolean
BeamsWanted(void)
{
	BeamCvars();
	return (r_om_beams->value != 0) && (B.nmarked > 0);
}

static void
BeamsBeginView(void)
{
	int i;

	for (i = 0; i < B.ncap; i++)
	{
		B.cap[i].nv = 0;
	}

	B.ncap = 0;
	B.numdyn = 0;
}

static int
BeamTexIndex(GLuint tex)
{
	int i;

	for (i = 0; i < B.nmarked; i++)
	{
		if (B.marked[i] == tex)
		{
			return i;
		}
	}

	return -1;
}

/* the lamp pools of the last view (per beam 16 floats: narrow end + length, axis + narrow radius,
   colour + wide radius, light start/end): a Ghoul ground shadow inside one gives way to the lamp's shadow */
int
GL3_Beam_Pools(const float **data)
{
	*data = B.pools;
	return B.npools;
}

qboolean
GL3_Beam_IsBeam(GLuint tex)
{
	return tex && (B.nmarked > 0) && (BeamTexIndex(tex) >= 0);
}

/* the texture bound now is a painted light cone: remember it and its colour (alpha-weighted mean) */
void
GL3_Beam_MarkTexture(GLuint tex)
{
	float col[3] = {1.0f, 0.92f, 0.75f}, prof[BEAM_PROF];
	int idx, r;

	if (!tex)
	{
		return;
	}

	for (r = 0; r < BEAM_PROF; r++)
	{
		prof[r] = 1.0f; /* unknown: lit along the whole length */
	}

	idx = BeamTexIndex(tex);

	if (idx < 0)
	{
		if (B.nmarked >= BEAM_MAXTEX)
		{
			return;
		}

		idx = B.nmarked++;
		B.marked[idx] = tex;
	}

#ifndef YQ2_GL3_GLES3
	{
		GLint w = 0, h = 0;

		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		gl3state.currenttexture = tex;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

		if ((w > 0) && (h > 0) && (w * h <= (1 << 20)))
		{
			byte *px = malloc((size_t)w * h * 4);

			if (px)
			{
				double s[3] = {0, 0, 0}, ws = 0;
				int i;

				glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);

				for (i = 0; i < w * h; i++)
				{
					double wt = (px[i * 4 + 3] / 255.0) * ((px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2]) / 765.0);

					s[0] += px[i * 4] * wt;
					s[1] += px[i * 4 + 1] * wt;
					s[2] += px[i * 4 + 2] * wt;
					ws += wt;
				}

				if (ws > 0)
				{
					double m;

					s[0] /= ws; s[1] /= ws; s[2] /= ws;
					m = Q_max(s[0], Q_max(s[1], s[2]));

					if (m > 0)
					{
						col[0] = (float)(s[0] / m);
						col[1] = (float)(s[1] / m);
						col[2] = (float)(s[2] / m);
					}
				}

				/* brightness by row (texture t from 0 at the first row): where the painted light is */
				{
					float pmax = 0;

					for (r = 0; r < BEAM_PROF; r++)
					{
						int y0 = r * h / BEAM_PROF, y1 = Q_max((r + 1) * h / BEAM_PROF, y0 + 1), x, y;
						double acc = 0;

						for (y = y0; y < y1 && y < h; y++)
						{
							for (x = 0; x < w; x++)
							{
								const byte *q = px + ((size_t)y * w + x) * 4;

								acc += (q[3] / 255.0) * ((q[0] + q[1] + q[2]) / 765.0);
							}
						}

						prof[r] = (float)(acc / ((double)(y1 - y0) * w));
						pmax = Q_max(pmax, prof[r]);
					}

					for (r = 0; r < BEAM_PROF; r++)
					{
						prof[r] = (pmax > 0) ? prof[r] / pmax : 1.0f;
					}
				}

				free(px);
			}
		}
	}
#endif

	VectorCopy(col, B.markcol[idx]);
	memcpy(B.markprof[idx], prof, sizeof(prof));
	R_Printf(PRINT_DEVELOPER, "r_om_beams: texture %u is a light cone (colour %.2f %.2f %.2f, rows %.2f %.2f %.2f %.2f ... %.2f)\n",
		tex, col[0], col[1], col[2], prof[0], prof[4], prof[8], prof[16], prof[BEAM_PROF - 1]);
}

void
GL3_Beam_ForgetTexture(GLuint tex)
{
	int idx = BeamTexIndex(tex);

	if (idx >= 0)
	{
		B.nmarked--;
		B.marked[idx] = B.marked[B.nmarked];
		VectorCopy(B.markcol[B.nmarked], B.markcol[idx]);
		memcpy(B.markprof[idx], B.markprof[B.nmarked], sizeof(B.markprof[0]));
	}
}

/* clip-space vertices of this view -> world space into *dst (grown as needed); false when out of memory */
static qboolean
ClipToWorld(const float *clip, int numverts, int stride, float **dst, int *have, int *cap, int limit)
{
	const float *m = GL3_CSM_InvPV();
	int i;

	if (*have + numverts * 3 > *cap)
	{
		int want = *have + numverts * 3, c = *cap ? *cap : 4096;
		float *d;

		while (c < want)
		{
			c *= 2;
		}

		if (c > limit)
		{
			return false;
		}

		d = realloc(*dst, c * sizeof(float));

		if (!d)
		{
			return false;
		}

		*dst = d;
		*cap = c;
	}

	for (i = 0; i < numverts; i++, clip += stride)
	{
		float x = clip[0], y = clip[1], z = clip[2], w = clip[3];
		float ow = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
		float *o = *dst + *have;

		if (fabsf(ow) < 1e-8f)
		{
			ow = 1e-8f;
		}

		o[0] = (m[0] * x + m[4] * y + m[8] * z + m[12] * w) / ow;
		o[1] = (m[1] * x + m[5] * y + m[9] * z + m[13] * w) / ow;
		o[2] = (m[2] * x + m[6] * y + m[10] * z + m[14] * w) / ow;
		*have += 3;
	}

	return true;
}

/* triangles of a painted cone (clip space): kept for the beam pass; true when they must not be drawn */
qboolean
GL3_Beam_Capture(const float *clip, const float *st, int numverts, int stride, GLuint tex)
{
	beamcap_t *c = NULL;
	vec3_t mn = {1e30f, 1e30f, 1e30f}, mx = {-1e30f, -1e30f, -1e30f};
	int i, k, have = 0;

	BeamCvars();

	if (!P.bound || !r_om_beams->value || (r_om_beams_debug->value == 2) || (numverts < 3))
	{
		return false;
	}

	if (!ClipToWorld(clip, numverts, stride, &B.tmp, &have, &B.maxtmp, 1 << 20))
	{
		return false;
	}

	for (i = 0; i < have; i += 3)
	{
		for (k = 0; k < 3; k++)
		{
			mn[k] = Q_min(mn[k], B.tmp[i + k]);
			mx[k] = Q_max(mx[k], B.tmp[i + k]);
		}
	}

	/* parts of one cone drawn in several batches join the cone they touch */
	for (i = 0; i < B.ncap; i++)
	{
		beamcap_t *o = &B.cap[i];

		if ((o->tex == tex) && (mn[0] <= o->maxs[0] + 16) && (mx[0] >= o->mins[0] - 16) && (mn[1] <= o->maxs[1] + 16) &&
			(mx[1] >= o->mins[1] - 16) && (mn[2] <= o->maxs[2] + 16) && (mx[2] >= o->mins[2] - 16))
		{
			c = o;
			break;
		}
	}

	if (!c)
	{
		if (B.ncap >= BEAM_MAXCAP)
		{
			return r_om_beams_debug->value != 1;
		}

		c = &B.cap[B.ncap++];
		c->tex = tex;
		c->nv = 0;
		VectorCopy(mn, c->mins);
		VectorCopy(mx, c->maxs);
	}

	if (c->nv + have > c->cap)
	{
		int nc = c->cap ? c->cap : 1024;
		float *d;

		while (nc < c->nv + have)
		{
			nc *= 2;
		}

		d = realloc(c->v, nc * sizeof(float));

		if (!d)
		{
			return false;
		}

		c->v = d;
		c->cap = nc;
	}

	if ((c->nv + have) / 3 > c->tcap)
	{
		int nt = c->cap / 3;
		float *d = realloc(c->t, nt * sizeof(float));

		if (!d)
		{
			return false;
		}

		c->t = d;
		c->tcap = nt;
	}

	for (i = 0; i < have / 3; i++)
	{
		c->t[c->nv / 3 + i] = st[i * stride + 1];
	}

	memcpy(c->v + c->nv, B.tmp, have * sizeof(float));
	c->nv += have;

	for (k = 0; k < 3; k++)
	{
		c->mins[k] = Q_min(c->mins[k], mn[k]);
		c->maxs[k] = Q_max(c->maxs[k], mx[k]);
	}

	return r_om_beams_debug->value != 1;
}

/* opaque Ghoul triangles of this view (clip space): the casters of the beam shadows */
void
GL3_Beam_AddCasterTris(const float *clip, int numverts, int stride)
{
	if (!P.bound || !r_om_beams || !r_om_beams->value || !r_om_beams_shadows->value || !B.nmarked || (numverts < 3))
	{
		return;
	}

	ClipToWorld(clip, numverts - numverts % 3, stride, &B.dyn, &B.numdyn, &B.maxdyn, BEAM_MAXDYN);
}

/* axis, length and radii of a captured cone from its vertices */
static qboolean
FitBeam(const beamcap_t *c, beam_t *b)
{
	double cen[3] = {0, 0, 0}, cov[3][3] = {{0}}, v[3], ext[3];
	double hmin = 1e30, hmax = -1e30, rlo = 0, rhi = 0, band;
	int n = c->nv / 3, i, j, k, nlo = 0, nhi = 0, it;

	if (n < 3)
	{
		return false;
	}

	for (i = 0; i < n; i++)
	{
		for (k = 0; k < 3; k++)
		{
			cen[k] += c->v[i * 3 + k];
		}
	}

	for (k = 0; k < 3; k++)
	{
		cen[k] /= n;
	}

	for (i = 0; i < n; i++)
	{
		double d[3];

		for (k = 0; k < 3; k++)
		{
			d[k] = c->v[i * 3 + k] - cen[k];
		}

		for (j = 0; j < 3; j++)
		{
			for (k = 0; k < 3; k++)
			{
				cov[j][k] += d[j] * d[k];
			}
		}
	}

	/* principal axis by power iteration, started along the longest box side */
	for (k = 0; k < 3; k++)
	{
		ext[k] = c->maxs[k] - c->mins[k];
	}

	v[0] = (ext[0] >= ext[1] && ext[0] >= ext[2]) ? 1 : 0.1;
	v[1] = (ext[1] > ext[0] && ext[1] >= ext[2]) ? 1 : 0.1;
	v[2] = (ext[2] > ext[0] && ext[2] > ext[1]) ? 1 : 0.1;

	for (it = 0; it < 32; it++)
	{
		double nv[3], l;

		for (j = 0; j < 3; j++)
		{
			nv[j] = cov[j][0] * v[0] + cov[j][1] * v[1] + cov[j][2] * v[2];
		}

		l = sqrt(nv[0] * nv[0] + nv[1] * nv[1] + nv[2] * nv[2]);

		if (l < 1e-12)
		{
			return false;
		}

		for (j = 0; j < 3; j++)
		{
			v[j] = nv[j] / l;
		}
	}

	for (i = 0; i < n; i++)
	{
		double h = 0;

		for (k = 0; k < 3; k++)
		{
			h += (c->v[i * 3 + k] - cen[k]) * v[k];
		}

		hmin = Q_min(hmin, h);
		hmax = Q_max(hmax, h);
	}

	band = 0.2 * (hmax - hmin);

	for (i = 0; i < n; i++)
	{
		double h = 0, q[3], r;

		for (k = 0; k < 3; k++)
		{
			h += (c->v[i * 3 + k] - cen[k]) * v[k];
		}

		for (k = 0; k < 3; k++)
		{
			q[k] = c->v[i * 3 + k] - cen[k] - h * v[k];
		}

		r = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);

		if (h <= hmin + band)
		{
			rlo += r;
			nlo++;
		}

		if (h >= hmax - band)
		{
			rhi += r;
			nhi++;
		}
	}

	rlo = nlo ? rlo / nlo : 0;
	rhi = nhi ? rhi / nhi : 0;

	/* the axis runs from the narrow end (at the lamp) to the wide end */
	if (rlo > rhi)
	{
		double t;

		for (k = 0; k < 3; k++)
		{
			v[k] = -v[k];
		}

		t = hmin;
		hmin = -hmax;
		hmax = -t;
		t = rlo;
		rlo = rhi;
		rhi = t;
	}

	b->len = (float)(hmax - hmin);

	if ((b->len < 8) || (rhi < 1))
	{
		return false;
	}

	/* where along the cone the painted texture actually shows light: t(h) by least squares over the
	   vertices, then the texture's row brightness; the dark tip above the lamp is no light */
	b->hs = 0.0f;
	b->he = 1.0f;
	{
		int ti = BeamTexIndex(c->tex);
		double sh = 0, st = 0, shh = 0, sht = 0, den, slope, icpt;

		for (i = 0; i < n && c->t; i++)
		{
			double h = -hmin, t = c->t[i];

			for (k = 0; k < 3; k++)
			{
				h += (c->v[i * 3 + k] - cen[k]) * v[k];
			}

			sh += h; st += t; shh += h * h; sht += h * t;
		}

		den = n * shh - sh * sh;

		if ((ti >= 0) && c->t && (fabs(den) > 1e-9))
		{
			float best = 0, prof[65];
			int s, first = -1, last = -1;

			slope = (n * sht - sh * st) / den;
			icpt = (st - slope * sh) / n;

			for (s = 0; s <= 64; s++)
			{
				double t = icpt + slope * b->len * s / 64.0, f, x;
				int r0i, r1i;

				t -= floor(t);
				x = t * BEAM_PROF - 0.5;
				r0i = (int)floor(x);
				f = x - r0i;
				r1i = (r0i + 1 + BEAM_PROF) % BEAM_PROF;
				r0i = (r0i + BEAM_PROF) % BEAM_PROF;
				prof[s] = (float)(B.markprof[ti][r0i] * (1 - f) + B.markprof[ti][r1i] * f);
				best = Q_max(best, prof[s]);
			}

			for (s = 0; s <= 64; s++)
			{
				if (prof[s] >= 0.2f * best)
				{
					if (first < 0)
					{
						first = s;
					}

					last = s;
				}
			}

			if ((first >= 0) && (last > first))
			{
				b->hs = Q_min(first / 64.0f, 0.6f);
				b->he = Q_max(last / 64.0f, b->hs + 0.2f);
			}
		}
	}

	for (k = 0; k < 3; k++)
	{
		b->axis[k] = (float)v[k];
		b->apex[k] = (float)(cen[k] + v[k] * hmin);
	}

	b->r0 = (float)Q_max(rlo, 0.5);
	b->r1 = (float)rhi;

	{
		int t = BeamTexIndex(c->tex);
		const float *col = (t >= 0) ? B.markcol[t] : vec3_origin;

		VectorCopy(col, b->color);
	}

	/* distance from the eye to the axis segment, for the choice of the nearest beams */
	{
		vec3_t d;
		float h;

		VectorSubtract(r_newrefdef.vieworg, b->apex, d);
		h = Q_clamp(DotProduct(d, b->axis), 0.0f, b->len);
		VectorMA(b->apex, h, b->axis, d);
		VectorSubtract(r_newrefdef.vieworg, d, d);
		b->dist = VectorLength(d);
	}

	return true;
}

/* column-major view-projection of a beam's shadow map: from the narrow end along the axis */
static void
BeamViewProj(const beam_t *b, float *vp)
{
	/* the near plane lies where the light starts, below the lamp housing, which so casts nothing */
	float view[16], proj[16], fov, t, n = Q_max(2.0f, b->len * Q_max(b->hs, 0.1f) + 4.0f), f = b->len * 1.3f + 16.0f;
	vec3_t fw, up = {0, 0, 1}, s, u;
	int i, j;

	VectorCopy(b->axis, fw);

	if (fabsf(fw[2]) > 0.95f)
	{
		VectorSet(up, 1, 0, 0);
	}

	CrossProduct(fw, up, s);
	VectorNormalize(s);
	CrossProduct(s, fw, u);

	view[0] = s[0]; view[1] = u[0]; view[2] = -fw[0]; view[3] = 0;
	view[4] = s[1]; view[5] = u[1]; view[6] = -fw[1]; view[7] = 0;
	view[8] = s[2]; view[9] = u[2]; view[10] = -fw[2]; view[11] = 0;
	view[12] = -DotProduct(s, b->apex);
	view[13] = -DotProduct(u, b->apex);
	view[14] = DotProduct(fw, b->apex);
	view[15] = 1;

	fov = 2.0f * atanf(b->r1 / b->len) + 0.17f;
	fov = Q_min(fov, 2.9f);
	t = 1.0f / tanf(fov * 0.5f);
	memset(proj, 0, sizeof(proj));
	proj[0] = t;
	proj[5] = t;
	proj[10] = (f + n) / (n - f);
	proj[11] = -1;
	proj[14] = 2 * f * n / (n - f);

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			vp[i * 4 + j] = proj[j] * view[i * 4] + proj[4 + j] * view[i * 4 + 1] + proj[8 + j] * view[i * 4 + 2] +
				proj[12 + j] * view[i * 4 + 3];
		}
	}
}

/* this view's casters without the lamp itself (triangles behind or around the narrow end) */
static int
FilterCasters(const beam_t *b)
{
	float keep = b->r0 * 1.5f + 12.0f;
	int i, n = 0;

	if (B.maxflt < B.numdyn)
	{
		float *d = realloc(B.flt, B.numdyn * sizeof(float));

		if (!d)
		{
			return 0;
		}

		B.flt = d;
		B.maxflt = B.numdyn;
	}

	for (i = 0; i + 9 <= B.numdyn; i += 9)
	{
		int k, behind = 0, close = 0;

		for (k = 0; k < 3; k++)
		{
			const float *p = B.dyn + i + k * 3;
			vec3_t d;
			float h;

			VectorSubtract(p, b->apex, d);
			h = DotProduct(d, b->axis);
			behind += (h < 4.0f);
			close += (VectorLength(d) < keep);
		}

		if ((behind == 3) || (close == 3))
		{
			continue;
		}

		memcpy(B.flt + n, B.dyn + i, 9 * sizeof(float));
		n += 9;
	}

	return n;
}

static qboolean
BeamProgram(void)
{
	GLuint v, f, p;
	GLint ok = 0;

	if (B.tried)
	{
		return B.prog != 0;
	}

	B.tried = true;
	v = CompilePost(GL_VERTEX_SHADER, post_vs);
	f = CompilePost(GL_FRAGMENT_SHADER, beam_fs);

	if (!v || !f)
	{
		if (v) glDeleteShader(v);
		if (f) glDeleteShader(f);
		R_Printf(PRINT_ALL, "r_om_beams: shader failed, light beams stay painted\n");
		return false;
	}

	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	GL3_Post_BindOutputs(p);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "r_om_beams: link failed: %s\n", log);
		glDeleteProgram(p);
		return false;
	}

	B.prog = p;
	B.u_depth = glGetUniformLocation(p, "depthtex");
	B.u_shadow = glGetUniformLocation(p, "beamshadow");
	B.u_invpv = glGetUniformLocation(p, "invpv");
	B.u_eye = glGetUniformLocation(p, "eye");
	B.u_prm = glGetUniformLocation(p, "prm");
	B.u_prm2 = glGetUniformLocation(p, "prm2");
	B.u_b0 = glGetUniformLocation(p, "b0");
	B.u_b1 = glGetUniformLocation(p, "b1");
	B.u_b2 = glGetUniformLocation(p, "b2");
	B.u_b3 = glGetUniformLocation(p, "b3");
	B.u_bvp = glGetUniformLocation(p, "bvp");
	B.u_rect = glGetUniformLocation(p, "rect");
	GL3_UseProgram(p);
	glUniform1i(B.u_depth, 7);
	glUniform1i(B.u_shadow, BEAM_UNIT);
	return true;
}

static qboolean
MoteProgram(void)
{
	GLuint v, f, p;
	GLint ok = 0;

	if (B.mtried)
	{
		return B.mprog != 0;
	}

	B.mtried = true;
	v = CompilePost(GL_VERTEX_SHADER, mote_vs);
	f = CompilePost(GL_FRAGMENT_SHADER, mote_fs);

	if (!v || !f)
	{
		if (v) glDeleteShader(v);
		if (f) glDeleteShader(f);
		R_Printf(PRINT_ALL, "r_om_beams: dust mote shader failed, beams without motes\n");
		return false;
	}

	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	GL3_Post_BindOutputs(p);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "r_om_beams: dust mote link failed: %s\n", log);
		glDeleteProgram(p);
		return false;
	}

	B.mprog = p;
	B.m_pv = glGetUniformLocation(p, "pv");
	B.m_eye = glGetUniformLocation(p, "eye");
	B.m_prm = glGetUniformLocation(p, "prm");
	B.m_b0 = glGetUniformLocation(p, "b0");
	B.m_b1 = glGetUniformLocation(p, "b1");
	B.m_b2 = glGetUniformLocation(p, "b2");
	B.m_b3 = glGetUniformLocation(p, "b3");
	B.m_bvp = glGetUniformLocation(p, "bvp");
	B.m_layer = glGetUniformLocation(p, "layer");
	B.m_shadow = glGetUniformLocation(p, "beamshadow");
	GL3_UseProgram(p);
	glUniform1i(B.m_shadow, BEAM_UNIT);
	return true;
}

static qboolean
ShadeProgram(void)
{
	GLuint v, f, p;
	GLint ok = 0;

	if (B.stried)
	{
		return B.sprog != 0;
	}

	B.stried = true;
	{
		char *src = GL3_CSM_Prepend(beamshade_fs);

		if (!src)
		{
			return false;
		}

		v = CompilePost(GL_VERTEX_SHADER, post_vs);
		f = CompilePost(GL_FRAGMENT_SHADER, src);
		free(src);
	}

	if (!v || !f)
	{
		if (v) glDeleteShader(v);
		if (f) glDeleteShader(f);
		R_Printf(PRINT_ALL, "r_om_beams: ground shadow shader failed\n");
		return false;
	}

	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	GL3_Post_BindOutputs(p);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "r_om_beams: ground shadow link failed: %s\n", log);
		glDeleteProgram(p);
		return false;
	}

	B.sprog = p;
	B.s_depth = glGetUniformLocation(p, "depthtex");
	B.s_shadow = glGetUniformLocation(p, "beamshadow");
	B.s_invpv = glGetUniformLocation(p, "invpv");
	B.s_eye = glGetUniformLocation(p, "eye");
	B.s_prm = glGetUniformLocation(p, "prm");
	B.s_b0 = glGetUniformLocation(p, "b0");
	B.s_b1 = glGetUniformLocation(p, "b1");
	B.s_b2 = glGetUniformLocation(p, "b2");
	B.s_b3 = glGetUniformLocation(p, "b3");
	B.s_bvp = glGetUniformLocation(p, "bvp");
	B.s_rect = glGetUniformLocation(p, "rect");
	GL3_UseProgram(p);
	GL3_CSM_BindProgram(p);
	glUniform1i(B.s_depth, 7);
	glUniform1i(B.s_shadow, BEAM_UNIT);
	return true;
}

static qboolean
BeamShadowTargets(void)
{
	GLenum st, none = GL_NONE;

	if (B.shadowfbo)
	{
		return true;
	}

	glGenTextures(1, &B.shadowtex);
	GL3_SelectTMU(GL_TEXTURE0 + BEAM_UNIT);
	glBindTexture(GL_TEXTURE_2D_ARRAY, B.shadowtex);
	/* layers 0..3: world + Ghoul casters, 4..7: the world alone (character shadows on the ground) */
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, BEAM_SHADOW, BEAM_SHADOW, 2 * BEAM_DRAW, 0,
		GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	GL3_SelectTMU(GL_TEXTURE0);

	glGenFramebuffers(1, &B.shadowfbo);
	glBindFramebuffer(GL_FRAMEBUFFER, B.shadowfbo);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, B.shadowtex, 0, 0);
	glDrawBuffers(1, &none);
	glReadBuffer(GL_NONE);
	st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);

	if (st != GL_FRAMEBUFFER_COMPLETE)
	{
		R_Printf(PRINT_ALL, "r_om_beams: shadow framebuffer incomplete (0x%x), beams without shadows\n", st);
		glDeleteFramebuffers(1, &B.shadowfbo);
		glDeleteTextures(1, &B.shadowtex);
		B.shadowfbo = B.shadowtex = 0;
		return false;
	}

	return true;
}

/* before the end of the view: the volumetric beams of the cones captured this view */
void
GL3_Post_Beams(void)
{
	beam_t bl[BEAM_MAXCAP];
	float b0[BEAM_DRAW * 4], b1[BEAM_DRAW * 4], b2[BEAM_DRAW * 4], b3[BEAM_DRAW * 4], vps[BEAM_DRAW * 16];
	int nb = 0, i, j;
	qboolean shadows = false;
	float fw, fh;

	B.npools = 0;

	if (!P.bound || !B.ncap)
	{
		return;
	}

	BeamCvars();

	for (i = 0; i < B.ncap; i++)
	{
		if (FitBeam(&B.cap[i], &bl[nb]))
		{
			nb++;
		}
	}

	if (!nb || !r_om_beams->value || !BeamProgram())
	{
		return;
	}

	/* the nearest ones */
	for (i = 1; i < nb; i++)
	{
		for (j = i; (j > 0) && (bl[j].dist < bl[j - 1].dist); j--)
		{
			beam_t t = bl[j];

			bl[j] = bl[j - 1];
			bl[j - 1] = t;
		}
	}

	nb = Q_min(nb, BEAM_DRAW);

	if (r_om_beams_debug->value >= 4)
	{
		/* the raw cone vertices relative to the fitted narrow end: h along the axis, r off it */
		for (i = 0; i < B.ncap; i++)
		{
			for (j = 0; j + 2 < B.cap[i].nv; j += 3)
			{
				const float *p = B.cap[i].v + j;
				vec3_t d, q;
				float h;

				VectorSubtract(p, bl[0].apex, d);
				h = DotProduct(d, bl[0].axis);
				VectorMA(d, -h, bl[0].axis, q);
				R_Printf(PRINT_ALL, "OMBEAMV %d: %.1f %.1f %.1f  h %.1f r %.1f t %.3f\n", i, p[0], p[1], p[2], h,
					VectorLength(q), B.cap[i].t ? B.cap[i].t[j / 3] : -1.0f);
			}
		}
	}

	if (r_om_beams_debug->value >= 3)
	{
		for (i = 0; i < nb; i++)
		{
			R_Printf(PRINT_ALL, "OMBEAM %d/%d: lamp %.0f %.0f %.0f axis %.2f %.2f %.2f len %.0f r %.1f..%.1f light %.2f..%.2f dist %.0f (%d verts, %d casters)\n",
				i, nb, bl[i].apex[0], bl[i].apex[1], bl[i].apex[2], bl[i].axis[0], bl[i].axis[1], bl[i].axis[2],
				bl[i].len, bl[i].r0, bl[i].r1, bl[i].hs, bl[i].he, bl[i].dist, B.cap[i].nv / 3, B.numdyn / 9);
		}
	}

	GL3_Draw3DBatchesNow();
	GL3_FX_Flush();

	/* the full depth of the view (models and effects included) for the march */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.depthfbo);
	glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
		GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	P.depthcopied = true;

	for (i = 0; i < nb; i++)
	{
		float *p0 = &b0[i * 4], *p1 = &b1[i * 4], *p2 = &b2[i * 4];

		BeamViewProj(&bl[i], vps + i * 16);
		VectorCopy(bl[i].apex, p0);
		p0[3] = bl[i].len;
		VectorCopy(bl[i].axis, p1);
		p1[3] = bl[i].r0;
		VectorCopy(bl[i].color, p2);
		p2[3] = bl[i].r1;
		b3[i * 4] = bl[i].hs;
		b3[i * 4 + 1] = bl[i].he;
		b3[i * 4 + 2] = b3[i * 4 + 3] = 0;

		/* kept for the next view's Ghoul ground shadows */
		memcpy(B.pools + i * 16, p0, 4 * sizeof(float));
		memcpy(B.pools + i * 16 + 4, p1, 4 * sizeof(float));
		memcpy(B.pools + i * 16 + 8, p2, 4 * sizeof(float));
		memcpy(B.pools + i * 16 + 12, b3 + i * 4, 4 * sizeof(float));
	}

	B.npools = (r_om_beams_groundshadow->value > 0) ? nb : 0;

	/* shadow maps: static world + this view's Ghoul casters, seen from each beam's lamp. The array exists
	   even without shadows: the shader's shadow sampler must always have a depth texture behind it. */
	if (!BeamShadowTargets())
	{
		return;
	}

	if (r_om_beams_shadows->value)
	{
		GLint oldfunc;
		GLfloat oldrange[2];
		GLboolean wasblend = glIsEnabled(GL_BLEND), wascull = glIsEnabled(GL_CULL_FACE);
		GLboolean wasoffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);

		glGetIntegerv(GL_DEPTH_FUNC, &oldfunc);
		glGetFloatv(GL_DEPTH_RANGE, oldrange);
		glBindFramebuffer(GL_FRAMEBUFFER, B.shadowfbo);
		glViewport(0, 0, BEAM_SHADOW, BEAM_SHADOW);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glDepthRange(0, 1);
		glDepthMask(GL_TRUE);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(1.5f, 2.0f);
		shadows = true;

		for (i = 0; i < nb; i++)
		{
			static const float one = 1.0f;
			int mode = (int)r_om_beams_shadows->value; /* 1 all, 2 world only, 3 models only (tests) */
			int nf = (mode == 2) ? 0 : FilterCasters(&bl[i]);

			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, B.shadowtex, 0, i);
			glClearBufferfv(GL_DEPTH, 0, &one);

			if (!GL3_CSM_DrawDepth(vps + i * 16, B.flt, nf, mode != 3))
			{
				shadows = false;
			}

			if (r_om_beams_groundshadow->value > 0)
			{
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, B.shadowtex, 0, BEAM_DRAW + i);
				glClearBufferfv(GL_DEPTH, 0, &one);
				GL3_CSM_DrawDepth(vps + i * 16, NULL, 0, true);
			}
		}

		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		glViewport(P.vx, P.vy, P.vw, P.vh);
		glDepthFunc(oldfunc);
		glDepthRange(oldrange[0], oldrange[1]);
		glPolygonOffset(0, 0);

		if (!wasoffset) glDisable(GL_POLYGON_OFFSET_FILL);
		if (wasblend) glEnable(GL_BLEND);
		if (wascull) glEnable(GL_CULL_FACE);
	}

	fw = (float)P.w;
	fh = (float)P.h;

	GL3_UseProgram(B.prog);
	glUniform4f(B.u_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	glUniformMatrix4fv(B.u_invpv, 1, GL_FALSE, GL3_CSM_InvPV());
	glUniform4f(B.u_eye, r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2], r_newrefdef.time);
	glUniform4f(B.u_prm, gl3depthmin, gl3depthmax, Q_clamp(r_om_beams_intensity->value, 0.0f, 4.0f),
		Q_clamp(r_om_beams_noise->value, 0.0f, 0.9f));
	glUniform4f(B.u_prm2, (float)nb, 24.0f, shadows ? 1.0f : 0.0f, 0.0f);
	glUniform4fv(B.u_b0, nb, b0);
	glUniform4fv(B.u_b1, nb, b1);
	glUniform4fv(B.u_b2, nb, b2);
	glUniform4fv(B.u_b3, nb, b3);
	glUniformMatrix4fv(B.u_bvp, nb, GL_FALSE, vps);

	GL3_SelectTMU(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);

	if (B.shadowtex)
	{
		GL3_SelectTMU(GL_TEXTURE0 + BEAM_UNIT);
		glBindTexture(GL_TEXTURE_2D_ARRAY, B.shadowtex);
	}

	GL3_SelectTMU(GL_TEXTURE0);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);

	/* the characters' shadows in the lamps' pools first (multiplied), then the light of the beams */
	if (shadows && (r_om_beams_groundshadow->value > 0) && ShadeProgram())
	{
		GL3_UseProgram(B.sprog);
		glUniform4f(B.s_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniformMatrix4fv(B.s_invpv, 1, GL_FALSE, GL3_CSM_InvPV());
		glUniform4f(B.s_prm, gl3depthmin, gl3depthmax, Q_clamp(r_om_beams_groundshadow->value, 0.0f, 1.0f), (float)nb);
		glUniform4fv(B.s_b0, nb, b0);
		glUniform4fv(B.s_b1, nb, b1);
		glUniform4fv(B.s_b2, nb, b2);
		glUniform4fv(B.s_b3, nb, b3);
		glUniformMatrix4fv(B.s_bvp, nb, GL_FALSE, vps);
		glUniform4f(B.s_eye, r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2], 0.0f);
		glBlendFunc(GL_ZERO, GL_SRC_COLOR);
		DrawQuad();

		/* inside the pools the lamp fills the sun's character shadow: dst * (1 + src) */
		if (GL3_CSM_Active())
		{
			glUniform4f(B.s_eye, r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2], 1.0f);
			glBlendFunc(GL_DST_COLOR, GL_ONE);
			DrawQuad();
		}

		GL3_UseProgram(B.prog);
	}

	glBlendFunc(GL_ONE, GL_ONE);
	DrawQuad();

	/* dust motes in the beams, depth-tested against the scene (the view's depth function and range stay) */
	if ((r_om_beams_motes->value > 0) && MoteProgram())
	{
		int count = (int)(BEAM_MOTES * Q_min(r_om_beams_motes->value, 4.0f));
		float pxu = P.vh / (2.0f * tanf(r_newrefdef.fov_y * (float)M_PI / 360.0f));

		GL3_UseProgram(B.mprog);
		glUniformMatrix4fv(B.m_pv, 1, GL_FALSE, &gl3state.uni3DData.transProjViewMat4.Elements[0][0]);
		glUniform4f(B.m_eye, r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2], r_newrefdef.time);
		glUniform4f(B.m_prm, 0.5f * Q_clamp(r_om_beams_intensity->value, 0.0f, 4.0f), pxu, shadows ? 1.0f : 0.0f, 0.0f);
		glEnable(GL_DEPTH_TEST);
#ifndef YQ2_GL3_GLES3
		glEnable(GL_PROGRAM_POINT_SIZE);
#endif
		GL3_BindVAO(P.vao);

		for (i = 0; i < nb; i++)
		{
			glUniform4fv(B.m_b0, 1, b0 + i * 4);
			glUniform4fv(B.m_b1, 1, b1 + i * 4);
			glUniform4fv(B.m_b2, 1, b2 + i * 4);
			glUniform4fv(B.m_b3, 1, b3 + i * 4);
			glUniformMatrix4fv(B.m_bvp, 1, GL_FALSE, vps + i * 16);
			glUniform1f(B.m_layer, (float)i);
			glDrawArrays(GL_POINTS, 0, count);
		}

#ifndef YQ2_GL3_GLES3
		glDisable(GL_PROGRAM_POINT_SIZE);
#endif
		glDisable(GL_DEPTH_TEST);
	}

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

static void
BeamShutdown(void)
{
	int i;

	if (B.prog)
	{
		if (gl3state.currentShaderProgram == B.prog)
		{
			gl3state.currentShaderProgram = 0;
		}

		glDeleteProgram(B.prog);
	}

	if (B.mprog)
	{
		if (gl3state.currentShaderProgram == B.mprog)
		{
			gl3state.currentShaderProgram = 0;
		}

		glDeleteProgram(B.mprog);
	}

	if (B.sprog)
	{
		if (gl3state.currentShaderProgram == B.sprog)
		{
			gl3state.currentShaderProgram = 0;
		}

		glDeleteProgram(B.sprog);
	}

	if (B.shadowfbo) glDeleteFramebuffers(1, &B.shadowfbo);
	if (B.shadowtex) glDeleteTextures(1, &B.shadowtex);

	for (i = 0; i < BEAM_MAXCAP; i++)
	{
		free(B.cap[i].v);
		free(B.cap[i].t);
	}

	free(B.dyn);
	free(B.flt);
	free(B.tmp);
	memset(&B, 0, sizeof(B));
}

/* ------------------------------------------------------------------
 * GFX-modern r_om_ssao: ambient occlusion of the view (GTAO). After the
 * whole view is drawn (before the light beams and the composite), the
 * final depth is copied and, at half resolution, each pixel's view-space
 * position and normal are rebuilt from it. Four screen directions (rotated
 * per pixel) are marched six steps each way within r_om_ssao_radius world
 * units; the highest horizon on both sides of each slice gives the
 * cosine-weighted visible arc around the normal (ground-truth AO, Jimenez
 * et al. 2016 / XeGTAO). Two depth-aware blur passes remove the noise and
 * a depth-aware upsample multiplies the scene by the result. The view
 * weapon (marked in the stencil while it is drawn), the sky, far pixels and
 * light sources (the emissive target) are left alone. r_om_ssao is the
 * strength (exponent of the visibility, 0 = off), r_om_ssao_debug 1 shows
 * the occlusion alone. Not in GLES3 builds (needs a float render target).
 * ------------------------------------------------------------------ */

static cvar_t *r_om_ssao, *r_om_ssao_radius, *r_om_ssao_debug, *r_om_ssao_full, *r_om_ssao_quality;
static cvar_t *r_om_ssao_temporal, *r_om_ssao_bounce;

static struct
{
	qboolean failed;
	int w, h, vw, vh;
	GLuint colortex;  /* scene texture the apply framebuffer was made for */
	GLuint aotex[2], aofbo[2], applyfbo, lintex, linfbo;
	GLuint histtex[2], histfbo[2], coltex, colfbo;  /* temporal history (ping-pong), scene colour copy */
	int histcur;
	qboolean havehist;
	hmm_mat4 prevpv;   /* projection * view of the last frame */
	float frame;
	GLuint proglin, progao, progblur, progapply, progtemp;
	GLint ln_depth, ln_rect, ln_proj;
	GLint ao_lin, ao_rect, ao_proj2, ao_params, ao_quality, ao_frame;
	GLint tm_cur, tm_hist, tm_rect, tm_reproj, tm_proj2, tm_keep;
	GLint ap_col, ap_bounce;
	GLint bl_tex, bl_rect, bl_step;
	GLint ap_ao, ap_depth, ap_emis, ap_rect, ap_proj, ap_size, ap_params;
} S;

static const char *ssao_common =
	"uniform sampler2D depthtex;\n"
	"uniform vec4 proj;\n"     /* projection C, D; depth range min, max */
	"float Dist(vec2 t)\n"
	"{\n"
	"	float z = texture(depthtex, t).r;\n"
	"	float ndc = (z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0;\n"
	"	return proj.y / (ndc + proj.x);\n"
	"}\n";

/* view distance at the occlusion resolution (one full-resolution sample per texel) */
static const char *ssao_lin_fs =
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	outColor = vec4(Dist(uv), 0.0, 0.0, 1.0);\n"
	"}\n";

static const char *ssao_ao_fs =
	"uniform sampler2D lintex;\n"
	"uniform vec4 proj2;\n"    /* 1/P00, 1/P11, texel of lintex */
	"uniform vec4 params;\n"   /* radius (units), texels per unit at distance 1, fade distance, max radius (texels) */
	"uniform ivec2 quality;\n" /* directions, steps each way */
	"uniform float frame;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"const float PI = 3.14159265;\n"
	"vec3 Pos(vec2 t)\n"
	"{\n"
	"	vec2 v = t * 2.0 - 1.0;\n"
	"	float d = texture(lintex, t).r;\n"
	"	return vec3(v.x * d * proj2.x, v.y * d * proj2.y, -d);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	vec2 px = proj2.zw;\n"
	"	vec2 uv = vuv;\n"
	"	vec2 lo = px * 0.5, hi = vec2(1.0) - px * 0.5;\n"
	"	vec3 P = Pos(uv);\n"
	"	float d = -P.z;\n"
	"	if (d > params.z || d <= 0.0)\n"
	"	{\n"
	"		outColor = vec4(1.0, d, 0.0, 1.0);\n"
	"		return;\n"
	"	}\n"
	/* normal from the neighbours with the smaller depth step (no smearing across edges) */
	"	vec3 Pr = Pos(uv + vec2(px.x, 0.0)), Pl = Pos(uv - vec2(px.x, 0.0));\n"
	"	vec3 Pu = Pos(uv + vec2(0.0, px.y)), Pd = Pos(uv - vec2(0.0, px.y));\n"
	"	vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;\n"
	"	vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;\n"
	"	vec3 N = normalize(cross(dx, dy));\n"
	"	vec3 V = normalize(-P);\n"
	"	if (dot(N, V) < 0.0) N = -N;\n"
	/* off the surface a little: depth steps of flat faces seen at a grazing angle are no occluders */
	"	P += N * (d * 0.004 + 0.25);\n"
	"	float rpx = min(params.x * params.y / d, params.w);\n"
	"	if (rpx < 2.0)\n"
	"	{\n"
	"		outColor = vec4(1.0, d, 0.0, 1.0);\n"
	"		return;\n"
	"	}\n"
	/* per-pixel rotation and step jitter (interleaved gradient noise) */
	/* the pattern moves every frame: the temporal pass averages the rotations */
	"	float n1 = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + frame * 0.618034);\n"
	"	float n2 = fract(n1 * 7.13 + 0.371);\n"
	"	float frange = params.x * 0.615, ffrom = params.x - frange;\n"
	"	float vis = 0.0;\n"
	"	int DIRS = quality.x;\n"
	"	int STEPS = quality.y;\n"
	"	for (int i = 0; i < DIRS; i++)\n"
	"	{\n"
	"		float phi = (float(i) + n1) * PI / float(DIRS);\n"
	"		vec2 dir = vec2(cos(phi), sin(phi));\n"
	"		vec3 dv = vec3(dir, 0.0);\n"
	"		vec3 ortho = dv - dot(dv, V) * V;\n"
	"		vec3 axis = normalize(cross(ortho, V));\n"
	"		vec3 pn = N - axis * dot(N, axis);\n"
	"		float pl = length(pn);\n"
	"		float cn = clamp(dot(pn, V) / max(pl, 1e-4), 0.0, 1.0);\n"
	"		float n = sign(dot(ortho, pn)) * acos(cn);\n"
	"		float low0 = cos(n + PI * 0.5), low1 = cos(n - PI * 0.5);\n"
	"		float hc0 = low0, hc1 = low1;\n"
	"		for (int s = 0; s < STEPS; s++)\n"
	"		{\n"
	"			float t = (float(s) + n2) / float(STEPS);\n"
	"			vec2 o = dir * (t * t * rpx + 1.0) * px;\n"
	"			vec2 ta = uv + o, tb = uv - o;\n"
	"			vec3 a = Pos(clamp(ta, lo, hi)) - P;\n"
	"			vec3 b = Pos(clamp(tb, lo, hi)) - P;\n"
	"			float la = length(a), lb = length(b);\n"
	/* samples beyond the view edge take the edge pixel: an occluder keeps its shade until it has left the view
	   (ignoring them made the occlusion fade out towards the edges while turning) */
	"			float wa = clamp((ffrom - la) / frange + 1.0, 0.0, 1.0);\n"
	"			float wb = clamp((ffrom - lb) / frange + 1.0, 0.0, 1.0);\n"
	"			hc0 = max(hc0, mix(low0, dot(a, V) / max(la, 1e-4), wa));\n"
	"			hc1 = max(hc1, mix(low1, dot(b, V) / max(lb, 1e-4), wb));\n"
	"		}\n"
	"		float h0 = -acos(clamp(hc1, -1.0, 1.0)), h1 = acos(clamp(hc0, -1.0, 1.0));\n"
	"		h0 = n + clamp(h0 - n, -PI * 0.5, PI * 0.5);\n"
	"		h1 = n + clamp(h1 - n, -PI * 0.5, PI * 0.5);\n"
	"		float sn = sin(n);\n"
	"		vis += pl * ((cn + 2.0 * h0 * sn - cos(2.0 * h0 - n)) + (cn + 2.0 * h1 * sn - cos(2.0 * h1 - n))) * 0.25;\n"
	"	}\n"
	"	vis = clamp(vis / float(DIRS), 0.0, 1.0);\n"
	"	outColor = vec4(vis, d, 0.0, 1.0);\n"
	"}\n";

static const char *ssao_blur_fs =
	"uniform sampler2D tex;\n"
	"uniform vec2 step;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec2 c = texture(tex, vuv).rg;\n"
	"	float sum = c.r, ws = 1.0;\n"
	"	for (int i = -4; i <= 4; i++)\n"
	"	{\n"
	"		if (i == 0) continue;\n"
	"		vec2 s = texture(tex, vuv + step * float(i)).rg;\n"
	"		float w = exp(-float(i * i) / 12.0) * exp(-abs(s.g - c.g) * 30.0 / max(c.g, 1.0));\n"
	"		sum += s.r * w;\n"
	"		ws += w;\n"
	"	}\n"
	"	outColor = vec4(sum / ws, c.g, 0.0, 1.0);\n"
	"}\n";

static const char *ssao_apply_fs =
	"uniform sampler2D aotex;\n"
	"uniform sampler2D emis;\n"
	"uniform sampler2D col;\n"  /* the scene colour (a smaller copy) */
	"uniform float bounce;\n"   /* multi-bounce amount (0 = plain occlusion) */
	"uniform vec4 size;\n"     /* occlusion target width, height and texel */
	"uniform vec4 params;\n"   /* strength, debug */
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	float d = Dist(uv);\n"
	"	vec2 p = vuv * size.xy - 0.5;\n"
	"	vec2 f = fract(p), b = (floor(p) + 0.5) * size.zw;\n"
	"	vec2 s00 = texture(aotex, b).rg, s10 = texture(aotex, b + vec2(size.z, 0.0)).rg;\n"
	"	vec2 s01 = texture(aotex, b + vec2(0.0, size.w)).rg, s11 = texture(aotex, b + size.zw).rg;\n"
	"	vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);\n"
	"	float k = 25.0 / max(d, 1.0);\n"
	"	w *= vec4(exp(-abs(s00.g - d) * k), exp(-abs(s10.g - d) * k), exp(-abs(s01.g - d) * k), exp(-abs(s11.g - d) * k));\n"
	"	w += 1e-4;\n"
	"	float ao = dot(w, vec4(s00.r, s10.r, s01.r, s11.r)) / dot(w, vec4(1.0));\n"
	"	vec3 e = texture(emis, uv).rgb;\n"
	"	ao = mix(ao, 1.0, clamp(max(e.r, max(e.g, e.b)) * 4.0, 0.0, 1.0));\n"
	"	ao = pow(ao, params.x);\n"
	/* multi-bounce (Jimenez 2016): light bounced inside the occluded corner lifts it by the surface colour;
	   the albedo is guessed from the scene colour (its hue at a moderate brightness) */
	"	vec3 c = texture(col, vuv).rgb;\n"
	"	vec3 al = clamp(c / max(max(c.r, max(c.g, c.b)), 0.2) * 0.65, 0.0, 0.9) * bounce;\n"
	"	vec3 a = 2.0404 * al - 0.3324, bb = -4.7951 * al + 0.6417, cc = 2.7552 * al + 0.6903;\n"
	"	vec3 mb = max(vec3(ao), ((ao * a + bb) * ao + cc) * ao);\n"
	"	outColor = vec4(mb, 1.0);\n"
	"}\n";

/* temporal accumulation: last frame's occlusion reprojected into this frame where the depth agrees */
static const char *ssao_temp_fs =
	"uniform sampler2D cur;\n"   /* this frame: visibility, distance */
	"uniform sampler2D hist;\n"  /* the accumulated result of the last frame */
	"uniform mat4 reproj;\n"     /* this frame's view space -> last frame's clip space */
	"uniform vec4 proj2;\n"      /* 1/P00, 1/P11 */
	"uniform float keep;\n"      /* weight of the history (0 = off) */
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec2 c = texture(cur, vuv).rg;\n"
	"	float d = c.g;\n"
	"	vec2 v = vuv * 2.0 - 1.0;\n"
	"	vec4 pc = reproj * vec4(v.x * d * proj2.x, v.y * d * proj2.y, -d, 1.0);\n"
	"	float r = c.r;\n"
	"	if (keep > 0.0 && pc.w > 0.0)\n"
	"	{\n"
	"		vec2 pu = pc.xy / pc.w * 0.5 + 0.5;\n"
	"		if (all(greaterThan(pu, vec2(0.0))) && all(lessThan(pu, vec2(1.0))))\n"
	"		{\n"
	"			vec2 h = texture(hist, pu).rg;\n"
	"			if (abs(h.g - pc.w) < 0.04 * pc.w + 1.0)\n"
	"				r = mix(c.r, h.r, keep);\n"
	"		}\n"
	"	}\n"
	"	outColor = vec4(r, d, 0.0, 1.0);\n"
	"}\n";

static qboolean
SSAOWanted(void)
{
#ifdef YQ2_GL3_GLES3
	return false;
#else
	if (!r_om_ssao)
	{
		r_om_ssao = ri.Cvar_Get("r_om_ssao", "1.5", CVAR_ARCHIVE);
		r_om_ssao_radius = ri.Cvar_Get("r_om_ssao_radius", "48", CVAR_ARCHIVE);
		r_om_ssao_debug = ri.Cvar_Get("r_om_ssao_debug", "0", 0);
		r_om_ssao_full = ri.Cvar_Get("r_om_ssao_full", "0", CVAR_ARCHIVE);
		r_om_ssao_quality = ri.Cvar_Get("r_om_ssao_quality", "3", CVAR_ARCHIVE);
		r_om_ssao_temporal = ri.Cvar_Get("r_om_ssao_temporal", "0.85", CVAR_ARCHIVE);
		r_om_ssao_bounce = ri.Cvar_Get("r_om_ssao_bounce", "0.6", CVAR_ARCHIVE);
	}

	return !S.failed && (r_om_ssao->value > 0);
#endif
}

static GLuint
SSAOLink(const char *a, const char *b)
{
	size_t la = strlen(a), lb = strlen(b);
	char *src = malloc(la + lb + 1);
	GLuint p;

	if (!src)
	{
		return 0;
	}

	memcpy(src, a, la);
	memcpy(src + la, b, lb + 1);
	p = LinkPost(src);
	free(src);
	return p;
}

static qboolean
SSAOPrograms(void)
{
	if (S.progapply)
	{
		return true;
	}

	S.proglin = SSAOLink(ssao_common, ssao_lin_fs);
	S.progao = SSAOLink("", ssao_ao_fs);
	S.progblur = SSAOLink("", ssao_blur_fs);
	S.progapply = SSAOLink(ssao_common, ssao_apply_fs);
	S.progtemp = SSAOLink("", ssao_temp_fs);

	if (!S.proglin || !S.progao || !S.progblur || !S.progapply || !S.progtemp)
	{
		return false;
	}

	S.ln_depth = glGetUniformLocation(S.proglin, "depthtex");
	S.ln_rect = glGetUniformLocation(S.proglin, "rect");
	S.ln_proj = glGetUniformLocation(S.proglin, "proj");
	S.ao_lin = glGetUniformLocation(S.progao, "lintex");
	S.ao_rect = glGetUniformLocation(S.progao, "rect");
	S.ao_quality = glGetUniformLocation(S.progao, "quality");
	S.ao_proj2 = glGetUniformLocation(S.progao, "proj2");
	S.ao_params = glGetUniformLocation(S.progao, "params");
	S.bl_tex = glGetUniformLocation(S.progblur, "tex");
	S.bl_rect = glGetUniformLocation(S.progblur, "rect");
	S.bl_step = glGetUniformLocation(S.progblur, "step");
	S.ap_ao = glGetUniformLocation(S.progapply, "aotex");
	S.ap_depth = glGetUniformLocation(S.progapply, "depthtex");
	S.ap_emis = glGetUniformLocation(S.progapply, "emis");
	S.ap_rect = glGetUniformLocation(S.progapply, "rect");
	S.ap_proj = glGetUniformLocation(S.progapply, "proj");
	S.ap_size = glGetUniformLocation(S.progapply, "size");
	S.ap_params = glGetUniformLocation(S.progapply, "params");
	S.ap_col = glGetUniformLocation(S.progapply, "col");
	S.ap_bounce = glGetUniformLocation(S.progapply, "bounce");
	S.ao_frame = glGetUniformLocation(S.progao, "frame");
	S.tm_cur = glGetUniformLocation(S.progtemp, "cur");
	S.tm_hist = glGetUniformLocation(S.progtemp, "hist");
	S.tm_rect = glGetUniformLocation(S.progtemp, "rect");
	S.tm_reproj = glGetUniformLocation(S.progtemp, "reproj");
	S.tm_proj2 = glGetUniformLocation(S.progtemp, "proj2");
	S.tm_keep = glGetUniformLocation(S.progtemp, "keep");
	return true;
}

static void
SSAODeleteTargets(void)
{
	if (S.aofbo[0]) glDeleteFramebuffers(2, S.aofbo);
	if (S.aotex[0]) glDeleteTextures(2, S.aotex);
	if (S.applyfbo) glDeleteFramebuffers(1, &S.applyfbo);
	if (S.linfbo) glDeleteFramebuffers(1, &S.linfbo);
	if (S.lintex) glDeleteTextures(1, &S.lintex);
	if (S.histfbo[0]) glDeleteFramebuffers(2, S.histfbo);
	if (S.histtex[0]) glDeleteTextures(2, S.histtex);
	if (S.colfbo) glDeleteFramebuffers(1, &S.colfbo);
	if (S.coltex) glDeleteTextures(1, &S.coltex);
	S.aofbo[0] = S.aofbo[1] = S.aotex[0] = S.aotex[1] = S.applyfbo = S.linfbo = S.lintex = 0;
	S.histfbo[0] = S.histfbo[1] = S.histtex[0] = S.histtex[1] = S.colfbo = S.coltex = 0;
	S.havehist = false;
	S.w = S.h = S.vw = S.vh = 0;
	S.colortex = 0;
}

static qboolean
SSAOTargets(void)
{
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	int div = (r_om_ssao_full->value != 0) ? 1 : 2;
	int w = Q_max(P.vw / div, 1), h = Q_max(P.vh / div, 1), i;

	if (S.aotex[0] && (S.vw == P.vw) && (S.vh == P.vh) && (S.w == w) && (S.colortex == P.texcolor))
	{
		return true;
	}

	SSAODeleteTargets();

	for (i = 0; i < 2; i++)
	{
		S.aotex[i] = NewTexture(GL_RG16F, GL_RG, GL_HALF_FLOAT, w, h, GL_NEAREST);
		glGenFramebuffers(1, &S.aofbo[i]);
		glBindFramebuffer(GL_FRAMEBUFFER, S.aofbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.aotex[i], 0);
		glDrawBuffers(1, one);

		if (!Complete("ssao"))
		{
			return false;
		}
	}

	for (i = 0; i < 2; i++)
	{
		S.histtex[i] = NewTexture(GL_RG16F, GL_RG, GL_HALF_FLOAT, w, h, GL_LINEAR);
		glGenFramebuffers(1, &S.histfbo[i]);
		glBindFramebuffer(GL_FRAMEBUFFER, S.histfbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.histtex[i], 0);
		glDrawBuffers(1, one);

		if (!Complete("ssao history"))
		{
			return false;
		}
	}

	S.coltex = NewTexture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h, GL_LINEAR);
	glGenFramebuffers(1, &S.colfbo);
	glBindFramebuffer(GL_FRAMEBUFFER, S.colfbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.coltex, 0);
	glDrawBuffers(1, one);

	if (!Complete("ssao colour"))
	{
		return false;
	}

	S.lintex = NewTexture(GL_R32F, GL_RED, GL_FLOAT, w, h, GL_NEAREST);
	glGenFramebuffers(1, &S.linfbo);
	glBindFramebuffer(GL_FRAMEBUFFER, S.linfbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, S.lintex, 0);
	glDrawBuffers(1, one);

	if (!Complete("ssao depth"))
	{
		return false;
	}

	/* without MSAA the scene texture is drawn through a framebuffer of its own (the emissive
	   texture of the scene framebuffer is read meanwhile); the stencil comes along for the weapon */
	if (!P.samples)
	{
		glGenFramebuffers(1, &S.applyfbo);
		glBindFramebuffer(GL_FRAMEBUFFER, S.applyfbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.texcolor, 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, P.rbdepth);
		glDrawBuffers(1, one);

		if (!Complete("ssao apply"))
		{
			return false;
		}
	}

	S.w = w;
	S.h = h;
	S.vw = P.vw;
	S.vh = P.vh;
	S.colortex = P.texcolor;
	return true;
}

/* while the view weapon is drawn: stencil bit 0x80 marks its pixels */
void
GL3_Post_MarkWeapon(qboolean begin)
{
	if (!ssaomark || !P.bound)
	{
		return;
	}

	if (begin)
	{
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_ALWAYS, 0x80, 0x80);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0x80);
	}
	else
	{
		glDisable(GL_STENCIL_TEST);
		glStencilMask(0xff);
	}
}

void
GL3_Post_SSAO(void)
{
	static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	float fw = (float)P.w, fh = (float)P.h, C, D, rect[4];
	qboolean nv = (r_fullbright->value != 0) || (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES);
	int dbg;

	if (!P.bound || nv || !ssaomark || !SSAOWanted())
	{
		return;
	}

	if (!SSAOPrograms())
	{
		S.failed = true;
		return;
	}

	if (!SSAOTargets())
	{
		SSAODeleteTargets();
		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		S.failed = true;
		return;
	}

	GL3_Draw3DBatchesNow();
	GL3_FX_Flush();
	dbg = (int)r_om_ssao_debug->value;

	/* the finished depth (and, with MSAA, the emissive target) as textures */
	glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.depthfbo);
	glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
		GL_DEPTH_BUFFER_BIT, GL_NEAREST);

	if (P.samples)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT1);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.resolvefbo[1]);
		glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_FALSE);

	C = gl3state.projMat3D.Elements[2][2];
	D = gl3state.projMat3D.Elements[3][2];
	rect[0] = P.vx / fw;
	rect[1] = P.vy / fh;
	rect[2] = P.vw / fw;
	rect[3] = P.vh / fh;

	/* view distance at the occlusion resolution */
	glBindFramebuffer(GL_FRAMEBUFFER, S.linfbo);
	glViewport(0, 0, S.w, S.h);
	GL3_UseProgram(S.proglin);
	glUniform1i(S.ln_depth, 0);
	glUniform4fv(S.ln_rect, 1, rect);
	glUniform4f(S.ln_proj, C, D, gl3depthmin, gl3depthmax);
	BindTex(GL_TEXTURE0, P.texdepthcopy);
	DrawQuad();

	/* occlusion */
	glBindFramebuffer(GL_FRAMEBUFFER, S.aofbo[0]);
	GL3_UseProgram(S.progao);
	glUniform1i(S.ao_lin, 0);
	glUniform4f(S.ao_rect, 0, 0, 1, 1);
	glUniform4f(S.ao_proj2, 1.0f / gl3state.projMat3D.Elements[0][0], 1.0f / gl3state.projMat3D.Elements[1][1],
		1.0f / S.w, 1.0f / S.h);
	glUniform4f(S.ao_params, Q_max(r_om_ssao_radius->value, 4.0f),
		0.5f * S.w * gl3state.projMat3D.Elements[0][0], r_farclipdist->value * 0.98f, 0.1f * S.h);
	{
		/* 1: 2 directions x 4 steps, 2: 3 x 5, 3: 4 x 6 */
		int q = Q_max(1, Q_min((int)r_om_ssao_quality->value, 3));

		glUniform2i(S.ao_quality, q + 1, q + 3);
	}
	S.frame = (S.frame >= 1000.0f) ? 0.0f : S.frame + 1.0f;
	glUniform1f(S.ao_frame, S.frame);
	BindTex(GL_TEXTURE0, S.lintex);
	DrawQuad();

	/* temporal accumulation into the next history texture: this view space -> last frame's clip space */
	{
		const hmm_mat4 *v = &gl3state.viewMat3D;
		hmm_mat4 inv, reproj;
		int next = S.histcur ^ 1, r, c;

		/* the view matrix is a rotation and a translation: inverse = transposed rotation, -R^T t */
		memset(&inv, 0, sizeof(inv));
		for (c = 0; c < 3; c++)
		{
			for (r = 0; r < 3; r++)
			{
				inv.Elements[c][r] = v->Elements[r][c];
			}
		}
		for (r = 0; r < 3; r++)
		{
			inv.Elements[3][r] = -(inv.Elements[0][r] * v->Elements[3][0] + inv.Elements[1][r] * v->Elements[3][1] +
				inv.Elements[2][r] * v->Elements[3][2]);
		}
		inv.Elements[3][3] = 1.0f;
		reproj = HMM_MultiplyMat4(S.prevpv, inv);

		glBindFramebuffer(GL_FRAMEBUFFER, S.histfbo[next]);
		GL3_UseProgram(S.progtemp);
		glUniform1i(S.tm_cur, 0);
		glUniform1i(S.tm_hist, 1);
		glUniform4f(S.tm_rect, 0, 0, 1, 1);
		glUniformMatrix4fv(S.tm_reproj, 1, GL_FALSE, &reproj.Elements[0][0]);
		glUniform4f(S.tm_proj2, 1.0f / gl3state.projMat3D.Elements[0][0], 1.0f / gl3state.projMat3D.Elements[1][1], 0, 0);
		glUniform1f(S.tm_keep, S.havehist ? Q_max(0.0f, Q_min(r_om_ssao_temporal->value, 0.95f)) : 0.0f);
		BindTex(GL_TEXTURE1, S.histtex[S.histcur]);
		BindTex(GL_TEXTURE0, S.aotex[0]);
		DrawQuad();
		S.histcur = next;
		S.havehist = true;
		S.prevpv = HMM_MultiplyMat4(gl3state.projMat3D, gl3state.viewMat3D);
	}

	/* depth-aware blur, horizontal then vertical */
	GL3_UseProgram(S.progblur);
	glUniform1i(S.bl_tex, 0);
	glUniform4f(S.bl_rect, 0, 0, 1, 1);
	glBindFramebuffer(GL_FRAMEBUFFER, S.aofbo[1]);
	BindTex(GL_TEXTURE0, S.histtex[S.histcur]);
	glUniform2f(S.bl_step, 1.0f / S.w, 0.0f);
	DrawQuad();
	glBindFramebuffer(GL_FRAMEBUFFER, S.aofbo[0]);
	BindTex(GL_TEXTURE0, S.aotex[1]);
	glUniform2f(S.bl_step, 0.0f, 1.0f / S.h);
	DrawQuad();

	/* a small copy of the scene colour for the multi-bounce tint (with MSAA resolved first) */
	if (P.samples)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.resolvefbo[0]);
		glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.resolvefbo[0]);
	}
	else
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	}
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, S.colfbo);
	glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, 0, 0, S.w, S.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	/* multiply the scene (not the weapon) */
	if (P.samples)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		glDrawBuffers(1, one);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, S.applyfbo);
	}

	glViewport(P.vx, P.vy, P.vw, P.vh);
	GL3_UseProgram(S.progapply);
	glUniform1i(S.ap_ao, 0);
	glUniform1i(S.ap_depth, 1);
	glUniform1i(S.ap_emis, 2);
	glUniform4fv(S.ap_rect, 1, rect);
	glUniform4f(S.ap_proj, C, D, gl3depthmin, gl3depthmax);
	glUniform4f(S.ap_size, (float)S.w, (float)S.h, 1.0f / S.w, 1.0f / S.h);
	glUniform4f(S.ap_params, Q_min(r_om_ssao->value, 4.0f), (float)dbg, 0, 0);
	glUniform1i(S.ap_col, 3);
	glUniform1f(S.ap_bounce, Q_max(0.0f, Q_min(r_om_ssao_bounce->value, 1.5f)));
	BindTex(GL_TEXTURE3, S.coltex);
	BindTex(GL_TEXTURE2, P.texemis);
	BindTex(GL_TEXTURE1, P.texdepthcopy);
	BindTex(GL_TEXTURE0, S.aotex[0]);

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_EQUAL, 0, 0x80);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilMask(0);

	if (!dbg)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ZERO, GL_SRC_COLOR);
	}

	DrawQuad();

	glDisable(GL_STENCIL_TEST);
	glStencilMask(0xff);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);

	/* back to the scene framebuffer for the rest of the view */
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	glDrawBuffers(2, bufs);
	glViewport(P.vx, P.vy, P.vw, P.vh);
	gl3state.currentlightmap = -1;
	GL3_SelectTMU(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);
	GL3_SelectTMU(GL_TEXTURE0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

static void
SSAOShutdown(void)
{
	SSAODeleteTargets();

	if (S.proglin) glDeleteProgram(S.proglin);
	if (S.progtemp) glDeleteProgram(S.progtemp);
	if (S.progao) glDeleteProgram(S.progao);
	if (S.progblur) glDeleteProgram(S.progblur);
	if (S.progapply) glDeleteProgram(S.progapply);

	if ((gl3state.currentShaderProgram == S.proglin) || (gl3state.currentShaderProgram == S.progao) ||
		(gl3state.currentShaderProgram == S.progblur) ||
		(gl3state.currentShaderProgram == S.progapply))
	{
		gl3state.currentShaderProgram = 0;
	}

	memset(&S, 0, sizeof(S));
}
