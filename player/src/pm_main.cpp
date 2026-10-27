/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - module entry points, export table, context flag, time, the time-seeded random
 * function and the weapon-description objects.
 * Implements: SPEC-003 (loading, export table, context flag), SPEC-600 (Interface 2, Behaviour 1)
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include "pm_local.h"

namespace om {

int g_context;
int g_forceServer;
int g_firedPrimary, g_firedAlt;
PlayerCommonImport g_com;
PlayerClientImport g_cl;
PlayerServerImport g_sv;
bool g_haveCl, g_haveSv;

/* I1d test aid: with the environment variable OM_PMTRACE=1 (the host's cvar tables differ per side, so no cvar)
   the module prints its inventory / view-instance / player-state steps through the common Com_Printf, prefixed
   "pm:" with the context ("S" server, "C" client) */
bool PmTraceOn()
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("OM_PMTRACE");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on == 1;
}

void PmTrace(const char *fmt, ...)
{
    if (!PmTraceOn() || !g_com.Com_Printf)
        return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    g_com.Com_Printf("pm:%c %s\n", IsClient() ? 'C' : 'S', buf);
}

/* the instance id for a trace line. Only with the trace on: the arguments of a PmTrace call are evaluated also when
   the trace is off, and a server inventory kept across a level change holds view instances the level's Ghoul reset
   freed - asking such an instance for its id was a call through freed memory (tsr2 -> trn1: `DeleteInventory` of
   the kept inventory crashed in about every second run, "execute of address 2F8ADF10") */
int PmInstId(IGhoulInst *inst) { return (inst && PmTraceOn()) ? (int)inst->GetInstanceId() : 0; }

/* ---------------------------------------------------------------------------------------------- */
/* weapon descriptions                                                                            */
/* ---------------------------------------------------------------------------------------------- */
int WeaponDesc::Slot00() { return 0; }
int WeaponDesc::Slot01() { return 0; }
int WeaponDesc::Slot02() { return 0; }
int WeaponDesc::Slot03() { return 0; }
int WeaponDesc::Slot04() { return 0; }
int WeaponDesc::Slot05() { return 0; }
int WeaponDesc::Slot06() { return 0; }
int WeaponDesc::Slot07() { return 0; }
int WeaponDesc::Slot08() { return 0; }
int WeaponDesc::Slot09() { return 0; }
int WeaponDesc::Slot10() { return 0; }
int WeaponDesc::Slot11() { return 0; }
int WeaponDesc::Slot12() { return 0; }
int WeaponDesc::Slot13() { return 0; }
int WeaponDesc::Slot14() { return 0; }
int WeaponDesc::Slot15() { return 0; }
int WeaponDesc::Slot16() { return 0; }
int WeaponDesc::Slot17() { return 0; }
int WeaponDesc::Slot18() { return 0; }
int WeaponDesc::Slot19() { return 0; }
int WeaponDesc::Slot20() { return 0; }
int WeaponDesc::Slot21() { return 0; }
int WeaponDesc::Slot22() { return 0; }
int WeaponDesc::Slot23() { return 0; }
int WeaponDesc::Slot24() { return 0; }
int WeaponDesc::Slot25() { return 0; }
int WeaponDesc::Slot26() { return 0; }
int WeaponDesc::Slot27() { return 0; }
int WeaponDesc::FiresFromMuzzle() { return muzzle ? 1 : 0; }
int WeaponDesc::Converges() { return converge ? 1 : 0; }
int WeaponDesc::Slot30() { return 0; }
int WeaponDesc::Slot31() { return 0; }

static WeaponDesc s_descStorage[NUM_WEAPONS];
WeaponDesc *g_desc[NUM_WEAPONS];

static void init_descs()
{
    static bool done;
    if (done)
        return;
    done = true;
    for (int w = 0; w < NUM_WEAPONS; w++) {
        const WeaponInfo &i = kWeapons[w];
        WeaponDesc *d = &s_descStorage[w];
        d->clip = i.clip;
        d->weight = i.weight;
        d->ammoType[0] = i.ammo1;
        d->ammoType[1] = i.ammo2;
        d->modeValue[0] = i.modeValue1;
        d->modeValue[1] = i.modeValue2;
        d->spreadPerShot[0] = i.spread1;
        d->spreadPerShot[1] = i.spread2;
        d->spreadMax[0] = i.max1;
        d->spreadMax[1] = i.max2;
        memset(d->dir, 0, sizeof(d->dir));
        Q_strncpyz(d->dir, i.dir, sizeof(d->dir));
        d->zero = 0;
        d->pad = 0;
        d->kick[0] = i.kick1;
        d->kick[1] = i.kick2;
        d->attack[0] = i.attack1;
        d->attack[1] = i.attack2;
        d->msgValue = i.msgValue;
        d->model = NULL;
        d->muzzle = i.muzzle;
        d->converge = i.converge;
        g_desc[w] = d;
    }
}

#if defined(_M_IX86)
static_assert(offsetof(WeaponDesc, clip) == 0x04, "desc clip");
static_assert(offsetof(WeaponDesc, spreadPerShot) == 0x1C, "desc spread");
static_assert(offsetof(WeaponDesc, dir) == 0x2C, "desc dir");
static_assert(offsetof(WeaponDesc, zero) == 0xAC, "desc zero");
static_assert(offsetof(WeaponDesc, kick) == 0xB0, "desc kick");
static_assert(offsetof(WeaponDesc, attack) == 0xB8, "desc attack");
static_assert(offsetof(WeaponDesc, msgValue) == 0xC0, "desc c0");
static_assert(offsetof(WeaponDesc, model) == 0xC4, "desc model");
#endif

/* ---------------------------------------------------------------------------------------------- */
/* time, Ghoul, random                                                                            */
/* ---------------------------------------------------------------------------------------------- */
float Now()
{
    if (IsClient())
        return (g_haveCl && g_cl.time) ? *g_cl.time : 0.f;
    return (g_haveSv && g_sv.time) ? *g_sv.time : 0.f;
}

IGhoulSystem *Ghoul()
{
    if (IsClient())
        return (g_haveCl && g_cl.GetGhoul) ? g_cl.GetGhoul() : NULL;
    return (g_haveSv && g_sv.GetGhoul) ? g_sv.GetGhoul() : NULL;
}

/* SPEC-600 Behaviour 1: n = trunc(now*10) (single precision), x = n*n (32-bit wrap),
   r = ((((x >> 5) ^ x) >> 3 ^ x) >> 4) & x, result lo + r % (hi - lo + 1) */
int PRandomAt(float now, int lo, int hi)
{
    if (hi <= lo)
        return lo;
    volatile float t = now * 10.0f;
    int n = (int)t;
    int x = (int)((unsigned)n * (unsigned)n);
    int a = (x >> 5) ^ x;
    int b = (a >> 3) ^ x;
    int r = (b >> 4) & x;
    return lo + r % (hi - lo + 1);
}

int PRandom(int lo, int hi) { return PRandomAt(Now(), lo, hi); }

int Q_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb)
            return ca - cb;
        if (!ca)
            return 0;
    }
}

void Q_strncpyz(char *dst, const char *src, int size)
{
    if (size <= 0)
        return;
    int i = 0;
    for (; i < size - 1 && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* ---------------------------------------------------------------------------------------------- */
/* exports                                                                                        */
/* ---------------------------------------------------------------------------------------------- */
static int Init(void) { return 1; }
static void Shutdown(void) {}

static void *NewInventory(void)
{
    Inventory *inv = new (std::nothrow) Inventory();
    if (inv)
        inv->Construct();
    PmTrace("NewInventory %p", inv);
    return inv;
}

static void DeleteInventory(void *p)
{
    Inventory *inv = (Inventory *)p;
    if (!inv)
        return;
    /* pointers, not ids: on the server path the instances may already be freed (see PmInstId) */
    PmTrace("DeleteInventory %p (inst %p next %p)", inv, (void *)inv->st.inst, (void *)inv->st.nextInst);
    inv->Destruct();
    delete inv;
}

static void RebuildAfterRestore(void *p);

static PlayerExport s_exports = {
    1, &g_context, Init, Shutdown, (void **)g_desc, &g_firedPrimary, &g_firedAlt,
    PrecacheViewModels, ForgetViewModels, NewInventory, DeleteInventory, RebuildAfterRestore,
};

/* SPEC-003 export 11: after a full restore, rebuild the owner's view weapon (server side only) */
static void RebuildAfterRestore(void *p)
{
    Inventory *inv = (Inventory *)p;
    if (!inv)
        return;
    ServerContext server;
    inv->st.inst = NULL;
    inv->st.nextInst = NULL;
    if (inv->x)
        inv->x->nextOwned = false;
    /* SPEC-AMEND-006 I7.16: the current weapon's cached model object is forgotten (not destroyed), then the
       ordinary selection runs without a draw: player state, a new model object (cached again, no precache),
       the instance, an idle choice unless holstered, the visible rounds; finally the scope hides the model */
    int slot = inv->st.curSlot;
    if (inv->st.curWeapon > 0 && slot >= 0 && slot < NUM_WSLOTS && inv->st.slots[slot].weapon == inv->st.curWeapon) {
        ForgetViewModel(inv->st.curWeapon);
        inv->SelectSlot(slot, false);
        inv->ScopeModel(inv->st.scoped != 0);
        return;
    }
    /* no selectable current weapon (hand pseudo-weapon or nothing): rebuild the animated one as it is */
    int w = inv->st.animWeapon;
    if (w > 0 && w < NUM_WEAPONS) {
        ForgetViewModel(w);
        inv->NewViewInstance(w, false);
        if (inv->st.inst && inv->st.seqName[0])
            inv->Play(inv->st.seqName);
    }
    inv->WritePSWeapon();
    inv->WriteCounters();
}

} /* namespace om */

using namespace om;

extern "C" PlayerExport *GetPlayerClientAPI(PlayerCommonImport *common, PlayerClientImport *client)
{
    init_descs();
    if (common)
        g_com = *common;
    if (client) {
        g_cl = *client;
        g_haveCl = true;
    }
    PmTrace("GetPlayerClientAPI (server side loaded %d)", (int)g_haveSv);
    return &s_exports;
}

/* OpenMerc marker export (see game/src/g_main.cpp, docs/DEVIATIONS.md "pure server") */
extern "C" const char *OpenMerc_Module(void)
{
	return "OpenMerc player module";
}

extern "C" PlayerExport *GetPlayerServerAPI(PlayerCommonImport *common, PlayerServerImport *server)
{
    init_descs();
    if (common)
        g_com = *common;
    if (server) {
        g_sv = *server;
        g_haveSv = true;
    }
    PmTrace("GetPlayerServerAPI (client side loaded %d)", (int)g_haveCl);
    return &s_exports;
}
