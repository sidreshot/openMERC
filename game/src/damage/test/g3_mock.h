// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G3 unit tests - mock Ghoul objects/instances built from the mock-model files that gen_mockmodels.py
// writes from the user's game data, and a mock player inventory.
// Implements: SPEC-010, SPEC-011 (behaviour subset needed by the damage/gore/body tests)
#pragma once
#include "../g3_local.h"
#include "stub_bases.h"

#include <map>
#include <string>
#include <vector>

namespace mock {

using namespace ghoul;

std::string Lower(const std::string &s);

struct Model {
	std::string dir, name, level;
	std::vector<std::string> parts;   // 1-based ids = index + 1
	std::vector<int> type;            // 1 mesh, 0 null
	std::vector<bool> visible;
	std::vector<std::string> mats;
	std::vector<std::string> seqs;
	std::vector<std::string> notes;
	std::vector<std::pair<int, std::string>> skins; // (material, name); id = index + 1
};

bool LoadModels(const char *path, std::vector<Model> &out);

class Inst;

class Object : public StubObject {
public:
	Model m;
	std::vector<Inst *> instances;
	GhoulID FindPart(const char *name) override;
	unsigned short NumParts() override { return (unsigned short)m.parts.size(); }
	int PartType(GhoulID p) override { return p && p <= m.type.size() ? m.type[p - 1] : 0; }
	void GetPartName(GhoulID p, char *out) override;
	GhoulID FindMaterial(const char *name) override;
	unsigned short NumMaterials() override { return (unsigned short)m.mats.size(); }
	void GetMaterialName(GhoulID mt, char *out) override;
	GhoulID FindSkin(GhoulID material, const char *name, int) override;   // every skin name "exists"
	GhoulID RegisterSkin(GhoulID material, const char *name, int x) override { return FindSkin(material, name, x); }
	void GetSkinName(GhoulID s, char *out) override;
	GhoulID SkinMaterial(GhoulID s) override { return s && s <= m.skins.size() ? (GhoulID)m.skins[s - 1].first : 0; }
	GhoulID FindSequence(const char *path) override;
	unsigned short NumSequences() override { return (unsigned short)m.seqs.size(); }
	GhoulID FindNote(const char *name) override;
	void EnumerateNotes(IGhoulCallback *, GhoulID) override {}
	IGhoulInst *CreateInstance() override;
	void FinishRegistration(bool) override {}
	void Prepare(bool) override {}
};

struct Child {
	Inst *inst;
	GhoulID bolt;
};

class Inst : public StubInst {
public:
	Object *obj;
	std::vector<bool> on;
	std::map<std::pair<int, int>, GhoulID> skin;   // (material, part) -> skin
	std::vector<Child> children;
	Inst *parent = nullptr;
	GhoulID attachBolt = 0;
	Matrix4 xf;
	GhoulID seq = 0;
	int mode = 0;
	float start = 0;
	bool paused = false;
	bool isOn = true;
	int speed = 4;
	float tint[4] = {1, 1, 1, 1};
	void *user = nullptr;
	bool destroyed = false;
	explicit Inst(Object *o);

	void SetTransform(const Matrix4 *m) override { xf = *m; }
	void GetTransform(Matrix4 *out) override { *out = xf; }
	void SetOnOff(bool v, float) override { isOn = v; }
	bool IsOn() override { return isOn; }
	void SetSpeed(int s) override { speed = s; }
	int GetSpeed() override { return speed; }
	void SetPartOnOff(GhoulID p, bool v) override { if (p && p <= on.size()) on[p - 1] = v; }
	bool IsPartOn(GhoulID p) override { return p && p <= on.size() && on[p - 1]; }
	void SetAllPartsOnOff(bool v) override;
	void SetSkin(GhoulID material, GhoulID s, GhoulID part) override { skin[{material, part}] = s; }
	GhoulID GetSkin(GhoulID material, GhoulID part, int) override;
	void RemoveSkin(GhoulID material, GhoulID part, int) override { skin.erase({material, part}); }
	void SetTint(float r, float g, float b, float a) override { tint[0] = r; tint[1] = g; tint[2] = b; tint[3] = a; }
	void Play(GhoulID s, float t, float, bool, int md, bool, bool) override { seq = s; start = t; mode = md; paused = false; }
	GhoulID GetPlayingSequence() override { return seq; }
	void Pause(float) override { paused = true; }
	void Resume(float) override { paused = false; }
	void SetUserValue(void *v) override { user = v; }
	void *GetUserValue() override { return user; }
	float SequenceDuration(GhoulID, int, float *) override { return 1.0f; }
	void AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt) override;
	void SetAttachBolt(GhoulID b) override { attachBolt = b; }
	GhoulID GetAttachBolt() override { return attachBolt; }
	void DetachChild(IGhoulInst *child) override;
	int NumChildren() override { return (int)children.size(); }
	IGhoulInst *GetChild(int i, GhoulID *bolt) override;
	IGhoulObject *GetGhoulObject() override { return obj; }
	IGhoulInst *GetParent() override { return parent; }
	IGhoulInst *Clone(bool withChildren) override;
	GhoulID GetInstanceId() override { return 77; }
	int TraceRay(float time, const float *start, const float *dir, HitRecord *hits, int maxHits) override;
	void GetBoltMatrix(float time, Matrix4 *out, GhoulID bolt, int mode, bool parents) override;
	void GetBounds(float, const Matrix4 *, float *mins, float *maxs, GhoulID, bool) override;
	void Destroy() override { destroyed = true; }
	// test helpers
	bool Shown(const char *part) { return IsPartOn(obj->FindPart(part)); }
	std::string SkinOn(const char *material, GhoulID part);
	Inst *ChildByModel(const char *name);
};

// bolt position model-space heuristic from the bolt name (only relative positions matter in the tests)
void BoltPos(const std::string &name, float out[3]);

// records the next TraceRay of any instance returns (part names on the hit instance or on a bolt-on model)
struct ShotRec {
	std::string model;   // "" = the hit instance itself, else the bolt-on model name
	std::string part;
	float distance;
};
extern std::vector<ShotRec> nextShot;

Object *MakeObject(const Model &m);
Object *FindModel(const char *dir, const char *name);  // from the loaded libraries

class Inventory : public StubInventory {
public:
	int armor = 0;
	int weapon = 3;
	float lastFraction = -1;
	int AbsorbArmor(int damage, float rate, float fraction) override;
	int Armor() override { return armor; }
	int AddArmor(int d) override { armor += d; if (armor < 0) armor = 0; return armor; }
	int CurrentWeapon() override { return weapon; }
};

} // namespace mock
