// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the Ghoul C++ interfaces the game calls through the engine.
//
// Game-side copy of the declarations of engine/src/ghoul/ghoul_api.h (package I3), reduced to what the
// game uses (no GL table, no render parameters). Every interface is an abstract class whose virtual
// methods are in exactly the table order of the specs; with MSVC x86 the tables are binary compatible
// with the original engine (thiscall, callee pops, bool in AL, 16-bit ids in AX, float on x87).
// To be replaced by the shared header once one exists.
//
// Implements: SPEC-010 (A system, B object, D player model, E callback), SPEC-011 (C instance, matrix, hit record)
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4265)
#endif

namespace ghoul {

typedef unsigned short GhoulID; // 1-based, 0 = none (SPEC-010 conventions)

class IGhoulSystem;
class IGhoulObject;
class IGhoulInst;

// Engine bit buffer for the engine-only network methods 16, 18, 24, 25 (see ghoul_host.h)
typedef void GhoulBits;

// SPEC-011: 68-byte row-vector matrix. Rows 0-2 = images of X (forward), Y (left), Z (up), row 3 =
// translation; p' = p.x*row0 + p.y*row1 + p.z*row2 + row3. A*B applies A first.
struct Matrix4
{
	float m[4][4];
	uint32_t flags; // bit 0: identity
};
static_assert(sizeof(Matrix4) == 68, "SPEC-011 matrix is 68 bytes");

// SPEC-011: hit record (36 bytes in the 32-bit build)
struct HitRecord
{
	IGhoulInst *inst;
	uint16_t part;
	uint16_t pad;
	float distance;
	float normal[3];
	float point[3];
};

// SPEC-010: light record, 28 bytes
struct LightRecord
{
	float origin[3];
	float radius;
	float color[3];
};
static_assert(sizeof(LightRecord) == 28, "SPEC-010 light record is 28 bytes");

// SPEC-010 E: event callback, implemented by the game and the player module.
class IGhoulCallback
{
public:
	// data: note argument text (notes), 68-byte matrix (bolt events) or text (precache enumeration).
	virtual bool Event(uint32_t undefinedValue, void *userValue, float time, const void *data) = 0;
};

class GhoulRenderParams;
struct GhoulGLTable;

// ---------------------------------------------------------------------------------------------
// SPEC-010 B: model object, 29 methods
class IGhoulObject
{
public:
	virtual GhoulID RegisterSequence(const char *path, bool alsoOthers) = 0;             // 0
	virtual void FinishRegistration(bool allSkins) = 0;                                  // 1
	virtual void Prepare(bool specular) = 0;                                             // 2
	virtual GhoulID FindSequence(const char *path) = 0;                                  // 3
	virtual unsigned short NumSequences() = 0;                                           // 4
	virtual void GetSequencePath(GhoulID seq, char *out) = 0;                            // 5
	virtual GhoulID RegisterPart(const char *name) = 0;                                  // 6
	virtual GhoulID FindPart(const char *name) = 0;                                      // 7
	virtual unsigned short NumParts() = 0;                                               // 8
	virtual int PartType(GhoulID part) = 0;                                              // 9
	virtual void GetPartName(GhoulID part, char *out) = 0;                               // 10
	virtual GhoulID RegisterNote(const char *name) = 0;                                  // 11
	virtual GhoulID FindNote(const char *name) = 0;                                      // 12
	virtual unsigned short NumNotes() = 0;                                               // 13
	virtual void EnumerateNotes(IGhoulCallback *cb, GhoulID note) = 0;                   // 14
	virtual GhoulID RegisterMaterial(const char *name) = 0;                              // 15
	virtual GhoulID FindMaterial(const char *name) = 0;                                  // 16
	virtual unsigned short NumMaterials() = 0;                                           // 17
	virtual void GetMaterialName(GhoulID material, char *out) = 0;                       // 18
	virtual GhoulID RegisterSkin(GhoulID material, const char *name, int ignored) = 0;   // 19
	virtual GhoulID FindSkin(GhoulID material, const char *name, int ignored) = 0;       // 20
	virtual unsigned short NumSkins() = 0;                                               // 21
	virtual void GetSkinName(GhoulID skin, char *out) = 0;                               // 22
	virtual GhoulID SkinMaterial(GhoulID skin) = 0;                                      // 23
	virtual IGhoulInst *CreateInstance() = 0;                                            // 24
	virtual unsigned short GetObjectId() = 0;                                            // 25
	virtual IGhoulSystem *GetSystem() = 0;                                               // 26
	virtual void Destroy() = 0;                                                          // 27
	virtual void DestroyAllInstances() = 0;                                              // 28

protected:
	~IGhoulObject() {}
};

// ---------------------------------------------------------------------------------------------
// SPEC-011: model instance, 55 methods
class IGhoulInst
{
public:
	virtual void SetTransform(const Matrix4 *m) = 0;                                     // 0
	virtual void SetTransform16(const float *m16) = 0;                                   // 1
	virtual void GetTransform(Matrix4 *out) = 0;                                         // 2
	virtual void GetTransform16(float *out16) = 0;                                       // 3
	virtual void SetOnOff(bool on, float time) = 0;                                      // 4
	virtual bool IsOn() = 0;                                                             // 5
	virtual void SetSpeed(int speedIndex) = 0;                                           // 6
	virtual int GetSpeed() = 0;                                                          // 7
	virtual void SetPartOnOff(GhoulID part, bool shown) = 0;                             // 8
	virtual bool IsPartOn(GhoulID part) = 0;                                             // 9
	virtual void SetAllPartsOnOff(bool shown) = 0;                                       // 10
	virtual void SetSkin(GhoulID material, GhoulID skin, GhoulID part) = 0;              // 11
	virtual GhoulID GetSkin(GhoulID material, GhoulID part, int ignored) = 0;            // 12
	virtual void RemoveSkin(GhoulID material, GhoulID part, int ignored) = 0;            // 13
	virtual void SetTint(float r, float g, float b, float a) = 0;                        // 14
	virtual void GetTint(float *r, float *g, float *b, float *a) = 0;                    // 15
	virtual void SetTintAll(float r, float g, float b, float a) = 0;                     // 16
	virtual void Play(GhoulID seq, float time, float offset, bool restart, int mode, bool matchPhase,
		bool reverse) = 0;                                                               // 17
	virtual GhoulID GetPlayingSequence() = 0;                                            // 18
	virtual void Pause(float time) = 0;                                                  // 19
	virtual void Resume(float time) = 0;                                                 // 20
	virtual void SetUserValue(void *value) = 0;                                          // 21
	virtual void *GetUserValue() = 0;                                                    // 22
	virtual void AddNoteCallback(IGhoulCallback *cb, GhoulID note) = 0;                  // 23
	virtual void RemoveNoteCallback(IGhoulCallback *cb, GhoulID note) = 0;               // 24
	virtual void AddMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int mode) = 0;      // 25
	virtual void RemoveMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int mode) = 0;   // 26
	virtual float SequenceDuration(GhoulID seq, int mode, float *frameTime) = 0;         // 27
	virtual void AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt) = 0;  // 28
	virtual void SetAttachBolt(GhoulID bolt) = 0;                                        // 29
	virtual GhoulID GetAttachBolt() = 0;                                                 // 30
	virtual void DetachChild(IGhoulInst *child) = 0;                                     // 31
	virtual int NumChildren() = 0;                                                       // 32
	virtual IGhoulInst *GetChild(int index, GhoulID *myBolt) = 0;                        // 33
	virtual IGhoulObject *GetGhoulObject() = 0;                                          // 34
	virtual IGhoulInst *GetParent() = 0;                                                 // 35
	virtual IGhoulInst *Clone(bool withChildren) = 0;                                    // 36
	virtual int SaveState(void *buffer, int capacity, IGhoulCallback **table, int count,
		const char *modelName) = 0;                                                      // 37
	virtual GhoulID LoadState(const void *buffer, int length, IGhoulCallback **table, int count,
		const char *modelName) = 0;                                                      // 38
	virtual GhoulID GetInstanceId() = 0;                                                 // 39
	virtual GhoulID GetObjectId() = 0;                                                   // 40
	virtual void Draw(GhoulRenderParams *params) = 0;                                    // 41
	virtual void Reserved42(int a, int b, int c) = 0;                                    // 42
	virtual int TraceRay(float time, const float *start, const float *dir, HitRecord *hits,
		int maxHits) = 0;                                                                // 43
	virtual int TraceBox(float time, const Matrix4 *root, const float *start, const float *end,
		const float *mins, const float *maxs, HitRecord *hits, int maxHits) = 0;          // 44
	virtual void ProcessEvents(float time) = 0;                                          // 45
	virtual void StopEvents() = 0;                                                       // 46
	virtual void StopMatrixEvents() = 0;                                                 // 47
	virtual void StartNoteEvents(float time) = 0;                                        // 48
	virtual void StartEvents(float time) = 0;                                            // 49
	virtual void GetBoltMatrix(float time, Matrix4 *out, GhoulID bolt, int matrixMode,
		bool throughParents) = 0;                                                        // 50
	virtual void GetBounds(float time, const Matrix4 *root, float *mins, float *maxs, GhoulID part,
		bool exact) = 0;                                                                 // 51
	virtual GhoulID NetOffer(float priority) = 0;                                        // 52
	virtual void Destroy() = 0;                                                          // 53
	virtual GhoulID GetPlayingSequence2() = 0;                                           // 54

protected:
	~IGhoulInst() {}
};

// ---------------------------------------------------------------------------------------------
// SPEC-010 A: system, 49 methods
class IGhoulSystem
{
public:
	virtual IGhoulObject *CreateObject() = 0;                                            // 0
	virtual void SetGLTable(const GhoulGLTable *table) = 0;                              // 1
	virtual void SetSpecular(bool specular) = 0;                                         // 2
	virtual void ReleaseTextures() = 0;                                                  // 3
	virtual void BeginFrame() = 0;                                                       // 4
	virtual void EndFrame() = 0;                                                         // 5
	virtual void DestroyAllObjects() = 0;                                                // 6
	virtual void Purge() = 0;                                                            // 7
	virtual void Reset() = 0;                                                            // 8
	virtual IGhoulInst *FindInstance(int handle) = 0;                                    // 9
	virtual IGhoulObject *FindObject(int handle) = 0;                                    // 10
	virtual void SetFullPathCallback(void (*fn)(char *out, const char *path)) = 0;       // 11
	virtual void NetClientConnected(int client) = 0;                                     // 12
	virtual void NetClientDropped(int client) = 0;                                       // 13
	virtual void NetResendAll() = 0;                                                     // 14
	virtual void NetDropAll() = 0;                                                       // 15
	virtual bool NetWriteReliable(int client, int frame, void *msg) = 0;            // 16
	virtual bool NetReliablePending(int client) = 0;                                     // 17
	virtual bool NetWriteUnreliable(int client, int frame, void *msg, float time) = 0; // 18
	virtual void NetAcknowledge(int client, int frame) = 0;                              // 19
	virtual void NetCommitFrame(int client, int frame) = 0;                              // 20
	virtual void NetBeginFrame(int client, int frame) = 0;                               // 21
	virtual void TouchFile(const char *path) = 0;                                        // 22
	virtual void ClientStartAccepting() = 0;                                             // 23
	virtual void ClientReadReliable(int unused, void *msg, bool specular) = 0;      // 24
	virtual void ClientReadUnreliable(int frame, void *msg, float time) = 0;        // 25
	virtual int ClientReliableSize() = 0;                                                // 26
	virtual void *ClientReliableData() = 0;                                              // 27
	virtual int ModelLightCount() = 0;                                                   // 28
	virtual void GetModelLight(int index, LightRecord *out) = 0;                         // 29
	virtual void ClearLights() = 0;                                                      // 30
	virtual void AddDynamicLight(const LightRecord *light) = 0;                          // 31
	virtual void EndRenderFrame() = 0;                                                   // 32
	virtual void FlushMeshCache() = 0;                                                   // 33
	virtual void AddWorldLight(const LightRecord *light) = 0;                            // 34
	virtual void FinishWorldLights() = 0;                                                // 35
	virtual void BeginWorldLights() = 0;                                                 // 36
	virtual void SetGamma(const unsigned char *table256) = 0;                            // 37
	virtual void SetTextureDetail(int level) = 0;                                        // 38
	virtual void SetPolygonOffset(float factor, float units) = 0;                        // 39
	virtual int StatTextures() = 0;                                                      // 40
	virtual int StatTextureBytes() = 0;                                                  // 41
	virtual int StatMeshes() = 0;                                                        // 42
	virtual int StatTriangles() = 0;                                                     // 43
	virtual int StatTrianglesTotal() = 0;                                                // 44
	virtual int StatVertices() = 0;                                                      // 45
	virtual int StatCorners() = 0;                                                       // 46
	virtual int StatLights() = 0;                                                        // 47
	virtual void ResetStats() = 0;                                                       // 48

protected:
	~IGhoulSystem() {}
};

// ---------------------------------------------------------------------------------------------
// SPEC-010 D: player-model description, 19 methods. Entry layouts:
struct PlayerSkinEntry
{
	char material[50];
	char skin[100];
	char part[50]; // "" = whole model
};
struct PlayerBoltonEntry
{
	float scale;
	char bolt[50];
	char boltonBolt[50];
	char file[100];
};
struct PlayerPartEntry
{
	char part[50];
	uint8_t shown;
};
static_assert(sizeof(PlayerSkinEntry) == 200, "SPEC-010 D skin entry");
static_assert(sizeof(PlayerBoltonEntry) == 204, "SPEC-010 D bolt-on entry");
static_assert(sizeof(PlayerPartEntry) == 51, "SPEC-010 D part entry");

class IGhoulPlayerModel
{
public:
	virtual void Load(const char *file) = 0;                                             // 0
	virtual int GetUserValue() = 0;                                                      // 1
	virtual void SetUserValue(int value) = 0;                                            // 2
	virtual int NumSkins() = 0;                                                          // 3
	virtual int NumBoltons() = 0;                                                        // 4
	virtual int NumPartSwitches() = 0;                                                   // 5
	virtual const char *GetModelDir() = 0;                                               // 6
	virtual const char *GetMenuSequence() = 0;                                           // 7
	virtual const char *GetBaseModel() = 0;                                              // 8
	virtual const char *GetSequenceList() = 0;                                           // 9
	virtual const char *GetFacePrefix() = 0;                                             // 10
	virtual bool GetTrailer(char *out) = 0;                                              // 11
	virtual bool GetTeam(char *out) = 0;                                                 // 12
	virtual bool GetSoundDir(char *out) = 0;                                             // 13
	virtual bool GetSkinEntry(int index, PlayerSkinEntry *out) = 0;                      // 14
	virtual bool GetBoltonEntry(int index, PlayerBoltonEntry *out) = 0;                  // 15
	virtual bool GetPartEntry(int index, PlayerPartEntry *out) = 0;                      // 16
	virtual bool IsLoaded() = 0;                                                         // 17
	virtual void *Destroy(int flags) = 0;                                                // 18 deleting destructor

protected:
	~IGhoulPlayerModel() {}
};

// Matrix modes of instance method 50 (SPEC-011)
enum
{
	kMatrixWithTransform = 1,
	kMatrixInverse = 2,
	kMatrixPose = 4
};

// Playback modes (SPEC-011)
enum
{
	kPlayLoop = 0,
	kPlayOnce = 1,
	kPlayPingPong = 2,
	kPlayStatic = 3
};

// Part types (SPEC-010)
enum
{
	kPartNull = 0,
	kPartMesh = 1,
	kPartCamera = 2,
	kPartLight = 3
};

} // namespace ghoul

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
