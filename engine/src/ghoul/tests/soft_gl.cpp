// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - a small software implementation of the 52-entry GL table: matrix stacks,
// immediate-mode triangles, textures (RGB/RGBA/ALPHA, nearest), depth buffer, alpha test, blending, culling.
// Also records the triangles it receives (world space when the view matrix is the identity) for exports.
// Implements: SPEC-010 G (test double of the renderer's GL entry points)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "../ghoul_gl.h"
#include "test_common.h"

using namespace ghoul;

struct SoftTex
{
	int w = 0, h = 0, comps = 4;
	std::vector<uint8_t> rgba;
	bool clampS = false, clampT = false;
};

struct SoftVert
{
	float clip[4];
	float world[3];
	float col[4];
	float uv[2];
	float n[3];
};

struct SoftGL
{
	int W, H;
	Image img;
	std::vector<float> depth;
	// matrices: column-major
	std::vector<std::array<float, 16>> stack[3];
	int mode = 0;
	std::map<unsigned, bool> caps;
	unsigned blendS = gl::SRC_ALPHA, blendD = gl::ONE_MINUS_SRC_ALPHA, cull = gl::BACK, alphaFunc = gl::GREATER;
	float alphaRef = 0;
	bool depthMask = true;
	float col[4] = {1, 1, 1, 1}, uv[2] = {0, 0}, nrm[3] = {0, 0, 1};
	unsigned bound = 0, nextName = 1;
	std::map<unsigned, SoftTex> tex;
	std::vector<SoftVert> verts;
	bool inBegin = false;
	int tris = 0, texturedTris = 0;
	// capture
	bool capture = false;
	std::vector<PosedTri> captured;
};

static SoftGL *g_s = nullptr;

static std::array<float, 16> Ident()
{
	std::array<float, 16> m{};
	m[0] = m[5] = m[10] = m[15] = 1;
	return m;
}

static std::array<float, 16> Mul(const std::array<float, 16> &a, const std::array<float, 16> &b)
{
	std::array<float, 16> r{};
	for (int c = 0; c < 4; c++)
		for (int rr = 0; rr < 4; rr++)
		{
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += a[k * 4 + rr] * b[c * 4 + k];
			r[c * 4 + rr] = s;
		}
	return r;
}

static std::array<float, 16> &Top() { return g_s->stack[g_s->mode].back(); }

static void Xform(const std::array<float, 16> &m, const float in[4], float out[4])
{
	for (int r = 0; r < 4; r++)
		out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r] * in[3];
}

// ------------------------------------------------------------------ rasteriser

static void Fragment(int x, int y, float z, const float c[4], const float uv[2])
{
	SoftGL &s = *g_s;
	size_t i = (size_t)y * s.W + x;
	if (s.caps[gl::DEPTH_TEST] && z > s.depth[i])
		return;
	float r = c[0], g = c[1], b = c[2], a = c[3];
	if (s.caps[gl::TEXTURE_2D] && s.tex.count(s.bound))
	{
		SoftTex &t = s.tex[s.bound];
		if (t.w > 0)
		{
			float u = uv[0], v = uv[1];
			u = t.clampS ? std::min(std::max(u, 0.0f), 1.0f) : u - std::floor(u);
			v = t.clampT ? std::min(std::max(v, 0.0f), 1.0f) : v - std::floor(v);
			int tx = std::min(t.w - 1, (int)(u * t.w)), ty = std::min(t.h - 1, (int)(v * t.h));
			const uint8_t *p = &t.rgba[((size_t)ty * t.w + tx) * 4];
			if (t.comps == 1)
				a *= p[3] / 255.0f;
			else
			{
				r *= p[0] / 255.0f;
				g *= p[1] / 255.0f;
				b *= p[2] / 255.0f;
				if (t.comps == 4)
					a *= p[3] / 255.0f;
			}
		}
	}
	if (s.caps[gl::ALPHA_TEST] && !(a > s.alphaRef))
		return;
	uint8_t *d = &s.img.rgb[i * 3];
	float dr = d[0] / 255.0f, dg = d[1] / 255.0f, db = d[2] / 255.0f;
	if (s.caps[gl::BLEND])
	{
		auto f = [&](unsigned m, float sa) {
			switch (m)
			{
			case gl::ZERO:
				return 0.0f;
			case gl::ONE:
				return 1.0f;
			case gl::SRC_ALPHA:
				return sa;
			case gl::ONE_MINUS_SRC_ALPHA:
				return 1.0f - sa;
			}
			return 1.0f;
		};
		float fs = f(s.blendS, a), fd = f(s.blendD, a);
		r = r * fs + dr * fd;
		g = g * fs + dg * fd;
		b = b * fs + db * fd;
	}
	d[0] = (uint8_t)(std::min(std::max(r, 0.0f), 1.0f) * 255.0f + 0.5f);
	d[1] = (uint8_t)(std::min(std::max(g, 0.0f), 1.0f) * 255.0f + 0.5f);
	d[2] = (uint8_t)(std::min(std::max(b, 0.0f), 1.0f) * 255.0f + 0.5f);
	if (s.depthMask && s.caps[gl::DEPTH_TEST])
		s.depth[i] = z;
}

static void RasterTri(const SoftVert *v[3])
{
	SoftGL &s = *g_s;
	// near-plane clipping (w > 0.1)
	std::vector<SoftVert> poly(3), out;
	for (int i = 0; i < 3; i++)
		poly[i] = *v[i];
	for (size_t i = 0; i < poly.size(); i++)
	{
		const SoftVert &a = poly[i], &b = poly[(i + 1) % poly.size()];
		bool ia = a.clip[3] > 0.1f, ib = b.clip[3] > 0.1f;
		if (ia)
			out.push_back(a);
		if (ia != ib)
		{
			float t = (0.1f - a.clip[3]) / (b.clip[3] - a.clip[3]);
			SoftVert m;
			for (int k = 0; k < 4; k++)
				m.clip[k] = a.clip[k] + (b.clip[k] - a.clip[k]) * t;
			for (int k = 0; k < 4; k++)
				m.col[k] = a.col[k] + (b.col[k] - a.col[k]) * t;
			for (int k = 0; k < 2; k++)
				m.uv[k] = a.uv[k] + (b.uv[k] - a.uv[k]) * t;
			out.push_back(m);
		}
	}
	if (out.size() < 3)
		return;
	struct P
	{
		float x, y, z, iw;
		float c[4], uv[2];
	};
	std::vector<P> pts;
	for (auto &q : out)
	{
		P p;
		p.iw = 1.0f / q.clip[3];
		p.x = (q.clip[0] * p.iw * 0.5f + 0.5f) * s.W;
		p.y = (1.0f - (q.clip[1] * p.iw * 0.5f + 0.5f)) * s.H;
		p.z = q.clip[2] * p.iw;
		for (int k = 0; k < 4; k++)
			p.c[k] = q.col[k] * p.iw;
		for (int k = 0; k < 2; k++)
			p.uv[k] = q.uv[k] * p.iw;
		pts.push_back(p);
	}
	// culling on the screen-space winding (screen y down: counter-clockwise front -> negative area)
	float area = 0;
	for (size_t i = 0; i < pts.size(); i++)
	{
		const P &a = pts[i], &b = pts[(i + 1) % pts.size()];
		area += a.x * b.y - b.x * a.y;
	}
	bool front = area < 0;
	if (s.caps[gl::CULL_FACE])
	{
		if (s.cull == gl::BACK && !front)
			return;
		if (s.cull == gl::FRONT && front)
			return;
	}
	for (size_t i = 1; i + 1 < pts.size(); i++)
	{
		const P &a = pts[0], &b = pts[i], &c = pts[i + 1];
		float minx = std::max(0.0f, std::floor(std::min(a.x, std::min(b.x, c.x))));
		float maxx = std::min((float)s.W - 1, std::ceil(std::max(a.x, std::max(b.x, c.x))));
		float miny = std::max(0.0f, std::floor(std::min(a.y, std::min(b.y, c.y))));
		float maxy = std::min((float)s.H - 1, std::ceil(std::max(a.y, std::max(b.y, c.y))));
		float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
		if (std::fabs(den) < 1e-9f)
			continue;
		for (int y = (int)miny; y <= (int)maxy; y++)
			for (int x = (int)minx; x <= (int)maxx; x++)
			{
				float px = x + 0.5f, py = y + 0.5f;
				float l0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
				float l1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
				float l2 = 1 - l0 - l1;
				if (l0 < 0 || l1 < 0 || l2 < 0)
					continue;
				float iw = l0 * a.iw + l1 * b.iw + l2 * c.iw;
				float z = l0 * a.z + l1 * b.z + l2 * c.z;
				if (z < -1 || z > 1)
					continue;
				float col[4], uv[2];
				for (int k = 0; k < 4; k++)
					col[k] = (l0 * a.c[k] + l1 * b.c[k] + l2 * c.c[k]) / iw;
				for (int k = 0; k < 2; k++)
					uv[k] = (l0 * a.uv[k] + l1 * b.uv[k] + l2 * c.uv[k]) / iw;
				Fragment(x, y, z, col, uv);
			}
	}
}

// ------------------------------------------------------------------ GL entry points

#define E GHOUL_GLAPI
static void E AlphaFunc(GhoulGLTable::GLenum f, GhoulGLTable::GLfloat r)
{
	g_s->alphaFunc = f;
	g_s->alphaRef = r;
}
static void E Begin(GhoulGLTable::GLenum)
{
	g_s->verts.clear();
	g_s->inBegin = true;
}
static void E BindTexture(GhoulGLTable::GLenum, GhoulGLTable::GLuint t) { g_s->bound = t; }
static void E BlendFunc(GhoulGLTable::GLenum a, GhoulGLTable::GLenum b)
{
	g_s->blendS = a;
	g_s->blendD = b;
}
static void E Color3f(float r, float g, float b)
{
	g_s->col[0] = r, g_s->col[1] = g, g_s->col[2] = b, g_s->col[3] = 1;
}
static void E Color4f(float r, float g, float b, float a)
{
	g_s->col[0] = r, g_s->col[1] = g, g_s->col[2] = b, g_s->col[3] = a;
}
static void E CullFace(GhoulGLTable::GLenum m) { g_s->cull = m; }
static void E DepthFunc(GhoulGLTable::GLenum) {}
static void E DepthMask(GhoulGLTable::GLboolean f) { g_s->depthMask = f != 0; }
static void E DeleteTextures(GhoulGLTable::GLsizei n, const GhoulGLTable::GLuint *t)
{
	for (int i = 0; i < n; i++)
		g_s->tex.erase(t[i]);
}
static void E Disable(GhoulGLTable::GLenum c) { g_s->caps[c] = false; }
static void E Enable(GhoulGLTable::GLenum c) { g_s->caps[c] = true; }
static void E End()
{
	SoftGL &s = *g_s;
	s.inBegin = false;
	for (size_t i = 0; i + 2 < s.verts.size(); i += 3)
	{
		const SoftVert *v[3] = {&s.verts[i], &s.verts[i + 1], &s.verts[i + 2]};
		s.tris++;
		if (s.caps[gl::TEXTURE_2D] && s.tex.count(s.bound))
			s.texturedTris++;
		if (s.capture)
		{
			PosedTri t;
			for (int k = 0; k < 3; k++)
			{
				std::memcpy(t.p[k], v[k]->world, 12);
				std::memcpy(t.uv[k], v[k]->uv, 8);
				std::memcpy(t.n[k], v[k]->n, 12);
			}
			t.material = s.caps[gl::TEXTURE_2D] && s.tex.count(s.bound) ? (int)s.bound : 0;
			s.captured.push_back(t);
		}
		RasterTri(v);
	}
}
static void E FrontFace(GhoulGLTable::GLenum) {}
static GhoulGLTable::GLenum E GetError() { return 0; }
static void E GenTextures(GhoulGLTable::GLsizei n, GhoulGLTable::GLuint *t)
{
	for (int i = 0; i < n; i++)
		t[i] = g_s->nextName++;
}
static void E GetFloatv(GhoulGLTable::GLenum, GhoulGLTable::GLfloat *p) { std::memcpy(p, Top().data(), 64); }
static void E GetIntegerv(GhoulGLTable::GLenum, GhoulGLTable::GLint *p) { *p = 0; }
static void E LoadIdentity() { Top() = Ident(); }
static void E LoadMatrixf(const float *m) { std::memcpy(Top().data(), m, 64); }
static void E MatrixMode(GhoulGLTable::GLenum m)
{
	g_s->mode = m == gl::PROJECTION ? 1 : (m == gl::TEXTURE ? 2 : 0);
}
static void E MultMatrixf(const float *m)
{
	std::array<float, 16> b;
	std::memcpy(b.data(), m, 64);
	Top() = Mul(Top(), b);
}
static void E Normal3f(float x, float y, float z)
{
	g_s->nrm[0] = x, g_s->nrm[1] = y, g_s->nrm[2] = z;
}
static void E PixelStorei(GhoulGLTable::GLenum, GhoulGLTable::GLint) {}
static void E PolygonOffset(float, float) {}
static void E PopMatrix()
{
	if (g_s->stack[g_s->mode].size() > 1)
		g_s->stack[g_s->mode].pop_back();
}
static void E PushMatrix() { g_s->stack[g_s->mode].push_back(Top()); }
static void E Rotatef(float ang, float x, float y, float z)
{
	float l = std::sqrt(x * x + y * y + z * z);
	if (l <= 0)
		return;
	x /= l, y /= l, z /= l;
	float c = std::cos(ang * 3.14159265f / 180.0f), s = std::sin(ang * 3.14159265f / 180.0f), t = 1 - c;
	std::array<float, 16> r = Ident();
	r[0] = t * x * x + c;
	r[1] = t * x * y + s * z;
	r[2] = t * x * z - s * y;
	r[4] = t * x * y - s * z;
	r[5] = t * y * y + c;
	r[6] = t * y * z + s * x;
	r[8] = t * x * z + s * y;
	r[9] = t * y * z - s * x;
	r[10] = t * z * z + c;
	Top() = Mul(Top(), r);
}
static void E ShadeModel(GhoulGLTable::GLenum) {}
static void E TexCoord2f(float s, float t)
{
	g_s->uv[0] = s, g_s->uv[1] = t;
}
static void E TexEnvf(GhoulGLTable::GLenum, GhoulGLTable::GLenum, float) {}
static void E TexImage2D(GhoulGLTable::GLenum, GhoulGLTable::GLint level, GhoulGLTable::GLint internal,
	GhoulGLTable::GLsizei w, GhoulGLTable::GLsizei h, GhoulGLTable::GLint, GhoulGLTable::GLenum format,
	GhoulGLTable::GLenum, const void *pixels)
{
	if (level != 0)
		return;
	SoftTex &t = g_s->tex[g_s->bound];
	t.w = w;
	t.h = h;
	t.rgba.assign((size_t)w * h * 4, 255);
	const uint8_t *p = (const uint8_t *)pixels;
	if (format == gl::ALPHA)
	{
		t.comps = 1;
		for (int i = 0; i < w * h; i++)
			t.rgba[i * 4 + 3] = p[i];
	}
	else
	{
		t.comps = internal == (int)gl::RGB || internal == 3 ? 3 : 4;
		std::memcpy(t.rgba.data(), p, (size_t)w * h * 4);
	}
}
static void E TexParameteri(GhoulGLTable::GLenum, GhoulGLTable::GLenum p, GhoulGLTable::GLint v)
{
	SoftTex &t = g_s->tex[g_s->bound];
	if (p == gl::TEXTURE_WRAP_S)
		t.clampS = v == (int)gl::CLAMP;
	if (p == gl::TEXTURE_WRAP_T)
		t.clampT = v == (int)gl::CLAMP;
}
static void E TexGeni(GhoulGLTable::GLenum, GhoulGLTable::GLenum, GhoulGLTable::GLint) {}
static void E Vertex3f(float x, float y, float z)
{
	SoftGL &s = *g_s;
	SoftVert v;
	float in[4] = {x, y, z, 1}, eye[4];
	Xform(s.stack[0].back(), in, eye);
	Xform(s.stack[1].back(), eye, v.clip);
	std::memcpy(v.world, eye, 12);
	std::memcpy(v.col, s.col, 16);
	float t[4] = {s.uv[0], s.uv[1], 0, 1}, to[4];
	Xform(s.stack[2].back(), t, to);
	v.uv[0] = to[0];
	v.uv[1] = to[1];
	float n4[4] = {s.nrm[0], s.nrm[1], s.nrm[2], 0}, ne[4];
	Xform(s.stack[0].back(), n4, ne);
	std::memcpy(v.n, ne, 12);
	s.verts.push_back(v);
}
static void E Color3fv(const float *v) { Color3f(v[0], v[1], v[2]); }
static void E Normal3fv(const float *v) { Normal3f(v[0], v[1], v[2]); }
static void E TexCoord2fv(const float *v) { TexCoord2f(v[0], v[1]); }
static void E Vertex3fv(const float *v) { Vertex3f(v[0], v[1], v[2]); }
static void E LightModelf(GhoulGLTable::GLenum, float) {}
static void E LightModelfv(GhoulGLTable::GLenum, const float *) {}
static void E LightModeli(GhoulGLTable::GLenum, GhoulGLTable::GLint) {}
static void E LightModeliv(GhoulGLTable::GLenum, const GhoulGLTable::GLint *) {}
// I4c: GL_LINEAR_ATTENUATION values are recorded for the test of SPEC-AMEND-005 I4b.5
static std::vector<float> g_linatt;
static void E Lightf(GhoulGLTable::GLenum, GhoulGLTable::GLenum pname, float v)
{
	if (pname == 0x1208)
		g_linatt.push_back(v);
}
std::vector<float> SoftGL_TakeLinearAttenuations()
{
	std::vector<float> r;
	r.swap(g_linatt);
	return r;
}
static void E Lightfv(GhoulGLTable::GLenum, GhoulGLTable::GLenum, const float *) {}
static void E Lighti(GhoulGLTable::GLenum, GhoulGLTable::GLenum, GhoulGLTable::GLint) {}
static void E Lightiv(GhoulGLTable::GLenum, GhoulGLTable::GLenum, const GhoulGLTable::GLint *) {}
static void E Materialf(GhoulGLTable::GLenum, GhoulGLTable::GLenum, float) {}
static void E Materialfv(GhoulGLTable::GLenum, GhoulGLTable::GLenum, const float *) {}
static void E Materiali(GhoulGLTable::GLenum, GhoulGLTable::GLenum, GhoulGLTable::GLint) {}
static void E Materialiv(GhoulGLTable::GLenum, GhoulGLTable::GLenum, const GhoulGLTable::GLint *) {}
static void E Flush() {}

static GhoulGLTable g_table = {AlphaFunc, Begin, BindTexture, BlendFunc, Color3f, Color4f, CullFace, DepthFunc,
	DepthMask, DeleteTextures, Disable, Enable, End, FrontFace, GetError, GenTextures, GetFloatv, GetIntegerv,
	LoadIdentity, LoadMatrixf, MatrixMode, MultMatrixf, Normal3f, PixelStorei, PolygonOffset, PopMatrix, PushMatrix,
	Rotatef, ShadeModel, TexCoord2f, TexEnvf, TexImage2D, TexParameteri, TexGeni, Vertex3f, Color3fv, Normal3fv,
	TexCoord2fv, Vertex3fv, LightModelf, LightModelfv, LightModeli, LightModeliv, Lightf, Lightfv, Lighti, Lightiv,
	Materialf, Materialfv, Materiali, Materialiv, Flush};

SoftGL *SoftGL_Create(int w, int h)
{
	SoftGL *s = new SoftGL();
	s->W = w;
	s->H = h;
	for (auto &st : s->stack)
		st.assign(1, Ident());
	g_s = s;
	return s;
}

const GhoulGLTable *SoftGL_Table() { return &g_table; }

void SoftGL_Resize(SoftGL *s, int w, int h)
{
	s->W = w;
	s->H = h;
}

void SoftGL_Begin(SoftGL *s, const float view[16], const float proj[16], uint8_t clear[3])
{
	g_s = s;
	s->img.Init(s->W, s->H, clear[0], clear[1], clear[2]);
	s->depth.assign((size_t)s->W * s->H, 1.0f);
	for (auto &st : s->stack)
		st.assign(1, Ident());
	std::memcpy(s->stack[0][0].data(), view, 64);
	std::memcpy(s->stack[1][0].data(), proj, 64);
	s->caps.clear();
	s->caps[gl::DEPTH_TEST] = true;
	s->caps[gl::TEXTURE_2D] = true;
	s->caps[gl::CULL_FACE] = true;
	s->cull = gl::BACK;
	s->depthMask = true;
	s->tris = s->texturedTris = 0;
	s->captured.clear();
}

const Image &SoftGL_Image(SoftGL *s) { return s->img; }
int SoftGL_Triangles(SoftGL *s) { return s->tris; }
int SoftGL_TexturedTriangles(SoftGL *s) { return s->texturedTris; }
void SoftGL_Reset(SoftGL *s) { s->captured.clear(); }

void SoftGL_SetCapture(SoftGL *s, bool on) { s->capture = on; }
const std::vector<PosedTri> &SoftGL_Captured(SoftGL *s) { return s->captured; }
bool SoftGL_TextureImage(SoftGL *s, unsigned name, Image &out)
{
	auto it = s->tex.find(name);
	if (it == s->tex.end() || it->second.w == 0)
		return false;
	out.Init(it->second.w, it->second.h, 0, 0, 0);
	for (int i = 0; i < it->second.w * it->second.h; i++)
		for (int k = 0; k < 3; k++)
			out.rgb[i * 3 + k] = it->second.rgba[i * 4 + k];
	return true;
}
