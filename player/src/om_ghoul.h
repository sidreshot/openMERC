/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - the Ghoul model system interfaces as seen by player.dll.
 *
 * The engine's Ghoul library (package I3, engine/src/ghoul/ghoul_api.h) declares the same interfaces. This header
 * mirrors its class names, method names and signatures exactly, so that switching is one macro:
 * define OM_PLAYER_USE_ENGINE_GHOUL to compile against I3's header instead of this copy.
 * Abstract classes with the virtual methods in table order: with MSVC x86 the method tables are binary
 * compatible with the engine's objects (thiscall, callee pops, bool in AL, 16-bit ids in AX, float on x87).
 * No virtual destructors, no overloaded names.
 * Owner of this header: package I7 (player module).
 * Implements: SPEC-010 (A system, B model object, E event callback), SPEC-011 (C instance, matrix)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#if defined(OM_PLAYER_USE_ENGINE_GHOUL)

#include "../../engine/src/ghoul/ghoul_api.h"
namespace om {
using ghoul::GhoulID;
using ghoul::IGhoulCallback;
using ghoul::IGhoulInst;
using ghoul::IGhoulObject;
using ghoul::IGhoulSystem;
using ghoul::Matrix4;
using ghoul::kPlayLoop;
using ghoul::kPlayOnce;
using ghoul::kPlayPingPong;
using ghoul::kPlayStatic;
using ghoul::kPartNull;
using ghoul::kPartMesh;
using ghoul::kPartCamera;
using ghoul::kPartLight;
} /* namespace om */

#else

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4265)
#endif

namespace om {

typedef unsigned short GhoulID; /* 1-based, 0 = none (SPEC-010 conventions) */

class IGhoulSystem;
class IGhoulObject;
class IGhoulInst;
class BitWriter;
class BitReader;
class GhoulRenderParams;
struct HitRecord;
struct LightRecord;
struct GhoulGLTable;

/* SPEC-011: 68-byte row-vector matrix plus flags (bit 0 = identity). */
struct Matrix4 {
    float m[4][4];
    uint32_t flags;
};
static_assert(sizeof(Matrix4) == 68, "SPEC-011 matrix is 68 bytes");

/* SPEC-010 E: event callback, implemented by this module and called by the Ghoul runtime. */
class IGhoulCallback {
public:
    virtual bool Event(uint32_t undefinedValue, void *userValue, float time, const void *data) = 0;
};

/* SPEC-010 B: model object, 29 methods. */
class IGhoulObject {
public:
    virtual GhoulID RegisterSequence(const char *path, bool alsoOthers) = 0;             /* 0 */
    virtual void FinishRegistration(bool allSkins) = 0;                                  /* 1 */
    virtual void Prepare(bool specular) = 0;                                             /* 2 */
    virtual GhoulID FindSequence(const char *path) = 0;                                  /* 3 */
    virtual unsigned short NumSequences() = 0;                                           /* 4 */
    virtual void GetSequencePath(GhoulID seq, char *out) = 0;                            /* 5 */
    virtual GhoulID RegisterPart(const char *name) = 0;                                  /* 6 */
    virtual GhoulID FindPart(const char *name) = 0;                                      /* 7 */
    virtual unsigned short NumParts() = 0;                                               /* 8 */
    virtual int PartType(GhoulID part) = 0;                                              /* 9 */
    virtual void GetPartName(GhoulID part, char *out) = 0;                               /* 10 */
    virtual GhoulID RegisterNote(const char *name) = 0;                                  /* 11 */
    virtual GhoulID FindNote(const char *name) = 0;                                      /* 12 */
    virtual unsigned short NumNotes() = 0;                                               /* 13 */
    virtual void EnumerateNotes(IGhoulCallback *cb, GhoulID note) = 0;                   /* 14 */
    virtual GhoulID RegisterMaterial(const char *name) = 0;                              /* 15 */
    virtual GhoulID FindMaterial(const char *name) = 0;                                  /* 16 */
    virtual unsigned short NumMaterials() = 0;                                           /* 17 */
    virtual void GetMaterialName(GhoulID material, char *out) = 0;                       /* 18 */
    virtual GhoulID RegisterSkin(GhoulID material, const char *name, int ignored) = 0;   /* 19 */
    virtual GhoulID FindSkin(GhoulID material, const char *name, int ignored) = 0;       /* 20 */
    virtual unsigned short NumSkins() = 0;                                               /* 21 */
    virtual void GetSkinName(GhoulID skin, char *out) = 0;                               /* 22 */
    virtual GhoulID SkinMaterial(GhoulID skin) = 0;                                      /* 23 */
    virtual IGhoulInst *CreateInstance() = 0;                                            /* 24 */
    virtual unsigned short GetObjectId() = 0;                                            /* 25 */
    virtual IGhoulSystem *GetSystem() = 0;                                               /* 26 */
    virtual void Destroy() = 0;                                                          /* 27 */
    virtual void DestroyAllInstances() = 0;                                              /* 28 */

protected:
    ~IGhoulObject() {}
};

/* SPEC-011: model instance, 55 methods. */
class IGhoulInst {
public:
    virtual void SetTransform(const Matrix4 *m) = 0;                                     /* 0 */
    virtual void SetTransform16(const float *m16) = 0;                                   /* 1 */
    virtual void GetTransform(Matrix4 *out) = 0;                                         /* 2 */
    virtual void GetTransform16(float *out16) = 0;                                       /* 3 */
    virtual void SetOnOff(bool on, float time) = 0;                                      /* 4 */
    virtual bool IsOn() = 0;                                                             /* 5 */
    virtual void SetSpeed(int speedIndex) = 0;                                           /* 6 */
    virtual int GetSpeed() = 0;                                                          /* 7 */
    virtual void SetPartOnOff(GhoulID part, bool shown) = 0;                             /* 8 */
    virtual bool IsPartOn(GhoulID part) = 0;                                             /* 9 */
    virtual void SetAllPartsOnOff(bool shown) = 0;                                       /* 10 */
    virtual void SetSkin(GhoulID material, GhoulID skin, GhoulID part) = 0;              /* 11 */
    virtual GhoulID GetSkin(GhoulID material, GhoulID part, int ignored) = 0;            /* 12 */
    virtual void RemoveSkin(GhoulID material, GhoulID part, int ignored) = 0;            /* 13 */
    virtual void SetTint(float r, float g, float b, float a) = 0;                        /* 14 */
    virtual void GetTint(float *r, float *g, float *b, float *a) = 0;                    /* 15 */
    virtual void SetTintAll(float r, float g, float b, float a) = 0;                     /* 16 */
    virtual void Play(GhoulID seq, float time, float offset, bool restart, int mode, bool matchPhase,
                      bool reverse) = 0;                                                 /* 17 */
    virtual GhoulID GetPlayingSequence() = 0;                                            /* 18 */
    virtual void Pause(float time) = 0;                                                  /* 19 */
    virtual void Resume(float time) = 0;                                                 /* 20 */
    virtual void SetUserValue(void *value) = 0;                                          /* 21 */
    virtual void *GetUserValue() = 0;                                                    /* 22 */
    virtual void AddNoteCallback(IGhoulCallback *cb, GhoulID note) = 0;                  /* 23 */
    virtual void RemoveNoteCallback(IGhoulCallback *cb, GhoulID note) = 0;               /* 24 */
    virtual void AddMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int mode) = 0;      /* 25 */
    virtual void RemoveMatrixCallback(IGhoulCallback *cb, GhoulID bolt, int mode) = 0;   /* 26 */
    virtual float SequenceDuration(GhoulID seq, int mode, float *frameTime) = 0;         /* 27 */
    virtual void AttachChild(GhoulID myBolt, IGhoulInst *child, GhoulID childBolt) = 0;  /* 28 */
    virtual void SetAttachBolt(GhoulID bolt) = 0;                                        /* 29 */
    virtual GhoulID GetAttachBolt() = 0;                                                 /* 30 */
    virtual void DetachChild(IGhoulInst *child) = 0;                                     /* 31 */
    virtual int NumChildren() = 0;                                                       /* 32 */
    virtual IGhoulInst *GetChild(int index, GhoulID *myBolt) = 0;                        /* 33 */
    virtual IGhoulObject *GetGhoulObject() = 0;                                          /* 34 */
    virtual IGhoulInst *GetParent() = 0;                                                 /* 35 */
    virtual IGhoulInst *Clone(bool withChildren) = 0;                                    /* 36 */
    virtual int SaveState(void *buffer, int capacity, IGhoulCallback **table, int count,
                          const char *modelName) = 0;                                    /* 37 */
    virtual GhoulID LoadState(const void *buffer, int length, IGhoulCallback **table, int count,
                              const char *modelName) = 0;                                /* 38 */
    virtual GhoulID GetInstanceId() = 0;                                                 /* 39 */
    virtual GhoulID GetObjectId() = 0;                                                   /* 40 */
    virtual void Draw(GhoulRenderParams *params) = 0;                                    /* 41 */
    virtual void Reserved42(int a, int b, int c) = 0;                                    /* 42 */
    virtual int TraceRay(float time, const float *start, const float *dir, HitRecord *hits,
                         int maxHits) = 0;                                               /* 43 */
    virtual int TraceBox(float time, const Matrix4 *root, const float *start, const float *end,
                         const float *mins, const float *maxs, HitRecord *hits, int maxHits) = 0; /* 44 */
    virtual void ProcessEvents(float time) = 0;                                          /* 45 */
    virtual void StopEvents() = 0;                                                       /* 46 */
    virtual void StopMatrixEvents() = 0;                                                 /* 47 */
    virtual void StartNoteEvents(float time) = 0;                                        /* 48 */
    virtual void StartEvents(float time) = 0;                                            /* 49 */
    virtual void GetBoltMatrix(float time, Matrix4 *out, GhoulID bolt, int matrixMode,
                               bool throughParents) = 0;                                 /* 50 */
    virtual void GetBounds(float time, const Matrix4 *root, float *mins, float *maxs, GhoulID part,
                           bool exact) = 0;                                              /* 51 */
    virtual GhoulID NetOffer(float priority) = 0;                                        /* 52 */
    virtual void Destroy() = 0;                                                          /* 53 */
    virtual GhoulID GetPlayingSequence2() = 0;                                           /* 54 */

protected:
    ~IGhoulInst() {}
};

/* SPEC-010 A: system, 49 methods. */
class IGhoulSystem {
public:
    virtual IGhoulObject *CreateObject() = 0;                                            /* 0 */
    virtual void SetGLTable(const GhoulGLTable *table) = 0;                              /* 1 */
    virtual void SetSpecular(bool specular) = 0;                                         /* 2 */
    virtual void ReleaseTextures() = 0;                                                  /* 3 */
    virtual void BeginFrame() = 0;                                                       /* 4 */
    virtual void EndFrame() = 0;                                                         /* 5 */
    virtual void DestroyAllObjects() = 0;                                                /* 6 */
    virtual void Purge() = 0;                                                            /* 7 */
    virtual void Reset() = 0;                                                            /* 8 */
    virtual IGhoulInst *FindInstance(int handle) = 0;                                    /* 9 */
    virtual IGhoulObject *FindObject(int handle) = 0;                                    /* 10 */
    virtual void SetFullPathCallback(void (*fn)(char *out, const char *path)) = 0;       /* 11 */
    virtual void NetClientConnected(int client) = 0;                                     /* 12 */
    virtual void NetClientDropped(int client) = 0;                                       /* 13 */
    virtual void NetResendAll() = 0;                                                     /* 14 */
    virtual void NetDropAll() = 0;                                                       /* 15 */
    virtual bool NetWriteReliable(int client, int frame, BitWriter *msg) = 0;            /* 16 */
    virtual bool NetReliablePending(int client) = 0;                                     /* 17 */
    virtual bool NetWriteUnreliable(int client, int frame, BitWriter *msg, float time) = 0; /* 18 */
    virtual void NetAcknowledge(int client, int frame) = 0;                              /* 19 */
    virtual void NetCommitFrame(int client, int frame) = 0;                              /* 20 */
    virtual void NetBeginFrame(int client, int frame) = 0;                               /* 21 */
    virtual void TouchFile(const char *path) = 0;                                        /* 22 */
    virtual void ClientStartAccepting() = 0;                                             /* 23 */
    virtual void ClientReadReliable(int unused, BitReader *msg, bool specular) = 0;      /* 24 */
    virtual void ClientReadUnreliable(int frame, BitReader *msg, float time) = 0;        /* 25 */
    virtual int ClientReliableSize() = 0;                                                /* 26 */
    virtual void *ClientReliableData() = 0;                                              /* 27 */
    virtual int ModelLightCount() = 0;                                                   /* 28 */
    virtual void GetModelLight(int index, LightRecord *out) = 0;                         /* 29 */
    virtual void ClearLights() = 0;                                                      /* 30 */
    virtual void AddDynamicLight(const LightRecord *light) = 0;                          /* 31 */
    virtual void EndRenderFrame() = 0;                                                   /* 32 */
    virtual void FlushMeshCache() = 0;                                                   /* 33 */
    virtual void AddWorldLight(const LightRecord *light) = 0;                            /* 34 */
    virtual void FinishWorldLights() = 0;                                                /* 35 */
    virtual void BeginWorldLights() = 0;                                                 /* 36 */
    virtual void SetGamma(const unsigned char *table256) = 0;                            /* 37 */
    virtual void SetTextureDetail(int level) = 0;                                        /* 38 */
    virtual void SetPolygonOffset(float factor, float units) = 0;                        /* 39 */
    virtual int StatTextures() = 0;                                                      /* 40 */
    virtual int StatTextureBytes() = 0;                                                  /* 41 */
    virtual int StatMeshes() = 0;                                                        /* 42 */
    virtual int StatTriangles() = 0;                                                     /* 43 */
    virtual int StatTrianglesTotal() = 0;                                                /* 44 */
    virtual int StatVertices() = 0;                                                      /* 45 */
    virtual int StatCorners() = 0;                                                       /* 46 */
    virtual int StatLights() = 0;                                                        /* 47 */
    virtual void ResetStats() = 0;                                                       /* 48 */

protected:
    ~IGhoulSystem() {}
};

/* playback modes (SPEC-011) */
enum { kPlayLoop = 0, kPlayOnce = 1, kPlayPingPong = 2, kPlayStatic = 3 };
/* part types (SPEC-010) */
enum { kPartNull = 0, kPartMesh = 1, kPartCamera = 2, kPartLight = 3 };

} /* namespace om */

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif /* OM_PLAYER_USE_ENGINE_GHOUL */
