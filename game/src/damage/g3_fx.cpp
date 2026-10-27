// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - effect messages sent by the damage model, gore and bodies: named effects
// (svc_effect at a position / on an entity / on a bolt), the temp entities of hits and gibs, entity effect slots,
// with the parental-lock rules for blood.
// Implements: SPEC-201 §1, §5 (message layouts, as a sender), SPEC-771 C.1-C.4 (emission rules used by bodies),
//             SPEC-721 §13 (lock_blood); temp entities through G7 effects/fx.h
#include "g3_local.h"
#include "../effects/fx.h"

static void WriteUnitScaled(const vec3_t v, float s)
{
	vec3_t t = {v[0] * s, v[1] * s, v[2] * s};
	gix.WritePosition(t);
}

// svc_effect (SPEC-201 §5)
static void SendEffect(int idx, int flags, const vec3_t pos, edict_t *ent, IGhoulInst *inst, GhoulID bolt,
                       const vec3_t dir, float scale, const vec3_t mcastOrigin)
{
	if (!idx || !gix.WriteByte)
		return;
	int ext = 0;
	if (dir)
		ext |= 0x08;
	if (scale != 0)
		ext |= 0x01;
	if (ext)
		flags |= 0x80;
	gix.WriteByte(svc_effect);
	gix.WriteByte(idx);
	gix.WriteByte(flags);
	if (flags & 0x01)
		gix.WritePosition(pos);
	else if (flags & 0x02)
		gix.WriteShort(G_EdictNum(ent));
	else if (flags & 0x04) {
		gix.WriteShort(G_EdictNum(ent));
		if (flags & 0x08)
			gix.WriteShort(inst ? inst->GetInstanceId() : 0);
		gix.WriteShort(bolt);
	}
	if (flags & 0x80) {
		gix.WriteByte(ext);
		if (ext & 0x01)
			gix.WriteShort((int)(scale * 128.0f));
		if (ext & 0x08)
			WriteUnitScaled(dir, 2048.0f);
	}
	gix.multicast(mcastOrigin, MULTICAST_PHS);
}

void G3FX_At(const char *effect, const vec3_t pos, const vec3_t dir, float scale)
{
	SendEffect(G3_EffectIndex(effect), 0x01, pos, NULL, NULL, 0, dir, scale, pos);
}

void G3FX_OnEnt(const char *effect, edict_t *ent)
{
	if (ent)
		SendEffect(G3_EffectIndex(effect), 0x02, NULL, ent, NULL, 0, NULL, 0, ent->s.origin);
}

void G3FX_OnBolt(const char *effect, edict_t *ent, GhoulID bolt, IGhoulInst *inst)
{
	if (!ent)
		return;
	int flags = 0x04;
	if (inst && inst != ent->ghoulInst)
		flags |= 0x08;
	SendEffect(G3_EffectIndex(effect), flags, NULL, ent, inst, bolt, NULL, 0, ent->s.origin);
}

// SPEC-771 C.3 fixed-name helpers
void G3FX_StumpBlood(edict_t *ent, GhoulID bolt, IGhoulInst *inst)
{
	if (!bolt)
		return;
	G3FX_OnBolt(g3locks.blood ? "gore/noblood" : "gore/stumpblood", ent, bolt, inst);
}

void G3FX_BloodCloud(edict_t *ent, GhoulID bolt, int size, IGhoulInst *inst)
{
	if (!bolt)
		return;
	const char *e;
	if (size <= 50)
		e = g3locks.blood ? "gore/noblood_s" : "gore/bloodcloud_s";
	else
		e = g3locks.blood ? "gore/noblood" : "gore/bloodcloud";
	G3FX_OnBolt(e, ent, bolt, inst);
}

void G3FX_BloodPool(const vec3_t pos)
{
	if (g3locks.blood)
		return;
	G3FX_At("gore/bloodpool", pos);
}

void G3FX_ExitSpray(const vec3_t pos, const vec3_t dir)
{
	if (g3locks.blood)
		return;
	G3FX_At("environ/gb_exitw", pos, dir);
}

// Temp entities go through G7's emitter (effects/fx.h) so the game-wide rules of SPEC-771 C.2 are shared: the TE 18
// repeat filter (same surface within 0.1 s), the lock_blood rules and the TE 3 impact descriptor.
void G3FX_BloodSplat(const vec3_t point, const vec3_t dir, edict_t *ignore)
{
	vec3_t d;
	VectorCopy(dir, d);
	if (VectorNormalize(d) == 0)
		return;
	fx::BloodSplat(point, d, ignore);
}

void G3FX_Debris(const vec3_t pos, const vec3_t dir, int count, int size, int chunkType, const vec3_t extents,
                 int material)
{
	fx::Debris(pos, dir ? dir : vec3_origin, count, size, chunkType, (int)extents[0], (int)extents[1],
	           (int)extents[2], material);
}

void G3FX_Sparks(int te, const vec3_t pos, const vec3_t dir)
{
	fx::Sparks(te, pos, dir);
}

void G3FX_LargeExplosion(int te, const vec3_t pos, int size, int mode)
{
	if (te == fx::TE_LARGE_EXPLOSION_SND)
		fx::LargeExplosion(pos, size);
	else
		fx::Explosion(te, pos, size, mode);
}

// entity effect slots (SPEC-771 C.4)
void G3FX_SlotAdd(edict_t *ent, const char *effect, GhoulID bolt, float scale)
{
	int idx = G3_EffectIndex(effect);
	if (!ent || !idx)
		return;
	for (int i = 0; i < 4; i++)
		if (ent->s.fx[i][0] == 0) {
			ent->s.fx[i][0] = (short)idx;
			ent->s.fx[i][1] = (short)bolt;
			ent->s.fx[i][2] = (short)(int)(scale * 16.0f);
			return;
		}
}

static int FindSlot(edict_t *ent, int idx, GhoulID bolt)
{
	for (int i = 0; i < 4; i++)
		if (ent->s.fx[i][0] == idx && (bolt == 0 || ent->s.fx[i][1] == bolt))
			return i;
	return -1;
}

void G3FX_SlotRemove(edict_t *ent, const char *effect, GhoulID bolt)
{
	int idx = G3_EffectIndex(effect);
	if (!ent || !idx)
		return;
	int i = FindSlot(ent, idx, bolt);
	if (i >= 0)
		ent->s.fx[i][0] = ent->s.fx[i][1] = ent->s.fx[i][2] = 0;
}

bool G3FX_SlotRescale(edict_t *ent, const char *effect, GhoulID bolt, float scale)
{
	int idx = G3_EffectIndex(effect);
	if (!ent || !idx)
		return false;
	int i = FindSlot(ent, idx, bolt);
	if (i < 0)
		return false;
	ent->s.fx[i][2] = (short)(int)(scale * 16.0f);
	return true;
}

bool G3FX_SlotHas(edict_t *ent, const char *effect, GhoulID bolt)
{
	int idx = G3_EffectIndex(effect);
	return ent && idx && FindSlot(ent, idx, bolt) >= 0;
}
