// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program - writer for small synthetic .ghb files with known contents.
// Implements: SPEC-120 / SPEC-014 (container layout, used to build test inputs)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct SynNode
{
	std::string name;
	std::string user;
	int kind = 0; // 0 null, 1 mesh
	// per-frame translation (empty = constant identity); rotation about Z by 'yawPerFrame' degrees
	std::vector<float> tx, ty, tz;
	float yawPerFrame = 0;
	bool noblock = false;
	bool hiddenFromFrame = false;
	uint32_t hideFrom = 0;
};

struct SynEvent
{
	float ms;
	std::string type, arg;
};

struct SynSeq
{
	std::string name;
	float frameMs;
	uint32_t first, count;
	std::vector<SynEvent> events;
};

struct SynModel
{
	std::vector<SynSeq> seqs;
	std::vector<SynNode> nodes; // nulls and meshes (meshes get one quad each)
	float quadX = 0;            // meshes: a 2x2 quad in the YZ plane at x = quadX + mesh index
	std::string material = "MAT1";
	// LIGHT records with constant tracks (SPEC-AMEND-005 I3.10): kind; brightness 0.5 + 0.1 * index, the extra
	// tracks of kinds 1-4 filled with 9s
	std::vector<uint32_t> lightKinds;
};

class SynWriter
{
public:
	std::vector<uint8_t> a, b;
	void U8(uint8_t v) { a.push_back(v); }
	void U32(uint32_t v)
	{
		for (int i = 0; i < 4; i++)
			a.push_back((uint8_t)(v >> (8 * i)));
	}
	void F(float f)
	{
		uint32_t u;
		std::memcpy(&u, &f, 4);
		U32(u);
	}
	void S(const std::string &s)
	{
		U32((uint32_t)s.size());
		a.insert(a.end(), s.begin(), s.end());
	}
	void Bounds(float lo, float hi)
	{
		U8(0);
		U8(0);
		for (int i = 0; i < 3; i++)
			F(lo);
		for (int i = 0; i < 3; i++)
			F(hi);
	}
	void Mat(const float m[16], uint32_t id)
	{
		for (int i = 0; i < 16; i++)
			F(m[i]);
		U32(id);
	}
	void BU32(uint32_t v)
	{
		for (int i = 0; i < 4; i++)
			b.push_back((uint8_t)(v >> (8 * i)));
	}
	void BF(float f)
	{
		uint32_t u;
		std::memcpy(&u, &f, 4);
		BU32(u);
	}
	void BI16(int16_t v)
	{
		b.push_back((uint8_t)v);
		b.push_back((uint8_t)((uint16_t)v >> 8));
	}
};

inline std::vector<uint8_t> BuildSynthetic(const SynModel &m)
{
	SynWriter w;
	uint32_t frames = 0;
	for (auto &s : m.seqs)
		frames = std::max(frames, s.first + s.count);
	w.U32(0x198237FE);
	w.U32(0); // size a (patched)
	w.U32(0); // size b (patched)
	w.U32(0x033FB1A3);
	w.U32(74);
	w.U32(0);
	w.S("synthetic.ghl");
	w.U32(frames);
	w.U32((uint32_t)m.seqs.size());
	for (auto &s : m.seqs)
	{
		w.F(0);
		w.F(0);
		w.F(0);
		w.F(s.frameMs);
		w.S(s.name);
		w.U32((uint32_t)s.events.size());
		for (auto &e : s.events)
		{
			w.F(e.ms);
			w.U32(0xFFFFFFFFu);
			w.S(e.type);
			w.S(e.arg);
		}
		w.U32(s.first);
		w.U32(s.count);
	}
	// one untextured material
	w.U32(1);
	w.S(m.material);
	w.U8(0);
	w.U8(0);
	w.U32(0);
	w.U32(2);
	float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	for (int sl = 0; sl < 2; sl++)
	{
		w.S("");
		w.S("");
		w.U8(0);
		w.U8(0);
		w.U8(0);
		w.U8(0);
		w.U8(1);
		w.U8(0); // image track
		w.U8(1);
		w.Mat(ident, 1); // uv track
		w.U8(1);
	}
	w.U8(1);
	w.F(0.8f);
	w.F(0.3f);
	w.F(0.2f); // diffuse
	w.U8(1);
	w.F(1);
	w.F(1);
	w.F(1);
	float props[4] = {0, 0, 0, 1};
	for (float p : props)
	{
		w.U8(1);
		w.F(p);
	}
	int nmesh = 0;
	for (auto &n : m.nodes)
		nmesh += n.kind == 1;
	float lo = -2.0f, hi = m.quadX + nmesh + 2.0f;
	w.Bounds(lo, hi);
	auto node = [&](const SynNode &n) {
		w.S(n.name);
		w.S(n.user);
		if (!n.hiddenFromFrame)
		{
			w.U8(1);
			w.U8(1);
		}
		else
		{
			w.U8(0);
			w.U32(frames);
			std::vector<uint32_t> words((frames + 31) / 32, 0);
			for (uint32_t f = 0; f < frames; f++)
				if (f < n.hideFrom)
					words[f >> 5] |= 1u << (f & 31);
			for (auto x : words)
				w.U32(x);
		}
		w.U8(0);
		w.U8(0);
		w.U8(n.noblock ? 1 : 0);
		w.Bounds(-100, 100);
		w.U32(0);
		if (n.tx.empty() && n.ty.empty() && n.tz.empty() && n.yawPerFrame == 0)
		{
			w.U8(1);
			w.Mat(ident, 1);
		}
		else
		{
			w.U8(0);
			w.U32(frames);
			for (uint32_t f = 0; f < frames; f++)
			{
				float yaw = n.yawPerFrame * f * 3.14159265f / 180.0f;
				float c = std::cos(yaw), s = std::sin(yaw);
				float mm[16] = {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, n.tx.empty() ? 0 : n.tx[f % n.tx.size()],
					n.ty.empty() ? 0 : n.ty[f % n.ty.size()], n.tz.empty() ? 0 : n.tz[f % n.tz.size()], 1};
				w.Mat(mm, 0);
			}
		}
	};
	w.U32(0x05DC159C);
	int nnull = 0;
	for (auto &n : m.nodes)
		nnull += n.kind == 0;
	w.U32((uint32_t)nnull);
	for (auto &n : m.nodes)
		if (n.kind == 0)
			node(n);
	w.U32(0x05DC159D);
	w.U32(0);
	w.U32(0x05DC159E);
	w.U32((uint32_t)nmesh);
	for (auto &n : m.nodes)
		if (n.kind == 1)
		{
			node(n);
			w.Bounds(lo, hi);
		}
	if (nmesh)
	{
		// static block: groups, positions, uvs, normals, surfaces, index lists, corners
		uint32_t nPos = 4 * nmesh, nUv = 4, nNorm = 1, nSurf = nmesh, nIdx = 8 * nmesh, nCorner = 4 * nmesh;
		std::vector<uint32_t> off(10);
		uint32_t o = 0;
		off[0] = o;
		o += 12;
		off[1] = o;
		o += nPos * 12;
		off[2] = o;
		o += nUv * 8;
		off[3] = o;
		o += nNorm * 12;
		off[4] = o;
		o += nSurf * 6;
		off[5] = o;
		o += nIdx * 2;
		off[6] = o;
		o += nCorner * 6;
		off[7] = off[8] = off[9] = o;
		uint32_t staticSize = o;
		w.U32(0xFFFFFFFFu);
		w.U32(1);
		w.U32((uint32_t)nmesh);
		w.U32(1);
		w.U32(nPos);
		w.U32(nUv);
		w.U32(nNorm);
		w.U32(nSurf);
		w.U32(nIdx);
		w.U32(nCorner);
		w.U32(0);
		w.U32(0);
		w.U32(0);
		for (auto x : off)
			w.U32(x);
		w.Bounds(lo, hi);
		for (int i = 0; i < 3; i++)
			w.F(100.8f);
		for (int i = 0; i < 3; i++)
			w.F(1 / 100.8f);
		for (int i = 0; i < 3; i++)
			w.F(0);
		for (int i = 0; i < 3; i++)
			w.F(1 / 256.0f);
		w.U8(0); // not chunked
		w.U32(staticSize);
		w.U32(0);
		// part B
		w.BU32(0);
		w.BU32(0);
		w.BU32((uint32_t)nmesh);
		for (int k = 0; k < nmesh; k++)
		{
			float x = m.quadX + k;
			float q[4][3] = {{x, -1, -1}, {x, 1, -1}, {x, 1, 1}, {x, -1, 1}};
			for (auto &v : q)
				for (float c : v)
					w.BF(c);
		}
		float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
		for (auto &u : uv)
		{
			w.BF(u[0]);
			w.BF(u[1]);
		}
		w.BF(1);
		w.BF(0);
		w.BF(0);
		for (int k = 0; k < nmesh; k++)
		{
			w.BI16((int16_t)k);
			w.BI16((int16_t)(k * 8));
			w.BI16(8);
		}
		for (int k = 0; k < nmesh; k++)
		{
			int16_t c0 = (int16_t)(k * 4);
			int16_t list[8] = {0, 6, c0, (int16_t)(c0 + 1), (int16_t)(c0 + 2), c0, (int16_t)(c0 + 2), (int16_t)(c0 + 3)};
			for (auto x : list)
				w.BI16(x);
		}
		for (int k = 0; k < nmesh; k++)
			for (int i = 0; i < 4; i++)
			{
				w.BI16((int16_t)~(k * 4 + i));
				w.BI16((int16_t)~0);
				w.BI16((int16_t)~i);
			}
	}
	w.U32(0x05DC159F);
	w.U32((uint32_t)m.lightKinds.size());
	for (size_t li = 0; li < m.lightKinds.size(); li++)
	{
		uint32_t kind = m.lightKinds[li];
		SynNode ln;
		ln.name = "LIGHT_" + std::to_string(li);
		node(ln);
		w.U8(0); // not a world light
		w.U32(kind);
		auto v3 = [&](float x, float y, float z) {
			w.U8(1);
			w.F(x);
			w.F(y);
			w.F(z);
		};
		auto f1 = [&](float x) {
			w.U8(1);
			w.F(x);
		};
		v3(1, 2, 3);                 // position
		v3(1, 1, 1);                 // colour
		f1(0.5f + 0.1f * (float)li); // brightness
		if (kind <= 4)
		{
			f1(1.0f);   // multiplier
			f1(10.0f);  // attenuation start
			f1(100.0f); // attenuation end
			if (kind >= 1)
			{
				v3(9, 9, 9);
				f1(9);
				f1(9);
				v3(9, 9, 9);
				v3(9, 9, 9);
				if (kind == 2 || kind == 4)
					f1(9);
			}
		}
	}
	uint32_t sa = (uint32_t)w.a.size(), sb = (uint32_t)w.b.size();
	std::memcpy(&w.a[4], &sa, 4);
	std::memcpy(&w.a[8], &sb, 4);
	std::vector<uint8_t> out = w.a;
	out.insert(out.end(), w.b.begin(), w.b.end());
	return out;
}
