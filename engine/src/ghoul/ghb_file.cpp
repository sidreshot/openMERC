// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - .ghb parser (part A, static block, per-frame data).
// Implements: SPEC-120 (layout), SPEC-014 sections 1-9, 14 (engine reading of every field); SPEC-AMEND-005 I3.10
//             (LIGHT records of kinds 1-4)

#include "ghb_file.h"

#include <algorithm>
#include <cstring>

namespace ghoul {

static uint32_t g_serial = 0;

GhbFile::GhbFile() { serial = ++g_serial; }
GhbFile::~GhbFile() { ForgetChunksOf(serial); }

namespace {

struct LoadError
{
	std::string why;
};

// Bounds-checked little-endian reader over part A. Overruns are a non-fatal load failure.
class Reader
{
public:
	Reader(const uint8_t *d, size_t n, const std::string &path) : d_(d), n_(n), path_(path) {}
	size_t pos = 0;

	void Need(size_t k)
	{
		if (pos + k > n_)
			throw LoadError{StrFormat("truncated at 0x%x", (unsigned)pos)};
	}
	uint8_t U8()
	{
		Need(1);
		return d_[pos++];
	}
	bool Flag() { return U8() == 1; } // SPEC-014: a one-byte flag is true only when exactly 1
	uint32_t U32()
	{
		Need(4);
		uint32_t v = (uint32_t)d_[pos] | ((uint32_t)d_[pos + 1] << 8) | ((uint32_t)d_[pos + 2] << 16) |
			((uint32_t)d_[pos + 3] << 24);
		pos += 4;
		return v;
	}
	int32_t I32() { return (int32_t)U32(); }
	float F32()
	{
		uint32_t u = U32();
		float f;
		std::memcpy(&f, &u, 4);
		return f;
	}
	Vec3 V3()
	{
		Vec3 v;
		v.x = F32();
		v.y = F32();
		v.z = F32();
		return v;
	}
	std::string Str()
	{
		uint32_t len = U32();
		if (len > kMaxGhbString)
			Fatal("Huge String");
		Need(len);
		std::string s((const char *)d_ + pos, len);
		pos += len;
		return s;
	}
	Matrix4 Mat()
	{
		Matrix4 m;
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				m.m[r][c] = F32();
		m.flags = U32();
		return m;
	}
	BoundsRec Bounds()
	{
		BoundsRec b;
		b.infinite = Flag();
		b.empty = Flag();
		b.mn = V3();
		b.mx = V3();
		return b;
	}
	// TRACK of n values
	uint32_t TrackCount()
	{
		uint32_t n = U32();
		if (n > 1000000)
			throw LoadError{StrFormat("implausible track length %u", n)};
		return n;
	}
	void TrackF(Track<float> &t)
	{
		t.constant = U8() == 1;
		uint32_t n = t.constant ? 1 : TrackCount();
		t.v.resize(n ? n : 1);
		if (!n)
			t.v[0] = 0.0f;
		for (uint32_t i = 0; i < n; i++)
			t.v[i] = F32();
	}
	void TrackV(Track<Vec3> &t)
	{
		t.constant = U8() == 1;
		uint32_t n = t.constant ? 1 : TrackCount();
		t.v.resize(n ? n : 1);
		for (uint32_t i = 0; i < n; i++)
			t.v[i] = V3();
	}
	void TrackM(Track<Matrix4> &t)
	{
		t.constant = U8() == 1;
		uint32_t n = t.constant ? 1 : TrackCount();
		t.v.resize(n ? n : 1);
		if (!n)
			MatIdentity(t.v[0]);
		for (uint32_t i = 0; i < n; i++)
			t.v[i] = Mat();
	}
	void TrackU8(Track<uint8_t> &t)
	{
		t.constant = U8() == 1;
		uint32_t n = t.constant ? 1 : TrackCount();
		t.v.resize(n ? n : 1);
		if (!n)
			t.v[0] = 0;
		for (uint32_t i = 0; i < n; i++)
			t.v[i] = U8();
	}
	void TrackBool(BoolTrack &t)
	{
		t.constant = U8() == 1;
		if (t.constant)
		{
			t.value = U8() == 1;
			return;
		}
		t.n = TrackCount();
		t.words.resize((t.n + 31) / 32);
		for (auto &w : t.words)
			w = U32();
	}

private:
	const uint8_t *d_;
	size_t n_;
	const std::string &path_;
};

void ReadNode(Reader &r, GhbNode &n, GhbNodeKind kind)
{
	n.kind = kind;
	n.name = r.Str();
	n.nameUpper = Upper(n.name);
	n.user = r.Str();
	r.TrackBool(n.visible);
	n.sprite = r.Flag();
	n.spriteball = r.Flag();
	n.noblock = r.Flag();
	n.bounds = r.Bounds();
	n.ncomp = r.U32();
	if (n.ncomp == 0)
		r.TrackM(n.mat);
	else
	{
		for (int i = 0; i < 3; i++)
			n.qscale[i] = r.F32();
		for (int i = 0; i < 3; i++)
			n.qinv[i] = r.F32();
		for (int i = 0; i < 3; i++)
			n.qofs[i] = r.F32();
		n.dataOffset = r.U32();
	}
}

void ReadSlot(Reader &r, GhbSlot &s)
{
	s.texture = r.Str();
	s.reserved = r.Str();
	s.hasTexture = r.Flag();
	s.environment = r.Flag();
	s.tileU = r.Flag();
	s.tileV = r.Flag();
	r.TrackU8(s.image);
	r.TrackM(s.uv);
	s.staticUV = r.Flag();
}

void ReadMaterial(Reader &r, GhbMaterial &m)
{
	m.name = Upper(r.Str());
	m.twoSided = r.Flag();
	m.alphaTexture = r.Flag();
	m.blend = r.U32();
	(void)r.U32(); // slot count: ignored, exactly two slots follow (SPEC-014 section 4)
	ReadSlot(r, m.slots[0]);
	ReadSlot(r, m.slots[1]);
	r.TrackV(m.diffuse);
	r.TrackV(m.specular);
	r.TrackF(m.gloss);
	r.TrackF(m.specLevel);
	r.TrackF(m.selfIllum);
	r.TrackF(m.opacity);
	// classification (constant tracks compared; animated tracks count as "may change")
	float op = 0, g = 0, sl = 0, si = 0;
	bool opConst = m.opacity.ConstValue(op);
	m.opaque = m.blend == 0 && !(m.alphaTexture && m.slots[0].hasTexture) && opConst && op >= 0.99f;
	bool noGloss = m.gloss.ConstValue(g) && g <= 0.0f;
	bool lowLevel = m.specLevel.ConstValue(sl) && sl < 0.051f;
	bool selfLit = m.selfIllum.ConstValue(si) && si >= 0.99f;
	m.specularCapable = !noGloss && !lowLevel && !selfLit;
	m.textured = false;
	for (auto &s : m.slots)
		if (s.hasTexture && !s.environment)
			m.textured = true;
}

// SPEC-120 index list grammar -> triangles (corner indexes)
bool DecodeIndexList(const int16_t *idx, int n, std::vector<uint16_t> &tris)
{
	int i = 0;
	while (i < n)
	{
		int c = idx[i++];
		if (c == 0)
			break;
		int k = c < 0 ? -c : c;
		if (i + k > n)
			return false;
		const int16_t *run = idx + i;
		i += k;
		if (c < 0) // fan
		{
			for (int j = 1; j + 1 < k; j++)
			{
				tris.push_back((uint16_t)run[0]);
				tris.push_back((uint16_t)run[j]);
				tris.push_back((uint16_t)run[j + 1]);
			}
		}
		else // strip
		{
			for (int j = 0; j + 2 < k; j++)
			{
				if (j & 1)
				{
					tris.push_back((uint16_t)run[j + 1]);
					tris.push_back((uint16_t)run[j]);
				}
				else
				{
					tris.push_back((uint16_t)run[j]);
					tris.push_back((uint16_t)run[j + 1]);
				}
				tris.push_back((uint16_t)run[j + 2]);
			}
		}
	}
	if (i < n)
	{
		int k = idx[i++];
		if (k < 0 || i + k > n)
			return false;
		for (int j = 0; j + 2 < k; j += 3)
		{
			tris.push_back((uint16_t)idx[i + j]);
			tris.push_back((uint16_t)idx[i + j + 1]);
			tris.push_back((uint16_t)idx[i + j + 2]);
		}
	}
	return true;
}

} // namespace

bool GhbFile::Load(const std::string &gamePath)
{
	void *d = nullptr;
	int len = Host().load_file ? Host().load_file(Host().ctx, gamePath.c_str(), &d) : -1;
	if (len < 0 || !d)
	{
		if (d && Host().free_file)
			Host().free_file(Host().ctx, d);
		return false;
	}
	bytes_.assign((const uint8_t *)d, (const uint8_t *)d + len);
	if (Host().free_file)
		Host().free_file(Host().ctx, d);
	std::string why;
	bool ok = Parse(gamePath, &why);
	if (!ok && !why.empty())
		Print("Ghoul :%s: %s\n", gamePath.c_str(), why.c_str());
	return ok;
}

bool GhbFile::ParseMemory(const uint8_t *b, size_t size, const std::string &gamePath, std::string *error)
{
	bytes_.assign(b, b + size);
	return Parse(gamePath, error);
}

bool GhbFile::Parse(const std::string &gamePath, std::string *error)
{
	path = gamePath;
	cacheKey = StripExtension(NormalizeName(gamePath));
	dir = DirectoryOf(NormalizeName(gamePath));
	const uint8_t *d = bytes_.data();
	size_t n = bytes_.size();
	Reader r(d, n, path);
	try
	{
		if (n < 24 || r.U32() != kGhbMagic)
		{
			Print("Old file %s\n", gamePath.c_str());
			if (error)
				*error = "";
			return false;
		}
		sizeA = r.U32();
		sizeB = r.U32();
		if (r.U32() != kGhbTag)
			Fatal("Not a ghb file %s", gamePath.c_str());
		uint32_t version = r.U32();
		if (version != kGhbVersion)
		{
			if (error)
				*error = StrFormat("version %u", version);
			return false;
		}
		if ((uint64_t)sizeA + sizeB > n)
			throw LoadError{"size fields exceed the file"};
		thunk = r.U32() == 1;
		if (thunk)
		{
			Print("Thunks are obsolete %s\n", gamePath.c_str());
			return true; // counts as loaded, no content
		}
		source = r.Str();
		frames = r.U32();
		uint32_t nseq = r.U32();
		if (nseq > 100000)
			throw LoadError{"implausible sequence count"};
		sequences.resize(nseq);
		for (auto &s : sequences)
		{
			for (int i = 0; i < 3; i++)
				s.reserved[i] = r.F32();
			s.frameMs = r.F32();
			s.name = r.Str();
			s.nameUpper = Upper(s.name);
			uint32_t ne = r.U32();
			if (ne > 100000)
				throw LoadError{"implausible event count"};
			s.events.resize(ne);
			for (auto &e : s.events)
			{
				e.timeMs = r.F32();
				e.reserved = r.I32();
				e.type = r.Str();
				e.typeUpper = Upper(e.type);
				e.arg = r.Str();
			}
			s.first = r.U32();
			s.count = r.U32();
			s.keysByTime.resize(ne);
			for (uint32_t i = 0; i < ne; i++)
				s.keysByTime[i] = i;
			std::stable_sort(s.keysByTime.begin(), s.keysByTime.end(),
				[&s](uint32_t a, uint32_t b) { return s.events[a].timeMs < s.events[b].timeMs; });
		}
		uint32_t nmat = r.U32();
		if (nmat > 10000)
			throw LoadError{"implausible material count"};
		materials.resize(nmat);
		for (auto &m : materials)
			ReadMaterial(r, m);
		bounds = r.Bounds();

		// four node lists; the list decides the part type
		(void)r.U32();
		uint32_t cnt = r.U32();
		if (cnt > 100000)
			throw LoadError{"implausible node count"};
		for (uint32_t i = 0; i < cnt; i++)
		{
			nodes.emplace_back();
			ReadNode(r, nodes.back(), kNodeNull);
		}
		(void)r.U32();
		cnt = r.U32();
		if (cnt > 100000)
			throw LoadError{"implausible camera count"};
		for (uint32_t i = 0; i < cnt; i++)
		{
			nodes.emplace_back();
			GhbNode &c = nodes.back();
			ReadNode(r, c, kNodeCamera);
			for (int k = 0; k < 4; k++)
				r.TrackV(c.camera[k]);
		}
		(void)r.U32();
		cnt = r.U32();
		if (cnt > 100000)
			throw LoadError{"implausible mesh count"};
		for (uint32_t i = 0; i < cnt; i++)
		{
			nodes.emplace_back();
			GhbNode &m = nodes.back();
			ReadNode(r, m, kNodeMesh);
			m.meshBounds = r.Bounds();
			m.meshIndex = (int)meshes.size();
			meshes.push_back((int)nodes.size() - 1);
		}
		GhbGeometry &g = geo;
		if (cnt)
		{
			hasGeometry = true;
			g.firstWord = r.I32();
			if (g.firstWord == -1)
			{
				g.revision = r.U32();
				g.meshCount = r.U32();
			}
			else
				g.meshCount = (uint32_t)g.firstWord; // older layout (no stock file)
			g.materialCount = r.U32();
			g.nPos = r.U32();
			g.nUv = r.U32();
			g.nNorm = r.U32();
			g.nSurf = r.U32();
			g.nIdx = r.U32();
			g.nCorner = r.U32();
			g.nApos = r.U32();
			g.nAtex = r.U32();
			g.nAnorm = r.U32();
			for (auto &o : g.offsets)
				o = r.U32();
			g.bounds = r.Bounds();
			for (int i = 0; i < 3; i++)
				g.vscale[i] = r.F32();
			for (int i = 0; i < 3; i++)
				g.vinv[i] = r.F32();
			for (int i = 0; i < 3; i++)
				g.center[i] = r.F32();
			for (int i = 0; i < 3; i++)
				g.atexScale[i] = r.F32();
			if (g.revision == 1)
			{
				g.chunked = r.Flag();
				if (g.chunked)
				{
					g.nApos2 = r.U32();
					g.nAnorm2 = r.U32();
					uint32_t nf = r.U32();
					if (nf > 1000000)
						throw LoadError{"implausible chunk frame count"};
					g.frameChunk.resize(nf);
					for (auto &f : g.frameChunk)
						f = r.U32();
					g.vanimSize = r.U32();
					g.nodeanimSize = r.U32();
					uint32_t nc = r.U32();
					if (nc > 1000000)
						throw LoadError{"implausible chunk count"};
					g.chunks.resize(nc);
					for (auto &c : g.chunks)
					{
						c.first = r.U32();
						c.count = r.U32();
						for (int i = 0; i < 3; i++)
							c.scale[i] = r.F32();
						for (int i = 0; i < 3; i++)
							c.offset[i] = r.F32();
						for (int i = 0; i < 3; i++)
							c.bits[i] = r.U8();
						c.posOffset = r.U32();
						c.normOffset = r.U32();
					}
					for (size_t i = 0; i < g.chunks.size(); i++)
						g.chunks[i].normEnd = i + 1 < g.chunks.size() ? g.chunks[i + 1].posOffset : g.vanimSize;
				}
			}
			g.staticSize = r.U32();
			g.staticOffset = r.U32();
		}
		(void)r.U32();
		cnt = r.U32();
		if (cnt > 100000)
			throw LoadError{"implausible light count"};
		for (uint32_t i = 0; i < cnt; i++)
		{
			nodes.emplace_back();
			GhbNode &l = nodes.back();
			ReadNode(r, l, kNodeLight);
			l.world = r.Flag();
			l.lightKind = r.U32();
			r.TrackV(l.lightPos);
			r.TrackV(l.lightColor);
			r.TrackF(l.brightness);
			if (l.lightKind <= 4)
			{
				r.TrackF(l.multiplier);
				r.TrackF(l.attStart);
				r.TrackF(l.attEnd);
				l.lightParsed = true;
				// SPEC-AMEND-005 I3.10: kinds 1 and 3 add vec3, f32, f32, vec3, vec3; kinds 2 and 4 the same and one more
				// f32. Their meanings (cone axis or target, hotspot, falloff, aspect) are UNVERIFIED: the tracks are
				// read to stay aligned and not used, so such a light acts like kind 0 with its six common tracks.
				if (l.lightKind >= 1)
				{
					Track<Vec3> v;
					Track<float> f;
					r.TrackV(v);
					r.TrackF(f);
					r.TrackF(f);
					r.TrackV(v);
					r.TrackV(v);
					if (l.lightKind == 2 || l.lightKind == 4)
						r.TrackF(f);
				}
			}
			// kinds above 4: nothing more is read and the node has no light
			lights.push_back((int)nodes.size() - 1);
		}

		// ---- part B
		const uint8_t *B = PartB();
		// compressed node tracks must lie inside part B
		for (auto &nd : nodes)
			if (nd.ncomp && (uint64_t)nd.dataOffset + (uint64_t)nd.ncomp * 8 > sizeB)
				throw LoadError{StrFormat("node %s: track outside part B", nd.name.c_str())};

		if (hasGeometry)
		{
			if ((uint64_t)g.staticOffset + g.staticSize > sizeB)
				throw LoadError{"static block outside part B"};
			const uint8_t *S = B + g.staticOffset;
			auto secStart = [&](int k) -> uint32_t { return g.offsets[k]; };
			auto secEnd = [&](int k) -> uint32_t { return k + 1 < 10 ? g.offsets[k + 1] : g.staticSize; };
			auto need = [&](int k, uint64_t bytes) {
				if (secStart(k) > secEnd(k) || secEnd(k) > g.staticSize || secEnd(k) - secStart(k) < bytes)
					throw LoadError{StrFormat("static section %d too small", k)};
			};
			auto f32at = [&](uint32_t o) {
				float f;
				std::memcpy(&f, S + o, 4);
				return f;
			};
			auto i16at = [&](uint32_t o) { return (int16_t)(S[o] | (S[o + 1] << 8)); };
			auto u32at = [&](uint32_t o) {
				return (uint32_t)S[o] | ((uint32_t)S[o + 1] << 8) | ((uint32_t)S[o + 2] << 16) | ((uint32_t)S[o + 3] << 24);
			};
			// material groups: three u32 each
			uint32_t ngroups = (secEnd(0) - secStart(0)) / 12;
			need(0, (uint64_t)ngroups * 12);
			g.groups.resize(ngroups * 3);
			for (uint32_t i = 0; i < ngroups * 3; i++)
				g.groups[i] = u32at(secStart(0) + i * 4);
			need(1, (uint64_t)g.nPos * 12);
			g.pos.resize(g.nPos * 3);
			for (uint32_t i = 0; i < g.nPos * 3; i++)
				g.pos[i] = f32at(secStart(1) + i * 4);
			need(2, (uint64_t)g.nUv * 8);
			g.uv.resize(g.nUv * 2);
			for (uint32_t i = 0; i < g.nUv * 2; i++)
				g.uv[i] = f32at(secStart(2) + i * 4);
			need(3, (uint64_t)g.nNorm * 12);
			g.norm.resize(g.nNorm * 3);
			for (uint32_t i = 0; i < g.nNorm * 3; i++)
				g.norm[i] = f32at(secStart(3) + i * 4);
			need(4, (uint64_t)g.nSurf * 6);
			need(5, (uint64_t)g.nIdx * 2);
			std::vector<int16_t> idx(g.nIdx);
			for (uint32_t i = 0; i < g.nIdx; i++)
				idx[i] = i16at(secStart(5) + i * 2);
			need(6, (uint64_t)g.nCorner * 6);
			g.corners.resize(g.nCorner * 3);
			for (uint32_t i = 0; i < g.nCorner * 3; i++)
				g.corners[i] = i16at(secStart(6) + i * 2);
			g.surfaces.resize(g.nSurf);
			for (uint32_t s = 0; s < g.nSurf; s++)
			{
				GhbSurface &sf = g.surfaces[s];
				sf.mesh = i16at(secStart(4) + s * 6);
				sf.firstIndex = (uint16_t)i16at(secStart(4) + s * 6 + 2);
				sf.numIndex = (uint16_t)i16at(secStart(4) + s * 6 + 4);
				sf.node = (sf.mesh >= 0 && sf.mesh < (int)meshes.size()) ? meshes[sf.mesh] : -1;
				if ((uint32_t)sf.firstIndex + (uint32_t)sf.numIndex > g.nIdx)
					throw LoadError{StrFormat("surface %u: index list out of range", s)};
				if (!DecodeIndexList(idx.data() + sf.firstIndex, sf.numIndex, sf.tris))
					throw LoadError{StrFormat("surface %u: bad index list", s)};
				for (uint16_t c : sf.tris)
					if (c >= g.nCorner)
						throw LoadError{StrFormat("surface %u: corner out of range", s)};
			}
			for (uint32_t gi = 0; gi < ngroups; gi++)
			{
				uint32_t mat = g.groups[gi * 3], f0 = g.groups[gi * 3 + 1], fc = g.groups[gi * 3 + 2];
				for (uint32_t s = f0; s < f0 + fc && s < g.nSurf; s++)
					g.surfaces[s].material = mat;
			}
			// corner references in range
			for (uint32_t c = 0; c < g.nCorner; c++)
			{
				int p = g.corners[c * 3], nn = g.corners[c * 3 + 1], t = g.corners[c * 3 + 2];
				bool ok = (p < 0 ? (uint32_t)~p < g.nPos : (uint32_t)p < g.nApos) &&
					(nn < 0 ? (uint32_t)~nn < g.nNorm : (uint32_t)nn < g.nAnorm) &&
					(t < 0 ? (uint32_t)~t < g.nUv : (uint32_t)t < g.nAtex);
				if (!ok)
					throw LoadError{StrFormat("corner %u: reference out of range", c)};
			}
			if (!g.chunked)
			{
				// per-sequence element-major blocks (SPEC-014 section 9)
				size_t pa = 0, pt = 0, pn = 0;
				for (auto &s : sequences)
				{
					s.aposBlock = pa;
					s.atexBlock = pt;
					s.anormBlock = pn;
					pa += (size_t)g.nApos * s.count;
					pt += (size_t)g.nAtex * s.count;
					pn += (size_t)g.nAnorm * s.count;
				}
				if (g.nApos)
				{
					need(7, (uint64_t)pa * 6);
					g.apos = S + secStart(7);
				}
				if (g.nAtex)
				{
					need(8, (uint64_t)pt * 4);
					g.atex = S + secStart(8);
				}
				if (g.nAnorm)
				{
					need(9, (uint64_t)pn);
					g.anorm = S + secStart(9);
				}
			}
			else
			{
				if ((uint64_t)g.nodeanimSize + g.vanimSize > sizeB)
					throw LoadError{"chunk data outside part B"};
				for (auto &c : g.chunks)
					if (c.posOffset > c.normOffset || c.normOffset > c.normEnd || c.normEnd > g.vanimSize ||
						c.count == 0)
						throw LoadError{"bad chunk record"};
				for (auto ci : g.frameChunk)
					if (ci >= g.chunks.size())
						throw LoadError{"bad frame chunk index"};
			}
		}
		// frame -> sequence
		frameSequence.assign(frames, 0);
		for (size_t s = 0; s < sequences.size(); s++)
			for (uint32_t f = sequences[s].first; f < sequences[s].first + sequences[s].count && f < frames; f++)
				frameSequence[f] = (uint32_t)s;
		usedNodes.assign(nodes.size(), 0);
		if (r.pos != sizeA && error)
			*error = StrFormat("part A ends at 0x%x, header says 0x%x", (unsigned)r.pos, sizeA);
		return true;
	}
	catch (const LoadError &e)
	{
		if (error)
			*error = e.why;
		return false;
	}
}

int GhbFile::FindNode(const std::string &upperName) const
{
	for (size_t i = 0; i < nodes.size(); i++)
		if (nodes[i].nameUpper == upperName)
			return (int)i;
	return -1;
}

int GhbFile::FindSequence(const std::string &upperName) const
{
	for (size_t i = 0; i < sequences.size(); i++)
		if (sequences[i].nameUpper == upperName)
			return (int)i;
	return -1;
}

int GhbFile::FindMaterial(const std::string &upperName) const
{
	for (size_t i = 0; i < materials.size(); i++)
		if (materials[i].name == upperName)
			return (int)i;
	return -1;
}

// ---- per-frame data

void GhbFile::AnimPositions(uint32_t frame, float *out) const
{
	const GhbGeometry &g = geo;
	if (!g.nApos)
		return;
	if (frame >= frames && frames)
		frame = frames - 1;
	if (g.chunked)
	{
		if (frame >= g.frameChunk.size())
		{
			std::memset(out, 0, g.nApos * 12);
			return;
		}
		uint32_t ci = g.frameChunk[frame];
		auto ch = GetDecodedChunk(*this, ci);
		uint32_t f = frame - ch->first;
		if (f >= ch->count)
			f = ch->count - 1;
		std::memcpy(out, ch->pos.data() + (size_t)f * g.nApos * 3, g.nApos * 12);
		return;
	}
	if (!g.apos || sequences.empty())
	{
		std::memset(out, 0, g.nApos * 12);
		return;
	}
	const GhbSequence &s = sequences[frameSequence[frame]];
	uint32_t fr = frame - s.first;
	const uint8_t *base = g.apos + s.aposBlock * 6;
	for (uint32_t v = 0; v < g.nApos; v++)
	{
		const uint8_t *p = base + ((size_t)v * s.count + fr) * 6;
		for (int a = 0; a < 3; a++)
		{
			int16_t q = (int16_t)(p[a * 2] | (p[a * 2 + 1] << 8));
			out[v * 3 + a] = g.center[a] + (float)q * g.vinv[a];
		}
	}
}

void GhbFile::AnimNormals(uint32_t frame, float *out) const
{
	const GhbGeometry &g = geo;
	if (!g.nAnorm)
		return;
	if (frame >= frames && frames)
		frame = frames - 1;
	if (g.chunked)
	{
		if (frame >= g.frameChunk.size())
		{
			std::memset(out, 0, g.nAnorm * 12);
			return;
		}
		uint32_t ci = g.frameChunk[frame];
		auto ch = GetDecodedChunk(*this, ci);
		uint32_t f = frame - ch->first;
		if (f >= ch->count)
			f = ch->count - 1;
		std::memcpy(out, ch->norm.data() + (size_t)f * g.nAnorm * 3, g.nAnorm * 12);
		return;
	}
	if (!g.anorm || sequences.empty())
	{
		std::memset(out, 0, g.nAnorm * 12);
		return;
	}
	const GhbSequence &s = sequences[frameSequence[frame]];
	uint32_t fr = frame - s.first;
	const uint8_t *base = g.anorm + s.anormBlock;
	for (uint32_t v = 0; v < g.nAnorm; v++)
		Direction(base[(size_t)v * s.count + fr]).Store(out + v * 3);
}

void GhbFile::AnimTexCoords(uint32_t frame, float *out) const
{
	const GhbGeometry &g = geo;
	if (!g.nAtex)
		return;
	if (frame >= frames && frames)
		frame = frames - 1;
	// chunked models carry no animated texture coordinates (none in stock data)
	if (!g.atex || g.chunked || sequences.empty())
	{
		std::memset(out, 0, g.nAtex * 8);
		return;
	}
	const GhbSequence &s = sequences[frameSequence[frame]];
	uint32_t fr = frame - s.first;
	const uint8_t *base = g.atex + s.atexBlock * 4;
	for (uint32_t v = 0; v < g.nAtex; v++)
	{
		const uint8_t *p = base + ((size_t)v * s.count + fr) * 4;
		int16_t u = (int16_t)(p[0] | (p[1] << 8)), t = (int16_t)(p[2] | (p[3] << 8));
		out[v * 2] = (float)u * g.atexScale[0];
		out[v * 2 + 1] = (float)t * g.atexScale[1];
	}
}

// ---- node transforms (SPEC-014 section 12 and Behaviour)

bool GhbFile::NodeWords(const GhbNode &n, uint32_t frame, uint32_t &w0, uint32_t &w1) const
{
	if (!n.ncomp)
		return false;
	if (frame >= n.ncomp)
		frame = n.ncomp - 1;
	const uint8_t *p = PartB() + n.dataOffset + (size_t)frame * 8;
	w0 = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	w1 = (uint32_t)p[4] | ((uint32_t)p[5] << 8) | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
	return true;
}

static void QuantPos(uint32_t w1, float q[3])
{
	q[0] = (float)(w1 & 1023u);
	q[1] = (float)((w1 >> 10) & 1023u);
	q[2] = (float)((w1 >> 20) & 4095u);
}

void GhbFile::NodeMatrix(const GhbNode &n, uint32_t frame, Matrix4 &out) const
{
	FramePair fp;
	fp.a = fp.b = frame;
	fp.w = 1.0f;
	NodeMatrixPose(n, fp, out);
}

void GhbFile::NodeMatrixPose(const GhbNode &n, const FramePair &fp, Matrix4 &out) const
{
	if (n.ncomp)
	{
		uint32_t a0, a1, b0, b1;
		NodeWords(n, fp.a, a0, a1);
		NodeWords(n, fp.b, b0, b1);
		float ra[3][3], rb[3][3], qa[3], qb[3];
		DecodePackedRotation(a0, ra);
		QuantPos(a1, qa);
		float w = fp.w, iw = 1.0f - fp.w;
		if (fp.a == fp.b)
		{
			std::memcpy(rb, ra, sizeof(ra));
			qb[0] = qa[0], qb[1] = qa[1], qb[2] = qa[2];
		}
		else
		{
			DecodePackedRotation(b0, rb);
			QuantPos(b1, qb);
		}
		for (int r = 0; r < 3; r++)
		{
			for (int c = 0; c < 3; c++)
				out.m[r][c] = rb[r][c] + (ra[r][c] - rb[r][c]) * w;
			out.m[r][3] = 0.0f;
		}
		for (int a = 0; a < 3; a++)
			out.m[3][a] = n.qofs[a] + (qb[a] * iw + qa[a] * w) * n.qinv[a];
		out.m[3][3] = 1.0f;
		MatUpdateFlags(out);
		return;
	}
	if (n.mat.v.empty())
	{
		MatIdentity(out);
		return;
	}
	if (n.mat.constant || n.mat.v.size() <= 1)
	{
		out = n.mat.v[0];
		MatUpdateFlags(out);
		return;
	}
	const Matrix4 &A = n.mat.At(fp.a), &B = n.mat.At(fp.b);
	if ((A.flags & 1) && (B.flags & 1))
	{
		MatIdentity(out);
		return;
	}
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			out.m[r][c] = B.m[r][c] + (A.m[r][c] - B.m[r][c]) * fp.w;
	MatUpdateFlags(out);
}

} // namespace ghoul
