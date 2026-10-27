// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - the three system objects, shared caches, ids, lights, statistics and
// the gserver debug command.
// Implements: SPEC-010 A (system interface, ids, roles, lifetimes), SPEC-012 sections 9-10 (roles, purge,
//             caches, gserver), SPEC-012 section 8 (world-light grid, model lights, statistics),
//             SPEC-AMEND-002 items 3.1, 5.1, 5.2

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ghoul_internal.h"

namespace ghoul {

const float kSpeedMultipliers[13] = {0.5f, 0.625f, 0.75f, 0.875f, 1.0f, 1.125f, 1.25f, 1.375f, 1.5f, 1.625f, 1.75f,
	1.875f, 2.0f};

float SpeedMultiplier(int index)
{
	// SPEC-011: the index is not range-checked by the original; out-of-range indexes are clamped here
	if (index < 0)
		index = 0;
	if (index > 12)
		index = 12;
	return kSpeedMultipliers[index];
}

Shared::Shared()
{
	for (int i = 0; i < 256; i++)
		gamma[i] = (unsigned char)i;
}

Shared &G()
{
	static Shared s;
	return s;
}

static System *g_systems[3];

System *GetSystemObject(int role)
{
	if (role < 0 || role > 2)
		return nullptr;
	if (!g_systems[role])
		g_systems[role] = new System(role);
	return g_systems[role];
}

void DestroySystems()
{
	for (auto &s : g_systems)
	{
		if (s)
		{
			s->Reset();
			delete s;
			s = nullptr;
		}
	}
}

// ------------------------------------------------------------------ helpers

std::string BaseName(const std::string &p)
{
	size_t s = p.find_last_of("/\\");
	std::string b = s == std::string::npos ? p : p.substr(s + 1);
	size_t d = b.find_last_of('.');
	if (d != std::string::npos)
		b = b.substr(0, d);
	return b;
}

void SetString(char *out, const std::string &s)
{
	if (!out)
		return;
	std::memcpy(out, s.c_str(), s.size() + 1);
}

int BitsFor(int count)
{
	int b = 0;
	while (b < 31 && (count >> b) != 0)
		b++;
	return b;
}

// ------------------------------------------------------------------ model-file cache

GhbFile *FileCache::Acquire(const std::string &ghbPath)
{
	std::string key = StripExtension(NormalizeName(ghbPath));
	auto it = files_.find(key);
	if (it != files_.end())
	{
		it->second->refCount++;
		return it->second.get();
	}
	std::unique_ptr<GhbFile> f(new GhbFile());
	if (!f->Load(ghbPath))
		return nullptr; // a failed load leaves no entry
	f->refCount = 1;
	GhbFile *r = f.get();
	files_[key] = std::move(f);
	return r;
}

void FileCache::Release(GhbFile *f)
{
	if (!f)
		return;
	f->refCount--;
	if (f->refCount <= 0 && !G().gs.cacheGhb)
	{
		auto it = files_.find(f->cacheKey);
		if (it != files_.end() && it->second.get() == f)
			files_.erase(it);
	}
}

void FileCache::PurgeUnreferenced()
{
	for (auto it = files_.begin(); it != files_.end();)
	{
		if (it->second->refCount <= 0)
			it = files_.erase(it);
		else
			++it;
	}
}

void FileCache::DeleteAll()
{
	// SPEC-012: RELOAD deletes every cached file regardless of references. Live objects would point to
	// freed data in the original; here files still referenced are kept alive and only unreferenced ones
	// are dropped, so that the next registration re-reads them (deviation, avoids a crash).
	PurgeUnreferenced();
}

// ------------------------------------------------------------------ string table

void StringTable::Clear()
{
	strings_.clear();
	index_.clear();
	strings_.push_back(std::string());
	index_[std::string()] = 0;
}

int StringTable::Add(const std::string &s)
{
	auto it = index_.find(s);
	if (it != index_.end())
		return it->second;
	int i = (int)strings_.size();
	strings_.push_back(s);
	index_[s] = i;
	return i;
}

int StringTable::Find(const std::string &s) const
{
	auto it = index_.find(s);
	return it == index_.end() ? -1 : it->second;
}

const std::string &StringTable::Get(int i) const
{
	static const std::string empty;
	return i >= 0 && i < (int)strings_.size() ? strings_[i] : empty;
}

// ------------------------------------------------------------------ ids

int IdAllocator::Allocate()
{
	int id = 1;
	for (int u : used_)
	{
		if (u > id)
			break;
		if (u == id)
			id++;
	}
	if (id > kMaxGhoulId)
		Fatal("Out of uuids");
	used_.insert(id);
	return id;
}

void IdAllocator::Free(int id) { used_.erase(id); }

// ------------------------------------------------------------------ world-light grid (SPEC-012 section 8)

void WorldLightGrid::Clear()
{
	lights.clear();
	cells.clear();
	built = false;
}

void WorldLightGrid::Build()
{
	cells.assign(32 * 32 * 32, std::vector<int>());
	built = true;
	if (lights.empty())
		return;
	Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
	for (auto &l : lights)
		for (int a = 0; a < 3; a++)
		{
			lo[a] = std::min(lo[a], l.origin[a] - l.radius);
			hi[a] = std::max(hi[a], l.origin[a] + l.radius);
		}
	mn = lo;
	for (int a = 0; a < 3; a++)
		size[a] = std::max(hi[a] - lo[a], 10000.0f);
	auto cell = [&](float v, int a) {
		int c = (int)std::floor((v - mn[a]) / size[a] * 32.0f);
		return c < 0 ? 0 : (c > 31 ? 31 : c);
	};
	for (size_t i = 0; i < lights.size(); i++)
	{
		const LightRecord &l = lights[i];
		int c0[3], c1[3];
		for (int a = 0; a < 3; a++)
		{
			c0[a] = cell(l.origin[a] - l.radius, a);
			c1[a] = cell(l.origin[a] + l.radius, a);
		}
		for (int x = c0[0]; x <= c1[0]; x++)
			for (int y = c0[1]; y <= c1[1]; y++)
				for (int z = c0[2]; z <= c1[2]; z++)
					cells[(x * 32 + y) * 32 + z].push_back((int)i);
	}
}

void WorldLightGrid::Query(const Vec3 &p, std::vector<const LightRecord *> &out) const
{
	out.clear();
	if (!built || lights.empty() || cells.empty())
		return;
	int c[3];
	for (int a = 0; a < 3; a++)
	{
		int v = (int)std::floor((p[a] - mn[a]) / size[a] * 32.0f);
		c[a] = v < 0 ? 0 : (v > 31 ? 31 : v);
	}
	std::vector<std::pair<float, const LightRecord *>> found;
	for (int i : cells[(c[0] * 32 + c[1]) * 32 + c[2]])
	{
		const LightRecord &l = lights[i];
		Vec3 d = Vec3(l.origin) - p;
		float dist = Length(d);
		if (dist <= l.radius)
			found.push_back(std::make_pair(dist, &l));
	}
	std::stable_sort(found.begin(), found.end(),
		[](const std::pair<float, const LightRecord *> &a, const std::pair<float, const LightRecord *> &b) {
			return a.first < b.first;
		});
	for (auto &f : found)
		out.push_back(f.second);
}

// ------------------------------------------------------------------ system

System::System(int r) : role(r) {}

System::~System()
{
	DestroyAllObjects();
}

Object *System::NewObject(int oid, bool received)
{
	Object *o = new Object(this, oid, received);
	auto it = objects.find(oid);
	if (it != objects.end())
		it->second->DestroyNow(false);
	objects[oid] = o;
	return o;
}

Inst *System::NewInst(Object *o, int forcedId)
{
	int iid = forcedId ? forcedId : instIds.Allocate();
	auto it = insts.find(iid);
	if (it != insts.end())
		it->second->Destroy();
	Inst *i = new Inst(this, o, iid);
	insts[iid] = i;
	o->insts[iid] = i;
	// gserver SIZEMATTERS (debug): scales new character instances with id >= 16 and no parent
	if (G().gs.sizeMatters != 1.0f && o->character && iid >= 16)
	{
		Matrix4 m;
		MatIdentity(m);
		float s = G().gs.sizeMatters;
		m.m[0][0] = m.m[1][1] = m.m[2][2] = s;
		m.flags = 0;
		i->SetTransform(&m);
	}
	return i;
}

void System::ObjectDestroyed(Object *o, bool notify)
{
	auto it = objects.find(o->id);
	if (it != objects.end() && it->second == o)
		objects.erase(it);
	if (o->id > 0)
		objIds.Free(o->id);
	if (notify && IsServer())
		NetObjectRemoved(o);
}

void System::InstDestroyed(Inst *i)
{
	auto it = insts.find(i->id);
	if (it != insts.end() && it->second == i)
		insts.erase(it);
	if (i->id > 0)
		instIds.Free(i->id);
	NetInstRemoved(i); // server: queue removals; client: drop stored delta states
}

IGhoulObject *System::CreateObject()
{
	Object *o = NewObject(objIds.Allocate(), false);
	if (IsServer())
		NetObjectCreated(o);
	return o;
}

void System::SetGLTable(const GhoulGLTable *table)
{
	G().gl = table;
	if (!table)
		return;
	// textures of every model file already loaded are uploaded now
	for (auto &t : G().textures)
		if (t.second->decoded && !t.second->missing && t.second->refs > 0)
			BindTexture(t.second.get(), true, true);
}

void System::SetSpecular(bool specular)
{
	G().specular = specular;
	for (int r = 1; r <= 2; r++)
	{
		System *s = GetSystemObject(r);
		for (auto &o : s->objects)
			o.second->Prepare(specular);
	}
}

void System::ReleaseTextures() { ReleaseAllTextures(); }
void System::BeginFrame() {}
void System::EndFrame() {}

void System::DestroyAllObjects()
{
	while (!objects.empty())
		objects.begin()->second->Destroy();
}

void System::Purge()
{
	FlushChunkCache();
	bool skip = role == OM_GHOUL_MENU && GetSystemObject(0)->objects.empty() && GetSystemObject(1)->objects.empty();
	if (skip)
		return;
	G().files.PurgeUnreferenced();
	PurgeImages();
	for (int r = 1; r <= 2; r++)
	{
		System *s = GetSystemObject(r);
		for (auto &o : s->objects)
			o.second->Prepare(G().specular);
	}
}

void System::Reset()
{
	NetDropAll();
	DestroyAllObjects();
	strings.Clear();
	objIds.Clear();
	instIds.Clear();
	modelLights.clear();
	dynLights.clear();
	NetResetData(this);
}

IGhoulInst *System::FindInstance(int handle)
{
	if (handle == 0)
		return nullptr;
	int key = role == OM_GHOUL_SERVER ? handle : -handle;
	auto it = insts.find(key);
	return it == insts.end() ? nullptr : it->second;
}

IGhoulObject *System::FindObject(int handle)
{
	if (handle == 0)
		return nullptr;
	int key = role == OM_GHOUL_SERVER ? handle : -handle;
	auto it = objects.find(key);
	return it == objects.end() ? nullptr : it->second;
}

void System::SetFullPathCallback(void (*fn)(char *out, const char *path)) { G().fullPath = fn; }

void System::TouchFile(const char *path)
{
	if (!path || !Host().load_file)
		return;
	void *d = nullptr;
	int n = Host().load_file(Host().ctx, path, &d);
	(void)n;
	if (d && Host().free_file)
		Host().free_file(Host().ctx, d);
}

int System::ModelLightCount() { return (int)modelLights.size(); }

void System::GetModelLight(int index, LightRecord *out)
{
	if (!out)
		return;
	// the original does no range check; out-of-range indexes give a zero record here
	if (index < 0 || index >= (int)modelLights.size())
	{
		std::memset(out, 0, sizeof(*out));
		return;
	}
	*out = modelLights[index];
}

void System::ClearLights()
{
	modelLights.clear();
	dynLights.clear();
}

void System::AddDynamicLight(const LightRecord *light)
{
	if (light)
		dynLights.push_back(*light);
}

void System::EndRenderFrame() {}
void System::FlushMeshCache() { FlushChunkCache(); }

void System::AddWorldLight(const LightRecord *light)
{
	if (light)
		world.lights.push_back(*light);
}

void System::FinishWorldLights() { world.Build(); }

void System::BeginWorldLights()
{
	world.Clear();
	FlushChunkCache();
	// the light states of instances refer to world-light records
	for (auto &i : insts)
		i.second->rc.Reset();
}

void System::SetGamma(const unsigned char *table256)
{
	if (table256)
		std::memcpy(G().gamma, table256, 256);
}

void System::SetTextureDetail(int level) { G().textureDetail = level; }

void System::SetPolygonOffset(float factor, float units)
{
	G().polyFactor = factor;
	G().polyUnits = units;
}

int System::StatTextures() { return G().stats.textures; }
int System::StatTextureBytes() { return G().stats.textureBytes; }
int System::StatMeshes() { return G().stats.meshes; }
int System::StatTriangles() { return G().stats.triangles; }
int System::StatTrianglesTotal() { return G().stats.trianglesTotal; }
int System::StatVertices() { return 0; }
int System::StatCorners() { return 0; }
int System::StatLights() { return 0; }

void System::ResetStats()
{
	Stats &s = G().stats;
	s.textures = s.textureBytes = s.meshes = s.triangles = 0;
	s.renderFrame++;
}

// ------------------------------------------------------------------ gserver (SPEC-012 section 10)

static bool Toggle(const char *arg, bool &v)
{
	if (!arg)
		return false;
	if (!std::strcmp(arg, "!"))
		v = !v;
	else if (!std::strcmp(arg, "1"))
		v = true;
	else if (!std::strcmp(arg, "0"))
		v = false;
	return true;
}

static float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void GserverCommand(int argc, const char *const *argv)
{
	if (argc < 2)
		return;
	std::string sub = Upper(argv[1] ? argv[1] : "");
	std::string argU = argc > 2 && argv[2] ? Upper(argv[2]) : std::string();
	const char *arg = argc > 2 ? argU.c_str() : nullptr;
	GserverSettings &gs = G().gs;
	if (sub == "RELOAD")
		G().files.DeleteAll();
	else if (sub == "SIZEMATTERS")
	{
		if (arg)
			gs.sizeMatters = Clampf((float)std::atof(arg), 0.01f, 100.0f);
	}
	else if (sub == "IMAGELIST")
	{
		int maps = 0, texs = 0;
		bool verbose = true;
		if (verbose)
			Print("Loaded Maps\n");
		for (auto &l : G().imageLists)
		{
			if (verbose)
				Print("%s\n", l.first.c_str());
			maps++;
		}
		if (verbose)
			Print("Loaded Textures\n");
		for (auto &t : G().textures)
		{
			if (verbose)
				Print("%s\n", t.first.c_str());
			texs++;
		}
		Print("%d Maps, %d Textures\n", maps, texs);
	}
	else if (sub == "USEAGE" || sub == "USAGE")
	{
		for (auto &f : G().files.Files())
		{
			const GhbFile &gf = *f.second;
			Print("Usage Report For: %s\n", gf.source.c_str());
			for (size_t n = 0; n < gf.nodes.size(); n++)
				if (!gf.usedNodes[n])
					Print("%s\n", gf.nodes[n].name.c_str());
			Print("End of Usage Report For: %s\n", gf.source.c_str());
		}
	}
	else if (sub == "TEST")
	{
		Toggle(arg, gs.test);
		Print(gs.test ? "Test is On\n" : "Test is Off\n");
	}
	else if (sub == "LIGHT_SMOOTH" || sub == "LIGHTSMOOTH")
	{
		if (arg)
			gs.lightSmooth = Clampf((float)std::atof(arg), 0.0f, 6.0f);
		Print("LIGHT_SMOOTH = %4.2f\n", gs.lightSmooth);
	}
	else if (sub == "LIGHT_AMBIENT" || sub == "LIGHTAMBIENT")
	{
		if (arg)
			gs.lightAmbient = Clampf((float)std::atof(arg), 0.0f, 1.0f);
		Print("LIGHT_AMBIENT = %4.2f\n", gs.lightAmbient);
	}
	else if (sub == "PRECACHE_ALL" || sub == "PRECACHEALL")
	{
		Toggle(arg, gs.precacheAll);
		Print(gs.precacheAll ? "PreCacheAll is On\n" : "PreCacheAll is Off\n");
	}
	else if (sub == "AUTOPREP" || sub == "AUTO_PREP")
	{
		Toggle(arg, gs.autoPrep);
		Print(gs.autoPrep ? "AutoPrep is On\n" : "AutoPrep is Off\n");
	}
	else if (sub == "CACHEGHB" || sub == "CACHE_GHB")
	{
		Toggle(arg, gs.cacheGhb);
		Print(gs.cacheGhb ? "CacheGHB is On\n" : "CacheGHB is Off\n");
		if (!gs.cacheGhb)
			G().files.PurgeUnreferenced();
	}
	else if (sub == "FLUSHMESHCACHE" || sub == "FLUSH_MESH_CACHE")
	{
		FlushChunkCache();
		Print("Mesh Cache Flushed\n");
	}
	else if (sub == "LISTFILES" || sub == "LIST_FILES")
	{
		Print("%d total ghoul files have been loaded.\n", (int)G().files.Files().size());
		for (auto &f : G().files.Files())
			Print("%s\n", f.second->path.c_str());
	}
	else if (sub == "SUMMARY")
	{
		static const char *names[3] = {"Server", "Client", "Menu"};
		for (int r = 0; r < 3; r++)
		{
			System *s = GetSystemObject(r);
			Print("%s  %d obj   %d inst\n", names[r], (int)s->objects.size(), (int)s->insts.size());
		}
	}
	// STATS, LEAKAGE, MEM, MEM_START, MEM_END, SPEED and unknown sub-commands: no effect
}

} // namespace ghoul
