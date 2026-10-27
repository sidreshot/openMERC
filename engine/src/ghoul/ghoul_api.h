// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc Ghoul model system - the C++ interfaces other modules see.
//
// Every interface is an abstract class whose virtual methods are declared in exactly the table order
// of the specs. With MSVC x86 the method tables are binary compatible with the original modules
// (thiscall, callee pops, bool in AL, 16-bit ids in AX, float on x87; checked by omghoul_test, which calls
// the slots through raw table pointers). No interface has a virtual destructor (it would take a table slot)
// and no method name is overloaded (MSVC reorders overloads in the table). Other compilers get source
// compatibility only.
//
// Using the library (implementation: engine/src/ghoul, static library "omghoul", see docs/impl/I3.md):
// - The host (engine or test) fills an om_ghoul_host_t (ghoul_host.h: file loading by game path, console,
//   fatal errors) and calls OMGhoul_Init once. OMGhoul_GetSystem(OM_GHOUL_SERVER / _CLIENT / _MENU) returns
//   the three IGhoulSystem objects of SPEC-010 A (game import #60 = the server one; renderer Init receives
//   the client one). Game imports #61-#66 are the OMGhoul_* C functions of ghoul_host.h.
// - Objects and instances are created and destroyed only through the interfaces (system 0, object 24 / 27,
//   instance 36 / 53). Pointers stay valid until destroyed; on the client and menu systems the replicated
//   ones are owned by the engine (system methods 23-25).
// - Engine-only system methods (12-27, 33) take the engine bit buffer om_ghoul_bits_t (ghoul_host.h):
//   method 16 / 18 write a complete svc 26 / 27 payload into it (the engine adds svc byte and length),
//   method 24 / 25 read one payload. Method 21 (begin frame), instance method 52 (offer, per instance),
//   method 18 (write), method 20 (commit), method 19 (acknowledge) are called in that order per client frame.
// - Drawing (instance method 41) goes through the 52-entry GL table given to system method 1 using
//   immediate mode (Begin/Vertex/End, matrix stack, texture parameters). A core-profile renderer emulates the
//   table (collect vertices between Begin and End); tests/soft_gl.cpp is a complete software example.
//   The renderer subclasses GhoulRenderParams (it is the render-cull object) and fills the block.
// - Model space follows the data: stock character and first-person models face -Y (the exporter's
//   front view); the game / player module chooses the instance transform (SPEC-011 matrix layout).
// - Not thread-safe: call from one thread.
//
// Implements: SPEC-010 (A system, B object, D player model, E callback, G GL table, light record),
//             SPEC-011 (C instance, matrix, hit record, render parameters)
#pragma once

#include <cstddef>
#include <cstdint>

#include "ghoul_host.h" // om_ghoul_bits_t

#if defined(_WIN32) && !defined(_WIN64)
#define GHOUL_GLAPI __stdcall
#else
#define GHOUL_GLAPI
#endif

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4265) // class has virtual functions but no virtual destructor (by design)
#endif

namespace ghoul {

typedef unsigned short GhoulID; // 1-based, 0 = none (SPEC-010 conventions)

class IGhoulSystem;
class IGhoulObject;
class IGhoulInst;

// Engine bit buffer for the engine-only network methods 16, 18, 24, 25 (see ghoul_host.h)
typedef om_ghoul_bits_t GhoulBits;

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

// SPEC-011: the render-parameter block is itself an object; entry 0 of its table answers whether a
// box with 8 world-space corners lies entirely outside the view.
class IGhoulRenderCull
{
public:
	virtual bool BoxOutside(const float (*corners)[3]) = 0;
};

// SPEC-011: render parameters, 180 bytes in the 32-bit build (the vtable pointer is the first word).
// The renderer derives from this class and implements BoxOutside.
class GhoulRenderParams : public IGhoulRenderCull
{
public:
	float time;              // 0x04 refdef game clock
	float lightColor[3];     // 0x08
	float lightDir[3];       // 0x14 directional light vector (x 500)
	float origin[3];         // 0x20 entity origin
	uint8_t reserved2c;      // 0x2c always 0
	uint8_t dirValid;        // 0x2d
	uint8_t useGLLights;     // 0x2e ghl_light_method >= 2
	uint8_t specular;        // 0x2f
	uint8_t fastLights;      // 0x30 ghl_fast_lights
	uint8_t hardwareLights;  // 0x31 ghl_light_method > 2.5
	uint8_t glDirectional;   // 0x32 ghl_gllight_directional
	uint8_t glAttenuation;   // 0x33 ghl_gllight_attenuation
	uint8_t entityFlag1000;  // 0x34 entity flag 0x1000 (enables shadows on non-characters)
	uint8_t shadowOnly;      // 0x35 RF_VIEWERMODEL
	uint8_t noTextureStack;  // 0x36 ghl_no_texture_stack
	uint8_t unused37;        // 0x37
	float shadowDarkness;    // 0x38
	float fogVisibility;     // 0x3c
	float shadowDist;        // 0x40
	float (*worldTrace)(const float *start, const float *end); // 0x44 fraction, -1 = start solid
	int32_t shadowMode;      // 0x48 0..2
	int32_t maxLights;       // 0x4c 1..8
	Matrix4 entityToWorld;   // 0x50
	float viewOrigin[3];     // 0x94
	float viewForward[3];    // 0xa0
	uint8_t noDynamicLights; // 0xac
	uint8_t padAD[3];        // 0xad
	void *entity;            // 0xb0 renderer entity (SPEC-002)
};

#if (defined(_WIN32) && !defined(_WIN64)) || defined(__i386__) /* 32-bit x86 layout */
static_assert(sizeof(HitRecord) == 36, "SPEC-011 hit record is 36 bytes");
static_assert(sizeof(GhoulRenderParams) == 180, "SPEC-011 render parameters are 180 bytes");
static_assert(offsetof(HitRecord, normal) == 0x0c, "hit record layout");
#endif

// SPEC-010 G: OpenGL 1.1 entry points supplied by the renderer (52 pointers, stdcall).
struct GhoulGLTable
{
	typedef unsigned int GLenum;
	typedef int GLint;
	typedef int GLsizei;
	typedef unsigned int GLuint;
	typedef float GLfloat;
	typedef unsigned char GLboolean;
	typedef double GLdouble;

	void(GHOUL_GLAPI *AlphaFunc)(GLenum func, GLfloat ref);
	void(GHOUL_GLAPI *Begin)(GLenum mode);
	void(GHOUL_GLAPI *BindTexture)(GLenum target, GLuint texture);
	void(GHOUL_GLAPI *BlendFunc)(GLenum sfactor, GLenum dfactor);
	void(GHOUL_GLAPI *Color3f)(GLfloat r, GLfloat g, GLfloat b);
	void(GHOUL_GLAPI *Color4f)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
	void(GHOUL_GLAPI *CullFace)(GLenum mode);
	void(GHOUL_GLAPI *DepthFunc)(GLenum func);
	void(GHOUL_GLAPI *DepthMask)(GLboolean flag);
	void(GHOUL_GLAPI *DeleteTextures)(GLsizei n, const GLuint *textures);
	void(GHOUL_GLAPI *Disable)(GLenum cap); // 10
	void(GHOUL_GLAPI *Enable)(GLenum cap);
	void(GHOUL_GLAPI *End)(void);
	void(GHOUL_GLAPI *FrontFace)(GLenum mode);
	GLenum(GHOUL_GLAPI *GetError)(void);
	void(GHOUL_GLAPI *GenTextures)(GLsizei n, GLuint *textures);
	void(GHOUL_GLAPI *GetFloatv)(GLenum pname, GLfloat *params);
	void(GHOUL_GLAPI *GetIntegerv)(GLenum pname, GLint *params);
	void(GHOUL_GLAPI *LoadIdentity)(void);
	void(GHOUL_GLAPI *LoadMatrixf)(const GLfloat *m);
	void(GHOUL_GLAPI *MatrixMode)(GLenum mode); // 20
	void(GHOUL_GLAPI *MultMatrixf)(const GLfloat *m);
	void(GHOUL_GLAPI *Normal3f)(GLfloat nx, GLfloat ny, GLfloat nz);
	void(GHOUL_GLAPI *PixelStorei)(GLenum pname, GLint param);
	void(GHOUL_GLAPI *PolygonOffset)(GLfloat factor, GLfloat units);
	void(GHOUL_GLAPI *PopMatrix)(void);
	void(GHOUL_GLAPI *PushMatrix)(void);
	void(GHOUL_GLAPI *Rotatef)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
	void(GHOUL_GLAPI *ShadeModel)(GLenum mode);
	void(GHOUL_GLAPI *TexCoord2f)(GLfloat s, GLfloat t);
	void(GHOUL_GLAPI *TexEnvf)(GLenum target, GLenum pname, GLfloat param); // 30
	void(GHOUL_GLAPI *TexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width,
		GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
	void(GHOUL_GLAPI *TexParameteri)(GLenum target, GLenum pname, GLint param);
	void(GHOUL_GLAPI *TexGeni)(GLenum coord, GLenum pname, GLint param);
	void(GHOUL_GLAPI *Vertex3f)(GLfloat x, GLfloat y, GLfloat z);
	void(GHOUL_GLAPI *Color3fv)(const GLfloat *v);
	void(GHOUL_GLAPI *Normal3fv)(const GLfloat *v);
	void(GHOUL_GLAPI *TexCoord2fv)(const GLfloat *v);
	void(GHOUL_GLAPI *Vertex3fv)(const GLfloat *v);
	void(GHOUL_GLAPI *LightModelf)(GLenum pname, GLfloat param);
	void(GHOUL_GLAPI *LightModelfv)(GLenum pname, const GLfloat *params); // 40
	void(GHOUL_GLAPI *LightModeli)(GLenum pname, GLint param);
	void(GHOUL_GLAPI *LightModeliv)(GLenum pname, const GLint *params);
	void(GHOUL_GLAPI *Lightf)(GLenum light, GLenum pname, GLfloat param);
	void(GHOUL_GLAPI *Lightfv)(GLenum light, GLenum pname, const GLfloat *params);
	void(GHOUL_GLAPI *Lighti)(GLenum light, GLenum pname, GLint param);
	void(GHOUL_GLAPI *Lightiv)(GLenum light, GLenum pname, const GLint *params);
	void(GHOUL_GLAPI *Materialf)(GLenum face, GLenum pname, GLfloat param);
	void(GHOUL_GLAPI *Materialfv)(GLenum face, GLenum pname, const GLfloat *params);
	void(GHOUL_GLAPI *Materiali)(GLenum face, GLenum pname, GLint param);
	void(GHOUL_GLAPI *Materialiv)(GLenum face, GLenum pname, const GLint *params); // 50
	void(GHOUL_GLAPI *FlushState)(void); // 51: apply deferred state before geometry
};
static_assert(sizeof(GhoulGLTable) == 52 * sizeof(void *), "SPEC-010 G: 52 entries");

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
	virtual bool NetWriteReliable(int client, int frame, om_ghoul_bits_t *msg) = 0;            // 16
	virtual bool NetReliablePending(int client) = 0;                                     // 17
	virtual bool NetWriteUnreliable(int client, int frame, om_ghoul_bits_t *msg, float time) = 0; // 18
	virtual void NetAcknowledge(int client, int frame) = 0;                              // 19
	virtual void NetCommitFrame(int client, int frame) = 0;                              // 20
	virtual void NetBeginFrame(int client, int frame) = 0;                               // 21
	virtual void TouchFile(const char *path) = 0;                                        // 22
	virtual void ClientStartAccepting() = 0;                                             // 23
	virtual void ClientReadReliable(int unused, om_ghoul_bits_t *msg, bool specular) = 0;      // 24
	virtual void ClientReadUnreliable(int frame, om_ghoul_bits_t *msg, float time) = 0;        // 25
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
