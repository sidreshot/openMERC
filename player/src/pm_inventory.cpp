/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - inventory object: construction, weapon slots, ammunition, armor, player variables,
 * simple queries, savegame chunk and full-state transfer.
 * Implements: SPEC-003 (inventory slots), SPEC-601 (Interface 1-3, Behaviour 1-3, 9)
 */
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include "pm_local.h"

namespace om {

void Inventory::Construct()
{
    x = new (std::nothrow) InvExtra;
    if (x)
        memset(x, 0, sizeof(*x));
    memset(priv08, 0, sizeof(priv08));
    memset(&st, 0, sizeof(st));
    /* defaults of a new inventory (SPEC-601 Interface 2/3, values observed on the 1.07f module) */
    st.animWeapon = W_KNIFE;
    st.dropSlot = -1;
    st.varA = -1;
    st.varB = -1;
    st.varI = 0x6001;
    st.varC = -1;
    st.varD = -1;
    st.varH = -1;
    Q_strncpyz(st.tag, "safe", sizeof(st.tag));
    st.ready = 1;
    st.lastSlot = -1;
    st.p1Stance = 2;
    st.transitionLast = 1;
    st.prepaid = 1;
    Q_strncpyz(st.seqName, "unused viewmodel", sizeof(st.seqName));
    if (IsClient()) {
        /* SPEC-602 Behaviour 6: the predictor owns every weapon of the slot order with empty clips */
        for (int i = 0; i < NUM_WSLOTS; i++)
            st.slots[i].weapon = kSlotOrder[i];
        st.owned = 0x7FFE;
    }
    /* The original leaves the idle-variation counter uninitialised (and decrements it once when it is not 0): a
       predicting client's inventories and a dedicated server's start with heap text there (0x57455448,
       0x434F524F, ... observed, one less for every inventory created), so they never choose an idle variation or a
       stance transition; single-player (listen) server inventories got fresh memory, 0. Reproduced with a large
       count where the original had heap text (docs/impl/I7.md, OPEN_QUESTIONS I7-2). */
    if (IsClient() || !g_haveCl)
        st.idleCounter = 0x40000000;
}

void Inventory::Destruct()
{
    /* Server inventories must not call Ghoul Destroy here: after GhoulResetLevel the
       view-instance pointers are stale, and a listen server may still have the client
       context flag set when ClientBegin runs (MODEB-GAPS crash B). The game clears the
       flag around PlayerDeleteInventory and the engine clears it around ClientBegin, so
       IsClient() is false on that path and we just abandon the pointers. The client
       prediction path sets the flag and destroys live instances. */
    if (IsClient()) {
        DestroyInstance(st.inst);
        DestroyInstance(st.nextInst);
    } else {
        st.inst = NULL;
        st.nextInst = NULL;
    }
    delete x;
    x = NULL;
}

/* ---------------------------------------------------------------------------------------------- */
/* slot 0, 1: clearing                                                                            */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::Clear(bool alsoFlag)
{
    for (int i = 0; i < NUM_WSLOTS; i++) {
        st.slots[i].clip = 0;
        st.slots[i].weapon = 0;
    }
    for (int i = 0; i < NUM_AMMO; i++)
        st.ammo[i] = 0;
    for (int i = 0; i < NUM_ISLOTS; i++) {
        if (st.items[i].type == IT_FLAG && !alsoFlag)
            continue;
        memset(&st.items[i], 0, sizeof(ItemSlot));
    }
    st.armor = 0;
    st.pendingRequest = 0;
}

void Inventory::RemoveItemType(int type)
{
    for (int i = 0; i < NUM_ISLOTS; i++)
        if (st.items[i].type == type)
            memset(&st.items[i], 0, sizeof(ItemSlot));
}

/* ---------------------------------------------------------------------------------------------- */
/* weapons and ammunition                                                                         */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::TopUpClips()
{
    for (int i = 0; i < NUM_SELECTABLE; i++) {
        WeaponSlot &s = st.slots[i];
        int w = s.weapon;
        if (w <= 0 || w >= NUM_WEAPONS)
            continue;
        int size = kWeapons[w].clip;
        int type = kWeapons[w].ammo1;
        if (size <= 0 || type <= 0 || type == AM_FUEL)
            continue;
        if (!Ammo()) {
            s.clip = size;
            continue;
        }
        if (st.varE) {
            if (st.ammo[type] >= size) {
                st.ammo[type] -= size;
                s.clip = size;
            } else if (s.clip < size) {
                s.clip = st.ammo[type];
                st.ammo[type] = 0;
            }
        } else {
            int need = size - s.clip;
            if (need > st.ammo[type])
                need = st.ammo[type];
            if (need > 0) {
                s.clip += need;
                st.ammo[type] -= need;
            }
        }
    }
}

int Inventory::AddAmmo(int type, int amount)
{
    if (type <= 0 || type >= NUM_AMMO - 1)
        return false;
    if (type == AM_FUEL) {
        /* SPEC-601 Behaviour 2: fuel goes straight into the flamethrower slot's clip */
        int &clip = st.slots[SlotOfWeapon(W_FLAMEGUN)].clip;
        if (clip >= kAmmoMax[AM_FUEL])
            return false;
        clip += amount;
        if (clip > kAmmoMax[AM_FUEL])
            clip = kAmmoMax[AM_FUEL];
        return true;
    }
    if (st.ammo[type] >= kAmmoMax[type])
        return false;
    st.ammo[type] += amount;
    if (st.ammo[type] > kAmmoMax[type])
        st.ammo[type] = kAmmoMax[type];
    return true;
}

int Inventory::WeaponMessageId(int weapon)
{
    if (weapon < 0 || weapon >= NUM_WEAPONS)
        return 20000;
    return kWeapons[weapon].msgValue + 20000;
}

int Inventory::AddAmmoForWeapon(int weapon, int amount)
{
    if (weapon <= 0 || weapon >= NUM_WEAPONS)
        return false;
    int type = kWeapons[weapon].ammo1;
    if (type <= 0)
        return false;
    if (amount == 0) {
        if (weapon == W_MPISTOL)
            amount = PRandom(8, 15);
        else
            amount = PRandom(kAmmoPickup[type][0], kAmmoPickup[type][1]);
    }
    return AddAmmo(type, amount);
}

int Inventory::AddAmmoForWeapon2(int weapon, int amount) { return AddAmmoForWeapon(weapon, amount); }

int Inventory::GiveWeapon(int weapon, int clip)
{
    int slot = SlotOfWeapon(weapon);
    if (slot < 0)
        return false;
    if (st.slots[slot].weapon == weapon)
        return false;
    st.slots[slot].weapon = weapon;
    if (clip == -1)
        clip = kWeapons[weapon].clip > 0 ? kWeapons[weapon].clip : 0;
    if (!Ammo())
        clip = 0;
    st.slots[slot].clip = clip;
    st.owned |= 1u << weapon;
    return true;
}

int Inventory::TotalWeight()
{
    int total = 0;
    for (int i = 0; i < NUM_SELECTABLE; i++) {
        int w = st.slots[i].weapon;
        if (w > 0 && w < NUM_WEAPONS)
            total += kWeapons[w].weight;
    }
    return total;
}

int Inventory::WeaponWeight(int weapon)
{
    return (weapon > 0 && weapon < NUM_WEAPONS) ? kWeapons[weapon].weight : 0;
}

int Inventory::OwnsWeapon(int weapon)
{
    if (weapon < 0 || weapon >= 32)
        return 0;
    return (int)(st.owned & (1u << weapon)); /* the flag bit itself (SPEC-AMEND-006 I7.13) */
}

int Inventory::CurrentWeapon() { return st.curWeapon; }

int Inventory::FindSlotOfWeapon(int weapon)
{
    for (int i = 0; i < NUM_SELECTABLE; i++)
        if (st.slots[i].weapon == weapon && weapon)
            return i;
    return -1;
}

int Inventory::CurrentReserve()
{
    int w = st.animWeapon;
    if (w <= 0 || w >= NUM_WEAPONS)
        return 0;
    int type = kWeapons[w].ammo1;
    return (type > 0 && type < NUM_AMMO) ? st.ammo[type] : 0;
}

int Inventory::CurrentClip()
{
    if (st.curSlot < 0 || st.curSlot >= NUM_WSLOTS)
        return 0;
    return st.slots[st.curSlot].clip;
}

int Inventory::CurrentClipSize() { return ClipSize(st.curWeapon); }

int Inventory::ClipSize(int weapon)
{
    if (weapon < 0 || weapon >= NUM_WEAPONS)
        return 0;
    return kWeapons[weapon].clip;
}

int Inventory::WeaponCount()
{
    int n = 0;
    for (int i = 0; i < NUM_SELECTABLE; i++)
        if (st.slots[i].weapon)
            n++;
    return n;
}

/* ---------------------------------------------------------------------------------------------- */
/* armor (SPEC-601 Behaviour 3)                                                                   */
/* ---------------------------------------------------------------------------------------------- */
int Inventory::AddArmor(int delta)
{
    if ((st.armor >= 100 && delta >= 0) || (st.armor <= 0 && delta <= 0))
        return 0;
    st.armor += delta;
    if (st.armor > 100)
        st.armor = 100;
    if (st.armor < 0)
        st.armor = 0;
    return st.armor;
}

int Inventory::AbsorbDamage(int damage, float mode, float fraction)
{
    if (st.armor <= 0)
        return damage;
    int a = (int)((float)damage - (float)damage * fraction);
    if (mode == 0.f) {
        if (st.armor > a) {
            st.armor -= a;
            return 0;
        }
        int r = a - st.armor;
        st.armor = 0;
        return r;
    }
    if (st.armor > 2 * a) {
        st.armor -= 2 * a;
        return 0;
    }
    int r = (int)((float)(2 * a - st.armor) * 0.5f);
    st.armor = 0;
    return r;
}

int Inventory::Armor() { return st.armor; }

/* ---------------------------------------------------------------------------------------------- */
/* items queries                                                                                  */
/* ---------------------------------------------------------------------------------------------- */
int Inventory::HasItem(int type)
{
    for (int i = 0; i < NUM_ISLOTS; i++)
        if (st.items[i].type == type && st.items[i].count > 0)
            return true;
    return false;
}

int Inventory::SelectedItemType()
{
    if (st.selItem < 0 || st.selItem >= NUM_ISLOTS)
        return 0;
    return st.items[st.selItem].type;
}

int Inventory::SelectedItemCount()
{
    if (st.selItem < 0 || st.selItem >= NUM_ISLOTS)
        return 0;
    return st.items[st.selItem].count;
}

int Inventory::ScopeActive() { return st.scoped; }

/* ---------------------------------------------------------------------------------------------- */
/* player variables (SPEC-601 Interface 2; replicated ones queue events 0-3)                      */
/* ---------------------------------------------------------------------------------------------- */
int Inventory::GetA() { return st.varA; }
void Inventory::SetA(int v)
{
    st.varA = v;
    if (!IsClient())
        QueueEvent(0, v);
}
int Inventory::GetB() { return st.varB; }
void Inventory::SetB(int v) { st.varB = v; }
int Inventory::GetI() { return st.varI; }
void Inventory::SetI(int v) { st.varI = v | 0x6001; }
int Inventory::GetC() { return st.varC; }
void Inventory::SetC(int v)
{
    st.varC = v;
    if (!IsClient())
        QueueEvent(1, v);
}
int Inventory::GetD() { return st.varD; }
void Inventory::SetD(int v)
{
    st.varD = v;
    if (!IsClient())
        QueueEvent(2, v);
}
int Inventory::GetE() { return st.varE; }
void Inventory::SetE(int v)
{
    st.varE = v;
    if (!IsClient())
        QueueEvent(3, v);
}
const char *Inventory::GetTag() { return st.tag; }
void Inventory::SetTag(const char *tag)
{
    memset(st.tag, 0, 5);
    if (tag)
        for (int i = 0; i < 4 && tag[i]; i++)
            st.tag[i] = tag[i];
}
int Inventory::GetF() { return st.varF; }
void Inventory::SetF(int v) { st.varF = v; }
int Inventory::GetG() { return st.varG; }
void Inventory::SetG(int v) { st.varG = v; }
int Inventory::GetH() { return st.varH; }
void Inventory::SetH(int v) { st.varH = v; }

void Inventory::SetOwnerContext(OwnerContext *ctx)
{
    PmTrace("SetOwnerContext inv %p ctx %p owner %p", this, ctx, ctx ? ctx->owner : NULL);
    st.ctx = ctx;
}
void Inventory::SetOwnerPredicts(int v) { st.ownerPredicts = v; }
int Inventory::GetOwnerPredicts() { return st.ownerPredicts; }

bool Inventory::PredictingOwner() const { return !IsClient() && st.ownerPredicts != 0; }

unsigned char *Inventory::PS() const
{
    edict_t *e = Owner();
    if (!e)
        return NULL;
    return *(unsigned char **)((char *)e + EDICT_CLIENT);
}

/* ---------------------------------------------------------------------------------------------- */
/* savegame and full state (SPEC-601 Behaviour 9)                                                 */
/* ---------------------------------------------------------------------------------------------- */
static const int kInvnFourcc = 0x494E564E; /* bytes "NVNI" in file order */

/* the game calls 75/76 whatever the context flag says (a console `save` runs in a client frame) */
void Inventory::WriteSave()
{
    if (!g_haveSv || !g_sv.WriteChunk)
        return;
    InvState copy = st;
    copy.inst = NULL;
    copy.nextInst = NULL;
    copy.ctx = NULL;
    g_sv.WriteChunk(kInvnFourcc, &copy, sizeof(copy));
}

void Inventory::ReadSave()
{
    if (!g_haveSv || !g_sv.ReadChunk)
        return;
    g_sv.ReadChunk(kInvnFourcc, &st, sizeof(st), NULL);
    st.inst = NULL;
    st.nextInst = NULL;
    st.ctx = NULL;
}

void Inventory::ReadFullState(sizebuf_t *msg)
{
    if (!g_haveCl || !g_cl.SZReadData || !msg)
        return;
    IGhoulInst *keep = st.inst, *keepNext = st.nextInst;
    g_cl.SZReadData(msg, &st, sizeof(st));
    /* in-view instances are dropped: ours are destroyed, the received pointers are meaningless */
    st.inst = keep;
    st.nextInst = keepNext;
    DestroyInstance(st.inst);
    DestroyInstance(st.nextInst);
    st.ctx = NULL;
    st.ownerPredicts = 0;
}

/* slot 78: the 640 live bytes into the client's reliable stream (the caller writes the svc 30 byte, observed) */
void Inventory::SendFullState(int clientIndex)
{
    if (!g_haveSv || !g_sv.ReliableData)
        return;
    g_sv.ReliableData(&st, sizeof(st), clientIndex);
}

} /* namespace om */
