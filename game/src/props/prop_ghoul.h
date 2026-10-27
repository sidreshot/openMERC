// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — the Ghoul helpers the props use, as a thin layer over G9's game-side Ghoul
// glue (game/src/ghoulglue/gg_ghoul.h, SPEC-791). Model records, object instances, bolt-ons, attached-object
// records (tracked parts, shown bits, link chain) and savegames are G9's; this file only adds the small helpers
// the prop behaviours need on top (bolt positions in world space, instance transforms, prop-specific aim calls,
// note callbacks with the entity as argument).
//
// Implements: SPEC-791 Behaviour 9, 10, 11 (as used by SPEC-770)
#pragma once

#include "../g_local.h"
#include "../ghoulglue/gg_ghoul.h"

namespace gg {

using ghoul::GhoulID;
using ghoul::IGhoulInst;
using ghoul::IGhoulObject;

// model-table entry fields the bolt helpers need (directory, file, skin material)
struct ModelRef {
	const char *dir;
	const char *file;
	const char *skinmat;
};

// SPEC-791 Behaviour 8
bool SetSimpleModel(edict_t *ent, const char *dir, const char *file, const char *skin = NULL,
                    const char *material = NULL);

// Behaviour 9 name helpers (instance may be NULL: nothing happens)
GhoulID PartId(IGhoulInst *inst, const char *name);
void HidePart(IGhoulInst *inst, const char *part);
void ShowPart(IGhoulInst *inst, const char *part);
void ShowOnlyPart(IGhoulInst *inst, const char *part);
void AllParts(IGhoulInst *inst, bool shown);
void HideAllRecursive(IGhoulInst *inst);
void SetSkinInst(IGhoulInst *inst, const char *dir, const char *file, const char *material, const char *skin);
// play by name on an entity: loop or once-and-hold; on an instance: code 0 once, 1 loop, 2 static pose
GhoulID PlayEnt(edict_t *ent, const char *name, bool loop);
GhoulID PlayInst(IGhoulInst *inst, const char *name, int code);
GhoulID PlayInstAt(IGhoulInst *inst, const char *name, int mode, float time, float offset);
void PauseEnt(edict_t *ent);
void ScaleInst(IGhoulInst *inst, float f);
void ScaleEnt(edict_t *ent, float f);
// transforms of an instance relative to its bolt (bolt-ons)
void RotateInst(IGhoulInst *inst, int axis, float radians);         // rotation about a local axis
void SetInstRotation(IGhoulInst *inst, int axis, float radians);    // replace by a pure rotation
void SetInstOffsetRotation(IGhoulInst *inst, const vec3_t ofs, int axis, float radians);

// attached-object record (G9's GGAttached): tracked parts 1..n, shown bits, link chain
void CreateAttached(edict_t *ent);
bool HasAttached(edict_t *ent);
IGhoulInst *AddTrackedBolt(edict_t *ent, const ModelRef &parent, const char *parentbolt, const ModelRef &child,
                           const char *childbolt, const char *skin);
// bolt onto a direct bolt-on `parentinst` of the entity's model, tracked on the entity's record
IGhoulInst *AddTrackedBoltOnBolt(edict_t *root, IGhoulInst *parentinst, const ModelRef &parent,
                                 const char *parentbolt, const ModelRef &child, const char *childbolt,
                                 const char *skin);
IGhoulInst *TrackedPart(edict_t *ent, int i);
int PartCount(edict_t *ent);
bool PartShown(edict_t *ent, int i);
void SetPartShown(edict_t *ent, int i, bool shown);
edict_t *Link(edict_t *ent);
void SetLink(edict_t *ent, edict_t *other);

// bolt positions (mode 5 = pose including transform, through parents), entity space and world
bool BoltMatrix(IGhoulInst *inst, GhoulID bolt, ghoul::Matrix4 &m);
bool BoltLocal(IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t dir = NULL);
void LocalToWorld(edict_t *ent, const vec3_t local, vec3_t out);
void LocalDirToWorld(edict_t *ent, const vec3_t local, vec3_t out);
bool BoltWorld(edict_t *ent, IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t dir = NULL);
// model bounds of an instance at the current time in its own space (part 0 = all)
void Bounds(IGhoulInst *inst, vec3_t mins, vec3_t maxs);
// bounds of an instance in the axes given by the rotation of matrix m (translation ignored)
void BoundsRotated(IGhoulInst *inst, const ghoul::Matrix4 &m, vec3_t mins, vec3_t maxs);

// Behaviour 10
bool InFrontCone(edict_t *ent, const vec3_t point, float halfangle);

// Behaviour 11 aim controller (forwarded to G9's GGAim). Angles in radians; +-4000 = unlimited.
struct AimParams {
	IGhoulInst *inst;       // part to turn; NULL = turn the entity
	IGhoulInst *parent;     // parent instance of the part
	GhoulID parentbolt;     // bolt of the parent it hangs on
	float minpitch, maxpitch, minyaw, maxyaw;
	float step;
	bool yaw, pitch;
	bool throughparents;
	float outyaw, outpitch;
};
void AimDefaults(AimParams &p);
int Aim(edict_t *ent, AimParams &p, const vec3_t target);

// note callbacks: `fn` runs with the entity stored in the instance's user value. Every callback object is
// registered with G9's savegame callback table on first use (the table order is part of the save format).
typedef void (*note_fn)(edict_t *ent);
class NoteCallback : public ghoul::IGhoulCallback {
public:
	explicit NoteCallback(note_fn f) : fn(f), registered(false) {}
	bool Event(uint32_t, void *user, float, const void *) override;
	note_fn fn;
	bool registered;
};
void OnNote(edict_t *ent, IGhoulInst *inst, const char *note, NoteCallback *cb);
void RemoveNote(IGhoulInst *inst, const char *note, NoteCallback *cb);

} // namespace gg
