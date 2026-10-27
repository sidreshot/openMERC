// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - stateless decoders: direction table, packed rotations, chunked
// vertex streams, normal keys, and the shared decoded-chunk cache.
// Implements: SPEC-014 sections 10, 12, 13; SPEC-012 (decoded-chunk cache: 3 MiB, FIFO);
//             SPEC-201 Appendix B.6 (packed rotation shared with the network transform)

#include <cmath>
#include <cstring>
#include <deque>
#include <map>

#include "ghb_file.h"
#include "ghoul_tables.h"

namespace ghoul {

// ---------------------------------------------------------------- direction table (section 13; built in ghoul_tables.cpp)

Vec3 Direction(int index)
{
	const float *t = DirTable() + (index & 255) * 3;
	return Vec3(t[0], t[1], t[2]);
}

int NearestDirection(const Vec3 &v)
{
	const float *t = DirTable();
	int best = 0;
	float bestDot = 0.0f;
	for (int i = 0; i < 256; i++)
	{
		float d = v.x * t[i * 3] + v.y * t[i * 3 + 1] + v.z * t[i * 3 + 2];
		if (d > bestDot)
		{
			bestDot = d;
			best = i;
		}
	}
	return best;
}

// ---------------------------------------------------------------- packed rotation (section 12)

static const float kRotQ = 0.018666666f;
static const float kRotQ0 = 0.14f;
static const float kTwoPi = 6.28318530717958647692f;

// steps 3-4 shared by decoder and encoder
static void RotationBasis(const Vec3 &t, const Vec3 &a, Vec3 &b, Vec3 &c)
{
	Vec3 p = Cross(t, Vec3(1, 0, 0));
	float lp = Length(p);
	Vec3 py = Cross(t, Vec3(0, 1, 0));
	float ly = Length(py);
	if (ly > lp)
	{
		p = py;
		lp = ly;
	}
	Vec3 pz = Cross(t, Vec3(0, 0, 1));
	if (Length(pz) > lp)
		p = pz;
	b = Normalize(Cross(p, a));
	c = Normalize(Cross(a, b));
}

static Vec3 CorrectedAxis(const Vec3 &t, uint32_t word)
{
	Vec3 c((float)((word >> 8) & 15) * kRotQ - kRotQ0, (float)((word >> 12) & 15) * kRotQ - kRotQ0,
		(float)((word >> 16) & 15) * kRotQ - kRotQ0);
	return Normalize(t + c);
}

void DecodePackedRotation(uint32_t word, float out[3][3])
{
	Vec3 t = Direction((int)(word & 255));
	Vec3 a = CorrectedAxis(t, word);
	Vec3 b, c;
	RotationBasis(t, a, b, c);
	float th = (float)((word >> 20) & 2047) * (kTwoPi / 2047.0f);
	Vec3 d = Normalize(b * std::cos(th) + c * std::sin(th));
	Vec3 e = Cross(a, d);
	if (word & 0x80000000u)
		e = -e;
	e = Normalize(e);
	for (int r = 0; r < 3; r++)
	{
		out[r][0] = a[r];
		out[r][1] = d[r];
		out[r][2] = e[r];
	}
}

uint32_t EncodePackedRotation(const Matrix4 &m)
{
	Vec3 col0(m.m[0][0], m.m[1][0], m.m[2][0]);
	Vec3 col1(m.m[0][1], m.m[1][1], m.m[2][1]);
	Vec3 col2(m.m[0][2], m.m[1][2], m.m[2][2]);
	Vec3 a0 = Normalize(col0);
	int index = NearestDirection(a0);
	Vec3 t = Direction(index);
	uint32_t q[3];
	for (int k = 0; k < 3; k++)
	{
		float v = std::floor((a0[k] - t[k] + kRotQ0) * 53.57143f + 0.5f);
		if (v < 0)
			v = 0;
		if (v > 15)
			v = 15;
		q[k] = (uint32_t)v;
	}
	uint32_t word = (uint32_t)index | (q[0] << 8) | (q[1] << 12) | (q[2] << 16);
	Vec3 a = CorrectedAxis(t, word);
	Vec3 b, c;
	RotationBasis(t, a, b, c);
	float ang = std::atan2(Dot(col1, c), Dot(col1, b));
	if (ang < 0)
		ang += kTwoPi;
	float rv = std::floor(ang * (2047.0f / kTwoPi));
	if (rv < 0)
		rv = 0;
	if (rv > 2047)
		rv = 2047;
	word |= ((uint32_t)rv & 2047u) << 20;
	if (Dot(Normalize(Cross(a, col1)), col2) < 0)
		word |= 0x80000000u;
	return word;
}

// ---------------------------------------------------------------- chunk streams (section 10)

namespace {
// k-bit field at stream bit p: the LE u32 at byte p/8, shifted by p%8, masked (k <= 25).
// Bytes beyond the data read as zero.
struct BitStream
{
	const uint8_t *d;
	size_t n;
	uint32_t p = 0;
	uint32_t Read(uint32_t k)
	{
		if (k == 0)
			return 0;
		size_t o = p >> 3;
		uint32_t v = 0;
		for (int i = 0; i < 4; i++)
			if (o + i < n)
				v |= (uint32_t)d[o + i] << (8 * i);
		v >>= (p & 7);
		p += k;
		return k >= 32 ? v : (v & ((1u << k) - 1u));
	}
};
} // namespace

uint32_t DecodePositionStream(const uint8_t *data, size_t size, uint32_t nApos, uint32_t nFrames,
	const uint8_t bits[3], const float scale[3], const float offset[3], float *out)
{
	BitStream bs{data, size};
	std::memset(out, 0, sizeof(float) * 3 * nApos * nFrames);
	if (nFrames == 1)
	{
		for (uint32_t v = 0; v < nApos; v++)
			for (int a = 0; a < 3; a++)
				out[v * 3 + a] = offset[a] + (float)bs.Read(bits[a]) * scale[a];
		return bs.p;
	}
	const size_t fstride = (size_t)nApos * 3; // frame-major output
	uint32_t R = 1;
	for (uint32_t v = 0; v < nApos; v++)
	{
		if ((int64_t)(1ull << R) <= (int64_t)v - 1)
			R++;
		uint32_t mode = bs.Read(2);
		if (mode == 0)
		{
			int32_t val[3];
			uint32_t w[3];
			for (int a = 0; a < 3; a++)
			{
				val[a] = (int32_t)bs.Read(bits[a]);
				w[a] = bs.Read(4);
			}
			for (int a = 0; a < 3; a++)
				out[v * 3 + a] = offset[a] + (float)val[a] * scale[a];
			for (uint32_t f = 1; f < nFrames; f++)
			{
				for (int a = 0; a < 3; a++)
					if (w[a])
						val[a] += (int32_t)bs.Read(w[a] + 1) - (int32_t)(1u << w[a]);
				for (int a = 0; a < 3; a++)
					out[f * fstride + v * 3 + a] = offset[a] + (float)val[a] * scale[a];
			}
		}
		else // modes 1, 2, 3: relative to an earlier vertex (2/3 decode as 1)
		{
			uint32_t ref = bs.Read(R);
			uint32_t M = bs.Read(4);
			uint32_t w[3];
			int32_t mn[3] = {0, 0, 0};
			for (int a = 0; a < 3; a++)
			{
				w[a] = bs.Read(4);
				if (M)
				{
					int32_t mag = (int32_t)bs.Read(M);
					uint32_t sign = bs.Read(1);
					mn[a] = sign ? -mag : mag;
				}
			}
			for (uint32_t f = 0; f < nFrames; f++)
				for (int a = 0; a < 3; a++)
				{
					uint32_t u = w[a] ? bs.Read(w[a]) : 0;
					float rp = ref < nApos ? out[f * fstride + ref * 3 + a] : 0.0f;
					out[f * fstride + v * 3 + a] = rp + (float)(mn[a] + (int32_t)u) * scale[a];
				}
		}
	}
	return bs.p;
}

void DecodeNormalStream(const uint8_t *data, size_t size, uint32_t nAnorm, uint32_t n, float *out)
{
	uint32_t k = NormalKeysPerChunk(n);
	const size_t fstride = (size_t)nAnorm * 3;
	uint32_t h = n / 2;
	for (uint32_t i = 0; i < nAnorm; i++)
	{
		Vec3 t[3];
		for (uint32_t j = 0; j < k; j++)
		{
			size_t o = (size_t)i * k + j;
			t[j] = Direction(o < size ? data[o] : 0);
		}
		for (uint32_t f = 0; f < n; f++)
		{
			Vec3 r;
			if (n == 1)
				r = t[0];
			else if (n == 2)
				r = t[f];
			else if (n <= 4)
				r = t[0] + (t[1] - t[0]) * ((float)f / (float)(n - 1));
			else if (f <= h)
				r = t[0] + (t[1] - t[0]) * ((float)f / (float)h);
			else
				r = t[1] + (t[1] - t[0]) * (1.0f / (float)h) + (t[2] - t[1]) * ((float)(f - h - 1) / (float)(n - 1 - h));
			r.Store(out + f * fstride + i * 3);
		}
	}
}

bool DecodeChunk(const GhbFile &f, uint32_t ci, DecodedChunk &out)
{
	const GhbGeometry &g = f.geo;
	if (!g.chunked || ci >= g.chunks.size())
		return false;
	const GhbChunk &c = g.chunks[ci];
	const uint8_t *base = f.PartB() + g.nodeanimSize;
	out.first = c.first;
	out.count = c.count;
	out.nApos = g.nApos;
	out.nAnorm = g.nAnorm;
	out.pos.assign((size_t)g.nApos * 3 * c.count, 0.0f);
	out.norm.assign((size_t)g.nAnorm * 3 * c.count, 0.0f);
	out.posBitsUsed = DecodePositionStream(base + c.posOffset, c.normOffset - c.posOffset, g.nApos, c.count, c.bits,
		c.scale, c.offset, out.pos.data());
	DecodeNormalStream(base + c.normOffset, c.normEnd - c.normOffset, g.nAnorm, c.count, out.norm.data());
	return true;
}

// ---------------------------------------------------------------- decoded-chunk cache (SPEC-012)

namespace {
const size_t kChunkCacheBytes = 3u * 1024u * 1024u;
struct ChunkCache
{
	typedef std::pair<uint32_t, uint32_t> Key; // file serial, chunk
	std::map<Key, std::shared_ptr<const DecodedChunk>> map;
	std::deque<Key> fifo;
	size_t bytes = 0;
};
// Never destroyed: the shared file cache (G()) can be torn down at process exit after this cache,
// and each GhbFile destructor still calls ForgetChunksOf(); a function-local static here would
// already be gone by then (double free at quit).
ChunkCache &Cache()
{
	static ChunkCache *c = new ChunkCache;
	return *c;
}
} // namespace

std::shared_ptr<const DecodedChunk> GetDecodedChunk(const GhbFile &f, uint32_t ci)
{
	ChunkCache &c = Cache();
	ChunkCache::Key k(f.serial, ci);
	auto it = c.map.find(k);
	if (it != c.map.end())
		return it->second;
	auto d = std::make_shared<DecodedChunk>();
	DecodeChunk(f, ci, *d);
	size_t sz = d->Bytes();
	// first-in-first-out eviction; a block larger than the heap still gets decoded (returned, cached alone)
	while (!c.fifo.empty() && c.bytes + sz > kChunkCacheBytes)
	{
		auto old = c.map.find(c.fifo.front());
		if (old != c.map.end())
		{
			c.bytes -= old->second->Bytes();
			c.map.erase(old);
		}
		c.fifo.pop_front();
	}
	c.map[k] = d;
	c.fifo.push_back(k);
	c.bytes += sz;
	return d;
}

void FlushChunkCache()
{
	ChunkCache &c = Cache();
	c.map.clear();
	c.fifo.clear();
	c.bytes = 0;
}

size_t ChunkCacheBytes() { return Cache().bytes; }

void ForgetChunksOf(uint32_t serial)
{
	ChunkCache &c = Cache();
	for (auto it = c.map.begin(); it != c.map.end();)
	{
		if (it->first.first == serial)
		{
			c.bytes -= it->second->Bytes();
			it = c.map.erase(it);
		}
		else
			++it;
	}
	std::deque<ChunkCache::Key> keep;
	for (auto &k : c.fifo)
		if (k.first != serial)
			keep.push_back(k);
	c.fifo.swap(keep);
}

} // namespace ghoul
