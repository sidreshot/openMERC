// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - pose sampling of stock models: draws instances through the Ghoul draw path into
// the software GL table, writes PNG contact sheets, and exports the posed triangles as OBJ and glTF.
// Implements: SPEC-012 sections 4 and 8, SPEC-010 D / SPEC-121 (.gpm skins, parts, bolt-ons) (verification)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "../ghoul_internal.h"
#include "test_common.h"

using namespace ghoul;

void SoftGL_SetCapture(SoftGL *s, bool on);
void SoftGL_Resize(SoftGL *s, int w, int h);
const std::vector<PosedTri> &SoftGL_Captured(SoftGL *s);
bool SoftGL_TextureImage(SoftGL *s, unsigned name, Image &out);

namespace {

float g_ground = 0.0f;

float GroundTrace(const float *start, const float *end)
{
	float dz = start[2] - end[2];
	if (dz <= 0)
		return 1.0f;
	float f = (start[2] - g_ground) / dz;
	return f < 0 ? -1.0f : (f > 1 ? 1.0f : f);
}

struct Params : public GhoulRenderParams
{
	bool BoxOutside(const float (*)[3]) override { return false; }
	Params()
	{
		std::memset((char *)this + sizeof(void *), 0, sizeof(*this) - sizeof(void *));
		lightColor[0] = lightColor[1] = lightColor[2] = 1.0f;
		fogVisibility = 1.0f;
		maxLights = 3;
		useGLLights = 1;
		specular = 1;
		shadowDarkness = 0.75f;
		shadowDist = 25.0f;
		shadowMode = 2;
		worldTrace = GroundTrace;
		MatIdentity(entityToWorld);
	}
};

void LookAt(const Vec3 &eye, const Vec3 &target, float view[16])
{
	Vec3 f = Normalize(target - eye), up(0, 0, 1);
	Vec3 r = Normalize(Cross(f, up));
	Vec3 u = Cross(r, f);
	// column-major, rows r, u, -f
	float m[16] = {r.x, u.x, -f.x, 0, r.y, u.y, -f.y, 0, r.z, u.z, -f.z, 0, -Dot(r, eye), -Dot(u, eye), Dot(f, eye), 1};
	std::memcpy(view, m, sizeof(m));
}

void Perspective(float fovy, float aspect, float n, float fz, float p[16])
{
	float t = 1.0f / std::tan(fovy * 3.14159265f / 360.0f);
	float m[16] = {t / aspect, 0, 0, 0, 0, t, 0, 0, 0, 0, (fz + n) / (n - fz), -1, 0, 0, 2 * fz * n / (n - fz), 0};
	std::memcpy(p, m, sizeof(m));
}

SoftGL *g_gl = nullptr;
bool g_axes = false;
bool g_flag1000 = false;
bool g_hwlights = false; // I4c: ghl_light_method > 2.5 with ghl_gllight_attenuation 1

void Blit(Image &dst, const Image &src, int x0, int y0)
{
	for (int y = 0; y < src.h && y + y0 < dst.h; y++)
		std::memcpy(&dst.rgb[((size_t)(y + y0) * dst.w + x0) * 3], &src.rgb[(size_t)y * src.w * 3], (size_t)src.w * 3);
}

// draw one instance; returns triangles drawn
int DrawInto(IGhoulInst *in, float t, const Vec3 &eye, const Vec3 &target, Image &out, bool ground, float fov = 40.0f)
{
	float view[16], proj[16];
	LookAt(eye, target, view);
	Perspective(fov, (float)out.w / out.h, 1.0f, 5000.0f, proj);
	SoftGL_Resize(g_gl, out.w, out.h);
	uint8_t clear[3] = {72, 84, 104};
	SoftGL_Begin(g_gl, view, proj, clear);
	const GhoulGLTable *g = SoftGL_Table();
	if (ground)
	{
		// a grey floor at z = g_ground so the projected shadow is visible
		g->Disable(0x0DE1);
		g->Disable(0x0B44);
		g->Color4f(0.55f, 0.55f, 0.5f, 1);
		g->Begin(4);
		float s = 400;
		float q[6][2] = {{-s, -s}, {s, -s}, {s, s}, {-s, -s}, {s, s}, {-s, s}};
		for (auto &v : q)
			g->Vertex3f(target.x + v[0], target.y + v[1], g_ground);
		g->End();
		g->Enable(0x0DE1);
		g->Enable(0x0B44);
	}
	Params P;
	P.time = t;
	P.entityFlag1000 = g_flag1000 ? 1 : 0;
	P.hardwareLights = g_hwlights ? 1 : 0;
	P.glAttenuation = g_hwlights ? 1 : 0;
	eye.Store(P.viewOrigin);
	Normalize(target - eye).Store(P.viewForward);
	P.origin[0] = P.origin[1] = 0;
	P.origin[2] = 0;
	in->Draw(&P);
	if (g_axes)
	{
		// axis markers at the model origin: X red, Y green, Z blue (40 units)
		g->Disable(0x0DE1);
		g->Disable(0x0B44);
		g->Disable(0x0B71);
		float cols[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		for (int a = 0; a < 3; a++)
		{
			g->Color4f(cols[a][0], cols[a][1], cols[a][2], 1);
			g->Begin(4);
			float p0[3] = {0, 0, 0}, p1[3] = {0, 0, 0}, p2[3] = {0, 0, 0};
			p1[a] = 40;
			p2[a] = 40;
			p2[(a + 1) % 3] = 1.5f;
			p0[(a + 2) % 3] = 1.5f;
			g->Vertex3f(p0[0], p0[1], p0[2]);
			g->Vertex3f(p1[0], p1[1], p1[2]);
			g->Vertex3f(p2[0], p2[1], p2[2]);
			g->End();
		}
		g->Enable(0x0DE1);
		g->Enable(0x0B44);
		g->Enable(0x0B71);
	}
	out = SoftGL_Image(g_gl);
	return SoftGL_Triangles(g_gl);
}

// world-space triangles of the posed instance (view = identity)
std::vector<PosedTri> Capture(IGhoulInst *in, float t)
{
	float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	uint8_t c[3] = {0, 0, 0};
	Image keep = SoftGL_Image(g_gl);
	SoftGL_Begin(g_gl, id, id, c);
	SoftGL_SetCapture(g_gl, true);
	Params P;
	P.time = t;
	P.shadowMode = 0;
	in->Draw(&P);
	SoftGL_SetCapture(g_gl, false);
	return SoftGL_Captured(g_gl);
}

void Export(const std::string &stem, const std::vector<PosedTri> &tris)
{
	WriteOBJ(stem + ".obj", tris);
	WriteGLTF(stem + ".gltf", tris);
	std::set<int> mats;
	for (auto &t : tris)
		mats.insert(t.material);
	std::string base = stem.substr(stem.find_last_of("/\\") + 1);
	for (int m : mats)
	{
		Image img;
		if (m && SoftGL_TextureImage(g_gl, (unsigned)m, img))
			WritePNG(stem + "_tex" + std::to_string(m) + ".png", img);
	}
}

// a player model from a .gpm (or .gbm): object, skins, parts, bolt-ons
struct Built
{
	IGhoulObject *obj = nullptr;
	IGhoulInst *inst = nullptr;
	std::vector<IGhoulObject *> extra;
	std::string dir;
};

Built BuildPlayer(IGhoulSystem *sys, const std::string &gpm, bool menuModel)
{
	Built b;
	IGhoulPlayerModel *pm = (IGhoulPlayerModel *)OMGhoul_NewPlayerModel(gpm.c_str());
	if (!pm->IsLoaded())
	{
		pm->Destroy(1);
		return b;
	}
	b.dir = pm->GetModelDir();
	std::string base = pm->GetBaseModel();
	std::string menu = pm->GetMenuSequence();
	b.obj = sys->CreateObject();
	if (menuModel || base.empty())
		b.obj->RegisterSequence(menu.c_str(), false);
	else
		b.obj->RegisterSequence(("ghoul/" + b.dir + "/" + base + ".ghl").c_str(), false);
	OMGhoul_RegisterList(b.dir.c_str(), pm->GetSequenceList(), b.obj);
	b.obj->FinishRegistration(false);
	std::vector<std::pair<int, std::pair<int, int>>> skins;
	for (int i = 1; i <= pm->NumSkins(); i++)
	{
		PlayerSkinEntry e;
		pm->GetSkinEntry(i, &e);
		GhoulID m = b.obj->FindMaterial(e.material);
		GhoulID s = m ? b.obj->RegisterSkin(m, e.skin, 0) : 0;
		GhoulID part = e.part[0] ? b.obj->FindPart(e.part) : 0;
		if (m && s)
			skins.push_back(std::make_pair((int)m, std::make_pair((int)s, (int)part)));
	}
	b.obj->Prepare(true);
	b.inst = b.obj->CreateInstance();
	for (auto &s : skins)
		b.inst->SetSkin((GhoulID)s.first, (GhoulID)s.second.first, (GhoulID)s.second.second);
	OMGhoul_PartsOff(b.dir.c_str(), (std::string(pm->GetSequenceList()) + "_poff").c_str(), b.obj, b.inst);
	for (int i = 1; i <= pm->NumPartSwitches(); i++)
	{
		PlayerPartEntry e;
		pm->GetPartEntry(i, &e);
		GhoulID part = b.obj->FindPart(e.part);
		if (part)
			b.inst->SetPartOnOff(part, e.shown != 0);
	}
	for (int i = 1; i <= pm->NumBoltons(); i++)
	{
		PlayerBoltonEntry e;
		pm->GetBoltonEntry(i, &e);
		Built c = BuildPlayer(sys, e.file, true);
		if (!c.obj)
			continue;
		b.extra.push_back(c.obj);
		for (auto *x : c.extra)
			b.extra.push_back(x);
		c.inst->Play(1, 0.0f, 0.0f, true, 0, false, false);
		if (e.scale != 1.0f && e.scale > 0)
		{
			Matrix4 m;
			MatIdentity(m);
			m.m[0][0] = m.m[1][1] = m.m[2][2] = e.scale;
			m.flags = 0;
			c.inst->SetTransform(&m);
		}
		b.inst->AttachChild(b.obj->FindPart(e.bolt), c.inst, c.obj->FindPart(e.boltonBolt));
	}
	pm->Destroy(1);
	return b;
}

void Destroy(Built &b)
{
	if (b.obj)
		b.obj->Destroy();
	for (auto *x : b.extra)
		x->Destroy();
	b = Built();
}

// contact sheet: one cell per sample time
struct Sheet
{
	Image img;
	int cw, ch, cols, n = 0;
	Sheet(int cells, int cellW, int cellH, int c) : cw(cellW), ch(cellH), cols(c)
	{
		int rows = (cells + c - 1) / c;
		img.Init(cellW * c, cellH * rows, 0, 0, 0);
	}
	void Add(const Image &cell)
	{
		Blit(img, cell, (n % cols) * cw, (n / cols) * ch);
		n++;
	}
};

struct Framing
{
	Vec3 eye, target;
};

std::vector<PosedTri> Capture(IGhoulInst *in, float t);

// frame the camera on the posed triangles at time t
Framing Frame(IGhoulInst *in, float t, const Vec3 &dir, float zoom, float &minZ)
{
	std::vector<PosedTri> tris = Capture(in, t);
	Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
	for (auto &tr : tris)
		for (int k = 0; k < 3; k++)
			for (int a = 0; a < 3; a++)
			{
				mn[a] = std::min(mn[a], tr.p[k][a]);
				mx[a] = std::max(mx[a], tr.p[k][a]);
			}
	if (tris.empty())
		mn = mx = Vec3();
	Vec3 c = (mn + mx) * 0.5f;
	float ext = std::max(mx[0] - mn[0], std::max(mx[1] - mn[1], mx[2] - mn[2]));
	Framing f;
	f.target = c;
	f.eye = c + Normalize(dir) * (ext * 1.45f / zoom + 2.0f);
	minZ = mn[2];
	return f;
}

int g_cells = 0, g_exports = 0;
std::string g_out;

// render 'seq' of 'in' at n evenly spaced times; returns triangles of the first pose
bool g_eyeView = false; // first-person: camera at the model origin looking along -Y (stock in-view models face -Y)

int SampleSequence(const std::string &name, IGhoulObject *o, IGhoulInst *in, GhoulID seq, int n, const Vec3 &viewDir,
	bool ground, int cellW = 256, int cellH = 320, float zoom = 1.0f, bool exportFirst = true)
{
	float d = in->SequenceDuration(seq, 0, nullptr);
	in->Play(seq, 0.0f, 0.0f, true, 0, false, false);
	Sheet sheet(n, cellW, cellH, std::min(n, 4));
	int tris0 = 0;
	float minZ = 0;
	Framing fr = Frame(in, 0.0f, viewDir, zoom, minZ);
	g_ground = ground ? minZ : fr.target.z - 1000.0f;
	if (g_eyeView)
	{
		fr.eye = Vec3(0, 0, 0);
		fr.target = Vec3(0, -100, 0);
	}
	for (int k = 0; k < n; k++)
	{
		float t = n > 1 ? d * k / n : 0.0f;
		Image cell;
		cell.Init(cellW, cellH, 0, 0, 0);
		int tris = DrawInto(in, t, fr.eye, fr.target, cell, ground, g_eyeView ? 75.0f : 40.0f);
		if (k == 0)
			tris0 = tris;
		sheet.Add(cell);
		g_cells++;
		if (k == 0 && exportFirst)
		{
			Export(g_out + "/" + name + "_f0", Capture(in, t));
			g_exports++;
		}
	}
	WritePNG(g_out + "/" + name + ".png", sheet.img);
	(void)o;
	return tris0;
}

} // namespace

void RunRenderTests(const std::string &outDir, bool quick)
{
	std::printf("== pose sampling, drawing and exports (SPEC-012 sections 4, 8) -> %s\n", outDir.c_str());
	g_out = outDir;
	IGhoulSystem *cl = (IGhoulSystem *)OMGhoul_GetSystem(OM_GHOUL_CLIENT);
	cl->Reset();
	g_gl = SoftGL_Create(256, 320);
	unsigned char gamma[256];
	for (int i = 0; i < 256; i++)
		gamma[i] = (unsigned char)i;
	cl->SetGamma(gamma);
	cl->SetGLTable(SoftGL_Table());
	cl->SetSpecular(true);
	cl->BeginWorldLights();
	LightRecord l = {{80, -60, 90}, 400, {1.0f, 0.95f, 0.85f}};
	cl->AddWorldLight(&l);
	LightRecord l2 = {{-60, 80, 40}, 300, {0.35f, 0.4f, 0.55f}};
	cl->AddWorldLight(&l2);
	cl->FinishWorldLights();

	// 1. characters from .gpm files, with skins, part switches and the menu weapon bolt-on
	struct Ch
	{
		const char *gpm;
		const char *seqs[3];
	} chars[] = {
		{"Dekker.gpm", {"std_ialert_n_p_n", "std_lrun_fwd_l_2", "std_ddeathfwd_n_a_n"}},
		{"Princess.gpm", {"std_ialert_n_p_n", "std_lrun_fwd_l_2", nullptr}},
		{"Amu.gpm", {"std_ialert_n_p_n", nullptr, nullptr}},
	};
	for (auto &c : chars)
	{
		Built b = BuildPlayer(cl, c.gpm, false);
		CHECK(b.obj && b.obj->NumSequences() > 0, "player model %s", c.gpm);
		if (!b.obj)
			continue;
		int any = 0;
		if (!std::strcmp(c.gpm, "Dekker.gpm"))
		{
			GhoulID seq = b.obj->FindSequence("ghoul/enemy/meso/std_ialert_n_p_n.ghl");
			if (seq)
			{
				b.inst->Play(seq, 0.0f, 0.0f, true, 0, false, false);
				Vec3 views[4] = {Vec3(1, 0, 0.15f), Vec3(0, -1, 0.15f), Vec3(-1, 0, 0.15f), Vec3(0.05f, 0.0f, 1)};
				Sheet sheet(4, 256, 320, 4);
				g_axes = true;
				for (auto &v : views)
				{
					float minZ;
					Framing fr = Frame(b.inst, 0.0f, v, 1.0f, minZ);
					g_ground = minZ;
					Image cell;
					cell.Init(256, 320, 0, 0, 0);
					DrawInto(b.inst, 0.0f, fr.eye, fr.target, cell, false);
					sheet.Add(cell);
				}
				g_axes = false;
				WritePNG(g_out + "/Dekker_orientation_views_px_my_mx_top.png", sheet.img);

				// I4c, SPEC-AMEND-005 I4b.5: with hardware lights and GL attenuation the world lights of radius
				// 400 and 300 get GL_LINEAR_ATTENUATION 1/400 and 1/300 (not 0.75/R)
				{
					float minZ;
					Framing fr = Frame(b.inst, 0.0f, views[0], 1.0f, minZ);
					Image cell;
					cell.Init(256, 320, 0, 0, 0);
					SoftGL_TakeLinearAttenuations();
					g_hwlights = true;
					DrawInto(b.inst, 0.0f, fr.eye, fr.target, cell, false);
					g_hwlights = false;
					std::vector<float> att = SoftGL_TakeLinearAttenuations();
					bool r300 = false, r400 = false, old = false;
					for (float a : att)
					{
						r300 = r300 || std::fabs(a - 1.0f / 300.0f) < 1e-7f;
						r400 = r400 || std::fabs(a - 1.0f / 400.0f) < 1e-7f;
						// the old 0.75/R: 0.0025 for R = 300 (equal to the new 1/400) and 0.001875 for R = 400
						old = old || std::fabs(a - 0.75f / 400.0f) < 1e-7f;
					}
					for (float a : att)
						std::printf("   GL_LINEAR_ATTENUATION %g (1/%g)\n", a, a != 0 ? 1.0f / a : 0.0f);
					CHECK(r300 && r400 && !old, "world-light GL attenuation 1/R (%d values)", (int)att.size());
				}
			}
		}
		for (const char *s : c.seqs)
		{
			if (!s || (quick && any))
				continue;
			GhoulID seq = b.obj->FindSequence(("ghoul/" + b.dir + "/" + s + ".ghl").c_str());
			if (!seq)
			{
				std::printf("   %s: sequence %s not present\n", c.gpm, s);
				continue;
			}
			std::string name = std::string(c.gpm).substr(0, std::strlen(c.gpm) - 4) + "_" + s;
			int tris = SampleSequence(name, b.obj, b.inst, seq, 8, Vec3(1.0f, -0.6f, 0.35f), true);
			CHECK(tris > 300, "%s: %d triangles", name.c_str(), tris);
			std::printf("   %s: %d triangles, %d bolt-ons\n", name.c_str(), tris, b.inst->NumChildren());
			any++;
		}
		Destroy(b);
	}

	// 2. first-person weapon (in view space), reload sequence
	{
		IGhoulObject *w = cl->CreateObject();
		OMGhoul_RegisterList("weapon/inview/pistol1", "pistol1", w);
		w->FinishRegistration(true);
		w->Prepare(true);
		IGhoulInst *in = w->CreateInstance();
		GhoulID seq = w->FindSequence("ghoul/weapon/inview/pistol1/reload_a.ghl");
		if (seq)
		{
			g_eyeView = true;
			int tris = SampleSequence("pistol1_reload_a", w, in, seq, 8, Vec3(-1.0f, 0.3f, 0.25f), false, 320, 240);
			GhoulID fire = w->FindSequence("ghoul/weapon/inview/pistol1/fire1_a.ghl");
			if (fire)
				SampleSequence("pistol1_fire1_a", w, in, fire, 4, Vec3(-1.0f, 0.3f, 0.25f), false, 320, 240, 1.0f, false);
			g_eyeView = false;
			CHECK(tris > 100, "pistol1 triangles %d", tris);
		}
		w->Destroy();
	}

	// 3. objects: static, int16-animated, chunk-animated, rigid arm, blinking material, model lights
	struct Ob
	{
		const char *dir, *list, *name;
		Vec3 view;
	} obs[] = {
		{"objects/generic/trashcan_barrel", "trashcan", "trashcan", Vec3(1, -0.7f, 0.6f)},
		{"enemy/chopper", "rotor_start", "rotor_start", Vec3(0.6f, -0.6f, 1.0f)},
		{"objects/bosnia/searchlight", "arm", "searchlight_arm", Vec3(1, -1, 0.5f)},
		{"objects/light/emergency", "emergency", "emergency", Vec3(1, -0.5f, 0.5f)},
		{"enemy/chopper", "nav_lite", "nav_lite", Vec3(1, -0.5f, 0.5f)},
		{"enemy/bolt", "w_assault_rifle", "w_assault_rifle", Vec3(0.3f, -1, 0.4f)},
	};
	for (auto &ob : obs)
	{
		IGhoulObject *o = cl->CreateObject();
		OMGhoul_RegisterList(ob.dir, ob.list, o);
		if (!o->NumSequences())
		{
			std::printf("   %s: no sequences\n", ob.name);
			o->Destroy();
			continue;
		}
		o->FinishRegistration(true);
		o->Prepare(true);
		IGhoulInst *in = o->CreateInstance();
		cl->ClearLights();
		int tris = SampleSequence(ob.name, o, in, 1, quick ? 2 : 8, ob.view, false);
		CHECK(tris > 0, "%s triangles %d", ob.name, tris);
		std::printf("   %s: %d sequences, %d triangles, model lights exported %d\n", ob.name, o->NumSequences(), tris,
			cl->ModelLightCount());
		o->Destroy();
	}

	// billboards: the candle's SPRITEBALL flame and flash face the camera from every side
	{
		IGhoulObject *o = cl->CreateObject();
		OMGhoul_RegisterList("objects/light/candle", "candle", o);
		if (o->NumSequences())
		{
			o->FinishRegistration(true);
			o->Prepare(true);
			IGhoulInst *in = o->CreateInstance();
			in->Play(1, 0.0f, 0.0f, true, 0, false, false);
			Vec3 views[4] = {Vec3(1, 0, 0.3f), Vec3(0, -1, 0.3f), Vec3(-1, 0.3f, 0.3f), Vec3(0.2f, 0.1f, 1)};
			Sheet sheet(4, 200, 200, 4);
			for (auto &v : views)
			{
				float minZ;
				Framing fr = Frame(in, 0.0f, v, 1.0f, minZ);
				Image cell;
				cell.Init(200, 200, 0, 0, 0);
				DrawInto(in, 0.0f, fr.eye, fr.target, cell, false);
				sheet.Add(cell);
			}
			WritePNG(g_out + "/candle_billboards_4views.png", sheet.img);
		}
		o->Destroy();
	}
	// SPEC-012 vector 12: a non-character model casts a shadow only with entity flag 0x1000
	{
		IGhoulObject *o = cl->CreateObject();
		OMGhoul_RegisterList("objects/generic/trashcan_barrel", "trashcan", o);
		o->FinishRegistration(false);
		o->Prepare(true);
		IGhoulInst *in = o->CreateInstance();
		in->Play(1, 0.0f, 0.0f, true, 0, false, false);
		float minZ;
		Framing fr = Frame(in, 0.0f, Vec3(1, -1, 1), 1.0f, minZ);
		g_ground = minZ - 1.0f;
		Image cell;
		cell.Init(128, 128, 0, 0, 0);
		int plain = DrawInto(in, 0.0f, fr.eye, fr.target, cell, false);
		g_flag1000 = true;
		int flagged = DrawInto(in, 0.0f, fr.eye, fr.target, cell, false);
		g_flag1000 = false;
		CHECK(flagged == plain + 6, "shadow only with flag 0x1000: %d vs %d triangles", plain, flagged);
		o->Destroy();
	}
	// SPEC-012 vector 10: a missing slot texture prints once with the COMSKIN path
	{
		g_host.log.clear();
		ImageList *a = AcquireImageList("GHOUL/ENEMY/MESO/", "FOO");
		ImageList *b = AcquireImageList("GHOUL/ENEMY/MESO/", "FOO");
		int n = 0;
		for (auto &l : g_host.log)
			n += l == "Ghoul :GHOUL/COMSKIN/FOO not found\n";
		CHECK(!a && !b && n == 1, "missing texture message printed %d times", n);
	}
	// SPEC-AMEND-002 vector 7 / SPEC-012 section 10: gserver
	{
		g_host.log.clear();
		const char *argv1[] = {"gserver", "light_ambient"};
		OMGhoul_Command(2, argv1);
		const char *argv2[] = {"gserver", "SUMMARY"};
		OMGhoul_Command(2, argv2);
		CHECK(g_host.log.size() == 4 && g_host.log[0] == "LIGHT_AMBIENT = 0.60\n", "gserver output '%s'",
			g_host.log.empty() ? "" : g_host.log[0].c_str());
	}

	// 4. every stock model: register, prepare, play its first sequence, draw one pose (no explosions: all posed
	//    vertices stay within the file's bounding box enlarged by 10 %)
	int models = 0, drawn = 0, bad = 0;
	if (!quick)
	{
		std::vector<std::string> files = ListFiles("ghoul", ".ghb");
		for (auto &p : files)
		{
			std::string norm = NormalizeName(p);
			IGhoulObject *o = cl->CreateObject();
			GhoulID s = o->RegisterSequence((StripExtension(p) + ".ghl").c_str(), false);
			models++;
			if (!s)
			{
				o->Destroy();
				continue;
			}
			o->FinishRegistration(false);
			IGhoulInst *in = o->CreateInstance();
			in->Play(s, 0.0f, 0.0f, true, 0, false, false);
			float d = in->SequenceDuration(s, 0, nullptr);
			std::vector<PosedTri> tris = Capture(in, d * 0.37f);
			if (!tris.empty())
				drawn++;
			const GhbFile *f = static_cast<Object *>(o)->files.empty() ? nullptr : static_cast<Object *>(o)->files[0];
			if (f && !tris.empty())
			{
				Vec3 lo = f->bounds.mn, hi = f->bounds.mx, ext = (hi - lo) * 0.1f + Vec3(1, 1, 1);
				bool out = false;
				for (auto &t : tris)
					for (int k = 0; k < 3; k++)
						for (int a = 0; a < 3; a++)
							if (t.p[k][a] < lo[a] - ext[a] || t.p[k][a] > hi[a] + ext[a])
								out = true;
				// billboards rotate about their node and may leave the box; ignore files with sprites
				bool sprites = false;
				for (auto &n : f->nodes)
					sprites = sprites || n.sprite;
				if (out && !sprites)
				{
					bad++;
					std::printf("   posed vertices outside the bounds: %s\n", p.c_str());
				}
			}
			o->Destroy();
			cl->Purge();
		}
		std::printf("   every model: %d registered, %d drew triangles, %d outside their bounds\n", models, drawn, bad);
		CHECK(bad == 0, "%d models posed outside their bounds", bad);
	}
	std::printf("   %d rendered cells, %d exports (OBJ + glTF)\n", g_cells, g_exports);
	cl->ReleaseTextures();
	cl->Reset();
}
