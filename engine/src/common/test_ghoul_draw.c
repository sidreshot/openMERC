#define _GNU_SOURCE
/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Draw the desktop Ghoul program. omRFTab (sampler2D) and omLsRaw
 * (sampler2DArray) on one unit is GL_INVALID_OPERATION. The unit from
 * OM_ShadeGhoulRawUnit, with a matching texture on that unit, draws clean.
 * Exit code is the failure count.
 */

#define GL_GLEXT_PROTOTYPES 1
#include "header/om_shade.h"

#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OM_REF_GL3_DIR
#define OM_REF_GL3_DIR "CMakeFiles/ref_gl3.dir"
#endif

static int failures;

static void
expect_true(const char *name, int cond)
{
	if (cond)
	{
		return;
	}

	fprintf(stderr, "FAIL %s\n", name);
	failures++;
}

static char *
readbin(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	long len;
	char *s;

	if (!f)
	{
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	rewind(f);

	if (len < 0)
	{
		fclose(f);
		return NULL;
	}

	s = malloc((size_t)len + 1);

	if (!s)
	{
		fclose(f);
		return NULL;
	}

	if (fread(s, 1, (size_t)len, f) != (size_t)len)
	{
		free(s);
		fclose(f);
		return NULL;
	}

	s[len] = 0;
	*n = (size_t)len;
	fclose(f);
	return s;
}

static char *
grab(const char *blob, size_t n, const char *anchor, const char *must)
{
	const char *p = blob;
	size_t al = strlen(anchor), ml = strlen(must);

	while (p && ((size_t)(p - blob) < n))
	{
		const char *hit = memmem(p, n - (size_t)(p - blob), anchor, al);
		const char *end;
		size_t len;

		if (!hit)
		{
			return NULL;
		}

		end = memchr(hit, 0, n - (size_t)(hit - blob));

		if (!end)
		{
			return NULL;
		}

		len = (size_t)(end - hit);

		if ((hit == blob || hit[-1] == 0) && memmem(hit, len, must, ml))
		{
			char *s = malloc(len + 1);

			if (!s)
			{
				return NULL;
			}

			memcpy(s, hit, len);
			s[len] = 0;
			return s;
		}

		p = hit + 1;
	}

	return NULL;
}

static char *
join2(const char *a, const char *b)
{
	size_t na = strlen(a), nb = b ? strlen(b) : 0;
	char *s = malloc(na + nb + 1);

	if (!s)
	{
		return NULL;
	}

	memcpy(s, a, na);

	if (b)
	{
		memcpy(s + na, b, nb);
	}

	s[na + nb] = 0;
	return s;
}

static char *
wrap_fragment(char *src)
{
	char *m = strstr(src, "void main()");
	const char *decl = "out vec4 outEmissive;\nout vec4 outMaterial;\n";
	const char *tail =
		"\nvoid main()\n{\n"
		"\toutEmissive = vec4(0.0, 0.0, 0.0, -1.0);\n"
		"\toutMaterial = vec4(0.0, 0.0, 0.0, -1.0);\n"
		"\tomMn();\n"
		"\toutColor = vec4(clamp(outColor.rgb, 0.0, 64.0), clamp(outColor.a, 0.0, 1.0));\n"
		"\tif (outEmissive.a < 0.0) outEmissive = vec4(0.0, 0.0, 0.0, outColor.a);\n"
		"\tif (outMaterial.a < 0.0) outMaterial = vec4(0.0, 0.0, 0.0, outColor.a);\n"
		"}\n";
	char *out;

	if (!m || strstr(m + 1, "void main()") || !strstr(src, "outColor"))
	{
		return src;
	}

	memcpy(m, "void omMn()", 11);
	out = malloc(strlen(decl) + strlen(src) + strlen(tail) + 1);

	if (!out)
	{
		return src;
	}

	strcpy(out, decl);
	strcat(out, src);
	strcat(out, tail);
	free(src);
	return out;
}

static int
make_context(void)
{
	static const EGLint cfgattr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	static const EGLint pbuf[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
	EGLint maj = 0, min = 0, n = 0;
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint attr[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE
	};

	if ((dpy == EGL_NO_DISPLAY) || !eglInitialize(dpy, &maj, &min))
	{
		fprintf(stderr, "FAIL eglInitialize\n");
		return 0;
	}

	if (!eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(dpy, cfgattr, &cfg, 1, &n) || (n < 1))
	{
		fprintf(stderr, "FAIL egl config 0x%x\n", eglGetError());
		return 0;
	}

	surf = eglCreatePbufferSurface(dpy, cfg, pbuf);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attr);

	if ((surf == EGL_NO_SURFACE) || (ctx == EGL_NO_CONTEXT) || !eglMakeCurrent(dpy, surf, surf, ctx))
	{
		fprintf(stderr, "FAIL context 0x%x\n", eglGetError());
		return 0;
	}

	printf("GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
	return 1;
}

static GLuint
compile_one(GLenum type, const char *src, const char *label)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0, len = 0;
	char *info;

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
	info = calloc((size_t)len + 1, 1);

	if (info && (len > 1))
	{
		glGetShaderInfoLog(s, len, NULL, info);
	}

	if (!ok)
	{
		fprintf(stderr, "FAIL compile %s: %s\n", label, info ? info : "");
		free(info);
		glDeleteShader(s);
		return 0;
	}

	free(info);
	return s;
}

static int
is_sampler(GLenum t)
{
	switch (t)
	{
		case GL_SAMPLER_2D:
		case GL_SAMPLER_2D_ARRAY:
		case GL_SAMPLER_2D_ARRAY_SHADOW:
		case GL_SAMPLER_CUBE:
		case GL_SAMPLER_BUFFER:
			return 1;
		default:
			return 0;
	}
}

static GLuint tex2d, texarr, texarrs, texcube, texbuf;

static void
make_textures(void)
{
	GLuint buf;
	unsigned char px[4] = {255, 255, 255, 255};
	float one = 1.0f;
	int face;

	glGenTextures(1, &tex2d);
	glBindTexture(GL_TEXTURE_2D, tex2d);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &texarr);
	glBindTexture(GL_TEXTURE_2D_ARRAY, texarr);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, 1, 1, 1, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

	glGenTextures(1, &texarrs);
	glBindTexture(GL_TEXTURE_2D_ARRAY, texarrs);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, 1, 1, 1, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

	glGenTextures(1, &texcube);
	glBindTexture(GL_TEXTURE_CUBE_MAP, texcube);

	for (face = 0; face < 6; face++)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenBuffers(1, &buf);
	glBindBuffer(GL_TEXTURE_BUFFER, buf);
	glBufferData(GL_TEXTURE_BUFFER, sizeof(one), &one, GL_STATIC_DRAW);
	glGenTextures(1, &texbuf);
	glBindTexture(GL_TEXTURE_BUFFER, texbuf);
	glTexBuffer(GL_TEXTURE_BUFFER, GL_R32F, buf);
}

static void
bind_unit(int unit, GLenum type)
{
	glActiveTexture(GL_TEXTURE0 + unit);

	if (type == GL_SAMPLER_2D)
	{
		glBindTexture(GL_TEXTURE_2D, tex2d);
	}
	else if (type == GL_SAMPLER_2D_ARRAY)
	{
		glBindTexture(GL_TEXTURE_2D_ARRAY, texarr);
	}
	else if (type == GL_SAMPLER_2D_ARRAY_SHADOW)
	{
		glBindTexture(GL_TEXTURE_2D_ARRAY, texarrs);
	}
	else if (type == GL_SAMPLER_CUBE)
	{
		glBindTexture(GL_TEXTURE_CUBE_MAP, texcube);
	}
	else if (type == GL_SAMPLER_BUFFER)
	{
		glBindTexture(GL_TEXTURE_BUFFER, texbuf);
	}
}

typedef struct
{
	GLint loc;
	GLenum type;
	char name[80];
	int unit;
} samp_t;

/* Units the Ghoul program is given: tex 0, soft depth 7, sun compare 8,
   lamp compare from OM_ShadeLampCompareUnit, fog table 17, sun raw 18,
   view map 19, tessellation buffer 20, probes 24..27, lamp raw on the
   shipped Ghoul unit. */
static int
engine_unit(const char *name, int maxu, int ghoul_raw)
{
	if (strcmp(name, "tex") == 0)
	{
		return 0;
	}

	if (strcmp(name, "omSoftAddDepth") == 0)
	{
		return 7;
	}

	if (strcmp(name, "omCsmMap") == 0)
	{
		return 8;
	}

	if (strcmp(name, "omLsMap") == 0)
	{
		return OM_ShadeLampCompareUnit(maxu, 0);
	}

	if (strcmp(name, "omRFTab") == 0)
	{
		return 17;
	}

	if (strcmp(name, "omCsmRaw") == 0)
	{
		return 18;
	}

	if (strcmp(name, "omVmMap") == 0)
	{
		return 19;
	}

	if (strcmp(name, "omTessBuf") == 0)
	{
		return 20;
	}

	if (strcmp(name, "omLsRaw") == 0)
	{
		return ghoul_raw;
	}

	if (strcmp(name, "pc0") == 0)
	{
		return 24;
	}

	if (strcmp(name, "pc1") == 0)
	{
		return 25;
	}

	if (strcmp(name, "pc2") == 0)
	{
		return 26;
	}

	if (strcmp(name, "pc3") == 0)
	{
		return 27;
	}

	return -1;
}

static int
draw_error(GLuint prog, samp_t *s, int ns)
{
	GLuint vao = 0, vbo = 0;
	float pos[12] = {-0.5f, -0.5f, 0.0f, 1.0f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f, 1.0f};
	GLenum err;
	int i;

	glUseProgram(prog);

	for (i = 0; i < ns; i++)
	{
		glUniform1i(s[i].loc, s[i].unit);
		bind_unit(s[i].unit, s[i].type);
	}

	glActiveTexture(GL_TEXTURE0);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(pos), pos, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, 0);

	while (glGetError() != GL_NO_ERROR)
	{
	}

	glDrawArrays(GL_TRIANGLES, 0, 3);
	err = glGetError();
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
	return (int)err;
}

int
main(void)
{
	char path[512];
	size_t ng = 0, nc = 0, nl = 0;
	char *ghoul, *csm, *lamp, *vs, *fs, *fog, *shade, *body, *wrapped, *fsrc, *vsrc;
	GLuint sv, sf, prog;
	GLint ok = 0, nuni = 0, maxu = 0;
	samp_t samps[64];
	int ns = 0, i, cursor, shared, ghoul_raw, saw_raw = 0, saw_fog = 0;
	int conflict, fixed;

	snprintf(path, sizeof(path), "%s/src/client/refresh/gl3/gl3_ghoulemu.c.o", OM_REF_GL3_DIR);
	ghoul = readbin(path, &ng);
	snprintf(path, sizeof(path), "%s/src/client/refresh/gl3/gl3_csm.c.o", OM_REF_GL3_DIR);
	csm = readbin(path, &nc);
	snprintf(path, sizeof(path), "%s/src/client/refresh/gl3/gl3_lshadow.c.o", OM_REF_GL3_DIR);
	lamp = readbin(path, &nl);

	if (!ghoul || !csm || !lamp)
	{
		fprintf(stderr, "FAIL missing renderer objects under %s\n", OM_REF_GL3_DIR);
		return 1;
	}

	vs = grab(ghoul, ng, "#define OM_TESS 1\nin vec4 position;\n", "gl_Position");
	fs = grab(ghoul, ng, "uniform sampler2D tex;\nuniform int texenv;\n", "omCsmModelVis");
	fog = grab(ghoul, ng, "uniform highp sampler2D omRFTab;\n", "omRegionFog");
	{
		char *csm_s = grab(csm, nc, "layout (std140) uniform uniOMcsm\n", "omCsmCascade");
		char *ls_s = grab(lamp, nl, "layout (std140) uniform uniOMls\n", "omLsSlotVis");

		free(csm);
		free(lamp);
		free(ghoul);
		csm = csm_s;
		lamp = ls_s;
	}

	shade = (char *)OM_Shade_Glsl();

	if (!vs || !fs || !fog || !csm || !lamp || !shade || !strstr(fs, "Kaplanyan") || !strstr(fs, "omLsSlotVis"))
	{
		fprintf(stderr, "FAIL shader pieces vs=%p fs=%p fog=%p csm=%p ls=%p\n",
			(void *)vs, (void *)fs, (void *)fog, (void *)csm, (void *)lamp);
		return 1;
	}

	body = join2(shade, csm);
	{
		char *t = join2(body, lamp);
		free(body);
		body = t;
	}
	{
		char *t = join2(body, fs);
		free(body);
		body = t;
	}
	{
		char *t = join2(fog, body);
		free(body);
		body = t;
	}

	wrapped = wrap_fragment(body);
	fsrc = join2("#version 150\n", wrapped);
	vsrc = join2("#version 150\n", vs);

	if (!make_context() || !fsrc || !vsrc)
	{
		fprintf(stderr, "FAIL setup\n");
		return 1;
	}

	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxu);
	shared = OM_ShadeLampRawUnit(maxu, 0);
	ghoul_raw = OM_ShadeGhoulRawUnit(maxu, shared);
	printf("units max=%d lamp_raw=%d ghoul_raw=%d\n", maxu, shared, ghoul_raw);
	expect_true("ghoul raw leaves the fog unit", ghoul_raw != shared);
	expect_true("ghoul raw is inside the GPU", (ghoul_raw >= 0) && (ghoul_raw < maxu));

	sv = compile_one(GL_VERTEX_SHADER, vsrc, "vertex");
	sf = compile_one(GL_FRAGMENT_SHADER, fsrc, "fragment");

	if (!sv || !sf)
	{
		return 1;
	}

	prog = glCreateProgram();
	glAttachShader(prog, sv);
	glAttachShader(prog, sf);
	glBindAttribLocation(prog, 0, "position");
	glBindAttribLocation(prog, 1, "texCoord");
	glBindAttribLocation(prog, 2, "fogFactor");
	glBindAttribLocation(prog, 3, "vertColor");
	glBindAttribLocation(prog, 4, "eyeNormal");
	glBindAttribLocation(prog, 5, "eyePos");
	glBindFragDataLocation(prog, 0, "outColor");
	glBindFragDataLocation(prog, 1, "outEmissive");
	glBindFragDataLocation(prog, 2, "outMaterial");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);

	if (!ok)
	{
		char log[2048];

		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		fprintf(stderr, "FAIL link %s\n", log);
		return 1;
	}

	glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &nuni);
	make_textures();

	for (i = 0; i < nuni && ns < 64; i++)
	{
		GLsizei len = 0;
		GLint size = 0;
		GLenum type = 0;

		glGetActiveUniform(prog, (GLuint)i, (GLsizei)sizeof(samps[ns].name), &len, &size, &type, samps[ns].name);

		if (!is_sampler(type))
		{
			continue;
		}

		samps[ns].loc = glGetUniformLocation(prog, samps[ns].name);
		samps[ns].type = type;
		samps[ns].unit = -1;

		if (strcmp(samps[ns].name, "omLsRaw") == 0)
		{
			saw_raw = 1;
		}

		if (strcmp(samps[ns].name, "omRFTab") == 0)
		{
			saw_fog = 1;
		}

		ns++;
	}

	printf("active samplers %d omLsRaw %d omRFTab %d\n", ns, saw_raw, saw_fog);
	expect_true("program samples omLsRaw", saw_raw);
	expect_true("program samples omRFTab", saw_fog);

	/* The bug: both on the desktop raw unit. */
	cursor = 0;

	for (i = 0; i < ns; i++)
	{
		if ((strcmp(samps[i].name, "omLsRaw") == 0) || (strcmp(samps[i].name, "omRFTab") == 0))
		{
			samps[i].unit = shared;
			continue;
		}

		while ((cursor == shared) || (cursor == ghoul_raw))
		{
			cursor++;
		}

		samps[i].unit = cursor++;
	}

	conflict = draw_error(prog, samps, ns);
	printf("draw shared unit %d error 0x%x\n", shared, conflict);
	expect_true("shared unit is GL_INVALID_OPERATION", conflict == 0x502);

	cursor = 0;

	for (i = 0; i < ns; i++)
	{
		if (strcmp(samps[i].name, "omLsRaw") == 0)
		{
			samps[i].unit = ghoul_raw;
			continue;
		}

		while (cursor == ghoul_raw)
		{
			cursor++;
		}

		samps[i].unit = cursor++;
	}

	fixed = draw_error(prog, samps, ns);
	printf("draw ghoul raw unit %d error 0x%x\n", ghoul_raw, fixed);
	expect_true("ghoul raw unit draws", fixed == 0);

	/* The units CreateObjects, BindProgram, the sun samplers and the probes
	   actually leave on this program. omRFTab stays on 17. omLsRaw is the
	   shipped Ghoul unit, with the raw depth array bound there. */
	for (i = 0; i < ns; i++)
	{
		int u = engine_unit(samps[i].name, maxu, ghoul_raw);
		int k;

		printf("engine %s type 0x%x unit %d\n", samps[i].name, samps[i].type, u);
		expect_true("engine unit known", u >= 0);
		expect_true("engine unit inside the GPU", (u >= 0) && (u < maxu));
		samps[i].unit = u;

		for (k = 0; k < i; k++)
		{
			if ((samps[k].unit == u) && (samps[k].type != samps[i].type))
			{
				fprintf(stderr, "FAIL type clash %s and %s on unit %d\n", samps[k].name, samps[i].name, u);
				failures++;
			}
		}
	}

	expect_true("engine map keeps omLsRaw off the fog unit", ghoul_raw != 17);
	fixed = draw_error(prog, samps, ns);
	printf("draw engine map error 0x%x\n", fixed);
	expect_true("engine map draws", fixed == 0);

	printf("ghoul_draw_test failures=%d\n", failures);
	return failures ? 1 : 0;
}
