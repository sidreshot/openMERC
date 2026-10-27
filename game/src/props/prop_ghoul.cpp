// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — prop Ghoul helpers over G9's glue (see prop_ghoul.h).
// Implements: SPEC-791 Behaviour 9, 10, 11 (as used by SPEC-770)

#include "prop_ghoul.h"

namespace gg {

using ghoul::Matrix4;

static gg_entry_t Entry(const ModelRef &r)
{
	gg_entry_t e;
	memset(&e, 0, sizeof(e));
	e.dir = r.dir;
	e.file = r.file;
	e.skinmaterial = r.skinmat;
	return e;
}

static void Identity(Matrix4 &m)
{
	memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1;
	m.flags = 1;
}

static void Mul(const Matrix4 &a, const Matrix4 &b, Matrix4 &out) // a applied first
{
	Matrix4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += a.m[i][k] * b.m[k][j];
			r.m[i][j] = s;
		}
	r.flags = 0;
	out = r;
}

static void Rotation(int axis, float a, Matrix4 &m)
{
	Identity(m);
	m.flags = 0;
	float c = cosf(a), s = sinf(a);
	int i = (axis + 1) % 3, j = (axis + 2) % 3;
	m.m[i][i] = c;
	m.m[i][j] = s;
	m.m[j][i] = -s;
	m.m[j][j] = c;
}

// ---------------------------------------------------------------------------------------------------------------

bool SetSimpleModel(edict_t *ent, const char *dir, const char *file, const char *skin, const char *material)
{
	return GG_SetSimpleModel(ent, dir, file, skin, material);
}

GhoulID PartId(IGhoulInst *inst, const char *name)
{
	if (!inst || !name)
		return 0;
	IGhoulObject *o = inst->GetGhoulObject();
	return o ? o->FindPart(name) : 0;
}

void HidePart(IGhoulInst *inst, const char *part) { GG_HidePartInst(inst, part); }
void ShowPart(IGhoulInst *inst, const char *part) { GG_ShowPartInst(inst, part); }
void ShowOnlyPart(IGhoulInst *inst, const char *part) { GG_ShowOnlyPart(inst, part); }
void AllParts(IGhoulInst *inst, bool shown) { GG_SetAllParts(inst, shown); }
void HideAllRecursive(IGhoulInst *inst) { GG_HideAllRecursive(inst); }

void SetSkinInst(IGhoulInst *inst, const char *dir, const char *file, const char *material, const char *skin)
{
	GG_SetSkinInst(inst, dir, file, material, skin);
}

GhoulID PlayInstAt(IGhoulInst *inst, const char *name, int mode, float time, float offset)
{
	if (!inst || !name)
		return 0;
	GhoulID seq = GG_FindSequence(GG_ModelOfInst(inst), name);
	if (!seq)
		return 0;
	if (GGInst *oi = GG_ObjInstOf(inst)) {
		GG_Play(oi, seq, time, offset, true, mode);
		return seq;
	}
	edict_t *owner = (edict_t *)inst->GetUserValue();
	GGBolt *b = owner ? GG_FindBoltByInst(GG_ObjInstOfEntity(owner), inst) : NULL;
	if (b)
		GG_PlayBolt(b, seq, time, offset, true, mode);
	else
		inst->Play(seq, time, offset, true, mode, false, false);
	return seq;
}

GhoulID PlayEnt(edict_t *ent, const char *name, bool loop)
{
	if (!ent || !ent->ghoulInst)
		return 0;
	return PlayInstAt(ent->ghoulInst, name, loop ? ghoul::kPlayLoop : ghoul::kPlayOnce, level.time, 0);
}

GhoulID PlayInst(IGhoulInst *inst, const char *name, int code)
{
	switch (code) {
	case 0: return PlayInstAt(inst, name, ghoul::kPlayOnce, level.time, 0);
	case 1: return PlayInstAt(inst, name, ghoul::kPlayLoop, level.time, 0);
	case 2: return PlayInstAt(inst, name, ghoul::kPlayStatic, level.time, 0);
	}
	return 0;
}

void PauseEnt(edict_t *ent)
{
	if (ent && ent->ghoulInst)
		ent->ghoulInst->Pause(level.time);
}

void ScaleInst(IGhoulInst *inst, float f) { GG_ScaleInst(inst, f); }
void ScaleEnt(edict_t *ent, float f) { GG_ScaleModel(ent, f); }

void RotateInst(IGhoulInst *inst, int axis, float radians)
{
	if (!inst)
		return;
	Matrix4 t, r;
	inst->GetTransform(&t);
	Rotation(axis, radians, r);
	Mul(r, t, t);
	inst->SetTransform(&t);
}

void SetInstRotation(IGhoulInst *inst, int axis, float radians)
{
	if (!inst)
		return;
	Matrix4 r;
	Rotation(axis, radians, r);
	inst->SetTransform(&r);
}

void SetInstOffsetRotation(IGhoulInst *inst, const vec3_t ofs, int axis, float radians)
{
	if (!inst)
		return;
	Matrix4 r;
	Rotation(axis, radians, r);
	r.m[3][0] = ofs[0];
	r.m[3][1] = ofs[1];
	r.m[3][2] = ofs[2];
	inst->SetTransform(&r);
}

// ---------------------------------------------------------------------------------------------------------------
// attached-object record

void CreateAttached(edict_t *ent) { GG_AttachedCreate(ent); }
bool HasAttached(edict_t *ent) { return GG_Attached(ent) != NULL; }

IGhoulInst *AddTrackedBolt(edict_t *ent, const ModelRef &parent, const char *parentbolt, const ModelRef &child,
                           const char *childbolt, const char *skin)
{
	gg_entry_t pe = Entry(parent), ce = Entry(child);
	return GG_BoltEngineInst(GG_AddTrackedPart(ent, &pe, parentbolt, &ce, childbolt, skin));
}

IGhoulInst *AddTrackedBoltOnBolt(edict_t *root, IGhoulInst *parentinst, const ModelRef &parent,
                                 const char *parentbolt, const ModelRef &child, const char *childbolt,
                                 const char *skin)
{
	GGBolt *pb = GG_FindBoltByInst(GG_ObjInstOfEntity(root), parentinst);
	if (!pb)
		return NULL;
	gg_entry_t pe = Entry(parent), ce = Entry(child);
	return GG_BoltEngineInst(GG_BoltEntryOnBolt(pb, &pe, parentbolt, &ce, childbolt, skin));
}

IGhoulInst *TrackedPart(edict_t *ent, int i) { return GG_TrackedPart(ent, i); }

int PartCount(edict_t *ent)
{
	GGAttached *a = GG_Attached(ent);
	return a ? a->partcount : 0;
}

bool PartShown(edict_t *ent, int i)
{
	GGAttached *a = GG_Attached(ent);
	return a && i >= 1 && i <= 32 && (a->shownmask & (1u << (i - 1)));
}

void SetPartShown(edict_t *ent, int i, bool shown)
{
	GGAttached *a = GG_Attached(ent);
	if (!a || i < 1 || i > 32)
		return;
	if (shown)
		a->shownmask |= 1u << (i - 1);
	else
		a->shownmask &= ~(1u << (i - 1));
}

edict_t *Link(edict_t *ent)
{
	GGAttached *a = GG_Attached(ent);
	return a ? a->link : NULL;
}

void SetLink(edict_t *ent, edict_t *other)
{
	if (GGAttached *a = GG_Attached(ent))
		a->link = other;
}

// ---------------------------------------------------------------------------------------------------------------
// positions

bool BoltMatrix(IGhoulInst *inst, GhoulID bolt, Matrix4 &m)
{
	Identity(m);
	if (!inst || !bolt)
		return false;
	inst->GetBoltMatrix(level.time, &m, bolt, ghoul::kMatrixWithTransform | ghoul::kMatrixPose, true);
	return true;
}

bool BoltLocal(IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t dir)
{
	Matrix4 m;
	if (!BoltMatrix(inst, PartId(inst, bolt), m)) {
		VectorClear(pos);
		if (dir)
			VectorSet(dir, 1, 0, 0);
		return false;
	}
	pos[0] = m.m[3][0];
	pos[1] = m.m[3][1];
	pos[2] = m.m[3][2];
	if (dir) {
		dir[0] = m.m[0][0];
		dir[1] = m.m[0][1];
		dir[2] = m.m[0][2];
	}
	return true;
}

// entity axes as the renderer builds the entity matrix: forward, -right, up with pitch and roll negated (SPEC-011)
static void EntityAxes(edict_t *ent, vec3_t f, vec3_t l, vec3_t u)
{
	vec3_t a = {-ent->s.angles[0], ent->s.angles[1], -ent->s.angles[2]};
	vec3_t r;
	AngleVectors(a, f, r, u);
	VectorNegate(r, l);
}

void LocalDirToWorld(edict_t *ent, const vec3_t local, vec3_t out)
{
	vec3_t f, l, u;
	EntityAxes(ent, f, l, u);
	for (int i = 0; i < 3; i++)
		out[i] = local[0] * f[i] + local[1] * l[i] + local[2] * u[i];
}

void LocalToWorld(edict_t *ent, const vec3_t local, vec3_t out)
{
	vec3_t d;
	LocalDirToWorld(ent, local, d);
	VectorAdd(ent->s.origin, d, out);
}

bool BoltWorld(edict_t *ent, IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t dir)
{
	vec3_t lp, ld;
	bool ok = BoltLocal(inst ? inst : ent->ghoulInst, bolt, lp, ld);
	LocalToWorld(ent, lp, pos);
	if (dir)
		LocalDirToWorld(ent, ld, dir);
	return ok;
}

void Bounds(IGhoulInst *inst, vec3_t mins, vec3_t maxs)
{
	VectorClear(mins);
	VectorClear(maxs);
	if (!inst)
		return;
	Matrix4 root;
	Identity(root);
	inst->GetBounds(level.time, &root, mins, maxs, 0, false);
}

void BoundsRotated(IGhoulInst *inst, const Matrix4 &m, vec3_t mins, vec3_t maxs)
{
	VectorClear(mins);
	VectorClear(maxs);
	if (!inst)
		return;
	Matrix4 root = m;
	root.m[3][0] = root.m[3][1] = root.m[3][2] = 0;
	root.flags &= ~1u;
	inst->GetBounds(level.time, &root, mins, maxs, 0, false);
}

bool InFrontCone(edict_t *ent, const vec3_t point, float halfangle) { return GG_InFrontCone(ent, point, halfangle); }

// ---------------------------------------------------------------------------------------------------------------
// aim controller

void AimDefaults(AimParams &p)
{
	memset(&p, 0, sizeof(p));
	p.throughparents = true;
}

int Aim(edict_t *ent, AimParams &p, const vec3_t target)
{
	GGAim a;
	GG_AimInit(&a);
	a.ent = ent;
	a.inst = p.inst;
	a.parent = p.parent;
	a.parentbolt = p.parentbolt;
	a.bolt = p.inst ? p.inst->GetAttachBolt() : 0;
	VectorCopy(target, a.target);
	a.pitchmin = p.minpitch;
	a.pitchmax = p.maxpitch;
	a.yawmin = p.minyaw;
	a.yawmax = p.maxyaw;
	a.maxstep = p.step;
	a.yawon = p.yaw;
	a.pitchon = p.pitch;
	a.throughparents = p.throughparents;
	int r = p.inst ? GG_AimPart(&a) : GG_AimEntity(&a);
	p.outyaw = a.yaw;
	p.outpitch = a.pitch;
	return r;
}

// ---------------------------------------------------------------------------------------------------------------
// note callbacks

bool NoteCallback::Event(uint32_t, void *user, float, const void *)
{
	edict_t *e = (edict_t *)user;
	if (fn && e && e->inuse)
		fn(e);
	return true;
}

void OnNote(edict_t *ent, IGhoulInst *inst, const char *note, NoteCallback *cb)
{
	if (!inst)
		inst = ent ? ent->ghoulInst : NULL;
	if (!inst || !cb)
		return;
	if (!cb->registered) {
		GG_RegisterCallback(cb);
		cb->registered = true;
	}
	IGhoulObject *o = inst->GetGhoulObject();
	GhoulID n = o ? o->FindNote(note) : 0;
	if (n)
		inst->AddNoteCallback(cb, n);
}

void RemoveNote(IGhoulInst *inst, const char *note, NoteCallback *cb)
{
	if (!inst)
		return;
	IGhoulObject *o = inst->GetGhoulObject();
	GhoulID n = o ? o->FindNote(note) : 0;
	if (n)
		inst->RemoveNoteCallback(cb, n);
}

} // namespace gg
