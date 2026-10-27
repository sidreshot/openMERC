/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: the bolt queries the effects make on Ghoul instances of the server (client Ghoul system
 * of package I1c, found by their server handle): a bolt matrix in the root instance's entity space, a random
 * switched-on part for bolt 0, and the muzzle bolt `flash` of the model hanging on `wbolt_hand_r`. Plain C
 * entry points for fx_ghoul.c. Matrices are SPEC-011 row matrices: rows 0..2 the axes X, Y, Z, row 3 the
 * origin.
 *
 * Implements: SPEC-011 (method 50 bolt matrices, methods 32/33 bolt-ons, object methods 7/8/34, instance
 * method 9), SPEC-400 (svc_effect bolt attachment, bolt 0), SPEC-401 (types 60-63 muzzle point)
 */

#include <cstring>

#include "../../ghoul/ghoul_api.h"
#include "../header/cl_ghoul.h"

using namespace ghoul;

namespace
{
const float FXGB_UNSET = -12345.0f;

IGhoulInst *Inst(int handle)
{
	return handle ? static_cast<IGhoulInst *>(CLG_FindInstance(handle)) : nullptr;
}

/* method 50 in mode 5 (pose with the instance transform) through the parents; false when the bolt is not in
   the current sequence (the output is then left unchanged, SPEC-011) */
bool Bolt(IGhoulInst *inst, GhoulID bolt, float time, float out[16])
{
	Matrix4 m;

	std::memset(&m, 0, sizeof(m));
	m.m[3][3] = FXGB_UNSET;
	inst->GetBoltMatrix(time, &m, bolt, 5, true);

	if (m.m[3][3] == FXGB_UNSET)
	{
		return false;
	}

	std::memcpy(out, m.m, sizeof(float) * 16);
	return true;
}
}

extern "C" int
FXGB_BoltMatrix(int handle, int bolt, float time, float out[16])
{
	IGhoulInst *inst = Inst(handle);

	if (!inst || (bolt <= 0))
	{
		return 0;
	}

	return Bolt(inst, (GhoulID)bolt, time, out) ? 1 : 0;
}

/* SPEC-400 svc_effect bolt 0: a random part of the model that is currently switched on - 6 random tries,
   then a scan from a random start; 0 when none. rnd(n) returns 0..n-1. */
extern "C" int
FXGB_RandomPart(int handle, int (*rnd)(int))
{
	IGhoulInst *inst = Inst(handle);
	IGhoulObject *obj;
	int n, i, start;

	if (!inst || !(obj = inst->GetGhoulObject()))
	{
		return 0;
	}

	n = obj->NumParts();

	if (n <= 0)
	{
		return 0;
	}

	for (i = 0; i < 6; i++)
	{
		GhoulID p = (GhoulID)(rnd(n) + 1);

		if (inst->IsPartOn(p))
		{
			return p;
		}
	}

	start = rnd(n);

	for (i = 0; i < n; i++)
	{
		GhoulID p = (GhoulID)((start + i) % n + 1);

		if (inst->IsPartOn(p))
		{
			return p;
		}
	}

	return 0;
}

/* SPEC-401 types 60-63: the model attached to the shooter's instance at its bolt `wbolt_hand_r`, and on it
   bolt `flash`. Returns 0 when there is no such model (caller: payload start), 1 with the flash bolt
   matrix in the shooter's entity space, 2 when the model has no `flash` bolt (caller: the point (0, 0, 0)). */
extern "C" int
FXGB_MuzzleMatrix(int handle, float time, float out[16])
{
	IGhoulInst *inst = Inst(handle), *child;
	IGhoulObject *obj, *cobj;
	GhoulID hand, at, flash;
	int i, n;

	if (!inst || !(obj = inst->GetGhoulObject()))
	{
		return 0;
	}

	hand = obj->FindPart("wbolt_hand_r");

	if (!hand)
	{
		return 0;
	}

	n = inst->NumChildren();

	for (i = 0; i < n; i++)
	{
		at = 0;
		child = inst->GetChild(i, &at);

		if (!child || (at != hand))
		{
			continue;
		}

		cobj = child->GetGhoulObject();
		flash = cobj ? cobj->FindPart("flash") : 0;

		if (!flash || !Bolt(child, flash, time, out))
		{
			return 2;
		}

		return 1;
	}

	return 0;
}

/* SPEC-401 type 37: whether the instance's model has a part of that name and it is switched on */
extern "C" int
FXGB_HasActivePart(int handle, const char *name)
{
	IGhoulInst *inst = Inst(handle);
	IGhoulObject *obj;
	GhoulID part;

	if (!inst || !(obj = inst->GetGhoulObject()))
	{
		return 0;
	}

	part = obj->FindPart(name);
	return (part && inst->IsPartOn(part)) ? 1 : 0;
}
