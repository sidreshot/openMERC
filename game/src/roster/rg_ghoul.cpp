// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: Ghoul helpers of the roster on top of G9's SPEC-791 glue (model records,
// object instances, bolt-ons on instances and on bolt-ons), silent skin/part name helpers (SPEC-722 "Set skin ...
// fails silently"), transforms of turned parts, bolt positions in world space, and the effect and temp-entity
// messages the vehicles send (SPEC-201 §1/§5, SPEC-202 effect slots).
//
// Implements: SPEC-791 §7/§9 (callers' side), SPEC-011 (bolt matrices), SPEC-201 §5, SPEC-202 effect slots,
// SPEC-761 §3.12 (camera shake rule)
#include "rg_local.h"
#include "../effects/fx.h"

using namespace ghoul;

// ---------------------------------------------------------------------------------------------------------------
// model records and instances: G9's SPEC-791 glue
// ---------------------------------------------------------------------------------------------------------------
RgModel *rg_FindModel(const char *dir, const char *list, bool allskins, const char *skin, const char *base)
{
	return GG_FindModel(dir, list, allskins, skin, base);
}

IGhoulInst *rg_NewInstance(RgModel *m, edict_t *ent)
{
	return GG_EngineInst(GG_NewInst(m, ent));
}

GhoulID rg_Bolt(IGhoulInst *inst, const char *bolt)
{
	if (!inst || !bolt || !bolt[0])
		return 0;
	IGhoulObject *o = inst->GetGhoulObject();
	return o ? o->FindPart(bolt) : 0;
}

// "rotate yaw degrees about the vertical axis, then scale" (SPEC-730 §1 step 3). The sense of the rotation is the
// one the original sends: -90 maps model X to entity +Y (packed rotation 0x00088703 in every original capture,
// G2's measurement in docs/impl/G2.md)
void rg_SetTransform(IGhoulInst *inst, float yawdeg, float scale)
{
	if (!inst)
		return;
	float a = yawdeg * (float)(M_PI / 180.0);
	float c = cosf(a), s = sinf(a);
	Matrix4 m;
	memset(&m, 0, sizeof(m));
	m.m[0][0] = c * scale;
	m.m[0][1] = -s * scale;
	m.m[1][0] = s * scale;
	m.m[1][1] = c * scale;
	m.m[2][2] = scale;
	m.m[3][3] = 1;
	m.flags = (yawdeg == 0 && scale == 1) ? 1 : 0;
	inst->SetTransform(&m);
}

// a turned part's own transform: rest orientation rotated by pitch about its y axis, then by yaw about z,
// so that its forward (x) axis points to (cos p cos y, cos p sin y, sin p)
void rg_SetPartRotation(IGhoulInst *inst, float yaw, float pitch, float scale)
{
	if (!inst)
		return;
	float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
	Matrix4 m;
	memset(&m, 0, sizeof(m));
	// row 0 = image of x, row 1 = image of y, row 2 = image of z
	m.m[0][0] = cp * cy * scale;
	m.m[0][1] = cp * sy * scale;
	m.m[0][2] = sp * scale;
	m.m[1][0] = -sy * scale;
	m.m[1][1] = cy * scale;
	m.m[1][2] = 0;
	m.m[2][0] = -sp * cy * scale;
	m.m[2][1] = -sp * sy * scale;
	m.m[2][2] = cp * scale;
	m.m[3][3] = 1;
	m.flags = 0;
	inst->SetTransform(&m);
}

// the glue's bolt-on record of an engine instance anywhere in owner's tree
static GGBolt *rg_FindBoltRec(GGBolt *b, IGhoulInst *inst)
{
	for (; b; b = GG_NextBolt(b)) {
		if (GG_BoltEngineInst(b) == inst)
			return b;
		GGBolt *c = rg_FindBoltRec(GG_FirstChildBolt(b), inst);
		if (c)
			return c;
	}
	return NULL;
}

IGhoulInst *rg_AddBolt(edict_t *owner, IGhoulInst *parent, const char *pbolt, RgModel *child, const char *cbolt,
                       const char *skin, float scale)
{
	if (!owner || !parent || !child)
		return NULL;
	IGhoulObject *co = GG_Object(child);
	GhoulID pb = rg_Bolt(parent, pbolt);
	GhoulID cb = co ? co->FindPart(cbolt) : 0;
	GGBolt *b = NULL;
	if (parent == owner->ghoulInst)
		b = GG_AddBoltChecked(GG_ObjInstOf(parent), pb, child, cb, skin, scale);
	else {
		GGBolt *pr = rg_FindBoltRec(GG_FirstBolt(GG_ObjInstOfEntity(owner)), parent);
		if (pr && pb && cb)
			b = GG_AddBoltToBolt(pr, pb, child, cb, skin, scale);
		else
			gi.dprintf("Unable to add bolt on!\n");
	}
	return b ? GG_BoltEngineInst(b) : NULL;
}

IGhoulInst *rg_ChildOnBolt(IGhoulInst *parent, const char *pbolt)
{
	GhoulID pb = rg_Bolt(parent, pbolt);
	if (!pb)
		return NULL;
	int n = parent->NumChildren();
	for (int i = 0; i < n; i++) {
		GhoulID b = 0;
		IGhoulInst *c = parent->GetChild(i, &b);
		if (c && b == pb)
			return c;
	}
	return NULL;
}

bool rg_HasSkin(IGhoulInst *inst, const char *material, const char *skin)
{
	if (!inst)
		return false;
	IGhoulObject *o = inst->GetGhoulObject();
	if (!o)
		return false;
	GhoulID mat = o->FindMaterial(material);
	return mat && o->FindSkin(mat, skin, 0);
}

bool rg_SetSkin(IGhoulInst *inst, const char *material, const char *skin)
{
	if (!inst)
		return false;
	IGhoulObject *o = inst->GetGhoulObject();
	if (!o)
		return false;
	GhoulID mat = o->FindMaterial(material);
	if (!mat)
		return false;
	GhoulID sk = o->FindSkin(mat, skin, 0);
	if (!sk)
		return false;
	inst->SetSkin(mat, sk, 0);
	return true;
}

bool rg_SetPart(IGhoulInst *inst, const char *part, bool shown)
{
	if (!inst)
		return false;
	IGhoulObject *o = inst->GetGhoulObject();
	GhoulID p = o ? o->FindPart(part) : 0;
	if (!p) {
		if (!shown)
			gi.dprintf("ERROR: SimpleModelRemoveObject - can find part %s!\n", part);
		return false;
	}
	inst->SetPartOnOff(p, shown);
	return true;
}

GhoulID rg_Play(IGhoulInst *inst, const char *seq, int mode)
{
	int code = mode == kPlayLoop ? 1 : mode == kPlayStatic ? 2 : 0;
	return GG_PlayByNameInst(inst, seq, code);
}

float rg_SeqDuration(IGhoulInst *inst, const char *seq, int mode)
{
	GhoulID s = GG_FindSequence(GG_ModelOfInst(inst), seq);
	if (!s)
		return 0;
	float ft = 0;
	return inst->SequenceDuration(s, mode, &ft);
}

bool rg_HasSequence(IGhoulInst *inst, const char *seq)
{
	RgModel *m = GG_ModelOfInst(inst);
	return m && GG_Prepared(m) && GG_FindSequence(m, seq) != 0;
}

// entity space: x forward, y left, z up, turned by the entity's full angles (SPEC-AMEND-006 G4.9: the aim step works
// in the full entity transform followed by the parent's bolt matrix; the helicopter's parts stay at rest in dives
// because "ahead" is taken from the full angles too, see heli_Ahead)
static void rg_AimAxes(edict_t *ent, vec3_t f, vec3_t r, vec3_t u)
{
	AngleVectors(ent->s.angles, f, r, u);
}

void rg_EntityToWorld(edict_t *ent, const vec3_t p, vec3_t out, bool point)
{
	vec3_t f, r, u;
	rg_AimAxes(ent, f, r, u);
	vec3_t o;
	for (int i = 0; i < 3; i++)
		o[i] = p[0] * f[i] - p[1] * r[i] + p[2] * u[i] + (point ? ent->s.origin[i] : 0);
	VectorCopy(o, out);
}

void rg_WorldToEntity(edict_t *ent, const vec3_t p, vec3_t out, bool point)
{
	vec3_t f, r, u, d;
	rg_AimAxes(ent, f, r, u);
	if (point)
		VectorSubtract(p, ent->s.origin, d);
	else
		VectorCopy(p, d);
	vec3_t o;
	o[0] = DotProduct(d, f);
	o[1] = -DotProduct(d, r);
	o[2] = DotProduct(d, u);
	VectorCopy(o, out);
}

bool rg_BoltWorld(edict_t *ent, IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t fwd, vec3_t left, vec3_t up)
{
	if (!ent || !inst)
		return false;
	GhoulID b = rg_Bolt(inst, bolt);
	if (!b)
		return false;
	Matrix4 m;
	memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1;
	m.flags = 1;
	inst->GetBoltMatrix(level.time, &m, b, kMatrixWithTransform | kMatrixPose, true);
	vec3_t t = {m.m[3][0], m.m[3][1], m.m[3][2]};
	rg_EntityToWorld(ent, t, pos, true);
	vec3_t a;
	if (fwd) {
		VectorSet(a, m.m[0][0], m.m[0][1], m.m[0][2]);
		rg_EntityToWorld(ent, a, fwd, false);
		VectorNormalize(fwd);
	}
	if (left) {
		VectorSet(a, m.m[1][0], m.m[1][1], m.m[1][2]);
		rg_EntityToWorld(ent, a, left, false);
		VectorNormalize(left);
	}
	if (up) {
		VectorSet(a, m.m[2][0], m.m[2][1], m.m[2][2]);
		rg_EntityToWorld(ent, a, up, false);
		VectorNormalize(up);
	}
	return true;
}

// muzzle bolt: position and firing direction. The direction of a muzzle bolt is its z axis: in every stock vehicle
// model the muzzle bolts' z axis runs along the barrel (enemy/tank FLASH_CANON and FLASH_MACHINE_GUN, enemy/chopper
// FLASH_BARREL and FLASH_ROCKET1/2, enemy/snowcat _FLASH1), and the original's cannon shell on irq2b flies along it
// (impact point in the capture), not along the bolt's x axis.
bool rg_MuzzleWorld(edict_t *ent, IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t dir)
{
	vec3_t f, l, u;
	if (!rg_BoltWorld(ent, inst, bolt, pos, f, l, u))
		return false;
	if (dir)
		VectorCopy(u, dir);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Effects: through G7's shared emitter (effects/fx.h, SPEC-771 C rules)
// ---------------------------------------------------------------------------------------------------------------
void rg_EffectAt(const char *effect, const vec3_t org, const vec3_t dir, float scale)
{
	if (scale > 0)
		fx::ExtScale(scale);
	if (dir)
		fx::ExtDir(dir);
	fx::EffectAtName(effect, org);
}

void rg_EffectOnBolt(const char *effect, edict_t *owner, IGhoulInst *inst, const char *bolt, float scale)
{
	if (!owner)
		return;
	if (!inst)
		inst = owner->ghoulInst;
	GhoulID b = rg_Bolt(inst, bolt);
	if (!b) {
		rg_EffectAt(effect, owner->s.origin, NULL, scale);
		return;
	}
	if (scale > 0)
		fx::ExtScale(scale);
	if (inst == owner->ghoulInst)
		fx::EffectBolt(fx::Index(effect), owner, b);
	else
		fx::EffectInstBolt(fx::Index(effect), owner, inst->GetInstanceId(), b);
}

void rg_SetEffectSlot(edict_t *ent, const char *effect, const char *bolt, float scale)
{
	if (!ent || !effect)
		return;
	fx::SlotAddName(ent, effect, bolt ? rg_Bolt(ent->ghoulInst, bolt) : 0, scale);
}

void rg_ClearEffectSlot(edict_t *ent, const char *effect)
{
	if (ent && effect)
		fx::SlotRemove(ent, fx::Index(effect), 0);
}

// SPEC-761 §3.12, one tick (parameter 30)
void rg_CameraShake(const vec3_t point, float maxint, float radius) { fx::ShakeTick(point, maxint, radius, 30); }
