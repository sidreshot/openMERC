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

/* float colour targets that GLES 3.0 cannot render to without EXT_color_buffer_half_float (the half-size fog,
   the mirrors): RGBA8 there, the effects work instead of switching themselves off */
#ifdef YQ2_GL3_GLES3
#define OM_RT_FMT GL_RGBA8
#define OM_RT_TYPE GL_UNSIGNED_BYTE
#else
#define OM_RT_FMT GL_RGBA16F
#define OM_RT_TYPE GL_HALF_FLOAT
#endif


gl3post_t gl3post;

static cvar_t *r_om_bloom, *r_om_bloom_threshold, *r_om_bloom_intensity, *r_om_bloom_radius;
static cvar_t *r_om_bloom_surfscale, *r_om_bloom_ghoulmin, *r_om_bloom_fullbright, *r_om_bloom_fx;
static cvar_t *r_om_bloom_fromscene, *r_om_softparticles, *r_om_softparticles_dist;
static cvar_t *r_om_softparticles_add, *r_om_post_debug, *r_om_gputime;
static cvar_t *r_om_overbright, *r_om_overbright_scale, *r_om_overbright_models;
static cvar_t *r_om_hdr, *r_om_hdr_key, *r_om_hdr_adapt, *r_om_hdr_min, *r_om_hdr_max, *r_om_hdr_saturation;
static cvar_t *r_om_hdr_overbright, *r_om_hdr_debug, *r_om_hdr_adapt_strength;
static cvar_t *r_om_hdr_curve, *r_om_hdr_shadows, *r_om_hdr_brightness;

static qboolean VolFogWanted(void);
static void VolFogShutdown(void);
static qboolean BeamsWanted(void);
static void BeamsBeginView(void);
static void BeamShutdown(void);
static qboolean SSAOWanted(void);
static void SSAOShutdown(void);
static qboolean ssaomark; /* r_om_ssao runs this view: the weapon is marked in the stencil */
static qboolean SSRWanted(void);
static void SSRShutdown(void);
static void FxaaShutdown(void);
static void WXShutdown(void);
static void SSAODeleteTargets(void);
static void SSRDeleteTargets(void);
static void MirrorShutdown(void);
static void TimersShutdown(void);
static void SSRKeepColour(void);
static cvar_t *r_om_ssr, *r_om_ssr_wet, *r_om_ssr_debug, *r_om_ssr_dist, *r_om_ssr_quality;
static cvar_t *r_om_ssr_glint, *r_om_ssr_damp, *r_om_ssr_smooth, *r_om_ssr_temporal, *r_om_ssr_ripple, *r_om_ssr_checker;

static struct
{
	qboolean failed;
	int w, h, samples;
	GLuint fbo, resolvefbo[3], depthfbo;
	GLuint rbcolor, rbemis, rbmat, rbdepth;
	GLuint texcolor, texemis, texmat, texdepthcopy;
	qboolean mat;           /* r_om_ssr: the targets have the material attachment (2) */

	int nlevels, lw[POST_MAXLEVELS], lh[POST_MAXLEVELS];
	int chainw, chainh;
	GLuint levelfbo[POST_MAXLEVELS], leveltex[POST_MAXLEVELS];

	GLuint vao;
	GLuint progdown, progup, progcomp;
	GLint down_tex, down_rect, down_half, down_mode, down_knee, down_clamp;
	GLint up_tex, up_rect, up_half;
	GLint comp_scene, comp_bloom, comp_emis, comp_rect, comp_intensity, comp_mode;
	GLint comp_hdr, comp_exp, comp_sat, comp_grade;

	/* r_om_hdr: float scene, log luminance with mipmaps, exposure adapted over time (1x1 ping-pong) */
	qboolean hdr;           /* the targets were made with a float scene */
	GLuint lumtex, lumfbo, exptex[2], expfbo[2];
	int expcur;
	float exptime;
	GLuint proglum, progexp;
	GLint lum_tex, lum_rect, exp_lum, exp_prev, exp_params, exp_rect, exp_neutral;

	qboolean bound, bloomthis, depthcopied;
	qboolean depthfinal;    /* the depth copy holds the finished view (r_om_ssao made it) */
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

/* r_om_gputime 2: GPU and CPU time of each part of the frame (timestamps at GL3_Post_ProfMark), mean and worst over
   200 frames ("OMPROF"), and every frame whose GPU or CPU time is more than twice the running mean ("OMSPIKE") with
   its parts: where the drops come from */
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif

#define PROFMARKS 24

typedef void (APIENTRY *pfnQueryCounter)(GLuint, GLenum);

static struct
{
	qboolean tried;
	pfnQueryCounter counter;
	GLuint q[GPUQ][PROFMARKS];
	const char *name[GPUQ][PROFMARKS];
	double cpu[GPUQ][PROFMARKS];
	int n[GPUQ];
	int cur, frames;
	double sum[PROFMARKS], worst[PROFMARKS], csum[PROFMARKS], cworst[PROFMARKS];
	const char *sname[PROFMARKS];
	int sn;
	double mean, cmean;
	LARGE_INTEGER freq;
} PR;

/* perf tour: every change of r_om_prof_tag prints "OMSPOT <old tag>" with the frames timed since the last change -
   whole frame (wall) and GPU mean/worst and each part's GPU mean(worst) summed by name, so parts that come and go
   (mirrors, lightning) do not restart the sums */
static cvar_t *r_om_prof_tag;

static struct
{
	const char *name[PROFMARKS * 2];
	double sum[PROFMARKS * 2], worst[PROFMARKS * 2];
	int nn, frames, walls, age;
	double gsum, gworst, wsum, wworst, settle;
	char tag[64];
} SP;

static void
SpotFlush(void)
{
	char line[1400];
	size_t len = 0;
	int i;

	if (SP.frames > 0 && SP.walls > 0)
	{
		len += snprintf(line + len, sizeof(line) - len, "OMSPOT %s frames %d wall %.2f(%.1f) settle %.1f gpu %.2f(%.1f) |", SP.tag,
			SP.frames, SP.wsum / SP.walls, SP.wworst, SP.settle, SP.gsum / SP.frames, SP.gworst);

		for (i = 0; i < SP.nn && len < sizeof(line) - 40; i++)
		{
			len += snprintf(line + len, sizeof(line) - len, " %s %.2f(%.1f)", SP.name[i], SP.sum[i] / SP.frames,
				SP.worst[i]);
		}

		R_Printf(PRINT_ALL, "%s\n", line);
	}

	memset(&SP, 0, sizeof(SP));
	Q_strlcpy(SP.tag, r_om_prof_tag->string, sizeof(SP.tag));
}

static void
ProfCollect(int f)
{
	GLuint64 t[PROFMARKS];
	double gpu = 0, cpu = 0;
	int i, n = PR.n[f];

	if (n < 2)
	{
		return;
	}

	for (i = 0; i < n; i++)
	{
		T.getui64(PR.q[f][i], GL_QUERY_RESULT, &t[i]);
	}

	gpu = (double)(t[n - 1] - t[0]) / 1.0e6;
	cpu = PR.cpu[f][n - 1] - PR.cpu[f][0];

	/* a frame with other parts than the ones being summed starts the sums again */
	if (PR.sn != n - 1)
	{
		memset(PR.sum, 0, sizeof(PR.sum));
		memset(PR.worst, 0, sizeof(PR.worst));
		memset(PR.csum, 0, sizeof(PR.csum));
		memset(PR.cworst, 0, sizeof(PR.cworst));
		PR.frames = 0;
		PR.sn = n - 1;
	}

	for (i = 1; i < n; i++)
	{
		double g = (double)(t[i] - t[i - 1]) / 1.0e6, c = PR.cpu[f][i] - PR.cpu[f][i - 1];

		PR.sname[i - 1] = PR.name[f][i];
		PR.sum[i - 1] += g;
		PR.csum[i - 1] += c;
		PR.worst[i - 1] = fmax(PR.worst[i - 1], g);
		PR.cworst[i - 1] = fmax(PR.cworst[i - 1], c);

		if (SP.tag[0])
		{
			int k;

			for (k = 0; k < SP.nn && strcmp(SP.name[k], PR.name[f][i]); k++)
			{
			}

			if (k == SP.nn && SP.nn < PROFMARKS * 2)
			{
				SP.name[SP.nn++] = PR.name[f][i];
			}

			if (k < SP.nn)
			{
				SP.sum[k] += g;
				SP.worst[k] = fmax(SP.worst[k], g);
			}
		}
	}

	if (SP.tag[0])
	{
		SP.frames++;
		SP.gsum += gpu;
		SP.gworst = fmax(SP.gworst, gpu);
	}

	if (PR.mean > 0 && (gpu > PR.mean * 2.0 + 1.0 || cpu > PR.cmean * 2.0 + 1.0))
	{
		char line[1024];
		size_t len = 0;

		len += snprintf(line + len, sizeof(line) - len, "OMSPIKE gpu %.2f (mean %.2f) cpu %.2f (mean %.2f):", gpu, PR.mean,
			cpu, PR.cmean);

		for (i = 1; i < n && len < sizeof(line) - 40; i++)
		{
			len += snprintf(line + len, sizeof(line) - len, " %s %.2f/%.2f", PR.name[f][i],
				(double)(t[i] - t[i - 1]) / 1.0e6, PR.cpu[f][i] - PR.cpu[f][i - 1]);
		}

		R_Printf(PRINT_ALL, "%s\n", line);
	}

	PR.mean = PR.mean > 0 ? PR.mean * 0.95 + gpu * 0.05 : gpu;
	PR.cmean = PR.cmean > 0 ? PR.cmean * 0.95 + cpu * 0.05 : cpu;

	if (++PR.frames >= 200)
	{
		char line[1024];
		size_t len = 0;

		len += snprintf(line + len, sizeof(line) - len, "OMPROF gpu/cpu mean(worst):");

		for (i = 0; i < PR.sn && len < sizeof(line) - 48; i++)
		{
			len += snprintf(line + len, sizeof(line) - len, " %s %.2f(%.1f)/%.2f(%.1f)", PR.sname[i], PR.sum[i] / PR.frames,
				PR.worst[i], PR.csum[i] / PR.frames, PR.cworst[i]);
		}

		R_Printf(PRINT_ALL, "%s\n", line);
		PR.sn = 0;
	}
}

void
GL3_Post_ProfMark(const char *name)
{
	LARGE_INTEGER now;
	int f;

	if (!r_om_gputime || r_om_gputime->value < 2 || !T.getui64)
	{
		return;
	}

	if (!PR.tried)
	{
		typedef PROC (WINAPI *pfnWglGetProcAddress)(LPCSTR);
		HMODULE gl = GetModuleHandleA("opengl32.dll");
		pfnWglGetProcAddress wgp = gl ? (pfnWglGetProcAddress)(void *)GetProcAddress(gl, "wglGetProcAddress") : NULL;

		PR.tried = true;
		PR.counter = wgp ? (pfnQueryCounter)(void *)wgp("glQueryCounter") : NULL;
		QueryPerformanceFrequency(&PR.freq);

		if (PR.counter)
		{
			glGenQueries(GPUQ * PROFMARKS, &PR.q[0][0]);
		}
	}

	if (!PR.counter)
	{
		return;
	}

	QueryPerformanceCounter(&now);

	/* "frame" starts the next frame: the oldest one's results are read first (GPUQ frames ago); the whole frame
	   (game, sound and the rest of the client too) is timed from one start to the next */
	if (!strcmp(name, "frame"))
	{
		static double last, fmean;
		double t = (double)now.QuadPart * 1000.0 / (double)PR.freq.QuadPart;

		if (last > 0)
		{
			double dt = t - last;

			if (fmean > 0 && dt > fmean * 2.0 + 2.0)
			{
				R_Printf(PRINT_ALL, "OMHITCH frame %.1f ms (mean %.1f)\n", dt, fmean);
			}

			fmean = fmean > 0 ? fmean * 0.95 + dt * 0.05 : dt;

			if (SP.tag[0] && ++SP.age <= 30)
			{
				SP.settle = fmax(SP.settle, dt); /* the first frames at a new place (uploads, first sight) */
			}
			else if (SP.tag[0])
			{
				SP.walls++;
				SP.wsum += dt;
				SP.wworst = fmax(SP.wworst, dt);
			}
		}

		last = t;

		if (r_om_prof_tag->modified)
		{
			r_om_prof_tag->modified = false;
			SpotFlush();
		}
		PR.cur = (PR.cur + 1) % GPUQ;

		if (PR.n[PR.cur])
		{
			ProfCollect(PR.cur);
		}

		PR.n[PR.cur] = 0;
	}

	f = PR.cur;

	if (PR.n[f] >= PROFMARKS)
	{
		return;
	}

	PR.counter(PR.q[f][PR.n[f]], GL_TIMESTAMP);
	PR.name[f][PR.n[f]] = name;
	PR.cpu[f][PR.n[f]] = (double)now.QuadPart * 1000.0 / (double)PR.freq.QuadPart;
	PR.n[f]++;
}
#else
void
GL3_Post_GpuTimer(qboolean begin)
{
	(void)begin;
}

void
GL3_Post_ProfMark(const char *name)
{
	(void)name;
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
		Append(&out, &ol, &oc, "layout(location = 2) out vec4 outMaterial;\n");
		Append(&out, &ol, &oc, joined);
		Append(&out, &ol, &oc, "layout(location = 0) out vec4 outColor;");
		Append(&out, &ol, &oc, oc0 + strlen("out vec4 outColor;"));
	}
	(void)decl;
#else
	decl = "out vec4 outEmissive;\nout vec4 outMaterial;\n";
	Append(&out, &ol, &oc, decl);
	Append(&out, &ol, &oc, joined);
#endif

	Append(&out, &ol, &oc,
		"\nvoid main()\n{\n"
		"\toutEmissive = vec4(0.0, 0.0, 0.0, -1.0);\n"
		/* r_om_ssr: reflectivity, roughness x reflectivity (world shaders); nothing reflects by default */
		"\toutMaterial = vec4(0.0, 0.0, 0.0, -1.0);\n"
		"\tomMn();\n"
		/* r_om_hdr: a float scene target does not clamp what the shader writes; alpha out of 0..1 or negative
		   colour would break blending (an 8-bit target clamped them); colour above 1 is the HDR headroom */
		"\toutColor = vec4(clamp(outColor.rgb, 0.0, 64.0), clamp(outColor.a, 0.0, 1.0));\n"
		"\tif (outEmissive.a < 0.0) outEmissive = vec4(0.0, 0.0, 0.0, outColor.a);\n"
		"\tif (outMaterial.a < 0.0) outMaterial = vec4(0.0, 0.0, 0.0, outColor.a);\n"
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
	glBindFragDataLocation(prog, 2, "outMaterial");
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
	"uniform vec4 grade;\n"  /* curve (0 shoulder, 1 ACES), shadow gamma, brightness, r_om_overbright (linear) */
	/* r_om_look (gl3_look.c): the picture graded in float instead of SoF's 8-bit colour table */
	"uniform int look;\n"
	"uniform vec4 look0;\n"  /* exposure (EV), toe, shoulder start, saturation */
	"uniform vec4 look1;\n"  /* shadow tint rgb, menu contrast power */
	"uniform vec4 look2;\n"  /* highlight tint rgb, menu gamma power (1 - vid_gamma) */
	"uniform vec4 look3;\n"  /* vignette, grain, time, LUT strength */
	"uniform vec4 look4;\n"  /* aspect, LUT size, auto exposure share, menu brightness offset */
	"uniform sampler2D lut;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	/* ACES filmic curve (Narkowicz fit): soft shoulder for the highlights, a little more contrast in the mids */
	"vec3 Aces(vec3 x) { return clamp(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }\n"
	/* shoulder only: unchanged up to 0.6 (linear), above it rolls off smoothly towards white (no darkened toe:
	   ACES made the dark SoF maps about a third darker in the shadows) */
	"vec3 Shoulder(vec3 x)\n"
	"{\n"
	"	const float T = 0.6;\n"
	"	vec3 s = T + (1.0 - T) * (1.0 - exp(-(x - T) / (1.0 - T)));\n"
	"	return mix(x, s, step(T, x));\n"
	"}\n"
	"float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
	/* a tint that keeps the luminance */
	"vec3 Tint(vec3 t) { return t / max(Luma(t), 1e-3); }\n"
	/* filmic curve: the toe presses the blacks down with mid grey (0.18) fixed, the shoulder rolls the highlights off
	   from its start towards white */
	"vec3 Film(vec3 x, float toe, float sh)\n"
	"{\n"
	"	x = pow(max(x, vec3(0.0)) / 0.18, vec3(1.0 + toe)) * 0.18;\n"
	"	vec3 s = sh + (1.0 - sh) * (1.0 - exp(-(x - sh) / max(1.0 - sh, 1e-3)));\n"
	"	return clamp(mix(x, s, step(sh, x)), 0.0, 1.0);\n"
	"}\n"
	/* the LUT strip: N slices of N x N, blue across the slices */
	"vec3 Lut(vec3 c, float n)\n"
	"{\n"
	"	float b = clamp(c.b, 0.0, 1.0) * (n - 1.0), b0 = floor(b), b1 = min(b0 + 1.0, n - 1.0);\n"
	"	vec2 p = vec2(clamp(c.r, 0.0, 1.0) * (n - 1.0) + 0.5, clamp(c.g, 0.0, 1.0) * (n - 1.0) + 0.5) / vec2(n * n, n);\n"
	"	vec3 a = texture(lut, p + vec2(b0 / n, 0.0)).rgb, d = texture(lut, p + vec2(b1 / n, 0.0)).rgb;\n"
	"	return mix(a, d, b - b0);\n"
	"}\n"
	"float Hash(vec2 p) { vec3 q = fract(vec3(p.xyx) * 0.1031); q += dot(q, q.yzx + 33.33); return fract((q.x + q.y) * q.z); }\n"
	"vec3 Grade(vec3 c, float autoexp)\n"
	"{\n"
	/* the scene is display-encoded: to linear, exposure (auto by the look's share, look, menu brightness) */
	"	vec3 l = pow(max(c, vec3(0.0)), vec3(2.2)) * pow(autoexp, look4.z) * exp2(look0.x) * grade.w;\n"
	/* split toning by luminance: cold shadows, warm light, luminance kept */
	"	float y = Luma(l);\n"
	"	float t = smoothstep(0.02, 0.35, pow(y, 1.0 / 2.2));\n"
	"	l *= mix(Tint(look1.rgb), Tint(look2.rgb), t);\n"
	"	l = max(mix(vec3(Luma(l)), l, look0.w), vec3(0.0));\n"
	"	l = Film(l, look0.y, look0.z);\n"
	"	vec3 d = pow(l, vec3(1.0 / 2.2));\n"
	/* the menu's gamma, contrast and brightness as SoF's colour table had them (SPEC-413 §9), in float: v^(1-g), an S
	   curve of power k around the middle, an offset */
	"	d = pow(max(d, vec3(0.0)), vec3(look2.a));\n"
	"	vec3 e = pow(abs(2.0 * d - 1.0), vec3(look1.a));\n"
	/* the offset lifts what is already lit, not black (on the finished picture a plain offset turned every black
	   into grey; SoF's table added it to the texture and the light separately) */
	"	d = 0.5 + 0.5 * sign(d - 0.5) * e;\n"
	"	d = clamp(d + look4.w * (1.0 - d) * smoothstep(0.0, 0.2, d) * 2.0, 0.0, 1.0);\n"
	"	if (look3.w > 0.0) d = mix(d, Lut(d, look4.y), look3.w);\n"
	"	vec2 v = (vuv - 0.5) * vec2(look4.x, 1.0);\n"
	"	d *= 1.0 - look3.x * smoothstep(0.25, 0.95, length(v));\n"
	/* film grain, stronger in the dark, and a dither against banding in the deep shadows */
	"	float g = Hash(gl_FragCoord.xy + fract(look3.z * 7.13) * 512.0) - 0.5;\n"
	"	d += g * (look3.y * (1.0 - 0.6 * Luma(d)) + 1.0 / 255.0);\n"
	"	return clamp(d, 0.0, 1.0);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	vec3 c;\n"
	"	if (mode == 2) c = texture(emis, uv).rgb;\n"
	"	else if (mode == 3) c = texture(bloom, vuv).rgb * intensity;\n"
	"	else\n"
	"	{\n"
	"		c = texture(scene, uv).rgb;\n"
	"		if (mode == 1) c += texture(bloom, vuv).rgb * intensity;\n"
	"		if (look != 0)\n"
	"			c = Grade(c, hdr != 0 ? texture(exptex, vec2(0.5)).r : 1.0);\n"
	"		else if (hdr != 0)\n"
	"		{\n"
	/* the scene is display-encoded (the shaders applied vid_gamma): to linear, expose, curve, back */
	"			vec3 l = pow(max(c, vec3(0.0)), vec3(2.2)) * texture(exptex, vec2(0.5)).r * grade.z * grade.w;\n"
	"			l = grade.x > 0.5 ? Aces(l) : Shoulder(l);\n"
	"			float y = dot(l, vec3(0.2126, 0.7152, 0.0722));\n"
	"			l = clamp(mix(vec3(y), l, saturation), 0.0, 1.0);\n"
	/* shadow gamma in display space: below 1 opens the dark tones, white stays white */
	"			c = pow(pow(l, vec3(1.0 / 2.2)), vec3(grade.y));\n"
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
	"	if (any(isnan(l)) || any(isinf(l))) l = vec3(0.0);\n"   /* one bad pixel must not blacken the whole exposure */
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
	const char *version = GL3_GLES_HEADER;
#else
	const char *version = "#version 150\n";
#endif
	const char *srcs[2] = {version, src};
	GLuint s = glCreateShader(type);
	GLint ok = 0;
#ifdef YQ2_GL3_GLES3
	/* GLSL ES needs a location on every output of a shader with several (desktop: glBindFragDataLocation) */
	static const char *const outs[4][2] = {{"out vec4 outColor;", "layout(location = 0) out vec4 outColor;"},
		{"out vec4 outEmissive;", "layout(location = 1) out vec4 outEmissive;"},
		{"out vec4 outEmis;", "layout(location = 1) out vec4 outEmis;"},
		{"out vec4 outMaterial;", "layout(location = 2) out vec4 outMaterial;"}};
	char *fix = NULL;

	if ((type == GL_FRAGMENT_SHADER) && !strstr(src, "layout(location"))
	{
		size_t len = strlen(src) + 128;
		int k;

		fix = malloc(len);

		if (fix)
		{
			Q_strlcpy(fix, src, len);

			for (k = 0; k < 4; k++)
			{
				char *at = strstr(fix, outs[k][0]);
				size_t ol = strlen(outs[k][0]), nlen = strlen(outs[k][1]);

				if (at && (strlen(fix) + nlen - ol < len))
				{
					memmove(at + nlen, at + ol, strlen(at + ol) + 1);
					memcpy(at, outs[k][1], nlen);
				}
			}

			srcs[1] = fix;
		}
	}
#endif

	glShaderSource(s, 2, srcs, NULL);
	glCompileShader(s);
#ifdef YQ2_GL3_GLES3
	free(fix);
#endif
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
	P.comp_grade = glGetUniformLocation(P.progcomp, "grade");
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

	/* the SSAO / SSR framebuffers attach the scene colour and depth: with new scene targets (r_om_hdr toggled) the
	   driver hands out the same names again, their keys matched and they kept drawing into the deleted ones */
	SSAODeleteTargets();
	SSRDeleteTargets();

	if (P.fbo) glDeleteFramebuffers(1, &P.fbo);
	for (i = 0; i < 3; i++)
	{
		if (P.resolvefbo[i]) glDeleteFramebuffers(1, &P.resolvefbo[i]);
		P.resolvefbo[i] = 0;
	}
	if (P.depthfbo) glDeleteFramebuffers(1, &P.depthfbo);
	if (P.rbcolor) glDeleteRenderbuffers(1, &P.rbcolor);
	if (P.rbemis) glDeleteRenderbuffers(1, &P.rbemis);
	if (P.rbmat) glDeleteRenderbuffers(1, &P.rbmat);
	if (P.rbdepth) glDeleteRenderbuffers(1, &P.rbdepth);
	if (P.texcolor) glDeleteTextures(1, &P.texcolor);
	if (P.texemis) glDeleteTextures(1, &P.texemis);
	if (P.texmat) glDeleteTextures(1, &P.texmat);
	P.rbmat = P.texmat = 0;
	P.mat = false;
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

	P.fbo = P.depthfbo = 0;
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

/* the draw buffers of the scene framebuffer (bound): scene, emissive and, for r_om_ssr, the material target */
static void
SceneBuffers(void)
{
	static const GLenum bufs[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};

	glDrawBuffers(P.mat ? 3 : 2, bufs);
}

void
GL3_Post_SceneDrawBuffers(void)
{
	SceneBuffers();
}

/* r_om_ssr: this view writes the material target */
qboolean
GL3_Post_SSRActive(void)
{
	return P.bound && P.mat;
}

/* r_om_ssao runs this view: it darkens the floor at a character's feet, the Ghoul ground blob's contact part goes */
qboolean
GL3_Post_SSAOActive(void)
{
	return P.bound && ssaomark;
}

/* r_om_ssr: off while a pass modulates faces already drawn (their reflectivity stays) */
void
GL3_Post_MaterialWrites(qboolean on)
{
	if (P.bound && P.mat)
	{
		glColorMaski(2, on, on, on, on);
	}
}

static qboolean
CreateTargets(int w, int h)
{
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
	P.mat = SSRWanted();

	if (P.mat)
	{
		P.texmat = NewTexture(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h, GL_NEAREST);
	}

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

		if (P.mat)
		{
			P.rbmat = NewRenderbuffer(GL_RGBA8, w, h, P.samples);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_RENDERBUFFER, P.rbmat);
		}
	}
	else
	{
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.texcolor, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, P.texemis, 0);

		if (P.mat)
		{
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, P.texmat, 0);
		}
	}

	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, P.rbdepth);
	SceneBuffers();

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

		for (i = 0; i < (P.mat ? 3 : 2); i++)
		{
			glGenFramebuffers(1, &P.resolvefbo[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, P.resolvefbo[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
				(i == 2) ? P.texmat : i ? P.texemis : P.texcolor, 0);
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

/* samples of the framebuffer the 3D draws go to (alpha-to-coverage needs a multisampled target): set where the
   view's framebuffer is bound, so the draw paths need not ask the driver (glGetIntegerv(GL_SAMPLE_BUFFERS) waited
   for the driver thread once per alpha-tested Ghoul primitive and per world batch) */
int gl3_drawsamples;
static int windowsamples = -1;

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
#if defined(_WIN32) && !defined(YQ2_GL3_GLES3)
	r_om_prof_tag = ri.Cvar_Get("r_om_prof_tag", "", 0);
#endif
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
	r_om_hdr_curve = ri.Cvar_Get("r_om_hdr_curve", "0", CVAR_ARCHIVE);
	r_om_hdr_shadows = ri.Cvar_Get("r_om_hdr_shadows", "0.9", CVAR_ARCHIVE);
	r_om_hdr_brightness = ri.Cvar_Get("r_om_hdr_brightness", "1.1", CVAR_ARCHIVE);
	r_om_hdr_overbright = ri.Cvar_Get("r_om_hdr_overbright", "1", CVAR_ARCHIVE);
	r_om_hdr_debug = ri.Cvar_Get("r_om_hdr_debug", "0", 0);

	memset(&P, 0, sizeof(P));
	memset(&gl3post, 0, sizeof(gl3post));
}

/* vid_restart: the timer queries are names of the old context (r_om_gputime gave GL errors and garbage after it) */
static void
TimersShutdown(void)
{
#if defined(_WIN32) && !defined(YQ2_GL3_GLES3)
	int i;

	if (T.tried && T.getui64)
	{
		glDeleteQueries(GPUQ, T.q);
	}

	if (PR.tried && PR.counter)
	{
		for (i = 0; i < GPUQ; i++)
		{
			glDeleteQueries(PROFMARKS, PR.q[i]);
		}
	}

	memset(&T, 0, sizeof(T));
	memset(&PR, 0, sizeof(PR));
#endif
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
	windowsamples = -1;
	FxaaShutdown();
	WXShutdown();
	MirrorShutdown();
	TimersShutdown();
	VolFogShutdown();
	BeamShutdown();
	SSAOShutdown();
	SSRShutdown();

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
	P.bound = P.depthcopied = P.depthfinal = false;

	if (windowsamples < 0)
	{
		GLint sb = 0;

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glGetIntegerv(GL_SAMPLE_BUFFERS, &sb);
		windowsamples = sb;
	}

	gl3_drawsamples = windowsamples; /* the window, unless the post path binds its own framebuffer below */
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

	if (!bloom && !soft && !volfog && !beams && !ssao && !HDRWanted() && !(SSRWanted() && !nv) &&
		!r_om_post_debug->value)
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

	if ((P.w != vid.width) || (P.h != vid.height) || !P.fbo || (P.hdr != HDRWanted()) || (P.mat != SSRWanted()))
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

	if (P.mat)
	{
		glColorMaski(2, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearBufferfv(GL_COLOR, 2, zero);
	}

	P.bound = true;
	gl3_drawsamples = P.samples;
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
	/* neutral keeps the original mid tones: 1 for the shoulder curve (unchanged below its knee), 0.72 for ACES
	   (ACES(0.13) ~ 0.18); the lightmap headroom (overbright, display-encoded) is taken back */
	/* r_om_overbright (the brighter light of the menu) is not in the lightmaps while r_om_hdr gives them headroom:
	   it comes back as part of the neutral exposure, so the adaptation measures the brighter scene and the curve
	   rolls off what it pushes past white */
	glUniform2f(P.exp_neutral, (r_om_hdr_curve->value > 0.5f ? 0.72f : 1.0f) / powf(GL3_Post_Overbright(), 2.2f) *
		((r_om_overbright && r_om_overbright->value) ? powf(Q_clamp(r_om_overbright_scale->value, 1.0f, 4.0f), 2.2f) : 1.0f),
		Q_max(r_om_hdr_adapt_strength->value, 0.0f));
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

/* ------------------------------------------------------------------
 * GFX-modern r_om_fxaa: FXAA (after Lottes, FXAA 3.11 quality: local
 * contrast test, edge direction from the 3x3 luma, a search along the edge
 * in growing steps, the blend offset from the nearer end, a sub-pixel term
 * for thin lines). The composite then draws into a view-size LDR texture and
 * this pass brings it to the window. -1 (default) = on when the window has no
 * MSAA (ports, r_msaa_samples 0), 0 off, 1 always. Only the 3D view; the HUD
 * comes later.
 * ------------------------------------------------------------------ */

static cvar_t *r_om_fxaa;

static struct
{
	qboolean tried;
	GLuint prog, tex, fbo;
	GLint u_tex, u_rcp, u_rect;
	int w, h;
} FA;

static const char *fxaa_fs =
	"uniform sampler2D tex;\n"
	"uniform vec4 rcp;\n"     /* 1 / texture width, 1 / height, subpixel amount, edge threshold */
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"float L(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
	"float LT(vec2 p) { return L(texture(tex, p).rgb); }\n"
	"void main()\n"
	"{\n"
	"	vec2 p = uv;\n"
	"	vec3 cM = texture(tex, p).rgb;\n"
	"	float lM = L(cM);\n"
	"	float lN = L(textureOffset(tex, p, ivec2(0, 1)).rgb), lS = L(textureOffset(tex, p, ivec2(0, -1)).rgb);\n"
	"	float lE = L(textureOffset(tex, p, ivec2(1, 0)).rgb), lW = L(textureOffset(tex, p, ivec2(-1, 0)).rgb);\n"
	"	float mx = max(lM, max(max(lN, lS), max(lE, lW))), mn = min(lM, min(min(lN, lS), min(lE, lW)));\n"
	"	float range = mx - mn;\n"
	"	if (range < max(0.0312, mx * rcp.w)) { outColor = vec4(cM, 1.0); return; }\n"
	"	float lNW = L(textureOffset(tex, p, ivec2(-1, 1)).rgb), lNE = L(textureOffset(tex, p, ivec2(1, 1)).rgb);\n"
	"	float lSW = L(textureOffset(tex, p, ivec2(-1, -1)).rgb), lSE = L(textureOffset(tex, p, ivec2(1, -1)).rgb);\n"
	"	float lNS = lN + lS, lWE = lW + lE;\n"
	"	float eH = abs(-2.0 * lW + lNW + lSW) + 2.0 * abs(-2.0 * lM + lNS) + abs(-2.0 * lE + lNE + lSE);\n"
	"	float eV = abs(-2.0 * lS + lSW + lSE) + 2.0 * abs(-2.0 * lM + lWE) + abs(-2.0 * lN + lNW + lNE);\n"
	"	bool horz = eH >= eV;\n"
	"	float stepLen = horz ? rcp.y : rcp.x;\n"
	"	float l1 = horz ? lS : lW, l2 = horz ? lN : lE;\n"
	"	float g1 = abs(l1 - lM), g2 = abs(l2 - lM);\n"
	"	bool is1 = g1 >= g2;\n"
	"	float grad = 0.25 * max(g1, g2);\n"
	"	if (is1) stepLen = -stepLen;\n"
	"	float lLocal = 0.5 * ((is1 ? l1 : l2) + lM);\n"
	"	vec2 cur = p;\n"
	"	if (horz) cur.y += 0.5 * stepLen; else cur.x += 0.5 * stepLen;\n"
	"	vec2 off = horz ? vec2(rcp.x, 0.0) : vec2(0.0, rcp.y);\n"
	"	vec2 u1 = cur - off, u2 = cur + off;\n"
	"	float e1 = LT(u1) - lLocal, e2 = LT(u2) - lLocal;\n"
	"	bool r1 = abs(e1) >= grad, r2 = abs(e2) >= grad;\n"
	"	const float Q[10] = float[10](1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0);\n"
	"	for (int i = 1; i < 10; i++)\n"
	"	{\n"
	"		if (r1 && r2) break;\n"
	"		if (!r1) { u1 -= off * Q[i]; e1 = LT(u1) - lLocal; r1 = abs(e1) >= grad; }\n"
	"		if (!r2) { u2 += off * Q[i]; e2 = LT(u2) - lLocal; r2 = abs(e2) >= grad; }\n"
	"	}\n"
	"	float d1 = horz ? p.x - u1.x : p.y - u1.y, d2 = horz ? u2.x - p.x : u2.y - p.y;\n"
	"	bool near1 = d1 < d2;\n"
	"	float edgeOff = 0.5 - min(d1, d2) / max(d1 + d2, 1e-6);\n"
	"	bool correct = ((near1 ? e1 : e2) < 0.0) != (lM < lLocal);\n"
	"	float f = correct ? edgeOff : 0.0;\n"
	"	float lAvg = (2.0 * (lNS + lWE) + (lNW + lNE + lSW + lSE)) / 12.0;\n"
	"	float s1 = clamp(abs(lAvg - lM) / range, 0.0, 1.0);\n"
	"	float s2 = (-2.0 * s1 + 3.0) * s1 * s1;\n"
	"	f = max(f, s2 * s2 * rcp.z);\n"
	"	if (horz) p.y += f * stepLen; else p.x += f * stepLen;\n"
	"	outColor = vec4(texture(tex, p).rgb, 1.0);\n"
	"}\n";

static void
FxaaShutdown(void)
{
	if (FA.prog) glDeleteProgram(FA.prog);
	if (FA.tex) glDeleteTextures(1, &FA.tex);
	if (FA.fbo) glDeleteFramebuffers(1, &FA.fbo);
	memset(&FA, 0, sizeof(FA));
}

/* true when this view goes through FXAA (its framebuffer is then bound) */
static qboolean
FxaaBegin(void)
{
	if (!r_om_fxaa)
	{
		r_om_fxaa = ri.Cvar_Get("r_om_fxaa", "-1", CVAR_ARCHIVE);
	}

	if (!r_om_fxaa->value || ((r_om_fxaa->value < 0) && P.samples) || (r_om_post_debug->value != 0))
	{
		return false;
	}

	if (!FA.prog)
	{
		if (FA.tried)
		{
			return false;
		}

		FA.tried = true;
		FA.prog = LinkPost(fxaa_fs);

		if (!FA.prog)
		{
			R_Printf(PRINT_ALL, "r_om_fxaa: the shader did not link, FXAA is off\n");
			return false;
		}

		FA.u_tex = glGetUniformLocation(FA.prog, "tex");
		FA.u_rcp = glGetUniformLocation(FA.prog, "rcp");
		FA.u_rect = glGetUniformLocation(FA.prog, "rect");
	}

	if (!FA.fbo || (FA.w != P.w) || (FA.h != P.h))
	{
		static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};

		if (FA.tex) glDeleteTextures(1, &FA.tex);
		if (FA.fbo) glDeleteFramebuffers(1, &FA.fbo);
		glGenTextures(1, &FA.tex);
		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, FA.tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, P.w, P.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl3state.currenttexture = -1;
		glGenFramebuffers(1, &FA.fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, FA.fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, FA.tex, 0);
		glDrawBuffers(1, one);
		FA.w = P.w;
		FA.h = P.h;

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			FxaaShutdown();
			FA.tried = true;
			R_Printf(PRINT_ALL, "r_om_fxaa: no framebuffer, FXAA is off\n");
			return false;
		}
	}

	glBindFramebuffer(GL_FRAMEBUFFER, FA.fbo);
	return true;
}

/* the composited view from the FXAA texture to the window */
static void
FxaaEnd(void)
{
	float fw = (float)FA.w, fh = (float)FA.h;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(P.vx, P.vy, P.vw, P.vh);
	GL3_UseProgram(FA.prog);
	glUniform1i(FA.u_tex, 0);
	glUniform4f(FA.u_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	glUniform4f(FA.u_rcp, 1.0f / fw, 1.0f / fh, 0.75f, 0.125f);
	BindTex(GL_TEXTURE0, FA.tex);
	DrawQuad();
}

/* end of the 3D view: resolve, bloom and composite into the default framebuffer */
void
GL3_Post_EndView(void)
{
	float fw, fh;
	int mode, dbg;
	qboolean hdr, fxaa;

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

	/* r_om_ssr: this view for the reflections of the next frame */
	if (P.mat)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.samples ? P.resolvefbo[0] : P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		SSRKeepColour();
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

	/* r_om_fxaa: the composite goes to the FXAA texture first */
	fxaa = FxaaBegin();

	if (!fxaa)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

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
	{
		float ob = 1.0f; /* (r_om_overbright is in the exposure, HDRExposure) */

		glUniform4f(P.comp_grade, r_om_hdr_curve->value, Q_max(0.3f, Q_min(r_om_hdr_shadows->value, 2.0f)),
			Q_max(0.1f, r_om_hdr_brightness->value), ob);
	}
	if (hdr)
	{
		BindTex(GL_TEXTURE3, P.exptex[P.expcur]);
	}
	/* r_om_look: the grade of this view (not over the debug views nor night vision) */
	if (GL3_Look_Active() && !dbg && !(r_newrefdef.rdflags & SOF_RDF_IRGOGGLES))
	{
		GL3_Look_Uniforms(P.progcomp, r_newrefdef.time, (float)P.vw / (float)Q_max(P.vh, 1));
	}
	else
	{
		glUniform1i(GL3_UniformLoc(P.progcomp, "look"), 0);
	}
	BindTex(GL_TEXTURE2, P.texemis);
	BindTex(GL_TEXTURE1, P.nlevels ? P.leveltex[0] : P.texemis);
	BindTex(GL_TEXTURE0, P.texcolor);
	DrawQuad();

	if (fxaa)
	{
		FxaaEnd();
	}

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
static cvar_t *r_om_volfog_shafts, *r_om_volfog_noise, *r_om_volfog_half;

static struct
{
	qboolean tried;
	GLuint prog, progup;
	GLint u_depth, u_invpv, u_eye, u_prm, u_prm2, u_vr, u_rect;
	GLint up_fog, up_depth, up_rect, up_proj, up_size;
	GLuint halftex, halffbo;   /* the fog at half size (r_om_volfog_half) */
	int hw, hh;
	float vr[VOLFOG_MAXREGIONS * 32];
} V;

/* r_om_volfog_half: the fog marched at half size and brought up to the view where the depth agrees (the fog is
   smooth; the march at every pixel of a 1080p view took 2-9 ms in the fog of ger1, nyc1 and kos2) */
static const char *volfog_up_fs =
	"uniform sampler2D fogtex;\n"    /* half size: premultiplied fog colour, transmittance */
	"uniform sampler2D depthtex;\n"
	"uniform vec4 rect;\n"
	"uniform vec4 proj;\n"           /* projection C, D; depth range min, max */
	"uniform vec4 size;\n"           /* half size: width, height, 1/width, 1/height */
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmissive;\n"
	"out vec4 outMaterial;\n"
	"float Dist(vec2 v)\n"
	"{\n"
	"	float z = texture(depthtex, rect.xy + v * rect.zw).r;\n"
	"	float ndc = (z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0;\n"
	"	return proj.y / (ndc + proj.x);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	float d = Dist(vuv);\n"
	"	vec2 p = vuv * size.xy - 0.5;\n"
	"	vec2 f = fract(p), b = (floor(p) + 0.5) * size.zw;\n"
	"	vec2 t[4] = vec2[4](b, b + vec2(size.z, 0.0), b + vec2(0.0, size.w), b + size.zw);\n"
	"	vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);\n"
	"	float k = 3.0 / (0.03 * d + 1.0), best = 1e9;\n"
	"	vec4 near = vec4(0.0, 0.0, 0.0, 1.0), sum = vec4(0.0);\n"
	"	float ws = 0.0;\n"
	"	for (int i = 0; i < 4; i++)\n"
	"	{\n"
	"		vec2 tc = clamp(t[i], size.zw * 0.5, 1.0 - size.zw * 0.5);\n"
	"		float e = abs(Dist(tc) - d);\n"
	"		vec4 s = texture(fogtex, tc);\n"
	"		float wi = w[i] * exp(-e * k);\n"
	"		sum += s * wi;\n"
	"		ws += wi;\n"
	"		if (e < best) { best = e; near = s; }\n"
	"	}\n"
	/* no tap at this depth (a thin edge): the nearest in depth, the fog never drops out */
	"	vec4 fog = ws > 0.02 ? sum / ws : near;\n"
	"	outColor = fog;\n"
	"	outEmissive = vec4(0.0, 0.0, 0.0, fog.a);\n"
	"	outMaterial = vec4(0.0, 0.0, 0.0, fog.a);\n"
	"}\n";

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
		r_om_volfog_quality = ri.Cvar_Get("r_om_volfog_quality", "1", CVAR_ARCHIVE); /* 2 looked the same on the tour views */
		r_om_volfog_intensity = ri.Cvar_Get("r_om_volfog_intensity", "0.4", CVAR_ARCHIVE);
		r_om_volfog_overlaydim = ri.Cvar_Get("r_om_volfog_overlaydim", "0.85", CVAR_ARCHIVE);
		r_om_volfog_shafts = ri.Cvar_Get("r_om_volfog_shafts", "1", CVAR_ARCHIVE);
		r_om_volfog_noise = ri.Cvar_Get("r_om_volfog_noise", "0.25", CVAR_ARCHIVE);
		r_om_volfog_half = ri.Cvar_Get("r_om_volfog_half", "1", CVAR_ARCHIVE);
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

	/* the upsampling pass of r_om_volfog_half (without it the fog is marched at full size) */
	v = CompilePost(GL_VERTEX_SHADER, post_vs);
	f = CompilePost(GL_FRAGMENT_SHADER, volfog_up_fs);

	if (v && f)
	{
		p = glCreateProgram();
		glAttachShader(p, v);
		glAttachShader(p, f);
		GL3_Post_BindOutputs(p);
		glLinkProgram(p);
		glGetProgramiv(p, GL_LINK_STATUS, &ok);

		if (ok)
		{
			V.progup = p;
			V.up_fog = glGetUniformLocation(p, "fogtex");
			V.up_depth = glGetUniformLocation(p, "depthtex");
			V.up_rect = glGetUniformLocation(p, "rect");
			V.up_proj = glGetUniformLocation(p, "proj");
			V.up_size = glGetUniformLocation(p, "size");
			GL3_UseProgram(p);
			glUniform1i(V.up_fog, 0);
			glUniform1i(V.up_depth, 7);
		}
		else
		{
			glDeleteProgram(p);
		}
	}

	if (v) glDeleteShader(v);
	if (f) glDeleteShader(f);
	return true;
}

/* the half-size fog target for this view size; false: march at full size */
static qboolean
VolFogHalfTarget(void)
{
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	int w = Q_max(P.vw / 2, 1), h = Q_max(P.vh / 2, 1);
	GLint fbo = 0;

	if (!V.progup || !r_om_volfog_half->value)
	{
		return false;
	}

	if (V.halftex && V.hw == w && V.hh == h)
	{
		return true;
	}

	if (V.halffbo) glDeleteFramebuffers(1, &V.halffbo);
	if (V.halftex) glDeleteTextures(1, &V.halftex);
	V.halffbo = V.halftex = 0;

	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
	V.halftex = NewTexture(OM_RT_FMT, GL_RGBA, OM_RT_TYPE, w, h, GL_NEAREST);
	glGenFramebuffers(1, &V.halffbo);
	glBindFramebuffer(GL_FRAMEBUFFER, V.halffbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, V.halftex, 0);
	glDrawBuffers(1, one);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glDeleteFramebuffers(1, &V.halffbo);
		glDeleteTextures(1, &V.halftex);
		V.halffbo = V.halftex = 0;
		glDeleteProgram(V.progup);
		V.progup = 0;
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	V.hw = w;
	V.hh = h;
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

	if (VolFogHalfTarget())
	{
		GLint fbo = 0, vp[4];

		/* marched at half size, then over the view where the depth agrees */
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
		glGetIntegerv(GL_VIEWPORT, vp);
		glDisable(GL_BLEND);
		glBindFramebuffer(GL_FRAMEBUFFER, V.halffbo);
		glViewport(0, 0, V.hw, V.hh);
		DrawQuad();

		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(vp[0], vp[1], vp[2], vp[3]);
		GL3_UseProgram(V.progup);
		glUniform4f(V.up_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniform4f(V.up_proj, gl3state.projMat3D.Elements[2][2], gl3state.projMat3D.Elements[3][2], gl3depthmin,
			gl3depthmax);
		glUniform4f(V.up_size, (float)V.hw, (float)V.hh, 1.0f / V.hw, 1.0f / V.hh);
		BindTex(GL_TEXTURE0, V.halftex);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_SRC_ALPHA);
		DrawQuad();
	}
	else
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_SRC_ALPHA);
		DrawQuad();
	}

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

	if (V.progup)
	{
		if (gl3state.currentShaderProgram == V.progup)
		{
			gl3state.currentShaderProgram = 0;
		}

		glDeleteProgram(V.progup);
	}

	if (V.halffbo) glDeleteFramebuffers(1, &V.halffbo);
	if (V.halftex) glDeleteTextures(1, &V.halftex);
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
	/* (the sun's character shadow in a lamp's pool is faint by itself now: the world shaders take away only the
	   sun's part of the light, gl3_csm.c omCsmApply) */
	"	outColor = vec4(0.0);\n"
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
	/* the face the lamp is set into (a subway train's front lies 5-20 units ahead of the cone's apex) shadowed the
	   whole beam; it showed only when the model was partly off screen and not captured as a caster */
	float front = Q_min(24.0f, Q_max(4.0f, b->len * 0.15f));
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
			behind += (h < front);
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
	float fw, fh, gshadow;

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

	/* the full depth of the view (models and effects included) for the march - unless SSAO / SSR copied the
	   finished view already (nothing between them writes depth) */
	if (!P.depthfinal)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.depthfbo);
		glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		P.depthfinal = true;
	}

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

	/* the characters' shadows in the pools: replaced by r_om_lshadow (the lamps' real shadows) when that is on */
	gshadow = GL3_LShadow_On() ? 0.0f : Q_clamp(r_om_beams_groundshadow->value, 0.0f, 1.0f);
	B.npools = (gshadow > 0) ? nb : 0;

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

			if (gshadow > 0)
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
	if (shadows && (gshadow > 0) && ShadeProgram())
	{
		GL3_UseProgram(B.sprog);
		glUniform4f(B.s_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniformMatrix4fv(B.s_invpv, 1, GL_FALSE, GL3_CSM_InvPV());
		glUniform4f(B.s_prm, gl3depthmin, gl3depthmax, gshadow, (float)nb);
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

/* the SoF fog overlay alpha (gl3_fog.c GL3_Fog_ViewFog) of the eye's region at a view pixel: passes that run
   after the overlay (SSAO, SSR) fade by it, so they do not draw over the fog. omVF0 start, range, max alpha,
   height falloff; omVF1 maxs z, size z, dim, on; omVF2 eye, 1 / P00; omVF3 forward, 1 / P11; omVF4 / omVF5
   right, up. z = view depth of the pixel, v = its position in the view (0..1) */
static const char *om_viewfog_glsl =
	"uniform vec4 omVF0;\n"
	"uniform vec4 omVF1;\n"
	"uniform vec4 omVF2;\n"
	"uniform vec4 omVF3;\n"
	"uniform vec3 omVF4;\n"
	"uniform vec3 omVF5;\n"
	"float omViewFog(vec2 v, float z)\n"
	"{\n"
	"	if (omVF1.w < 0.5) return 0.0;\n"
	"	vec2 n = v * 2.0 - 1.0;\n"
	"	vec3 d = omVF3.xyz * z + omVF4 * (n.x * z * omVF2.w) + omVF5 * (n.y * z * omVF3.w);\n"
	"	float a = min(max((dot(d, d) - omVF0.x) / omVF0.y, 0.0) / 255.0, omVF0.z);\n"
	"	if (omVF0.w > 0.5 && omVF1.y > 0.0)\n"
	"	{\n"
	"		float h = (omVF1.x - (omVF2.z + d.z)) / omVF1.y;\n"
	"		a *= h * h;\n"
	"	}\n"
	"	return clamp(a * mix(omVF1.z, 1.0, a), 0.0, 1.0);\n"
	"}\n";

/* the uniform locations of om_viewfog_glsl, cached per program (ViewFogForget when a program is deleted) */
static struct
{
	GLuint prog;
	GLint loc[6];
} viewfogloc[4];

static void
ViewFogForget(GLuint prog)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		if (viewfogloc[i].prog == prog)
		{
			viewfogloc[i].prog = 0;
		}
	}
}

static void
ViewFogUniforms(GLuint prog)
{
	static const char *const names[6] = {"omVF0", "omVF1", "omVF2", "omVF3", "omVF4", "omVF5"};
	float f[7];
	vec3_t fwd, right, up;
	GLint *loc = NULL;
	int i, k;

	for (i = 0; (i < 4) && !loc; i++)
	{
		if (viewfogloc[i].prog == prog)
		{
			loc = viewfogloc[i].loc;
		}
	}

	for (i = 0; (i < 4) && !loc; i++)
	{
		if (!viewfogloc[i].prog)
		{
			viewfogloc[i].prog = prog;

			for (k = 0; k < 6; k++)
			{
				viewfogloc[i].loc[k] = glGetUniformLocation(prog, names[k]);
			}

			loc = viewfogloc[i].loc;
		}
	}

	if (!loc)
	{
		return;
	}

	if (!GL3_Fog_ViewFog(f))
	{
		glUniform4f(loc[1], 0, 0, 0, 0);
		return;
	}

	AngleVectors(r_newrefdef.viewangles, fwd, right, up);
	glUniform4f(loc[0], f[0], f[1], f[2], f[3]);
	glUniform4f(loc[1], f[4], f[5], f[6], 1);
	glUniform4f(loc[2], r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2],
		1.0f / gl3state.projMat3D.Elements[0][0]);
	glUniform4f(loc[3], fwd[0], fwd[1], fwd[2], 1.0f / gl3state.projMat3D.Elements[1][1]);
	glUniform3f(loc[4], right[0], right[1], right[2]);
	glUniform3f(loc[5], up[0], up[1], up[2]);
}

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
	/* the fog overlay lies over what the occlusion darkens: fade it by the fog of the pixel */
	"	mb = mix(mb, vec3(1.0), omViewFog(vuv, d));\n"
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
		r_om_ssao_quality = ri.Cvar_Get("r_om_ssao_quality", "1", CVAR_ARCHIVE); /* the temporal pass evens 2x4 out: 3 looked the same, 0.5 ms more */
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
	{
		/* the apply pass also needs the fog of the view (om_viewfog_glsl) */
		size_t la = strlen(ssao_common), lb = strlen(om_viewfog_glsl);
		char *ab = malloc(la + lb + 1);

		if (ab)
		{
			memcpy(ab, ssao_common, la);
			memcpy(ab + la, om_viewfog_glsl, lb + 1);
			S.progapply = SSAOLink(ab, ssao_apply_fs);
			free(ab);
		}
	}
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

/* view space -> world: the view matrix is a rotation and a translation, inverse = transposed rotation, -R^T t */
static hmm_mat4
ViewInverse(void)
{
	const hmm_mat4 *v = &gl3state.viewMat3D;
	hmm_mat4 inv;
	int r, c;

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
	return inv;
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
	P.depthfinal = true;

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
		hmm_mat4 reproj;
		int next = S.histcur ^ 1;

		reproj = HMM_MultiplyMat4(S.prevpv, ViewInverse());

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
	ViewFogUniforms(S.progapply);
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
	SceneBuffers();
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
	if (S.progapply) { ViewFogForget(S.progapply); glDeleteProgram(S.progapply); }

	if ((gl3state.currentShaderProgram == S.proglin) || (gl3state.currentShaderProgram == S.progao) ||
		(gl3state.currentShaderProgram == S.progblur) ||
		(gl3state.currentShaderProgram == S.progapply))
	{
		gl3state.currentShaderProgram = 0;
	}

	memset(&S, 0, sizeof(S));
}

/* ------------------------------------------------------------------
 * GFX-modern r_om_rain: rain, splashes, a darker sky and lightning.
 * On by itself (r_om_rain 1) on maps whose entities speak of rain (a rain
 * effect or a rainy ambient set: arm1), always with 2, never with 0.
 * A depth map of the static world seen from straight above the camera
 * (1024^2 over 3072 units, re-made when the camera moves 256 units) tells
 * where the sky is open: drops and splashes exist only above the first
 * surface met from the top. Drops are camera-facing streaks made in the
 * vertex shader from gl_VertexID (world-fixed cells wrapped around the eye,
 * falling 700 u/s with a little wind); splashes are short rings on the top
 * surfaces. The sky is multiplied by r_om_rain_sky. Lightning
 * (r_om_lightning): every 8..20 s (r_om_lightning_rate scales) two or three
 * flickers within 0.5 s light the open surfaces (full-view pass over the
 * depth, tested against the top-down map), brighten the sky and draw a
 * branched midpoint-displaced bolt 1500..3000 units away into the scene and
 * the emissive target (bloom). No thunder: the renderer has no sound.
 * ------------------------------------------------------------------ */

#define RAIN_OCC 1024
#define RAIN_HALF 1536.0f
#define BOLT_MAXPTS 512

static cvar_t *r_om_rain, *r_om_rain_density, *r_om_rain_sky, *r_om_lightning, *r_om_lightning_rate;
static cvar_t *r_om_storm_maps, *r_om_aa, *r_om_aa_maps, *r_om_snow, *r_om_snow_maps, *r_om_war, *r_om_rain_maps;
static cvar_t *r_om_lightning_zones;

#define SKYM_W 256
#define SKYM_H 64

/* the loaded map's name ("trn1") is a word of the list */
static qboolean
MapListed(const char *list)
{
	char name[MAX_QPATH], *dot;
	const char *base, *p;
	size_t n;

	if (!gl3_worldmodel || !list || !list[0])
	{
		return false;
	}

	base = strrchr(gl3_worldmodel->name, '/');
	Q_strlcpy(name, base ? base + 1 : gl3_worldmodel->name, sizeof(name));
	dot = strrchr(name, '.');
	if (dot)
	{
		*dot = 0;
	}
	n = strlen(name);

	for (p = list; (p = Q_strcasestr(p, name)) != NULL; p += n)
	{
		if (((p == list) || (p[-1] == ' ') || (p[-1] == ',')) && ((p[n] == 0) || (p[n] == ' ') || (p[n] == ',')))
		{
			return true;
		}
	}

	return false;
}

static float
frand(void)
{
	return (float)(rand() & 0x7fff) / 32768.0f;
}

/* r_om_lightning_zones "jpn2:200-250,290-330 trn1:...": the stretches of sky (world yaw, degrees) where the
   lightning of a map strikes, chosen by eye where the sky is open; one of them at random into *yaw. false: the
   map has none (the strikes follow the view) */
static qboolean
LightningZone(const char *list, float *yaw)
{
	char name[MAX_QPATH], *dot;
	const char *base, *p;
	float zones[8][2];
	int nz = 0;
	size_t n;

	if (!gl3_worldmodel || !list || !list[0])
	{
		return false;
	}

	base = strrchr(gl3_worldmodel->name, '/');
	Q_strlcpy(name, base ? base + 1 : gl3_worldmodel->name, sizeof(name));
	dot = strrchr(name, '.');
	if (dot)
	{
		*dot = 0;
	}
	n = strlen(name);

	for (p = list; (p = Q_strcasestr(p, name)) != NULL; p += n)
	{
		if (((p == list) || (p[-1] == ' ')) && (p[n] == ':'))
		{
			break;
		}
	}

	if (!p)
	{
		return false;
	}

	for (p += n + 1; nz < 8;)
	{
		float a, b;
		int used = 0;

		if (sscanf(p, "%f-%f%n", &a, &b, &used) != 2)
		{
			break;
		}

		zones[nz][0] = a;
		zones[nz][1] = (b < a) ? b + 360 : b;
		nz++;
		p += used;

		if (*p != ',')
		{
			break;
		}
		p++;
	}

	if (!nz)
	{
		return false;
	}

	{
		int z = rand() % nz;

		*yaw = zones[z][0] + frand() * (zones[z][1] - zones[z][0]);
	}

	return true;
}

static struct
{
	qboolean maprain, failed, active;
	qboolean rain, storm, aa;   /* this map: rain (and lightning), distant lightning only, anti-aircraft fire */
	qboolean snow;              /* this map: snowfall */
	GLuint progaa, progsnow;
	GLint n_vp, n_eye, n_time, n_params, n_occ, n_occvp, n_color, n_wind, n_px, n_layer;
	float windx, windy, windt;  /* snow: the wind drift integrated over time (world units) */
	/* war on the aa maps: distant explosions (position, start), fires with smoke (position, seed) */
	GLuint progwar, progsmoke;
	GLint w_vp, w_eye, w_time, w_exps, w_fires, s2_vp, s2_eye, s2_time, s2_fires, s2_wind;
	float exps[6][4], fires[4][4], nextexp;
	int expcur;
	GLint a_vp, a_eye, a_time, a_guns, a_params;
	float guns[8][4];           /* anti-aircraft guns: position, seed */
	vec3_t gunanchor;
	const void *gunmodel;
	GLuint occtex, occfbo;
	float occvp[16], occc[3];
	const void *occmodel;
	GLuint prograin, progsplash, progbolt, vao, bolt_vbo;
	GLint r_vp, r_eye, r_time, r_params, r_occ, r_occvp, r_color;
	GLint s_vp, s_eye, s_time, s_params, s_occ, s_occvp, s_color;
	GLint b_vp, b_color;
	/* lightning */
	float next, start, flashk[3];
	int nflicker;
	float bolt[BOLT_MAXPTS * 3];
	int nbolt;  /* segment end points, pairs */
	vec3_t boltpos;  /* where the light of the world shaders comes from */
	vec3_t ldir;     /* unit direction from the eye at the strike towards the channel */
	GLuint shtex, shfbo;  /* the strike's shadow map of the static world along ldir */
	float shvp[16];
	qboolean shneed, shvalid;
	qboolean forced; /* rain asked for by r_om_rain_maps: the map's sky stays as it is */
	/* the open sky by direction (WXSkyMask): yaw across (-180..180), elevation up (0..90) */
	unsigned char skym[SKYM_H][SKYM_W];
	GLuint skytex;
	const void *skymodel;
	qboolean occvalid;
	const void *warm;     /* the map the lightning warm-up was done for */
} WX;

/* vid_restart: the weather's GL objects belong to the old context (WXCreate returned early on the old names and
   drew with them); the per-map flags stay, the map is loaded again after the restart */
static void
WXShutdown(void)
{
	GLuint progs[7] = {WX.progaa, WX.progsnow, WX.progwar, WX.progsmoke, WX.prograin, WX.progsplash, WX.progbolt};
	int i;

	for (i = 0; i < 7; i++)
	{
		if (progs[i]) glDeleteProgram(progs[i]);
	}

	if (WX.vao) glDeleteVertexArrays(1, &WX.vao);
	if (WX.bolt_vbo) glDeleteBuffers(1, &WX.bolt_vbo);
	if (WX.occtex) glDeleteTextures(1, &WX.occtex);
	if (WX.occfbo) glDeleteFramebuffers(1, &WX.occfbo);
	if (WX.shtex) glDeleteTextures(1, &WX.shtex);
	if (WX.shfbo) glDeleteFramebuffers(1, &WX.shfbo);
	if (WX.skytex) glDeleteTextures(1, &WX.skytex);
	WX.progaa = WX.progsnow = WX.progwar = WX.progsmoke = WX.prograin = WX.progsplash = WX.progbolt = 0;
	WX.vao = WX.bolt_vbo = WX.occtex = WX.occfbo = WX.shtex = WX.shfbo = WX.skytex = 0;
	WX.failed = false;
	WX.occmodel = WX.skymodel = WX.gunmodel = WX.warm = NULL;
	WX.occvalid = WX.shvalid = false;
}

/* map load (gl3_model.c): does the map ask for rain? */
void
GL3_Weather_MapLoad(const char *ents, int len)
{
	char buf[64];
	int i;

	WX.maprain = false;
	WX.occmodel = NULL;
	WX.occvalid = false;
	WX.next = 0;
	/* gl3_worldmodel is always mod_known[0]: the per-map caches keyed on it must be reset here (the sky mask,
	   the anti-aircraft guns and the lightning warm-up of the previous map were kept) */
	WX.skymodel = WX.gunmodel = NULL;
	WX.warm = NULL;

	if (!ents || len <= 0)
	{
		return;
	}

	/* "rain" starting a word in a value (environ/gb_rain, CityTrafficRainy ...): after a non-letter or as a
	   capitalised part; "train", "drain", "terrain" are no rain (tsr1, kos1 and others got rain from them) */
	for (i = 0; i + 4 <= len && ents[i]; i++)
	{
		int k;

		for (k = 0; k < 4; k++)
		{
			buf[k] = (char)tolower((unsigned char)ents[i + k]);
		}

		if (!memcmp(buf, "rain", 4) && ((i == 0) || !isalpha((unsigned char)ents[i - 1]) || (ents[i] == 'R')))
		{
			WX.maprain = true;
			return;
		}
	}
}

static qboolean
WeatherWanted(void)
{
	if (!r_om_rain)
	{
		r_om_rain = ri.Cvar_Get("r_om_rain", "1", CVAR_ARCHIVE);
		r_om_rain_density = ri.Cvar_Get("r_om_rain_density", "1", CVAR_ARCHIVE);
		r_om_rain_sky = ri.Cvar_Get("r_om_rain_sky", "0.45", CVAR_ARCHIVE);
		r_om_lightning = ri.Cvar_Get("r_om_lightning", "1", CVAR_ARCHIVE);
		r_om_lightning_rate = ri.Cvar_Get("r_om_lightning_rate", "1", CVAR_ARCHIVE);
		/* distant thunderstorm without rain (the train in the night); anti-aircraft fire in the sky of the war maps */
		r_om_storm_maps = ri.Cvar_Get("r_om_storm_maps", "trn1", CVAR_ARCHIVE);
		/* rain and thunderstorm on maps whose data has none (the night of the Tokyo towers) */
		r_om_rain_maps = ri.Cvar_Get("r_om_rain_maps", "jpn2", CVAR_ARCHIVE);
		r_om_lightning_zones = ri.Cvar_Get("r_om_lightning_zones", "", CVAR_ARCHIVE);
		r_om_aa = ri.Cvar_Get("r_om_aa", "1", CVAR_ARCHIVE);
		r_om_aa_maps = ri.Cvar_Get("r_om_aa_maps", "kos1 kos2 kos3", CVAR_ARCHIVE);
		r_om_snow = ri.Cvar_Get("r_om_snow", "1", CVAR_ARCHIVE);
		/* the war around the aa maps: explosions, fires and smoke on the horizon, ash */
		r_om_war = ri.Cvar_Get("r_om_war", "1", CVAR_ARCHIVE);
		r_om_snow_maps = ri.Cvar_Get("r_om_snow_maps", "sib1 sib2 sib3", CVAR_ARCHIVE);
	}

	WX.rain = WX.storm = WX.aa = WX.snow = false;

#ifdef YQ2_GL3_GLES3
	return false;
#else
	if (WX.failed || !gl3_worldmodel || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		return false;
	}

	WX.forced = (r_om_rain->value >= 1) && !WX.maprain && MapListed(r_om_rain_maps->string);
	WX.rain = (r_om_rain->value >= 2) || ((r_om_rain->value >= 1) && WX.maprain) || WX.forced;
	WX.storm = !WX.rain && (r_om_rain->value >= 1) && MapListed(r_om_storm_maps->string);
	WX.aa = (r_om_aa->value > 0) && MapListed(r_om_aa_maps->string);
	WX.snow = !WX.rain && (r_om_snow->value > 0) && MapListed(r_om_snow_maps->string);

	/* the client plays the downpour (cl_view.c) while it rains */
	{
		static int heard = -1;

		if (heard != (int)WX.rain)
		{
			heard = (int)WX.rain;
			ri.Cvar_Set("r_om_rain_audible", heard ? "1" : "0");
		}
	}

	return WX.rain || WX.storm || WX.aa || WX.snow;
#endif
}

/* 0..1 brightness of the lightning now */
static float
LightningFlash(void)
{
	float t = r_newrefdef.time - WX.start, f = 0;
	int i;

	if (WX.start <= 0 || t < 0 || t > 1.5f)
	{
		return 0;
	}

	for (i = 0; i < WX.nflicker; i++)
	{
		float d = t - WX.flashk[i];

		if (d >= 0)
		{
			f = Q_max(f, expf(-d * 9.0f) * (i == 0 ? 1.0f : 0.8f));
		}
	}

	return f;
}

static void
BoltSegment(const vec3_t a, const vec3_t b, float disp, int depth, int branch)
{
	vec3_t m, d;

	if (depth == 0 || WX.nbolt + 2 > BOLT_MAXPTS)
	{
		if (WX.nbolt + 2 <= BOLT_MAXPTS)
		{
			{ float *o = WX.bolt + WX.nbolt * 3; VectorCopy(a, o); }
			{ float *o = WX.bolt + (WX.nbolt + 1) * 3; VectorCopy(b, o); }
			WX.nbolt += 2;
		}

		return;
	}

	VectorAdd(a, b, m);
	VectorScale(m, 0.5f, m);
	m[0] += (frand() * 2 - 1) * disp;
	m[1] += (frand() * 2 - 1) * disp;
	m[2] += (frand() * 2 - 1) * disp * 0.3f;
	BoltSegment(a, m, disp * 0.55f, depth - 1, branch);
	BoltSegment(m, b, disp * 0.55f, depth - 1, branch);

	/* a side branch now and then, thinner (shorter recursion) */
	if (branch > 0 && depth >= 4 && frand() < 0.3f)
	{
		VectorSubtract(b, a, d);
		VectorMA(m, 0.6f, d, d);
		d[0] += (frand() * 2 - 1) * disp * 1.5f;
		d[1] += (frand() * 2 - 1) * disp * 1.5f;
		BoltSegment(m, d, disp * 0.5f, depth - 2, branch - 1);
	}
}

/* the open sky by direction, once per map, from the sky box images themselves: a texel is clear where its colour is
   that of the sky (the mean of the up face) and nothing painted lies above it up to the zenith (jpn2 paints the
   whole street beyond the map on its sky box, towers up to 40 degrees). Eroded a little: the bolt keeps clear of
   the painted edges */
static void
WXSkyMask(void)
{
	static unsigned char col[SKYM_H][SKYM_W];
	gl3image_t *imgs[6];
	unsigned char *pix[6];
	int iw[6], ih[6], nimg = 0, x, y, k;
	float ref[3] = {0, 0, 0};
	vec3_t up = {0, 0, 1};
	qboolean haveref = false;

	if ((WX.skymodel == gl3_worldmodel) && WX.skytex)
	{
		return;
	}

	WX.skymodel = gl3_worldmodel;

	for (y = -1; y < SKYM_H; y++)
	{
		for (x = 0; x < SKYM_W; x++)
		{
			float az = ((x + 0.5f) / SKYM_W * 2 - 1) * (float)M_PI, el = (y + 0.5f) / SKYM_H * (float)M_PI * 0.5f, s, t;
			vec3_t d;
			gl3image_t *im;
			unsigned char clear = 255;

			if (y < 0)
			{
				VectorCopy(up, d);
			}
			else
			{
				VectorSet(d, cosf(el) * cosf(az), cosf(el) * sinf(az), sinf(el));
			}

			im = GL3_SkyAt(d, &s, &t);

			if (im)
			{
				for (k = 0; (k < nimg) && (imgs[k] != im); k++)
				{
				}

				if ((k == nimg) && (nimg < 6))
				{
					imgs[k] = im;
					pix[k] = NULL;
					GL3_SelectTMU(GL_TEXTURE0);
					GL3_Bind(im->texnum);
					iw[k] = ih[k] = 0;
#ifndef YQ2_GL3_GLES3 /* no texture read-back in GLES (weather is desktop only) */
					glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iw[k]);
					glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &ih[k]);
#endif
					if ((iw[k] > 0) && (ih[k] > 0) && (iw[k] <= 4096) && (ih[k] <= 4096))
					{
						pix[k] = malloc((size_t)iw[k] * ih[k] * 4);
						if (pix[k])
						{
#ifndef YQ2_GL3_GLES3
							glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix[k]);
#endif
						}
					}
					nimg++;
				}

				if ((k < nimg) && pix[k])
				{
					if ((y < 0) && !haveref)
					{
						/* the sky colour: the mean of the up face */
						size_t i, n = (size_t)iw[k] * ih[k];
						double sum[3] = {0, 0, 0};

						for (i = 0; i < n; i++)
						{
							sum[0] += pix[k][i * 4];
							sum[1] += pix[k][i * 4 + 1];
							sum[2] += pix[k][i * 4 + 2];
						}
						for (i = 0; i < 3; i++)
						{
							ref[i] = (float)(sum[i] / n);
						}
						haveref = true;
					}
					else if ((y >= 0) && haveref)
					{
						const unsigned char *p = pix[k] + 4 * (Q_min((int)(t * ih[k]), ih[k] - 1) * iw[k] +
							Q_min((int)(s * iw[k]), iw[k] - 1));
						int c;

						for (c = 0; c < 3; c++)
						{
							if (fabsf(p[c] - ref[c]) > 4.0f + 0.6f * ref[c])
							{
								clear = 0;
							}
						}
					}
				}
			}

			if (y >= 0)
			{
				col[y][x] = clear;
			}
		}

		if ((y < 0) && !haveref)
		{
			break; /* no up face to judge the sky by: all of it counts as open */
		}
	}

	for (k = 0; k < nimg; k++)
	{
		free(pix[k]);
	}

	if (!haveref)
	{
		memset(col, 255, sizeof(col));
	}

	/* open only below open sky: from the zenith down to the first painted thing */
	for (x = 0; x < SKYM_W; x++)
	{
		qboolean open = true;

		for (y = SKYM_H - 1; y >= 0; y--)
		{
			open = open && col[y][x];
			col[y][x] = open ? 255 : 0;
		}
	}

	/* eroded: two columns (about 3 degrees) aside, one row (1.4 degrees) up and down */
	for (y = 0; y < SKYM_H; y++)
	{
		for (x = 0; x < SKYM_W; x++)
		{
			unsigned char m = 255;
			int dx, dy;

			for (dy = -1; dy <= 1; dy++)
			{
				for (dx = -2; dx <= 2; dx++)
				{
					int yy = Q_clamp(y + dy, 0, SKYM_H - 1), xx = (x + dx + SKYM_W) % SKYM_W;

					m = Q_min(m, col[yy][xx]);
				}
			}

			WX.skym[y][x] = m;
		}
	}

	if (!WX.skytex)
	{
		glGenTextures(1, &WX.skytex);
	}

	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, WX.skytex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SKYM_W, SKYM_H, 0, GL_RED, GL_UNSIGNED_BYTE, WX.skym);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl3state.currenttexture = WX.skytex;
}

/* is the sky open at world yaw / elevation (degrees)? */
static qboolean
SkyClear(float yaw, float el)
{
	float u = (yaw + 180.0f) / 360.0f;
	int x, y;

	u -= floorf(u);
	x = Q_min((int)(u * SKYM_W), SKYM_W - 1);
	y = Q_clamp((int)(el / 90.0f * SKYM_H), 0, SKYM_H - 1);
	return WX.skym[y][x] > 127;
}

static void
LightningThink(void)
{
	float now = r_newrefdef.time, rate = Q_max(r_om_lightning_rate->value, 0.05f);

	if (!r_om_lightning->value)
	{
		WX.start = 0;
		return;
	}

	if (WX.next <= 0 || WX.next - now > 60)
	{
		WX.next = now + (4.0f + frand() * 8.0f) / rate;
	}

	if (now >= WX.next)
	{
		/* far out in the sky, high above the horizon, roughly where the player looks (seen through the gaps
		   between the buildings; the world in front hides it by depth) */
		float az = (r_newrefdef.viewangles[YAW] + (frand() * 2 - 1) * 55.0f) * (float)M_PI / 180.0f;
		float dist = WX.storm ? 6500 + frand() * 1500 : 5500 + frand() * 1500, zyaw;
		qboolean zoned = false;
		float te = 0, be = 0, drift = 0;
		vec3_t top, bottom;
		int i;

		/* where the rain was added (jpn2): a column of open sky whose upper end is clear and at least a third of it
		   seen; lower down the channel passes behind the towers painted on the sky. A stretch of r_om_lightning_zones
		   if the map has one, else near the view (then anywhere) */
		if (WX.forced)
		{
			static float cand[2][SKYM_W];
			int nc[2] = {0, 0}, x, side;

			WXSkyMask();
			te = 32.0f + frand() * 20.0f;
			be = 12.0f + frand() * 10.0f;
			drift = (frand() * 2 - 1) * 4.0f;

			if (LightningZone(r_om_lightning_zones->string, &zyaw))
			{
				zoned = true;
			}
			else
			{
				/* every yaw whose column fits; those within 60 degrees of the view first */
				for (x = 0; x < SKYM_W; x++)
				{
					float yaw = (x + 0.5f) / SKYM_W * 360.0f - 180.0f, off;
					int k, seen = 0;

					if (!SkyClear(yaw, te))
					{
						continue;
					}
					for (k = 0; k <= 12; k++)
					{
						float f = k / 12.0f;

						seen += SkyClear(yaw + drift * f, te + (be - te) * f);
					}
					if (seen < 5)
					{
						continue;
					}
					off = (yaw - r_newrefdef.viewangles[YAW]) / 360.0f;
					off = fabsf(off - floorf(off + 0.5f)) * 360.0f;
					side = (off <= 60.0f) ? 0 : 1;
					cand[side][nc[side]++] = yaw;
				}

				side = nc[0] ? 0 : 1;
				if (nc[side])
				{
					zyaw = cand[side][rand() % nc[side]];
					zoned = true;
				}
			}
		}

		WX.start = now;
		WX.nflicker = 2 + (frand() < 0.5f ? 1 : 0);
		WX.flashk[0] = 0;
		for (i = 1; i < WX.nflicker; i++)
		{
			WX.flashk[i] = WX.flashk[i - 1] + 0.08f + frand() * 0.15f;
		}
		WX.next = now + (8.0f + frand() * 12.0f) / rate;

		top[0] = r_newrefdef.vieworg[0] + cosf(az) * dist;
		top[1] = r_newrefdef.vieworg[1] + sinf(az) * dist;
		top[2] = r_newrefdef.vieworg[2] + (WX.storm ? 2600 + frand() * 900 : 4200 + frand() * 1500); /* a distant storm: low over the horizon */
		VectorCopy(top, bottom);
		bottom[0] += (frand() * 2 - 1) * 900;
		bottom[1] += (frand() * 2 - 1) * 900;
		/* down to the horizon on a map's own rain; high in the clouds where the rain was added (jpn2: the towers
		   painted on the sky stay clear) */
		bottom[2] = r_newrefdef.vieworg[2] + 300 + frand() * 400;

		if (zoned)
		{
			/* far out, from high in the clouds down behind the skyline (the bolt shader keeps it on the open sky) */
			te *= (float)M_PI / 180.0f;
			be *= (float)M_PI / 180.0f;
			dist = 9000 + frand() * 2000;
			az = zyaw * (float)M_PI / 180.0f;
			top[0] = r_newrefdef.vieworg[0] + cosf(az) * dist;
			top[1] = r_newrefdef.vieworg[1] + sinf(az) * dist;
			top[2] = r_newrefdef.vieworg[2] + tanf(te) * dist;
			az += drift * (float)M_PI / 180.0f;
			bottom[0] = r_newrefdef.vieworg[0] + cosf(az) * dist;
			bottom[1] = r_newrefdef.vieworg[1] + sinf(az) * dist;
			bottom[2] = r_newrefdef.vieworg[2] + tanf(be) * dist;
		}

		WX.nbolt = 0;
		/* (no open column found: the strike stays in the clouds, only its light and thunder come) */
		if (zoned || !WX.forced)
		{
			BoltSegment(top, bottom, zoned ? 700.0f * dist / 6000.0f : 700.0f, 8, 3);
		}
		/* the light of the world shaders comes from the upper part of the bolt; kilometres away it arrives in
		   parallel: one direction and one shadow map for the whole strike */
		for (i = 0; i < 3; i++)
		{
			WX.boltpos[i] = top[i] * 0.6f + bottom[i] * 0.4f;
		}
		VectorSubtract(WX.boltpos, r_newrefdef.vieworg, WX.ldir);
		VectorNormalize(WX.ldir);
		WX.shneed = true;
		R_Printf(PRINT_DEVELOPER, "lightning: strike towards (%.2f %.2f %.2f) from (%.0f %.0f %.0f)\n", WX.ldir[0], WX.ldir[1],
			WX.ldir[2], r_newrefdef.vieworg[0], r_newrefdef.vieworg[1], r_newrefdef.vieworg[2]);

		/* the client plays the thunder (cl_view.c): strike number and distance */
		{
			static int seq;
			char buf[64];

			Com_sprintf(buf, sizeof(buf), "%d %.0f", ++seq, dist);
			ri.Cvar_Set("r_om_lightning_strike", buf);
		}
	}
}

static void WXExtraGeometry(void);
static float *wxextra; /* (defined with WXExtraGeometry below) */
static int wxnextra;

/* the strike's shadow map: the static world (and the brush entities, glass) seen along the direction towards the
   channel, orthographic over 4096 units around the eye; made once per strike (about a millisecond) */
static void
WXLightningShadow(void)
{
	static const GLenum none[1] = {GL_NONE};
	const float half = 2048.0f;
	float *m = WX.shvp, k, zmin, zmax, cx, cy, oldrange[2];
	vec3_t X, Y, Z, ref = {0, 0, 1};
	GLint fbo = 0, vp[4], oldfunc;

	WX.shneed = false;

	if (!WX.shtex)
	{
		glGenTextures(1, &WX.shtex);
		GL3_SelectTMU(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, WX.shtex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		gl3state.currenttexture = WX.shtex;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
		glGenFramebuffers(1, &WX.shfbo);
		glBindFramebuffer(GL_FRAMEBUFFER, WX.shfbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, WX.shtex, 0);
		glDrawBuffers(1, none);
		glReadBuffer(GL_NONE);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glDeleteFramebuffers(1, &WX.shfbo);
			glDeleteTextures(1, &WX.shtex);
			WX.shfbo = WX.shtex = 0;
			return;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	}

	/* light space: Z towards the channel */
	VectorCopy(WX.ldir, Z);
	if (fabsf(Z[2]) > 0.99f)
	{
		VectorSet(ref, 1, 0, 0);
	}
	CrossProduct(ref, Z, X);
	VectorNormalize(X);
	CrossProduct(Z, X, Y);
	cx = DotProduct(r_newrefdef.vieworg, X);
	cy = DotProduct(r_newrefdef.vieworg, Y);
	zmax = DotProduct(r_newrefdef.vieworg, Z) + 6000.0f;
	zmin = DotProduct(r_newrefdef.vieworg, Z) - 3000.0f;
	k = -2.0f / (zmax - zmin);
	memset(m, 0, 16 * sizeof(float));
	m[0] = X[0] / half;
	m[4] = X[1] / half;
	m[8] = X[2] / half;
	m[12] = -cx / half;
	m[1] = Y[0] / half;
	m[5] = Y[1] / half;
	m[9] = Y[2] / half;
	m[13] = -cy / half;
	m[2] = Z[0] * k;
	m[6] = Z[1] * k;
	m[10] = Z[2] * k;
	m[14] = 2.0f * zmax / (zmax - zmin) - 1.0f;
	m[15] = 1;

	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
	glGetIntegerv(GL_VIEWPORT, vp);
	glGetIntegerv(GL_DEPTH_FUNC, &oldfunc);
	glGetFloatv(GL_DEPTH_RANGE, oldrange);
	glBindFramebuffer(GL_FRAMEBUFFER, WX.shfbo);
	glViewport(0, 0, 2048, 2048);
	glDepthRange(0, 1);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	{
		float one = 1.0f;

		glClearBufferfv(GL_DEPTH, 0, &one);
	}
	WXExtraGeometry();
	GL3_CSM_DrawDepth(m, wxextra, wxnextra, true);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	glDepthFunc(oldfunc);
	glDepthRange(oldrange[0], oldrange[1]);
	WX.shvalid = true;
}

/* every frame before the world: lightning timeline, the sky tint */

void
GL3_Post_WeatherFrame(void)
{
	GLuint p = gl3state.si3Dsky.shaderProgram;
	GLint loc;
	float tint = 1, add = 0;

	WX.active = WeatherWanted();

	if (WX.active && (WX.rain || WX.storm))
	{
		float bolt[4], col[4] = {0.72f, 0.8f, 1.0f, 0};

		LightningThink();
		/* the sky of a rainy map is darker; a distant storm and rain added to a map (r_om_rain_maps) leave it as it
		   is. The lightning no longer flashes the whole sky (it washed out the painted towers of jpn2's sky): its
		   light falls only where the channel is seen (the strike's shadow map, world shaders) */
		tint = (WX.rain && !WX.forced) ? Q_max(0.05f, Q_min(r_om_rain_sky->value, 1.0f)) : 1.0f;
		add = 0;

		/* at the first frame of a stormy map: the sky mask (sky images read back), the extra caster geometry and
		   the strike's shadow target are made now, not at the first strike (a 30-40 ms hitch on arm1 and jpn2) */
		{
			if (WX.warm != gl3_worldmodel)
			{
				vec3_t keep;

				WX.warm = gl3_worldmodel;
				WXSkyMask();
				VectorCopy(WX.ldir, keep);
				VectorSet(WX.ldir, 0, 0, 1);
				WXLightningShadow();
				VectorCopy(keep, WX.ldir);
				WX.shvalid = false;
			}
		}

		if (WX.shneed)
		{
			WXLightningShadow();
		}

		VectorCopy(WX.ldir, bolt);
		bolt[3] = (WX.occvalid && WX.shvalid) ? LightningFlash() * (WX.rain ? 2.2f : 0.8f) : 0;
		/* r_om_lightning_test 1 (a test aid): the last strike's light stays on at full strength */
		{
			static cvar_t *test;

			if (!test)
			{
				test = ri.Cvar_Get("r_om_lightning_test", "0", 0);
			}
			if (test->value && WX.occvalid && WX.shvalid)
			{
				bolt[3] = WX.rain ? 2.2f : 0.8f;
				col[3] = test->value; /* 2: the direct light only (red), 3: the light from above only (green) */
			}
		}
		GL3_SoF_LightningUniforms(bolt, col, WX.occvalid ? WX.occvp : NULL, WX.occvalid ? WX.occtex : 0);
		GL3_SoF_LightningShadow(WX.shvp, WX.shvalid ? WX.shtex : 0);
	}
	else
	{
		static const float none[4] = {0, 0, 0, 0};

		GL3_SoF_LightningUniforms(none, none, NULL, 0);
	}

	/* the war (r_om_war on the aa maps): a distant explosion every 3..9 s; the client plays its boom
	   (r_om_war_boom "number distance", cl_view.c); the sky glows orange for a moment */
	{
		float warr = 0, warg = 0;

		if (WX.active && WX.aa && r_om_war && r_om_war->value)
		{
			float now = r_newrefdef.time;
			int i;

			if (WX.nextexp <= 0 || WX.nextexp - now > 60)
			{
				WX.nextexp = now + 1.0f + frand() * 3.0f;
			}

			if (now >= WX.nextexp)
			{
				float az = frand() * 2 * (float)M_PI, dist = 4500 + frand() * 4000;
				float *e = WX.exps[WX.expcur];
				static int seq;
				char buf[64];

				e[0] = r_newrefdef.vieworg[0] + cosf(az) * dist;
				e[1] = r_newrefdef.vieworg[1] + sinf(az) * dist;
				e[2] = r_newrefdef.vieworg[2] - 250 + frand() * 150;
				e[3] = now;
				WX.expcur = (WX.expcur + 1) % 6;
				WX.nextexp = now + (3.0f + frand() * 6.0f) / Q_max(r_om_war->value, 0.1f);
				Com_sprintf(buf, sizeof(buf), "%d %.0f", ++seq, dist);
				ri.Cvar_Set("r_om_war_boom", buf);
			}

			for (i = 0; i < 6; i++)
			{
				float age = now - WX.exps[i][3];

				if (WX.exps[i][3] > 0 && age >= 0 && age < 2)
				{
					float k = expf(-age * 2.5f) * 0.25f;

					warr += k;
					warg += k * 0.45f;
				}
			}
		}

		if (p && (loc = GL3_UniformLoc(p, "omSkyTint")) != -1)
		{
			GL3_UseProgram(p);
			glUniform4f(loc, tint * (0.95f + add) + warr, tint * (0.97f + add) + warg, tint + add, 0);
		}
	}

	/* r_om_ssr: the world shaders write the material target; wet ground needs the top-down map */
	{
		qboolean nv = (r_fullbright->value != 0) || (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES);
		float ssr[4] = {0, 0, 0, 0};

		if (SSRWanted() && !nv)
		{
			ssr[0] = 1;
			ssr[1] = (WX.active && WX.rain && WX.occvalid) ? Q_max(0.0f, Q_min(r_om_ssr_wet->value, 1.0f)) : 0;
			ssr[2] = Q_max(0.0f, r_om_ssr_glint->value);
			ssr[3] = Q_max(0.0f, Q_min(r_om_ssr_damp->value, 1.0f));
		}

		GL3_SoF_SSRUniforms(ssr);
	}
}

/* r_om_rain drops: streaks that fall through two layers around the eye (near and dense, far and thin, ending well
   before the distant towers). Each refracts what lies behind it (the last frame's colour, shifted across the streak),
   takes the light of its surroundings (a blurred level of that colour: bright drops by the lamps, nearly invisible in
   the dark) and fades softly where it meets the world (depth) */
static const char *wx_rain_vs =
	"uniform mat4 vp;\n"
	"uniform vec3 eye;\n"
	"uniform float time;\n"
	"uniform vec4 params;\n"   /* box size, box height, fall speed, streak length (s) */
	"uniform vec4 layer;\n"    /* near fade from, to; far fade from, to (units) */
	"uniform sampler2D occ;\n"
	"uniform mat4 occvp;\n"
	"out float vA;\n"
	"out float vT;\n"
	"out float vS;\n"
	"out float vD;\n"
	"float H(float n) { return fract(sin(n) * 43758.5453); }\n"
	"void main()\n"
	"{\n"
	"	int id = gl_VertexID / 6, c = gl_VertexID - id * 6;\n"
	"	float f = float(id);\n"
	"	float B = params.x, Hh = params.y, v = params.z * (0.85 + 0.3 * H(f * 3.7));\n"
	"	vec3 p;\n"
	"	p.x = eye.x + (fract((H(f * 1.13) * B - eye.x) / B) - 0.5) * B;\n"
	"	p.y = eye.y + (fract((H(f * 2.71) * B - eye.y) / B) - 0.5) * B;\n"
	"	p.z = eye.z - Hh * 0.45 + fract((H(f * 5.3) * Hh - time * v - eye.z) / Hh) * Hh;\n"
	"	vec3 vel = vec3(25.0, 12.0, -v);\n"
	"	vec3 q = p - vel * params.w;\n"   /* tail above the head */
	/* open sky above the drop? (above the first surface seen from the top) */
	"	vec4 oc = occvp * vec4(p, 1.0);\n"
	"	vec2 ou = oc.xy * 0.5 + 0.5;\n"
	"	float od = texture(occ, ou).r;\n"
	"	bool open = any(lessThan(ou, vec2(0.0))) || any(greaterThan(ou, vec2(1.0))) || (oc.z * 0.5 + 0.5 <= od + 0.0002);\n"
	"	float dist = length(p - eye);\n"
	"	vA = open ? smoothstep(layer.x, layer.y, dist) * (1.0 - smoothstep(layer.z, layer.w, dist)) : 0.0;\n"
	"	vec3 a = (c == 0 || c == 1 || c == 4) ? q : p;\n"
	"	vec3 side = normalize(cross(p - q, a - eye)) * (0.18 + dist * 0.0011);\n"
	"	bool right = (c == 1 || c == 2 || c == 5);\n"
	"	a += right ? side : -side;\n"
	"	vT = (c == 0 || c == 1 || c == 4) ? 0.0 : 1.0;\n"
	"	vS = right ? 1.0 : -1.0;\n"
	"	gl_Position = vA > 0.0 ? vp * vec4(a, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);\n"
	"	vD = gl_Position.w;\n"
	"}\n";

static const char *wx_rain_fs =
	"uniform vec4 color;\n"      /* tint (+ the lightning), opacity */
	"uniform sampler2D scenecol;\n" /* the last frame, half size, mipmapped (r_om_ssr keeps it) */
	"uniform sampler2D depthtex;\n"
	"uniform vec4 rect;\n"       /* the view in the depth texture */
	"uniform vec4 proj;\n"       /* projection C, D; depth range min, max */
	"uniform vec4 view;\n"       /* viewport x, y, w, h (pixels) */
	"uniform float refr;\n"      /* 1: the last frame's colour is there (refraction and the light around) */
	"in float vA;\n"
	"in float vT;\n"
	"in float vS;\n"
	"in float vD;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec2 vuv = (gl_FragCoord.xy - view.xy) / view.zw;\n"
	"	float z = texture(depthtex, rect.xy + vuv * rect.zw).r;\n"
	"	float sd = proj.y / ((z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0 + proj.x);\n"
	"	float soft = clamp((sd - vD) / 24.0, 0.0, 1.0);\n"
	/* a round streak: thick in the middle, a bright head, a long thinning tail */
	"	float edge = 1.0 - vS * vS;\n"
	"	float body = smoothstep(0.0, 0.45, vT) * (1.0 - smoothstep(0.9, 1.0, vT));\n"
	"	float a = vA * edge * body * soft * color.a;\n"
	"	if (a <= 0.003) discard;\n"
	"	vec3 c = color.rgb;\n"
	"	if (refr > 0.5)\n"
	"	{\n"
	"		vec3 behind = textureLod(scenecol, clamp(vuv + vec2(vS * 0.0045, -0.002), 0.0, 1.0), 0.5).rgb;\n"
	"		vec3 amb = textureLod(scenecol, vuv, 4.5).rgb;\n"
	"		c = behind * 1.3 + amb * 1.1 + c * 0.08;\n"
	"	}\n"
	"	outColor = vec4(c, a);\n"
	"}\n";


static const char *wx_splash_vs =
	"uniform mat4 vp;\n"
	"uniform vec3 eye;\n"
	"uniform float time;\n"
	"uniform vec4 params;\n"   /* area size, life (s), size, - */
	"uniform sampler2D occ;\n"
	"uniform mat4 occvp;\n"
	"uniform mat4 occinv;\n"
	"out vec2 vUV;\n"
	"out float vPh;\n"
	"float H(float n) { return fract(sin(n) * 43758.5453); }\n"
	"void main()\n"
	"{\n"
	"	int id = gl_VertexID / 6, c = gl_VertexID - id * 6;\n"
	"	float f = float(id);\n"
	"	float ph = fract(H(f * 7.1) + time / params.y);\n"
	"	float cyc = floor(H(f * 7.1) + time / params.y);\n"
	"	float B = params.x;\n"
	"	vec2 xy = eye.xy + (vec2(H(f * 1.3 + cyc * 17.0), H(f * 2.9 + cyc * 31.0)) - 0.5) * B;\n"
	"	vec4 oc = occvp * vec4(xy, eye.z, 1.0);\n"
	"	vec2 ou = oc.xy * 0.5 + 0.5;\n"
	/* the highest surface under the whole ring, and only on even ground: a ring at the height of one texel lay
	   below a recessed ceiling panel next to it and showed inside a room (arm1) */
	"	float t = 2.0 / 1024.0;\n"
	"	float d0 = texture(occ, ou).r;\n"
	"	float d1 = texture(occ, ou + vec2(t, 0.0)).r, d2 = texture(occ, ou - vec2(t, 0.0)).r;\n"
	"	float d3 = texture(occ, ou + vec2(0.0, t)).r, d4 = texture(occ, ou - vec2(0.0, t)).r;\n"
	"	float od = min(d0, min(min(d1, d2), min(d3, d4)));\n"
	"	float spread = max(d0, max(max(d1, d2), max(d3, d4))) - od;\n"
	"	vec4 w = occinv * vec4(oc.xy, od * 2.0 - 1.0, 1.0);\n"
	"	float z = w.z / w.w + 0.6;\n"
	"	vec2 cr = vec2((c == 1 || c == 2 || c == 5) ? 1.0 : -1.0, (c == 2 || c == 4 || c == 5) ? 1.0 : -1.0);\n"
	"	float s = params.z * (0.4 + ph);\n"
	"	vUV = cr;\n"
	"	vPh = ph;\n"
	"	bool ok = od < 0.999 && abs(z - eye.z) < 400.0 && spread < 0.0003;\n"
	"	gl_Position = ok ? vp * vec4(xy + cr * s, z, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);\n"
	"}\n";

static const char *wx_splash_fs =
	"uniform vec4 color;\n"
	"in vec2 vUV;\n"
	"in float vPh;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	float r = length(vUV);\n"
	"	float ring = smoothstep(0.7, 0.88, r) * (1.0 - smoothstep(0.88, 1.0, r));\n"
	"	float a = ring * (1.0 - vPh) * color.a;\n"
	"	if (a <= 0.003) discard;\n"
	"	outColor = vec4(color.rgb, a);\n"
	"}\n";

static const char *wx_bolt_vs =
	"uniform mat4 vp;\n"
	"in vec3 position;\n"
	"in vec2 texCoord;\n"
	"out float vE;\n"
	"out vec3 vW;\n"
	"void main()\n"
	"{\n"
	"	vE = texCoord.x;\n"
	"	vW = position;\n"
	"	gl_Position = vp * vec4(position, 1.0);\n"
	"}\n";

/* the channel only over the open sky: wherever the view shows anything of the world (a tower, a sign, the edge of a
   roof) it stays behind it, however far the world reaches; and the towers painted on the sky box (jpn2: the whole
   street beyond the map) hide it as well (WXSkyMask) */
static const char *wx_bolt_fs =
	"uniform vec4 color;\n"
	"uniform sampler2D depthtex;\n"
	"uniform sampler2D skymask;\n" /* clear sky by direction: yaw across, elevation 0..90 up */
	"uniform vec3 eye;\n"
	"uniform vec4 rect;\n"     /* the view in the depth texture */
	"uniform vec4 view;\n"     /* viewport x, y, w, h (pixels) */
	"uniform vec4 proj;\n"     /* projection C, D; depth range min, max */
	"in float vE;\n"
	"in vec3 vW;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmis;\n"
	"void main()\n"
	"{\n"
	"	vec2 vuv = (gl_FragCoord.xy - view.xy) / view.zw;\n"
	"	float z = texture(depthtex, rect.xy + vuv * rect.zw).r;\n"
	/* the sky box lies 37800 units out (gl3_warp.c), the world well inside it */
	"	float d = proj.y / ((z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0 + proj.x);\n"
	"	if (d < 30000.0)\n"
	"		discard;\n"
	"	vec3 dir = normalize(vW - eye);\n"
	"	if (dir.z <= 0.0)\n"
	"		discard;\n"
	"	float clear = texture(skymask, vec2(atan(dir.y, dir.x) * 0.15915494 + 0.5, asin(dir.z) * 0.63661977)).r;\n"
	"	if (clear < 0.5)\n"
	"		discard;\n"
	"	float core = 1.0 - abs(vE);\n"
	"	float a = pow(core, 2.0) * color.a;\n"
	"	outColor = vec4(color.rgb * a, 1.0);\n"
	"	outEmis = vec4(color.rgb * a, 1.0);\n"
	"}\n";

/* anti-aircraft fire in the sky: up to eight guns far beyond the map fire bursts of tracers that climb, slow and
   bend down; some end in a flak burst. All from gl_VertexID, 6 vertices per tracer, 40 tracers per gun; the
   world in front hides them by depth (like the lightning bolt) */
static const char *wx_aa_vs =
	"uniform mat4 vp;\n"
	"uniform vec3 eye;\n"
	"uniform float time;\n"
	"uniform vec4 guns[8];\n"   /* position, seed (0 = no gun) */
	"uniform vec4 params;\n"    /* tracer interval (s), muzzle speed, brightness, - */
	"out vec2 vUV;\n"
	"out vec3 vCol;\n"
	"out float vA;\n"
	"out float vK;\n"
	"float H(float n) { return fract(sin(n) * 43758.5453); }\n"
	"void main()\n"
	"{\n"
	"	const int PER = 40;\n"
	"	int id = gl_VertexID / 6, c = gl_VertexID - id * 6;\n"
	"	int g = id / PER, k = id - g * PER;\n"
	"	vec4 gun = guns[min(g, 7)];\n"
	"	vec2 cr = vec2((c == 1 || c == 2 || c == 5) ? 1.0 : -1.0, (c == 2 || c == 4 || c == 5) ? 1.0 : -1.0);\n"
	"	vUV = cr;\n"
	"	gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
	"	vA = 0.0; vK = 0.0; vCol = vec3(0.0);\n"
	"	if (gun.w <= 0.0) return;\n"
	"	float s = gun.w, dt = params.x * (0.8 + 0.4 * H(s * 3.1));\n"
	"	float slot = floor(time / dt) - float(k);\n"
	"	float ts = slot * dt, age = time - ts;\n"
	/* bursts: each gun fires in its own rhythm (on 45..70 % of a 3..8 s period) */
	"	float per = 3.0 + 5.0 * H(s * 7.7);\n"
	"	if (fract(ts / per + H(s * 1.9)) > 0.45 + 0.25 * H(s * 5.3)) return;\n"
	/* the barrel sweeps slowly; every round leaves with a little scatter */
	"	float yaw = H(s * 2.3) * 6.2832 + 0.7 * sin(ts * (0.2 + 0.3 * H(s * 4.1)) + s * 9.0);\n"
	"	float pitch = radians(48.0 + 22.0 * sin(ts * (0.3 + 0.2 * H(s * 8.3)) + s * 3.0));\n"
	"	yaw += (H(slot * 13.17 + s) - 0.5) * 0.04;\n"
	"	pitch += (H(slot * 7.31 + s) - 0.5) * 0.04;\n"
	"	vec3 dir = vec3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch));\n"
	"	float life = 2.4 + 0.8 * H(slot * 3.7 + s);\n"
	"	bool flak = H(slot * 5.9 + s * 1.3) < 0.18;\n"
	"	float v = params.y;\n"
	"	vec3 grav = vec3(0.0, 0.0, -260.0);\n"
	"	if (age > life + (flak ? 0.7 : 0.0)) return;\n"
	"	vec3 col = H(s * 6.1) < 0.3 ? vec3(0.45, 1.0, 0.35) : (H(s * 6.1) < 0.6 ? vec3(1.0, 0.3, 0.12) : vec3(1.0, 0.62, 0.2));\n"
	"	if (age > life)\n"
	"	{\n"
	/* the flak burst where the round ends: a ball of light that grows and fades */
	"		float ta = age - life;\n"
	"		vec3 p = gun.xyz + dir * v * life + 0.5 * grav * life * life;\n"
	"		vec3 to = normalize(p - eye);\n"
	"		vec3 r = normalize(cross(to, vec3(0.0, 0.0, 1.0)));\n"
	"		vec3 u = cross(r, to);\n"
	"		float sz = (60.0 + 260.0 * sqrt(ta / 0.7)) * (1.0 + length(p - eye) * 0.0002);\n"
	"		vA = (1.0 - ta / 0.7) * (ta < 0.06 ? 1.6 : 1.0);\n"
	"		vK = 1.0;\n"
	"		vCol = mix(vec3(1.0, 0.95, 0.8), vec3(1.0, 0.5, 0.15), clamp(ta * 3.0, 0.0, 1.0));\n"
	"		gl_Position = vp * vec4(p + (r * cr.x + u * cr.y) * sz, 1.0);\n"
	"		return;\n"
	"	}\n"
	"	vec3 vel = dir * v + grav * age;\n"
	"	vec3 head = gun.xyz + dir * v * age + 0.5 * grav * age * age;\n"
	"	vec3 tail = head - normalize(vel) * (90.0 + 0.06 * length(vel));\n"
	"	vec3 a = (c == 0 || c == 1 || c == 4) ? tail : head;\n"
	"	float dist = length(a - eye);\n"
	"	vec3 side = normalize(cross(head - tail, a - eye)) * (4.0 + dist * 0.0028);\n"
	"	a += (c == 1 || c == 2 || c == 5) ? side : -side;\n"
	"	vA = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(life - 0.7, life, age)) * (0.75 + 0.25 * sin(age * 40.0 + s));\n"
	"	vCol = col;\n"
	"	gl_Position = vp * vec4(a, 1.0);\n"
	"}\n";

static const char *wx_aa_fs =
	"uniform vec4 params;\n"
	"in vec2 vUV;\n"
	"in vec3 vCol;\n"
	"in float vA;\n"
	"in float vK;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmis;\n"
	"void main()\n"
	"{\n"
	"	float a;\n"
	"	vec3 c = vCol;\n"
	"	if (vK > 0.5)\n"
	"	{\n"
	"		float r = length(vUV);\n"
	"		a = pow(max(1.0 - r, 0.0), 2.0) * vA * 0.9;\n"
	"	}\n"
	"	else\n"
	"	{\n"
	/* a hot white core along the streak, the colour around it, brighter at the head */
	"		float w = 1.0 - abs(vUV.x);\n"
	"		a = pow(w, 1.5) * vA * mix(0.35, 1.0, vUV.y * 0.5 + 0.5);\n"
	"		c = mix(vCol, vec3(1.0, 0.95, 0.85), pow(w, 6.0));\n"
	"	}\n"
	"	if (a <= 0.003) discard;\n"
	"	outColor = vec4(c * a * params.z, 1.0);\n"
	"	outEmis = vec4(c * a * params.z * 0.8, 1.0);\n"
	"}\n";

/* snowfall: flakes in world-fixed cells wrapped around the eye, falling slowly, each swaying on its own and all
   carried by the wind (its drift integrated on the CPU, so gusts push the whole fall sideways); only above the
   first surface of the top-down map, like the rain */
static const char *wx_snow_vs =
	"uniform mat4 vp;\n"
	"uniform vec3 eye;\n"
	"uniform float time;\n"
	"uniform vec4 params;\n"   /* box size, box height, fall speed, flake size */
	"uniform vec4 wind;\n"     /* drift x, y (units), wind x, y (units/s) */
	"uniform float pxsize;\n"  /* one pixel in world units at distance 1 */
	"uniform vec4 layer;\n"    /* fade in from x to y (where the nearer layer ends), seed, - */
	"uniform sampler2D occ;\n"
	"uniform mat4 occvp;\n"
	"out vec2 vUV;\n"
	"out float vA;\n"
	"uint Hu(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }\n"
	"float H(float n) { return float(Hu(uint(gl_VertexID / 6) * 977u + uint(n * 16.0) + uint(layer.z) * 7919u)) / 4294967295.0; }\n"
	"void main()\n"
	"{\n"
	"	int id = gl_VertexID / 6, c = gl_VertexID - id * 6;\n"
	"	float f = float(id);\n"
	"	float B = params.x, Hh = params.y, v = params.z * (0.7 + 0.6 * H(f * 3.7));\n"
	/* lighter flakes drift more */
	"	float drift = 0.7 + 0.6 * H(f * 9.1);\n"
	"	vec3 p;\n"
	"	p.x = eye.x + (fract((H(f * 1.13) * B + wind.x * drift - eye.x) / B) - 0.5) * B;\n"
	"	p.y = eye.y + (fract((H(f * 2.71) * B + wind.y * drift - eye.y) / B) - 0.5) * B;\n"
	"	p.z = eye.z - Hh * 0.45 + fract((H(f * 5.3) * Hh - time * v - eye.z) / Hh) * Hh;\n"
	"	float ph = H(f * 4.3) * 6.2832, fr = 0.6 + 0.9 * H(f * 6.7);\n"
	"	p.x += sin(time * fr + ph) * 9.0;\n"
	"	p.y += cos(time * fr * 0.8 + ph * 1.3) * 9.0;\n"
	"	vec4 oc = occvp * vec4(p, 1.0);\n"
	"	vec2 ou = oc.xy * 0.5 + 0.5;\n"
	"	float od = texture(occ, ou).r;\n"
	"	bool open = any(lessThan(ou, vec2(0.0))) || any(greaterThan(ou, vec2(1.0))) || (oc.z * 0.5 + 0.5 <= od + 0.0002);\n"
	"	float dist = length(p - eye);\n"
	/* nothing right in front of the eye (flakes there were blots flying into the face) */
	"	vA = open ? (1.0 - smoothstep(B * 0.35, B * 0.5, dist)) * smoothstep(layer.x, layer.y, dist) : 0.0;\n"
	"	vec2 cr = vec2((c == 1 || c == 2 || c == 5) ? 1.0 : -1.0, (c == 2 || c == 4 || c == 5) ? 1.0 : -1.0);\n"
	"	vUV = cr;\n"
	"	vec3 to = normalize(p - eye);\n"
	"	vec3 r = normalize(cross(to, vec3(0.0, 0.0, 1.0)));\n"
	"	vec3 u = cross(r, to);\n"
	/* a real flake is small: its world size, kept between 1 and 2.5 pixels on the screen */
	"	float s = clamp(params.w * (0.6 + 0.8 * H(f * 8.3)), dist * pxsize * 0.8, dist * pxsize * 1.6);\n"
	"	gl_Position = vA > 0.0 ? vp * vec4(p + (r * cr.x + u * cr.y) * s, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);\n"
	"}\n";

static const char *wx_snow_fs =
	"uniform vec4 color;\n"
	"in vec2 vUV;\n"
	"in float vA;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	float r = length(vUV);\n"
	"	float a = (1.0 - smoothstep(0.35, 1.0, r)) * vA * color.a;\n"
	"	if (a <= 0.003) discard;\n"
	"	outColor = vec4(color.rgb * mix(1.0, 0.82, smoothstep(0.2, 0.9, r)), a);\n"
	"}\n";

/* war glows, additive: six distant explosions (a fireball that swells and cools) and four fires on the ground
   (a flickering glow); camera-facing quads from gl_VertexID */
static const char *wx_war_vs =
	"uniform mat4 vp;\n"
	"uniform vec3 eye;\n"
	"uniform float time;\n"
	"uniform vec4 exps[6];\n"    /* position, start time (0 = none) */
	"uniform vec4 fires[4];\n"   /* position, seed (0 = none) */
	"out vec2 vUV;\n"
	"out vec3 vCol;\n"
	"out float vA;\n"
	"void main()\n"
	"{\n"
	"	int id = gl_VertexID / 6, c = gl_VertexID - id * 6;\n"
	"	vec2 cr = vec2((c == 1 || c == 2 || c == 5) ? 1.0 : -1.0, (c == 2 || c == 4 || c == 5) ? 1.0 : -1.0);\n"
	"	vUV = cr;\n"
	"	vA = 0.0; vCol = vec3(0.0);\n"
	"	gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
	"	vec3 p; float sz;\n"
	"	if (id < 6)\n"
	"	{\n"
	"		vec4 e = exps[id];\n"
	"		float age = time - e.w;\n"
	"		if (e.w <= 0.0 || age < 0.0 || age > 3.0) return;\n"
	"		sz = 520.0 * (0.35 + 0.65 * smoothstep(0.0, 0.35, age));\n"
	"		vA = (age < 0.1 ? 1.8 : 1.0) * exp(-age * 1.6);\n"
	"		vCol = mix(vec3(1.0, 0.92, 0.7), vec3(1.0, 0.35, 0.08), smoothstep(0.05, 0.9, age));\n"
	"		p = e.xyz + vec3(0.0, 0.0, sz * 0.5);\n"
	"	}\n"
	"	else\n"
	"	{\n"
	"		vec4 fi = fires[id - 6];\n"
	"		if (fi.w <= 0.0) return;\n"
	"		float fl = 0.75 + 0.15 * sin(time * 7.3 + fi.w) + 0.1 * sin(time * 13.1 + fi.w * 2.0);\n"
	"		sz = 380.0 * fl;\n"
	"		vA = 0.55 * fl;\n"
	"		vCol = vec3(1.0, 0.45, 0.12);\n"
	"		p = fi.xyz + vec3(0.0, 0.0, 120.0);\n"
	"	}\n"
	"	vec3 to = normalize(p - eye);\n"
	"	vec3 r = normalize(cross(to, vec3(0.0, 0.0, 1.0)));\n"
	"	vec3 u = cross(r, to);\n"
	"	gl_Position = vp * vec4(p + (r * cr.x + u * cr.y) * sz, 1.0);\n"
	"}\n";

static const char *wx_war_fs =
	"in vec2 vUV;\n"
	"in vec3 vCol;\n"
	"in float vA;\n"
	"out vec4 outColor;\n"
	"out vec4 outEmis;\n"
	"void main()\n"
	"{\n"
	"	float a = pow(max(1.0 - length(vUV), 0.0), 2.2) * vA;\n"
	"	if (a <= 0.003) discard;\n"
	"	outColor = vec4(vCol * a, 1.0);\n"
	"	outEmis = vec4(vCol * a * 0.9, 1.0);\n"
	"}\n";

/* smoke columns over the fires: an upright quad turned to the eye, widening upwards and bent by the wind; the
   fragment shader rolls fractal noise upwards (the smoke climbs), the foot lit orange by the fire */
static const char *wx_smoke_vs =
	"uniform mat4 vp;\n"
	"uniform vec3 eye;\n"
	"uniform vec4 fires[4];\n"
	"uniform vec2 wind;\n"       /* direction of the bend (units at the top) */
	"out vec2 vUV;\n"
	"out float vSeed;\n"
	"void main()\n"
	"{\n"
	"	int id = gl_VertexID / 6, c = gl_VertexID - id * 6;\n"
	"	vec4 fi = fires[id];\n"
	"	vec2 cr = vec2((c == 1 || c == 2 || c == 5) ? 1.0 : -1.0, (c == 2 || c == 4 || c == 5) ? 1.0 : 0.0);\n"
	"	vUV = cr;\n"
	"	vSeed = fi.w;\n"
	"	if (fi.w <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
	"	float hgt = 2600.0 + 600.0 * fract(fi.w * 0.37);\n"
	"	vec3 base = fi.xyz + vec3(wind * cr.y * cr.y, hgt * cr.y);\n"
	"	vec3 to = base - eye; to.z = 0.0;\n"
	"	vec3 r = normalize(cross(normalize(to), vec3(0.0, 0.0, 1.0)));\n"
	"	gl_Position = vp * vec4(base + r * cr.x * mix(260.0, 1300.0, cr.y), 1.0);\n"
	"}\n";

static const char *wx_smoke_fs =
	"uniform float time;\n"
	"in vec2 vUV;\n"
	"in float vSeed;\n"
	"out vec4 outColor;\n"
	"float h2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
	"float vn(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
	"	return mix(mix(h2(i), h2(i + vec2(1.0, 0.0)), f.x), mix(h2(i + vec2(0.0, 1.0)), h2(i + vec2(1.0)), f.x), f.y); }\n"
	"float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 5; i++) { s += a * vn(p); p = p * 2.03 + 1.7; a *= 0.5; } return s; }\n"
	"void main()\n"
	"{\n"
	"	float y = vUV.y, x = vUV.x;\n"
	"	vec2 q = vec2(x * 1.6, y * 4.0 - time * 0.06) + vSeed;\n"
	"	vec2 w = vec2(fbm(q + vec2(0.0, time * 0.02)), fbm(q + 5.2));\n"
	"	float n = fbm(q + w * 1.6);\n"
	"	float edge = 1.0 - smoothstep(0.25, 1.0, abs(x + (w.x - 0.5) * 0.5));\n"
	"	float d = smoothstep(0.26, 0.62, n) * edge * smoothstep(0.0, 0.06, y) * (1.0 - smoothstep(0.6, 1.0, y));\n"
	"	if (d <= 0.004) discard;\n"
	"	vec3 col = mix(vec3(0.7, 0.32, 0.1), vec3(0.2, 0.18, 0.17), smoothstep(0.02, 0.3, y));\n"
	"	col *= 0.6 + 0.7 * n;\n"
	"	outColor = vec4(col, d * 0.92);\n"
	"}\n";

static GLuint
WXLink(const char *vs, const char *fs, qboolean twoout)
{
	GLuint v = vs ? CompilePost(GL_VERTEX_SHADER, vs) : CompilePost(GL_VERTEX_SHADER, post_vs);
	GLuint f = CompilePost(GL_FRAGMENT_SHADER, fs), p;
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
	if (twoout)
	{
		glBindFragDataLocation(p, 1, "outEmis");
	}
#endif
	glBindAttribLocation(p, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(p, GL3_ATTRIB_TEXCOORD, "texCoord");
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		R_Printf(PRINT_ALL, "GL3 rain: link failed: %s\n", log);
		glDeleteProgram(p);
		return 0;
	}

	return p;
}

static qboolean
WXCreate(void)
{
	static const GLenum none[1] = {GL_NONE};

	if (WX.prograin)
	{
		return true;
	}

	WX.prograin = WXLink(wx_rain_vs, wx_rain_fs, false);
	WX.progsplash = WXLink(wx_splash_vs, wx_splash_fs, false);
	WX.progbolt = WXLink(wx_bolt_vs, wx_bolt_fs, true);
	WX.progaa = WXLink(wx_aa_vs, wx_aa_fs, true);
	WX.progsnow = WXLink(wx_snow_vs, wx_snow_fs, false);
	WX.progwar = WXLink(wx_war_vs, wx_war_fs, true);
	WX.progsmoke = WXLink(wx_smoke_vs, wx_smoke_fs, false);

	if (!WX.prograin || !WX.progsplash || !WX.progbolt || !WX.progaa || !WX.progsnow ||
		!WX.progwar || !WX.progsmoke)
	{
		return false;
	}

	WX.w_vp = glGetUniformLocation(WX.progwar, "vp");
	WX.w_eye = glGetUniformLocation(WX.progwar, "eye");
	WX.w_time = glGetUniformLocation(WX.progwar, "time");
	WX.w_exps = glGetUniformLocation(WX.progwar, "exps");
	WX.w_fires = glGetUniformLocation(WX.progwar, "fires");
	WX.s2_vp = glGetUniformLocation(WX.progsmoke, "vp");
	WX.s2_eye = glGetUniformLocation(WX.progsmoke, "eye");
	WX.s2_time = glGetUniformLocation(WX.progsmoke, "time");
	WX.s2_fires = glGetUniformLocation(WX.progsmoke, "fires");
	WX.s2_wind = glGetUniformLocation(WX.progsmoke, "wind");

	WX.n_vp = glGetUniformLocation(WX.progsnow, "vp");
	WX.n_eye = glGetUniformLocation(WX.progsnow, "eye");
	WX.n_time = glGetUniformLocation(WX.progsnow, "time");
	WX.n_params = glGetUniformLocation(WX.progsnow, "params");
	WX.n_occ = glGetUniformLocation(WX.progsnow, "occ");
	WX.n_occvp = glGetUniformLocation(WX.progsnow, "occvp");
	WX.n_color = glGetUniformLocation(WX.progsnow, "color");
	WX.n_wind = glGetUniformLocation(WX.progsnow, "wind");
	WX.n_px = glGetUniformLocation(WX.progsnow, "pxsize");
	WX.n_layer = glGetUniformLocation(WX.progsnow, "layer");

	WX.a_vp = glGetUniformLocation(WX.progaa, "vp");
	WX.a_eye = glGetUniformLocation(WX.progaa, "eye");
	WX.a_time = glGetUniformLocation(WX.progaa, "time");
	WX.a_guns = glGetUniformLocation(WX.progaa, "guns");
	WX.a_params = glGetUniformLocation(WX.progaa, "params");

	WX.r_vp = glGetUniformLocation(WX.prograin, "vp");
	WX.r_eye = glGetUniformLocation(WX.prograin, "eye");
	WX.r_time = glGetUniformLocation(WX.prograin, "time");
	WX.r_params = glGetUniformLocation(WX.prograin, "params");
	WX.r_occ = glGetUniformLocation(WX.prograin, "occ");
	WX.r_occvp = glGetUniformLocation(WX.prograin, "occvp");
	WX.r_color = glGetUniformLocation(WX.prograin, "color");
	WX.s_vp = glGetUniformLocation(WX.progsplash, "vp");
	WX.s_eye = glGetUniformLocation(WX.progsplash, "eye");
	WX.s_time = glGetUniformLocation(WX.progsplash, "time");
	WX.s_params = glGetUniformLocation(WX.progsplash, "params");
	WX.s_occ = glGetUniformLocation(WX.progsplash, "occ");
	WX.s_occvp = glGetUniformLocation(WX.progsplash, "occvp");
	WX.s_color = glGetUniformLocation(WX.progsplash, "color");
	WX.b_vp = glGetUniformLocation(WX.progbolt, "vp");
	WX.b_color = glGetUniformLocation(WX.progbolt, "color");

	glGenVertexArrays(1, &WX.vao);
	glGenBuffers(1, &WX.bolt_vbo);

	glGenTextures(1, &WX.occtex);
	GL3_SelectTMU(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, WX.occtex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, RAIN_OCC, RAIN_OCC, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl3state.currenttexture = WX.occtex;
	glGenFramebuffers(1, &WX.occfbo);
	glBindFramebuffer(GL_FRAMEBUFFER, WX.occfbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, WX.occtex, 0);
	glDrawBuffers(1, none);
	glReadBuffer(GL_NONE);

	if (!Complete("rain occlusion"))
	{
		return false;
	}

	return true;
}

/* the top-down depth of the static world around the eye (orthographic, looking down) */
/* what the static caster set leaves out but still keeps rain and lightning off: glass and other translucent world
   faces, and the brush entities (awnings, func_walls, doors as placed) - once per map, world-space triangles */
static float *wxextra;
static int wxnextra, wxcapextra;
static const void *wxextramodel;

static void
WXExtraGeometry(void)
{
	const gl3model_t *w = gl3_worldmodel;
	const gl3model_t *sub = (w->numsubmodels > 0) ? &w->submodels[0] : w;
	int first = sub->firstmodelsurface, count = sub->nummodelsurfaces, i;

	if (wxextramodel == w)
	{
		return;
	}

	wxextramodel = w;
	wxnextra = 0;

	for (i = 0; i < w->numsurfaces; i++)
	{
		const msurface_t *s = &w->surfaces[i];
		const glpoly_t *p;
		int tf = s->texinfo ? s->texinfo->flags : 0;
		qboolean inworld = (i >= first) && (i < first + count);

		if ((tf & (SURF_SKY | SURF_WARP | SURF_NODRAW)) || (s->flags & (SURF_DRAWSKY | SURF_DRAWTURB)))
		{
			continue;
		}

		/* world faces the static set already has */
		if (inworld && !(tf & (SURF_TRANS33 | SURF_TRANS66 | SOFSURF_ALPHA)))
		{
			continue;
		}

		for (p = s->polys; p; p = p->next)
		{
			int k;

			for (k = 2; k < p->numverts; k++)
			{
				const float *v[3] = {p->vertices[0].pos, p->vertices[k - 1].pos, p->vertices[k].pos};
				int j;

				if (wxnextra + 9 > wxcapextra)
				{
					int c = wxcapextra ? wxcapextra * 2 : 65536;
					float *n = realloc(wxextra, c * sizeof(float));

					if (!n)
					{
						return;
					}

					wxextra = n;
					wxcapextra = c;
				}

				for (j = 0; j < 3; j++)
				{
					{ float *o = wxextra + wxnextra; VectorCopy(v[j], o); }
					wxnextra += 3;
				}
			}
		}
	}
}

static void
WXOcclusion(void)
{
	float zt, zb, cx, cy, half = RAIN_HALF, oldrange[2];
	float *m = WX.occvp;
	vec3_t d;
	GLint oldfunc;

	VectorSubtract(r_newrefdef.vieworg, WX.occc, d);

	if (WX.occmodel == gl3_worldmodel && fabsf(d[0]) < 256 && fabsf(d[1]) < 256 && fabsf(d[2]) < 256)
	{
		return;
	}

	cx = floorf(r_newrefdef.vieworg[0] / 64) * 64;
	cy = floorf(r_newrefdef.vieworg[1] / 64) * 64;
	zt = r_newrefdef.vieworg[2] + 3000;
	zb = r_newrefdef.vieworg[2] - 2000;
	VectorCopy(r_newrefdef.vieworg, WX.occc);
	WX.occmodel = gl3_worldmodel;

	memset(m, 0, 16 * sizeof(float));
	m[0] = 1.0f / half;
	m[5] = 1.0f / half;
	m[10] = -2.0f / (zt - zb);
	m[12] = -cx / half;
	m[13] = -cy / half;
	m[14] = (zt + zb) / (zt - zb);
	m[15] = 1;

	glGetIntegerv(GL_DEPTH_FUNC, &oldfunc);
	glGetFloatv(GL_DEPTH_RANGE, oldrange);
	glBindFramebuffer(GL_FRAMEBUFFER, WX.occfbo);
	glViewport(0, 0, RAIN_OCC, RAIN_OCC);
	glDepthRange(0, 1);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	{
		float one = 1.0f;

		glClearBufferfv(GL_DEPTH, 0, &one);
	}
	WXExtraGeometry();
	GL3_CSM_DrawDepth(m, wxextra, wxnextra, true);
	WX.occvalid = true;
	glDepthFunc(oldfunc);
	glDepthRange(oldrange[0], oldrange[1]);
}

static void
WXInverse4(const float *m, float *o)
{
	/* general 4x4 inverse (column-major) */
	float inv[16], det;
	int i;

	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
	det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	det = (fabsf(det) > 1e-12f) ? 1.0f / det : 0.0f;

	for (i = 0; i < 16; i++)
	{
		o[i] = inv[i] * det;
	}
}

static GLuint SSRSceneColour(void);

/* after the opaque view (and its occlusion): rain, splashes, lightning */
void
GL3_Post_Weather(void)
{
	static const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	hmm_mat4 pv;
	float flash, dens, fw = (float)P.w, fh = (float)P.h;
	qboolean nv = (r_fullbright->value != 0) || (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES);

	if (!P.bound || !WX.active || nv)
	{
		return;
	}

	if (!WXCreate())
	{
		WX.failed = true;
		return;
	}

	GL3_Draw3DBatchesNow();
	GL3_FX_Flush();
	WXOcclusion();
	flash = LightningFlash();
	dens = Q_max(0.0f, Q_min(r_om_rain_density->value, 4.0f));
	pv = HMM_MultiplyMat4(gl3state.projMat3D, gl3state.viewMat3D);

	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	glViewport(P.vx, P.vy, P.vw, P.vh);
	glDrawBuffers(1, one);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL3_BindVAO(WX.vao);

	/* anti-aircraft fire (r_om_aa) */
	if (WX.aa)
	{
		static const GLenum two[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
		vec3_t d;

		/* the guns stay where they were placed for this map until the player has gone far from them */
		VectorSubtract(r_newrefdef.vieworg, WX.gunanchor, d);
		if (WX.gunmodel != gl3_worldmodel || VectorLength(d) > 5000)
		{
			int i, n = 5 + (rand() % 3);

			WX.gunmodel = gl3_worldmodel;
			VectorCopy(r_newrefdef.vieworg, WX.gunanchor);
			memset(WX.guns, 0, sizeof(WX.guns));

			for (i = 0; i < n; i++)
			{
				float az = (i + 0.5f * frand()) * 2 * (float)M_PI / n, dist = 4500 + frand() * 3500;

				WX.guns[i][0] = r_newrefdef.vieworg[0] + cosf(az) * dist;
				WX.guns[i][1] = r_newrefdef.vieworg[1] + sinf(az) * dist;
				WX.guns[i][2] = r_newrefdef.vieworg[2] - 300 + frand() * 200;
				WX.guns[i][3] = 1 + frand() * 100;
			}

			/* three or four burning places on the horizon */
			memset(WX.fires, 0, sizeof(WX.fires));
			n = 3 + (rand() & 1);
			for (i = 0; i < n; i++)
			{
				float az = (i + 0.3f + 0.4f * frand()) * 2 * (float)M_PI / n + 0.7f, dist = 3800 + frand() * 3500;

				WX.fires[i][0] = r_newrefdef.vieworg[0] + cosf(az) * dist;
				WX.fires[i][1] = r_newrefdef.vieworg[1] + sinf(az) * dist;
				WX.fires[i][2] = r_newrefdef.vieworg[2] - 300 + frand() * 100;
				WX.fires[i][3] = 1 + frand() * 50;
			}
		}

		/* the war: smoke columns (blended), then the glows of fires and explosions (additive) */
		if (r_om_war->value)
		{
			glDrawBuffers(1, one);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			GL3_UseProgram(WX.progsmoke);
			glUniformMatrix4fv(WX.s2_vp, 1, GL_FALSE, &pv.Elements[0][0]);
			glUniform3fv(WX.s2_eye, 1, r_newrefdef.vieworg);
			glUniform1f(WX.s2_time, fmodf(r_newrefdef.time, 3600.0f));
			glUniform4fv(WX.s2_fires, 4, &WX.fires[0][0]);
			glUniform2f(WX.s2_wind, 700.0f, 250.0f);
			glDrawArrays(GL_TRIANGLES, 0, 6 * 4);

			glDrawBuffers(2, two);
			glBlendFunc(GL_ONE, GL_ONE);
			GL3_UseProgram(WX.progwar);
			glUniformMatrix4fv(WX.w_vp, 1, GL_FALSE, &pv.Elements[0][0]);
			glUniform3fv(WX.w_eye, 1, r_newrefdef.vieworg);
			glUniform1f(WX.w_time, r_newrefdef.time);
			glUniform4fv(WX.w_exps, 6, &WX.exps[0][0]);
			glUniform4fv(WX.w_fires, 4, &WX.fires[0][0]);
			glDrawArrays(GL_TRIANGLES, 0, 6 * 10);
		}

		glDrawBuffers(2, two);
		glBlendFunc(GL_ONE, GL_ONE);
		GL3_UseProgram(WX.progaa);
		glUniformMatrix4fv(WX.a_vp, 1, GL_FALSE, &pv.Elements[0][0]);
		glUniform3fv(WX.a_eye, 1, r_newrefdef.vieworg);
		glUniform1f(WX.a_time, fmodf(r_newrefdef.time, 3600.0f));
		glUniform4fv(WX.a_guns, 8, &WX.guns[0][0]);
		glUniform4f(WX.a_params, 0.075f, 2300.0f, 1.6f * Q_max(0.0f, r_om_aa->value), 0);
		glDrawArrays(GL_TRIANGLES, 0, 6 * 40 * 8);
		glDrawBuffers(1, one);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	/* snowfall (r_om_snow); on the war maps the same pass lets ash drift down */
	if (WX.snow || (WX.aa && r_om_war->value))
	{
		qboolean ash = !WX.snow;
		float now = r_newrefdef.time, dt = now - WX.windt, wx, wy, gust, light;
		vec3_t lc;

		if ((dt < 0) || (dt > 0.25f))
		{
			dt = 0;
		}
		WX.windt = now;

		/* a steady breeze turning slowly, now and then a gust that sweeps the fall sideways */
		gust = Q_max(0.0f, sinf(now * 0.37f) + sinf(now * 0.83f + 1.7f) - 0.7f) * 140.0f;
		wx = cosf(now * 0.05f) * (35.0f + gust);
		wy = sinf(now * 0.05f) * (35.0f + gust) * 0.6f + 10.0f;
		WX.windx = fmodf(WX.windx + wx * dt, 100000.0f);
		WX.windy = fmodf(WX.windy + wy * dt, 100000.0f);

		/* the flakes take the light of the place (the probe at the eye), never fully dark */
		GL3_SoF_LightPoint(r_newrefdef.vieworg, lc, 255);
		light = Q_max(0.65f, Q_min(1.1f, 0.6f + Q_max(lc[0], Q_max(lc[1], lc[2]))));

		GL3_UseProgram(WX.progsnow);
		glUniformMatrix4fv(WX.n_vp, 1, GL_FALSE, &pv.Elements[0][0]);
		glUniform3fv(WX.n_eye, 1, r_newrefdef.vieworg);
		glUniform1f(WX.n_time, fmodf(r_newrefdef.time, 3600.0f));
		glUniform1f(WX.n_px, 2.0f / (gl3state.projMat3D.Elements[1][1] * Q_max(P.vh, 1)));
		glUniform4f(WX.n_wind, WX.windx, WX.windy, wx, wy);
		glUniform1i(WX.n_occ, 0);
		glUniformMatrix4fv(WX.n_occvp, 1, GL_FALSE, WX.occvp);
		if (ash)
		{
			glUniform4f(WX.n_color, 0.2f * light, 0.19f * light, 0.18f * light, 0.8f);
		}
		else
		{
			glUniform4f(WX.n_color, 0.95f * light, 0.97f * light, 1.0f * light, 1.0f);
		}
		BindTex(GL_TEXTURE0, WX.occtex);

		if (ash)
		{
			glUniform4f(WX.n_params, 900.0f, 700.0f, 38.0f, 0.8f);
			glUniform4f(WX.n_layer, 40.0f, 120.0f, 0, 0);
			glDrawArrays(GL_TRIANGLES, 0, 6 * 1800);
		}
		else
		{
			/* three layers in depth, each starting where the nearer one ends: clear flakes near, a dense fall in
			   the middle, a veil of falling snow far into the fog */
			static const float lay[3][6] = {
				/* box, height, fade from, fade to, flakes, size */
				{700.0f, 600.0f, 45.0f, 110.0f, 12000.0f, 0.5f},
				{1800.0f, 1300.0f, 250.0f, 340.0f, 16000.0f, 0.7f},
				{3600.0f, 2200.0f, 650.0f, 880.0f, 14000.0f, 1.0f},
			};
			float dens = Q_min(r_om_snow->value, 3.0f);
			int l;

			for (l = 0; l < 3; l++)
			{
				glUniform4f(WX.n_params, lay[l][0], lay[l][1], 70.0f, lay[l][5]);
				glUniform4f(WX.n_layer, lay[l][2], lay[l][3], (float)l, 0);
				glDrawArrays(GL_TRIANGLES, 0, 6 * (int)(lay[l][4] * dens));
			}
		}
	}

	if (!WX.rain)
	{
		goto nodrops;
	}

	/* drops: a near dense layer and a far thin one (ending at 1200 units: the distant towers stay clear) */
	{
		GLuint col = SSRSceneColour();
		static const float lay[2][8] = {
			/* box, height, near fade from, to, far fade from, to, drops, opacity */
			{1000.0f, 900.0f, 40.0f, 140.0f, 380.0f, 500.0f, 9000.0f, 0.55f},
			{2600.0f, 1700.0f, 380.0f, 500.0f, 1000.0f, 1200.0f, 9000.0f, 0.38f},
		};
		int l;

		GL3_UseProgram(WX.prograin);
		glUniformMatrix4fv(WX.r_vp, 1, GL_FALSE, &pv.Elements[0][0]);
		glUniform3fv(WX.r_eye, 1, r_newrefdef.vieworg);
		glUniform1f(WX.r_time, r_newrefdef.time);
		glUniform1i(WX.r_occ, 0);
		glUniformMatrix4fv(WX.r_occvp, 1, GL_FALSE, WX.occvp);
		glUniform1i(GL3_UniformLoc(WX.prograin, "scenecol"), 1);
		glUniform1i(GL3_UniformLoc(WX.prograin, "depthtex"), 2);
		glUniform4f(GL3_UniformLoc(WX.prograin, "rect"), P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniform4f(GL3_UniformLoc(WX.prograin, "proj"), gl3state.projMat3D.Elements[2][2],
			gl3state.projMat3D.Elements[3][2], gl3depthmin, gl3depthmax);
		glUniform4f(GL3_UniformLoc(WX.prograin, "view"), (float)P.vx, (float)P.vy, (float)P.vw, (float)P.vh);
		glUniform1f(GL3_UniformLoc(WX.prograin, "refr"), col ? 1.0f : 0.0f);
		BindTex(GL_TEXTURE2, P.texdepthcopy);
		BindTex(GL_TEXTURE1, col ? col : WX.occtex);
		BindTex(GL_TEXTURE0, WX.occtex);

		for (l = 0; l < 2; l++)
		{
			glUniform4f(WX.r_params, lay[l][0], lay[l][1], 750.0f, 0.032f);
			glUniform4f(GL3_UniformLoc(WX.prograin, "layer"), lay[l][2], lay[l][3], lay[l][4], lay[l][5]);
			glUniform4f(WX.r_color, 0.55f + flash * 0.9f, 0.6f + flash * 0.9f, 0.68f + flash * 1.0f,
				lay[l][7] + flash * 0.25f);
			glDrawArrays(GL_TRIANGLES, 0, 6 * (int)(lay[l][6] * dens));
		}
	}

	/* splashes */
	{
		float inv[16];

		WXInverse4(WX.occvp, inv);
		GL3_UseProgram(WX.progsplash);
		glUniformMatrix4fv(WX.s_vp, 1, GL_FALSE, &pv.Elements[0][0]);
		glUniform3fv(WX.s_eye, 1, r_newrefdef.vieworg);
		glUniform1f(WX.s_time, r_newrefdef.time);
		glUniform4f(WX.s_params, 900.0f, 0.3f, 3.5f, 0);
		glUniform1i(WX.s_occ, 0);
		glUniformMatrix4fv(WX.s_occvp, 1, GL_FALSE, WX.occvp);
		glUniformMatrix4fv(GL3_UniformLoc(WX.progsplash, "occinv"), 1, GL_FALSE, inv);
		glUniform4f(WX.s_color, 0.7f + flash * 0.5f, 0.72f + flash * 0.5f, 0.78f + flash * 0.5f, 0.16f);
		glDrawArrays(GL_TRIANGLES, 0, 6 * (int)(1500 * dens));
	}

nodrops:

	/* lightning: the bolt far in the sky (a soft glow and a bright core), into the scene and the emissive target;
	   its light on the world comes from the world shaders (omLightningLight, GL3_SoF_LightningUniforms) */
	if (flash > 0.01f && WX.nbolt >= 2)
	{
		static float quads[BOLT_MAXPTS * 3 * 5 * 2];
		int pass;

		WXSkyMask();
		glDrawBuffers(2, bufs);
		glDisable(GL_DEPTH_TEST);
		glBlendFunc(GL_ONE, GL_ONE);
		GL3_UseProgram(WX.progbolt);
		glUniform1i(GL3_UniformLoc(WX.progbolt, "skymask"), 3);
		glUniform3fv(GL3_UniformLoc(WX.progbolt, "eye"), 1, r_newrefdef.vieworg);
		BindTex(GL_TEXTURE3, WX.skytex);
		glUniformMatrix4fv(WX.b_vp, 1, GL_FALSE, &pv.Elements[0][0]);
		glUniform1i(GL3_UniformLoc(WX.progbolt, "depthtex"), 2);
		glUniform4f(GL3_UniformLoc(WX.progbolt, "rect"), P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniform4f(GL3_UniformLoc(WX.progbolt, "view"), (float)P.vx, (float)P.vy, (float)P.vw, (float)P.vh);
		glUniform4f(GL3_UniformLoc(WX.progbolt, "proj"), gl3state.projMat3D.Elements[2][2],
			gl3state.projMat3D.Elements[3][2], gl3depthmin, gl3depthmax);
		BindTex(GL_TEXTURE2, P.texdepthcopy);

		GL3_BindVAO(WX.vao);
		GL3_BindVBO(WX.bolt_vbo);
		glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
		glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);

		for (pass = 0; pass < 2; pass++)
		{
			float w = pass ? 28.0f : 150.0f;
			int i, n = 0;

			for (i = 0; i + 1 < WX.nbolt; i += 2)
			{
				const float *a = &WX.bolt[i * 3], *b = &WX.bolt[(i + 1) * 3];
				vec3_t dir, to, s;
				int k, j;
				float pts[6][5];

				VectorSubtract(b, a, dir);
				VectorSubtract(a, r_newrefdef.vieworg, to);
				CrossProduct(dir, to, s);
				if (VectorNormalize(s) == 0)
				{
					continue;
				}
				/* the same apparent width however far the strike (sized for 6500 units) */
				VectorScale(s, w * Q_max(VectorLength(to) / 6500.0f, 1.0f), s);

				for (k = 0; k < 6; k++)
				{
					const float *e = (k == 0 || k == 1 || k == 4) ? a : b;
					float side = (k == 1 || k == 2 || k == 5) ? 1.0f : -1.0f;

					for (j = 0; j < 3; j++)
					{
						pts[k][j] = e[j] + s[j] * side;
					}
					pts[k][3] = side;
					pts[k][4] = 0;
				}

				if (n + 30 > (int)(sizeof(quads) / sizeof(quads[0])))
				{
					break;
				}

				memcpy(quads + n, pts, sizeof(pts));
				n += 30;
			}

			glUniform4f(WX.b_color, 0.75f, 0.82f, 1.0f, pass ? Q_min(1.0f, flash * 1.8f) : flash * 0.22f);
			glBufferData(GL_ARRAY_BUFFER, n * sizeof(float), quads, GL_STREAM_DRAW);
			qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
			qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (GLintptr)(3 * sizeof(float)));
			glDrawArrays(GL_TRIANGLES, 0, n / 5);
		}

		glDisableVertexAttribArray(GL3_ATTRIB_POSITION);
		glDisableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	}

	GL3_BindVAO(0);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	SceneBuffers();
	glViewport(P.vx, P.vy, P.vw, P.vh);
	gl3state.currentlightmap = -1;
	GL3_SelectTMU(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);
	GL3_SelectTMU(GL_TEXTURE0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

/* ------------------------------------------------------------------
 * GFX-modern r_om_ssr: screen-space reflections. The lightmapped world
 * shaders write a material target (attachment 2): reflectivity by the SoF
 * surface material, floors more than walls, polished (little fine texture
 * detail) more than rough, and wet ground with puddles under the rain
 * (r_om_rain's top-down map); everything else writes 0, translucent
 * surfaces and fog fade what is behind them. After the view (and its
 * occlusion) each reflective pixel rebuilds its position and normal from
 * the depth copy, marches the mirrored eye ray across the screen in
 * perspective-correct steps (more of them near the start), refines the
 * hit by bisection and takes its colour from the last finished frame
 * (half size, mipmapped, reprojected; no copy of the unfinished view):
 * rough surfaces read a higher level, the farther the hit the blurrier.
 * Fresnel raises the reflection towards grazing angles; it fades at the
 * screen edges, with the distance and for rays coming back at the eye.
 * Tracing runs at half size; the result goes over the reflective pixels of
 * the full-size target. Mirrors (puddles, glass, liquids) replace the scene
 * by the reflection, dry floors and metal mostly add it (lamps shine on
 * them). The modulating passes (detail textures, marks) keep the material
 * target as it is (per-buffer colour mask).
 * r_om_ssr strength (0 = off, also removes the material target),
 * r_om_ssr_wet rain wetness, r_om_ssr_dist ray length in units,
 * r_om_ssr_quality 1..3 (16 / 24 / 40 steps), r_om_ssr_debug 1 shows the
 * reflection weight, 2 the material target. Not in GLES3 builds.
 * ------------------------------------------------------------------ */

static struct
{
	qboolean failed;
	int cw, ch, vw, vh;
	GLuint colortex;   /* scene texture the apply framebuffer was made for */
	GLuint coltex, colfbo, applyfbo, reftex, reffbo, smtex[2], smfbo[2], histtex[2], histfbo[2];
	int histcur;
	qboolean havehist;
	qboolean havecol;  /* coltex holds the last frame (seen through prevpv) */
	hmm_mat4 prevpv;
	float frame;
	GLuint prog, progapply, progsmooth, progtemp;
	GLint t_cur, t_hist, t_depth, t_rect, t_proj, t_proj2, t_size, t_reproj, t_keep;
	GLint u_depth, u_mat, u_col, u_rect, u_proj, u_proj2, u_params, u_texel, u_frame, u_strength, u_reproj;
	GLint u_inv, u_ripple;
	GLint a_ref, a_sm, a_depth, a_rect, a_proj, a_proj2, a_size, a_texel, a_inv, a_glint;
	GLint m_depth, m_src, m_rect, m_proj, m_proj2, m_params, m_texel;
} SR;

static const char *ssr_fs =
	"uniform sampler2D depthtex;\n"
	"uniform sampler2D mattex;\n"
	"uniform sampler2D coltex;\n"   /* the last frame's view, half size, with mipmaps (0..1 = the view) */
	"uniform mat4 reproj;\n"        /* this view space -> the last frame's clip space */
	"uniform mat4 invview;\n"       /* this view space -> world */
	"uniform vec2 ripple;\n"        /* time, strength */
	"uniform vec4 rect;\n"
	"uniform vec4 proj;\n"          /* projection C, D; depth range min, max */
	"uniform vec4 proj2;\n"         /* 1/P00, 1/P11, P00, P11 */
	"uniform vec4 params;\n"        /* ray length, steps, far distance, debug */
	"uniform vec4 texel;\n"         /* one pixel in view coordinates (x, y), colour copy height */
	"uniform float frame;\n"
	"uniform float strength;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"float Dist(vec2 v)\n"
	"{\n"
	"	float z = texture(depthtex, rect.xy + v * rect.zw).r;\n"
	"	float ndc = (z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0;\n"
	"	return proj.y / (ndc + proj.x);\n"
	"}\n"
	"vec3 Pos(vec2 v)\n"
	"{\n"
	"	float d = Dist(v);\n"
	"	vec2 n = v * 2.0 - 1.0;\n"
	"	return vec3(n.x * d * proj2.x, n.y * d * proj2.y, -d);\n"
	"}\n"	/* a degenerate depth neighbourhood gives no normal (a NaN here spread through the frame and its mipmaps) */
	"vec3 SafeN(vec3 c) { float l = length(c); return l > 1e-12 ? c / l : vec3(0.0, 0.0, 1.0); }\n"
	"vec2 Proj(vec3 p) { return vec2(p.x * proj2.z, p.y * proj2.w) / -p.z * 0.5 + 0.5; }\n"
	"void main()\n"
	"{\n"
	"	vec4 m = texture(mattex, vuv);\n"
	"	float refl = m.r;\n"
	"	int dbg = int(params.w + 0.5);\n"
	"	if (dbg == 2)\n"
	"	{\n"
	"		outColor = vec4(refl * 3.0, m.r > 0.003 ? m.g / m.r : 0.0, 0.0, 1.0);\n"
	"		return;\n"
	"	}\n"
	/* below this a reflection is not seen (walls of faintly glossy classes); the lamp glints of the apply pass still
	   come from the material */
	"	if (refl < 0.015)\n"
	"		discard;\n"
	/* r_om_ssr_checker: half the pixels are traced each frame, in a checkerboard that alternates; the others are
	   filled from their neighbours and the history by the temporal pass (marked with alpha -1) */
	"	if (texel.w > 0.5 && ((int(gl_FragCoord.x) + int(gl_FragCoord.y) + int(frame)) & 1) != 0)\n"
	"	{\n"
	"		outColor = vec4(0.0, 0.0, 0.0, -1.0);\n"
	"		return;\n"
	"	}\n"
	"	float rough = clamp(m.g / refl, 0.0, 1.0);\n"
	"	vec3 P = Pos(vuv);\n"
	"	float d = -P.z;\n"
	"	if (d > params.z || d < 1.0)\n"
	"		discard;\n"
	/* normal from the neighbours with the smaller depth step (no smearing across edges) */
	"	vec2 px = texel.xy;\n"
	"	vec3 Pr = Pos(vuv + vec2(px.x, 0.0)), Pl = Pos(vuv - vec2(px.x, 0.0));\n"
	"	vec3 Pu = Pos(vuv + vec2(0.0, px.y)), Pd = Pos(vuv - vec2(0.0, px.y));\n"
	"	vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;\n"
	"	vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;\n"
	"	vec3 N = SafeN(cross(dx, dy));\n"
	"	vec3 V = normalize(-P);\n"
	"	if (dot(N, V) < 0.0) N = -N;\n"
	/* the faces of a map are nearly all level or upright: a normal within ~10 degrees of a world axis is that axis.
	   Rebuilt from the half-size depth it wobbles at grazing angles, and every wobble moved the reflection: stripes,
	   torn rows, sparkle at the edges */
	"	{\n"
	"		vec3 Nw = mat3(invview) * N;\n"
	"		vec3 an = abs(Nw);\n"
	"		float mc = max(an.x, max(an.y, an.z));\n"
	"		if (mc > 0.985)\n"
	"		{\n"
	"			Nw = an.z == mc ? vec3(0.0, 0.0, sign(Nw.z)) : an.x == mc ? vec3(sign(Nw.x), 0.0, 0.0) : vec3(0.0, sign(Nw.y), 0.0);\n"
	"			N = transpose(mat3(invview)) * Nw;\n"
	"		}\n"
	"	}\n"
	/* smooth level faces (water, puddles, polished floors) ripple a little: the reflection wobbles softly instead
	   of showing the exact pattern of what it hits (arch ribs over the kos1 channel gave stripes) */
	"	if (rough < 0.12 && ripple.y > 0.0)\n"
	"	{\n"
	"		vec3 Nw = mat3(invview) * N;\n"
	"		if (Nw.z > 0.7)\n"
	"		{\n"
	"			vec3 wp = (invview * vec4(P, 1.0)).xyz;\n"
	"			float tt = ripple.x;\n"
	"			vec2 q = vec2(sin(wp.x * 0.09 + tt * 1.7) + sin(wp.y * 0.13 - tt * 1.3) + sin((wp.x + wp.y) * 0.21 + tt * 2.3),\n"
	"				cos(wp.y * 0.08 - tt * 1.1) + cos(wp.x * 0.15 + tt * 1.9) + cos((wp.x - wp.y) * 0.19 - tt * 2.6));\n"
	"			Nw = normalize(Nw + vec3(q * 0.012 * ripple.y * (1.0 - rough / 0.12), 0.0));\n"
	"			N = normalize(transpose(mat3(invview)) * Nw);\n"
	"		}\n"
	"	}\n"
	"	vec3 R = normalize(reflect(-V, N));\n"
	"	float ndv = clamp(dot(N, V), 0.0, 1.0);\n"
	/* Schlick towards grazing angles, at most three times the head-on value, less on rough faces */
	"	float k = refl + (min(0.85, refl * 3.0) - refl) * pow(1.0 - ndv, 5.0) * (1.0 - rough);\n"
	"	vec3 P0 = P + N * (0.5 + d * 0.002);\n"
	"	float len = params.x;\n"
	"	if (R.z > 0.0) len = min(len, (-P0.z - 2.0) / R.z);\n"   /* stay in front of the eye */
	"	if (len < 2.0)\n"
	"		discard;\n"
	"	vec3 E = P0 + R * len;\n"
	"	vec2 s0 = Proj(P0), s1 = Proj(E), ds = s1 - s0;\n"
	/* the part of the ray on the screen */
	"	float tmax = 1.0;\n"
	"	if (ds.x > 0.0) tmax = min(tmax, (1.0 - s0.x) / ds.x); else if (ds.x < 0.0) tmax = min(tmax, -s0.x / ds.x);\n"
	"	if (ds.y > 0.0) tmax = min(tmax, (1.0 - s0.y) / ds.y); else if (ds.y < 0.0) tmax = min(tmax, -s0.y / ds.y);\n"
	"	float lpx = length(ds / px) * tmax;\n"
	"	if (lpx < 2.0)\n"
	"		discard;\n"
	"	float tmin = 1.5 / length(ds / px);\n"
	"	float iz0 = 1.0 / P0.z, iz1 = 1.0 / E.z;\n"
	/* rough faces read a blurred level of the colour: half the steps find the same */
	"	int steps = rough > 0.35 ? int(params.y) / 2 : int(params.y);\n"
	"	float j = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + frame * 0.618034);\n"
	"	float tp = 0.0, hit = -1.0, rdp = -P0.z;\n"
	"	for (int i = 0; i < steps; i++)\n"
	"	{\n"
	"		float f = (float(i) + j) / float(steps);\n"
	"		float t = max(tmax * f * f, tmin);\n"
	"		float rd = -1.0 / mix(iz0, iz1, t);\n"
	"		float sd = Dist(s0 + ds * t);\n"
	/* behind the depth: the crossing is refined first, then judged by the thickness there (a coarse far step that
	   jumped from in front of a wall to deep behind it was taken for a miss: bands of hit and missed rows) */
	"		if (rd > sd)\n"
	"		{\n"
	"			float ra = tp, rb = t;\n"
	"			for (int r = 0; r < 6; r++)\n"
	"			{\n"
	"				float mid = (ra + rb) * 0.5;\n"
	"				if (-1.0 / mix(iz0, iz1, mid) > Dist(s0 + ds * mid)) rb = mid; else ra = mid;\n"
	"			}\n"
	"			float zb = -1.0 / mix(iz0, iz1, rb), sb = Dist(s0 + ds * rb);\n"
	"			if (zb - sb < max(4.0, sb * 0.02) + abs(zb + 1.0 / mix(iz0, iz1, ra)))\n"
	"			{\n"
	"				hit = rb;\n"
	"				tp = ra;\n"
	"				break;\n"
	"			}\n"
	"		}\n"
	"		tp = t;\n"
	"		rdp = rd;\n"
	"	}\n"
	"	if (hit < 0.0)\n"
	"	{\n"
	/* no hit (the ray left the view or went into the sky): mirrors take the blurred colour where it left, else a
	   puddle would show as a dark stain; the rest keeps the scene */
	"		if (refl < 0.2 || dbg != 0)\n"
	"			discard;\n"
	"		vec3 c = textureLod(coltex, clamp(s0 + ds * tmax, 0.0, 1.0), 4.0).rgb;\n"
	"		float w = clamp(k * strength * 0.7 * (1.0 - smoothstep(0.35, 0.9, R.z)), 0.0, 0.85);\n"
	"		outColor = vec4(c * w, w * smoothstep(0.15, 0.45, refl));\n"
	"		return;\n"
	"	}\n"
	"	float a = tp, b = hit;\n"
	"	for (int i = 0; i < 5; i++)\n"
	"	{\n"
	"		float mid = (a + b) * 0.5;\n"
	"		if (-1.0 / mix(iz0, iz1, mid) > Dist(s0 + ds * mid)) b = mid; else a = mid;\n"
	"	}\n"
	"	vec2 sh = s0 + ds * b;\n"
	"	vec3 H = Pos(sh);\n"
	/* the sky: mirrors (water, puddles) show it where the ray reaches it, other faces keep the scene */
	"	if (-H.z > params.z)\n"
	"	{\n"
	"		if (refl < 0.2)\n"
	"			discard;\n"
	"		H = P + R * params.x;\n"
	"	}\n"
	"	float dist = length(H - P);\n"
	/* the colour of the hit in the last frame */
	"	vec4 pc = reproj * vec4(H, 1.0);\n"
	"	vec2 su = pc.w > 0.0 ? pc.xy / pc.w * 0.5 + 0.5 : vec2(-1.0);\n"
	"	vec2 e = min(min(sh, 1.0 - sh), min(su, 1.0 - su));\n"
	"	float conf = smoothstep(0.0, 0.07, min(e.x, e.y));\n"
	"	conf *= 1.0 - smoothstep(params.x * 0.6, params.x, dist);\n"
	"	conf *= 1.0 - smoothstep(0.35, 0.9, R.z);\n"
	/* glossy: the blur grows with the roughness and the way to the hit, seen at this pixel's distance */
	"	float blur = rough * dist * proj2.w * texel.z / max(d, 1.0);\n"
	"	vec3 c = textureLod(coltex, su, clamp(log2(1.0 + blur), rough * 3.0, 6.0)).rgb;\n"
	"	float w = clamp(k * conf * strength, 0.0, 0.85);\n"
	/* mirrors: towards the edges the hit fades into the colour a miss would take, not to nothing (a hard border
	   between hit and miss rows showed as stripes on the water) */
	"	if (refl >= 0.2)\n"
	"	{\n"
	"		vec3 fc = textureLod(coltex, clamp(s0 + ds * tmax, 0.0, 1.0), 4.0).rgb;\n"
	"		float fw = 0.7 * (1.0 - smoothstep(0.35, 0.9, R.z));\n"
	"		c = mix(fc, c, conf / max(mix(fw, 1.0, conf), 1e-3));\n"
	"		w = clamp(k * strength * mix(fw, 1.0, conf) * (1.0 - smoothstep(0.35, 0.9, R.z)), 0.0, 0.85);\n"
	"	}\n"
	"	if (dbg == 4)\n"
	"		outColor = vec4(c, 1.0);\n"
	"	else if (dbg == 5)\n"
	"		outColor = vec4(su, textureLod(coltex, su, 0.0).g * 4.0, 1.0);\n"
	"	else if (dbg == 1)\n"
	"		outColor = vec4(vec3(w), 1.0);\n"
	/* premultiplied (filtered up without dark fringes). Mirrors (puddles, glass) replace the scene by the reflection;
	   dry floors and metal mostly add it, so bright lamps shine on them and dark walls don't darken them (their
	   lightmap already holds what they receive) */
	"	else\n"
	"		outColor = vec4(c * w, w * smoothstep(0.15, 0.45, refl));\n"
	"	if (any(isnan(outColor)) || any(isinf(outColor)))\n"
	"		discard;\n"
	"}\n";

/* the material smoothed across texture seams, at half size: 9 taps over about r_om_ssr_smooth world units, one axis
   per pass, only between samples on the plane of the centre pixel (floor with floor, never floor with the wall or
   a character standing on it). Values stay premultiplied: reflectivity, reflectivity x roughness, wetness, light */
static const char *ssr_smooth_fs =
	"uniform sampler2D depthtex;\n"
	"uniform sampler2D srctex;\n"
	"uniform vec4 rect;\n"
	"uniform vec4 proj;\n"     /* projection C, D; depth range min, max */
	"uniform vec4 proj2;\n"    /* 1/P00, 1/P11, P00, P11 */
	"uniform vec4 params;\n"   /* 1 = source is the full-size target (through rect), axis x, axis y, radius (units) */
	"uniform vec2 texel;\n"    /* one full-size pixel in view coordinates */
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"float Dist(vec2 v)\n"
	"{\n"
	"	float z = texture(depthtex, rect.xy + v * rect.zw).r;\n"
	"	float ndc = (z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0;\n"
	"	return proj.y / (ndc + proj.x);\n"
	"}\n"
	"vec3 Pos(vec2 v)\n"
	"{\n"
	"	float d = Dist(v);\n"
	"	vec2 n = v * 2.0 - 1.0;\n"
	"	return vec3(n.x * d * proj2.x, n.y * d * proj2.y, -d);\n"
	"}\n"	/* a degenerate depth neighbourhood gives no normal (a NaN here spread through the frame and its mipmaps) */
	"vec3 SafeN(vec3 c) { float l = length(c); return l > 1e-12 ? c / l : vec3(0.0, 0.0, 1.0); }\n"
	"vec4 Src(vec2 v) { return params.x > 0.5 ? texture(srctex, rect.xy + v * rect.zw) : texture(srctex, v); }\n"
	"void main()\n"
	"{\n"
	"	vec4 c = Src(vuv);\n"
	/* matte and dry (most of a view since the material classes): nothing to smooth */
	"	if (c.r < 0.002 && c.b < 0.002)\n"
	"	{\n"
	"		outColor = c;\n"
	"		return;\n"
	"	}\n"
	"	vec3 P = Pos(vuv);\n"
	"	float d = -P.z;\n"
	"	vec3 Pr = Pos(vuv + vec2(texel.x, 0.0)), Pl = Pos(vuv - vec2(texel.x, 0.0));\n"
	"	vec3 Pu = Pos(vuv + vec2(0.0, texel.y)), Pd = Pos(vuv - vec2(0.0, texel.y));\n"
	"	vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;\n"
	"	vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;\n"
	"	vec3 N = SafeN(cross(dx, dy));\n"
	/* the radius on the screen at this distance, at most 8 % of the view near the eye */
	"	vec2 rad = params.yz * min(0.5 * vec2(proj2.z, proj2.w) * params.w / max(d, 1.0), vec2(0.08));\n"
	"	float tol = 1.5 + d * 0.01;\n"
	"	vec4 sum = c;\n"
	"	float ws = 1.0;\n"
	/* five taps half the radius apart (nine cost 0.6 ms on the tiled subway of tsr1 for no visible difference) */
	"	for (int i = -2; i <= 2; i++)\n"
	"	{\n"
	"		if (i == 0) continue;\n"
	"		vec2 v = vuv + rad * (float(i) * 0.5);\n"
	"		if (any(lessThan(v, vec2(0.0))) || any(greaterThan(v, vec2(1.0)))) continue;\n"
	"		float w = exp(-float(i * i) / 2.0) * (1.0 - smoothstep(tol * 0.5, tol, abs(dot(Pos(v) - P, N))));\n"
	"		vec4 sv = Src(v);\n"
	"		if (any(isnan(sv)) || isnan(w)) continue;\n"
	"		sum += sv * w;\n"
	"		ws += w;\n"
	"	}\n"
	"	outColor = sum / ws;\n"
	"	if (any(isnan(outColor)) || any(isinf(outColor))) outColor = vec4(0.0);\n"
	"}\n";

/* the reflections accumulated over frames (half size): last frame's result at this surface point (reprojected),
   clamped to the range of this frame's 3x3 neighbourhood (no ghosts of what moved), mixed with this frame's
   jittered trace: the noise and the stepped edges between hit and missed rows average out */
static const char *ssr_temporal_fs =
	"uniform sampler2D cur;\n"
	"uniform sampler2D hist;\n"
	"uniform sampler2D depthtex;\n"
	"uniform vec4 rect;\n"
	"uniform vec4 proj;\n"
	"uniform vec4 proj2;\n"
	"uniform vec4 size;\n"     /* half size: width, height, 1/width, 1/height */
	"uniform mat4 reproj;\n"   /* this view space -> last frame's clip space */
	"uniform float keep;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"void main()\n"
	"{\n"
	"	vec4 c = texture(cur, vuv);\n"
	/* a pixel not traced this frame (r_om_ssr_checker, alpha -1): the mean of its traced neighbours for now, the
	   history carries it; the neighbourhood range only from traced pixels */
	"	bool skip = c.a < -0.5;\n"
	"	vec4 mn = vec4(1e9), mx = vec4(-1e9), nsum = vec4(0.0);\n"
	"	float nn = 0.0;\n"
	"	for (int y = -1; y <= 1; y++)\n"
	"		for (int x = -1; x <= 1; x++)\n"
	"		{\n"
	"			vec4 s = texture(cur, vuv + vec2(float(x), float(y)) * size.zw);\n"
	"			if (s.a < -0.5) continue;\n"
	"			mn = min(mn, s);\n"
	"			mx = max(mx, s);\n"
	"			if (x == 0 || y == 0) { nsum += s; nn += 1.0; }\n"
	"		}\n"
	"	if (skip) c = nn > 0.0 ? nsum / nn : vec4(0.0);\n"
	"	if (mn.a > 1e8) { mn = c; mx = c; }\n"
	"	float z = texture(depthtex, rect.xy + vuv * rect.zw).r;\n"
	"	float d = proj.y / ((z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0 + proj.x);\n"
	"	vec2 n = vuv * 2.0 - 1.0;\n"
	"	vec4 pc = reproj * vec4(n.x * d * proj2.x, n.y * d * proj2.y, -d, 1.0);\n"
	"	outColor = c;\n"
	"	if (keep > 0.0 && pc.w > 0.0)\n"
	"	{\n"
	"		vec2 pu = pc.xy / pc.w * 0.5 + 0.5;\n"
	"		if (all(greaterThan(pu, vec2(0.0))) && all(lessThan(pu, vec2(1.0))))\n"
	"		{\n"
	"			vec4 h = clamp(texture(hist, pu), mn, mx);\n"
	"			outColor = mix(c, h, skip ? max(keep, 0.6) : keep);\n"
	"		}\n"
	"	}\n"
	"	if (any(isnan(outColor)) || any(isinf(outColor))) outColor = vec4(0.0);\n"
	"}\n";

/* over the scene at full size: the half-size reflections and smoothed material, upsampled where the depth agrees
   (no halo on a character in front of a shiny floor); the wet darkening; the glints of the eight nearest map lamps
   (lump 20, gl3_lamps.c), gated by the light the face receives (no shadows here) */
static const char *ssr_apply_fs =
	"uniform sampler2D reftex;\n"
	"uniform sampler2D smtex;\n"
	"uniform sampler2D depthtex;\n"
	"uniform vec4 rect;\n"
	"uniform vec4 proj;\n"
	"uniform vec4 proj2;\n"
	"uniform vec4 size;\n"     /* half size: width, height, 1/width, 1/height */
	"uniform vec2 texel;\n"    /* one full-size pixel in view coordinates */
	"uniform mat4 invview;\n"  /* view space -> world */
	"uniform vec4 glint;\n"    /* glint strength, wet darkening, debug */
	"uniform vec4 omBL[8];\n"
	"uniform vec4 omBC[8];\n"
	"uniform int omBN;\n"
	"in vec2 uv;\n"
	"in vec2 vuv;\n"
	"out vec4 outColor;\n"
	"float Dist(vec2 v)\n"
	"{\n"
	"	float z = texture(depthtex, rect.xy + v * rect.zw).r;\n"
	"	float ndc = (z - proj.z) / (proj.w - proj.z) * 2.0 - 1.0;\n"
	"	return proj.y / (ndc + proj.x);\n"
	"}\n"
	"vec3 Pos(vec2 v)\n"
	"{\n"
	"	float d = Dist(v);\n"
	"	vec2 n = v * 2.0 - 1.0;\n"
	"	return vec3(n.x * d * proj2.x, n.y * d * proj2.y, -d);\n"
	"}\n"	/* a degenerate depth neighbourhood gives no normal (a NaN here spread through the frame and its mipmaps) */
	"vec3 SafeN(vec3 c) { float l = length(c); return l > 1e-12 ? c / l : vec3(0.0, 0.0, 1.0); }\n"
	"void main()\n"
	"{\n"
	/* nothing reflected, matte and dry around here (one filtered tap covers the four the upsampling reads) */
	"	vec4 r0 = texture(reftex, vuv), m0 = texture(smtex, vuv);\n"
	"	if (r0.r + r0.g + r0.b + r0.a + m0.r + m0.b <= 0.0)\n"
	"		discard;\n"
	"	vec3 P = Pos(vuv);\n"
	"	float d = -P.z;\n"
	"	vec2 p = vuv * size.xy - 0.5;\n"
	"	vec2 f = fract(p), b = (floor(p) + 0.5) * size.zw;\n"
	"	vec2 t0 = b, t1 = b + vec2(size.z, 0.0), t2 = b + vec2(0.0, size.w), t3 = b + size.zw;\n"
	"	vec4 w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);\n"
	"	float k = 3.0 / (0.03 * d + 1.0);\n"
	"	w *= vec4(exp(-abs(Dist(t0) - d) * k), exp(-abs(Dist(t1) - d) * k), exp(-abs(Dist(t2) - d) * k), exp(-abs(Dist(t3) - d) * k));\n"
	"	float wsum = dot(w, vec4(1.0));\n"
	"	if (wsum < 0.02)\n"
	"		discard;\n"
	"	w /= wsum;\n"
	"	vec4 r = texture(reftex, t0) * w.x + texture(reftex, t1) * w.y + texture(reftex, t2) * w.z + texture(reftex, t3) * w.w;\n"
	/* blurred by as much as the trace resolution is reduced (half size: one trace pixel, the stair of the upsampling;
	   full size: none), depth-aware like the taps above */
	"	float br = max(size.z / texel.x - 1.0, 0.0);\n"
	"	if (br > 0.01)\n"
	"	{\n"
	"		vec4 acc = vec4(0.0);\n"
	"		float ws = 0.0;\n"
	"		for (int y = -1; y <= 1; y++)\n"
	"			for (int x = -1; x <= 1; x++)\n"
	"			{\n"
	"				vec2 tv = clamp(vuv + vec2(float(x), float(y)) * size.zw * br, vec2(0.0), vec2(1.0));\n"
	"				float wd = (x == 0 ? 1.0 : 0.6) * (y == 0 ? 1.0 : 0.6) * exp(-abs(Dist(tv) - d) * k);\n"
	"				acc += texture(reftex, tv) * wd;\n"
	"				ws += wd;\n"
	"			}\n"
	"		if (ws > 0.05)\n"
	"			r = acc / ws;\n"
	"	}\n"
	"	if (glint.z > 0.5)\n"
	"	{\n"
	"		outColor = vec4(r.rgb, 1.0);\n"
	"		return;\n"
	"	}\n"
	"	vec4 m = texture(smtex, t0) * w.x + texture(smtex, t1) * w.y + texture(smtex, t2) * w.z + texture(smtex, t3) * w.w;\n"
	"	float refl = m.r, wet = m.b;\n"
	"	vec3 add = r.rgb;\n"
	/* the glints where the face is really glossy (the faint sheen of tiled walls cost 1 ms at full size for nothing) */
	"	if (glint.x > 0.0 && refl > 0.06 && omBN > 0)\n"
	"	{\n"
	"		float rough = clamp(m.g / refl, 0.02, 1.0);\n"
	"		vec3 Pr = Pos(vuv + vec2(texel.x, 0.0)), Pl = Pos(vuv - vec2(texel.x, 0.0));\n"
	"		vec3 Pu = Pos(vuv + vec2(0.0, texel.y)), Pd = Pos(vuv - vec2(0.0, texel.y));\n"
	"		vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;\n"
	"		vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;\n"
	"		vec3 Nv = SafeN(cross(dx, dy));\n"
	"		if (dot(Nv, P) > 0.0) Nv = -Nv;\n"
	"		vec3 N = normalize(mat3(invview) * Nv);\n"
	"		vec3 wp = (invview * vec4(P, 1.0)).xyz;\n"
	"		vec3 V = normalize(invview[3].xyz - wp);\n"
	"		float ndv = max(dot(N, V), 0.0);\n"
	"		float n = exp2(mix(11.0, 3.0, sqrt(rough)));\n"
	"		float fr = refl + (min(1.0, refl * 3.0) - refl) * pow(1.0 - ndv, 5.0);\n"
	"		vec3 acc = vec3(0.0);\n"
	"		for (int i = 0; i < omBN; i++)\n"
	"		{\n"
	"			vec3 Lp = omBL[i].xyz - wp;\n"
	"			float dl = length(Lp);\n"
	"			float att = 1.0 - dl / omBL[i].w;\n"
	"			if (att <= 0.0) continue;\n"
	"			vec3 L = Lp / dl;\n"
	"			float ndl = dot(N, L);\n"
	"			if (ndl <= 0.0) continue;\n"
	"			acc += omBC[i].rgb * (att * att * ndl * pow(max(dot(N, normalize(L + V)), 0.0), n) * (n + 8.0) / 64.0);\n"
	"		}\n"
	"		add += min(acc, vec3(1.5)) * fr * clamp(m.a * 1.6 - 0.05, 0.0, 1.0) * glint.x;\n"
	"	}\n"
	"	float a = 1.0 - (1.0 - r.a) * (1.0 - glint.y * wet);\n"
	/* reflections, glints and the wet darkening lie under the fog overlay: fade them by its alpha */
	"	float fogk = 1.0 - omViewFog(vuv, Dist(vuv));\n"
	"	add *= fogk;\n"
	"	a *= fogk;\n"
	"	if (dot(add, vec3(1.0)) + a <= 0.002 || any(isnan(add)) || isnan(a) || any(isinf(add)))\n"
	"		discard;\n"
	"	outColor = vec4(add, a);\n"
	"}\n";
static qboolean
SSRWanted(void)
{
#ifdef YQ2_GL3_GLES3
	return false;
#else
	if (!r_om_ssr)
	{
		r_om_ssr = ri.Cvar_Get("r_om_ssr", "1", CVAR_ARCHIVE);
		r_om_ssr_wet = ri.Cvar_Get("r_om_ssr_wet", "1", CVAR_ARCHIVE);
		r_om_ssr_dist = ri.Cvar_Get("r_om_ssr_dist", "1500", CVAR_ARCHIVE);
		r_om_ssr_quality = ri.Cvar_Get("r_om_ssr_quality", "1", CVAR_ARCHIVE); /* 2 looked the same on the tour views */
		r_om_ssr_debug = ri.Cvar_Get("r_om_ssr_debug", "0", 0);
		r_om_ssr_glint = ri.Cvar_Get("r_om_ssr_glint", "1", CVAR_ARCHIVE);
		r_om_ssr_damp = ri.Cvar_Get("r_om_ssr_damp", "1", CVAR_ARCHIVE);
		r_om_ssr_smooth = ri.Cvar_Get("r_om_ssr_smooth", "32", CVAR_ARCHIVE);
		r_om_ssr_temporal = ri.Cvar_Get("r_om_ssr_temporal", "0.85", CVAR_ARCHIVE);
		r_om_ssr_ripple = ri.Cvar_Get("r_om_ssr_ripple", "1", CVAR_ARCHIVE);
		/* 0: the filled half of the checkerboard zipped along reflection edges in motion */
		r_om_ssr_checker = ri.Cvar_Get("r_om_ssr_checker", "0", CVAR_ARCHIVE);
	}

	return !SR.failed && (r_om_ssr->value > 0);
#endif
}

static void
SSRDeleteTargets(void)
{
	if (SR.colfbo) glDeleteFramebuffers(1, &SR.colfbo);
	if (SR.coltex) glDeleteTextures(1, &SR.coltex);
	if (SR.applyfbo) glDeleteFramebuffers(1, &SR.applyfbo);
	if (SR.reffbo) glDeleteFramebuffers(1, &SR.reffbo);
	if (SR.reftex) glDeleteTextures(1, &SR.reftex);
	if (SR.smfbo[0]) glDeleteFramebuffers(2, SR.smfbo);
	if (SR.smtex[0]) glDeleteTextures(2, SR.smtex);
	if (SR.histfbo[0]) glDeleteFramebuffers(2, SR.histfbo);
	if (SR.histtex[0]) glDeleteTextures(2, SR.histtex);
	SR.histfbo[0] = SR.histfbo[1] = SR.histtex[0] = SR.histtex[1] = 0;
	SR.havehist = false;
	SR.colfbo = SR.coltex = SR.applyfbo = SR.reffbo = SR.reftex = 0;
	SR.smfbo[0] = SR.smfbo[1] = SR.smtex[0] = SR.smtex[1] = 0;
	SR.havecol = false;
	SR.cw = SR.ch = SR.vw = SR.vh = 0;
	SR.colortex = 0;
}

static qboolean
SSRTargets(void)
{
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	int w = Q_max(P.vw / 2, 1), h = Q_max(P.vh / 2, 1);

	if (SR.coltex && (SR.vw == P.vw) && (SR.vh == P.vh) && (SR.colortex == P.texcolor))
	{
		return true;
	}

	SSRDeleteTargets();

	SR.coltex = NewTexture(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, w, h, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	glGenFramebuffers(1, &SR.colfbo);
	glBindFramebuffer(GL_FRAMEBUFFER, SR.colfbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, SR.coltex, 0);
	glDrawBuffers(1, one);

	if (!Complete("ssr colour"))
	{
		return false;
	}

	SR.reftex = NewTexture(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, w, h, GL_LINEAR);
	glGenFramebuffers(1, &SR.reffbo);
	glBindFramebuffer(GL_FRAMEBUFFER, SR.reffbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, SR.reftex, 0);
	glDrawBuffers(1, one);

	if (!Complete("ssr reflections"))
	{
		return false;
	}

	{
		int i;

		for (i = 0; i < 2; i++)
		{
			SR.smtex[i] = NewTexture(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, w, h, GL_LINEAR);
			glGenFramebuffers(1, &SR.smfbo[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, SR.smfbo[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, SR.smtex[i], 0);
			glDrawBuffers(1, one);

			if (!Complete("ssr material"))
			{
				return false;
			}

			SR.histtex[i] = NewTexture(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, w, h, GL_LINEAR);
			glGenFramebuffers(1, &SR.histfbo[i]);
			glBindFramebuffer(GL_FRAMEBUFFER, SR.histfbo[i]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, SR.histtex[i], 0);
			glDrawBuffers(1, one);

			if (!Complete("ssr history"))
			{
				return false;
			}
		}
	}

	/* without MSAA the scene texture is drawn through a framebuffer of its own (the material
	   texture of the scene framebuffer is read meanwhile) */
	if (!P.samples)
	{
		glGenFramebuffers(1, &SR.applyfbo);
		glBindFramebuffer(GL_FRAMEBUFFER, SR.applyfbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, P.texcolor, 0);
		glDrawBuffers(1, one);

		if (!Complete("ssr apply"))
		{
			return false;
		}
	}

	SR.cw = w;
	SR.ch = h;
	SR.vw = P.vw;
	SR.vh = P.vh;
	SR.colortex = P.texcolor;
	return true;
}

void
GL3_Post_SSR(void)
{
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	float fw = (float)P.w, fh = (float)P.h;
	qboolean nv = (r_fullbright->value != 0) || (r_newrefdef.rdflags & SOF_RDF_IRGOGGLES);
	int dbg, q;

	if (!P.bound || nv || !P.mat || !SSRWanted())
	{
		return;
	}

	if (!SR.prog)
	{
		SR.prog = LinkPost(ssr_fs);
		{
			/* the apply pass also needs the fog of the view (om_viewfog_glsl) */
			size_t la = strlen(om_viewfog_glsl), lb = strlen(ssr_apply_fs);
			char *ab = malloc(la + lb + 1);

			SR.progapply = 0;

			if (ab)
			{
				memcpy(ab, om_viewfog_glsl, la);
				memcpy(ab + la, ssr_apply_fs, lb + 1);
				SR.progapply = LinkPost(ab);
				free(ab);
			}
		}
		SR.progsmooth = LinkPost(ssr_smooth_fs);
		SR.progtemp = LinkPost(ssr_temporal_fs);

		if (!SR.prog || !SR.progapply || !SR.progsmooth || !SR.progtemp)
		{
			SR.failed = true;
			return;
		}

		SR.t_cur = glGetUniformLocation(SR.progtemp, "cur");
		SR.t_hist = glGetUniformLocation(SR.progtemp, "hist");
		SR.t_depth = glGetUniformLocation(SR.progtemp, "depthtex");
		SR.t_rect = glGetUniformLocation(SR.progtemp, "rect");
		SR.t_proj = glGetUniformLocation(SR.progtemp, "proj");
		SR.t_proj2 = glGetUniformLocation(SR.progtemp, "proj2");
		SR.t_size = glGetUniformLocation(SR.progtemp, "size");
		SR.t_reproj = glGetUniformLocation(SR.progtemp, "reproj");
		SR.t_keep = glGetUniformLocation(SR.progtemp, "keep");

		SR.a_ref = glGetUniformLocation(SR.progapply, "reftex");
		SR.a_sm = glGetUniformLocation(SR.progapply, "smtex");
		SR.a_depth = glGetUniformLocation(SR.progapply, "depthtex");
		SR.a_rect = glGetUniformLocation(SR.progapply, "rect");
		SR.a_proj = glGetUniformLocation(SR.progapply, "proj");
		SR.a_proj2 = glGetUniformLocation(SR.progapply, "proj2");
		SR.a_size = glGetUniformLocation(SR.progapply, "size");
		SR.a_texel = glGetUniformLocation(SR.progapply, "texel");
		SR.a_inv = glGetUniformLocation(SR.progapply, "invview");
		SR.a_glint = glGetUniformLocation(SR.progapply, "glint");
		SR.m_depth = glGetUniformLocation(SR.progsmooth, "depthtex");
		SR.m_src = glGetUniformLocation(SR.progsmooth, "srctex");
		SR.m_rect = glGetUniformLocation(SR.progsmooth, "rect");
		SR.m_proj = glGetUniformLocation(SR.progsmooth, "proj");
		SR.m_proj2 = glGetUniformLocation(SR.progsmooth, "proj2");
		SR.m_params = glGetUniformLocation(SR.progsmooth, "params");
		SR.m_texel = glGetUniformLocation(SR.progsmooth, "texel");

		SR.u_depth = glGetUniformLocation(SR.prog, "depthtex");
		SR.u_mat = glGetUniformLocation(SR.prog, "mattex");
		SR.u_col = glGetUniformLocation(SR.prog, "coltex");
		SR.u_rect = glGetUniformLocation(SR.prog, "rect");
		SR.u_proj = glGetUniformLocation(SR.prog, "proj");
		SR.u_proj2 = glGetUniformLocation(SR.prog, "proj2");
		SR.u_params = glGetUniformLocation(SR.prog, "params");
		SR.u_texel = glGetUniformLocation(SR.prog, "texel");
		SR.u_frame = glGetUniformLocation(SR.prog, "frame");
		SR.u_strength = glGetUniformLocation(SR.prog, "strength");
		SR.u_reproj = glGetUniformLocation(SR.prog, "reproj");
		SR.u_inv = glGetUniformLocation(SR.prog, "invview");
		SR.u_ripple = glGetUniformLocation(SR.prog, "ripple");
	}

	if (!SSRTargets())
	{
		SSRDeleteTargets();
		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		SR.failed = true;
		return;
	}

	GL3_Draw3DBatchesNow();
	GL3_FX_Flush();
	dbg = (int)r_om_ssr_debug->value;

	if (!SR.havecol)
	{
		return; /* the colour of the last frame comes at the end of this one */
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_FALSE);

	/* the finished depth (unless r_om_ssao copied it) and the material target as textures; the scene colour is
	   the last frame's (SSRKeepColour), reprojected: no copy of the unfinished view */
	if (!P.depthfinal)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.depthfbo);
		glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		P.depthfinal = true;
	}

	if (P.samples)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT2);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, P.resolvefbo[2]);
		glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, P.vx, P.vy, P.vx + P.vw, P.vy + P.vh,
			GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, P.fbo);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	}

	GL3_Post_ProfMark("ssr_blit");

	/* the material smoothed across texture seams (half size, two passes) */
	{
		float rad = Q_max(r_om_ssr_smooth->value, 0.0f);
		int i;

		GL3_UseProgram(SR.progsmooth);
		glUniform1i(SR.m_depth, 0);
		glUniform1i(SR.m_src, 1);
		glUniform4f(SR.m_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniform4f(SR.m_proj, gl3state.projMat3D.Elements[2][2], gl3state.projMat3D.Elements[3][2], gl3depthmin, gl3depthmax);
		glUniform4f(SR.m_proj2, 1.0f / gl3state.projMat3D.Elements[0][0], 1.0f / gl3state.projMat3D.Elements[1][1],
			gl3state.projMat3D.Elements[0][0], gl3state.projMat3D.Elements[1][1]);
		glUniform2f(SR.m_texel, 1.0f / P.vw, 1.0f / P.vh);
		BindTex(GL_TEXTURE0, P.texdepthcopy);
		glViewport(0, 0, SR.cw, SR.ch);

		for (i = 0; i < 2; i++)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, SR.smfbo[i]);
			glUniform4f(SR.m_params, i ? 0.0f : 1.0f, i ? 0.0f : 1.0f, i ? 1.0f : 0.0f, rad);
			BindTex(GL_TEXTURE1, i ? SR.smtex[0] : P.texmat);
			DrawQuad();
		}
	}

	GL3_Post_ProfMark("ssr_smooth");

	/* the reflections at half size (premultiplied colour, weight) */
	{
		static const float zero[4] = {0, 0, 0, 0};

		glBindFramebuffer(GL_FRAMEBUFFER, SR.reffbo);
		glViewport(0, 0, SR.cw, SR.ch);
		glClearBufferfv(GL_COLOR, 0, zero);
	}

	GL3_UseProgram(SR.prog);
	glUniform1i(SR.u_depth, 0);
	glUniform1i(SR.u_mat, 1);
	glUniform1i(SR.u_col, 2);
	glUniform4f(SR.u_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	glUniform4f(SR.u_proj, gl3state.projMat3D.Elements[2][2], gl3state.projMat3D.Elements[3][2], gl3depthmin, gl3depthmax);
	glUniform4f(SR.u_proj2, 1.0f / gl3state.projMat3D.Elements[0][0], 1.0f / gl3state.projMat3D.Elements[1][1],
		gl3state.projMat3D.Elements[0][0], gl3state.projMat3D.Elements[1][1]);
	/* 1: 16 steps, 2: 24, 3: 40 */
	q = Q_max(1, Q_min((int)r_om_ssr_quality->value, 3));
	glUniform4f(SR.u_params, Q_max(r_om_ssr_dist->value, 64.0f), (float)(q == 1 ? 16 : q == 2 ? 24 : 40),
		r_farclipdist->value * 0.98f, (float)dbg);
	/* w: r_om_ssr_checker (only with a history to fill the gaps from) */
	glUniform4f(SR.u_texel, 1.0f / P.vw, 1.0f / P.vh, (float)SR.ch,
		(r_om_ssr_checker->value && SR.havehist && !dbg && r_om_ssr_temporal->value > 0) ? 1.0f : 0.0f);
	SR.frame = (SR.frame >= 1000.0f) ? 0.0f : SR.frame + 1.0f;
	glUniform1f(SR.u_frame, SR.frame);
	glUniform1f(SR.u_strength, Q_min(r_om_ssr->value, 4.0f));
	{
		hmm_mat4 inv = ViewInverse();
		hmm_mat4 reproj = HMM_MultiplyMat4(SR.prevpv, inv);

		glUniformMatrix4fv(SR.u_reproj, 1, GL_FALSE, &reproj.Elements[0][0]);
		glUniformMatrix4fv(SR.u_inv, 1, GL_FALSE, &inv.Elements[0][0]);
		glUniform2f(SR.u_ripple, fmodf(r_newrefdef.time, 1000.0f), Q_max(0.0f, r_om_ssr_ripple->value));
	}
	BindTex(GL_TEXTURE2, SR.coltex);
	BindTex(GL_TEXTURE1, SR.smtex[1]);
	BindTex(GL_TEXTURE0, P.texdepthcopy);
	DrawQuad();

	GL3_Post_ProfMark("ssr_trace");

	/* accumulated over frames into the next history texture (its reprojection uses last frame's projection x view,
	   the same one the colour of the last frame was made with) */
	{
		hmm_mat4 reproj = HMM_MultiplyMat4(SR.prevpv, ViewInverse());
		int next = SR.histcur ^ 1;

		glBindFramebuffer(GL_FRAMEBUFFER, SR.histfbo[next]);
		GL3_UseProgram(SR.progtemp);
		glUniform1i(SR.t_cur, 0);
		glUniform1i(SR.t_hist, 1);
		glUniform1i(SR.t_depth, 2);
		glUniform4f(SR.t_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
		glUniform4f(SR.t_proj, gl3state.projMat3D.Elements[2][2], gl3state.projMat3D.Elements[3][2], gl3depthmin, gl3depthmax);
		glUniform4f(SR.t_proj2, 1.0f / gl3state.projMat3D.Elements[0][0], 1.0f / gl3state.projMat3D.Elements[1][1],
			gl3state.projMat3D.Elements[0][0], gl3state.projMat3D.Elements[1][1]);
		glUniform4f(SR.t_size, (float)SR.cw, (float)SR.ch, 1.0f / SR.cw, 1.0f / SR.ch);
		glUniformMatrix4fv(SR.t_reproj, 1, GL_FALSE, &reproj.Elements[0][0]);
		glUniform1f(SR.t_keep, (SR.havehist && !dbg) ? Q_max(0.0f, Q_min(r_om_ssr_temporal->value, 0.95f)) : 0.0f);
		BindTex(GL_TEXTURE2, P.texdepthcopy);
		BindTex(GL_TEXTURE1, SR.histtex[SR.histcur]);
		BindTex(GL_TEXTURE0, SR.reftex);
		DrawQuad();
		SR.histcur = next;
		SR.havehist = true;
	}

	GL3_Post_ProfMark("ssr_temporal");

	/* over the scene */
	if (P.samples)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		glDrawBuffers(1, one);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, SR.applyfbo);
	}

	glViewport(P.vx, P.vy, P.vw, P.vh);
	GL3_UseProgram(SR.progapply);
	ViewFogUniforms(SR.progapply);
	glUniform1i(SR.a_ref, 0);
	glUniform1i(SR.a_sm, 1);
	glUniform1i(SR.a_depth, 2);
	glUniform4f(SR.a_rect, P.vx / fw, P.vy / fh, P.vw / fw, P.vh / fh);
	glUniform4f(SR.a_proj, gl3state.projMat3D.Elements[2][2], gl3state.projMat3D.Elements[3][2], gl3depthmin, gl3depthmax);
	glUniform4f(SR.a_proj2, 1.0f / gl3state.projMat3D.Elements[0][0], 1.0f / gl3state.projMat3D.Elements[1][1],
		gl3state.projMat3D.Elements[0][0], gl3state.projMat3D.Elements[1][1]);
	glUniform4f(SR.a_size, (float)SR.cw, (float)SR.ch, 1.0f / SR.cw, 1.0f / SR.ch);
	glUniform2f(SR.a_texel, 1.0f / P.vw, 1.0f / P.vh);
	{
		hmm_mat4 inv = ViewInverse();

		glUniformMatrix4fv(SR.a_inv, 1, GL_FALSE, &inv.Elements[0][0]);
	}
	glUniform4f(SR.a_glint, Q_max(0.0f, r_om_ssr_glint->value), 0.3f,
		(float)dbg, 0);
	GL3_Lamps_View(SR.progapply, r_newrefdef.vieworg);
	BindTex(GL_TEXTURE2, P.texdepthcopy);
	BindTex(GL_TEXTURE1, SR.smtex[1]);
	BindTex(GL_TEXTURE0, SR.histtex[SR.histcur]);

	if (!dbg)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	DrawQuad();

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);

	/* back to the scene framebuffer for the rest of the view */
	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	SceneBuffers();
	glViewport(P.vx, P.vy, P.vw, P.vh);
	gl3state.currentlightmap = -1;
	GL3_SelectTMU(GL_TEXTURE7);
	glBindTexture(GL_TEXTURE_2D, P.texdepthcopy);
	GL3_SelectTMU(GL_TEXTURE0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

/* GL3_Post_EndView, the scene resolved (read framebuffer bound, attachment 0): the finished view, half size and
   mipmapped, for the reflections of the next frame */
static void
SSRKeepColour(void)
{
	if (!SR.coltex || !P.mat || (SR.vw != P.vw) || (SR.vh != P.vh) || !SSRWanted())
	{
		SR.havecol = false;
		return;
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, SR.colfbo);
	glBlitFramebuffer(P.vx, P.vy, P.vx + P.vw, P.vy + P.vh, 0, 0, SR.cw, SR.ch, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	BindTex(GL_TEXTURE0, SR.coltex);
	glGenerateMipmap(GL_TEXTURE_2D);
	SR.prevpv = HMM_MultiplyMat4(gl3state.projMat3D, gl3state.viewMat3D);
	SR.havecol = true;
}

/* the last finished frame (half size, mipmapped) for the refraction of the rain drops; 0 without r_om_ssr */
static GLuint
SSRSceneColour(void)
{
	return (SR.havecol && SR.coltex && (SR.vw == P.vw) && (SR.vh == P.vh)) ? SR.coltex : 0;
}

static void
SSRShutdown(void)

{
	SSRDeleteTargets();

	if ((gl3state.currentShaderProgram == SR.prog) || (gl3state.currentShaderProgram == SR.progapply) ||
		(gl3state.currentShaderProgram == SR.progsmooth) || (gl3state.currentShaderProgram == SR.progtemp))
	{
		gl3state.currentShaderProgram = 0;
	}

	if (SR.prog) glDeleteProgram(SR.prog);
	if (SR.progapply) { ViewFogForget(SR.progapply); glDeleteProgram(SR.progapply); }
	if (SR.progsmooth) glDeleteProgram(SR.progsmooth);
	if (SR.progtemp) glDeleteProgram(SR.progtemp);

	memset(&SR, 0, sizeof(SR));
}
/* ------------------------------------------------------------------
 * GFX-modern r_om_mirror: planar mirrors. The world faces of class 11
 * (texture name "mirror" with the glass material, gl3_material.c) note
 * themselves while drawn; of those facing the eye the nearest gives the
 * plane of the next frame. Before the world of a view is drawn, it is
 * drawn once more from the eye mirrored in that plane (view matrix x
 * reflection, face culling flipped, an oblique near plane on the mirror
 * so nothing behind it shows; the PVS of the real eye) into a window-
 * sized texture. The world shaders replace the colour of the faces on
 * that plane by the texture at their screen position. World only: no
 * models or effects in the mirror. Mirrors on other planes keep the
 * screen-space reflection. r_om_mirror 0 turns it off.
 * ------------------------------------------------------------------ */

static cvar_t *r_om_mirror, *r_om_mirror_scale;

#define MIRROR_PLANES 2     /* planes drawn per frame (the tsr1 restroom: the wall over the sinks and a side wall) */
#define MIRROR_CANDS 8

typedef struct
{
	float plane[4];         /* normal towards the eye, dist */
	float score;            /* how much of the view the mirror faces on this plane take: area x facing / distance^2 */
} mirrorcand_t;

static struct
{
	qboolean failed, inpass;
	int w, h;
	GLuint tex[MIRROR_PLANES], fbo[MIRROR_PLANES], rbdepth;
	/* noted this frame (by the world walk) / drawn this frame */
	mirrorcand_t cand[MIRROR_CANDS];
	int numcand, candframe;
	float plane[MIRROR_PLANES][4];
	int numplanes;
} MR;

extern vec3_t gl3_origin;

static qboolean
SamePlane(const float *a, const float *b)
{
	return (DotProduct(a, b) > 0.999f) && (fabsf(a[3] - b[3]) < 1.0f);
}

/* a world face of a mirror is drawn (gl3_surf.c): its share of the view goes to the score of its plane */
void
GL3_Mirror_Note(const msurface_t *surf)
{
	float pl[4], side, dist2, area = 0, score;
	vec3_t c = {0, 0, 0}, e1, e2, x;
	const glpoly_t *p = surf->polys;
	int i;

	if (MR.inpass || !p || !surf->plane || !r_om_mirror || !r_om_mirror->value || p->numverts < 3)
	{
		return;
	}

	VectorCopy(surf->plane->normal, pl);
	pl[3] = surf->plane->dist;

	if (surf->flags & SURF_PLANEBACK)
	{
		VectorNegate(pl, pl);
		pl[3] = -pl[3];
	}

	side = DotProduct(pl, r_newrefdef.vieworg) - pl[3];

	if (side < 2)
	{
		return;
	}

	for (i = 0; i < p->numverts; i++)
	{
		VectorAdd(c, p->vertices[i].pos, c);
	}

	VectorScale(c, 1.0f / p->numverts, c);

	for (i = 1; i + 1 < p->numverts; i++)
	{
		VectorSubtract(p->vertices[i].pos, p->vertices[0].pos, e1);
		VectorSubtract(p->vertices[i + 1].pos, p->vertices[0].pos, e2);
		CrossProduct(e1, e2, x);
		area += 0.5f * VectorLength(x);
	}

	VectorSubtract(c, r_newrefdef.vieworg, c);
	dist2 = Q_max(DotProduct(c, c), 1.0f);
	/* solid angle of the face, roughly: area x cos(view angle) / distance^2 */
	score = area * (side / sqrtf(dist2)) / dist2;

	for (i = 0; i < MR.numcand; i++)
	{
		if (SamePlane(MR.cand[i].plane, pl))
		{
			MR.cand[i].score += score;
			return;
		}
	}

	if (MR.numcand < MIRROR_CANDS)
	{
		memcpy(MR.cand[MR.numcand].plane, pl, sizeof(pl));
		MR.cand[MR.numcand].score = score;
		MR.numcand++;
	}
}

static void
MirrorDeleteTargets(void)
{
	int k;

	for (k = 0; k < MIRROR_PLANES; k++)
	{
		if (MR.fbo[k]) glDeleteFramebuffers(1, &MR.fbo[k]);
		if (MR.tex[k]) glDeleteTextures(1, &MR.tex[k]);
		MR.fbo[k] = MR.tex[k] = 0;
	}

	if (MR.rbdepth) glDeleteRenderbuffers(1, &MR.rbdepth);
	MR.rbdepth = 0;
}

/* vid_restart: the mirror targets are names of the old context (with the same window size they were used
   again; with another size the deletes hit new objects of the new context) */
static void
MirrorShutdown(void)
{
	MirrorDeleteTargets();
	MR.w = MR.h = 0;
	MR.failed = false;
}

static qboolean
MirrorTargets(void)
{
	static const GLenum one[1] = {GL_COLOR_ATTACHMENT0};
	int k;
	/* r_om_mirror_scale: the mirror images at a share of the window size (0.7: about half the pixels) */
	float s = Q_clamp(r_om_mirror_scale->value, 0.25f, 1.0f);
	int w = Q_max((int)(vid.width * s), 16), h = Q_max((int)(vid.height * s), 16);

	if (MR.fbo[0] && (MR.w == w) && (MR.h == h))
	{
		return true;
	}

	MirrorDeleteTargets();
	MR.w = w;
	MR.h = h;
	/* one depth buffer: the passes run one after the other */
	MR.rbdepth = NewRenderbuffer(GL_DEPTH24_STENCIL8, MR.w, MR.h, 0);

	for (k = 0; k < MIRROR_PLANES; k++)
	{
		MR.tex[k] = NewTexture(OM_RT_FMT, GL_RGBA, OM_RT_TYPE, MR.w, MR.h, GL_LINEAR);
		glGenFramebuffers(1, &MR.fbo[k]);
		glBindFramebuffer(GL_FRAMEBUFFER, MR.fbo[k]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, MR.tex[k], 0);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, MR.rbdepth);
		glDrawBuffers(1, one);

		if (!Complete("mirror"))
		{
			return false;
		}
	}

	return true;
}

/* the inverse of a rigid (possibly mirrored) view matrix: transposed 3x3, -R^T t */
static hmm_mat4
RigidInverse(const hmm_mat4 *v)
{
	hmm_mat4 inv;
	int r, c;

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
	return inv;
}

/* the world (and the opaque brush entities) seen in the plane n.x = d, into framebuffer fbo; false when the view
   cannot be built */
static qboolean
MirrorPass(const float *n, float d, GLuint fbo)
{
	hmm_mat4 refl, saveview = gl3state.viewMat3D, saveproj = gl3state.projMat3D;
	hmm_mat4 savepv = gl3state.uni3DData.transProjViewMat4, view, inv, proj;
	cplane_t savefrustum[4];
	vec3_t saveorg, saveglorg, savevpn, savevright, savevup, e;
	float cw[4], cv[4], q[4], k, dot;
	float zfar = (gl3depthmin > gl3depthmax) ? 0.0f : 1.0f;
	int i, j;
	extern vec3_t vup, vpn, vright;

	/* reflection in the plane n.x = d */
	memset(&refl, 0, sizeof(refl));
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
		{
			refl.Elements[j][i] = (i == j ? 1.0f : 0.0f) - 2 * n[i] * n[j];
		}
		refl.Elements[3][i] = 2 * d * n[i];
	}
	refl.Elements[3][3] = 1;
	view = HMM_MultiplyMat4(gl3state.viewMat3D, refl);

	/* oblique near plane on the mirror (Lengyel): keep what is on the eye's side */
	cw[0] = n[0]; cw[1] = n[1]; cw[2] = n[2]; cw[3] = -d;
	inv = RigidInverse(&view);
	for (i = 0; i < 4; i++)
	{
		cv[i] = inv.Elements[i][0] * cw[0] + inv.Elements[i][1] * cw[1] + inv.Elements[i][2] * cw[2] +
			inv.Elements[i][3] * cw[3];
	}
	cv[3] -= 0.5f; /* a little in front of the glass: the mirror face itself is not drawn into its image */
	proj = gl3state.projMat3D;
	q[0] = ((cv[0] > 0 ? 1.0f : cv[0] < 0 ? -1.0f : 0.0f) + proj.Elements[2][0]) / proj.Elements[0][0];
	q[1] = ((cv[1] > 0 ? 1.0f : cv[1] < 0 ? -1.0f : 0.0f) + proj.Elements[2][1]) / proj.Elements[1][1];
	q[2] = -1.0f;
	q[3] = (1.0f + proj.Elements[2][2]) / proj.Elements[3][2];
	dot = cv[0] * q[0] + cv[1] * q[1] + cv[2] * q[2] + cv[3] * q[3];
	if (fabsf(dot) < 1e-8f)
	{
		return false;
	}
	k = 2.0f / dot;
	proj.Elements[0][2] = cv[0] * k;
	proj.Elements[1][2] = cv[1] * k;
	proj.Elements[2][2] = cv[2] * k + 1.0f;
	proj.Elements[3][2] = cv[3] * k;

	/* the mirrored eye for the world walk (face sides, culling) */
	VectorCopy(r_newrefdef.vieworg, saveorg);
	VectorCopy(gl3_origin, saveglorg);
	VectorCopy(vpn, savevpn);
	VectorCopy(vright, savevright);
	VectorCopy(vup, savevup);
	memcpy(savefrustum, frustum, sizeof(savefrustum));

	VectorMA(r_newrefdef.vieworg, -2 * (DotProduct(n, r_newrefdef.vieworg) - d), n, e);
	VectorCopy(e, r_newrefdef.vieworg);
	VectorCopy(e, gl3_origin);
	VectorMA(vpn, -2 * DotProduct(n, vpn), n, vpn);
	VectorMA(vright, -2 * DotProduct(n, vright), n, vright);
	VectorMA(vup, -2 * DotProduct(n, vup), n, vup);
	R_SetFrustum(vup, vpn, vright, gl3_origin, r_newrefdef.fov_x, r_newrefdef.fov_y, frustum);

	gl3state.viewMat3D = view;
	gl3state.projMat3D = proj;
	gl3state.uni3DData.transProjViewMat4 = HMM_MultiplyMat4(proj, view);
	GL3_UpdateUBO3D();

	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	gl3_drawsamples = 0; /* the mirror textures are single-sampled */
	/* the view's rectangle at the mirror texture's scale */
	{
		float sx = (float)MR.w / vid.width, sy = (float)MR.h / vid.height;

		glViewport((int)(P.vx * sx), (int)(P.vy * sy), (int)(P.vw * sx), (int)(P.vh * sy));
	}
	{
		float black[4] = {0, 0, 0, 1};

		glClearBufferfv(GL_COLOR, 0, black);
		glDepthMask(GL_TRUE);
		glClearBufferfi(GL_DEPTH_STENCIL, 0, zfar, 0);
	}
	glCullFace(GL_BACK);

	MR.inpass = true;
	GL3_FX_BeginFrame3D();
	GL3_Fog_BeginFrame();
	GL3_DrawWorld();

	/* the brush entities too (doors, func_walls, lifts): without them the mirror shows a black hole where the door
	   stands */
	if (r_drawentities->value)
	{
		int en;

		glDepthMask(GL_TRUE);

		for (en = 0; en < r_newrefdef.num_entities; en++)
		{
			entity_t *ent = &r_newrefdef.entities[en];

			if (ent->model && !(ent->flags & (SOF_RF_GHOUL | SOF_RF_WEAPONMODEL | RF_TRANSLUCENT)) &&
				(((gl3model_t *)ent->model)->type == mod_brush))
			{
				GL3_DrawBrushModel(ent, (gl3model_t *)ent->model);
			}
		}

		GL3_FX_Flush();

		/* the models too: characters, the player's own body and props (the mirrored view turns the winding round) */
		{
			extern qboolean gl3_ghoul_mirror;

			gl3_ghoul_mirror = true;
			glFrontFace(GL_CW);
			GL3_FX_DrawGhoulMirrored();
			glFrontFace(GL_CCW);
			gl3_ghoul_mirror = false;
			glDisable(GL_BLEND);
			glDepthMask(GL_TRUE);
			glEnable(GL_DEPTH_TEST);
		}
	}

	GL3_Draw3DBatchesNow();
	MR.inpass = false;

	/* back to the real eye; what the walk queued for the sorted and fog passes is dropped */
	glCullFace(GL_FRONT);
	VectorCopy(saveorg, r_newrefdef.vieworg);
	VectorCopy(saveglorg, gl3_origin);
	VectorCopy(savevpn, vpn);
	VectorCopy(savevright, vright);
	VectorCopy(savevup, vup);
	memcpy(frustum, savefrustum, sizeof(savefrustum));
	gl3state.viewMat3D = saveview;
	gl3state.projMat3D = saveproj;
	gl3state.uni3DData.transProjViewMat4 = savepv;
	GL3_UpdateUBO3D();
	GL3_FX_BeginFrame3D();
	GL3_Fog_BeginFrame();
	return true;
}

/* before the world of the view (gl3_main.c): the mirror images of the world in the planes that took the most of
   the last frame's view. Choosing the plane of the nearest mirror face made the mirrors over the tsr1 sinks go
   blank as soon as the player stepped closer to a mirror on a side wall. */
void
GL3_Mirror_Render(void)
{
	float on[4] = {0, 0, 0, 0}, planes[MIRROR_PLANES][4], rect[4] = {0, 0, 0, 0};
	float prev[MIRROR_PLANES][4];
	int numprev = MR.numplanes, i, k, n = 0;
	mirrorcand_t cand[MIRROR_CANDS];
	int numcand = (MR.candframe == 1) ? MR.numcand : 0;

	if (!r_om_mirror)
	{
		r_om_mirror = ri.Cvar_Get("r_om_mirror", "1", CVAR_ARCHIVE);
		r_om_mirror_scale = ri.Cvar_Get("r_om_mirror_scale", "0.7", CVAR_ARCHIVE);
	}

	memset(planes, 0, sizeof(planes));
	memcpy(prev, MR.plane, sizeof(prev));
	memcpy(cand, MR.cand, sizeof(cand));
	MR.numcand = 0;
	MR.candframe = 1;
	MR.numplanes = 0;

	/* a plane drawn last frame keeps its place unless another takes clearly more of the view (no flicker between
	   two mirrors of about the same size) */
	for (i = 0; i < numcand; i++)
	{
		for (k = 0; k < numprev; k++)
		{
			if (SamePlane(cand[i].plane, prev[k]))
			{
				cand[i].score *= 1.5f;
			}
		}
	}

	/* the best planes the eye is in front of */
	while (n < MIRROR_PLANES)
	{
		int best = -1;

		for (i = 0; i < numcand; i++)
		{
			if (cand[i].score > 0 && DotProduct(cand[i].plane, r_newrefdef.vieworg) - cand[i].plane[3] >= 2 &&
				(best < 0 || cand[i].score > cand[best].score))
			{
				best = i;
			}
		}

		if (best < 0)
		{
			break;
		}

		memcpy(MR.plane[n], cand[best].plane, sizeof(MR.plane[n]));
		cand[best].score = 0;
		n++;
	}

	if (MR.failed || !n || !r_om_mirror->value || !P.bound || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		GL3_SoF_MirrorUniforms(on, planes[0], planes[1], rect, 0, 0);
		return;
	}

	if (!MirrorTargets())
	{
		MR.failed = true;
		glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
		gl3_drawsamples = P.samples;
		GL3_SoF_MirrorUniforms(on, planes[0], planes[1], rect, 0, 0);
		return;
	}

	GL3_Draw3DBatchesNow();
	MR.numplanes = n;
	/* no mirror replacement inside the mirror passes */
	GL3_SoF_MirrorUniforms(on, planes[0], planes[1], rect, 0, 0);

	for (k = 0; k < n; k++)
	{
		memcpy(planes[k], MR.plane[k], sizeof(planes[0]));
		on[k] = MirrorPass(MR.plane[k], MR.plane[k][3], MR.fbo[k]) ? 1.0f : 0.0f;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, P.fbo);
	gl3_drawsamples = P.samples;
	SceneBuffers();
	glViewport(P.vx, P.vy, P.vw, P.vh);
	gl3state.currentlightmap = -1;
	gl3state.currenttexture = -1;

	/* sampled by window position: 1/window size covers the whole (smaller) texture */
	rect[0] = 1.0f / vid.width;
	rect[1] = 1.0f / vid.height;
	rect[2] = Q_max(0.0f, Q_min(r_om_mirror->value, 1.0f));
	GL3_SoF_MirrorUniforms(on, planes[0], planes[1], rect, MR.tex[0], MR.tex[1]);
}

/* a new map: no plane from the old one */
void
GL3_Mirror_MapLoad(void)
{
	MR.numcand = MR.numplanes = 0;
	MR.candframe = 0;
}

/* a new map is loading: the volumetric fog program is compiled now, not when the first fog comes into view (a
   50 ms stall in the middle of kos1) */
void
GL3_Post_MapLoad(void)
{
	VolFogCvars();

	if (r_om_volfog->value)
	{
		VolFogProgram();
	}
}
