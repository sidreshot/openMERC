// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - writers for the effect message (svc_effect), entity effect slots, the scaled
// vector encodings of temp entities, and sound helpers.
// Implements: SPEC-201 1 (unit encodings), SPEC-201 5 (svc_effect), SPEC-202 2 (effect slots)

#include "g_local.h"

void WriteUnit1000(const vec3_t v)
{
	vec3_t s;
	VectorScale(v, 1000.0f, s);
	gi.WritePosition(s);
}

void WriteUnit2048(const vec3_t v)
{
	vec3_t s;
	VectorScale(v, 2048.0f, s);
	gi.WritePosition(s);
}

void WriteImpactWorld(const vec3_t point, const vec3_t normal)
{
	gi.WriteByte(0);
	gi.WritePosition(point);
	WriteUnit1000(normal);
}

static void FX_Send(const vec3_t origin, int to, edict_t *exclude)
{
	if (exclude)
		gix.multicastexclude(origin, to, G_EdictNum(exclude));
	else
		gi.multicast(origin, to);
}

void FX_Effect(int effect, const vec3_t origin, int to, edict_t *exclude)
{
	if (effect <= 0 || effect > 255)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect);
	gi.WriteByte(0x01);
	gi.WritePosition(origin);
	FX_Send(origin, to, exclude);
}

void FX_EffectDir(int effect, const vec3_t origin, const vec3_t dir, int to, edict_t *exclude)
{
	if (effect <= 0 || effect > 255)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect);
	gi.WriteByte(0x81);
	gi.WritePosition(origin);
	gi.WriteByte(0x08);
	WriteUnit2048(dir);
	FX_Send(origin, to, exclude);
}

void FX_EffectBeam(int effect, const vec3_t start, const vec3_t end, int to, edict_t *exclude)
{
	if (effect <= 0 || effect > 255)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect);
	gi.WriteByte(0x81);
	gi.WritePosition(start);
	gi.WriteByte(0x04);
	gi.WritePosition(end);
	FX_Send(start, to, exclude);
}

void FX_EffectEnt(int effect, edict_t *ent, int to)
{
	if (effect <= 0 || effect > 255)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect);
	gi.WriteByte(0x02);
	gi.WriteShort(G_EdictNum(ent));
	gi.multicast(ent->s.origin, to);
}

void FX_EffectBolt(int effect, edict_t *owner, int bolt, int to)
{
	if (effect <= 0 || effect > 255)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect);
	gi.WriteByte(0x04);
	gi.WriteShort(G_EdictNum(owner));
	gi.WriteShort(bolt);
	gi.multicast(owner->s.origin, to);
}

void FX_SetSlot(edict_t *ent, int slot, const char *effect, int bolt, float scale)
{
	if (slot < 0 || slot > 3)
		return;
	ent->s.fx[slot][0] = effect ? (short)gi.effectindex(effect) : 0;
	ent->s.fx[slot][1] = (short)bolt;
	ent->s.fx[slot][2] = (short)(scale * 16);
}

void FX_ClearSlots(edict_t *ent) { memset(ent->s.fx, 0, sizeof(ent->s.fx)); }

void G_Sound(edict_t *ent, int chan, const char *name, float vol, float att, int local)
{
	gi.sound(ent, chan, gi.soundindex(name), vol, att, 0, local);
}

void G_PosSound(const vec3_t org, edict_t *ent, int chan, const char *name, float vol, float att)
{
	gi.positioned_sound(org, ent, chan, gi.soundindex(name), vol, att, 0);
}
