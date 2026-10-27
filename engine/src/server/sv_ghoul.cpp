// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 OpenMerc contributors
//
// OpenMerc server: the Ghoul services the engine hands to the game module and the player module
// (game imports #60-#66, SPEC-001/SPEC-010), and the server's use of the Ghoul network methods
// (system methods 12-21, instance method 52; SPEC-010/011/013).
//
// The services are those of the OpenMerc Ghoul library (engine/src/ghoul, package I3). With cvar
// sv_ghoul_stub 1 (read when a game module is loaded) a stub server system is used instead: it
// answers every method of the five interfaces with plain bookkeeping (registries, instance
// state, a fixed one-second sequence clock that delivers EOS events) and replicates nothing (the
// reliable stream carries empty lists and every instance offer returns 0). It exists to tell
// host problems from library problems. Its player-model description (#61) and .gsq helpers
// (#62-#66) follow SPEC-010/SPEC-121 and only use the object and instance interfaces.
//
// Implements: SPEC-001 (#6, #60-#66), SPEC-010 (A, B, D, E; helper imports), SPEC-011 (stub
//             instance), SPEC-013 s4 (server calls), SPEC-121 (.gpm/.gbm tokens)

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../ghoul/ghoul_api.h"
#include "../ghoul/ghoul_host.h"

using namespace ghoul;

// ---------------------------------------------------------------------------------------------
// engine services (implemented in C, sv_game.c)
extern "C" {
void SV_GhoulHost_Print(const char *text);
void SV_GhoulHost_Fatal(const char *text);
int SV_GhoulHost_LoadFile(const char *path, void **data);
void SV_GhoulHost_FreeFile(void *data);
int SV_GhoulHost_Debug(void);
float SV_GhoulHost_Cvar(const char *name);
double SV_GhoulHost_Milliseconds(void);
int SV_GhoulFileIndex(const char *name); // sv_init.c: configstring slot 1498.. for a name
void SV_ImportTrace_HookVtable(int iface, void *object); // sv_imptrace.c (test aid)
}

static void GPrintf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	SV_GhoulHost_Print(buf);
}

static void GFatal(const char *msg)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "Ghoul :%s", msg);
	SV_GhoulHost_Fatal(buf);
}

static bool GDebug() { return (SV_GhoulHost_Debug() & 2) != 0; }

// call counters per interface method (gserver STATS)
static unsigned g_calls[5][64];
enum { kSys, kObj, kInst, kPM, kCB };
static const char *const g_ifname[5] = {"system", "object", "instance", "playermodel", "callback"};
#define COUNT(i, m) (g_calls[(i)][(m)]++)

static std::string Normalize(const char *s)
{
	std::string r = s ? s : "";
	for (char &c : r)
	{
		if (c == '\\')
			c = '/';
		c = (char)toupper((unsigned char)c);
	}
	return r;
}

// ---------------------------------------------------------------------------------------------
// name registry with 1-based ids
struct Registry
{
	std::vector<std::string> names;
	std::map<std::string, int> ids;

	GhoulID Add(const char *name)
	{
		std::string n = Normalize(name);
		auto it = ids.find(n);
		if (it != ids.end())
			return (GhoulID)it->second;
		names.push_back(n);
		ids[n] = (int)names.size();
		return (GhoulID)names.size();
	}
	GhoulID Find(const char *name) const
	{
		if (!name)
			return 0;
		auto it = ids.find(Normalize(name));
		return it == ids.end() ? 0 : (GhoulID)it->second;
	}
	void Copy(GhoulID id, char *out) const
	{
		if (!out)
			return;
		if (id >= 1 && id <= names.size())
			strcpy(out, names[id - 1].c_str());
		else
			out[0] = 0;
	}
};

// ---------------------------------------------------------------------------------------------
// id allocator: 1..32766, freed ids are reused (SPEC-010 ids)
struct IdPool
{
	std::vector<bool> used;
	int Alloc()
	{
		if (used.empty())
			used.push_back(true); // slot 0 = none
		for (size_t i = 1; i < used.size(); i++)
			if (!used[i])
			{
				used[i] = true;
				return (int)i;
			}
		if (used.size() > 32766)
		{
			GFatal("Out of uuids");
			return 0;
		}
		used.push_back(true);
		return (int)used.size() - 1;
	}
	void Free(int id)
	{
		if (id > 0 && (size_t)id < used.size())
			used[id] = false;
	}
	void Reset() { used.clear(); }
};

class StubSystem;
class StubObject;
class StubInst;

static void Identity(Matrix4 *m)
{
	memset(m, 0, sizeof(*m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1.0f;
	m->flags = 1;
}

static const float kStubFrameTime = 0.1f; // stub sequence: 10 frames of 100 ms
static const int kStubFrames = 10;
static const float kSpeed[13] = {0.5f, 0.625f, 0.75f, 0.875f, 1.0f, 1.125f, 1.25f, 1.375f, 1.5f,
	1.625f, 1.75f, 1.875f, 2.0f};

// ---------------------------------------------------------------------------------------------
class StubInst : public IGhoulInst
{
public:
	StubObject *obj;
	int id;
	Matrix4 xf;
	bool on = true;
	int speed = 4;
	std::vector<uint8_t> parts; // 1 = shown, index = part id
	std::map<std::pair<int, int>, int> skins;
	uint8_t tint[4] = {255, 255, 255, 255};
	GhoulID seq = 0;
	int mode = 0;
	float start = 0, pauseTime = 0, cursor = 0;
	bool paused = false, reverse = false;
	void *user = nullptr;
	struct NoteCB
	{
		IGhoulCallback *cb;
		GhoulID note;
	};
	struct BoltCB
	{
		IGhoulCallback *cb;
		GhoulID bolt;
		int mode;
	};
	std::vector<NoteCB> notecbs;
	std::vector<BoltCB> boltcbs;
	bool noteEvents = false, matrixEvents = false;
	StubInst *parent = nullptr;
	GhoulID attachBolt = 0;
	struct Child
	{
		StubInst *inst;
		GhoulID bolt;
	};
	std::vector<Child> children;

	StubInst(StubObject *o, int i);

	float Duration(GhoulID s, int m) const
	{
		if (!s)
			return 0;
		float f = kStubFrameTime * kSpeed[(speed >= 0 && speed <= 12) ? speed : 4];
		switch (m)
		{
			case 0: return kStubFrames * f;
			case 1: return (kStubFrames - 1) * f;
			case 2: return (2 * kStubFrames - 1) * f;
			default: return 0;
		}
	}

	void SetTransform(const Matrix4 *m) override
	{
		COUNT(kInst, 0);
		if (m)
			xf = *m;
	}
	void SetTransform16(const float *m16) override
	{
		COUNT(kInst, 1);
		if (!m16)
			return;
		memcpy(xf.m, m16, sizeof(xf.m));
		bool ident = true;
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				if (fabsf(xf.m[r][c] - (r == c ? 1.0f : 0.0f)) > 0.0001f)
					ident = false;
		xf.flags = ident ? 1u : 0u;
	}
	void GetTransform(Matrix4 *out) override
	{
		COUNT(kInst, 2);
		if (out)
			*out = xf;
	}
	void GetTransform16(float *out16) override
	{
		COUNT(kInst, 3);
		if (out16)
			memcpy(out16, xf.m, sizeof(xf.m));
	}
	void SetOnOff(bool o, float time) override
	{
		COUNT(kInst, 4);
		if (o && !on)
			cursor = time;
		on = o;
	}
	bool IsOn() override { COUNT(kInst, 5); return on; }
	void SetSpeed(int s) override { COUNT(kInst, 6); speed = s; }
	int GetSpeed() override { COUNT(kInst, 7); return speed; }
	void SetPartOnOff(GhoulID part, bool shown) override
	{
		COUNT(kInst, 8);
		if (parts.size() <= part)
			parts.resize(part + 1, 1);
		parts[part] = shown ? 1 : 0;
	}
	bool IsPartOn(GhoulID part) override
	{
		COUNT(kInst, 9);
		return part >= parts.size() ? true : parts[part] != 0;
	}
	void SetAllPartsOnOff(bool shown) override
	{
		COUNT(kInst, 10);
		for (auto &p : parts)
			p = shown ? 1 : 0;
	}
	void SetSkin(GhoulID material, GhoulID skin, GhoulID part) override
	{
		COUNT(kInst, 11);
		skins[{material, part}] = skin;
	}
	GhoulID GetSkin(GhoulID material, GhoulID part, int) override
	{
		COUNT(kInst, 12);
		auto it = skins.find({material, part});
		return it == skins.end() ? 0 : (GhoulID)it->second;
	}
	void RemoveSkin(GhoulID material, GhoulID part, int) override
	{
		COUNT(kInst, 13);
		skins.erase({material, part});
	}
	void SetTint(float r, float g, float b, float a) override
	{
		COUNT(kInst, 14);
		float v[4] = {r, g, b, a};
		for (int i = 0; i < 4; i++)
		{
			float c = v[i] < 0 ? 0 : (v[i] > 1 ? 1 : v[i]);
			tint[i] = (uint8_t)(c * 255.0f);
		}
	}
	void GetTint(float *r, float *g, float *b, float *a) override
	{
		COUNT(kInst, 15);
		if (r) *r = tint[0] / 255.0f;
		if (g) *g = tint[1] / 255.0f;
		if (b) *b = tint[2] / 255.0f;
		if (a) *a = tint[3] / 255.0f;
	}
	void SetTintAll(float r, float g, float b, float a) override
	{
		COUNT(kInst, 16);
		SetTint(r, g, b, a);
		for (auto &c : children)
			c.inst->SetTint(r, g, b, a);
	}
	void Play(GhoulID s, float time, float offset, bool restart, int m, bool, bool rev) override;
	GhoulID GetPlayingSequence() override { COUNT(kInst, 18); return seq; }
	void Pause(float time) override
	{
		COUNT(kInst, 19);
		if (!paused)
		{
			paused = true;
			pauseTime = time;
		}
	}
	void Resume(float time) override
	{
		COUNT(kInst, 20);
		if (paused)
		{
			start += time - pauseTime;
			paused = false;
		}
	}
	void SetUserValue(void *v) override { COUNT(kInst, 21); user = v; }
	void *GetUserValue() override { COUNT(kInst, 22); return user; }
	void AddNoteCallback(IGhoulCallback *cb, GhoulID note) override
	{
		COUNT(kInst, 23);
		notecbs.push_back({cb, note});
		noteEvents = true;
	}
	void RemoveNoteCallback(IGhoulCallback *cb, GhoulID note) override
	{
		COUNT(kInst, 24);
		for (size_t i = 0; i < notecbs.size();)
			if (notecbs[i].cb == cb && notecbs[i].note == note)
				notecbs.erase(notecbs.begin() + i);
			else
				i++;
	}
	void AddMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int m) override
	{
		COUNT(kInst, 25);
		boltcbs.push_back({cb, bolt, m});
		noteEvents = matrixEvents = true;
	}
	void RemoveMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int m) override
	{
		COUNT(kInst, 26);
		for (size_t i = 0; i < boltcbs.size();)
			if (boltcbs[i].cb == cb && boltcbs[i].bolt == bolt && boltcbs[i].mode == m)
				boltcbs.erase(boltcbs.begin() + i);
			else
				i++;
	}
	float SequenceDuration(GhoulID s, int m, float *frameTime) override
	{
		COUNT(kInst, 27);
		if (frameTime)
			*frameTime = s ? kStubFrameTime * kSpeed[(speed >= 0 && speed <= 12) ? speed : 4] : 0;
		return Duration(s, m);
	}
	void AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt) override
	{
		COUNT(kInst, 28);
		if (!child)
			return;
		StubInst *c = static_cast<StubInst *>(child);
		children.push_back({c, myBolt});
		c->parent = this;
		c->attachBolt = childBolt;
	}
	void SetAttachBolt(GhoulID bolt) override { COUNT(kInst, 29); attachBolt = bolt; }
	GhoulID GetAttachBolt() override { COUNT(kInst, 30); return attachBolt; }
	void DetachChild(IGhoulInst *child) override
	{
		COUNT(kInst, 31);
		for (size_t i = 0; i < children.size(); i++)
			if (children[i].inst == child)
			{
				children[i].inst->parent = nullptr;
				children.erase(children.begin() + i);
				return;
			}
	}
	int NumChildren() override { COUNT(kInst, 32); return (int)children.size(); }
	IGhoulInst *GetChild(int index, GhoulID *myBolt) override
	{
		COUNT(kInst, 33);
		if (index < 0 || index >= (int)children.size())
			return nullptr;
		if (myBolt)
			*myBolt = children[index].bolt;
		return children[index].inst;
	}
	IGhoulObject *GetGhoulObject() override;
	IGhoulInst *GetParent() override { COUNT(kInst, 35); return parent; }
	IGhoulInst *Clone(bool withChildren) override;
	int SaveState(void *buffer, int capacity, IGhoulCallback **, int, const char *) override
	{
		COUNT(kInst, 37);
		// private stub image: id only
		if (buffer && capacity >= 4)
		{
			memcpy(buffer, &id, 4);
			return 4;
		}
		return 0;
	}
	GhoulID LoadState(const void *buffer, int length, IGhoulCallback **, int, const char *) override
	{
		COUNT(kInst, 38);
		int saved = 0;
		if (buffer && length >= 4)
			memcpy(&saved, buffer, 4);
		return (GhoulID)saved;
	}
	GhoulID GetInstanceId() override { COUNT(kInst, 39); return (GhoulID)id; }
	GhoulID GetObjectId() override;
	void Draw(GhoulRenderParams *) override { COUNT(kInst, 41); }
	void Reserved42(int, int, int) override { COUNT(kInst, 42); }
	int TraceRay(float, const float *, const float *, HitRecord *, int) override
	{
		COUNT(kInst, 43);
		return 0;
	}
	int TraceBox(float, const Matrix4 *, const float *, const float *, const float *, const float *,
		HitRecord *, int) override
	{
		COUNT(kInst, 44);
		return 0;
	}
	void ProcessEvents(float time) override;
	void StopEvents() override { COUNT(kInst, 46); noteEvents = matrixEvents = false; }
	void StopMatrixEvents() override { COUNT(kInst, 47); matrixEvents = false; }
	void StartNoteEvents(float time) override
	{
		COUNT(kInst, 48);
		noteEvents = true;
		cursor = time;
	}
	void StartEvents(float time) override
	{
		COUNT(kInst, 49);
		noteEvents = matrixEvents = true;
		cursor = time;
	}
	void GetBoltMatrix(float, Matrix4 *out, GhoulID, int matrixMode, bool) override
	{
		COUNT(kInst, 50);
		if (!out)
			return;
		if (!seq || !on)
		{
			Identity(out);
			return;
		}
		if (!(matrixMode & kMatrixPose))
		{
			memset(out, 0, sizeof(*out)); // rate: no root motion in the stub
			return;
		}
		if (matrixMode & kMatrixWithTransform)
			*out = xf;
		else
			Identity(out);
	}
	void GetBounds(float, const Matrix4 *, float *mins, float *maxs, GhoulID, bool) override
	{
		COUNT(kInst, 51);
		if (mins)
			mins[0] = mins[1] = mins[2] = 0;
		if (maxs)
			maxs[0] = maxs[1] = maxs[2] = 0;
	}
	GhoulID NetOffer(float) override
	{
		COUNT(kInst, 52);
		return 0; // no client knows a stub object
	}
	void Destroy() override;
	GhoulID GetPlayingSequence2() override { COUNT(kInst, 54); return seq; }
};

// ---------------------------------------------------------------------------------------------
class StubObject : public IGhoulObject
{
public:
	StubSystem *sys;
	int id;
	Registry seqs, partreg, notes, materials;
	struct Skin
	{
		std::string name;
		GhoulID material;
	};
	std::vector<Skin> skinlist;
	std::vector<StubInst *> insts;

	StubObject(StubSystem *s, int i) : sys(s), id(i)
	{
		notes.Add("BOS");
		notes.Add("EOS");
	}

	GhoulID RegisterSequence(const char *path, bool) override
	{
		COUNT(kObj, 0);
		if (!path || !path[0])
			return 0;
		return seqs.Add(path);
	}
	void FinishRegistration(bool) override { COUNT(kObj, 1); }
	void Prepare(bool) override { COUNT(kObj, 2); }
	GhoulID FindSequence(const char *path) override { COUNT(kObj, 3); return seqs.Find(path); }
	unsigned short NumSequences() override { COUNT(kObj, 4); return (unsigned short)seqs.names.size(); }
	void GetSequencePath(GhoulID s, char *out) override { COUNT(kObj, 5); seqs.Copy(s, out); }
	GhoulID RegisterPart(const char *name) override
	{
		COUNT(kObj, 6);
		return name ? partreg.Add(name) : 0;
	}
	GhoulID FindPart(const char *name) override
	{
		COUNT(kObj, 7);
		// the stub knows no model data: every asked-for part exists (bolts, gore zones)
		return name && name[0] ? partreg.Add(name) : 0;
	}
	unsigned short NumParts() override { COUNT(kObj, 8); return (unsigned short)partreg.names.size(); }
	int PartType(GhoulID part) override
	{
		COUNT(kObj, 9);
		if (part < 1 || part > partreg.names.size())
			return 0;
		return partreg.names[part - 1].find("BOLT") != std::string::npos ? kPartNull : kPartMesh;
	}
	void GetPartName(GhoulID part, char *out) override { COUNT(kObj, 10); partreg.Copy(part, out); }
	GhoulID RegisterNote(const char *name) override { COUNT(kObj, 11); return name ? notes.Add(name) : 0; }
	GhoulID FindNote(const char *name) override
	{
		COUNT(kObj, 12);
		return name && name[0] ? notes.Add(name) : 0;
	}
	unsigned short NumNotes() override { COUNT(kObj, 13); return (unsigned short)notes.names.size(); }
	void EnumerateNotes(IGhoulCallback *, GhoulID) override { COUNT(kObj, 14); }
	GhoulID RegisterMaterial(const char *name) override
	{
		COUNT(kObj, 15);
		return name ? materials.Add(name) : 0;
	}
	GhoulID FindMaterial(const char *name) override
	{
		COUNT(kObj, 16);
		return name && name[0] ? materials.Add(name) : 0;
	}
	unsigned short NumMaterials() override { COUNT(kObj, 17); return (unsigned short)materials.names.size(); }
	void GetMaterialName(GhoulID m, char *out) override { COUNT(kObj, 18); materials.Copy(m, out); }
	GhoulID SkinId(GhoulID material, const char *name)
	{
		std::string n = Normalize(name);
		for (size_t i = 0; i < skinlist.size(); i++)
			if (skinlist[i].material == material && skinlist[i].name == n)
				return (GhoulID)(i + 1);
		skinlist.push_back({n, material});
		return (GhoulID)skinlist.size();
	}
	GhoulID RegisterSkin(GhoulID material, const char *name, int) override
	{
		COUNT(kObj, 19);
		return name ? SkinId(material, name) : 0;
	}
	GhoulID FindSkin(GhoulID material, const char *name, int) override
	{
		COUNT(kObj, 20);
		return name && name[0] ? SkinId(material, name) : 0;
	}
	unsigned short NumSkins() override { COUNT(kObj, 21); return (unsigned short)skinlist.size(); }
	void GetSkinName(GhoulID skin, char *out) override
	{
		COUNT(kObj, 22);
		if (!out)
			return;
		if (skin >= 1 && skin <= skinlist.size())
			strcpy(out, skinlist[skin - 1].name.c_str());
		else
			out[0] = 0;
	}
	GhoulID SkinMaterial(GhoulID skin) override
	{
		COUNT(kObj, 23);
		return (skin >= 1 && skin <= skinlist.size()) ? skinlist[skin - 1].material : 0;
	}
	IGhoulInst *CreateInstance() override;
	unsigned short GetObjectId() override { COUNT(kObj, 25); return (unsigned short)id; }
	IGhoulSystem *GetSystem() override;
	void Destroy() override;
	void DestroyAllInstances() override
	{
		COUNT(kObj, 28);
		while (!insts.empty())
			insts.back()->Destroy();
	}
	void Forget(StubInst *in)
	{
		for (size_t i = 0; i < insts.size(); i++)
			if (insts[i] == in)
			{
				insts.erase(insts.begin() + i);
				return;
			}
	}
};

// ---------------------------------------------------------------------------------------------
class StubSystem : public IGhoulSystem
{
public:
	IdPool objIds, instIds;
	std::map<int, StubObject *> objects;
	std::map<int, StubInst *> insts;
	int texDetail = 0;
	void (*fullpath)(char *out, const char *path) = nullptr;

	IGhoulObject *CreateObject() override
	{
		COUNT(kSys, 0);
		int id = objIds.Alloc();
		StubObject *o = new StubObject(this, id);
		objects[id] = o;
		return o;
	}
	void SetGLTable(const GhoulGLTable *) override { COUNT(kSys, 1); }
	void SetSpecular(bool) override { COUNT(kSys, 2); }
	void ReleaseTextures() override { COUNT(kSys, 3); }
	void BeginFrame() override { COUNT(kSys, 4); }
	void EndFrame() override { COUNT(kSys, 5); }
	void DestroyAllObjects() override
	{
		COUNT(kSys, 6);
		while (!objects.empty())
			objects.begin()->second->Destroy();
	}
	void Purge() override { COUNT(kSys, 7); }
	void Reset() override
	{
		COUNT(kSys, 8);
		DestroyAllObjects();
		objIds.Reset();
		instIds.Reset();
	}
	IGhoulInst *FindInstance(int handle) override
	{
		COUNT(kSys, 9);
		auto it = insts.find(handle);
		return it == insts.end() ? nullptr : it->second;
	}
	IGhoulObject *FindObject(int handle) override
	{
		COUNT(kSys, 10);
		auto it = objects.find(handle);
		return it == objects.end() ? nullptr : it->second;
	}
	void SetFullPathCallback(void (*fn)(char *, const char *)) override { COUNT(kSys, 11); fullpath = fn; }
	void NetClientConnected(int) override { COUNT(kSys, 12); }
	void NetClientDropped(int) override { COUNT(kSys, 13); }
	void NetResendAll() override { COUNT(kSys, 14); }
	void NetDropAll() override { COUNT(kSys, 15); }
	bool NetWriteReliable(int, int, om_ghoul_bits_t *msg) override
	{
		COUNT(kSys, 16);
		// four empty lists (string table, objects, removed objects, removed instances): 4 zero bits
		if (msg && msg->maxbits >= 4)
			msg->bit += 4;
		return true;
	}
	bool NetReliablePending(int) override { COUNT(kSys, 17); return false; }
	bool NetWriteUnreliable(int, int, om_ghoul_bits_t *, float) override { COUNT(kSys, 18); return false; }
	void NetAcknowledge(int, int) override { COUNT(kSys, 19); }
	void NetCommitFrame(int, int) override { COUNT(kSys, 20); }
	void NetBeginFrame(int, int) override { COUNT(kSys, 21); }
	void TouchFile(const char *path) override
	{
		COUNT(kSys, 22);
		void *data = nullptr;
		if (path && SV_GhoulHost_LoadFile(path, &data) >= 0 && data)
			SV_GhoulHost_FreeFile(data);
	}
	void ClientStartAccepting() override { COUNT(kSys, 23); }
	void ClientReadReliable(int, om_ghoul_bits_t *, bool) override { COUNT(kSys, 24); }
	void ClientReadUnreliable(int, om_ghoul_bits_t *, float) override { COUNT(kSys, 25); }
	int ClientReliableSize() override { COUNT(kSys, 26); return 0; }
	void *ClientReliableData() override { COUNT(kSys, 27); return nullptr; }
	int ModelLightCount() override { COUNT(kSys, 28); return 0; }
	void GetModelLight(int, LightRecord *) override { COUNT(kSys, 29); }
	void ClearLights() override { COUNT(kSys, 30); }
	void AddDynamicLight(const LightRecord *) override { COUNT(kSys, 31); }
	void EndRenderFrame() override { COUNT(kSys, 32); }
	void FlushMeshCache() override { COUNT(kSys, 33); }
	void AddWorldLight(const LightRecord *) override { COUNT(kSys, 34); }
	void FinishWorldLights() override { COUNT(kSys, 35); }
	void BeginWorldLights() override { COUNT(kSys, 36); }
	void SetGamma(const unsigned char *) override { COUNT(kSys, 37); }
	void SetTextureDetail(int level) override { COUNT(kSys, 38); texDetail = level; }
	void SetPolygonOffset(float, float) override { COUNT(kSys, 39); }
	int StatTextures() override { COUNT(kSys, 40); return 0; }
	int StatTextureBytes() override { COUNT(kSys, 41); return 0; }
	int StatMeshes() override { COUNT(kSys, 42); return 0; }
	int StatTriangles() override { COUNT(kSys, 43); return 0; }
	int StatTrianglesTotal() override { COUNT(kSys, 44); return 0; }
	int StatVertices() override { COUNT(kSys, 45); return 0; }
	int StatCorners() override { COUNT(kSys, 46); return 0; }
	int StatLights() override { COUNT(kSys, 47); return 0; }
	void ResetStats() override { COUNT(kSys, 48); }
};

static StubSystem g_stub;

// ---- out-of-line members ----
StubInst::StubInst(StubObject *o, int i) : obj(o), id(i) { Identity(&xf); }

IGhoulObject *StubInst::GetGhoulObject() { COUNT(kInst, 34); return obj; }
GhoulID StubInst::GetObjectId() { COUNT(kInst, 40); return (GhoulID)obj->id; }

void StubInst::Play(GhoulID s, float time, float offset, bool restart, int m, bool, bool rev)
{
	COUNT(kInst, 17);
	if (!s || s > obj->seqs.names.size())
		return;
	if (s == seq && m == mode && rev == reverse && !restart)
		return;
	seq = s;
	mode = m;
	reverse = rev;
	start = time - offset;
	paused = false;
	cursor = time;
}

void StubInst::ProcessEvents(float time)
{
	COUNT(kInst, 45);
	if (!on || !seq || !noteEvents)
	{
		cursor = time;
		return;
	}
	float t = paused ? pauseTime : time;
	float D = Duration(seq, mode);
	int cycles = 0;
	if (D > 0 && t > cursor)
	{
		if (mode == 1)
			cycles = (cursor - start < D && t - start >= D) ? 1 : 0;
		else
		{
			int a = (int)floorf((cursor - start) / D);
			int b = (int)floorf((t - start) / D);
			cycles = b - a;
			if (cycles > 2)
				cycles = 2;
		}
	}
	cursor = time;
	if (cycles <= 0)
		return;
	GhoulID eos = obj->notes.Find("EOS");
	std::vector<NoteCB> cbs = notecbs; // callbacks may change the list
	for (int c = 0; c < cycles; c++)
		for (auto &n : cbs)
			if (n.note == eos && n.cb)
				n.cb->Event(0, user, time, "");
}

IGhoulInst *StubInst::Clone(bool withChildren)
{
	COUNT(kInst, 36);
	StubInst *c = static_cast<StubInst *>(obj->CreateInstance());
	int newid = c->id;
	auto kids = children;
	*c = *this;
	c->id = newid;
	c->parent = nullptr;
	c->children.clear();
	if (withChildren)
		for (auto &k : kids)
			c->AttachChild(k.bolt, k.inst->Clone(true), k.inst->attachBolt);
	return c;
}

void StubInst::Destroy()
{
	COUNT(kInst, 53);
	while (!children.empty())
		children.back().inst->Destroy();
	if (parent)
		parent->DetachChild(this);
	obj->Forget(this);
	obj->sys->insts.erase(id);
	obj->sys->instIds.Free(id);
	delete this;
}

IGhoulInst *StubObject::CreateInstance()
{
	COUNT(kObj, 24);
	int iid = sys->instIds.Alloc();
	StubInst *in = new StubInst(this, iid);
	insts.push_back(in);
	sys->insts[iid] = in;
	return in;
}

IGhoulSystem *StubObject::GetSystem() { COUNT(kObj, 26); return sys; }

void StubObject::Destroy()
{
	COUNT(kObj, 27);
	while (!insts.empty())
		insts.back()->Destroy();
	sys->objects.erase(id);
	sys->objIds.Free(id);
	delete this;
}

// ---------------------------------------------------------------------------------------------
// shared file cache of imports #62/#64 (buffers live until shutdown)
struct CachedFile
{
	void *data;
	int length;
};
static std::map<std::string, CachedFile> g_filecache;

static int CacheLoad(const std::string &path, void **buffer)
{
	std::string key = Normalize(path.c_str());
	auto it = g_filecache.find(key);
	if (it == g_filecache.end())
	{
		void *data = nullptr;
		int len = SV_GhoulHost_LoadFile(path.c_str(), &data);
		if (len < 0 || !data)
			return -1;
		it = g_filecache.emplace(key, CachedFile{data, len}).first;
	}
	if (buffer)
		*buffer = it->second.data;
	return it->second.length;
}

static std::string GhoulPath(const char *folder, const char *file)
{
	std::string p = "Ghoul/";
	if (folder && folder[0])
	{
		p += folder;
		p += "/";
	}
	p += file ? file : "";
	return p;
}

// #62
static int LoadGhoulFile(const char *folder, const char *file, void **buffer)
{
	if (buffer)
		*buffer = nullptr;
	if (!file)
		return -1;
	int len = CacheLoad(GhoulPath(folder, file), buffer);
	if (len < 0)
		len = CacheLoad(GhoulPath("comskin", file), buffer);
	if (len < 0 && buffer)
		*buffer = nullptr;
	return len;
}

// #63: next line; the byte before the LF is replaced by NUL (CR LF expected)
static int GetLine(int *remaining, char **cursor, char *line)
{
	int n = 0;
	if (!remaining || !cursor || !*cursor || !line)
		return 0;
	while (*remaining > 0)
	{
		char c = **cursor;
		(*cursor)++;
		(*remaining)--;
		if (c == '\n')
		{
			line[n > 0 ? n - 1 : 0] = 0;
			return 1;
		}
		line[n++] = c;
	}
	line[n] = 0;
	return 0;
}

static std::vector<std::string> ListLines(const char *folder, const char *name, bool &found)
{
	std::vector<std::string> out;
	void *buf = nullptr;
	std::string file = std::string(name ? name : "") + ".gsq";
	int len = LoadGhoulFile(folder, file.c_str(), &buf);
	found = len >= 0 && buf;
	if (!found)
		return out;
	const char *p = (const char *)buf, *end = p + len;
	while (p < end)
	{
		const char *e = p;
		while (e < end && *e != '\n')
			e++;
		std::string l(p, e);
		while (!l.empty() && isspace((unsigned char)l.back()))
			l.pop_back();
		size_t s = 0;
		while (s < l.size() && isspace((unsigned char)l[s]))
			s++;
		l = l.substr(s);
		if (!l.empty())
			out.push_back(l);
		p = e + 1;
	}
	return out;
}

// #65
static int RegisterList_r(const char *folder, const char *name, IGhoulObject *obj, int depth)
{
	bool found;
	std::vector<std::string> lines = ListLines(folder, name, found);
	if (!found)
	{
		std::string path = GhoulPath(folder, (std::string(name) + ".GHL").c_str());
		return obj->RegisterSequence(path.c_str(), false) != 0;
	}
	bool ok = true;
	for (auto &l : lines)
	{
		if (l[0] == '#')
		{
			if (depth < 16 && !RegisterList_r(folder, l.c_str() + 1, obj, depth + 1))
				ok = false;
			continue;
		}
		std::string path = GhoulPath(folder, (l + ".GHL").c_str());
		if (!obj->RegisterSequence(path.c_str(), false))
			ok = false;
	}
	return ok;
}

// #66
static void PartsOff_r(const char *folder, const char *name, IGhoulObject *obj, IGhoulInst *inst,
	int depth)
{
	bool found;
	std::vector<std::string> lines = ListLines(folder, name, found);
	for (auto &l : lines)
	{
		if (l[0] == '#')
		{
			if (depth < 16)
				PartsOff_r(folder, l.c_str() + 1, obj, inst, depth + 1);
			continue;
		}
		GhoulID part = obj->FindPart(l.c_str());
		if (part)
			inst->SetPartOnOff(part, false);
	}
}

// ---------------------------------------------------------------------------------------------
// player-model description (SPEC-010 D, tokens of SPEC-121)
class StubPlayerModel : public IGhoulPlayerModel
{
public:
	int userValue = 0;
	bool loaded = false;
	std::string dir, menu, base, set, team, sound, face, trailer, menuSeq;
	std::vector<PlayerSkinEntry> skins;
	std::vector<PlayerBoltonEntry> boltons;
	std::vector<PlayerPartEntry> partsw;

	static void Tokens(const char *text, int len, std::vector<std::string> &out)
	{
		const char *p = text, *end = text + len;
		while (p < end)
		{
			if (isspace((unsigned char)*p))
			{
				p++;
				continue;
			}
			if (p + 1 < end && p[0] == '/' && p[1] == '/')
			{
				while (p < end && *p != '\n')
					p++;
				continue;
			}
			if (*p == '"')
			{
				const char *s = ++p;
				while (p < end && *p != '"')
					p++;
				out.emplace_back(s, p);
				if (p < end)
					p++;
				continue;
			}
			if (*p == '{' || *p == '}')
			{
				out.emplace_back(p, p + 1);
				p++;
				continue;
			}
			const char *s = p;
			while (p < end && !isspace((unsigned char)*p) && *p != '{' && *p != '}' && *p != '"')
				p++;
			out.emplace_back(s, p);
		}
	}

	// a header value: word, quoted string or {} (empty)
	static std::string Value(const std::vector<std::string> &t, size_t &i)
	{
		if (i >= t.size())
			return "";
		if (t[i] == "{")
		{
			while (i < t.size() && t[i] != "}")
				i++;
			i++;
			return "";
		}
		return t[i++];
	}

	static std::vector<std::string> Block(const std::vector<std::string> &t, size_t &i)
	{
		std::vector<std::string> b;
		if (i < t.size() && t[i] == "{")
		{
			i++;
			while (i < t.size() && t[i] != "}")
				b.push_back(t[i++]);
			i++;
		}
		return b;
	}

	void Load(const char *file) override
	{
		COUNT(kPM, 0);
		userValue = 0;
		loaded = false;
		dir = menu = base = set = team = sound = face = trailer = menuSeq = "";
		skins.clear();
		boltons.clear();
		partsw.clear();
		if (!file)
			return;
		void *data = nullptr;
		std::string path = std::string("Ghoul/pmodels/") + file;
		int len = SV_GhoulHost_LoadFile(path.c_str(), &data);
		if (len < 0 || !data)
			return;
		std::vector<std::string> t;
		Tokens((const char *)data, len, t);
		SV_GhoulHost_FreeFile(data);
		size_t i = 0;
		dir = Value(t, i);
		menu = Value(t, i);
		base = Value(t, i);
		set = Value(t, i);
		team = Value(t, i);
		sound = Value(t, i);
		face = Value(t, i);
		std::vector<std::string> sk;
		if (i < t.size() && t[i] == "{")
			sk = Block(t, i);
		else // .gbm: triples without braces up to the end
			while (i + 2 < t.size() && t[i] != "{")
			{
				sk.push_back(t[i++]);
				sk.push_back(t[i++]);
				sk.push_back(t[i++]);
			}
		for (size_t k = 0; k + 2 < sk.size(); k += 3)
		{
			PlayerSkinEntry e;
			memset(&e, 0, sizeof(e));
			strncpy(e.material, sk[k].c_str(), sizeof(e.material) - 1);
			strncpy(e.skin, sk[k + 1].c_str(), sizeof(e.skin) - 1);
			skins.push_back(e);
		}
		std::vector<std::string> bo = Block(t, i);
		for (size_t k = 0; k + 3 < bo.size(); k += 4)
		{
			PlayerBoltonEntry e;
			memset(&e, 0, sizeof(e));
			strncpy(e.file, bo[k].c_str(), sizeof(e.file) - 1);
			strncpy(e.bolt, bo[k + 1].c_str(), sizeof(e.bolt) - 1);
			strncpy(e.boltonBolt, bo[k + 2].c_str(), sizeof(e.boltonBolt) - 1);
			e.scale = (float)atof(bo[k + 3].c_str());
			boltons.push_back(e);
		}
		std::vector<std::string> pa = Block(t, i);
		for (size_t k = 0; k + 1 < pa.size(); k += 2)
		{
			PlayerPartEntry e;
			memset(&e, 0, sizeof(e));
			strncpy(e.part, pa[k].c_str(), sizeof(e.part) - 1);
			e.shown = (uint8_t)atoi(pa[k + 1].c_str());
			partsw.push_back(e);
		}
		trailer = Value(t, i);
		menuSeq = "ghoul/" + dir + "/" + menu + ".ghl";
		loaded = true;
	}
	int GetUserValue() override { COUNT(kPM, 1); return userValue; }
	void SetUserValue(int v) override { COUNT(kPM, 2); userValue = v; }
	int NumSkins() override { COUNT(kPM, 3); return (int)skins.size(); }
	int NumBoltons() override { COUNT(kPM, 4); return (int)boltons.size(); }
	int NumPartSwitches() override { COUNT(kPM, 5); return (int)partsw.size(); }
	const char *GetModelDir() override { COUNT(kPM, 6); return dir.c_str(); }
	const char *GetMenuSequence() override { COUNT(kPM, 7); return menuSeq.c_str(); }
	const char *GetBaseModel() override { COUNT(kPM, 8); return base.c_str(); }
	const char *GetSequenceList() override { COUNT(kPM, 9); return set.c_str(); }
	const char *GetFacePrefix() override { COUNT(kPM, 10); return face.c_str(); }
	bool GetTrailer(char *out) override
	{
		COUNT(kPM, 11);
		if (out)
			strcpy(out, trailer.c_str());
		return true;
	}
	bool GetTeam(char *out) override
	{
		COUNT(kPM, 12);
		if (out)
			strcpy(out, team.c_str());
		return true;
	}
	bool GetSoundDir(char *out) override
	{
		COUNT(kPM, 13);
		if (out)
			strcpy(out, sound.c_str());
		return true;
	}
	bool GetSkinEntry(int index, PlayerSkinEntry *out) override
	{
		COUNT(kPM, 14);
		if (index < 1 || index > (int)skins.size() || !out)
			return false;
		*out = skins[index - 1];
		return true;
	}
	bool GetBoltonEntry(int index, PlayerBoltonEntry *out) override
	{
		COUNT(kPM, 15);
		if (index < 1 || index > (int)boltons.size() || !out)
			return false;
		*out = boltons[index - 1];
		return true;
	}
	bool GetPartEntry(int index, PlayerPartEntry *out) override
	{
		COUNT(kPM, 16);
		if (index < 1 || index > (int)partsw.size() || !out)
			return false;
		*out = partsw[index - 1];
		return true;
	}
	bool IsLoaded() override { COUNT(kPM, 17); return loaded; }
	void *Destroy(int flags) override
	{
		COUNT(kPM, 18);
		if (flags & 1)
		{
			delete this;
			return nullptr;
		}
		return this;
	}
};

// ---------------------------------------------------------------------------------------------
// provider selection: latched when a game module is loaded (the game keeps the system pointer)
static bool g_uselib = true;

static IGhoulSystem *Sys()
{
	if (g_uselib)
		return static_cast<IGhoulSystem *>(OMGhoul_GetSystem(OM_GHOUL_SERVER));
	return &g_stub;
}

// every file the Ghoul library opened through the engine, named by game import #6. Black-box rule
// (original server, dm/jpndm2): model-cache files (.ghb, .ifl, .tga) appear with the whole path in
// upper case, player-model descriptions and touched files (.gpm, .gbm, .rmf, .m32) as given, and the
// sequence lists (.gsq, imports #62-#66 text cache) not at all.
static std::set<std::string> g_usedfiles;

static void RecordUsedFile(const char *path)
{
	std::string p(path);
	for (char &c : p)
		if (c == '\\')
			c = '/';
	size_t dot = p.find_last_of('.');
	std::string ext = dot == std::string::npos ? "" : p.substr(dot + 1);
	for (char &c : ext)
		c = (char)tolower((unsigned char)c);
	if (ext == "gsq")
		return;
	if (ext == "ghb" || ext == "ifl" || ext == "tga")
		for (char &c : p)
			c = (char)toupper((unsigned char)c);
	else if ((ext == "gpm" || ext == "gbm") && (p.size() > 14))
	{
		std::string head = p.substr(0, 14);
		for (char &c : head)
			c = (char)tolower((unsigned char)c);
		if (head == "ghoul/pmodels/")
			p.replace(0, 14, head); // import #61 opens "ghoul/pmodels/<file>"
	}
	g_usedfiles.insert(p);
}

static int HostLoad(void *, const char *path, void **data)
{
	int len = SV_GhoulHost_LoadFile(path, data);
	if ((len >= 0) && path && path[0])
		RecordUsedFile(path);
	return len;
}
static void HostFree(void *, void *data) { SV_GhoulHost_FreeFile(data); }
static void HostPrint(void *, const char *text) { SV_GhoulHost_Print(text); }
static void HostFatal(void *, const char *text) { SV_GhoulHost_Fatal(text); }
static double HostMs(void *) { return SV_GhoulHost_Milliseconds(); }

extern "C" {

// once per process, before any module uses the Ghoul system (the client and renderer too)
void SV_Ghoul_Init(void)
{
	static bool done;
	om_ghoul_host_t host;
	if (done)
		return;
	done = true;
	memset(&host, 0, sizeof(host));
	host.load_file = HostLoad;
	host.free_file = HostFree;
	host.print = HostPrint;
	host.fatal = HostFatal;
	host.milliseconds = HostMs;
	OMGhoul_Init(&host);
}

// before GetGameAPI: library or stub for this game module
void SV_Ghoul_SelectForGame(void)
{
	SV_Ghoul_Init();
	g_uselib = SV_GhoulHost_Cvar("sv_ghoul_stub") == 0;
	GPrintf("Ghoul services: %s\n", g_uselib ? "OpenMerc Ghoul library" : "server stub (no replication)");
}

void SV_Ghoul_Shutdown(void)
{
	OMGhoul_Shutdown();
	for (auto &f : g_filecache)
		SV_GhoulHost_FreeFile(f.second.data);
	g_filecache.clear();
}

void *SV_Ghoul_System(void) { return Sys(); }

// test aid (sv_watchdog): trace the instance and object method tables
void SV_Ghoul_HookTraceInstance(void *inst)
{
	if (!inst)
		return;
	SV_ImportTrace_HookVtable(2, inst);
	SV_ImportTrace_HookVtable(1, static_cast<IGhoulInst *>(inst)->GetGhoulObject());
}

void *SV_Ghoul_NewPlayerModel(const char *file)
{
	if (g_uselib)
		return OMGhoul_NewPlayerModel(file);
	StubPlayerModel *m = new StubPlayerModel();
	m->Load(file);
	return static_cast<IGhoulPlayerModel *>(m);
}

int SV_Ghoul_LoadFile(const char *folder, const char *file, void **buffer)
{
	if (g_uselib)
		return OMGhoul_LoadFile(folder, file, buffer);
	return LoadGhoulFile(folder, file, buffer);
}

int SV_Ghoul_GetLine(int *remaining, char **cursor, char *line)
{
	if (g_uselib)
		return OMGhoul_GetLine(remaining, cursor, line);
	return GetLine(remaining, cursor, line);
}

void SV_Ghoul_PreloadList(const char *folder, const char *name)
{
	void *buf;
	if (g_uselib)
	{
		OMGhoul_PreloadList(folder, name);
		return;
	}
	LoadGhoulFile(folder, (std::string(name ? name : "") + ".gsq").c_str(), &buf);
}

int SV_Ghoul_RegisterList(const char *folder, const char *name, void *object)
{
	if (g_uselib)
		return OMGhoul_RegisterList(folder, name, object);
	if (!object || !name)
		return 0;
	return RegisterList_r(folder ? folder : "", name, static_cast<IGhoulObject *>(object), 0);
}

void SV_Ghoul_PartsOff(const char *folder, const char *name, void *object, void *instance)
{
	if (g_uselib)
	{
		OMGhoul_PartsOff(folder, name, object, instance);
		return;
	}
	if (!object || !instance || !name)
		return;
	PartsOff_r(folder ? folder : "", name, static_cast<IGhoulObject *>(object),
		static_cast<IGhoulInst *>(instance), 0);
}

// gserver: the library's debug commands; STATS also prints the host's call counters
void SV_Ghoul_Command(int argc, const char *const *argv)
{
	if (g_uselib)
		OMGhoul_Command(argc, argv);
}

// SPEC-010 lifetimes: dedicated spawn = purge; `map` with clients = resend all
void SV_Ghoul_ServerSpawn(int clientsconnected)
{
	if (clientsconnected)
		Sys()->NetResendAll();
	Sys()->Purge();
}

void SV_Ghoul_ServerDropAll(void) { Sys()->NetDropAll(); }
void SV_Ghoul_ServerReset(void) { Sys()->Reset(); }

void SV_Ghoul_ClientConnected(int clientnum) { Sys()->NetClientConnected(clientnum); }
void SV_Ghoul_ClientDropped(int clientnum) { Sys()->NetClientDropped(clientnum); }
int SV_Ghoul_ReliablePending(int clientnum) { return Sys()->NetReliablePending(clientnum); }

// one reliable chunk of at most `budget` bytes; *done = nothing left
int SV_Ghoul_WriteReliable(int clientnum, unsigned char *out, int budget, int *done)
{
	om_ghoul_bits_t bits;
	memset(out, 0, budget);
	bits.data = out;
	bits.maxbits = budget * 8;
	bits.bit = 0;
	bits.overflowed = 0;
	bool d = Sys()->NetWriteReliable(clientnum, 0, &bits);
	if (done)
		*done = d ? 1 : 0;
	if (bits.overflowed)
		GFatal("Ghoul reliable overflow");
	return (bits.bit + 7) / 8;
}

void SV_Ghoul_BeginFrame(int clientnum, int frame) { Sys()->NetBeginFrame(clientnum, frame); }

int SV_Ghoul_Offer(void *inst, float priority)
{
	if (!inst)
		return 0;
	return static_cast<IGhoulInst *>(inst)->NetOffer(priority);
}

int SV_Ghoul_WriteUnreliable(int clientnum, int frame, float time, unsigned char *out, int budget)
{
	om_ghoul_bits_t bits;
	memset(out, 0, budget);
	bits.data = out;
	bits.maxbits = budget * 8;
	bits.bit = 0;
	bits.overflowed = 0;
	if (!Sys()->NetWriteUnreliable(clientnum, frame, &bits, time))
		return 0;
	return (bits.bit + 7) / 8;
}

void SV_Ghoul_CommitFrame(int clientnum, int frame) { Sys()->NetCommitFrame(clientnum, frame); }
void SV_Ghoul_Acknowledge(int clientnum, int frame) { Sys()->NetAcknowledge(clientnum, frame); }
void SV_Ghoul_TouchFile(const char *path) { Sys()->TouchFile(path); }

// game import #6 (deathmatch only, checked by the caller): name slots for every file the Ghoul
// library has opened through the engine, in strcmp order (as the original's configstrings 1498..).
// The stub loads no model files.
void SV_Ghoul_RegisterAllFiles(void)
{
	if (!g_uselib)
		return;
	for (const std::string &f : g_usedfiles)
		SV_GhoulFileIndex(f.c_str());
}

void SV_Ghoul_StatsPrint(void)
{
	if (g_uselib)
		return;
	for (int i = 0; i < 5; i++)
	{
		GPrintf("%s:", g_ifname[i]);
		for (int m = 0; m < 64; m++)
			if (g_calls[i][m])
				GPrintf(" %d=%u", m, g_calls[i][m]);
		GPrintf("\n");
	}
	GPrintf("stub: %d objects, %d instances\n", (int)g_stub.objects.size(), (int)g_stub.insts.size());
}

} // extern "C"
