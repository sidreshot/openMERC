// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - the .ghb model file in memory and its decoders.
// Implements: SPEC-120 (container layout), SPEC-014 (engine reading of every field: header rules,
//             tracks, materials, slots, bounds, node/camera/mesh/light records, geometry header,
//             per-sequence animated data, chunked vertex streams, normal keys, packed rotations,
//             direction table), SPEC-012 (decoded-chunk cache)
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ghoul_common.h"
#include "ghoul_math.h"

namespace ghoul {

constexpr uint32_t kGhbMagic = 0x198237FE;
constexpr uint32_t kGhbTag = 0x033FB1A3;
constexpr uint32_t kGhbVersion = 74;
constexpr uint32_t kMarkNulls = 0x05DC159C;
constexpr uint32_t kMarkCameras = 0x05DC159D;
constexpr uint32_t kMarkMeshes = 0x05DC159E;
constexpr uint32_t kMarkLights = 0x05DC159F;
constexpr uint32_t kMaxGhbString = 100000;

// TRACK (SPEC-014 section 1): constant -> one value, else one value per absolute model frame
template <class T> struct Track
{
	bool constant = true;
	std::vector<T> v; // 1 value when constant
	const T &At(uint32_t frame) const
	{
		if (constant || v.size() <= 1)
			return v[0];
		return v[frame < v.size() ? frame : (uint32_t)v.size() - 1];
	}
	bool ConstValue(T &out) const
	{
		if (!constant)
			return false;
		out = v[0];
		return true;
	}
};

// bool track: constant byte, or one bit per frame (LSB first in u32 words)
struct BoolTrack
{
	bool constant = true;
	bool value = true;
	uint32_t n = 0;
	std::vector<uint32_t> words;
	bool At(uint32_t frame) const
	{
		if (constant)
			return value;
		if (n == 0)
			return false;
		if (frame >= n)
			frame = n - 1;
		return ((words[frame >> 5] >> (frame & 31)) & 1u) != 0;
	}
};

// SPEC-014 section 6: bounds record (u8 infinite, u8 empty, min, max)
struct BoundsRec
{
	bool infinite = false;
	bool empty = false;
	Vec3 mn, mx;
};

struct GhbEvent
{
	float timeMs = 0;
	int32_t reserved = -1; // always -1 (ignored)
	std::string type;      // as stored (lower case)
	std::string typeUpper; // note name
	std::string arg;
};

struct GhbSequence
{
	float reserved[3] = {0, 0, 0};
	float frameMs = 0;
	std::string name;      // as stored
	std::string nameUpper;
	std::vector<GhbEvent> events;      // file order
	std::vector<uint32_t> keysByTime;  // event indexes sorted by time (stable)
	uint32_t first = 0, count = 0;
	// non-chunked per-sequence blocks (SPEC-014 section 9): element offset of this sequence's block
	size_t aposBlock = 0, atexBlock = 0, anormBlock = 0;
};

struct GhbSlot
{
	std::string texture;
	std::string reserved;
	bool hasTexture = false, environment = false, tileU = false, tileV = false;
	Track<uint8_t> image; // image index per frame (dominant frame, no blending)
	Track<Matrix4> uv;    // UV transform (texture matrix)
	bool staticUV = false;
};

struct GhbMaterial
{
	std::string name; // upper-cased on load
	bool twoSided = false, alphaTexture = false;
	uint32_t blend = 0; // 0 normal, 1 additive (any non-zero: not opaque)
	GhbSlot slots[2];   // 0 base texture, 1 specular map
	Track<Vec3> diffuse, specular;
	Track<float> gloss, specLevel, selfIllum, opacity;
	// classification at load (SPEC-014 section 4)
	bool opaque = true, specularCapable = false, textured = false;
};

enum GhbNodeKind
{
	kNodeNull = 0,
	kNodeMesh = 1,
	kNodeCamera = 2,
	kNodeLight = 3
};

struct GhbNode
{
	GhbNodeKind kind = kNodeNull;
	std::string name; // as stored
	std::string nameUpper;
	std::string user;
	BoolTrack visible;
	bool sprite = false, spriteball = false, noblock = false;
	BoundsRec bounds; // node position range over all frames
	// transform: ncomp = 0 -> MAT track, else compressed (ncomp frames of two words in part B)
	uint32_t ncomp = 0;
	Track<Matrix4> mat;
	float qscale[3] = {0, 0, 0}, qinv[3] = {0, 0, 0}, qofs[3] = {0, 0, 0};
	uint32_t dataOffset = 0;
	// mesh
	BoundsRec meshBounds;
	int meshIndex = -1; // index of the MESH record (surfaces refer to it)
	// camera: position, direction, up, right (UNVERIFIED order, no stock data)
	Track<Vec3> camera[4];
	// light (SPEC-014 section 7)
	bool world = false;
	uint32_t lightKind = 0;
	bool lightParsed = false; // kind 0 tracks present
	Track<Vec3> lightPos, lightColor;
	Track<float> brightness, multiplier, attStart, attEnd;
};

struct GhbChunk
{
	uint32_t first = 0, count = 0;
	float scale[3] = {0, 0, 0}, offset[3] = {0, 0, 0};
	uint8_t bits[3] = {0, 0, 0};
	uint32_t posOffset = 0, normOffset = 0; // relative to nodeanim_size in part B
	uint32_t normEnd = 0;                   // next chunk's pos offset or vanim_size
};

struct GhbSurface
{
	int mesh = 0;              // index of the MESH record
	int node = -1;             // node index of that mesh (-1 when out of range)
	int firstIndex = 0, numIndex = 0;
	uint32_t material = 0;     // from the material groups
	std::vector<uint16_t> tris; // corner index triples
};

struct GhbGeometry
{
	int32_t firstWord = -1;
	uint32_t revision = 0;
	uint32_t meshCount = 0, materialCount = 0;
	uint32_t nPos = 0, nUv = 0, nNorm = 0, nSurf = 0, nIdx = 0, nCorner = 0, nApos = 0, nAtex = 0, nAnorm = 0;
	uint32_t offsets[10] = {};
	BoundsRec bounds;
	float vscale[3] = {0, 0, 0}, vinv[3] = {0, 0, 0}, center[3] = {0, 0, 0}, atexScale[3] = {0, 0, 0};
	bool chunked = false;
	uint32_t nApos2 = 0, nAnorm2 = 0;
	std::vector<uint32_t> frameChunk;
	uint32_t vanimSize = 0, nodeanimSize = 0;
	std::vector<GhbChunk> chunks;
	uint32_t staticSize = 0, staticOffset = 0;

	// decoded static block
	std::vector<uint32_t> groups; // 3 per material group: material, first surface, count
	std::vector<float> pos;       // 3 per static position
	std::vector<float> uv;        // 2 per uv
	std::vector<float> norm;      // 3 per static normal
	std::vector<GhbSurface> surfaces;
	std::vector<int16_t> corners; // 3 per corner: position, normal, uv
	// non-chunked per-frame data: pointers into the file (part B)
	const uint8_t *apos = nullptr, *atex = nullptr, *anorm = nullptr;
};

// Pose = frame pair (absolute frames A, B) with weight w of A (SPEC-012 section 4)
struct FramePair
{
	uint32_t a = 0, b = 0;
	float w = 1.0f;
	uint32_t Dominant() const { return w >= 0.5f ? a : b; }
};

// One loaded .ghb file
class GhbFile
{
public:
	std::string path;     // game path it was loaded from
	std::string cacheKey; // upper-case, '/', extension removed
	std::string source;   // exporter source path (display only)
	std::string dir;      // model directory, upper-case with trailing '/', e.g. "GHOUL/ENEMY/MESO/"
	uint32_t sizeA = 0, sizeB = 0;
	bool thunk = false;
	uint32_t frames = 0;
	std::vector<GhbSequence> sequences;
	std::vector<GhbMaterial> materials;
	BoundsRec bounds;
	std::vector<GhbNode> nodes; // nulls, cameras, meshes, lights in file order
	std::vector<int> meshes;    // mesh record index -> node index
	std::vector<int> lights;    // light record index -> node index
	bool hasGeometry = false;
	GhbGeometry geo;
	std::vector<uint32_t> frameSequence; // sequence index of every model frame
	uint32_t serial = 0;                 // unique id for the decoded-chunk cache
	int refCount = 0;                    // model-file cache references (SPEC-012)
	std::vector<uint8_t> usedNodes;      // "evaluated since load" (gserver USAGE)

	GhbFile();
	~GhbFile();
	GhbFile(const GhbFile &) = delete;
	GhbFile &operator=(const GhbFile &) = delete;

	// Load from the host file system. Returns false on a non-fatal failure (missing, old file,
	// wrong version, truncated); fatal errors (tag, huge string) go through Fatal().
	bool Load(const std::string &gamePath);
	// Parse a buffer (takes a copy). 'error' receives the reason of a non-fatal failure.
	bool ParseMemory(const uint8_t *bytes, size_t size, const std::string &gamePath, std::string *error);

	int FindNode(const std::string &upperName) const; // -1 when absent
	int FindSequence(const std::string &upperName) const;
	int FindMaterial(const std::string &upperName) const;
	bool HasLights() const { return !lights.empty(); }

	const uint8_t *PartB() const { return bytes_.data() + sizeA; }

	// ---- decoding at one absolute frame
	// Model-space positions / normals / texture coordinates of the animated elements
	void AnimPositions(uint32_t frame, float *out) const; // nApos * 3
	void AnimNormals(uint32_t frame, float *out) const;   // nAnorm * 3
	void AnimTexCoords(uint32_t frame, float *out) const; // nAtex * 2
	// Pointer to cached decoded positions/normals of a chunked frame (valid until the next cache use)
	// Node matrix at one frame / blended frame pair (SPEC-014 behaviour), model space
	void NodeMatrix(const GhbNode &n, uint32_t frame, Matrix4 &out) const;
	void NodeMatrixPose(const GhbNode &n, const FramePair &fp, Matrix4 &out) const;
	bool NodeWords(const GhbNode &n, uint32_t frame, uint32_t &w0, uint32_t &w1) const;

	size_t MemoryUse() const { return bytes_.size(); }

private:
	bool Parse(const std::string &gamePath, std::string *error);
	std::vector<uint8_t> bytes_;
};

// ---- stateless codecs (SPEC-014 sections 10, 12, 13)

// direction table entry (SPEC-014 section 13)
Vec3 Direction(int index);
// index of the table entry with the largest dot product (strictly greater wins, start value 0 at index 0)
int NearestDirection(const Vec3 &v);

// packed rotation word -> 3x3 (row-vector layout: m[r][c])
void DecodePackedRotation(uint32_t word, float out[3][3]);
// rotation part of a matrix (row-vector layout) -> packed rotation word (exporter / network encoder)
uint32_t EncodePackedRotation(const Matrix4 &m);

// Decode one position stream (SPEC-014 section 10). Bits beyond 'size' read as zero.
// Returns the number of bits consumed. out = nFrames * nApos * 3 floats, frame-major.
uint32_t DecodePositionStream(const uint8_t *data, size_t size, uint32_t nApos, uint32_t nFrames,
	const uint8_t bits[3], const float scale[3], const float offset[3], float *out);
// Reconstruct normals of one chunk from its key indexes. out = nFrames * nAnorm * 3, frame-major.
void DecodeNormalStream(const uint8_t *data, size_t size, uint32_t nAnorm, uint32_t nFrames, float *out);
inline uint32_t NormalKeysPerChunk(uint32_t nFrames) { return nFrames <= 1 ? 1 : (nFrames <= 4 ? 2 : 3); }

// Decoded vertex chunk: positions [frame][vertex][3], normals [frame][normal][3]
struct DecodedChunk
{
	uint32_t first = 0, count = 0, nApos = 0, nAnorm = 0;
	std::vector<float> pos;
	std::vector<float> norm;
	uint32_t posBitsUsed = 0;
	size_t Bytes() const { return 20 + (pos.size() + norm.size()) * sizeof(float); }
};

// Decode chunk 'ci' of a file (no caching)
bool DecodeChunk(const GhbFile &f, uint32_t ci, DecodedChunk &out);
// Cached access (shared 3 MiB FIFO cache, SPEC-012)
std::shared_ptr<const DecodedChunk> GetDecodedChunk(const GhbFile &f, uint32_t ci);
void FlushChunkCache();
size_t ChunkCacheBytes();
void ForgetChunksOf(uint32_t serial);

} // namespace ghoul
