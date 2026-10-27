/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: models and instances the effects create themselves on the client Ghoul system (debris,
 * Ghoul-model particles). They get positive client ids k; the renderer finds them through the entity's
 * frame = -k (SPEC-AMEND-005 I4b.1: system method 9 negates on the client, in 16 bits) and the entity flags
 * Ghoul model + ambient-only lighting (I4b.3). Objects and instances die with a reset of the client system
 * (map change, disconnect: I1c's method 8), so every record is checked against the system by id before use
 * and reloaded / dropped when stale. Plain C entry points for fx_ghoul.c.
 *
 * Implements: SPEC-400 (Debris 1-2, Ghoul particles, Rendering hand-off 2/3), SPEC-401 (debris chunk parts and
 * skins), SPEC-010 (A methods 0/9/10, B typical model set-up), SPEC-011 (instance methods 1/8/10/11/14/17/39/53),
 * SPEC-AMEND-005 I4b.1, I4b.3, I5.9
 */

#include <cctype>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "../../ghoul/ghoul_api.h"
#include "../header/cl_ghoul.h"

using namespace ghoul;

namespace
{
struct FxModel
{
	std::string path;
	IGhoulObject *obj = nullptr;
	int id = 0;          /* client object id (positive) */
	GhoulID seq = 0;
	bool failed = false; /* the sequence did not load */
};

struct FxInst
{
	IGhoulInst *inst;
	int id;              /* client instance id (positive) */
};

std::map<std::string, FxModel *> models;

IGhoulSystem *Sys()
{
	return static_cast<IGhoulSystem *>(CL_Ghoul_System());
}

/* client-created records are found under the negated argument of methods 9/10 (16-bit ids) */
short Local(int id)
{
	return (short)(-id);
}

bool Alive(FxModel *m)
{
	IGhoulSystem *sys = Sys();
	return sys && m->obj && m->id && (sys->FindObject(Local(m->id)) == m->obj);
}

bool Alive(FxInst *fi)
{
	IGhoulSystem *sys = Sys();
	return sys && fi && fi->inst && fi->id && (sys->FindInstance(Local(fi->id)) == fi->inst);
}

/* SPEC-010 typical set-up: object, sequence, finish (all skins), prepare */
void Load(FxModel *m)
{
	IGhoulSystem *sys = Sys();

	m->obj = nullptr;
	m->id = 0;
	m->seq = 0;

	if (!sys || m->failed)
	{
		return;
	}

	IGhoulObject *obj = sys->CreateObject();

	if (!obj)
	{
		return;
	}

	GhoulID seq = obj->RegisterSequence(m->path.c_str(), false);

	if (!seq)
	{
		obj->Destroy();
		m->failed = true;
		return;
	}

	obj->FinishRegistration(true);
	obj->Prepare(true);
	m->obj = obj;
	m->id = obj->GetObjectId();
	m->seq = seq;
}

bool SameNoCase(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
		{
			return false;
		}
	}

	return *a == *b;
}

GhoulID FindNoCase(IGhoulObject *obj, const char *name, bool material)
{
	char buf[256];
	int n = material ? obj->NumMaterials() : obj->NumParts();
	GhoulID id = material ? obj->FindMaterial(name) : obj->FindPart(name);

	if (id || !name || !name[0])
	{
		return id;
	}

	for (int i = 1; i <= n; i++)
	{
		buf[0] = 0;

		if (material)
		{
			obj->GetMaterialName((GhoulID)i, buf);
		}
		else
		{
			obj->GetPartName((GhoulID)i, buf);
		}

		if (SameNoCase(buf, name))
		{
			return (GhoulID)i;
		}
	}

	return 0;
}
} // namespace

/* a model record by its sequence path ("ghoul/<dir>/<file>.ghl"), cached and (re)loaded on the client
   system whenever needed; NULL only when the sequence cannot be loaded */
extern "C" void *
FXGM_Model(const char *path)
{
	if (!path || !path[0] || !Sys())
	{
		return nullptr;
	}

	FxModel *&m = models[path];

	if (!m)
	{
		m = new FxModel;
		m->path = path;
	}

	if (!Alive(m))
	{
		Load(m); /* also after a reset of the client system */
	}

	return m->failed ? nullptr : m;
}

/* name of the index-th part of the model in case-insensitive alphabetical order (the debris chunk models carry
   their chunk shapes as parts; SPEC-401 picks one by number). Returns 0 when there is no such part. */
extern "C" int
FXGM_PartName(void *model, int index, char *out, int size)
{
	FxModel *m = static_cast<FxModel *>(model);

	if (!out || (size < 1) || (index < 0) || !m || (!Alive(m) && (!FXGM_Model(m->path.c_str()) || !m->obj)))
	{
		return 0;
	}

	std::vector<std::string> names;
	int n = m->obj->NumParts();

	for (int i = 1; i <= n; i++)
	{
		char buf[256];

		buf[0] = 0;
		m->obj->GetPartName((GhoulID)i, buf);

		if (buf[0])
		{
			names.emplace_back(buf);
		}
	}

	std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
		for (size_t i = 0;; i++)
		{
			int ca = (i < a.size()) ? tolower((unsigned char)a[i]) : -1;
			int cb = (i < b.size()) ? tolower((unsigned char)b[i]) : -1;

			if ((ca != cb) || (ca < 0))
			{
				return ca < cb;
			}
		}
	});

	if (index >= (int)names.size())
	{
		return 0;
	}

	snprintf(out, size, "%s", names[index].c_str());
	return 1;
}

/* a new instance of the model, playing its sequence from `time`; skin = (material group, skin name) or NULL;
   part: NULL = all parts as the model has them, else only that part on ("" = every part off: the invisible
   chunk of SPEC-401's inclusive part choice) */
extern "C" void *
FXGM_NewInstance(void *model, float time, const char *material, const char *skin, const char *part)
{
	FxModel *m = static_cast<FxModel *>(model);

	if (!m || (!Alive(m) && (!FXGM_Model(m->path.c_str()) || !m->obj)))
	{
		return nullptr;
	}

	IGhoulInst *inst = m->obj->CreateInstance();

	if (!inst)
	{
		return nullptr;
	}

	inst->Play(m->seq, time, 0.0f, true, kPlayLoop, false, false);

	if (material && skin && skin[0])
	{
		GhoulID mat = FindNoCase(m->obj, material, true);
		GhoulID sk = mat ? m->obj->FindSkin(mat, skin, 0) : 0;

		if (mat && sk)
		{
			inst->SetSkin(mat, sk, 0);
		}
	}

	if (part)
	{
		GhoulID p = part[0] ? FindNoCase(m->obj, part, false) : 0;

		inst->SetAllPartsOnOff(false);

		if (p)
		{
			inst->SetPartOnOff(p, true);
		}
	}

	FxInst *fi = new FxInst;
	fi->inst = inst;
	fi->id = inst->GetInstanceId();
	return fi;
}

extern "C" void
FXGM_FreeInstance(void *instance)
{
	FxInst *fi = static_cast<FxInst *>(instance);

	if (!fi)
	{
		return;
	}

	if (Alive(fi))
	{
		fi->inst->Destroy();
	}

	delete fi;
}

/* the renderer entity's frame for the instance (-id), 0 when the instance is gone (system reset) */
extern "C" int
FXGM_Frame(void *instance)
{
	FxInst *fi = static_cast<FxInst *>(instance);
	return Alive(fi) ? -fi->id : 0;
}

/* uniform scale in the instance transform and the model tint's alpha (SPEC-400 Particle simulation 3) */
extern "C" void
FXGM_SetScaleAlpha(void *instance, float scale, float alpha)
{
	FxInst *fi = static_cast<FxInst *>(instance);
	float m[16];

	if (!Alive(fi))
	{
		return;
	}

	std::memset(m, 0, sizeof(m));
	m[0] = m[5] = m[10] = scale;
	m[15] = 1.0f;
	fi->inst->SetTransform16(m);

	if (alpha >= 0)
	{
		fi->inst->SetTint(1.0f, 1.0f, 1.0f, alpha);
	}
}
