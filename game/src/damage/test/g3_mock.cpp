// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G3 unit tests - mock Ghoul objects and instances, mock inventory.
// Implements: SPEC-010, SPEC-011 (behaviour subset), SPEC-003 method 31 (mock)
#include "g3_mock.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace mock {

std::vector<ShotRec> nextShot;

std::string Lower(const std::string &s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)tolower(c); });
	return r;
}

bool LoadModels(const char *path, std::vector<Model> &out)
{
	std::ifstream f(path);
	if (!f)
		return false;
	std::string line;
	Model *cur = nullptr;
	while (std::getline(f, line)) {
		std::istringstream is(line);
		std::string k;
		is >> k;
		if (k == "model") {
			out.emplace_back();
			cur = &out.back();
			is >> cur->dir >> cur->name >> cur->level;
		} else if (!cur)
			continue;
		else if (k == "mat") {
			std::string n;
			is >> n;
			cur->mats.push_back(n);
		} else if (k == "mesh" || k == "node") {
			std::string n;
			int v = 1;
			is >> n;
			if (k == "mesh")
				is >> v;
			cur->parts.push_back(n);
			cur->type.push_back(k == "mesh" ? 1 : 0);
			cur->visible.push_back(v != 0);
		} else if (k == "seq") {
			std::string n;
			is >> n;
			cur->seqs.push_back(n);
		} else if (k == "note") {
			std::string n;
			is >> n;
			cur->notes.push_back(n);
		}
	}
	return true;
}

GhoulID Object::FindPart(const char *name)
{
	if (!name)
		return 0;
	std::string n = Lower(name);
	for (size_t i = 0; i < m.parts.size(); i++)
		if (Lower(m.parts[i]) == n)
			return (GhoulID)(i + 1);
	return 0;
}

void Object::GetPartName(GhoulID p, char *out)
{
	strcpy(out, p && p <= m.parts.size() ? m.parts[p - 1].c_str() : "");
}

GhoulID Object::FindMaterial(const char *name)
{
	if (!name)
		return 0;
	std::string n = Lower(name);
	for (size_t i = 0; i < m.mats.size(); i++)
		if (Lower(m.mats[i]) == n)
			return (GhoulID)(i + 1);
	return 0;
}

void Object::GetMaterialName(GhoulID mt, char *out)
{
	strcpy(out, mt && mt <= m.mats.size() ? m.mats[mt - 1].c_str() : "");
}

GhoulID Object::FindSkin(GhoulID material, const char *name, int)
{
	if (!material || !name)
		return 0;
	std::string n = Lower(name);
	for (size_t i = 0; i < m.skins.size(); i++)
		if (m.skins[i].first == material && Lower(m.skins[i].second) == n)
			return (GhoulID)(i + 1);
	m.skins.push_back({material, name});
	return (GhoulID)m.skins.size();
}

void Object::GetSkinName(GhoulID s, char *out)
{
	strcpy(out, s && s <= m.skins.size() ? m.skins[s - 1].second.c_str() : "");
}

GhoulID Object::FindSequence(const char *path)
{
	if (!path)
		return 0;
	std::string p = Lower(path);
	size_t sl = p.find_last_of('/');
	std::string b = sl == std::string::npos ? p : p.substr(sl + 1);
	if (b.size() > 4 && b.substr(b.size() - 4) == ".ghl")
		b = b.substr(0, b.size() - 4);
	for (size_t i = 0; i < m.seqs.size(); i++)
		if (Lower(m.seqs[i]) == b)
			return (GhoulID)(i + 1);
	return 0;
}

GhoulID Object::FindNote(const char *name)
{
	static const char *const always[] = {"EOS", "BOS", "fire", "jump", "thud", "knee", "inair", "throw", "dropweapon",
		"sound", nullptr};
	for (int i = 0; always[i]; i++)
		if (!Q_stricmp(always[i], name))
			return (GhoulID)(i + 1);
	return 0;
}

IGhoulInst *Object::CreateInstance()
{
	Inst *i = new Inst(this);
	instances.push_back(i);
	return i;
}

Inst::Inst(Object *o) : obj(o)
{
	on = o->m.visible;
	memset(&xf, 0, sizeof xf);
	xf.m[0][0] = xf.m[1][1] = xf.m[2][2] = xf.m[3][3] = 1;
	xf.flags = 1;
}

void Inst::SetAllPartsOnOff(bool v)
{
	for (size_t i = 0; i < on.size(); i++)
		if (obj->m.type[i] == 1)
			on[i] = v;
}

GhoulID Inst::GetSkin(GhoulID material, GhoulID part, int)
{
	auto it = skin.find({material, part});
	return it == skin.end() ? 0 : it->second;
}

void Inst::AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt)
{
	Inst *c = static_cast<Inst *>(child);
	c->parent = this;
	c->attachBolt = childBolt;
	children.push_back({c, myBolt});
}

void Inst::DetachChild(IGhoulInst *child)
{
	for (size_t i = 0; i < children.size(); i++)
		if (children[i].inst == child) {
			children[i].inst->parent = nullptr;
			children.erase(children.begin() + i);
			return;
		}
}

IGhoulInst *Inst::GetChild(int i, GhoulID *bolt)
{
	if (i < 0 || i >= (int)children.size())
		return nullptr;
	if (bolt)
		*bolt = children[i].bolt;
	return children[i].inst;
}

IGhoulInst *Inst::Clone(bool)
{
	Inst *c = static_cast<Inst *>(obj->CreateInstance());
	c->on = on;
	c->skin = skin;
	c->xf = xf;
	c->seq = seq;
	c->mode = mode;
	c->start = start;
	return c;
}

void BoltPos(const std::string &nm, float out[3])
{
	std::string n = Lower(nm);
	float x = 0, y = 0, z = 0;
	auto has = [&](const char *s) { return n.find(s) != std::string::npos; };
	if (has("head") || has("mouth"))
		z = 32;
	else if (has("neck"))
		z = 26;
	else if (has("shoulder") || has("bicep"))
		z = 22;
	else if (has("chest"))
		z = 15;
	else if (has("gut") || has("hip_") || has("forearm") || has("hand"))
		z = 3;
	else if (has("groin"))
		z = -2;
	else if (has("thigh"))
		z = -10;
	else if (has("calf"))
		z = -22;
	else if (has("heel") || has("foot"))
		z = -31;
	if (has("_r") || has("rf") || has("rb") || has("right"))
		y = -8;
	if (has("_l") || has("lf") || has("lb") || has("left"))
		y = 8;
	size_t L = n.size();
	if ((L > 2 && n.substr(L - 2) == "_f") || has("_rf") || has("_lf"))
		x = 5;
	if ((L > 2 && n.substr(L - 2) == "_b") || has("_rb") || has("_lb"))
		x = -5;
	out[0] = x;
	out[1] = y;
	out[2] = z;
}

int Inst::TraceRay(float, const float *st, const float *dir, HitRecord *hits, int maxHits)
{
	int n = 0;
	for (const ShotRec &r : nextShot) {
		if (n >= maxHits)
			break;
		Inst *target = this;
		if (!r.model.empty())
			target = ChildByModel(r.model.c_str());
		if (!target)
			continue;
		GhoulID p = target->obj->FindPart(r.part.c_str());
		if (!p)
			continue;
		HitRecord &h = hits[n++];
		h.inst = target;
		h.part = p;
		h.pad = 0;
		h.distance = r.distance;
		for (int k = 0; k < 3; k++) {
			h.point[k] = st[k] + dir[k] * r.distance;
			h.normal[k] = -dir[k];
		}
	}
	return n;
}

void Inst::GetBoltMatrix(float, Matrix4 *out, GhoulID bolt, int md, bool parents)
{
	if (!bolt || bolt > obj->m.parts.size())
		return; // left unchanged: "bolt not in the current sequence"
	memset(out, 0, sizeof *out);
	out->m[0][0] = out->m[1][1] = out->m[2][2] = out->m[3][3] = 1;
	if (!(md & 4)) {
		out->m[0][0] = out->m[1][1] = out->m[2][2] = 0; // rate: no root motion in the mock
		out->m[3][3] = 0;
		return;
	}
	float p[3];
	BoltPos(obj->m.parts[bolt - 1], p);
	if (md & 1)
		for (int k = 0; k < 3; k++)
			p[k] += xf.m[3][k];
	if (parents && parent) {
		for (auto &c : parent->children)
			if (c.inst == this) {
				Matrix4 pm;
				parent->GetBoltMatrix(0, &pm, c.bolt, md, true);
				for (int k = 0; k < 3; k++)
					p[k] += pm.m[3][k];
			}
	}
	out->m[3][0] = p[0];
	out->m[3][1] = p[1];
	out->m[3][2] = p[2];
}

void Inst::GetBounds(float, const Matrix4 *, float *mins, float *maxs, GhoulID, bool)
{
	for (int k = 0; k < 3; k++) {
		mins[k] = -6;
		maxs[k] = 6;
	}
	maxs[2] = 14;
}

std::string Inst::SkinOn(const char *material, GhoulID part)
{
	GhoulID mt = obj->FindMaterial(material);
	GhoulID s = GetSkin(mt, part, 0);
	char b[128];
	obj->GetSkinName(s, b);
	return s ? b : "";
}

Inst *Inst::ChildByModel(const char *name)
{
	for (auto &c : children) {
		if (Lower(c.inst->obj->m.name) == Lower(name))
			return c.inst;
		Inst *d = c.inst->ChildByModel(name);
		if (d)
			return d;
	}
	return nullptr;
}

// SPEC-601 §3 (method 31) with the float fraction of SPEC-AMEND-005 G3.14
int Inventory::AbsorbArmor(int damage, float rate, float f)
{
	lastFraction = f;
	if (armor <= 0)
		return damage;
	int a = (int)(damage - damage * f);
	if (rate == 0) {
		if (armor > a) {
			armor -= a;
			return 0;
		}
		int left = a - armor;
		armor = 0;
		return left;
	}
	if (armor > 2 * a) {
		armor -= 2 * a;
		return 0;
	}
	int left = (int)((2 * a - armor) * 0.5f);
	armor = 0;
	return left;
}

} // namespace mock
