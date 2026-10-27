// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul test program: spec test vectors, every stock Ghoul file, runtime behaviour, savegame and network
// round trips, pose sampling with OBJ/glTF export and software-rendered images.
// Implements: SPEC-010..014, SPEC-120, SPEC-121 (verification)
//
// usage: omghoul_test [data root] [output dir] [--quick] [--replay <captured ghoul payloads>]
//   data root  = directory holding pak0/, pak2/, pak3/ extracted with tools/pak.py
//                (default C:\openmerc-data\ghoul); tests needing data skip when it is absent
//   output dir = where renders and exports go (default <data root>/../renders/ghoul-i3)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "../ghb_file.h"
#include "../ghoul_internal.h"
#include "../ghoul_tables.h"
#include "test_common.h"

using namespace ghoul;

void RunRuntimeTests();
void RunSaveNetTests();
void RunRenderTests(const std::string &outDir, bool quick);
void RunReplay(const std::string &path);
std::string g_outDir;

// ------------------------------------------------------------------ SHA-256 (direction table hash)

static std::string Sha256(const uint8_t *data, size_t len)
{
	static const uint32_t k[64] = {0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
		0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152,
		0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138,
		0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70,
		0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa,
		0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
	uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	std::vector<uint8_t> m(data, data + len);
	uint64_t bits = (uint64_t)len * 8;
	m.push_back(0x80);
	while (m.size() % 64 != 56)
		m.push_back(0);
	for (int i = 7; i >= 0; i--)
		m.push_back((uint8_t)(bits >> (8 * i)));
	auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
	for (size_t off = 0; off < m.size(); off += 64)
	{
		uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = (uint32_t)m[off + i * 4] << 24 | (uint32_t)m[off + i * 4 + 1] << 16 | (uint32_t)m[off + i * 4 + 2] << 8 |
				m[off + i * 4 + 3];
		for (int i = 16; i < 64; i++)
		{
			uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++)
		{
			uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			uint32_t ch = (e & f) ^ (~e & g);
			uint32_t t1 = hh + S1 + ch + k[i] + w[i];
			uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
			uint32_t t2 = S0 + mj;
			hh = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
	}
	char out[65];
	for (int i = 0; i < 8; i++)
		std::snprintf(out + i * 8, 9, "%08x", h[i]);
	return out;
}

// ------------------------------------------------------------------ SPEC-014 codec vectors

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static void TestCodecs()
{
	std::printf("== codecs (SPEC-014 test vectors 1, 3, 4, 8)\n");
	{
		// the table is generated (docs/impl/LEGAL-tables.md): unit vectors, axes first, all distinct
		const float *t = DirTable();
		bool unit = true, distinct = true;
		for (int i = 0; i < 256; i++)
		{
			float l = t[i * 3] * t[i * 3] + t[i * 3 + 1] * t[i * 3 + 1] + t[i * 3 + 2] * t[i * 3 + 2];
			unit = unit && std::fabs(l - 1.0f) < 1e-5f;
			for (int j = 0; j < i; j++)
			{
				float d = t[i * 3] * t[j * 3] + t[i * 3 + 1] * t[j * 3 + 1] + t[i * 3 + 2] * t[j * 3 + 2];
				distinct = distinct && d < 0.9999f;
			}
		}
		CHECK(unit && distinct && t[0] == 1.0f && t[14] == 1.0f && t[17] == -1.0f, "direction table (generated)");
		CHECK(std::fabs(t[6 * 3 + 2] - 0.975584f) < 2e-6f && std::fabs(t[(6 + 14) * 3] - 0.894427f) < 2e-6f,
			"direction table: geodesic layout");
	}
	struct RotVec
	{
		uint32_t w;
		float r[9];
	} rv[] = {
		{0x00000000, {0.974508f, 0.160676f, 0.156580f, -0.158641f, 0.987007f, -0.025490f, -0.158641f, 0.0f, 0.987336f}},
		{0x00088800, {0.999915f, -0.009247f, -0.009246f, 0.009246f, 0.999957f, -0.000085f, 0.009246f, 0.0f, 0.999957f}},
		{0x00188800, {0.999915f, -0.009275f, -0.009217f, 0.009246f, 0.999952f, -0.003155f, 0.009246f, 0.003069f, 0.999953f}},
		{0x80088800, {0.999915f, -0.009247f, 0.009246f, 0.009246f, 0.999957f, 0.000085f, 0.009246f, 0.0f, -0.999957f}},
		{0x00088804, {0.009246f, 0.999957f, -0.000085f, 0.009246f, 0.0f, 0.999957f, 0.999915f, -0.009247f, -0.009246f}},
		{0x00088802, {0.009246f, 0.999957f, 0.000085f, 0.999915f, -0.009247f, 0.009246f, 0.009246f, 0.0f, -0.999957f}},
		{0x40088800, {0.999915f, 0.009261f, 0.009232f, 0.009246f, -0.999956f, 0.001620f, 0.009246f, -0.001535f, -0.999956f}},
		{0x1ff7a9c3, {-0.168538f, -0.985485f, -0.020335f, -0.981041f, 0.169709f, -0.093583f, 0.095675f, 0.004177f, -0.995404f}},
	};
	for (auto &v : rv)
	{
		float m[3][3];
		DecodePackedRotation(v.w, m);
		float err = 0;
		for (int i = 0; i < 9; i++)
			err = std::max(err, std::fabs(m[i / 3][i % 3] - v.r[i]));
		CHECK(err < 2e-6f, "rotation %08x max error %g", v.w, err);
		// encoder reproduces the word from its own decode
		Matrix4 M;
		MatIdentity(M);
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				M.m[r][c] = m[r][c];
		M.flags = 0;
		uint32_t back = EncodePackedRotation(M);
		float m2[3][3];
		DecodePackedRotation(back, m2);
		float err2 = 0;
		for (int i = 0; i < 9; i++)
			err2 = std::max(err2, std::fabs(m2[i / 3][i % 3] - m[i / 3][i % 3]));
		CHECK(err2 < 0.01f, "re-encode %08x -> %08x error %g", v.w, back, err2);
	}
	// position stream (test vector 3)
	{
		uint8_t s[] = {0x54, 0x08, 0xE0, 0x22, 0x51, 0x10, 0xC0, 0x06, 0, 0, 0, 0};
		uint8_t bits[3] = {4, 3, 0};
		float scale[3] = {0.5f, 1, 1}, ofs[3] = {10, 20, 30};
		float out[3 * 3 * 3];
		uint32_t used = DecodePositionStream(s, sizeof(s), 3, 3, bits, scale, ofs, out);
		CHECK(used == 75, "position stream bits used %u", used);
		float want[3][3][3] = {{{12.5f, 22, 30}, {12, 24, 30}, {12, 24, 30}},
			{{13, 22, 30}, {13, 24, 30}, {13, 24, 30}}, {{12.5f, 22, 30}, {12.5f, 24, 30}, {12.5f, 24, 30}}};
		bool ok = true;
		for (int f = 0; f < 3; f++)
			for (int v = 0; v < 3; v++)
				for (int a = 0; a < 3; a++)
					if (!Near(out[(f * 3 + v) * 3 + a], want[f][v][a], 1e-5f))
						ok = false;
		CHECK(ok, "position stream values");
		uint8_t r[] = {0x7F, 0x00, 0, 0, 0, 0};
		float o2[6];
		DecodePositionStream(r, sizeof(r), 2, 1, bits, scale, ofs, o2);
		CHECK(Near(o2[0], 17.5f, 1e-5f) && Near(o2[1], 27, 1e-5f) && Near(o2[2], 30, 1e-5f) && Near(o2[3], 10, 1e-5f) &&
				Near(o2[4], 20, 1e-5f) && Near(o2[5], 30, 1e-5f),
			"raw chunk %g %g %g / %g %g %g", o2[0], o2[1], o2[2], o2[3], o2[4], o2[5]);
	}
	// normal reconstruction (test vector 4): keys +Z (4), +X (0), +Y (2)
	{
		uint8_t k5[] = {4, 0, 2};
		float o[8 * 3];
		DecodeNormalStream(k5, 3, 1, 5, o);
		float w5[5][3] = {{0, 0, 1}, {.5f, 0, .5f}, {1, 0, 0}, {1.5f, 0, -.5f}, {1, .5f, -.5f}};
		bool ok = true;
		for (int f = 0; f < 5; f++)
			for (int a = 0; a < 3; a++)
				ok = ok && Near(o[f * 3 + a], w5[f][a], 1e-5f);
		CHECK(ok, "normals n=5");
		DecodeNormalStream(k5, 3, 1, 8, o);
		float w8[4][3] = {{1, 0, 0}, {1.25f, 0, -.25f}, {.9167f, .3333f, -.25f}, {.5833f, .6667f, -.25f}};
		ok = true;
		for (int f = 4; f < 8; f++)
			for (int a = 0; a < 3; a++)
				ok = ok && Near(o[f * 3 + a], w8[f - 4][a], 1e-3f);
		CHECK(ok, "normals n=8");
		uint8_t k4[] = {4, 0};
		DecodeNormalStream(k4, 2, 1, 4, o);
		float w4[4][3] = {{0, 0, 1}, {.3333f, 0, .6667f}, {.6667f, 0, .3333f}, {1, 0, 0}};
		ok = true;
		for (int f = 0; f < 4; f++)
			for (int a = 0; a < 3; a++)
				ok = ok && Near(o[f * 3 + a], w4[f][a], 1e-3f);
		CHECK(ok, "normals n=4");
	}
}

// ------------------------------------------------------------------ bit streams (SPEC-201 B, SPEC-013 vectors 1-3)


static void TestBits()
{
	std::printf("== bit streams (SPEC-201 B.2/B.3, SPEC-013 test vectors 1-3)\n");
	uint8_t buf[4096];
	std::memset(buf, 0, sizeof(buf));
	om_ghoul_bits_t raw = {buf, (int)sizeof(buf) * 8, 0, 0};
	Bits b(&raw);
	uint32_t ids[] = {0, 1, 15, 16, 255, 256, 4095, 4096, 65535};
	for (uint32_t v : ids)
		b.WriteId(v);
	int32_t times[] = {0, 1, -1, 255, -256, 65535, -70000};
	for (int32_t v : times)
		b.WriteTime(v);
	const char *strs[] = {"", "BOS", "GHOUL/ENEMY/MESO/MESO_PLAYER.GHL", "weapons/gpistol/reloadout",
		"some literal name 123", "x", "WBOLT_HAND_R", "GHOUL/ENEMY/MESO/NOT_IN_DICTIONARY_SEQUENCE.GHL"};
	for (const char *s : strs)
		b.WriteString(s);
	int end = raw.bit;
	raw.bit = 0;
	bool ok = true;
	for (uint32_t v : ids)
		ok = ok && b.ReadId() == v;
	CHECK(ok, "id round trip");
	ok = true;
	for (int32_t v : times)
		ok = ok && b.ReadTime() == v;
	CHECK(ok, "time round trip");
	for (const char *s : strs)
	{
		std::string r = b.ReadString();
		CHECK(r == s, "string round trip '%s' -> '%s'", s, r.c_str());
	}
	CHECK(raw.bit == end && !raw.overflowed, "stream end %d/%d", raw.bit, end);

	// SPEC-013 test vector 1: 3 sequences (w0 2), 5 parts (w1 3); Play(seq 2, t 10, mode 1); frame time 10
	{
		NetState base;
		base.parts.assign(5, 1);
		NetState cur = base;
		cur.seq = 2;
		cur.mode = 1;
		cur.paused = false;
		cur.A = 10.0f;
		cur.S = 10.0f;
		uint8_t o[16] = {0};
		om_ghoul_bits_t w = {o, 128, 0, 0};
		NetWriteStateForTest(&w, cur, base, 10.0f, 2, 3, 0, 0);
		CHECK(w.bit == 19 && o[0] == 0xA1 && o[1] == 0x0B && o[2] == 0x00, "minimal state record: %d bits %02X %02X %02X",
			w.bit, o[0], o[1], o[2]);
		NetState rd = base;
		om_ghoul_bits_t r = {o, 19, 0, 0};
		NetReadStateForTest(&r, rd, 10.0f, 2, 3, 0, 0);
		CHECK(rd.seq == 2 && rd.mode == 1 && !rd.paused && rd.A == 10.0f && rd.S == 10.0f && r.bit == 19,
			"minimal state decode");
	}
	// test vector 2: A - frame time = -0.0125 -> t1 = -1; S - A = -0.02 -> t2 = -1
	{
		NetState base;
		base.parts.assign(1, 1);
		NetState cur = base;
		cur.paused = false;
		cur.A = 10.0f - 0.0126f; // -0.0125 exactly is a float rounding boundary; -0.0126 gives t1 = -1 as well
		cur.S = cur.A - 0.02f;
		uint8_t o[16] = {0};
		om_ghoul_bits_t w = {o, 128, 0, 0};
		NetWriteStateForTest(&w, cur, base, 10.0f, 0, 1, 0, 0);
		NetState rd = base;
		om_ghoul_bits_t r = {o, w.bit, 0, 0};
		NetReadStateForTest(&r, rd, 10.0f, 0, 1, 0, 0);
		CHECK(Near(rd.A, 10.0f - 1.0f / 120.0f, 1e-5f) && Near(rd.S, rd.A - 1.0f / 120.0f, 1e-5f),
			"time coding A %f S %f", rd.A, rd.S);
	}
	// test vector 3: short translation
	{
		float vals[] = {10.0f, 64.0f, 64.5f};
		float want[] = {10.0f, 63.9375f, 64.5f};
		for (int i = 0; i < 3; i++)
		{
			NetState base;
			base.parts.assign(1, 1);
			NetState cur = base;
			cur.xf.m[3][0] = vals[i];
			cur.xf.flags = 0;
			uint8_t o[64] = {0};
			om_ghoul_bits_t w = {o, 512, 0, 0};
			NetWriteStateForTest(&w, cur, base, 0.0f, 0, 1, 0, 0);
			NetState rd = base;
			om_ghoul_bits_t r = {o, w.bit, 0, 0};
			NetReadStateForTest(&r, rd, 0.0f, 0, 1, 0, 0);
			CHECK(rd.xf.m[3][0] == want[i], "translation %g -> %g", vals[i], rd.xf.m[3][0]);
		}
	}
}

// ------------------------------------------------------------------ every stock .ghb

static void TestAllFiles()
{
	std::vector<std::string> files = ListFiles("ghoul", ".ghb");
	std::printf("== %d .ghb files (SPEC-120 validation, SPEC-014 decoding)\n", (int)files.size());
	if (files.empty())
	{
		std::printf("SKIP: no extracted data under %s\n", g_host.root.c_str());
		return;
	}
	int ok = 0, chunks = 0, chunkBad = 0, tracks = 0, trackBad = 0, partA = 0;
	size_t bytes = 0;
	for (auto &p : files)
	{
		GhbFile f;
		g_host.log.clear();
		bool loaded = false;
		try
		{
			loaded = f.Load(p);
		}
		catch (const std::exception &e)
		{
			std::printf("FATAL %s: %s\n", p.c_str(), e.what());
		}
		CHECK(loaded, "load %s", p.c_str());
		if (!loaded)
			continue;
		ok++;
		bytes += f.MemoryUse();
		if (f.sizeA && f.sizeA + f.sizeB == f.MemoryUse())
			partA++;
		const GhbGeometry &g = f.geo;
		if (g.chunked)
			for (uint32_t ci = 0; ci < g.chunks.size(); ci++)
			{
				DecodedChunk d;
				DecodeChunk(f, ci, d);
				const GhbChunk &c = g.chunks[ci];
				uint32_t streamBytes = c.normOffset - c.posOffset;
				chunks++;
				if (streamBytes - (d.posBitsUsed + 7) / 8 != 4)
					chunkBad++;
				uint32_t want = g.nAnorm * NormalKeysPerChunk(c.count) + 4;
				if (c.normEnd - c.normOffset != want)
					chunkBad++;
			}
		for (auto &n : f.nodes)
		{
			if (!n.ncomp)
				continue;
			tracks++;
			bool good = true;
			for (uint32_t fr = 0; fr < n.ncomp && good; fr++)
			{
				Matrix4 m;
				f.NodeMatrix(n, fr, m);
				for (int a = 0; a < 3; a++)
				{
					float tol = std::fabs(n.qinv[a]) + 1e-3f;
					if (m.m[3][a] < n.bounds.mn[a] - tol || m.m[3][a] > n.bounds.mx[a] + tol)
						good = false;
				}
			}
			if (!good)
				trackBad++;
		}
	}
	std::printf("   loaded %d/%d (%.1f MB), size fields consistent %d, chunks %d (bad %d), compressed tracks %d (bad %d)\n", ok,
		(int)files.size(), bytes / 1048576.0, partA, chunks, chunkBad, tracks, trackBad);
	CHECK(chunkBad == 0, "%d chunk stream ends wrong", chunkBad);
	CHECK(trackBad == 0, "%d node tracks outside their range", trackBad);

	// SPEC-014 test vector 2: MESO_PLAYER WBOLT_HAND_R frames 0 and 819
	GhbFile mp;
	if (mp.Load("ghoul/enemy/meso/MESO_PLAYER.ghb"))
	{
		int ni = mp.FindNode("WBOLT_HAND_R");
		CHECK(ni >= 0, "WBOLT_HAND_R present");
		if (ni >= 0)
		{
			uint32_t w0, w1;
			mp.NodeWords(mp.nodes[ni], 0, w0, w1);
			CHECK(w0 == 0x3FF88605u && w1 == 0x9F65E962u, "frame 0 words %08X %08X", w0, w1);
			Matrix4 m;
			mp.NodeMatrix(mp.nodes[ni], 0, m);
			float want0[12] = {-7.9463f, -14.13325f, 18.97554f, -0.02825f, -0.9996f, -0.00127f, 0.00942f, -0.00153f,
				0.99995f, -0.99956f, 0.02824f, 0.00946f};
			float e = 0;
			for (int a = 0; a < 3; a++)
				e = std::max(e, std::fabs(m.m[3][a] - want0[a]));
			for (int i = 0; i < 9; i++)
				e = std::max(e, std::fabs(m.m[i / 3][i % 3] - want0[3 + i]));
			CHECK(e < 2e-4f, "WBOLT_HAND_R frame 0 error %g", e);
			mp.NodeMatrix(mp.nodes[ni], 819, m);
			float want1[12] = {-9.88696f, -15.89471f, 3.1862f, 0.00942f, -0.99975f, 0.02004f, -0.00942f, 0.01995f,
				0.99976f, -0.99991f, -0.00961f, -0.00923f};
			e = 0;
			for (int a = 0; a < 3; a++)
				e = std::max(e, std::fabs(m.m[3][a] - want1[a]));
			for (int i = 0; i < 9; i++)
				e = std::max(e, std::fabs(m.m[i / 3][i % 3] - want1[3 + i]));
			CHECK(e < 2e-4f, "WBOLT_HAND_R frame 819 error %g", e);
			// rigidity: _RCLOSED vertices expressed in the bolt's frame stay constant
			int mesh = mp.FindNode("_RCLOSED");
			if (mesh >= 0)
			{
				int mi = mp.nodes[mesh].meshIndex;
				std::vector<int> corners;
				for (auto &s : mp.geo.surfaces)
					if (s.mesh == mi)
						for (uint16_t c : s.tris)
							corners.push_back(c);
				std::vector<float> apos(mp.geo.nApos * 3);
				std::vector<std::vector<Vec3>> local;
				for (uint32_t fr = 0; fr < mp.frames; fr += mp.frames / 80)
				{
					mp.AnimPositions(fr, apos.data());
					Matrix4 bm;
					mp.NodeMatrix(mp.nodes[ni], fr, bm);
					Matrix4 bi = MatInverse(bm);
					std::vector<Vec3> l;
					for (int c : corners)
					{
						int pi = mp.geo.corners[c * 3];
						Vec3 v = pi < 0 ? Vec3(&mp.geo.pos[(size_t)~pi * 3]) : Vec3(&apos[(size_t)pi * 3]);
						l.push_back(XformPoint(v, bi));
					}
					local.push_back(l);
				}
				std::vector<float> sds;
				for (size_t k = 0; k < corners.size(); k++)
				{
					Vec3 mean;
					for (auto &l : local)
						mean += l[k];
					mean = mean * (1.0f / local.size());
					float var = 0;
					for (auto &l : local)
					{
						Vec3 d = l[k] - mean;
						var += Dot(d, d);
					}
					sds.push_back(std::sqrt(var / local.size()));
				}
				std::sort(sds.begin(), sds.end());
				float med = sds.empty() ? 0 : sds[sds.size() / 2];
				std::printf("   _RCLOSED in WBOLT_HAND_R frame: median sd %.3f over %d frames\n", med, (int)local.size());
				CHECK(med < 0.15f, "rigidity median sd %g", med);
			}
		}
	}
}

// ------------------------------------------------------------------ text files (SPEC-121)

static void TestTextFiles()
{
	std::vector<std::string> gsq = ListFiles("ghoul", ".gsq"), gpm = ListFiles("ghoul", ".gpm"),
							 gbm = ListFiles("ghoul", ".gbm"), ifl = ListFiles("ghoul", ".ifl");
	std::printf("== text files: %d .gsq, %d .gpm, %d .gbm, %d .ifl\n", (int)gsq.size(), (int)gpm.size(), (int)gbm.size(),
		(int)ifl.size());
	if (gpm.empty())
		return;
	// player models: every .gpm and .gbm loads and has the expected list shapes
	int pmOk = 0, skins = 0, boltons = 0, parts = 0;
	for (auto *list : {&gpm, &gbm})
		for (auto &p : *list)
		{
			std::string rel = p.substr(std::strlen("ghoul/pmodels/"));
			IGhoulPlayerModel *pm = (IGhoulPlayerModel *)OMGhoul_NewPlayerModel(rel.c_str());
			CHECK(pm->IsLoaded(), "player model %s", rel.c_str());
			if (pm->IsLoaded())
				pmOk++;
			skins += pm->NumSkins();
			boltons += pm->NumBoltons();
			parts += pm->NumPartSwitches();
			PlayerSkinEntry se;
			CHECK(!pm->GetSkinEntry(0, &se) && !pm->GetSkinEntry(pm->NumSkins() + 1, &se), "range rule %s", rel.c_str());
			pm->Destroy(1);
		}
	std::printf("   player models loaded %d (skins %d, bolt-ons %d, part switches %d)\n", pmOk, skins, boltons, parts);
	// SPEC-010 test vector 7: Dekker.gpm
	{
		IGhoulPlayerModel *pm = (IGhoulPlayerModel *)OMGhoul_NewPlayerModel("Dekker.gpm");
		char buf[256];
		CHECK(!std::strcmp(pm->GetModelDir(), "enemy/meso"), "dir %s", pm->GetModelDir());
		CHECK(!std::strcmp(pm->GetMenuSequence(), "ghoul/enemy/meso/meso_menu1.ghl"), "menu %s", pm->GetMenuSequence());
		CHECK(!std::strcmp(pm->GetBaseModel(), "meso_player"), "base");
		CHECK(!std::strcmp(pm->GetSequenceList(), "playerboss"), "set");
		pm->GetTeam(buf);
		CHECK(!std::strcmp(buf, "The Order"), "team %s", buf);
		pm->GetSoundDir(buf);
		CHECK(!std::strcmp(buf, "enemy/dth/rad"), "sound %s", buf);
		CHECK(!std::strcmp(pm->GetFacePrefix(), ""), "face '%s'", pm->GetFacePrefix());
		CHECK(pm->NumSkins() == 4 && pm->NumBoltons() == 1 && pm->NumPartSwitches() == 4, "counts %d %d %d",
			pm->NumSkins(), pm->NumBoltons(), pm->NumPartSwitches());
		PlayerSkinEntry se;
		CHECK(pm->GetSkinEntry(2, &se) && !std::strcmp(se.material, "b") && !std::strcmp(se.skin, "b_xdekker") &&
				se.part[0] == 0,
			"skin 2 %s %s '%s'", se.material, se.skin, se.part);
		PlayerBoltonEntry be;
		CHECK(pm->GetBoltonEntry(1, &be) && !std::strcmp(be.file, "boltons/menuassault.gbm") &&
				!std::strcmp(be.bolt, "wbolt_hand_r") && !std::strcmp(be.boltonBolt, "to_wbolt_hand_r") && be.scale == 1.0f,
			"bolt-on 1");
		PlayerPartEntry pe;
		CHECK(pm->GetPartEntry(4, &pe) && !std::strcmp(pe.part, "_mohawk") && pe.shown == 0, "part 4");
		pm->GetTrailer(buf);
		CHECK(buf[0] == 0, "trailer");
		pm->Destroy(1);
	}
	// .gsq: every list loads through import #62 and every include resolves
	int lists = 0, missingInc = 0, lines = 0;
	for (auto &p : gsq)
	{
		std::string rel = p.substr(6); // after "ghoul/"
		size_t s = rel.find_last_of('/');
		std::string folder = s == std::string::npos ? "" : rel.substr(0, s), file = rel.substr(s + 1);
		void *buf;
		int len = OMGhoul_LoadFile(folder.c_str(), file.c_str(), &buf);
		CHECK(len >= 0, "gsq %s", p.c_str());
		if (len < 0)
			continue;
		lists++;
		int rem = len;
		char *cur = (char *)buf;
		std::vector<char> line((size_t)len + 2);
		for (;;)
		{
			int more = OMGhoul_GetLine(&rem, &cur, line.data());
			std::string l(line.data());
			while (!l.empty() && (l.back() == ' ' || l.back() == '\t' || l.back() == '\r'))
				l.pop_back();
			if (!l.empty())
			{
				lines++;
				if (l[0] == '#')
				{
					void *b2;
					if (OMGhoul_LoadFile(folder.c_str(), (l.substr(1) + ".gsq").c_str(), &b2) < 0)
						missingInc++;
				}
			}
			if (!more)
				break;
		}
	}
	std::printf("   .gsq lists %d, items %d, unresolved includes %d\n", lists, lines, missingInc);
	CHECK(missingInc == 0, "includes");
	// SPEC-010 test vector 6 (import #63)
	{
		char data[] = "ab\r\ncd";
		int rem = 6;
		char *cur = data;
		char line[16];
		int r1 = OMGhoul_GetLine(&rem, &cur, line);
		CHECK(r1 == 1 && !std::strcmp(line, "ab"), "line 1 '%s'", line);
		int r2 = OMGhoul_GetLine(&rem, &cur, line);
		CHECK(r2 == 0 && !std::strcmp(line, "cd"), "line 2 '%s'", line);
	}
	// .ifl image lists resolve
	int iflOk = 0, images = 0;
	for (auto &p : ifl)
	{
		std::string up = NormalizeName(p);
		size_t s = up.find_last_of('/');
		ImageList *l = AcquireImageList(up.substr(0, s + 1), up.substr(s + 1));
		if (l)
		{
			iflOk++;
			images += (int)l->images.size();
			ReleaseImageList(l);
		}
	}
	std::printf("   .ifl lists resolved %d/%d (%d entries)\n", iflOk, (int)ifl.size(), images);
	CHECK(iflOk == (int)ifl.size(), "ifl lists");
}

int main(int argc, char **argv)
{
	std::string root = "C:/openmerc-data/ghoul";
	bool quick = false;
	std::string replay;
	std::vector<std::string> pos;
	for (int i = 1; i < argc; i++)
	{
		if (!std::strcmp(argv[i], "--quick"))
			quick = true;
		else if (!std::strcmp(argv[i], "--replay") && i + 1 < argc)
			replay = argv[++i];
		else
			pos.push_back(argv[i]);
	}
	if (pos.size() > 0)
		root = pos[0];
	g_outDir = pos.size() > 1 ? pos[1] : root + "/../renders/ghoul-i3";
	std::error_code ec;
	std::filesystem::create_directories(g_outDir, ec);
	InstallHost(root);
	g_host.quiet = true;
	g_haveData = !ListFiles("ghoul", ".ghb").empty();
	try
	{
		TestCodecs();
		TestBits();
		TestAllFiles();
		TestTextFiles();
		RunRuntimeTests();
		RunSaveNetTests();
		RunReplay(replay.empty() ? root + "/../net/cap1.ghoul.bin" : replay);
		RunRenderTests(g_outDir, quick);
	}
	catch (const std::exception &e)
	{
		std::printf("FATAL (uncaught): %s\n", e.what());
		g_fail++;
	}
	std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
	OMGhoul_Shutdown();
	return g_fail ? 1 : 0;
}
