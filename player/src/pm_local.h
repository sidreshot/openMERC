/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - internal declarations: the inventory object (exact 668-byte layout with the 640-byte
 * state block at 0x1C), the weapon-description objects, module globals and helpers.
 * Implements: SPEC-003 (export/inventory layout), SPEC-600 (weapon descriptions, state), SPEC-601 (state block),
 *             SPEC-602 (prediction fields)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../../engine/src/common/header/om_platform.h" /* OpenMerc: MSVC names on other platforms */
#include "om_ghoul.h"
#include "om_player_api.h"

namespace om {

enum {
    NUM_WEAPONS = 15,
    NUM_WSLOTS = 14,
    NUM_SELECTABLE = 12,
    NUM_AMMO = 11,
    NUM_ISLOTS = 6,
    HAND_HURT_SLOT = 12,
    HAND_THROW_SLOT = 13,
};

enum WeaponId {
    W_NONE, W_KNIFE, W_PISTOL2, W_PISTOL1, W_MPISTOL, W_ASSAULT, W_SNIPER, W_SLUGGER, W_SHOTGUN, W_MACHINEGUN,
    W_ROCKET, W_MPG, W_FLAMEGUN, W_HURTHAND, W_THROWHAND
};

enum AmmoType { AM_NONE, AM_KNIFE, AM_44, AM_9MM, AM_SHELLS, AM_556, AM_ROCKETS, AM_BATTERY, AM_FUEL, AM_SLUG };

enum ItemType { IT_NONE, IT_FPAK, IT_C4, IT_GOGGLES, IT_CLAYMORE, IT_MEDKIT, IT_GRENADE, IT_FLAG };

/* weapon state, SPEC-600 Behaviour 3 */
enum WState { WS_IDLE = 0, WS_RELOAD = 1, WS_FIRE1 = 2, WS_FIRE2 = 3, WS_PUTAWAY = 4, WS_HAND = 5 };

/* pending weapon request codes, SPEC-003 / SPEC-601 Behaviour 6 */
enum {
    REQ_NONE = 0, REQ_RELOAD = 1, REQ_NEXT = 2, REQ_PREV = 3, REQ_BESTSAFE = 4, REQ_BESTUNSAFE = 5,
    REQ_LOSE = 6, REQ_DROP = 7, REQ_SLOT0 = 8
};

/* item requests, SPEC-601 Behaviour 4 */
enum { IREQ_NONE = 0, IREQ_NEXT = 1, IREQ_PREV = 2, IREQ_DROP = 3, IREQ_USE = 4, IREQ_USESLOT0 = 5 };

/* player-state words written/read by the module (observed on the original 1.07f, see docs/impl/I7.md). The
   offsets are the 32-bit ones on x86; on 64-bit targets the pointer members widen and the same member list gives
   the natural layout the engine (sof_player_state_t) and the open game module (player_state_t) also use. */
struct PsPrefix {
    unsigned char pmove[28];       /* 0x00 */
    float viewangles[3];           /* 0x1C */
    float viewoffset[3];           /* 0x28 */
    float kick_angles[3];          /* 0x34 */
    float weaponkick[3];           /* 0x40 */
    float remote_origin[3];        /* 0x4C */
    float remote_angles[3];        /* 0x58 */
    int remote_entity;             /* 0x64 */
    int remote_type;               /* 0x68 */
    void *viewinst;                /* 0x6C */
    unsigned short viewid;         /* 0x70 */
    unsigned short viewtype;       /* 0x72 */
    short clip;                    /* 0x74 */
    short reserve;                 /* 0x76 */
    unsigned char weaponseq;       /* 0x78 */
    unsigned char resync, inhibit, pad7b;
    void *bodyinst;                /* 0x7C */
    int unknown80;                 /* 0x80 */
    float blend[4];                /* 0x84 */
    float fov;                     /* 0x94 */
};
enum : int {
    PS_WEAPONKICK = (int)offsetof(PsPrefix, weaponkick), /* float[3], copied by the game from the client's kick */
    PS_VIEWINST = (int)offsetof(PsPrefix, viewinst),     /* Ghoul instance pointer of the view weapon (server) */
    PS_VIEWID = (int)offsetof(PsPrefix, viewid),         /* uint16 instance handle (engine writes it) */
    PS_VIEWTYPE = (int)offsetof(PsPrefix, viewtype),     /* uint16 weapon id of the view weapon */
    PS_CLIP = (int)offsetof(PsPrefix, clip),             /* int16 weapon counter A */
    PS_RESERVE = (int)offsetof(PsPrefix, reserve),       /* int16 weapon counter B */
    PS_WEAPONSEQ = (int)offsetof(PsPrefix, weaponseq),   /* uint8 weapon-state sequence */
    PS_FOV = (int)offsetof(PsPrefix, fov),               /* float field of view */
};
static_assert(sizeof(void *) != 4 || (PS_VIEWINST == 0x6C && PS_VIEWID == 0x70 && PS_WEAPONSEQ == 0x78 &&
                  PS_FOV == 0x94), "SPEC-202 player state offsets");

struct WeaponSlot {
    int clip;
    int weapon;
    int unused;
};

/* The 640-byte state block (SPEC-601 Interface 3), object bytes 0x1C..0x29B. */
struct InvState {
    int pendingRequest;        /* 0x000 */
    int dictated;              /* 0x004 client: switch dictated by the server pending */
    int synced;                /* 0x008 client: synchronised with the server's weapon */
    int targetSlot;            /* 0x00C */
    int curWeapon;             /* 0x010 */
    int animWeapon;            /* 0x014 */
    int curSlot;               /* 0x018 */
    int predWeapon;            /* 0x01C */
    int predAnim;              /* 0x020 */
    int predSlot;              /* 0x024 */
    int dropSlot;              /* 0x028 */
    WeaponSlot slots[NUM_WSLOTS]; /* 0x02C */
    int varA;                  /* 0x0D4 ammunition is used */
    int varB;                  /* 0x0D8 dropping allowed */
    int varI;                  /* 0x0DC kept-weapon mask */
    int varC;                  /* 0x0E0 reloading enabled */
    int varE;                  /* 0x0E4 discard partial clips */
    int varD;                  /* 0x0E8 auto-reload */
    int varF;                  /* 0x0EC drop all on death */
    int varG;                  /* 0x0F0 next/prev stop at empty weapons */
    char tag[8];               /* 0x0F4 */
    int varH;                  /* 0x0FC weapon-change commands without force */
    int ammo[NUM_AMMO];        /* 0x100 */
    int ready;                 /* 0x12C */
    IGhoulInst *inst;          /* 0x130 */
    IGhoulInst *nextInst;      /* 0x134 */
    int lastSlot;              /* 0x138 */
    int armor;                 /* 0x13C */
    unsigned owned;            /* 0x140 */
    int switchNow;             /* 0x144 */
    int ownerPredicts;         /* 0x148 */
    int unused14c[17];         /* 0x14C */
    OwnerContext *ctx;         /* 0x190 */
    int selItem;               /* 0x194 */
    int itemRequest;           /* 0x198 */
    int lastItemUse;           /* 0x19C */
    ItemSlot items[NUM_ISLOTS]; /* 0x1A0 */
    int p1Stance;              /* 0x200 pistol1: 1 one hand (a), 2 two hands (b) */
    int subPose;               /* 0x204 pistol1 c / pistol2 d */
    int transitionLast;        /* 0x208 */
    int knifeBloody;           /* 0x20C */
    int p2Stance;              /* 0x210 pistol2: 0 a, 1 c */
    char seqName[64];          /* 0x214 */
    int spread;                /* 0x254 */
    int wstate;                /* 0x258 */
    int knifeThrowOk;          /* 0x25C */
    int stanceB;               /* 0x260 assault stance b / sniper "drawn" */
    int prepaid;               /* 0x264 */
    int zoomHeld;              /* 0x268 */
    int scoped;                /* 0x26C */
    int idleCounter;           /* 0x270 */
    int savedZoom;             /* 0x274 */
    int holstered;             /* 0x278 */
    float lastAutoShot;        /* 0x27C */
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(InvState) == 640, "SPEC-601: 640-byte state block");
static_assert(offsetof(InvState, slots) == 0x2C, "slots");
static_assert(offsetof(InvState, varA) == 0xD4, "varA");
static_assert(offsetof(InvState, ammo) == 0x100, "ammo");
static_assert(offsetof(InvState, ready) == 0x12C, "ready");
static_assert(offsetof(InvState, ctx) == 0x190, "ctx");
static_assert(offsetof(InvState, items) == 0x1A0, "items");
static_assert(offsetof(InvState, seqName) == 0x214, "seqName");
static_assert(offsetof(InvState, spread) == 0x254, "spread");
static_assert(offsetof(InvState, lastAutoShot) == 0x27C, "lastAutoShot");
#elif defined(_M_X64) || defined(__x86_64__)
/* the svc 30 full state is this block verbatim; the engine skips CLP_STATE_BYTES (cl_player.c) */
static_assert(sizeof(InvState) == 656, "64-bit state block: 640 + 3 pointers of 8 bytes");
#endif

/* events queued per inventory (server: outgoing; client: received, SPEC-003 svc 28) */
struct InvEvent {
    int id;
    int value;
};
enum { MAX_EVENTS = 64 };
struct InvExtra {
    InvEvent out[MAX_EVENTS];
    int nout;
    InvEvent in[MAX_EVENTS];
    int nin;
    int value73;          /* not used: slot 73/74 live in the state block (ownerPredicts) */
    float pendingFire1;   /* server, predicting owner: fire times read in this think */
    float pendingFire2;
    bool nextOwned;       /* st.nextInst is a live instance of its own (after a predicted switch the original
                             leaves the word pointing at the instance that became current, SPEC-AMEND-006 I7.4) */
};

/* ------------------------------------------------------------------------------------------------
 * Weapon description object, 200 bytes, 32 virtual slots (SPEC-600 Interface 2). Only slots 28 (0x70)
 * and 29 (0x74) are called from outside.
 * ------------------------------------------------------------------------------------------------ */
class WeaponDesc {
public:
    virtual int Slot00();
    virtual int Slot01();
    virtual int Slot02();
    virtual int Slot03();
    virtual int Slot04();
    virtual int Slot05();
    virtual int Slot06();
    virtual int Slot07();
    virtual int Slot08();
    virtual int Slot09();
    virtual int Slot10();
    virtual int Slot11();
    virtual int Slot12();
    virtual int Slot13();
    virtual int Slot14();
    virtual int Slot15();
    virtual int Slot16();
    virtual int Slot17();
    virtual int Slot18();
    virtual int Slot19();
    virtual int Slot20();
    virtual int Slot21();
    virtual int Slot22();
    virtual int Slot23();
    virtual int Slot24();
    virtual int Slot25();
    virtual int Slot26();
    virtual int Slot27();
    virtual int FiresFromMuzzle(); /* 28, byte offset 0x70; full EAX set (callers may test more than AL) */
    virtual int Converges();       /* 29, byte offset 0x74 */
    virtual int Slot30();
    virtual int Slot31();

    int clip;                /* 0x04 */
    int weight;              /* 0x08 */
    int ammoType[2];         /* 0x0C, 0x10 */
    float modeValue[2];      /* 0x14, 0x18 */
    float spreadPerShot[2];  /* 0x1C, 0x20 */
    float spreadMax[2];      /* 0x24, 0x28 */
    char dir[128];           /* 0x2C */
    short zero;              /* 0xAC */
    short pad;               /* 0xAE */
    float kick[2];           /* 0xB0, 0xB4 */
    int attack[2];           /* 0xB8, 0xBC */
    int msgValue;            /* 0xC0 */
    IGhoulObject *model;     /* 0xC4 */

    bool muzzle, converge;   /* 0xC8: our own, beyond the 200 bytes the game knows */
};

/* ------------------------------------------------------------------------------------------------
 * Inventory object (SPEC-003 Interface, SPEC-601). 79 thiscall slots in table order, then the data:
 * 0x04..0x1B private, 0x1C state block. 668 bytes (SPEC-AMEND-006 I7.2).
 * ------------------------------------------------------------------------------------------------ */
class Inventory {
public:
    virtual void Clear(bool alsoFlag);                         /* 0 */
    virtual void RemoveItemType(int type);                     /* 1 */
    virtual void DeathDrop();                                  /* 2 */
    virtual void Think();                                      /* 3 */
    virtual int Command(const char *cmd, int force);          /* 4 */
    virtual void TopUpClips();                                 /* 5 */
    virtual int SelectWeaponNow(int weapon);                  /* 6 */
    virtual void SelectSlotNow(int slot);                      /* 7 */
    virtual void SelectBestNow();                              /* 8 */
    virtual void RequestWeapon(int weapon);                    /* 9 */
    virtual void RequestSlot(int slot);                        /* 10 */
    virtual void RequestBest();                                /* 11 */
    virtual int AddAmmo(int type, int amount);                /* 12 */
    virtual int WeaponMessageId(int weapon);                   /* 13 */
    virtual int AddAmmoForWeapon(int weapon, int amount);     /* 14 */
    virtual int AddAmmoForWeapon2(int weapon, int amount);    /* 15 */
    virtual int GiveWeapon(int weapon, int clip);             /* 16 */
    virtual int TotalWeight();                                 /* 17 */
    virtual int WeaponWeight(int weapon);                      /* 18 */
    virtual int AddItems(int type, int count, int max);       /* 19 */
    virtual int AddArmor(int delta);                           /* 20 */
    virtual int HasItem(int type);                            /* 21 */
    virtual int OwnsWeapon(int weapon);                        /* 22 */
    virtual int CurrentWeapon();                               /* 23 */
    virtual int BestWeapon();                                  /* 24 */
    virtual int SelectedItemType();                            /* 25 */
    virtual int SelectedItemCount();                           /* 26 */
    virtual int CurrentReserve();                              /* 27 */
    virtual int CurrentClip();                                 /* 28 */
    virtual int CurrentClipSize();                             /* 29 */
    virtual int ClipSize(int weapon);                          /* 30 */
    virtual int AbsorbDamage(int damage, float mode, float fraction); /* 31 */
    virtual int Armor();                                       /* 32 */
    virtual int ScopeActive();                                 /* 33 */
    virtual void KnifeBlood(int arg);                          /* 34 */
    virtual int IsHolstered();                                 /* 35 */
    virtual void ToggleHolster();                              /* 36 */
    virtual int GetA();                                        /* 37 */
    virtual void SetA(int v);                                  /* 38 */
    virtual int GetB();                                        /* 39 */
    virtual void SetB(int v);                                  /* 40 */
    virtual int GetI();                                        /* 41 */
    virtual void SetI(int v);                                  /* 42 */
    virtual int GetC();                                        /* 43 */
    virtual void SetC(int v);                                  /* 44 */
    virtual int GetD();                                        /* 45 */
    virtual void SetD(int v);                                  /* 46 */
    virtual int GetE();                                        /* 47 */
    virtual void SetE(int v);                                  /* 48 */
    virtual const char *GetTag();                              /* 49 */
    virtual void SetTag(const char *tag);                      /* 50 */
    virtual int GetF();                                        /* 51 */
    virtual void SetF(int v);                                  /* 52 */
    virtual int GetG();                                        /* 53 */
    virtual void SetG(int v);                                  /* 54 */
    virtual int GetH();                                        /* 55 */
    virtual void SetH(int v);                                  /* 56 */
    virtual int RequestDrop();                                /* 57 */
    virtual void Deselect();                                   /* 58 */
    virtual void ItemsSwitchOff();                             /* 59 */
    virtual int WeaponCount();                                 /* 60 */
    virtual void SetOwnerContext(OwnerContext *ctx);           /* 61 */
    virtual void DropAllWeapons();                             /* 62 */
    virtual void Landed();                                     /* 63 */
    virtual int LoadFromCvars(unsigned *maskOut, int *healthOut); /* 64 */
    virtual void SaveToCvars(bool addClips, int health);       /* 65 */
    virtual int LoadFromInfo(unsigned *maskOut, const char *info); /* 66 */
    virtual void WriteInfo(char *out, bool weapons, bool ammo, bool items, bool armor); /* 67 */
    virtual void ClearEvents();                                /* 68 */
    virtual void ReadEvents();                                 /* 69 */
    virtual void ApplyEvents();                                /* 70 */
    virtual void ClearOutEvents();                             /* 71 */
    virtual void SendEvents();                                 /* 72 */
    virtual void SetOwnerPredicts(int v);                      /* 73 */
    virtual int GetOwnerPredicts();                            /* 74 */
    virtual void WriteSave();                                  /* 75 */
    virtual void ReadSave();                                   /* 76 */
    virtual void ReadFullState(sizebuf_t *msg);                /* 77 */
    virtual void SendFullState(int clientIndex);               /* 78 */

    /* ---------------- data, exact layout ---------------- */
    InvExtra *x;             /* 0x04 */
    int priv08[5];           /* 0x08..0x1B */
    InvState st;             /* 0x1C */

    /* ---------------- non-virtual implementation (pm_*.cpp) ---------------- */
    void Construct();
    void Destruct();

    /* weapons (pm_weapons.cpp) */
    int W() const { return st.curWeapon; }
    WeaponSlot &CurSlot() { return st.slots[st.curSlot >= 0 && st.curSlot < NUM_WSLOTS ? st.curSlot : 0]; }
    bool Ammo() const { return st.varA != 0; }
    bool Play(const char *name, int mode = 1, float start = -1.f);
    bool HasSeq(const char *name);
    void GoIdle();
    void ChooseIdle();
    void DryFire();
    bool TakeShot(bool alt, bool startSeq);
    bool CanPay(bool alt);
    bool StartAttack(bool alt, bool held);
    bool StartReload(bool automatic);
    void EndOfSequence();
    void FireNote(bool alt, const char *arg);
    bool ProcessRequest();
    bool SwitchToSlot(int slot, bool dictated);
    void SelectSlot(int slot, bool draw);
    bool StartDraw();
    void PrepareNextInstance(int slot);
    void CompleteSwitch();
    void DropOrLose(bool lose);
    void HandFinished();
    void WriteCounters();
    void WritePSWeapon();
    void NewViewInstance(int weapon, bool asNext);
    void DestroyInstance(IGhoulInst *&inst);
    bool Usable(int slot, bool allowEmpty);
    int BestSlot(bool safe);
    int FindSlotOfWeapon(int weapon);
    void HolsterThink(int &att, int &alt);
    void PlayPutaway();

    /* per-weapon rules (pm_weaponrules.cpp) */
    const char *PrimarySeq();
    const char *AltSeq();
    const char *ReloadSeq();
    const char *PutawaySeq();
    const char *DrySeq();
    const char *DrawSeq();
    void IdleRule();
    void PerThinkRule(int &att, int &alt, int x1, int x2);
    bool EndRule(const char *seq);
    const char *RefireSeq(bool alt);
    bool WantsRefire(bool alt);
    bool ShotGate(bool alt);
    void DeselectRule();
    void VisibleRounds(bool onSelect, bool serverPaidShot);
    void ShellBullets();
    void SniperZoomThink(int alt, int x1, int x2);
    void SetFov(float fov);
    float GetFov();
    void ScopeModel(bool hidden);
    void FlameSound(bool on);

    /* items (pm_items.cpp) */
    void ItemsThink();
    void ProcessItemRequest();
    void UseItemSlot(int slot);
    void DropSelectedItem();
    void AdvanceItemSelection(int dir);
    int FindItemSlot(int type);

    /* events / prediction (pm_predict.cpp) */
    void QueueEvent(int id, int value);
    void ApplyEvent(int id, int value);
    void SwitchFollower();
    void PredictedReloadStart();
    void ClientCountersFromPS();
    bool PredictingOwner() const;

    /* owner data */
    edict_t *Owner() const { return st.ctx ? st.ctx->owner : NULL; }
    unsigned char *PS() const;
};

#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(WeaponDesc) - 4 >= 0xC4 + 4 - 4, "weapon description");
#endif

/* ------------------------------------------------------------------------------------------------
 * Globals (pm_main.cpp)
 * ------------------------------------------------------------------------------------------------ */
extern int g_context;
extern int g_firedPrimary, g_firedAlt;
extern PlayerCommonImport g_com;
extern PlayerClientImport g_cl;
extern PlayerServerImport g_sv;
extern bool g_haveCl, g_haveSv;
extern WeaponDesc *g_desc[NUM_WEAPONS];

extern int g_forceServer;
inline bool IsClient() { return g_context != 0 && !g_forceServer; }

/* Game-only entry points (exports 7, 11) run as the server whatever the context flag says: the game also calls
   them from server console commands executed inside a client frame (`gamemap`, `load`), where the original
   module still uses the server imports (observed). The shared flag itself is left alone. */
struct ServerContext {
    ServerContext() { g_forceServer++; }
    ~ServerContext() { g_forceServer--; }
};
float Now();
IGhoulSystem *Ghoul();
int PRandom(int lo, int hi);
int PRandomAt(float now, int lo, int hi);
/* I1d test aid (pm_main.cpp): `om_pmtrace 1` */
bool PmTraceOn();
void PmTrace(const char *fmt, ...);
int PmInstId(IGhoulInst *inst);

/* tables (pm_tables.cpp) */
struct WeaponInfo {
    const char *cvar;      /* persistence name, NULL for hands */
    const char *dir;
    const char *display;
    int clip, weight, ammo1, ammo2;
    float modeValue1, modeValue2, spread1, spread2, max1, max2, kick1, kick2;
    int attack1, attack2, msgValue;
    bool muzzle, converge, hasAlt;
    const char *muzzleFx;  /* first-person muzzle effect precached with the model */
    const char *muzzleFx2;
};
extern const WeaponInfo kWeapons[NUM_WEAPONS];
extern const int kSlotOrder[NUM_WSLOTS];       /* slot -> weapon id */
extern const int kGiveOrder[12];               /* persistence give order */
extern const char *const kAmmoNames[NUM_AMMO];
extern const int kAmmoMax[NUM_AMMO];
extern const int kAmmoPickup[NUM_AMMO][2];
extern const char *const kItemNames[8];        /* cvar names (index = item type) */
extern const char *const kItemDisplay[8];
extern const int kItemStringId[8];
extern const int kBestSafe[12], kBestUnsafe[12], kKeepExplosive[3];
int SlotOfWeapon(int weapon);                  /* fixed slot of a weapon id, -1 if none */

/* view models (pm_viewmodel.cpp) */
IGhoulObject *ViewModel(int weapon, bool create);
void ForgetViewModel(int weapon);
void PrecacheViewModels(unsigned mask);
void ForgetViewModels();
void SetupViewInstance(IGhoulInst *inst, Inventory *inv, IGhoulInst *skinTarget = NULL);
extern IGhoulCallback *g_noteCallbacks[7];

/* persistence helpers (pm_persist.cpp) */
int WeaponByCvarName(const char *name);
int AmmoByName(const char *name);
int ItemByName(const char *name);

/* utilities */
int Q_stricmp(const char *a, const char *b);
void Q_strncpyz(char *dst, const char *src, int size);

} /* namespace om */
