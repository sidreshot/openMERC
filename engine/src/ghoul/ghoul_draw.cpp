// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - drawing instances through the renderer's GL table (instance method 41):
// traversal, culling, posed geometry, materials, software and OpenGL lighting, model lights, world-light
// fading, billboards, blob and projected shadows, statistics.
// Implements: SPEC-012 sections 4 (pose evaluation of vertices, normals, UVs, material tracks) and 8 (drawing),
//             SPEC-411 sections 6-7 (lighting inside the Ghoul drawer, shadows), SPEC-AMEND-002 items 3.1, 5.1-5.3,
//             SPEC-011 (render-parameter block, render cull); SPEC-AMEND-005 I3.7 (software specular colour and
//             pass state), I3.8 (projected shadow mask placement)

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ghoul_gl.h"
#include "ghoul_internal.h"

namespace ghoul {

extern int g_drawRole;

// ------------------------------------------------------------------ posed geometry (SPEC-012 section 4)

void EvalGeometry(const GhbFile &f, const FramePair &fp, std::vector<float> &apos, std::vector<float> &anorm,
	std::vector<float> &atex)
{
	const GhbGeometry &g = f.geo;
	apos.resize(g.nApos * 3);
	anorm.resize(g.nAnorm * 3);
	atex.resize(g.nAtex * 2);
	if (g.nApos)
	{
		f.AnimPositions(fp.a, apos.data());
		if (fp.a != fp.b && fp.w < 1.0f)
		{
			std::vector<float> b(apos.size());
			f.AnimPositions(fp.b, b.data());
			for (size_t i = 0; i < apos.size(); i++)
				apos[i] = fp.w * apos[i] + (1.0f - fp.w) * b[i];
		}
	}
	if (g.nAnorm)
	{
		f.AnimNormals(fp.a, anorm.data());
		if (fp.a != fp.b && fp.w < 1.0f)
		{
			std::vector<float> b(anorm.size());
			f.AnimNormals(fp.b, b.data());
			for (size_t i = 0; i < anorm.size(); i++)
				anorm[i] = fp.w * anorm[i] + (1.0f - fp.w) * b[i]; // not renormalised
		}
	}
	if (g.nAtex)
	{
		f.AnimTexCoords(fp.a, atex.data());
		if (fp.a != fp.b && fp.w < 1.0f)
		{
			std::vector<float> b(atex.size());
			f.AnimTexCoords(fp.b, b.data());
			for (size_t i = 0; i < atex.size(); i++)
				atex[i] = fp.w * atex[i] + (1.0f - fp.w) * b[i];
		}
	}
}

template <class T> static T Blend(const Track<T> &t, const FramePair &fp)
{
	if (t.constant || t.v.size() <= 1)
		return t.v[0];
	const T &a = t.At(fp.a), &b = t.At(fp.b);
	return b + (a - b) * fp.w;
}
static float BlendF(const Track<float> &t, const FramePair &fp)
{
	if (t.constant || t.v.size() <= 1)
		return t.v[0];
	return t.At(fp.b) + (t.At(fp.a) - t.At(fp.b)) * fp.w;
}

// ------------------------------------------------------------------ lights

namespace {

enum LightKind
{
	kPoint,       // world / dynamic light: point-light falloff with radius cut-off
	kDirectional, // fast light or probe wrap light
	kWrap,
	kModelOmni
};

struct ActiveLight
{
	LightKind kind;
	Vec3 pos;    // model space (point lights)
	Vec3 dir;    // model space unit vector towards the light (directional)
	Vec3 color;
	float radius = 0, start = 0, end = 0, mult = 1;
	bool world = false;
};

float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct DrawCtx
{
	GhoulRenderParams *P;
	const GhoulGLTable *g;
	System *sys;
	bool shadowOnly;
	float time;
	// shadow mask (mode 2)
	bool wantMask = false;
	uint8_t mask[64 * 64];
	int mx0 = 64, my0 = 64, mx1 = -1, my1 = -1;
	Vec3 shadowOrigin, shadowDir, shadowU, shadowV; // SPEC-AMEND-005 I3.8: origin, a, X, Y of the mask basis
	float shadowScale = 1, groundZ = 0;
	bool shadowValid = false;
	// I1c: SPEC-AMEND-006 I1c.3 - the world-flag-0 light nodes of the whole hierarchy, collected before
	// anything is drawn (positions in world space), shared by every instance of this draw call; and the
	// instances the gather pass visited
	std::vector<ActiveLight> shared;
	std::vector<const Inst *> gathered;
};

} // namespace

// SPEC-411 section 6.3: per-instance world-light states (fading)
static void GatherWorldLights(Inst *in, DrawCtx &c, const Vec3 &entityOrigin, int maxL, int ownCount,
	std::vector<ActiveLight> &out, const Matrix4 &worldToModel, bool fast)
{
	std::vector<const LightRecord *> cand;
	in->sys->world.Query(entityOrigin, cand);
	RenderCache &rc = in->rc;
	float now = c.time, T = G().gs.lightSmooth;
	auto contribute = [&](const LightRecord &l, float level) {
		ActiveLight a;
		Vec3 col = Vec3(l.color) * level;
		if (Length(col) < 0.01f)
			return;
		a.color = col;
		a.radius = l.radius;
		a.world = true;
		if (fast)
		{
			Vec3 d = Vec3(l.origin) - entityOrigin;
			float dist = Length(d);
			if (dist > l.radius || dist <= 0.0f)
				return;
			float h = dist <= 0.75f * l.radius ? 1.0f : (l.radius - dist) / (0.25f * l.radius);
			a.kind = kDirectional;
			a.dir = Normalize(XformVector(d * (1.0f / dist), worldToModel));
			a.color = col * h;
		}
		else
		{
			a.kind = kPoint;
			a.pos = XformPoint(Vec3(l.origin), worldToModel);
		}
		out.push_back(a);
	};
	bool immediate = T <= 0.0f || !rc.valid;
	if (immediate)
	{
		rc.lights.clear();
		for (size_t i = 0; i < cand.size() && (int)i < maxL; i++)
		{
			LightState s;
			s.key = cand[i];
			s.light = *cand[i];
			s.phase = 1;
			s.start = now;
			rc.lights.push_back(s);
			contribute(*cand[i], 1.0f);
		}
		return;
	}
	for (auto &s : rc.lights)
	{
		s.seen = false;
		s.contributed = false;
		s.fresh = false;
	}
	int count = ownCount;
	for (size_t i = 0; i < cand.size() && (int)i < 2 * maxL; i++)
	{
		LightState *s = nullptr;
		for (auto &x : rc.lights)
			if (x.key == cand[i])
				s = &x;
		if (!s)
		{
			LightState n;
			n.key = cand[i];
			n.light = *cand[i];
			n.phase = 0;
			n.start = now;
			n.fresh = true;
			n.seen = true;
			rc.lights.push_back(n);
			continue;
		}
		s->seen = true;
		if (s->phase == 2)
		{
			s->phase = 0;
			s->start = 2 * s->start + T - now;
		}
		float e = now - s->start;
		if (s->phase == 0 && e > T)
			s->phase = 1;
		if ((s->phase == 1 || (s->phase == 0 && e > 0)) && count < maxL)
		{
			contribute(s->light, s->phase == 1 ? 1.0f : e / T);
			s->contributed = true;
			count++;
		}
	}
	for (auto &s : rc.lights)
		if (!s.seen && s.phase != 2)
		{
			s.start = s.phase == 1 ? now : 2 * s.start + T - now;
			s.phase = 2;
		}
	for (auto it = rc.lights.begin(); it != rc.lights.end();)
	{
		LightState &s = *it;
		if (s.phase == 2)
		{
			float e = now - s.start;
			float f = e > 0 ? 1.0f - e / T : 1.0f;
			if (e > 0 && (f < 0 || f > 1 || Dot(Vec3(s.light.color) * f, Vec3(s.light.color) * f) < 0.2f))
			{
				it = rc.lights.erase(it);
				continue;
			}
			if (count < maxL + 2)
			{
				contribute(s.light, f);
				s.contributed = true;
				count++;
			}
		}
		if (!s.contributed && !s.fresh)
		{
			it = rc.lights.erase(it);
			continue;
		}
		++it;
	}
	while (rc.lights.size() > 32)
		rc.lights.erase(rc.lights.begin());
}

// software vertex lighting (SPEC-012 lighting, SPEC-411 section 6)
static Vec3 LightVertex(const Vec3 &p, const Vec3 &n, const Vec3 &ambient, const std::vector<ActiveLight> &lights,
	bool spec, const Vec3 &viewer, float expo, float level, const Vec3 &specColor, Vec3 &specOut)
{
	Vec3 c = ambient;
	specOut = Vec3();
	Vec3 rv;
	if (spec)
	{
		Vec3 v = Normalize(viewer - p);
		rv = n * (2.0f * Dot(n, v)) - v; // reflection of the vertex->viewer vector about n
	}
	for (const ActiveLight &l : lights)
	{
		switch (l.kind)
		{
		case kPoint:
		{
			Vec3 d = l.pos - p;
			float q = Dot(d, d), nd = Dot(n, d);
			if (nd < 0 || q < 1.0f || q > l.radius * l.radius)
				break;
			float R2 = l.radius * l.radius;
			float gfac = q <= 0.5625f * R2 ? 1.0f : 4.0f * (R2 - q) / R2;
			float dist = std::sqrt(q);
			c += l.color * (nd / dist * gfac);
			if (spec)
			{
				float s = Dot(d, rv) / dist;
				if (s > 0)
				{
					float k = std::pow(s, expo) * level;
					specOut += Vec3(l.color.x * specColor.x, l.color.y * specColor.y, l.color.z * specColor.z) * k;
				}
			}
			break;
		}
		case kDirectional:
		{
			float nl = Dot(n, l.dir);
			if (nl < 0)
				break;
			c += l.color * nl;
			if (spec && Dot(l.dir, rv) > 0)
			{
				float k = std::pow(Dot(l.dir, rv), expo) * level;
				specOut += Vec3(l.color.x * specColor.x, l.color.y * specColor.y, l.color.z * specColor.z) * k;
			}
			break;
		}
		case kWrap:
		{
			c += l.color * (0.75f * (Dot(n, l.dir) + 0.5f));
			if (spec)
			{
				Vec3 v = l.pos - p;
				float lv = Length(v);
				if (lv >= 1.0f && Dot(n, v) >= 0)
				{
					float s = Dot(v, rv) / lv;
					if (s > 0)
					{
						float k = 2.0f * std::pow(s, expo) * level;
						specOut += Vec3(l.color.x * specColor.x, l.color.y * specColor.y, l.color.z * specColor.z) * k;
					}
				}
			}
			break;
		}
		case kModelOmni:
		{
			Vec3 d = l.pos - p;
			float dist = Length(d), nd = Dot(n, d);
			if (dist > l.end || dist < 1.0f || nd < 0)
				break;
			float fall = dist <= l.start ? 1.0f : (l.end - dist) / std::max(l.end - l.start, 1e-6f);
			c += l.color * (nd / dist * l.mult * fall);
			if (spec)
			{
				float s = Dot(d, rv) / dist;
				if (s > 0)
				{
					float k = std::pow(s, expo) * level * l.mult;
					specOut += Vec3(l.color.x * specColor.x, l.color.y * specColor.y, l.color.z * specColor.z) * k;
				}
			}
			break;
		}
		}
	}
	return c;
}

// ------------------------------------------------------------------ shadow mask

// SPEC-AMEND-005 I3.8: orthographic projection onto the plane through the origin perpendicular to the light axis:
// u = trunc(k (p - origin).X), v = trunc(k (p - origin).Y), clamped to -30...29, texel (u + 32, v + 32); the touched
// rectangle is kept in texel coordinates
static void Stamp(DrawCtx &c, const Vec3 &w)
{
	Vec3 d = w - c.shadowOrigin;
	int x = (int)(Dot(d, c.shadowU) * c.shadowScale), y = (int)(Dot(d, c.shadowV) * c.shadowScale);
	x = x < -30 ? -30 : (x > 29 ? 29 : x);
	y = y < -30 ? -30 : (y > 29 ? 29 : y);
	x += 32;
	y += 32;
	c.mask[y * 64 + x] = 255;
	c.mx0 = std::min(c.mx0, x);
	c.my0 = std::min(c.my0, y);
	c.mx1 = std::max(c.mx1, x);
	c.my1 = std::max(c.my1, y);
}

static void StampTriangle(DrawCtx &c, const Vec3 &a, const Vec3 &b, const Vec3 &d)
{
	Stamp(c, a);
	Stamp(c, b);
	Stamp(c, d);
	Stamp(c, (a + b + d) * (1.0f / 3.0f));
	Stamp(c, (a + b) * 0.5f);
	Stamp(c, (b + d) * 0.5f);
	Stamp(c, (d + a) * 0.5f);
	Stamp(c, (a * 2.0f + b + d) * 0.25f);
	Stamp(c, (b * 2.0f + a + d) * 0.25f);
	Stamp(c, (d * 2.0f + a + b) * 0.25f);
}

// ------------------------------------------------------------------ meshes of one level

static void GlMatrix(const GhoulGLTable *g, const Matrix4 &m) { g->MultMatrixf(&m.m[0][0]); }
// GFX-modern bloom: tell the renderer how much the next primitive is a light source (gl::OM_BLOOM_SOURCE)
static void BloomSource(const GhoulGLTable *g, float w)
{
	if (w > 0.0f)
	{
		float v[4] = {w, 0.0f, 0.0f, 0.0f};
		g->Materialfv(gl::FRONT_AND_BACK, gl::OM_BLOOM_SOURCE, v);
	}
}

static void DrawOwn(Inst *in, DrawCtx &c, const Pose &p, const Matrix4 &W)
{
	const GhoulGLTable *g = c.g;
	GhoulRenderParams *P = c.P;
	GhbFile &f = *p.file;
	Object *o = in->obj;
	if (!f.hasGeometry)
		return;
	std::vector<float> apos, anorm, atex;
	EvalGeometry(f, p.fp, apos, anorm, atex);
	const GhbGeometry &geo = f.geo;
	Matrix4 Wi = MatInverse(W);
	Vec3 viewerModel = XformPoint(Vec3(P->viewOrigin), Wi);

	// shadow-only: positions feed the mask
	if (c.shadowOnly || c.wantMask)
	{
		for (auto &sf : geo.surfaces)
		{
			if (sf.node < 0)
				continue;
			const GhbNode &nd = f.nodes[sf.node];
			int part = o->PartOfNode(&f, sf.node);
			if ((part && !in->PartShown(part)) || !nd.visible.At(p.dominant))
				continue;
			if (!c.wantMask)
				continue;
			for (size_t k = 0; k + 2 < sf.tris.size(); k += 3)
			{
				Vec3 v[3];
				for (int j = 0; j < 3; j++)
				{
					int pi = geo.corners[sf.tris[k + j] * 3];
					Vec3 mp = pi < 0 ? Vec3(&geo.pos[(size_t)~pi * 3]) : Vec3(&apos[(size_t)pi * 3]);
					v[j] = XformPoint(mp, W);
				}
				StampTriangle(c, v[0], v[1], v[2]);
			}
		}
		if (c.shadowOnly)
			return;
	}

	// ---- lights for this instance
	RenderCache &rc = in->rc;
	if (rc.valid && c.time - rc.lastDraw > 0.15f)
		rc.Reset();
	bool first = !rc.valid;
	Vec3 color(P->lightColor);
	std::vector<ActiveLight> lights;
	Vec3 ambient;
	bool dynAllowed = !P->noDynamicLights && !c.shadowOnly;
	// model (file) lights (SPEC-AMEND-006 I1c.3): the gather pass of Inst::Draw collected the world-flag-0 light
	// nodes of the whole hierarchy into one shared list (world space) and exported the WORLD_* ones; every
	// instance takes the shared lights that reach its box. I1c.
	Box3 worldBox;
	int own = 0;
	if (dynAllowed)
	{
		const BoundsRec &bb = p.file->bounds;
		bool boxed = !bb.infinite && !bb.empty;
		for (const ActiveLight &sl : c.shared)
		{
			ActiveLight a = sl;
			a.pos = XformPoint(sl.pos, Wi); // into this instance's model space (where the ranges apply)
			if (boxed)
			{
				// the light reaches the box when its distance to the box is at most its end range
				float d2 = 0.0f;
				for (int k = 0; k < 3; k++)
				{
					float v = a.pos[k] < bb.mn[k] ? bb.mn[k] - a.pos[k] : (a.pos[k] > bb.mx[k] ? a.pos[k] - bb.mx[k] : 0.0f);
					d2 += v * v;
				}
				if (d2 > a.end * a.end)
					continue;
			}
			lights.push_back(a);
			own++;
		}
		if (std::find(c.gathered.begin(), c.gathered.end(), in) != c.gathered.end())
			worldBox.AddBox(f.bounds, W);
	}
	if (P->dirValid)
	{
		// probe method: smoothed colour and vector, ambient 0.5 S and one wrap light
		Vec3 dv(P->lightDir);
		if (first || !rc.smoothInit)
		{
			rc.probeS = color;
			rc.probeV = dv;
			rc.smoothInit = true;
		}
		else
		{
			rc.probeS = rc.probeS * 0.9f + color * 0.1f;
			rc.probeV = rc.probeV * 0.9f + dv * 0.1f;
		}
		ambient = rc.probeS * 0.5f;
		ActiveLight w;
		w.kind = kWrap;
		w.color = rc.probeS * 0.5f;
		w.dir = Normalize(XformVector(Normalize(rc.probeV), Wi));
		w.pos = XformPoint(Vec3(P->origin) + rc.probeV, Wi);
		lights.push_back(w);
	}
	else if (P->useGLLights)
	{
		ambient = color * G().gs.lightAmbient;
		int maxL = P->maxLights < 1 ? 1 : (P->maxLights > 8 ? 8 : P->maxLights);
		GatherWorldLights(in, c, Vec3(P->origin), maxL, own, lights, Wi, P->fastLights != 0);
	}
	else
		ambient = color;
	if (dynAllowed && worldBox.any)
	{
		for (auto &d : in->sys->dynLights)
		{
			Vec3 o3(d.origin);
			bool inside = true;
			for (int a = 0; a < 3; a++)
				if (o3[a] < worldBox.mn[a] - d.radius || o3[a] > worldBox.mx[a] + d.radius)
					inside = false;
			if (!inside)
				continue;
			ActiveLight a;
			a.color = Vec3(d.color);
			a.radius = d.radius;
			if (P->fastLights)
			{
				Vec3 dd = o3 - Vec3(P->origin);
				float dist = Length(dd);
				if (dist > d.radius || dist <= 0)
					continue;
				a.kind = kDirectional;
				a.dir = Normalize(XformVector(dd * (1.0f / dist), Wi));
				a.color = a.color * (dist <= 0.75f * d.radius ? 1.0f : (d.radius - dist) / (0.25f * d.radius));
			}
			else
			{
				a.kind = kPoint;
				a.pos = XformPoint(o3, Wi);
			}
			lights.push_back(a);
		}
	}
	rc.valid = true;
	rc.lastDraw = c.time;
	bool hw = P->hardwareLights && !lights.empty();

	g->PushMatrix();
	GlMatrix(g, W);
	g->TexEnvf(gl::TEXTURE_ENV, gl::TEXTURE_ENV_MODE, (float)gl::MODULATE);
	bool mirrored = MatDet3(W) < 0.0f;
	float alphaInst = in->tint[3] / 255.0f;
	Vec3 tint(in->tint[0] / 255.0f, in->tint[1] / 255.0f, in->tint[2] / 255.0f);
	bool tinted = Dot(tint - Vec3(1, 1, 1), tint - Vec3(1, 1, 1)) > 0.1f;
	bool sysSpec = o->specular && P->specular;

	if (hw)
	{
		float amb[4] = {ambient.x, ambient.y, ambient.z, 1.0f};
		g->LightModelfv(gl::LIGHT_MODEL_AMBIENT, amb);
		int n = 0;
		for (auto &l : lights)
		{
			if (n >= 8)
				break;
			unsigned int id = gl::LIGHT0 + n;
			// black box (F5, the original's Ghoul GL-table trace, orig1.dm2, ghl_light_method 3): an OpenGL world
			// light's DIFFUSE and SPECULAR are its colour x (1 - LIGHT_AMBIENT) - 0.4 with the default 0.6, 0.2 after
			// `gserver LIGHT_AMBIENT 0.8`, 0.8 after 0.2, constant over time (no fade). Model omni lights keep their
			// colour (SPEC-012 GL rules; not observed). OPEN_QUESTIONS F5 6.
			float hwk = l.kind == kModelOmni ? 1.0f : 1.0f - G().gs.lightAmbient;
			float col[4] = {l.color.x * hwk, l.color.y * hwk, l.color.z * hwk, 1.0f};
			g->Lightfv(id, gl::DIFFUSE, col);
			g->Lightfv(id, gl::SPECULAR, col);
			Vec3 lp = l.kind == kDirectional || l.kind == kWrap ? l.dir : l.pos;
			float pos[4] = {lp.x, lp.y, lp.z, (P->glDirectional || l.kind == kDirectional || l.kind == kWrap) ? 0.0f : 1.0f};
			g->Lightfv(id, gl::POSITION, pos);
			float lin = 0.0f;
			if (P->glAttenuation)
				// SPEC-AMEND-005 I4b.5: world/dynamic lights get 1 / radius (was 0.75 / radius) (I4c)
				lin = l.kind == kModelOmni ? (l.start > 0 ? 1.0f / l.start : 0.0f) : (l.radius > 0 ? 1.0f / l.radius : 0.0f);
			g->Lightf(id, gl::LINEAR_ATTENUATION, lin);
			g->Lightf(id, gl::QUADRATIC_ATTENUATION, 0.0f);
			g->Enable(id);
			n++;
		}
		g->Enable(gl::LIGHTING);
	}

	int lastGroupMat = -1;
	(void)lastGroupMat;
	for (auto &sf : geo.surfaces)
	{
		if (sf.node < 0 || sf.material >= f.materials.size())
			continue;
		const GhbNode &nd = f.nodes[sf.node];
		int part = o->PartOfNode(&f, sf.node);
		if (part && !in->PartShown(part))
			continue;
		if (!nd.visible.At(p.dominant))
			continue;
		f.usedNodes[sf.node] = 1; // gserver USAGE
		const GhbMaterial &m = f.materials[sf.material];
		float opacity = BlendF(m.opacity, p.fp);
		float alpha = opacity * alphaInst;
		if (alpha < 0.01f)
			continue;
		// texture: per-frame image index + the instance's skin index for the material
		int mid = o->MaterialOf(&f, (int)sf.material);
		ImageList *list = o->SlotList(&f, (int)sf.material, 0);
		Texture *tex = nullptr;
		const GhbSlot &s0 = m.slots[0];
		if (list)
		{
			int skin = 0;
			if (mid)
			{
				auto it = in->skinOverrides.find(std::make_pair(mid, part));
				if (it == in->skinOverrides.end())
					it = in->skinOverrides.find(std::make_pair(mid, 0));
				if (it != in->skinOverrides.end())
					skin = it->second;
			}
			int skinIndex = skin ? std::max(0, o->SkinImage(skin)) : 0;
			int img = (int)s0.image.At(p.dominant) + skinIndex;
			tex = ListTexture(list, img, true);
		}
		bool textured = tex && tex->decoded && s0.hasTexture;
		Vec3 base = textured ? Vec3(1, 1, 1) : Blend(m.diffuse, p.fp);
		if (tinted)
			base = Vec3(base.x * tint.x, base.y * tint.y, base.z * tint.z);
		float selfIllum = BlendF(m.selfIllum, p.fp);
		Vec3 cmul(1, 1, 1);
		bool fogOff = false;
		if (m.blend == 1)
		{
			if (P->fogVisibility < 1.0f)
			{
				alpha *= P->fogVisibility;
				fogOff = true;
			}
			if (alpha < 1.0f)
			{
				cmul = Vec3(alpha, alpha, alpha);
				alpha = 1.0f;
			}
		}
		bool blend = m.alphaTexture || alpha < 0.99f || m.blend != 0;
		// state
		if (m.twoSided)
			g->Disable(gl::CULL_FACE);
		else
		{
			g->Enable(gl::CULL_FACE);
			g->CullFace(mirrored ? gl::FRONT : gl::BACK);
		}
		if (blend)
		{
			g->Enable(gl::BLEND);
			if (m.blend == 1)
				g->BlendFunc(gl::ONE, gl::ONE);
			else
				g->BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
			g->DepthMask(0);
			g->Enable(gl::POLYGON_OFFSET_FILL);
			g->PolygonOffset(G().polyFactor, G().polyUnits);
			g->Enable(gl::ALPHA_TEST);
			g->AlphaFunc(gl::GREATER, 0.0f);
		}
		else
		{
			g->Disable(gl::BLEND);
			g->DepthMask(1);
			g->Disable(gl::POLYGON_OFFSET_FILL);
			g->Disable(gl::ALPHA_TEST);
		}
		if (fogOff)
			g->Disable(gl::FOG);
		if (textured)
		{
			g->Enable(gl::TEXTURE_2D);
			BindTexture(tex, s0.tileU, s0.tileV);
		}
		else
			g->Disable(gl::TEXTURE_2D);
		if (s0.environment)
		{
			g->TexGeni(gl::S, gl::TEXTURE_GEN_MODE, gl::SPHERE_MAP);
			g->TexGeni(gl::T, gl::TEXTURE_GEN_MODE, gl::SPHERE_MAP);
			g->Enable(gl::TEXTURE_GEN_S);
			g->Enable(gl::TEXTURE_GEN_T);
		}
		bool uvMatrix = !s0.staticUV && !P->noTextureStack && textured;
		if (uvMatrix)
		{
			Matrix4 um;
			if (s0.uv.constant || s0.uv.v.size() <= 1)
				um = s0.uv.v[0];
			else if (p.adjacent)
			{
				const Matrix4 &a = s0.uv.At(p.fp.a), &b = s0.uv.At(p.fp.b);
				for (int r = 0; r < 4; r++)
					for (int k = 0; k < 4; k++)
						um.m[r][k] = b.m[r][k] + (a.m[r][k] - b.m[r][k]) * p.fp.w;
			}
			else
				um = s0.uv.At(p.fp.a);
			g->MatrixMode(gl::TEXTURE);
			g->PushMatrix();
			g->LoadMatrixf(&um.m[0][0]);
			g->MatrixMode(gl::MODELVIEW);
		}
		// specular term (SPEC-AMEND-005 I3.7: a level of exactly 0.051 counts as on; I1c)
		float gloss = BlendF(m.gloss, p.fp), level = BlendF(m.specLevel, p.fp);
		bool spec = sysSpec && gloss > 0.0f && level >= 0.051f && selfIllum < 0.99f;
		float expo = std::pow(2.0f, 10.0f * gloss);
		Vec3 specColor = Blend(m.specular, p.fp);
		// I1c: SPEC-AMEND-006 I1c.9 - with OpenGL lights a specular material with a specular map is drawn twice:
		// first the map, opaque, lit by the specular term only (black ambient/diffuse, negated polygon offset),
		// then the base texture added (ONE, ONE) lit by ambient/diffuse/emission only
		Texture *hwSpecTex = nullptr;
		if (hw && spec)
		{
			ImageList *sl = o->SlotList(&f, (int)sf.material, 1);
			hwSpecTex = sl ? ListTexture(sl, (int)m.slots[1].image.At(p.dominant), true) : nullptr;
			if (hwSpecTex && !hwSpecTex->decoded)
				hwSpecTex = nullptr;
		}
		if (hw)
		{
			float shin = 4.0f * expo;
			float spc[4] = {specColor.x * level, specColor.y * level, specColor.z * level, opacity};
			float zero4[4] = {0.0f, 0.0f, 0.0f, 1.0f};
			if (hwSpecTex)
			{
				g->Enable(gl::TEXTURE_2D);
				BindTexture(hwSpecTex, m.slots[1].tileU, m.slots[1].tileV);
				g->Color4f(1.0f, 1.0f, 1.0f, alpha);
				g->Enable(gl::POLYGON_OFFSET_FILL);
				g->PolygonOffset(-G().polyFactor, -G().polyUnits);
				float amb[4] = {0.0f, 0.0f, 0.0f, alpha};
				g->Materialfv(gl::FRONT_AND_BACK, gl::AMBIENT_AND_DIFFUSE, amb);
				g->Materialfv(gl::FRONT_AND_BACK, gl::EMISSION, zero4);
				if (shin <= 128.0f)
					g->Materialf(gl::FRONT_AND_BACK, gl::SHININESS, shin);
				g->Materialfv(gl::FRONT_AND_BACK, gl::SPECULAR, spc);
			}
			else
			{
				float amb[4] = {base.x, base.y, base.z, alpha};
				g->Materialfv(gl::FRONT_AND_BACK, gl::AMBIENT_AND_DIFFUSE, amb);
				float em[4] = {selfIllum * base.x, selfIllum * base.y, selfIllum * base.z, alpha};
				g->Materialfv(gl::FRONT_AND_BACK, gl::EMISSION, em);
				if (spec)
				{
					if (shin <= 128.0f)
						g->Materialf(gl::FRONT_AND_BACK, gl::SHININESS, shin);
					g->Materialfv(gl::FRONT_AND_BACK, gl::SPECULAR, spc);
				}
				else
				{
					g->Materialf(gl::FRONT_AND_BACK, gl::SHININESS, 0.0f);
					g->Materialfv(gl::FRONT_AND_BACK, gl::SPECULAR, zero4);
				}
			}
		}
		std::vector<Vec3> specs;
		// SPEC-AMEND-005 I3.7 (software lighting): the specular colour is white when slot 1 has a texture (the map
		// supplies the colour), else the material's; either way times the render-parameter fog visibility
		Texture *swSpecTex = nullptr;
		Vec3 swSpecColor = specColor;
		if (spec && !hw)
		{
			specs.reserve(sf.tris.size());
			ImageList *sl = o->SlotList(&f, (int)sf.material, 1);
			swSpecTex = sl ? ListTexture(sl, (int)m.slots[1].image.At(p.dominant), true) : nullptr;
			if (swSpecTex && !swSpecTex->decoded)
				swSpecTex = nullptr;
			if (swSpecTex)
				swSpecColor = Vec3(1, 1, 1);
			swSpecColor = swSpecColor * P->fogVisibility;
		}
		// GFX-modern bloom (SPEC-AMEND-016 R16.7): self-illumination and additive materials as drawn; the
		// specular map pass of a two-pass material is not a light
		float bloomSource = std::max(selfIllum, m.blend == 1 ? 1.0f : 0.0f);
		if (!hwSpecTex)
			BloomSource(g, bloomSource);
		if (g->FlushState)
			g->FlushState();
		// sprite / spriteball nodes face the viewer (about local Y; spriteball also pitches about local X)
		bool billboard = nd.sprite;
		if (billboard)
		{
			Matrix4 N, Ni;
			f.NodeMatrixPose(nd, p.fp, N);
			Ni = MatInverse(N);
			Vec3 v = Normalize(XformPoint(viewerModel, Ni));
			g->PushMatrix();
			GlMatrix(g, N);
			g->Rotatef(-std::atan2(-v.z, -v.x) * 57.2957795f - 90.0f, 0, 1, 0);
			if (nd.spriteball)
				g->Rotatef(std::atan2(-v.y, std::sqrt(v.x * v.x + v.z * v.z)) * 57.2957795f, 1, 0, 0);
			GlMatrix(g, Ni);
		}
		g->Begin(gl::TRIANGLES);
		for (uint16_t ci : sf.tris)
		{
			int pi = geo.corners[ci * 3], ni = geo.corners[ci * 3 + 1], ti = geo.corners[ci * 3 + 2];
			Vec3 pos = pi < 0 ? Vec3(&geo.pos[(size_t)~pi * 3]) : Vec3(&apos[(size_t)pi * 3]);
			Vec3 nrm = ni < 0 ? Vec3(&geo.norm[(size_t)~ni * 3]) : Vec3(&anorm[(size_t)ni * 3]);
			float u = 0, v = 0;
			if (ti < 0)
			{
				u = geo.uv[(size_t)~ti * 2];
				v = geo.uv[(size_t)~ti * 2 + 1];
			}
			else if ((size_t)ti * 2 + 1 < atex.size())
			{
				u = atex[(size_t)ti * 2];
				v = atex[(size_t)ti * 2 + 1];
			}
			if (hw)
				g->Normal3f(nrm.x, nrm.y, nrm.z);
			else
			{
				Vec3 so;
				Vec3 lc = LightVertex(pos, nrm, ambient, lights, spec, viewerModel, expo, level, swSpecColor, so);
				lc += Vec3(selfIllum, selfIllum, selfIllum);
				Vec3 col(ClampF(lc.x * base.x * cmul.x, 0, 1), ClampF(lc.y * base.y * cmul.y, 0, 1),
					ClampF(lc.z * base.z * cmul.z, 0, 1));
				g->Color4f(col.x, col.y, col.z, alpha);
				if (spec)
					specs.push_back(so);
			}
			g->TexCoord2f(u, v);
			g->Vertex3f(pos.x, pos.y, pos.z);
		}
		g->End();
		G().stats.meshes++;
		G().stats.triangles += (int)(sf.tris.size() / 3);
		G().stats.trianglesTotal += (int)(sf.tris.size() / 3);
		// I1c: SPEC-AMEND-006 I1c.9, the base pass of a two-pass hardware-lit material, additive
		if (hwSpecTex)
		{
			g->Disable(gl::POLYGON_OFFSET_FILL);
			if (textured)
			{
				g->Enable(gl::TEXTURE_2D);
				BindTexture(tex, s0.tileU, s0.tileV);
			}
			else
				g->Disable(gl::TEXTURE_2D);
			g->Enable(gl::BLEND);
			g->BlendFunc(gl::ONE, gl::ONE);
			g->DepthMask(0);
			float amb[4] = {base.x, base.y, base.z, opacity};
			g->Materialfv(gl::FRONT_AND_BACK, gl::AMBIENT_AND_DIFFUSE, amb);
			float em[4] = {selfIllum * base.x, selfIllum * base.y, selfIllum * base.z, opacity};
			g->Materialfv(gl::FRONT_AND_BACK, gl::EMISSION, em);
			g->Materialf(gl::FRONT_AND_BACK, gl::SHININESS, 0.0f);
			float zero4[4] = {0.0f, 0.0f, 0.0f, 1.0f};
			g->Materialfv(gl::FRONT_AND_BACK, gl::SPECULAR, zero4);
			BloomSource(g, bloomSource);
			if (g->FlushState)
				g->FlushState();
			g->Begin(gl::TRIANGLES); // a billboard's matrix of the first pass is still on the stack
			for (uint16_t ci : sf.tris)
			{
				int pi = geo.corners[ci * 3], ni = geo.corners[ci * 3 + 1], ti = geo.corners[ci * 3 + 2];
				Vec3 pos = pi < 0 ? Vec3(&geo.pos[(size_t)~pi * 3]) : Vec3(&apos[(size_t)pi * 3]);
				Vec3 nrm = ni < 0 ? Vec3(&geo.norm[(size_t)~ni * 3]) : Vec3(&anorm[(size_t)ni * 3]);
				float u = 0, v = 0;
				if (ti < 0)
				{
					u = geo.uv[(size_t)~ti * 2];
					v = geo.uv[(size_t)~ti * 2 + 1];
				}
				else if ((size_t)ti * 2 + 1 < atex.size())
				{
					u = atex[(size_t)ti * 2];
					v = atex[(size_t)ti * 2 + 1];
				}
				g->Normal3f(nrm.x, nrm.y, nrm.z);
				g->TexCoord2f(u, v);
				g->Vertex3f(pos.x, pos.y, pos.z);
			}
			g->End();
			g->Disable(gl::BLEND);
			g->DepthMask(1);
			g->Disable(gl::POLYGON_OFFSET_FILL);
		}
		// specular pass (slot 1 = specular map when present), additive (SPEC-AMEND-005 I3.7)
		if (spec && !specs.empty())
		{
			if (swSpecTex)
			{
				g->Enable(gl::TEXTURE_2D);
				BindTexture(swSpecTex, m.slots[1].tileU, m.slots[1].tileV);
			}
			else
				g->Disable(gl::TEXTURE_2D);
			g->Enable(gl::BLEND);
			g->BlendFunc(gl::ONE, gl::ONE);
			g->DepthMask(0);
			g->Enable(gl::POLYGON_OFFSET_FILL);
			g->PolygonOffset(G().polyFactor, G().polyUnits);
			if (g->FlushState)
				g->FlushState();
			g->Begin(gl::TRIANGLES);
			size_t k = 0;
			for (uint16_t ci : sf.tris)
			{
				int pi = geo.corners[ci * 3], ti = geo.corners[ci * 3 + 2];
				Vec3 pos = pi < 0 ? Vec3(&geo.pos[(size_t)~pi * 3]) : Vec3(&apos[(size_t)pi * 3]);
				const Vec3 &sc = specs[k++];
				g->Color4f(ClampF(sc.x, 0, 1), ClampF(sc.y, 0, 1), ClampF(sc.z, 0, 1), 1.0f);
				if (ti < 0)
					g->TexCoord2f(geo.uv[(size_t)~ti * 2], geo.uv[(size_t)~ti * 2 + 1]);
				g->Vertex3f(pos.x, pos.y, pos.z);
			}
			g->End();
			// state restored: blend off, (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), offset off, depth writes on (I3.7)
			g->Disable(gl::BLEND);
			g->BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
			g->Disable(gl::POLYGON_OFFSET_FILL);
			g->DepthMask(1);
		}
		if (billboard)
			g->PopMatrix();
		if (uvMatrix)
		{
			g->MatrixMode(gl::TEXTURE);
			g->PopMatrix();
			g->MatrixMode(gl::MODELVIEW);
		}
		if (s0.environment)
		{
			g->Disable(gl::TEXTURE_GEN_S);
			g->Disable(gl::TEXTURE_GEN_T);
		}
		if (fogOff)
			g->Enable(gl::FOG);
	}
	if (hw)
	{
		g->Disable(gl::LIGHTING);
		for (int n = 0; n < 8; n++)
			g->Disable(gl::LIGHT0 + n);
	}
	g->PopMatrix();
}

// I1c: SPEC-AMEND-006 I1c.3 gather pass - the called instance first, then its bolt-ons depth-first in list
// order. An instance (and the bolt-ons reached through it) is visited only when it is on, has a sequence and its
// own model or the model of one of its direct bolt-ons has light nodes. Active nodes (SPEC-AMEND-005 I3.9: part
// shown, visibility track, brightness and multiplier >= 0.01): WORLD_* go to the system's "lights from models"
// list, the others into the shared list of the draw call.
static void GatherLights(Inst *in, DrawCtx &c, const Matrix4 &root)
{
	if (!in->on || !in->seq)
		return;
	bool any = in->obj->hasLights;
	for (auto &ch : in->children)
		if (ch.inst->obj->hasLights)
			any = true;
	if (!any)
		return;
	Pose p;
	if (!in->EvalPose(c.time, p))
		return;
	Matrix4 R = MatMul(in->XF(p), root);
	Matrix4 W = MatMul(R, c.P->entityToWorld);
	c.gathered.push_back(in);
	GhbFile &f = *p.file;
	if (in->obj->hasLights)
	{
		for (int li : f.lights)
		{
			const GhbNode &L = f.nodes[li];
			if (!L.lightParsed || !L.visible.At(p.dominant))
				continue;
			int lpart = in->obj->PartOfNode(&f, li);
			if (lpart && !in->PartShown(lpart))
				continue;
			float br = BlendF(L.brightness, p.fp), mu = BlendF(L.multiplier, p.fp);
			if (br < 0.01f || mu < 0.01f)
				continue;
			Vec3 lp = Blend(L.lightPos, p.fp), col = Blend(L.lightColor, p.fp);
			float st = BlendF(L.attStart, p.fp), en = BlendF(L.attEnd, p.fp);
			if (L.world)
			{
				LightRecord r;
				XformPoint(lp, W).Store(r.origin);
				r.radius = en;
				col.Store(r.color);
				in->sys->modelLights.push_back(r);
			}
			else
			{
				ActiveLight a;
				a.kind = kModelOmni;
				a.pos = XformPoint(lp, W);
				a.color = col;
				a.start = st;
				a.end = en;
				a.mult = mu;
				c.shared.push_back(a);
			}
		}
	}
	for (auto &ch : in->children)
		GatherLights(ch.inst, c, in->ChildRoot(c.time, R, ch.bolt));
}

static void DrawLevel(Inst *in, DrawCtx &c, const Matrix4 &root)
{
	Pose p;
	if (!in->EvalPose(c.time, p))
	{
		in->rc.Reset();
		return;
	}
	Matrix4 R = MatMul(in->XF(p), root);
	Matrix4 W = MatMul(R, c.P->entityToWorld);
	const BoundsRec &b = p.file->bounds;
	bool culled = false;
	if (!b.infinite && !b.empty)
	{
		float corners[8][3];
		for (int i = 0; i < 8; i++)
			XformPoint(Vec3((i & 1) ? b.mx.x : b.mn.x, (i & 2) ? b.mx.y : b.mn.y, (i & 4) ? b.mx.z : b.mn.z), W)
				.Store(corners[i]);
		culled = c.P->BoxOutside(corners);
	}
	if (!culled)
		DrawOwn(in, c, p, W);
	for (auto &ch : in->children)
		DrawLevel(ch.inst, c, in->ChildRoot(c.time, R, ch.bolt));
}

// world-space box of the whole hierarchy (shadow size)
static void LevelBox(Inst *in, float t, const Matrix4 &root, const Matrix4 &e2w, Box3 &box)
{
	Pose p;
	if (!in->EvalPose(t, p))
		return;
	Matrix4 R = MatMul(in->XF(p), root);
	box.AddBox(p.file->bounds, MatMul(R, e2w));
	for (auto &ch : in->children)
		LevelBox(ch.inst, t, in->ChildRoot(t, R, ch.bolt), e2w, box);
}

// ------------------------------------------------------------------ shadows (SPEC-012 section 8, SPEC-411 section 7)

static uint32_t g_shadowTex[16];
static int g_shadowRing = 0;
static uint32_t g_blobTex = 0;

// SPEC-AMEND-005 I3.8 item 4: the quad over the (grown) touched rectangle; corners in mask units mapped back to world
// space, shifted horizontally by (origin - light point) (+ the directional vector when shadow-only) and put on the
// ground; g = 16 x the trace fraction grows the first pair of corners on both axes, the second pair on u only (quirk)
struct ShadowQuad
{
	Vec3 pos[4];
	float st[4][2];
};

static void DrawShadowQuad(DrawCtx &c, uint32_t tex, float alpha, const ShadowQuad &q, const Vec3 &shift, bool blob)
{
	const GhoulGLTable *g = c.g;
	g->BindTexture(gl::TEXTURE_2D, tex);
	g->Color4f(1, 1, 1, alpha);
	static const float blobST[4][2] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
	auto vtx = [&](int i) {
		const float *st = blob ? blobST[i] : q.st[i];
		g->TexCoord2f(st[0], st[1]);
		g->Vertex3f(q.pos[i].x + shift.x, q.pos[i].y + shift.y, q.pos[i].z + shift.z);
	};
	g->Begin(gl::TRIANGLES);
	vtx(0);
	vtx(1);
	vtx(2);
	vtx(0);
	vtx(2);
	vtx(3);
	g->End();
}

static void Shadow(Inst *in, DrawCtx &c)
{
	GhoulRenderParams *P = c.P;
	const GhoulGLTable *g = c.g;
	if (!P->worldTrace)
		return;
	Vec3 org(P->origin);
	float start[3], end[3];
	org.Store(start);
	(org - Vec3(0, 0, 500)).Store(end);
	float f = P->worldTrace(start, end);
	if (f < 0.0f)
		return;
	float dark = P->shadowDarkness * (1.0f - f);
	float ground = org.z - 495.0f * f;
	RenderCache &rc = in->rc;
	if (P->useGLLights && !c.shadowOnly)
	{
		std::vector<const LightRecord *> wl;
		in->sys->world.Query(org, wl);
		if (!wl.empty())
		{
			Vec3 d = Normalize(Vec3(wl[0]->origin) - org);
			if (d.z >= -0.95f)
			{
				if (d.z < 0.75f)
				{
					d.z = 0.75f;
					float xy = std::sqrt(d.x * d.x + d.y * d.y);
					float k = xy > 1e-6f ? std::sqrt(0.4375f) / xy : 0.0f;
					d.x *= k;
					d.y *= k;
				}
				rc.shadowDir = rc.shadowDir * 0.95f + d * 0.05f;
			}
		}
	}
	float k = ClampF(24.75f * f, 0.0f, 1.0f);
	Vec3 lightPoint = org + Normalize(rc.shadowDir * k + Vec3(0, 0, 1.0f - k)) * P->shadowDist;
	// SPEC-AMEND-005 I3.8 item 6: darkness also times the square of the instance's tint alpha
	float ta = in->tint[3] / 255.0f;
	dark *= ta * ta;
	Box3 box;
	Matrix4 id;
	MatIdentity(id);
	LevelBox(in, c.time, id, P->entityToWorld, box);
	float ext = 0;
	if (box.any)
		for (int a = 0; a < 3; a++)
			ext = std::max(ext, box.mx[a] - box.mn[a]);
	float size = std::min(0.25f * ext, 60.0f);
	if (size <= 0.0f)
		return;
	// item 1: basis a = normalise(origin - light point) ((1, 0, 0) when tiny); X = normalise(a x e) with e the axis
	// giving the longest cross product (X, Y, Z; a later axis only when strictly longer); Y = normalise(a x X)
	Vec3 a = org - lightPoint;
	a = Dot(a, a) < 0.01f ? Vec3(1, 0, 0) : Normalize(a);
	Vec3 axes[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
	Vec3 X;
	float best = -1.0f;
	for (auto &e : axes)
	{
		Vec3 cr = Cross(a, e);
		float l2 = Dot(cr, cr);
		if (l2 > best)
		{
			best = l2;
			X = cr;
		}
	}
	X = Normalize(X);
	Vec3 Y = Normalize(Cross(a, X));
	c.shadowDir = a;
	c.groundZ = ground;
	c.shadowOrigin = org;
	c.shadowU = X;
	c.shadowV = Y;
	c.shadowScale = 32.0f / size;

	// item 2: every posed vertex of every drawn mesh (bolt-ons into the same mask) gives a mask point; the touched
	// rectangle decides the quad in both modes
	std::memset(c.mask, 0, sizeof(c.mask));
	c.mx0 = c.my0 = 64;
	c.mx1 = c.my1 = -1;
	c.wantMask = true;
	bool so = c.shadowOnly;
	c.shadowOnly = true;
	struct Walk
	{
		static void Go(Inst *i, DrawCtx &c, const Matrix4 &root)
		{
			Pose p;
			if (!i->EvalPose(c.time, p))
				return;
			Matrix4 R = MatMul(i->XF(p), root);
			DrawOwn(i, c, p, MatMul(R, c.P->entityToWorld));
			for (auto &ch : i->children)
				Go(ch.inst, c, i->ChildRoot(c.time, R, ch.bolt));
		}
	};
	Matrix4 root;
	MatIdentity(root);
	Walk::Go(in, c, root);
	c.shadowOnly = so;
	c.wantMask = false;
	if (c.mx1 < 0)
		return; // no vertex: nothing is drawn

	g->Enable(gl::TEXTURE_2D);
	g->Enable(gl::BLEND);
	g->BlendFunc(gl::ZERO, gl::ONE_MINUS_SRC_ALPHA);
	g->DepthMask(0);
	g->Enable(gl::POLYGON_OFFSET_FILL);
	g->PolygonOffset(-3.0f, -3.0f);
	g->Enable(gl::ALPHA_TEST);
	g->AlphaFunc(gl::GREATER, 0.0f);
	g->Disable(gl::CULL_FACE);
	g->TexEnvf(gl::TEXTURE_ENV, gl::TEXTURE_ENV_MODE, (float)gl::MODULATE);

	// item 3: the blur stays inside the touched rectangle; the drawn rectangle then grows by one texel
	if (P->shadowMode == 2)
	{
		int x0 = std::max(1, c.mx0), y0 = std::max(1, c.my0), x1 = std::min(62, c.mx1), y1 = std::min(62, c.my1);
		for (int pass = 0; pass < 2; pass++)
		{
			uint8_t tmp[64 * 64];
			std::memcpy(tmp, c.mask, sizeof(tmp));
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++)
				{
					int sum = 0;
					for (int dy = -1; dy <= 1; dy++)
						for (int dx = -1; dx <= 1; dx++)
							sum += tmp[(y + dy) * 64 + x + dx];
					sum /= 8;
					c.mask[y * 64 + x] = (uint8_t)(sum > 255 ? 255 : sum);
				}
		}
	}
	int u0 = c.mx0 - 32 - 1, u1 = c.mx1 - 32 + 1, v0 = c.my0 - 32 - 1, v1 = c.my1 - 32 + 1;
	float gq = 16.0f * f;
	ShadowQuad q;
	float mu[4][2] = {{u0 - gq, v0 - gq}, {u0 - gq, v1 + gq}, {u1 + gq, v1 + 1.0f}, {u1 + gq, v0 - 1.0f}};
	if (P->shadowMode == 1)
	{
		// OpenMerc (fb3): the blob spreads past the silhouette it is fitted to (x 1.5, the thin side at least 60 %
		// of the long one), so its soft edge shows around the feet / body instead of only under it
		float cu = 0.5f * (mu[0][0] + mu[2][0]), cv = 0.5f * (mu[0][1] + mu[1][1]);
		float hu = 0.5f * (mu[2][0] - mu[0][0]), hv = 0.5f * (mu[1][1] - mu[0][1]);
		float hm = std::max(hu, hv);
		hu = std::max(hu * 1.5f, hm * 0.9f);
		hv = std::max(hv * 1.5f, hm * 0.9f);
		const float sg[4][2] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};
		for (int i = 0; i < 4; i++)
		{
			mu[i][0] = cu + sg[i][0] * hu;
			mu[i][1] = cv + sg[i][1] * hv;
		}
	}
	const int tu[4][2] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
	Vec3 hshift = org - lightPoint;
	if (c.shadowOnly)
		hshift = hshift + Vec3(P->lightDir[0], P->lightDir[1], 0.0f);
	for (int i = 0; i < 4; i++)
	{
		Vec3 w = org + (X * mu[i][0] + Y * mu[i][1]) * (1.0f / c.shadowScale);
		q.pos[i] = Vec3(w.x + hshift.x, w.y + hshift.y, ground);
		q.st[i][0] = tu[i][0] / 64.0f + 0.5f;
		q.st[i][1] = tu[i][1] / 64.0f + 0.5f;
	}
	if (P->shadowMode == 2)
	{
		uint32_t &t = g_shadowTex[g_shadowRing];
		g_shadowRing = (g_shadowRing + 1) & 15;
		if (!t)
			g->GenTextures(1, &t);
		g->BindTexture(gl::TEXTURE_2D, t);
		g->TexImage2D(gl::TEXTURE_2D, 0, gl::ALPHA, 64, 64, 0, gl::ALPHA, gl::UNSIGNED_BYTE, c.mask);
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP);
		g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP);
		if (g->FlushState)
			g->FlushState();
		// item 5: three passes, unshifted and by (-1, -1, 0) / (+1, +1, 0) world units, alpha darkness x 0.3333
		DrawShadowQuad(c, t, dark * 0.3333f, q, Vec3(0, 0, 0), false);
		DrawShadowQuad(c, t, dark * 0.3333f, q, Vec3(-1, -1, 0), false);
		DrawShadowQuad(c, t, dark * 0.3333f, q, Vec3(1, 1, 0), false);
	}
	else if (P->shadowMode == 1)
	{
		if (!g_blobTex)
		{
			uint8_t blob[64 * 64];
			for (int y = -32; y < 32; y++)
				for (int x = -32; x < 32; x++)
				{
					// SPEC-012 blob is a 1/2048 cubic (mean alpha ~0.4 over the quad, its dark core hidden under
					// the body: owner fb3 "barely visible"). OpenMerc (DEVIATIONS): a full core to radius 9 that
					// eases (smoothstep) to 0 at radius 31, so the quad's edge draws no outline and the visible
					// ring around the feet / corpse carries the shadow
					float r = std::sqrt((float)(x * x + y * y));
					float e = (31.0f - r) / 22.0f;
					e = e < 0.0f ? 0.0f : (e > 1.0f ? 1.0f : e);
					int al = (int)(255.0f * e * e * (3.0f - 2.0f * e));
					blob[(y + 32) * 64 + x + 32] = (uint8_t)(al < 0 ? 0 : (al > 255 ? 255 : al));
				}
			g->GenTextures(1, &g_blobTex);
			g->BindTexture(gl::TEXTURE_2D, g_blobTex);
			g->TexImage2D(gl::TEXTURE_2D, 0, gl::ALPHA, 64, 64, 0, gl::ALPHA, gl::UNSIGNED_BYTE, blob);
			g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
			g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
			g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP);
			g->TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP);
		}
		if (g->FlushState)
			g->FlushState();
		// item 5: the same corners once, texture coordinates (0, 0), (0, 1), (1, 1), (1, 0), alpha = darkness
		DrawShadowQuad(c, g_blobTex, dark, q, Vec3(0, 0, 0), true);
	}
	// restore
	g->Disable(gl::POLYGON_OFFSET_FILL);
	g->DepthMask(1);
	g->Disable(gl::BLEND);
	g->Disable(gl::ALPHA_TEST);
	g->Enable(gl::CULL_FACE);
	g->BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
	g->Color4f(1, 1, 1, 1);
}

// the shadow textures die with the GL context: system method 3 (ReleaseTextures, called on every renderer
// restart while the old context still exists) deletes them. Keeping the names made the blob / mask draw with a
// name the new context had given to another texture (opaque alpha): dark squares after vid_restart (fb2)
void ReleaseShadowTextures(const GhoulGLTable *g)
{
	for (auto &t : g_shadowTex)
	{
		if (t && g)
			g->DeleteTextures(1, &t);
		t = 0;
	}
	if (g_blobTex && g)
		g->DeleteTextures(1, &g_blobTex);
	g_blobTex = 0;
	g_shadowRing = 0;
}

void ForgetShadowTextures()
{
	std::memset(g_shadowTex, 0, sizeof(g_shadowTex));
	g_blobTex = 0;
}

void Inst::Draw(GhoulRenderParams *P)
{
	const GhoulGLTable *g = G().gl;
	if (!P || !g)
		return;
	if (!on || !seq)
	{
		rc.Reset();
		return;
	}
	g_drawRole = sys->role;
	DrawCtx c;
	c.P = P;
	c.g = g;
	c.sys = sys;
	c.time = P->time;
	bool casts = P->shadowMode != 0 && (obj->character || P->entityFlag1000);
	c.shadowOnly = P->shadowOnly && casts;
	Matrix4 root;
	MatIdentity(root);
	// I1c: smooth shading for the draw, flat again afterwards (black-box GL trace of the original Ghoul
	// system: ShadeModel(SMOOTH) first, ShadeModel(FLAT) last in every instance draw)
	g->ShadeModel(gl::SMOOTH);
	if (!P->noDynamicLights && !c.shadowOnly)
		GatherLights(this, c, root); // I1c: SPEC-AMEND-006 I1c.3
	if (!c.shadowOnly)
		DrawLevel(this, c, root);
	if (casts)
		Shadow(this, c);
	// leave the state the renderer expects: no blending, depth writes, back-face culling, white
	g->Disable(gl::BLEND);
	g->Disable(gl::ALPHA_TEST);
	g->Disable(gl::POLYGON_OFFSET_FILL);
	g->DepthMask(1);
	g->Enable(gl::CULL_FACE);
	g->CullFace(gl::BACK);
	g->Enable(gl::TEXTURE_2D);
	g->Color4f(1, 1, 1, 1);
	g->ShadeModel(gl::FLAT); // I1c
}

} // namespace ghoul
