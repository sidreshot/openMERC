// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — savegame records of the per-prop state (props_save.h).
// Implements: SPEC-752 2.1 (encodings) for G7's level state; SPEC-770 §2 (sequence reference)

#include "props_save.h"
#include "../save/save_api.h"

namespace props {

PropData *DataTable(void); // prop_core.cpp

// one record per prop; written and read by the same build only (G10 checks the build fingerprint)
struct PropDisk {
	int32_t ent;
	int32_t entry;            // G10_EncodePtr of the model-table entry
	int32_t table, index, seqref;
	int32_t parts;            // G10_EncodePtr of the part table
	int32_t nparts;
	float start_angles[3];
	int32_t pain;
	int32_t fall_dir;
	float fall_turned, fall_speed;
	float fall_pivot[3], fall_axis[3];
	int32_t fall_think, fall_use;
	float t0, t1, t2;
	int32_t i0, i1, i2, i3;
	float f0, f1, f2, f3;
	float v0[3], v1[3];
	int32_t e0;
	int32_t list[10];
	int32_t nlist;
	int32_t next;
	int32_t inst0, inst1;     // tracked part index, 0 none, -1 the entity's own instance
	int32_t flags;
	float box_mins[3], box_maxs[3];
	int32_t push_saved_think;
	float push_saved_next;
};

static int32_t Ptr(const void *p) { return p ? G10_EncodePtr(p) : -1; }

static int32_t InstRef(edict_t *ent, gg::IGhoulInst *inst)
{
	if (!inst)
		return 0;
	if (inst == ent->ghoulInst)
		return -1;
	int n = gg::PartCount(ent);
	for (int k = 1; k <= n; k++)
		if (gg::TrackedPart(ent, k) == inst)
			return k;
	return 0; // not a tracked part: dropped (no pointer into a destroyed instance survives a load)
}

static gg::IGhoulInst *InstOf(edict_t *ent, int32_t ref)
{
	if (ref == -1)
		return ent->ghoulInst;
	if (ref > 0)
		return gg::TrackedPart(ent, ref);
	return NULL;
}

static bool IsEmpty(const PropData &d)
{
	static const PropData zero = {};
	return !memcmp(&d, &zero, sizeof(d));
}

static void V(float *o, const vec3_t i) { o[0] = i[0], o[1] = i[1], o[2] = i[2]; }
static void U(vec3_t o, const float *i) { o[0] = i[0], o[1] = i[1], o[2] = i[2]; }

void WriteLevelProps(void)
{
	PropData *t = DataTable();
	int count = 0;
	for (int i = 0; i < globals.num_edicts && i < MAX_EDICTS; i++)
		if (g_edicts[i].inuse && !IsEmpty(t[i]))
			count++;
	G10_WriteInt("G7PN", count);
	for (int i = 0; i < globals.num_edicts && i < MAX_EDICTS; i++) {
		edict_t *e = g_edicts + i;
		const PropData &d = t[i];
		if (!e->inuse || IsEmpty(d))
			continue;
		PropDisk r;
		memset(&r, 0, sizeof(r));
		r.ent = i;
		r.entry = Ptr(d.entry);
		r.table = d.table, r.index = d.index, r.seqref = d.seqref;
		r.parts = Ptr(d.parts);
		r.nparts = d.nparts;
		V(r.start_angles, d.start_angles);
		r.pain = Ptr((const void *)d.pain);
		r.fall_dir = d.fall_dir;
		r.fall_turned = d.fall_turned, r.fall_speed = d.fall_speed;
		V(r.fall_pivot, d.fall_pivot);
		V(r.fall_axis, d.fall_axis);
		r.fall_think = Ptr((const void *)d.fall_think);
		r.fall_use = Ptr((const void *)d.fall_use);
		r.t0 = d.t0, r.t1 = d.t1, r.t2 = d.t2;
		r.i0 = d.i0, r.i1 = d.i1, r.i2 = d.i2, r.i3 = d.i3;
		r.f0 = d.f0, r.f1 = d.f1, r.f2 = d.f2, r.f3 = d.f3;
		V(r.v0, d.v0);
		V(r.v1, d.v1);
		r.e0 = G10_EncodeEnt(d.e0);
		for (int k = 0; k < 10; k++)
			r.list[k] = G10_EncodeEnt(d.list[k]);
		r.nlist = d.nlist;
		r.next = Ptr((const void *)d.next);
		r.inst0 = InstRef(e, d.inst0);
		r.inst1 = InstRef(e, d.inst1);
		r.flags = d.flags;
		V(r.box_mins, d.box_mins);
		V(r.box_maxs, d.box_maxs);
		r.push_saved_think = Ptr((const void *)d.push_saved_think);
		r.push_saved_next = d.push_saved_next;
		G10_WriteChunk("G7PD", &r, sizeof(r));
	}
}

void ReadLevelProps(void)
{
	PropData *t = DataTable();
	// the spawned level's data belongs to entities the load replaced (and points at destroyed Ghoul instances)
	memset(t, 0, sizeof(PropData) * MAX_EDICTS);
	int count = G10_ReadInt("G7PN");
	for (int c = 0; c < count; c++) {
		PropDisk r;
		if (!G10_ReadChunk("G7PD", &r, sizeof(r)))
			return;
		if (r.ent < 0 || r.ent >= MAX_EDICTS)
			continue;
		edict_t *e = g_edicts + r.ent;
		PropData &d = t[r.ent];
		d.entry = (const ModelEntry *)G10_DecodePtr(r.entry);
		d.table = r.table, d.index = r.index, d.seqref = r.seqref;
		d.parts = (const PartRow *)G10_DecodePtr(r.parts);
		d.nparts = d.parts ? r.nparts : 0;
		U(d.start_angles, r.start_angles);
		d.pain = (prop_pain_t)G10_DecodePtr(r.pain);
		d.fall_dir = r.fall_dir;
		d.fall_turned = r.fall_turned, d.fall_speed = r.fall_speed;
		U(d.fall_pivot, r.fall_pivot);
		U(d.fall_axis, r.fall_axis);
		d.fall_think = (void (*)(edict_t *))G10_DecodePtr(r.fall_think);
		d.fall_use = (void (*)(edict_t *, edict_t *, edict_t *))G10_DecodePtr(r.fall_use);
		d.t0 = r.t0, d.t1 = r.t1, d.t2 = r.t2;
		d.i0 = r.i0, d.i1 = r.i1, d.i2 = r.i2, d.i3 = r.i3;
		d.f0 = r.f0, d.f1 = r.f1, d.f2 = r.f2, d.f3 = r.f3;
		U(d.v0, r.v0);
		U(d.v1, r.v1);
		d.e0 = G10_DecodeEnt(r.e0);
		for (int k = 0; k < 10; k++)
			d.list[k] = G10_DecodeEnt(r.list[k]);
		d.nlist = r.nlist;
		d.next = (void (*)(edict_t *))G10_DecodePtr(r.next);
		d.inst0 = e->inuse ? InstOf(e, r.inst0) : NULL;
		d.inst1 = e->inuse ? InstOf(e, r.inst1) : NULL;
		d.flags = r.flags;
		U(d.box_mins, r.box_mins);
		U(d.box_maxs, r.box_maxs);
		d.push_saved_think = (void (*)(edict_t *))G10_DecodePtr(r.push_saved_think);
		d.push_saved_next = r.push_saved_next;
	}
}

static G10Extension s_ext("G7PD", WriteLevelProps, ReadLevelProps);

} // namespace props
