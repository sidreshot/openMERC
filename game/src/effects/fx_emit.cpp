// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — server-side effect emission rules (see fx.h).
// Implements: SPEC-771 C, SPEC-761 3.12, SPEC-201 1/5 (message forms written by the game)

#include "fx.h"
#include "../damage/damage_api.h"

namespace fx {

ai_noise_fn g_ai_noise = NULL;

// game-wide filters (C.2)
static int s_debris_surface = -1;
static float s_debris_time = -999;
static float s_impact_time = -999999;

// pending extension values (C.1)
static int s_ext_flags;
static float s_ext_scale;
static vec3_t s_ext_pos2;
static vec3_t s_ext_dir;

void LevelReset(void)
{
	s_debris_surface = -1;
	s_debris_time = -999;
	s_impact_time = -999999;
	s_ext_flags = 0;
}

bool BloodLocked(void)
{
	// the parental locks are read once at game start (SPEC-721 §13), by G3
	return g3locks.blood;
}

// ---------------------------------------------------------------------------------------------------------------
// primitives

static void WriteScaled(const vec3_t v, float k)
{
	vec3_t t = {v[0] * k, v[1] * k, v[2] * k};
	gi.WritePosition(t);
}

static void WriteDirByte(const vec3_t d)
{
	if (!d) {
		gi.WriteByte(0);
		return;
	}
	gi.WriteDir(d);
}

static bool IsCreature(const edict_t *e) { return e && (e->client || (e->svflags & SVF_MONSTER)); }

static int SurfaceMaterial(const trace_t &tr)
{
	if (!tr.surface)
		return 0;
	return (int)(((unsigned)tr.surface->flags >> 24) & 0xFF);
}

// ---------------------------------------------------------------------------------------------------------------
// impact descriptors

void ImpactFromTrace(const trace_t &tr, Impact &out)
{
	memset(&out, 0, sizeof(out));
	edict_t *e = tr.ent;
	if (!e || e == g_edicts) {
		VectorCopy(tr.endpos, out.point);
		VectorCopy(tr.plane.normal, out.normal);
		out.kind = (out.normal[0] || out.normal[1] || out.normal[2]) ? 0 : 1;
		return;
	}
	if (e->solid == SOLID_BSP) {
		out.kind = 2;
		vec3_t ofs;
		VectorSubtract(tr.endpos, e->s.origin, ofs);
		float a = -e->s.angles[YAW] * (float)(M_PI / 180.0);
		float c = cosf(a), s = sinf(a);
		out.point[0] = ofs[0] * c - ofs[1] * s;
		out.point[1] = ofs[0] * s + ofs[1] * c;
		out.point[2] = ofs[2];
		out.normal[0] = tr.plane.normal[0] * c - tr.plane.normal[1] * s;
		out.normal[1] = tr.plane.normal[0] * s + tr.plane.normal[1] * c;
		out.normal[2] = tr.plane.normal[2];
		out.entnum = G_EdictNum(e);
		// secondary-model test (UNVERIFIED reading, SPEC-771 OQ 3)
		if (e->s.modelindex2 == 0)
			out.material = SurfaceMaterial(tr);
		else
			out.material = e->surfacetype ? e->surfacetype : SurfaceMaterial(tr);
		return;
	}
	out.kind = 3;
	VectorCopy(tr.endpos, out.point);
	out.normal[0] = tr.endpos[0] - e->s.origin[0];
	out.normal[1] = tr.endpos[1] - e->s.origin[1];
	out.normal[2] = 0;
	out.entnum = G_EdictNum(e);
	out.material = IsCreature(e) ? 35 : e->surfacetype;
}

void ImpactAlong(const vec3_t P, const vec3_t d, edict_t *ignore, Impact &out)
{
	vec3_t a, b;
	VectorMA(P, 5, d, a);
	VectorMA(P, -35, d, b);
	trace_t tr = gi.trace(a, vec3_origin, vec3_origin, b, ignore, 3);
	if (tr.fraction < 0.99f) {
		ImpactFromTrace(tr, out);
		return;
	}
	memset(&out, 0, sizeof(out));
	out.kind = 1;
	VectorCopy(P, out.point);
}

void WriteImpact(const Impact &imp)
{
	gi.WriteByte(imp.kind);
	switch (imp.kind) {
	case 0:
		gi.WritePosition(imp.point);
		WriteScaled(imp.normal, 1000);
		break;
	case 1:
		gi.WritePosition(imp.point);
		break;
	case 2:
		gi.WritePosition(imp.point);
		WriteScaled(imp.normal, 1000);
		gi.WriteShort(imp.entnum);
		gi.WriteByte(imp.material);
		break;
	default:
		gi.WritePosition(imp.point);
		WriteScaled(imp.normal, 1000);
		gi.WriteByte(imp.material);
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// temp entities

void Sparks(int te, const vec3_t p, const vec3_t dir)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(te);
	gi.WritePosition(p);
	WriteDirByte(dir);
	gi.multicast(p, MULTICAST_PHS);
}

void PointTE(int te, const vec3_t p, int multicast)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(te);
	gi.WritePosition(p);
	gi.multicast(p, multicast);
}

void Arc(const vec3_t p, int length, const vec3_t dir)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_ARC);
	gi.WritePosition(p);
	gi.WriteByte(length & 0xFF);
	WriteDirByte(dir);
	gi.multicast(p, MULTICAST_PHS);
}

void GroundFlash(const vec3_t p, edict_t *ignore)
{
	static const vec3_t up = {0, 0, 1};
	Impact imp;
	ImpactAlong(p, up, ignore, imp);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_GROUNDFLASH);
	WriteImpact(imp);
	gi.multicast(p, MULTICAST_PHS);
}

void FloorDecal(int kind, const vec3_t p, int lifetime)
{
	static const int te[5] = {-1, TE_SCORCH, TE_DECAL17, TE_PIPELEFT, TE_PUDDLE};
	if (kind == 0) {
		BloodPool(p);
		return;
	}
	if (kind < 0 || kind > 4)
		return; // kinds >= 5 write no type byte in the original (malformed); never used
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(te[kind]);
	gi.WritePosition(p);
	gi.WriteLong(lifetime ? lifetime : 10000);
	gi.multicast(p, MULTICAST_PHS);
}

void Lightning(void)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_LIGHTNING);
	gi.multicast(vec3_origin, MULTICAST_ALL_R);
}

bool Debris(const vec3_t p, const vec3_t dir, int count, int size, int chunk, int ex, int ey, int ez, int surface)
{
	if (chunk == 32 && BloodLocked())
		return false;
	int sb = surface & 0xFF;
	if (sb == s_debris_surface && level.time <= s_debris_time + 0.1f)
		return false;
	s_debris_surface = sb;
	s_debris_time = level.time;
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_DEBRIS);
	gi.WritePosition(p);
	WriteDirByte(dir);
	gi.WriteByte(count & 0xFF);
	gi.WriteByte(size & 0xFF);
	gi.WriteByte(chunk & 0xFF);
	gi.WriteByte(ex & 0xFF);
	gi.WriteByte(ey & 0xFF);
	gi.WriteByte(ez & 0xFF);
	gi.WriteByte(sb);
	gi.multicast(p, MULTICAST_PHS);
	return true;
}

void ExplosionDebris(edict_t *ent)
{
	vec3_t f, P;
	AngleVectors(ent->s.angles, f, NULL, NULL);
	VectorMA(ent->s.origin, 2, f, P);
	Impact imp;
	ImpactAlong(P, f, ent, imp);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION_DEBRIS);
	WriteImpact(imp);
	gi.multicast(ent->s.origin, MULTICAST_PHS);
}

void SmokeBurst(const vec3_t p, int speed)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_SMOKEBURST);
	gi.WritePosition(p);
	gi.WriteByte(speed & 0xFF);
	gi.multicast(p, MULTICAST_PHS);
}

void Puff(const vec3_t p, int r, int g, int b, int a)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_PUFF);
	gi.WritePosition(p);
	gi.WriteByte(r & 0xFF);
	gi.WriteByte(g & 0xFF);
	gi.WriteByte(b & 0xFF);
	gi.WriteByte(a & 0xFF);
	gi.multicast(p, MULTICAST_PHS);
}

void Explosion(int te, const vec3_t p, int sizebyte, int soundbyte)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(te);
	gi.WritePosition(p);
	gi.WriteByte(sizebyte & 0xFF);
	gi.WriteByte(soundbyte & 0xFF);
	gi.multicast(p, MULTICAST_PHS);
}

void LargeExplosion(const vec3_t p, int magnitude)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_LARGE_EXPLOSION_SND);
	gi.WritePosition(p);
	gi.WriteByte(magnitude & 0xFF);
	gi.multicast(p, MULTICAST_PHS);
}

void Paper(const vec3_t p, int count)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_PAPER);
	gi.WritePosition(p);
	gi.WriteShort(count & 0xFF);
	gi.multicast(p, MULTICAST_PHS);
}

void SodaCans(const vec3_t p, const vec3_t dir, int speed, int count)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_SODACANS);
	gi.WritePosition(p);
	gi.WritePosition(dir);
	gi.WriteShort(speed);
	gi.WriteShort(count);
	gi.multicast(p, MULTICAST_PHS);
}

void Dust(const vec3_t p, int count)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_DUST);
	gi.WritePosition(p);
	gi.WriteShort(count);
	gi.multicast(p, MULTICAST_PVS);
}

bool BloodSplat(const vec3_t P, const vec3_t dir, edict_t *ignore, float reach)
{
	if (BloodLocked())
		return false;
	vec3_t end;
	VectorMA(P, reach, dir, end);
	trace_t tr = gi.trace(P, vec3_origin, vec3_origin, end, ignore, 3);
	if (tr.fraction > 0.99f)
		return false;
	if (tr.ent && tr.ent != g_edicts && tr.ent->solid != SOLID_BSP)
		return false;
	Impact imp;
	ImpactFromTrace(tr, imp);
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BLOODSPLAT);
	WriteImpact(imp);
	gi.multicast(P, MULTICAST_PHS);
	return true;
}

bool WeaponImpact(const trace_t &tr, const vec3_t shotdir, int power, int style, int debris, bool stagechanged)
{
	if (tr.surface && (tr.surface->flags & 4)) // sky
		return false;
	if (IsCreature(tr.ent) && BloodLocked())
		return false;
	Impact imp;
	ImpactFromTrace(tr, imp);
	switch (imp.material) {
	case 32: case 37: case 38: case 39: case 40: case 41:
		if (level.time < s_impact_time)
			return false;
		if (level.time > s_impact_time)
			s_impact_time = level.time + 0.2f;
		break;
	}
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(stagechanged ? TE_IMPACT_STAGE : TE_IMPACT);
	WriteImpact(imp);
	WriteScaled(shotdir, 1000);
	gi.WriteByte(power & 0xFF);
	gi.WriteByte(style & 0xFF);
	gi.WriteByte(debris & 0xFF);
	gi.multicast(tr.endpos, MULTICAST_PHS);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// named effects

int Index(const char *name) { return name && name[0] ? gi.effectindex(name) : 0; }

void ExtScale(float scale)
{
	s_ext_scale = scale;
	s_ext_flags |= 0x01;
}
void ExtPos2(const vec3_t p)
{
	VectorCopy(p, s_ext_pos2);
	s_ext_flags |= 0x04;
}
void ExtDir(const vec3_t d)
{
	VectorCopy(d, s_ext_dir);
	s_ext_flags |= 0x08;
}

static void WriteExtension(void)
{
	gi.WriteByte(s_ext_flags);
	if (s_ext_flags & 0x01)
		gi.WriteShort((int)(s_ext_scale * 128));
	if (s_ext_flags & 0x04)
		gi.WritePosition(s_ext_pos2);
	if (s_ext_flags & 0x08)
		WriteScaled(s_ext_dir, 2048);
	s_ext_flags = 0;
}

static void SendEffect(const vec3_t org, int multicast, edict_t *exclude)
{
	if (exclude)
		gix.multicastexclude(org, multicast, G_EdictNum(exclude));
	else
		gi.multicast(org, multicast);
}

void EffectAt(int effect, const vec3_t pos, int multicast, edict_t *exclude)
{
	if (effect <= 0)
		return; // pending extension values stay for the next effect (original quirk)
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect & 0xFF);
	gi.WriteByte(0x01 | (s_ext_flags ? 0x80 : 0));
	gi.WritePosition(pos);
	if (s_ext_flags)
		WriteExtension();
	SendEffect(pos, multicast, exclude);
}

void EffectAtName(const char *name, const vec3_t pos, int multicast) { EffectAt(Index(name), pos, multicast); }

void EffectEnt(int effect, edict_t *ent, int multicast, edict_t *exclude)
{
	if (effect <= 0 || !ent)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect & 0xFF);
	gi.WriteByte(0x02 | (s_ext_flags ? 0x80 : 0));
	gi.WriteShort(G_EdictNum(ent));
	if (s_ext_flags)
		WriteExtension();
	SendEffect(ent->s.origin, multicast, exclude);
}

void EffectBolt(int effect, edict_t *ent, int bolt, bool reversed, int multicast, edict_t *exclude)
{
	if (effect <= 0 || !ent)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect & 0xFF);
	gi.WriteByte(0x04 | (reversed ? 0x40 : 0) | (s_ext_flags ? 0x80 : 0));
	gi.WriteShort(G_EdictNum(ent));
	gi.WriteShort(bolt);
	if (s_ext_flags)
		WriteExtension();
	SendEffect(ent->s.origin, multicast, exclude);
}

void EffectInstBolt(int effect, edict_t *ent, int inst, int bolt, bool reversed, int multicast, edict_t *exclude)
{
	if (effect <= 0 || !ent)
		return;
	gi.WriteByte(svc_effect);
	gi.WriteByte(effect & 0xFF);
	gi.WriteByte(0x04 | 0x08 | (reversed ? 0x40 : 0) | (s_ext_flags ? 0x80 : 0));
	gi.WriteShort(G_EdictNum(ent));
	gi.WriteShort(inst);
	gi.WriteShort(bolt);
	if (s_ext_flags)
		WriteExtension();
	SendEffect(ent->s.origin, multicast, exclude);
}

void BloodPool(const vec3_t p)
{
	if (BloodLocked())
		return;
	EffectAt(Index("gore/bloodpool"), p, MULTICAST_PHS);
}

// ---------------------------------------------------------------------------------------------------------------
// entity-state effects

void AddEvent(edict_t *ent, int id, int parm)
{
	entity_state_t &s = ent->s;
	int slot;
	if (!(s.event[0] & 0x7F))
		slot = 0;
	else if (!(s.event[1] & 0x7F)) {
		s.event[0] |= 0x80;
		slot = 1;
	} else {
		s.event[1] |= 0x80;
		slot = 2;
	}
	s.event[slot] = id & 0x7F;
	s.eventparm[slot] = parm;
}

void ClearEvents(edict_t *ent) { ent->s.event[0] = 0; }

bool SlotAdd(edict_t *ent, int effect, int bolt, float scale)
{
	if (!ent || effect <= 0)
		return false;
	for (int i = 0; i < 4; i++) {
		if (ent->s.fx[i][0])
			continue;
		ent->s.fx[i][0] = (short)effect;
		ent->s.fx[i][1] = (short)bolt;
		ent->s.fx[i][2] = (short)(int)(scale * 16);
		return true;
	}
	return false;
}

bool SlotAddName(edict_t *ent, const char *name, int bolt, float scale)
{
	return SlotAdd(ent, Index(name), bolt, scale);
}

static int FindSlot(edict_t *ent, int effect, int bolt)
{
	if (!ent || effect <= 0)
		return -1;
	for (int i = 0; i < 4; i++)
		if (ent->s.fx[i][0] == effect && (!bolt || ent->s.fx[i][1] == bolt))
			return i;
	return -1;
}

void SlotRemove(edict_t *ent, int effect, int bolt)
{
	int i = FindSlot(ent, effect, bolt);
	if (i >= 0)
		ent->s.fx[i][0] = ent->s.fx[i][1] = ent->s.fx[i][2] = 0;
}

void SlotRescale(edict_t *ent, int effect, int bolt, float scale)
{
	int i = FindSlot(ent, effect, bolt);
	if (i >= 0)
		ent->s.fx[i][2] = (short)(int)(scale * 16);
}

bool SlotHas(edict_t *ent, int effect, int bolt) { return FindSlot(ent, effect, bolt) >= 0; }

short RotationValue(edict_t *ent, int i) { return ent->s.fx[i / 3][i % 3]; }
void RotationSetValue(edict_t *ent, int i, int value) { ent->s.fx[i / 3][i % 3] = (short)value; }

void RotationSet(edict_t *ent, const float rate[3], const int inst[4])
{
	memset(ent->s.fx, 0, sizeof(ent->s.fx));
	ent->s.effects |= 0x0001;
	RotationSetValue(ent, 0, 999);
	for (int i = 0; i < 3; i++)
		RotationSetValue(ent, 1 + i, (int)(512.0f * rate[i]));
	for (int i = 0; i < 4; i++)
		RotationSetValue(ent, 4 + i, inst ? inst[i] : 0);
}

void RotationClear(edict_t *ent)
{
	ent->s.effects &= ~0x0001;
	memset(ent->s.fx, 0, sizeof(ent->s.fx));
}

bool RotationActive(edict_t *ent) { return (ent->s.effects & 0x0001) != 0; }

void FadeOut(edict_t *ent, float seconds)
{
	memset(ent->s.fx, 0, sizeof(ent->s.fx));
	ent->s.effects &= ~(0x0001 | 0x8000);
	if (ent->ghoulInst)
		ent->ghoulInst->SetTint(1, 1, 1, 1);
	AddEvent(ent, 22);
	ent->s.effects &= ~0x0800;
	ent->s.effects |= 0x8000;
	int start = (int)(level.time * 1000);
	RotationSetValue(ent, 0, start & 0xFFFF);
	RotationSetValue(ent, 1, (start >> 16) & 0xFFFF);
	RotationSetValue(ent, 2, (int)(seconds * 1000));
}

void FadeCancel(edict_t *ent)
{
	memset(ent->s.fx, 0, sizeof(ent->s.fx));
	ent->s.effects &= ~(0x0001 | 0x8000);
	if (ent->ghoulInst)
		ent->ghoulInst->SetTintAll(1, 1, 1, 1);
	AddEvent(ent, 22);
}

bool FadeCheck(edict_t *ent)
{
	if (!(ent->s.effects & 0x8000))
		return false;
	int start = (unsigned short)RotationValue(ent, 0) | ((int)(unsigned short)RotationValue(ent, 1) << 16);
	int d = RotationValue(ent, 2);
	if (d <= 0) {
		FadeCancel(ent);
		return false;
	}
	if (level.time >= (start + d) / 1000.0f) {
		ent->s.effects &= ~0x8000;
		memset(ent->s.fx, 0, sizeof(ent->s.fx));
		if (ent->ghoulInst)
			ent->ghoulInst->SetTintAll(1, 1, 1, 0);
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------------------------

void ShakeTick(const vec3_t origin, float maxintensity, float radius, int parm)
{
	if (radius <= 0)
		return;
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = g_edicts + i;
		if (!p->inuse || !p->client)
			continue;
		vec3_t c, d;
		for (int k = 0; k < 3; k++)
			c[k] = p->s.origin[k] + (p->mins[k] + p->maxs[k]) * 0.5f;
		VectorSubtract(c, origin, d);
		float dist = VectorLength(d);
		if (dist > radius)
			continue;
		float in = (1 - dist / radius) * maxintensity;
		int lvl = in > 80 ? 4 : in > 55 ? 3 : in > 35 ? 2 : in > 17 ? 1 : 0;
		AddEvent(p, 16 + lvl, parm);
	}
}

void FireAttack(edict_t *owner, int attack, const vec3_t start, const vec3_t dir, float extra, bool nodamage)
{
	if (g_fire_attack)
		g_fire_attack(owner, attack, start, dir, extra, nodamage);
	else if (g3hooks.fireAttack && !nodamage)
		g3hooks.fireAttack(owner, attack, start, dir, NULL);
}

void AINoise(edict_t *source, const vec3_t where, float loudness, float radius)
{
	if (g_ai_noise)
		g_ai_noise(source, where, loudness, radius);
}

} // namespace fx
