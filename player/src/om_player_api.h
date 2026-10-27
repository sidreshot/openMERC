/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - the engine/game <-> player.dll boundary: import tables, export table, owner
 * context, sizebuf, item slot and the engine data the module touches (entity prefix, player state).
 * Owner of this header: package I7 (player module).
 * Implements: SPEC-003 (tables, owner context, events), SPEC-601 (item slot), SPEC-760 (entity prefix)
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "om_ghoul.h"

namespace om {

typedef struct edict_s edict_t; /* opaque; only the documented prefix offsets are used */

/* sizebuf_t of this interface: 20 bytes (SPEC-003). */
struct sizebuf_t {
    unsigned char allowoverflow;
    unsigned char overflowed;
    unsigned char pad[2];
    unsigned char *data;
    int maxsize;
    int cursize;
    int readcount;
};

struct OwnerContext;

/* Common import table, 19 pointers (SPEC-003). Only 5, 7, 12 and 17 are used. */
struct PlayerCommonImport {
    void *cvarGet;                                   /* 0 */
    void *cvarSet;                                   /* 1 */
    void *cvarSetValue;                              /* 2 */
    int (*Cmd_Argc)(void);                           /* 3 */
    char *(*Cmd_Argv)(int);                          /* 4 */
    char *(*Cmd_Args)(void);                         /* 5 */
    void (*Com_Printf)(const char *fmt, ...);        /* 6 */
    void (*Com_sprintf)(char *dst, int size, const char *fmt, ...); /* 7 */
    void (*Com_DPrintf)(const char *fmt, ...);       /* 8 */
    void *fsLoadFile;                                /* 9 */
    void *fsFreeFile;                                /* 10 */
    void *irand;                                     /* 11 */
    void *ghlSpecular;                               /* 12: cvar_t*, float value at +0x18 */
    void *newPlayerModelInfo;                        /* 13: undefined on the server side */
    void *loadGhoulFile;                             /* 14 */
    void *readLine;                                  /* 15 */
    void *precacheGsq;                               /* 16 */
    bool (*RegisterGsq)(const char *dir, const char *name, void *ghoulObject); /* 17 */
    void *applyGsqSkins;                             /* 18 */
};
static_assert(sizeof(PlayerCommonImport) == 76 || sizeof(void *) != 4, "SPEC-003: 76 bytes");

/* Client import table, 20 pointers (SPEC-003). */
struct PlayerClientImport {
    IGhoulSystem *(*GetGhoul)(void);                  /* 0 */
    float *time;                                     /* 1: client time in seconds */
    /* 2/3: predicted fire effects; the 1.07f module passes (owner context, owner, inventory) like the server's
       fire imports (observed), the engine derives the effect code (SPEC-400) itself */
    void (*FirePrimaryFx)(OwnerContext *ctx, edict_t *owner, void *inv); /* 2 */
    void (*FireAltFx)(OwnerContext *ctx, edict_t *owner, void *inv);     /* 3 */
    void (*NoteSound)(const char *text);             /* 4: note text only (observed) */
    void (*KillSound)(void);                         /* 5: never called */
    void (*NoteEffect)(const char *text);            /* 6: note text only (observed) */
    int (*ReadByte)(void);                           /* 7 */
    int (*ReadShort)(void);                          /* 8 */
    int (*ReadLong)(void);                           /* 9 */
    int (*SZReadByte)(sizebuf_t *);                  /* 10 */
    int (*SZReadShort)(sizebuf_t *);                 /* 11 */
    int (*SZReadLong)(sizebuf_t *);                  /* 12 */
    void (*SZReadData)(sizebuf_t *, void *dst, int len); /* 13 */
    void (*WriteByte)(sizebuf_t *, int);             /* 14 */
    void (*WriteShort)(sizebuf_t *, int);            /* 15 */
    void (*WriteLong)(sizebuf_t *, int);             /* 16 */
    void (*SZ_Init)(sizebuf_t *, unsigned char *data, int maxsize); /* 17 */
    void (*SZ_Clear)(sizebuf_t *);                   /* 18 */
    void (*SZ_Write)(sizebuf_t *, const void *, int); /* 19 */
};
static_assert(sizeof(PlayerClientImport) == 80 || sizeof(void *) != 4, "SPEC-003: 80 bytes");

struct ItemSlot; /* below */

/* Server import table, 38 pointers, supplied by the game module (SPEC-003, SPEC-782, ERRATA). */
struct PlayerServerImport {
    IGhoulSystem *(*GetGhoul)(void);                  /* 0 */
    float *time;                                     /* 1: level time in seconds */
    void (*FirePrimary)(void *unused, edict_t *owner, void *inv); /* 2 */
    void (*FireAlt)(void *unused, edict_t *owner, void *inv);     /* 3 */
    void (*ReloadStarted)(edict_t *owner);           /* 4 (ERRATA: reload started) */
    void (*DropWeapon)(edict_t *owner, int weaponId, int rounds); /* 5 */
    void (*ItemUsed)(edict_t *owner, int itemType);  /* 6 */
    void (*DropItem)(edict_t *owner, int itemType, int count); /* 7 */
    void (*PrecacheAttack)(int attackId);            /* 8 */
    int (*EffectIndex)(const char *name);            /* 9 */
    int (*SoundIndex)(const char *name);             /* 10 */
    void *startSound;                                /* 11: unused */
    bool (*CarriesFlag)(edict_t *owner);             /* 12 (ERRATA: owner carries the CTF flag) */
    float (*GetFireTimeA)(edict_t *owner);           /* 13: pending primary fire time (argument UNVERIFIED) */
    void (*ClearFireTimeA)(edict_t *owner);          /* 14 */
    float (*GetFireTimeB)(edict_t *owner);           /* 15: pending alternate fire time */
    void (*ClearFireTimeB)(edict_t *owner);          /* 16 */
    void (*Cbuf_AddText)(const char *text);          /* 17 */
    char *(*Cvar_BitInfo)(int flagmask);             /* 18 */
    void (*PrintId)(edict_t *to, int id, ...);       /* 19 */
    void *textOfId;                                  /* 20: unused */
    bool (*UseFlashpack)(edict_t *, ItemSlot *);     /* 21 */
    void *unused22;                                  /* 22 */
    bool (*UseC4)(edict_t *, ItemSlot *);            /* 23 */
    bool (*UseClaymore)(edict_t *, ItemSlot *);      /* 24 */
    bool (*UseMedkit)(edict_t *, ItemSlot *);        /* 25 */
    bool (*UseGrenade)(edict_t *, ItemSlot *);       /* 26 */
    bool (*Goggles)(edict_t *, ItemSlot *);          /* 27 */
    void (*WriteByte)(sizebuf_t *, int);             /* 28 */
    void (*WriteShort)(sizebuf_t *, int);            /* 29 */
    void (*WriteLong)(sizebuf_t *, int);             /* 30 */
    void (*ReliableByte)(int byteValue, int clientIndex); /* 31 */
    void (*ReliableData)(const void *data, int len, int clientIndex); /* 32 */
    void (*SZ_Init)(sizebuf_t *, unsigned char *data, int maxsize); /* 33 */
    void (*SZ_Clear)(sizebuf_t *);                   /* 34 */
    void (*SZ_Write)(sizebuf_t *, const void *, int); /* 35 */
    void (*WriteChunk)(int fourcc, const void *data, int len); /* 36 */
    void (*ReadChunk)(int fourcc, void *dst, int len, void **alloc); /* 37 */
};
static_assert(sizeof(PlayerServerImport) == 152 || sizeof(void *) != 4, "SPEC-003: 152 bytes");

/* Export table, 12 entries (SPEC-003). Function entries use the C calling convention. */
struct PlayerExport {
    int version;                                     /* 0: 1 */
    int *contextFlag;                                /* 1: 0 = server, non-zero = client predictor */
    int (*Init)(void);                               /* 2 */
    void (*Shutdown)(void);                          /* 3 */
    void **weaponDescs;                              /* 4: array of 15 weapon-description pointers */
    int *firedPrimary;                               /* 5 */
    int *firedAlt;                                   /* 6 */
    void (*PrecacheViewModels)(unsigned mask);       /* 7 */
    void (*ForgetViewModels)(void);                  /* 8 */
    void *(*NewInventory)(void);                     /* 9 */
    void (*DeleteInventory)(void *inv);              /* 10 */
    void (*RebuildAfterRestore)(void *inv);          /* 11 */
};
static_assert(sizeof(PlayerExport) == 48 || sizeof(void *) != 4, "SPEC-003: 48 bytes");

/* Owner context, 52 bytes, passed to inventory method 61 (SPEC-003, SPEC-602 Interface 4). */
struct OwnerContext {
    void *inventory;      /* 0x00 */
    edict_t *owner;       /* 0x04 */
    int attack;           /* 0x08 */
    int altAttack;        /* 0x0C */
    int extra1;           /* 0x10 */
    int extra2;           /* 0x14 */
    int leanPositive;     /* 0x18 */
    int leanNegative;     /* 0x1C */
    int cinematicFreeze;  /* 0x20 */
    int remoteView;       /* 0x24 */
    float *kick;          /* 0x28: weapon kick angles (pitch first) */
    float frameTen;       /* 0x2C: frame length x 10 */
    int reloadOneShot;    /* 0x30 */
};
static_assert(sizeof(OwnerContext) == 52 || sizeof(void *) != 4, "SPEC-003: 52 bytes");

/* Item slot, 16 bytes (SPEC-003, SPEC-601). */
struct ItemSlot {
    int type;
    int count;
    int state;
    float nextTime;
};
static_assert(sizeof(ItemSlot) == 16, "SPEC-601: 16 bytes");

/* Engine-visible entity prefix (SPEC-760 Interface 1), up to the fields the module reads. The offsets are
   the 32-bit ones of SPEC-760 on x86; on 64-bit targets the pointer members widen and the same member list
   gives the natural layout the engine (sof_edict_t) and the open game module (edict_s) also use. */
struct EdictPrefix {
    unsigned char s[116];          /* entity state, no pointers: loop sound at 0x3C, its data at 0x40 */
    void *client;                  /* 0x074 */
    int inuse;                     /* 0x078 */
    int linkcount;                 /* 0x07C */
    void *area_prev, *area_next;   /* 0x080 */
    int num_clusters;              /* 0x088 */
    int clusternums[32];           /* 0x08C */
    int headnode;                  /* 0x10C */
    int areanum, areanum2;         /* 0x110 */
    int svflags;                   /* 0x118 */
};
enum : int {
    EDICT_LOOP_SOUND = 0x3C,
    EDICT_LOOP_DATA = 0x40,
    EDICT_CLIENT = (int)offsetof(EdictPrefix, client),
    EDICT_SVFLAGS = (int)offsetof(EdictPrefix, svflags),
};
static_assert(EDICT_CLIENT == 0x74 || sizeof(void *) != 4, "SPEC-760: client at 0x74");
static_assert(EDICT_SVFLAGS == 0x118 || sizeof(void *) != 4, "SPEC-760: svflags at 0x118");

} /* namespace om */
