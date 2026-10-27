// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 OpenMerc contributors
//
// OpenMerc: the menu runtime's use of the engine's Ghoul model system (package I3, engine/src/ghoul):
// the menu system object (SPEC-010 A, role 2), model objects and instances created the way SPEC-803
// section 1 describes (system 0, object 0/1/2/24), playback, bounds, bolt matrices through parents
// (instance 50, mode 5), bolt-ons (instance 28) and note events (instance 23/45: EOS and sound).
// Drawing the models needs the renderer's menu-model support (package I4b) and is not done here.
//
// Implements: SPEC-803 (section 1), SPEC-010 (A, B), SPEC-011 (C, matrix layout)

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../ghoul/ghoul_api.h"
#include "header/m_ghoul.h"

using namespace ghoul;

extern "C" void SV_Ghoul_Init(void); // engine: once per process, idempotent (package I1b)

namespace {

struct NoteCb : IGhoulCallback
{
	mgmodel_t *model = nullptr;
	int kind = 0;
	bool Event(uint32_t, void *, float, const void *data) override;
};

} // namespace

struct mgmodel_s
{
	IGhoulObject *obj = nullptr;
	IGhoulInst *inst = nullptr;
	GhoulID seq = 0;
	NoteCb eos, sound;
	void (*fn)(void *ctx, int kind, const char *arg) = nullptr;
	void *ctx = nullptr;
	std::vector<IGhoulObject *> extra; // I1c: objects of a player model's bolt-ons (MG_LoadPlayer)
};

bool NoteCb::Event(uint32_t, void *, float, const void *data)
{
	if (model && model->fn)
	{
		model->fn(model->ctx, kind, data ? static_cast<const char *>(data) : "");
	}

	return true;
}

static IGhoulSystem *MenuSystem()
{
	SV_Ghoul_Init();
	return static_cast<IGhoulSystem *>(OMGhoul_GetSystem(OM_GHOUL_MENU));
}

extern "C" mgmodel_t *MG_Load(const char *name, int *noeos)
{
	IGhoulSystem *sys = MenuSystem();
	char path[256];

	if (noeos)
	{
		*noeos = 0;
	}

	if (!sys || !name || !name[0])
	{
		return nullptr;
	}

	IGhoulObject *obj = sys->CreateObject();

	if (!obj)
	{
		return nullptr;
	}

	std::snprintf(path, sizeof(path), "ghoul/menus/%s.ghl", name);
	GhoulID seq = obj->RegisterSequence(path, false);

	if (!seq)
	{
		obj->Destroy();
		return nullptr;
	}

	obj->FinishRegistration(true);
	obj->Prepare(true);
	IGhoulInst *inst = obj->CreateInstance();

	if (!inst)
	{
		obj->Destroy();
		return nullptr;
	}

	mgmodel_t *m = new mgmodel_s;
	m->obj = obj;
	m->inst = inst;
	m->seq = seq;
	m->eos.model = m;
	m->eos.kind = MG_NOTE_EOS;
	m->sound.model = m;
	m->sound.kind = MG_NOTE_SOUND;

	GhoulID eos = obj->FindNote("EOS");

	if (eos)
	{
		inst->AddNoteCallback(&m->eos, eos);
	}
	else if (noeos)
	{
		*noeos = 1;
	}

	GhoulID snd = obj->FindNote("sound");

	if (snd)
	{
		inst->AddNoteCallback(&m->sound, snd);
	}

	return m;
}

extern "C" void MG_Free(mgmodel_t *m)
{
	if (!m)
	{
		return;
	}

	if (m->inst)
	{
		if (m->inst->GetParent())
		{
			m->inst->GetParent()->DetachChild(m->inst);
		}

		m->inst->Destroy();
	}

	if (m->obj)
	{
		m->obj->Destroy();
	}

	for (IGhoulObject *o : m->extra) // I1c: bolt-on objects (their instances died with the parent)
	{
		o->Destroy();
	}

	delete m;
}

// I1c: SPEC-803 section 2 "Rebuilding" - one player model from its description ghoul/pmodels/<file>
// (SPEC-010 D): its menu sequence playing, the skins of the description per part, the "parts off" list and
// the part switches, the bolt-on descriptions attached at their bolts with their scale. Objects of the
// bolt-ons are collected in `extra`.
static IGhoulInst *BuildPlayer(IGhoulSystem *sys, const char *file, float time, std::vector<IGhoulObject *> &objs,
	GhoulID *seqOut, int depth)
{
	IGhoulPlayerModel *pm = static_cast<IGhoulPlayerModel *>(OMGhoul_NewPlayerModel(file));

	if (!pm)
	{
		return nullptr;
	}

	if (!pm->IsLoaded() || depth > 4)
	{
		pm->Destroy(1);
		return nullptr;
	}

	std::string dir = pm->GetModelDir();
	IGhoulObject *obj = sys->CreateObject();

	if (!obj)
	{
		pm->Destroy(1);
		return nullptr;
	}

	GhoulID seq = obj->RegisterSequence(pm->GetMenuSequence(), false);

	if (!seq)
	{
		obj->Destroy();
		pm->Destroy(1);
		return nullptr;
	}

	objs.push_back(obj);
	obj->FinishRegistration(true);

	struct Skin
	{
		GhoulID material, skin, part;
	};
	std::vector<Skin> skins;

	for (int i = 1; i <= pm->NumSkins(); i++)
	{
		PlayerSkinEntry e;

		if (!pm->GetSkinEntry(i, &e))
		{
			continue;
		}

		GhoulID mat = obj->FindMaterial(e.material);
		GhoulID skin = mat ? obj->RegisterSkin(mat, e.skin, 0) : 0;
		GhoulID part = e.part[0] ? obj->FindPart(e.part) : 0;

		if (mat && skin)
		{
			skins.push_back(Skin{mat, skin, part});
		}
	}

	obj->Prepare(true);
	IGhoulInst *inst = obj->CreateInstance();

	if (!inst)
	{
		pm->Destroy(1);
		return nullptr;
	}

	for (const Skin &s : skins)
	{
		inst->SetSkin(s.material, s.skin, s.part);
	}

	OMGhoul_PartsOff(dir.c_str(), (std::string(pm->GetSequenceList()) + "_poff").c_str(), obj, inst);

	for (int i = 1; i <= pm->NumPartSwitches(); i++)
	{
		PlayerPartEntry e;

		if (pm->GetPartEntry(i, &e))
		{
			GhoulID part = obj->FindPart(e.part);

			if (part)
			{
				inst->SetPartOnOff(part, e.shown != 0);
			}
		}
	}

	inst->Play(seq, time, 0.0f, true, kPlayLoop, false, false);

	for (int i = 1; i <= pm->NumBoltons(); i++)
	{
		PlayerBoltonEntry e;

		if (!pm->GetBoltonEntry(i, &e))
		{
			continue;
		}

		IGhoulInst *child = BuildPlayer(sys, e.file, time, objs, nullptr, depth + 1);

		if (!child)
		{
			continue;
		}

		if (e.scale > 0.0f && e.scale != 1.0f)
		{
			Matrix4 m;
			std::memset(&m, 0, sizeof(m));
			m.m[0][0] = m.m[1][1] = m.m[2][2] = e.scale;
			m.m[3][3] = 1.0f;
			child->SetTransform(&m);
		}

		inst->AttachChild(obj->FindPart(e.bolt), child, child->GetGhoulObject()->FindPart(e.boltonBolt));
	}

	if (seqOut)
	{
		*seqOut = seq;
	}

	pm->Destroy(1);
	return inst;
}

extern "C" mgmodel_t *MG_LoadPlayer(const char *file, float time)
{
	IGhoulSystem *sys = MenuSystem();
	std::vector<IGhoulObject *> objs;
	GhoulID seq = 0;

	if (!sys || !file || !file[0])
	{
		return nullptr;
	}

	IGhoulInst *inst = BuildPlayer(sys, file, time, objs, &seq, 0);

	if (!inst)
	{
		for (IGhoulObject *o : objs)
		{
			o->Destroy();
		}

		return nullptr;
	}

	mgmodel_t *m = new mgmodel_s;
	m->obj = objs[0];
	m->inst = inst;
	m->seq = seq;
	m->eos.model = m;
	m->sound.model = m;
	m->extra.assign(objs.begin() + 1, objs.end());
	return m;
}

extern "C" int MG_FindPart(mgmodel_t *m, const char *name)
{
	if (!m || !m->obj || !name)
	{
		return 0;
	}

	return m->obj->FindPart(name);
}

extern "C" void MG_Attach(mgmodel_t *parent, int parentbolt, mgmodel_t *child, int childbolt)
{
	if (parent && child && parent->inst && child->inst)
	{
		parent->inst->AttachChild((GhoulID)parentbolt, child->inst, (GhoulID)childbolt);
	}
}

extern "C" void MG_Play(mgmodel_t *m, float time, int mode, float offset)
{
	if (m && m->inst)
	{
		m->inst->Play(m->seq, time, offset, true, mode, false, false);
		/* OpenMerc MENU2: a static pose (mode 3) keeps its fraction in the instance's aux time; starting the
		   events would overwrite it with the clock and every page showed the end pose (the raised folder tab
		   of t_* pages always at the first tab). Mode 3 produces no events anyway (SPEC-AMEND-005 I6m.7). */
		if (mode != 3)
		{
			m->inst->StartEvents(time);
		}
	}
}

extern "C" void MG_SetTransform(mgmodel_t *m, float scale, const float angles[3])
{
	Matrix4 x;
	float a = angles ? angles[0] : 0, b = angles ? angles[1] : 0, c = angles ? angles[2] : 0;
	float ca = std::cos(a), sa = std::sin(a), cb = std::cos(b), sb = std::sin(b), cc = std::cos(c), sc = std::sin(c);
	// rotation about the first, then the second, then the third axis (row vectors: R = Rx * Ry * Rz);
	// the composition order is UNVERIFIED (SPEC-803 open question 2)
	float rx[3][3] = {{1, 0, 0}, {0, ca, sa}, {0, -sa, ca}};
	float ry[3][3] = {{cb, 0, -sb}, {0, 1, 0}, {sb, 0, cb}};
	float rz[3][3] = {{cc, sc, 0}, {-sc, cc, 0}, {0, 0, 1}};
	float t[3][3], r[3][3];

	if (!m || !m->inst)
	{
		return;
	}

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			t[i][j] = rx[i][0] * ry[0][j] + rx[i][1] * ry[1][j] + rx[i][2] * ry[2][j];
		}
	}

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			r[i][j] = t[i][0] * rz[0][j] + t[i][1] * rz[1][j] + t[i][2] * rz[2][j];
		}
	}

	std::memset(&x, 0, sizeof(x));

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			x.m[i][j] = r[i][j] * scale;
		}
	}

	x.m[3][3] = 1;
	x.flags = (scale == 1.0f && a == 0 && b == 0 && c == 0) ? 1u : 0u;
	m->inst->SetTransform(&x);
}

extern "C" void MG_Bounds(mgmodel_t *m, float time, float mins[3], float maxs[3])
{
	mins[0] = mins[1] = mins[2] = maxs[0] = maxs[1] = maxs[2] = 0;

	if (!m || !m->inst)
	{
		return;
	}

	/* model units: the caller multiplies by the model scale (SPEC-803 section 1) */
	m->inst->GetBounds(time, nullptr, mins, maxs, 0, false);
}

extern "C" void MG_BoltPos(mgmodel_t *m, int bolt, float time, float out[3])
{
	Matrix4 x;

	out[0] = out[1] = out[2] = 0;

	if (!m || !m->inst || !bolt)
	{
		return;
	}

	m->inst->GetBoltMatrix(time, &x, (GhoulID)bolt, kMatrixWithTransform | kMatrixPose, true);
	out[0] = x.m[3][0];
	out[1] = x.m[3][1];
	out[2] = x.m[3][2];
}

extern "C" void MG_SetNoteHandler(mgmodel_t *m, void (*fn)(void *ctx, int kind, const char *arg), void *ctx)
{
	if (m)
	{
		m->fn = fn;
		m->ctx = ctx;
	}
}

extern "C" void MG_ProcessEvents(mgmodel_t *m, float time)
{
	if (m && m->inst)
	{
		m->inst->ProcessEvents(time);
	}
}

extern "C" float MG_Duration(mgmodel_t *m, int mode)
{
	if (!m || !m->inst)
	{
		return 0;
	}

	return m->inst->SequenceDuration(m->seq, mode, nullptr);
}

// I1c: the instance for drawing (cl_ghoul.c)
extern "C" void *MG_Instance(mgmodel_t *m)
{
	return m ? static_cast<void *>(m->inst) : nullptr;
}

extern "C" int MG_PlayerTeam(const char *file, char *team, int size)
{
	MenuSystem();
	IGhoulPlayerModel *p = static_cast<IGhoulPlayerModel *>(OMGhoul_NewPlayerModel(file));
	char buf[256] = {0};
	int ok = 0;

	if (!p)
	{
		return 0;
	}

	if (p->IsLoaded())
	{
		p->GetTeam(buf);
		ok = 1;
	}

	p->Destroy(1);

	if (team && size > 0)
	{
		std::snprintf(team, size, "%s", buf);
	}

	return ok;
}
