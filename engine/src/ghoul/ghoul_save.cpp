// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - instance savegame images (instance methods 37 and 38).
// Implements: SPEC-013 section 6 (field-by-field bit layout, length, loading checks and fatal errors),
//             SPEC-011 "Savegames"

#include <algorithm>
#include <cstring>

#include "ghoul_internal.h"

namespace ghoul {

static int CallbackIndex(IGhoulCallback *cb, IGhoulCallback **table, int count)
{
	if (table)
		for (int i = 0; i < count; i++)
			if (table[i] == cb)
				return i;
	return -1;
}

static void WriteMatrixRaw(Bits &b, const Matrix4 &m)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			b.WriteFloat(m.m[r][c]);
	b.Write(m.flags, 32);
}

static void ReadMatrixRaw(Bits &b, Matrix4 &m)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			m.m[r][c] = b.ReadFloat();
	m.flags = b.Read(32);
}

int Inst::SaveState(void *buffer, int capacity, IGhoulCallback **table, int count, const char *modelName)
{
	// write into a scratch buffer, then check the capacity (the original checks after writing)
	std::vector<unsigned char> scratch(65536 + obj->parts.size() + 16 * (skinOverrides.size() + noteCbs.size() +
		matrixCbs.size()));
	om_ghoul_bits_t raw = {scratch.data(), (int)scratch.size() * 8, 0, 0};
	Bits b(&raw);
	b.WriteString(modelName ? modelName : "");
	b.Write((uint32_t)id & 0xFFFFu, 16);
	WriteMatrixRaw(b, xf);
	WriteMatrixRaw(b, inv);
	b.Write((uint32_t)(int32_t)detSign, 32);
	b.WriteFloat(scale);
	b.Write((uint32_t)attachBolt & 0xFFFFu, 16);
	b.Write(on ? 1 : 0, 8);
	b.Write((uint32_t)speed, 32);
	b.WriteFloat(S);
	b.WriteFloat(A);
	b.WriteFloat(pauseEnd);
	b.Write(paused ? 1 : 0, 8);
	b.Write(reverse ? 1 : 0, 8);
	for (int i = 0; i < 4; i++)
		b.Write(tint[i], 8);
	b.Write((uint32_t)seq & 0xFFFFu, 16);
	b.Write((uint32_t)mode, 32);
	uint32_t P = (uint32_t)obj->parts.size();
	b.Write(P, 32);
	for (uint32_t i = 0; i < P; i++)
		b.Write(i < partFlags.size() ? partFlags[i] : 1, 8);
	b.Write((uint32_t)skinOverrides.size(), 32);
	for (auto &s : skinOverrides) // std::map: ascending (material, part)
	{
		b.Write((uint32_t)s.first.first & 0xFFFFu, 16);
		b.Write(0, 16);
		b.Write((uint32_t)s.first.second & 0xFFFFu, 16);
		b.Write(0, 16);
		b.Write(0, 32);
		b.Write((uint32_t)s.second & 0xFFFFu, 16);
		b.Write(0, 16);
	}
	b.WriteFloat(cursorTime);
	b.WriteFloat(cursorPhase);
	b.Write(noteOn ? 1 : 0, 8);
	b.Write(matrixOn ? 1 : 0, 8);
	b.Write(obj->hasLights ? 1 : 0, 8);
	std::vector<NoteCb> nc(noteCbs);
	std::stable_sort(nc.begin(), nc.end(), [](const NoteCb &x, const NoteCb &y) { return x.note < y.note; });
	b.Write((uint32_t)nc.size(), 32);
	for (auto &n : nc)
	{
		b.Write((uint32_t)CallbackIndex(n.cb, table, count), 32);
		b.Write((uint32_t)n.note & 0xFFFFu, 16);
	}
	b.Write((uint32_t)matrixCbs.size(), 32);
	for (auto &m : matrixCbs)
	{
		b.Write((uint32_t)CallbackIndex(m.cb, table, count), 32);
		b.Write((uint32_t)m.bolt & 0xFFFFu, 16);
		b.Write(0, 16);
		b.Write((uint32_t)m.mode, 32);
	}
	int bytes = (raw.bit + 7) / 8;
	if (bytes > capacity || raw.overflowed)
		Fatal("GhoulInst savegame packet overflow");
	if (buffer)
		std::memcpy(buffer, scratch.data(), (size_t)bytes);
	return bytes;
}

GhoulID Inst::LoadState(const void *buffer, int length, IGhoulCallback **table, int count, const char *modelName)
{
	if (!buffer || length <= 0)
		return 0;
	om_ghoul_bits_t raw = {(unsigned char *)buffer, length * 8, 0, 0};
	Bits b(&raw);
	std::string name = b.ReadString();
	if (name != (modelName ? modelName : "")) // case-sensitive
		Fatal("Inst has different model than expected");
	int savedId = (int)b.Read(16);
	ReadMatrixRaw(b, xf);
	ReadMatrixRaw(b, inv);
	detSign = (int32_t)b.Read(32);
	scale = b.ReadFloat();
	attachBolt = (int)b.Read(16);
	on = b.Read(8) != 0;
	speed = (int32_t)b.Read(32);
	S = b.ReadFloat();
	A = b.ReadFloat();
	pauseEnd = b.ReadFloat();
	paused = b.Read(8) != 0;
	reverse = b.Read(8) != 0;
	for (int i = 0; i < 4; i++)
		tint[i] = (uint8_t)b.Read(8);
	seq = (int)b.Read(16);
	mode = (int32_t)b.Read(32);
	uint32_t P = b.Read(32);
	if (P != obj->parts.size())
		Fatal("Number of parts mismatch in savegame");
	partFlags.assign(P, 1);
	for (uint32_t i = 0; i < P; i++)
		partFlags[i] = (uint8_t)b.Read(8);
	uint32_t K = b.Read(32);
	skinOverrides.clear();
	for (uint32_t i = 0; i < K; i++)
	{
		int mat = (int)b.Read(16);
		b.Read(16);
		int part = (int)b.Read(16);
		b.Read(16);
		b.Read(32);
		int skin = (int)b.Read(16);
		b.Read(16);
		if (mat < 1 || mat > (int)obj->mats.size() || skin < 1 || skin > (int)obj->skins.size() ||
			obj->skins[skin - 1].material != mat)
			Fatal("Skin mismatch in savegame");
		if (part > (int)P)
			Fatal("Part mismatch in savegame");
		skinOverrides[std::make_pair(mat, part)] = skin;
	}
	cursorTime = b.ReadFloat();
	cursorPhase = b.ReadFloat();
	noteOn = b.Read(8) != 0;
	matrixOn = b.Read(8) != 0;
	hasModelLightsFlag = b.Read(8) != 0;
	uint32_t C = b.Read(32);
	noteCbs.clear();
	for (uint32_t i = 0; i < C && !raw.overflowed; i++)
	{
		int32_t idx = (int32_t)b.Read(32);
		int note = (int)b.Read(16);
		if (idx >= 0 && idx < count && table)
			noteCbs.push_back(NoteCb{table[idx], note});
	}
	uint32_t M = b.Read(32);
	matrixCbs.clear();
	for (uint32_t i = 0; i < M && !raw.overflowed; i++)
	{
		int32_t idx = (int32_t)b.Read(32);
		int bolt = (int)b.Read(16);
		b.Read(16);
		int mm = (int32_t)b.Read(32);
		if (idx >= 0 && idx < count && table)
			matrixCbs.push_back(MatrixCb{table[idx], bolt, mm});
	}
	rc.Reset();
	RecomputeTiming();
	return (GhoulID)savedId;
}

} // namespace ghoul
