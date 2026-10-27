// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - model objects: sequence files, name registries, lookups, preparation.
// Implements: SPEC-010 B (29 methods), SPEC-012 section 1 (registration rules, alphabetical ids,
//             always-consuming register methods, frozen lookup tables, characters, model lights),
//             SPEC-012 section 8 (prepare: textures of slots and registered skins), SPEC-013 section 2 (baseline)

#include <algorithm>
#include <cstring>

#include "ghoul_internal.h"

namespace ghoul {

Object::Object(System *s, int oid, bool rec) : sys(s), id(oid), received(rec) {}

Object::~Object()
{
	delete baseline;
}

// ------------------------------------------------------------------ file maps

FileMap &Object::Map(GhbFile *f)
{
	FileMap *m = nullptr;
	for (auto &x : maps)
		if (x.file == f)
		{
			m = &x;
			break;
		}
	if (!m)
	{
		maps.emplace_back();
		m = &maps.back();
		m->file = f;
		m->partNode.assign(1, -1);
		m->nodePart.assign(f->nodes.size(), 0);
		m->matIndex.assign(1, -1);
		m->fileMatId.assign(f->materials.size(), 0);
	}
	while (m->partsMapped < parts.size())
	{
		size_t pid = ++m->partsMapped;
		int node = f->FindNode(parts[pid - 1].name);
		m->partNode.push_back(node);
		if (node >= 0 && m->nodePart[node] == 0)
			m->nodePart[node] = (int)pid;
	}
	while (m->matsMapped < mats.size())
	{
		size_t mid = ++m->matsMapped;
		int fm = f->FindMaterial(mats[mid - 1].name);
		m->matIndex.push_back(fm);
		if (fm >= 0 && m->fileMatId[fm] == 0)
			m->fileMatId[fm] = (int)mid;
	}
	return *m;
}

int Object::NodeOf(GhbFile *f, int part)
{
	if (!f || part <= 0 || part > (int)parts.size())
		return -1;
	FileMap &m = Map(f);
	return m.partNode[part];
}

int Object::PartOfNode(GhbFile *f, int node)
{
	if (!f || node < 0)
		return 0;
	FileMap &m = Map(f);
	return node < (int)m.nodePart.size() ? m.nodePart[node] : 0;
}

int Object::MaterialOf(GhbFile *f, int fileMat)
{
	if (!f || fileMat < 0)
		return 0;
	FileMap &m = Map(f);
	return fileMat < (int)m.fileMatId.size() ? m.fileMatId[fileMat] : 0;
}

int Object::SkinImage(int skin) const
{
	if (skin <= 0 || skin > (int)skins.size())
		return -1;
	return skins[skin - 1].image;
}

ImageList *Object::MaterialList(int material)
{
	if (material <= 0 || material > (int)mats.size())
		return nullptr;
	MaterialEntry &e = mats[material - 1];
	if (!e.listLooked)
	{
		e.listLooked = true;
		for (GhbFile *f : files)
		{
			int fm = f->FindMaterial(e.name);
			if (fm < 0)
				continue;
			const GhbSlot &s = f->materials[fm].slots[0];
			if (s.hasTexture && !s.texture.empty())
				e.list = AcquireImageList(f->dir, s.texture);
			break;
		}
	}
	return e.list;
}

ImageList *Object::SlotList(GhbFile *f, int fileMat, int slot)
{
	if (!f || fileMat < 0 || fileMat >= (int)f->materials.size() || slot < 0 || slot > 1)
		return nullptr;
	const GhbSlot &s = f->materials[fileMat].slots[slot];
	if (!s.hasTexture || s.texture.empty())
		return nullptr;
	if (slot == 0)
	{
		int mid = MaterialOf(f, fileMat);
		if (mid)
		{
			// the material's list comes from the first file that has the material
			ImageList *l = MaterialList(mid);
			if (l || mats[mid - 1].listLooked)
			{
				GhbFile *first = nullptr;
				for (GhbFile *x : files)
					if (x->FindMaterial(mats[mid - 1].name) >= 0)
					{
						first = x;
						break;
					}
				if (first == f || (first && first->materials[first->FindMaterial(mats[mid - 1].name)].slots[0].texture ==
										 s.texture))
					return l;
			}
		}
	}
	std::string key = f->dir + "|" + Upper(s.texture);
	auto it = slotLists.find(key);
	if (it != slotLists.end())
		return it->second;
	ImageList *l = AcquireImageList(f->dir, s.texture);
	slotLists[key] = l;
	return l;
}

GhbFile *Object::AddFile(GhbFile *f)
{
	files.push_back(f);
	if (f->HasLights())
		hasLights = true;
	return f;
}

// ------------------------------------------------------------------ sequences

GhoulID Object::RegisterSequence(const char *path, bool alsoOthers)
{
	if (!path)
		return 0;
	std::string norm = NormalizeName(path);
	sys->strings.Add(norm);
	if (norm.find("GHOUL/ENEMY") != std::string::npos)
		character = true;
	std::string name = BaseName(norm);
	SeqEntry e;
	e.path = norm;
	// rule 2: a sequence of that name in a file already loaded by this object
	for (GhbFile *f : files)
	{
		int si = f->FindSequence(name);
		if (si >= 0)
		{
			e.file = f;
			e.index = si;
			break;
		}
	}
	GhbFile *loaded = nullptr;
	if (!e.file)
	{
		// rule 3: <dir>/<NAME>.ghb, its first sequence whatever its name
		std::string ghb = StripExtension(path) + ".ghb";
		std::string key = StripExtension(NormalizeName(ghb));
		GhbFile *f = nullptr;
		for (GhbFile *x : files)
			if (x->cacheKey == key)
				f = x;
		if (!f)
		{
			f = G().files.Acquire(ghb);
			if (f)
				AddFile(f);
		}
		if (f && !f->sequences.empty())
		{
			e.file = f;
			e.index = 0;
			loaded = f;
		}
	}
	if (!e.file)
	{
		if (received)
		{
			// received objects keep ids aligned with the server: a placeholder takes the id
			e.placeholder = true;
			seqs.push_back(e);
		}
		return 0;
	}
	seqs.push_back(e);
	GhoulID sid = (GhoulID)seqs.size();
	if (alsoOthers && loaded)
	{
		std::string dir = DirectoryOf(norm);
		for (size_t i = 0; i < loaded->sequences.size(); i++)
			RegisterSequence((dir + loaded->sequences[i].nameUpper + ".GHL").c_str(), false);
	}
	return sid;
}

GhoulID Object::FindSequence(const char *path)
{
	if (!path)
		return 0;
	if (!seqTableBuilt)
	{
		seqTableBuilt = true;
		for (size_t i = 0; i < seqs.size(); i++)
			if (!seqs[i].placeholder && !seqTable.count(seqs[i].path))
				seqTable[seqs[i].path] = (int)i + 1;
	}
	auto it = seqTable.find(NormalizeName(path));
	if (it == seqTable.end())
		return 0;
	const SeqEntry *s = Seq(it->second);
	return (s && s->file) ? (GhoulID)it->second : 0;
}

unsigned short Object::NumSequences() { return (unsigned short)seqs.size(); }

void Object::GetSequencePath(GhoulID s, char *out)
{
	const SeqEntry *e = Seq(s);
	SetString(out, e ? e->path : std::string());
}

// ------------------------------------------------------------------ registration

void Object::FinishRegistration(bool allSkins)
{
	std::set<std::string> nodeNames, noteNames, matNames;
	for (GhbFile *f : files)
	{
		for (auto &n : f->nodes)
			nodeNames.insert(n.nameUpper);
		for (auto &s : f->sequences)
			for (auto &e : s.events)
				noteNames.insert(e.typeUpper);
		for (auto &m : f->materials)
			matNames.insert(m.name);
	}
	noteNames.insert("BOS");
	noteNames.insert("EOS");
	// std::string ordering is unsigned byte order: ids are alphabetical ('_' after letters)
	for (auto &n : nodeNames)
		RegisterPart(n.c_str());
	for (auto &n : noteNames)
		RegisterNote(n.c_str());
	for (auto &n : matNames)
		RegisterMaterial(n.c_str());
	if (allSkins)
	{
		size_t nm = mats.size();
		for (size_t m = 1; m <= nm; m++)
		{
			ImageList *l = MaterialList((int)m);
			if (!l)
				continue;
			std::vector<std::string> names(l->images);
			std::sort(names.begin(), names.end());
			for (auto &n : names)
				RegisterSkin((GhoulID)m, n.c_str(), 0);
		}
	}
}

GhoulID Object::RegisterPart(const char *name)
{
	if (!name)
		return 0;
	PartEntry e;
	e.name = NormalizeName(name);
	sys->strings.Add(e.name);
	for (GhbFile *f : files)
	{
		int n = f->FindNode(e.name);
		if (n < 0)
			continue;
		int type = (int)f->nodes[n].kind;
		if (!e.known)
		{
			e.known = true;
			e.type = type;
		}
		else if (e.type != type)
			Fatal("%s Collision between different object types", e.name.c_str());
	}
	parts.push_back(e);
	for (auto &i : insts)
		i.second->SetPartFlagsSize();
	return e.known ? (GhoulID)parts.size() : 0;
}

GhoulID Object::FindPart(const char *name)
{
	if (!name)
		return 0;
	if (!partTableBuilt)
	{
		partTableBuilt = true;
		for (size_t i = 0; i < parts.size(); i++)
			if (!partTable.count(parts[i].name))
				partTable[parts[i].name] = (int)i + 1;
	}
	auto it = partTable.find(NormalizeName(name));
	return it == partTable.end() ? 0 : (GhoulID)it->second;
}

unsigned short Object::NumParts() { return (unsigned short)parts.size(); }

int Object::PartType(GhoulID part)
{
	if (part == 0 || part > parts.size())
		return 0;
	return parts[part - 1].type;
}

void Object::GetPartName(GhoulID part, char *out)
{
	SetString(out, part >= 1 && part <= parts.size() ? parts[part - 1].name : std::string());
}

GhoulID Object::RegisterNote(const char *name)
{
	if (!name)
		return 0;
	std::string n = NormalizeName(name);
	auto it = noteMap.find(n);
	if (it != noteMap.end())
		return (GhoulID)it->second;
	sys->strings.Add(n);
	notes.push_back(n);
	noteMap[n] = (int)notes.size();
	return (GhoulID)notes.size();
}

GhoulID Object::FindNote(const char *name)
{
	if (!name)
		return 0;
	auto it = noteMap.find(NormalizeName(name));
	return it == noteMap.end() ? 0 : (GhoulID)it->second;
}

unsigned short Object::NumNotes() { return (unsigned short)notes.size(); }

void Object::EnumerateNotes(IGhoulCallback *cb, GhoulID note)
{
	if (!cb)
		return;
	std::string want = note >= 1 && note <= notes.size() ? notes[note - 1] : std::string();
	if (note && want.empty())
		return;
	for (auto &s : seqs)
	{
		if (!s.file || s.index < 0)
			continue;
		const GhbSequence &q = s.file->sequences[s.index];
		for (auto &e : q.events)
		{
			if (note)
			{
				if (e.typeUpper == want)
					cb->Event(0, nullptr, 0.0f, e.arg.c_str());
			}
			else
			{
				std::string t = e.typeUpper + " " + e.arg;
				cb->Event(0, nullptr, 0.0f, t.c_str());
			}
		}
	}
}

GhoulID Object::RegisterMaterial(const char *name)
{
	if (!name)
		return 0;
	MaterialEntry e;
	e.name = NormalizeName(name);
	sys->strings.Add(e.name);
	for (GhbFile *f : files)
		if (f->FindMaterial(e.name) >= 0)
		{
			e.known = true;
			break;
		}
	mats.push_back(e);
	return e.known ? (GhoulID)mats.size() : 0;
}

GhoulID Object::FindMaterial(const char *name)
{
	if (!name)
		return 0;
	if (!matTableBuilt)
	{
		matTableBuilt = true;
		for (size_t i = 0; i < mats.size(); i++)
			if (!matTable.count(mats[i].name))
				matTable[mats[i].name] = (int)i + 1;
	}
	auto it = matTable.find(NormalizeName(name));
	return it == matTable.end() ? 0 : (GhoulID)it->second;
}

unsigned short Object::NumMaterials() { return (unsigned short)mats.size(); }

void Object::GetMaterialName(GhoulID material, char *out)
{
	SetString(out, material >= 1 && material <= mats.size() ? mats[material - 1].name : std::string());
}

static std::string SkinKey(const char *name)
{
	std::string n = NormalizeName(name);
	size_t slash = n.find_last_of('/');
	size_t dot = n.find_last_of('.');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		n = n.substr(0, dot);
	return n;
}

GhoulID Object::RegisterSkin(GhoulID material, const char *name, int)
{
	if (!name)
		return 0;
	SkinEntry e;
	e.name = SkinKey(name);
	e.material = material;
	sys->strings.Add(e.name);
	ImageList *l = MaterialList(material);
	if (l)
	{
		for (size_t i = 0; i < l->images.size(); i++)
			if (l->images[i] == e.name)
			{
				e.image = (int)i;
				break;
			}
		if (e.image < 0 && e.name.compare(0, 7, "CUSTOM/") == 0)
			e.image = ImageListAddCustom(l, e.name);
	}
	skins.push_back(e);
	return e.image >= 0 ? (GhoulID)skins.size() : 0;
}

GhoulID Object::FindSkin(GhoulID material, const char *name, int)
{
	if (!name)
		return 0;
	if (!skinTableBuilt)
	{
		skinTableBuilt = true;
		for (size_t i = 0; i < skins.size(); i++)
		{
			auto k = std::make_pair(skins[i].material, skins[i].name);
			if (!skinTable.count(k))
				skinTable[k] = (int)i + 1;
		}
	}
	auto it = skinTable.find(std::make_pair((int)material, SkinKey(name)));
	return it == skinTable.end() ? 0 : (GhoulID)it->second;
}

unsigned short Object::NumSkins() { return (unsigned short)skins.size(); }

void Object::GetSkinName(GhoulID skin, char *out)
{
	SetString(out, skin >= 1 && skin <= skins.size() ? skins[skin - 1].name : std::string());
}

GhoulID Object::SkinMaterial(GhoulID skin)
{
	return skin >= 1 && skin <= skins.size() ? (GhoulID)skins[skin - 1].material : 0;
}

// ------------------------------------------------------------------ preparation

void Object::Prepare(bool spec)
{
	prepared = true;
	specular = spec;
	if (sys->IsServer() && !baselineTaken)
		TakeBaseline();
	// textures: none without a GL table (dedicated server); geometry is already in memory for traces
	if (!G().gl)
		return;
	for (GhbFile *f : files)
	{
		for (size_t fm = 0; fm < f->materials.size(); fm++)
		{
			const GhbMaterial &m = f->materials[fm];
			for (int s = 0; s < 2; s++)
			{
				const GhbSlot &sl = m.slots[s];
				if (!sl.hasTexture || sl.texture.empty())
					continue;
				ImageList *l = SlotList(f, (int)fm, s);
				if (!l)
					continue;
				if (G().gs.precacheAll)
				{
					for (size_t i = 0; i < l->images.size(); i++)
						ListTexture(l, (int)i, true);
				}
				else
					ListTexture(l, 0, true);
			}
		}
	}
	for (auto &sk : skins)
	{
		ImageList *l = MaterialList(sk.material);
		if (l && sk.image >= 0)
			ListTexture(l, sk.image, true);
	}
}

// ------------------------------------------------------------------ instances and destruction

IGhoulInst *Object::CreateInstance() { return sys->NewInst(this, 0); }
unsigned short Object::GetObjectId() { return (unsigned short)id; }
IGhoulSystem *Object::GetSystem() { return sys; }

void Object::DestroyAllInstances()
{
	while (!insts.empty())
		insts.begin()->second->Destroy();
}

void Object::Destroy() { DestroyNow(true); }

void Object::DestroyNow(bool notify)
{
	DestroyAllInstances();
	for (GhbFile *f : files)
		G().files.Release(f);
	files.clear();
	maps.clear();
	for (auto &m : mats)
		if (m.list)
			ReleaseImageList(m.list);
	mats.clear();
	for (auto &l : slotLists)
		if (l.second)
			ReleaseImageList(l.second);
	slotLists.clear();
	sys->ObjectDestroyed(this, notify);
	delete this;
}

} // namespace ghoul
