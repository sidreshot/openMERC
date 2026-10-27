// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - internal classes behind the interfaces (not for other modules).
// Implements: SPEC-010 (system roles, ids, objects), SPEC-011 (instances), SPEC-012 (runtime state, caches),
//             SPEC-013 (network/save state)
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ghb_file.h"
#include "ghoul_api.h"
#include "ghoul_common.h"
#include "ghoul_math.h"

namespace ghoul {

class System;
class Object;
class Inst;
struct ImageList;
struct Texture;

constexpr int kMaxGhoulId = 32766;
extern const float kSpeedMultipliers[13];
float SpeedMultiplier(int index);

// ------------------------------------------------------------------ shared state (all three systems)

struct GserverSettings
{
	float sizeMatters = 1.0f;
	float lightSmooth = 1.0f;
	float lightAmbient = 0.6f; // SPEC-AMEND-002 item 5.1
	bool test = false;
	bool precacheAll = false;
	bool autoPrep = false;
	bool cacheGhb = true;
};

struct Stats
{
	int textures = 0, textureBytes = 0, meshes = 0, triangles = 0, trianglesTotal = 0;
	uint32_t renderFrame = 1;
};

// Model-file cache (SPEC-012): key = path upper-cased, '/', extension removed
class FileCache
{
public:
	GhbFile *Acquire(const std::string &ghbPath); // load or reuse, count+1; NULL on failure (no entry)
	void Release(GhbFile *f);                     // count-1; freed at 0 unless CacheGHB
	void PurgeUnreferenced();
	void DeleteAll(); // gserver RELOAD
	const std::map<std::string, std::unique_ptr<GhbFile>> &Files() const { return files_; }

private:
	std::map<std::string, std::unique_ptr<GhbFile>> files_;
};

// game import #62 buffer cache (missing files cached too)
struct TextBuffer
{
	int length = -1;
	std::vector<char> data;
};

struct PlayerModelText
{
	std::vector<char> data;
};

struct Shared
{
	FileCache files;
	std::map<std::string, TextBuffer> texts;             // import #62
	std::map<std::string, PlayerModelText> playerTexts;  // .gpm/.gbm (missing not cached)
	std::map<std::string, std::unique_ptr<ImageList>> imageLists;
	std::map<std::string, std::unique_ptr<Texture>> textures;
	const GhoulGLTable *gl = nullptr;
	unsigned char gamma[256];
	int textureDetail = 0;
	float polyFactor = -0.1f, polyUnits = -0.1f;
	bool specular = false;
	GserverSettings gs;
	Stats stats;
	void (*fullPath)(char *out, const char *path) = nullptr;
	Shared();
};
Shared &G();
System *GetSystemObject(int role);

// ------------------------------------------------------------------ string table (SPEC-013 section 1)

class StringTable
{
public:
	StringTable() { Clear(); }
	int Add(const std::string &s); // existing -> its index
	int Find(const std::string &s) const; // -1 when absent
	const std::string &Get(int i) const;
	int Count() const { return (int)strings_.size(); }
	void Clear();

private:
	std::vector<std::string> strings_;
	std::map<std::string, int> index_;
};

// ------------------------------------------------------------------ id allocator (SPEC-010)

class IdAllocator
{
public:
	int Allocate(); // lowest free id >= 1; fatal beyond 32766
	void Free(int id);
	void Clear() { used_.clear(); }
	bool Used(int id) const { return used_.count(id) != 0; }

private:
	std::set<int> used_;
};

// ------------------------------------------------------------------ image lists and textures (ghoul_texture.cpp)

struct Texture
{
	std::string name; // full game path without extension, upper case ("GHOUL/COMSKIN/DEFAULT")
	bool decoded = false, missing = false;
	int width = 0, height = 0;
	bool hasAlpha = false, nomip = false;
	std::vector<uint8_t> rgba; // level 0 after gamma
	uint32_t glName = 0;
	bool uploaded = false;
	int refs = 0;
	uint32_t statFrame = 0;
	bool usedSincePurge = false;
};

struct ImageList
{
	std::string key;   // directory + name, upper case, no extension
	std::string dir;   // directory the images are relative to
	bool found = false;
	std::vector<std::string> images;  // image names (upper, no extension)
	std::vector<Texture *> textures;  // resolved lazily (same size as images)
	int refs = 0;
};

// texture lookup for a slot texture name in a model directory (SPEC-012 section 8); NULL = untextured
ImageList *AcquireImageList(const std::string &modelDir, const std::string &name);
void ReleaseImageList(ImageList *l);
int ImageListAddCustom(ImageList *l, const std::string &upperName); // CUSTOM/ entries
Texture *ListTexture(ImageList *l, int index, bool load);      // clamped index; load = decode now
bool BindTexture(Texture *t, bool tileU, bool tileV);          // uploads when needed
void ReleaseShadowTextures(const GhoulGLTable *g);                // ghoul_draw.cpp, from ReleaseAllTextures
void ReleaseAllTextures();                                      // system method 3
void PurgeImages();
bool DecodeTGA(const uint8_t *d, size_t n, const std::string &name, Texture &out); // SPEC-012 TGA rules

// ------------------------------------------------------------------ model object (ghoul_object.cpp)

struct SeqEntry
{
	std::string path; // normalised (upper, '/')
	GhbFile *file = nullptr;
	int index = -1;
	bool placeholder = false;
};

struct PartEntry
{
	std::string name;
	int type = 0;
	bool known = false;
};

struct MaterialEntry
{
	std::string name;
	bool known = false;
	ImageList *list = nullptr; // image list of slot 0 of the first file that has the material
	bool listLooked = false;
};

struct SkinEntry
{
	std::string name;
	int material = 0;
	int image = -1; // index in the material's image list, -1 = unknown
};

// per loaded file: id maps
struct FileMap
{
	GhbFile *file = nullptr;
	size_t partsMapped = 0, matsMapped = 0;
	std::vector<int> partNode;  // part id -> node index (-1), index 0 unused
	std::vector<int> nodePart;  // node index -> lowest part id (0)
	std::vector<int> matIndex;  // material id -> file material index (-1)
	std::vector<int> fileMatId; // file material index -> lowest material id (0)
};

class Object : public IGhoulObject
{
public:
	Object(System *sys, int id, bool received);
	~Object();

	// IGhoulObject
	GhoulID RegisterSequence(const char *path, bool alsoOthers) override;
	void FinishRegistration(bool allSkins) override;
	void Prepare(bool specular) override;
	GhoulID FindSequence(const char *path) override;
	unsigned short NumSequences() override;
	void GetSequencePath(GhoulID seq, char *out) override;
	GhoulID RegisterPart(const char *name) override;
	GhoulID FindPart(const char *name) override;
	unsigned short NumParts() override;
	int PartType(GhoulID part) override;
	void GetPartName(GhoulID part, char *out) override;
	GhoulID RegisterNote(const char *name) override;
	GhoulID FindNote(const char *name) override;
	unsigned short NumNotes() override;
	void EnumerateNotes(IGhoulCallback *cb, GhoulID note) override;
	GhoulID RegisterMaterial(const char *name) override;
	GhoulID FindMaterial(const char *name) override;
	unsigned short NumMaterials() override;
	void GetMaterialName(GhoulID material, char *out) override;
	GhoulID RegisterSkin(GhoulID material, const char *name, int ignored) override;
	GhoulID FindSkin(GhoulID material, const char *name, int ignored) override;
	unsigned short NumSkins() override;
	void GetSkinName(GhoulID skin, char *out) override;
	GhoulID SkinMaterial(GhoulID skin) override;
	IGhoulInst *CreateInstance() override;
	unsigned short GetObjectId() override;
	IGhoulSystem *GetSystem() override;
	void Destroy() override;
	void DestroyAllInstances() override;

	// internal
	System *sys;
	int id;        // positive (own) or negated server id (received)
	bool received; // created from server data (placeholder sequences consume ids)
	bool character = false, hasLights = false, prepared = false, specular = false;
	std::vector<GhbFile *> files; // load order, each acquired once
	std::vector<FileMap> maps;
	std::vector<SeqEntry> seqs;   // id - 1
	std::vector<PartEntry> parts;
	std::vector<std::string> notes;
	std::map<std::string, int> noteMap; // live
	std::vector<MaterialEntry> mats;
	std::vector<SkinEntry> skins;
	std::map<int, Inst *> insts; // by id
	// frozen lookup tables (SPEC-012: built on first Find, never rebuilt)
	bool seqTableBuilt = false, partTableBuilt = false, matTableBuilt = false, skinTableBuilt = false;
	std::map<std::string, int> seqTable, partTable, matTable;
	std::map<std::pair<int, std::string>, int> skinTable;
	// network (server): baseline state snapshot (SPEC-013 section 2)
	bool baselineTaken = false;
	struct NetState *baseline = nullptr;

	FileMap &Map(GhbFile *f);
	int NodeOf(GhbFile *f, int part);       // -1 when the part is not in that file
	int PartOfNode(GhbFile *f, int node);   // 0 when none
	int MaterialOf(GhbFile *f, int fileMat); // object material id, 0 when none
	int SkinImage(int skin) const;
	ImageList *MaterialList(int material);
	std::map<std::string, ImageList *> slotLists; // other slot lists (acquired once)
	ImageList *SlotList(GhbFile *f, int fileMat, int slot);
	GhbFile *AddFile(GhbFile *f); // acquire + register in load order (already acquired by caller)
	void TakeBaseline();
	void DestroyNow(bool notify);
	const SeqEntry *Seq(int id) const { return id >= 1 && id <= (int)seqs.size() ? &seqs[id - 1] : nullptr; }
};

// ------------------------------------------------------------------ instance state shared by net/save

// The replicated part of an instance (SPEC-013 section 3)
struct NetState
{
	bool on = true;
	int speed = 4;
	bool paused = true;
	bool reverse = false;
	int attachBolt = 0;
	int seq = 0;
	int mode = 0;
	float A = 0.0f, S = 0.0f, pauseEnd = 0.0f;
	uint8_t tint[4] = {255, 255, 255, 255};
	Matrix4 xf;
	std::vector<uint8_t> parts;           // one byte per object part
	std::map<int, int> boltons;           // child instance id -> own bolt
	std::map<std::pair<int, int>, int> skins; // (material, part) -> skin
	NetState() { MatIdentity(xf); }
};

struct NoteCb
{
	IGhoulCallback *cb;
	int note;
};
struct MatrixCb
{
	IGhoulCallback *cb;
	int bolt;
	int mode;
};

// per-instance render caches (SPEC-012 section 8, SPEC-411 section 6)
struct LightState
{
	const LightRecord *key = nullptr; // world-light identity (pointer into the system's set)
	LightRecord light;
	int phase = 0; // 0 fading in, 1 on, 2 fading out
	float start = 0;
	bool seen = false, contributed = false, fresh = false;
};
struct RenderCache
{
	bool valid = false;
	float lastDraw = -1e30f;
	Vec3 lightColor, lightDir; // smoothed (directional vector valid)
	bool smoothInit = false;
	Vec3 probeS, probeV;
	Vec3 shadowDir = Vec3(0, 0, 1);
	std::vector<LightState> lights;
	int lastImage[64];
	void Reset();
	RenderCache() { Reset(); }
};

// Pose of an instance at a time (SPEC-012 sections 3-4)
struct Pose
{
	bool valid = false; // has a sequence, is on, file present
	GhbFile *file = nullptr;
	const GhbSequence *seq = nullptr;
	float f = 0;        // fraction
	FramePair fp;       // absolute frames
	uint32_t dominant = 0;
	bool adjacent = false; // A and B adjacent frames of one sequence (UV-matrix blending)
};

class Inst : public IGhoulInst
{
public:
	Inst(System *sys, Object *obj, int id);
	~Inst();

	// IGhoulInst
	void SetTransform(const Matrix4 *m) override;
	void SetTransform16(const float *m16) override;
	void GetTransform(Matrix4 *out) override;
	void GetTransform16(float *out16) override;
	void SetOnOff(bool on, float time) override;
	bool IsOn() override;
	void SetSpeed(int speedIndex) override;
	int GetSpeed() override;
	void SetPartOnOff(GhoulID part, bool shown) override;
	bool IsPartOn(GhoulID part) override;
	void SetAllPartsOnOff(bool shown) override;
	void SetSkin(GhoulID material, GhoulID skin, GhoulID part) override;
	GhoulID GetSkin(GhoulID material, GhoulID part, int ignored) override;
	void RemoveSkin(GhoulID material, GhoulID part, int ignored) override;
	void SetTint(float r, float g, float b, float a) override;
	void GetTint(float *r, float *g, float *b, float *a) override;
	void SetTintAll(float r, float g, float b, float a) override;
	void Play(GhoulID seq, float time, float offset, bool restart, int mode, bool matchPhase, bool reverse) override;
	GhoulID GetPlayingSequence() override;
	void Pause(float time) override;
	void Resume(float time) override;
	void SetUserValue(void *value) override;
	void *GetUserValue() override;
	void AddNoteCallback(IGhoulCallback *cb, GhoulID note) override;
	void RemoveNoteCallback(IGhoulCallback *cb, GhoulID note) override;
	void AddMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int mode) override;
	void RemoveMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int mode) override;
	float SequenceDuration(GhoulID seq, int mode, float *frameTime) override;
	void AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt) override;
	void SetAttachBolt(GhoulID bolt) override;
	GhoulID GetAttachBolt() override;
	void DetachChild(IGhoulInst *child) override;
	int NumChildren() override;
	IGhoulInst *GetChild(int index, GhoulID *myBolt) override;
	IGhoulObject *GetGhoulObject() override;
	IGhoulInst *GetParent() override;
	IGhoulInst *Clone(bool withChildren) override;
	int SaveState(void *buffer, int capacity, IGhoulCallback **table, int count, const char *modelName) override;
	GhoulID LoadState(const void *buffer, int length, IGhoulCallback **table, int count, const char *modelName) override;
	GhoulID GetInstanceId() override;
	GhoulID GetObjectId() override;
	void Draw(GhoulRenderParams *params) override;
	void Reserved42(int a, int b, int c) override;
	int TraceRay(float time, const float *start, const float *dir, HitRecord *hits, int maxHits) override;
	int TraceBox(float time, const Matrix4 *root, const float *start, const float *end, const float *mins,
		const float *maxs, HitRecord *hits, int maxHits) override;
	void ProcessEvents(float time) override;
	void StopEvents() override;
	void StopMatrixEvents() override;
	void StartNoteEvents(float time) override;
	void StartEvents(float time) override;
	void GetBoltMatrix(float time, Matrix4 *out, GhoulID bolt, int matrixMode, bool throughParents) override;
	void GetBounds(float time, const Matrix4 *root, float *mins, float *maxs, GhoulID part, bool exact) override;
	GhoulID NetOffer(float priority) override;
	void Destroy() override;
	GhoulID GetPlayingSequence2() override;

	// ---- state
	System *sys;
	Object *obj;
	int id;
	Matrix4 xf, inv;
	int detSign = 1;
	float scale = 1.0f;
	bool on = true;
	int speed = 4;
	std::vector<uint8_t> partFlags; // part id - 1; missing entries = on
	std::map<std::pair<int, int>, int> skinOverrides;
	uint8_t tint[4] = {255, 255, 255, 255};
	int seq = 0, mode = 0;
	float S = 0, A = 0, pauseEnd = 0;
	bool paused = true, reverse = false;
	float D = 0, F = 0;
	int N = 0;
	void *user = nullptr;
	bool noteOn = false, matrixOn = false;
	float cursorTime = 0, cursorPhase = 0;
	std::vector<NoteCb> noteCbs;
	std::vector<MatrixCb> matrixCbs;
	Inst *parent = nullptr;
	int attachBolt = 0;
	struct Child
	{
		Inst *inst;
		int bolt;
	};
	std::vector<Child> children;
	RenderCache rc;
	bool hasModelLightsFlag = false;

	// ---- runtime helpers (ghoul_pose.cpp)
	void UpdateTransformDerived();
	void RecomputeTiming();
	float Fraction(float t) const;
	float RawPhase(float t) const;
	bool EvalPose(float t, Pose &p) const;
	bool PoseAtFraction(float f, Pose &p) const;
	bool BoltModel(const Pose &p, int bolt, Matrix4 &out) const; // false when the bolt is not in the file
	Matrix4 XF(const Pose &p) const;                              // section 6
	void BoltMatrix(float t, Matrix4 &out, int bolt, int mode, bool throughParents) const;
	Matrix4 ChildRoot(float t, const Matrix4 &myR, int myBolt) const; // root of a child hanging on myBolt
	bool PartShown(int part) const;
	void RestartEvents(float t, bool notes, bool matrices, bool fromAux);
	void CollectEvents(float t, std::vector<struct QueuedEvent> &q, const Matrix4 *root);
	void SetPartFlagsSize();
	NetState Capture() const;
	void Apply(const NetState &s, bool keepChildren);
	void DetachFromParent();
	bool IsCharacter() const { return obj->character; }

	// network (server side, SPEC-013 section 4 with SPEC-AMEND-005 I3.1): stored states are per instance (shared by
	// all clients, at most 20), the acknowledged frame is per instance and client
	struct ClientNet
	{
		int ack = -1; // acknowledged frame of this instance for this client, -1 = none
	};
	std::map<int, ClientNet> net;        // per client
	std::map<int, NetState> netStates;   // frame -> state written in that frame (at most 20)
};

struct QueuedEvent
{
	IGhoulCallback *cb;
	void *user;
	float time;
	std::string text;
	bool isMatrix;
	Matrix4 m;
};

// ------------------------------------------------------------------ system (ghoul_system.cpp)

struct WorldLightGrid
{
	std::vector<LightRecord> lights;
	bool built = false;
	Vec3 mn, size;
	std::vector<std::vector<int>> cells; // 32^3
	void Clear();
	void Build();
	void Query(const Vec3 &p, std::vector<const LightRecord *> &out) const;
};

class System : public IGhoulSystem
{
public:
	explicit System(int role);
	~System();

	// IGhoulSystem
	IGhoulObject *CreateObject() override;
	void SetGLTable(const GhoulGLTable *table) override;
	void SetSpecular(bool specular) override;
	void ReleaseTextures() override;
	void BeginFrame() override;
	void EndFrame() override;
	void DestroyAllObjects() override;
	void Purge() override;
	void Reset() override;
	IGhoulInst *FindInstance(int handle) override;
	IGhoulObject *FindObject(int handle) override;
	void SetFullPathCallback(void (*fn)(char *out, const char *path)) override;
	void NetClientConnected(int client) override;
	void NetClientDropped(int client) override;
	void NetResendAll() override;
	void NetDropAll() override;
	bool NetWriteReliable(int client, int frame, om_ghoul_bits_t *msg) override;
	bool NetReliablePending(int client) override;
	bool NetWriteUnreliable(int client, int frame, om_ghoul_bits_t *msg, float time) override;
	void NetAcknowledge(int client, int frame) override;
	void NetCommitFrame(int client, int frame) override;
	void NetBeginFrame(int client, int frame) override;
	void TouchFile(const char *path) override;
	void ClientStartAccepting() override;
	void ClientReadReliable(int unused, om_ghoul_bits_t *msg, bool specular) override;
	void ClientReadUnreliable(int frame, om_ghoul_bits_t *msg, float time) override;
	int ClientReliableSize() override;
	void *ClientReliableData() override;
	int ModelLightCount() override;
	void GetModelLight(int index, LightRecord *out) override;
	void ClearLights() override;
	void AddDynamicLight(const LightRecord *light) override;
	void EndRenderFrame() override;
	void FlushMeshCache() override;
	void AddWorldLight(const LightRecord *light) override;
	void FinishWorldLights() override;
	void BeginWorldLights() override;
	void SetGamma(const unsigned char *table256) override;
	void SetTextureDetail(int level) override;
	void SetPolygonOffset(float factor, float units) override;
	int StatTextures() override;
	int StatTextureBytes() override;
	int StatMeshes() override;
	int StatTriangles() override;
	int StatTrianglesTotal() override;
	int StatVertices() override;
	int StatCorners() override;
	int StatLights() override;
	void ResetStats() override;

	// internal
	int role;
	std::map<int, Object *> objects;
	std::map<int, Inst *> insts;
	IdAllocator objIds, instIds;
	StringTable strings;
	std::vector<LightRecord> modelLights, dynLights;
	WorldLightGrid world;

	Object *NewObject(int id, bool received);
	Inst *NewInst(Object *o, int forcedId);
	void ObjectDestroyed(Object *o, bool notify);
	void InstDestroyed(Inst *i);
	bool IsServer() const { return role == 0; }

	// ---- network (ghoul_net.cpp)
	struct NetData *net = nullptr;
	void NetObjectCreated(Object *o);
	void NetObjectRemoved(Object *o);
	void NetInstRemoved(Inst *i);
	GhoulID NetOfferInst(Inst *i, float priority);
};

// axis-aligned box accumulator
struct Box3
{
	bool any = false;
	Vec3 mn, mx;
	void Add(const Vec3 &p)
	{
		if (!any)
		{
			mn = mx = p;
			any = true;
			return;
		}
		for (int a = 0; a < 3; a++)
		{
			if (p[a] < mn[a])
				mn[a] = p[a];
			if (p[a] > mx[a])
				mx[a] = p[a];
		}
	}
	void AddBox(const BoundsRec &b, const Matrix4 &m)
	{
		if (b.empty)
			return;
		Vec3 lo = b.mn, hi = b.mx;
		if (b.infinite)
		{
			lo = Vec3(-1e30f, -1e30f, -1e30f);
			hi = Vec3(1e30f, 1e30f, 1e30f);
		}
		for (int i = 0; i < 8; i++)
			Add(XformPoint(Vec3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z), m));
	}
};

// posed animated elements of a file at a frame pair (SPEC-012 section 4)
void EvalGeometry(const GhbFile &f, const FramePair &fp, std::vector<float> &apos, std::vector<float> &anorm,
	std::vector<float> &atex);

// network test hooks and reset (ghoul_net.cpp)
void NetResetData(System *s);
void NetWriteStateForTest(om_ghoul_bits_t *bits, const NetState &cur, const NetState &base, float frameTime, int w0,
	int w1, int w2, int w3);
void NetReadStateForTest(om_ghoul_bits_t *bits, NetState &s, float frameTime, int w0, int w1, int w2, int w3);

// helpers
std::string BaseName(const std::string &normPath); // file name without directory and extension
void SetString(char *out, const std::string &s);

// network/save string coding (ghoul_bits.cpp, SPEC-201 B.3)
class Bits
{
public:
	explicit Bits(om_ghoul_bits_t *b) : b_(b) {}
	void Write(uint32_t v, int n);
	uint32_t Read(int n);
	void WriteId(uint32_t v);
	uint32_t ReadId();
	void WriteTime(int32_t v);
	int32_t ReadTime();
	void WriteFrameOffset(uint32_t v);
	uint32_t ReadFrameOffset();
	void WriteFloat(float f);
	float ReadFloat();
	void WriteString(const std::string &s);
	std::string ReadString();
	int Pos() const { return b_->bit; }
	void Seek(int bit) { b_->bit = bit; }
	bool Overflowed() const { return b_->overflowed != 0; }
	void ClearOverflow() { b_->overflowed = 0; }
	om_ghoul_bits_t *raw() { return b_; }

private:
	om_ghoul_bits_t *b_;
};
int BitsFor(int count); // smallest b >= 0 with count < 2^b

} // namespace ghoul
