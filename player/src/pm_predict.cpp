/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - inventory events (svc 28) on both sides and the client predictor's followers:
 * counters from the player state, the weapon-change follower and the predicted reload start.
 * Implements: SPEC-003 (events, svc 28), SPEC-602 (Behaviour 1, 3, 4, 5)
 */
#include <stdio.h>
#include "pm_local.h"

namespace om {

/* ---------------------------------------------------------------------------------------------- */
/* events                                                                                         */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::QueueEvent(int id, int value)
{
    if (!x || x->nout >= MAX_EVENTS)
        return;
    x->out[x->nout].id = id;
    x->out[x->nout].value = value;
    x->nout++;
}

void Inventory::ClearOutEvents()
{
    if (x)
        x->nout = 0;
}

/* slot 72: one block 0x1C, count, events into the owner's reliable stream */
void Inventory::SendEvents()
{
    if (IsClient() || !x || !x->nout || !g_haveSv)
        return;
    edict_t *o = Owner();
    if (!o)
        return;
    int client = *(int *)o - 1; /* entity number (entity state offset 0) - 1 */
    unsigned char buf[MAX_EVENTS * 5];
    int len = 0;
    for (int i = 0; i < x->nout; i++) {
        unsigned v = (unsigned)x->out[i].value;
        int cls = v <= 0xFFu ? 0 : v <= 0xFFFFu ? 1 : v <= 0xFFFFFFu ? 2 : 3;
        buf[len++] = (unsigned char)((cls << 4) | (x->out[i].id & 15));
        switch (cls) {
        case 0: buf[len++] = (unsigned char)v; break;
        case 1:
            buf[len++] = (unsigned char)v;
            buf[len++] = (unsigned char)(v >> 8);
            break;
        case 2:
            buf[len++] = (unsigned char)v;
            buf[len++] = (unsigned char)(v >> 8);
            buf[len++] = (unsigned char)(v >> 16);
            break;
        default:
            buf[len++] = (unsigned char)v;
            buf[len++] = (unsigned char)(v >> 8);
            buf[len++] = (unsigned char)(v >> 16);
            buf[len++] = (unsigned char)(v >> 24);
            break;
        }
    }
    if (g_sv.ReliableByte) {
        g_sv.ReliableByte(0x1C, client);
        g_sv.ReliableByte(x->nout, client);
    }
    if (g_sv.ReliableData)
        g_sv.ReliableData(buf, len, client);
}

void Inventory::ClearEvents()
{
    if (x)
        x->nin = 0;
}

/* slot 69: read count + events from the current incoming message */
void Inventory::ReadEvents()
{
    if (!g_haveCl || !g_cl.ReadByte)
        return;
    int count = g_cl.ReadByte();
    for (int i = 0; i < count; i++) {
        int h = g_cl.ReadByte();
        int cls = (h >> 4) & 3;
        int v;
        switch (cls) {
        case 0: v = g_cl.ReadByte(); break;
        case 1: v = g_cl.ReadShort() & 0xFFFF; break;
        case 2: {
            int lo = g_cl.ReadShort() & 0xFFFF;
            v = lo | (g_cl.ReadByte() << 16);
            break;
        }
        default: v = g_cl.ReadLong(); break;
        }
        if (x && x->nin < MAX_EVENTS) {
            x->in[x->nin].id = h & 15;
            x->in[x->nin].value = v;
            x->nin++;
        }
    }
}

/* slot 70 */
void Inventory::ApplyEvents()
{
    if (!x)
        return;
    for (int i = 0; i < x->nin; i++)
        ApplyEvent(x->in[i].id, x->in[i].value);
}

void Inventory::ApplyEvent(int id, int value)
{
    /* through the method table, as the original does (no events are queued in client context) */
    switch (id) {
    case 0: SetA(value); break;
    case 1: SetC(value); break;
    case 2: SetD(value); break;
    case 3: SetE(value); break;
    case 4: KnifeBlood(value); break;
    case 5: ToggleHolster(); break;
    default: break;
    }
}

/* ---------------------------------------------------------------------------------------------- */
/* client predictor                                                                               */
/* ---------------------------------------------------------------------------------------------- */
void Inventory::ClientCountersFromPS()
{
    unsigned char *ps = PS();
    if (!ps)
        return;
    if (st.curSlot >= 0 && st.curSlot < NUM_WSLOTS)
        st.slots[st.curSlot].clip = *(short *)(ps + PS_CLIP);
    int w = st.animWeapon;
    if (w > 0 && w < NUM_WEAPONS) {
        int type = kWeapons[w].ammo1;
        if (type > 0 && type < NUM_AMMO)
            st.ammo[type] = *(short *)(ps + PS_RESERVE);
    }
}

/* SPEC-602 Behaviour 4, client side */
void Inventory::SwitchFollower()
{
    unsigned char *ps = PS();
    IGhoulSystem *gs = Ghoul();
    if (!ps || !gs)
        return;
    int handle = *(unsigned short *)(ps + PS_VIEWID);
    IGhoulInst *si = handle ? gs->FindInstance(handle) : NULL;
    if (!si || si->GetUserValue())
        return;
    int w = *(unsigned short *)(ps + PS_VIEWTYPE);
    if (st.switchNow) {
        st.switchNow = 0;
        int slot = st.targetSlot;
        if (slot < 0 || slot >= NUM_WSLOTS)
            slot = SlotOfWeapon(w);
        if (slot < 0)
            return;
        DestroyInstance(st.inst);
        IGhoulInst *clone = si->Clone(true);
        si->SetUserValue(this);
        if (clone)
            SetupViewInstance(clone, this);
        st.inst = clone;
        st.curSlot = slot;
        st.animWeapon = st.slots[slot].weapon ? st.slots[slot].weapon : w;
        st.curWeapon = (st.animWeapon >= W_HURTHAND) ? 0 : st.animWeapon;
        st.lastSlot = slot;
        st.dropSlot = -1;
        /* SPEC-AMEND-006 I7.4 adoption: idle choice (unless holstered), visible rounds with the player-state clip,
           then the draw with its stance (the draw clears the synchronised flag and the "fired" exports) */
        if (!st.holstered)
            ChooseIdle();
        int c = *(short *)(ps + PS_CLIP);
        st.slots[slot].clip = c;
        VisibleRounds(true, false);
        StartDraw();
        return;
    }
    if (st.synced)
        return;
    int slot;
    int code;
    if (w == W_HURTHAND) {
        slot = HAND_HURT_SLOT;
        code = REQ_LOSE;
    } else if (w == W_THROWHAND) {
        slot = HAND_THROW_SLOT;
        code = REQ_DROP;
    } else {
        slot = SlotOfWeapon(w);
        code = slot >= 0 ? REQ_SLOT0 + slot : 0;
    }
    if (slot < 0)
        return;
    st.slots[slot].clip = *(short *)(ps + PS_CLIP);
    int type = (w > 0 && w < NUM_WEAPONS) ? kWeapons[w].ammo1 : 0;
    if (type > 0 && type < NUM_AMMO)
        st.ammo[type] = *(short *)(ps + PS_RESERVE);
    st.targetSlot = slot;
    st.pendingRequest = code;
    st.dictated = 1;
    st.synced = 1;
    ChooseIdle();
}

/* SPEC-602 Behaviour 3, client side */
void Inventory::PredictedReloadStart()
{
    st.spread = 0;
    int w = W();
    if (w <= 0 || w >= W_HURTHAND)
        return;
    const WeaponSlot &s = CurSlot();
    int size = kWeapons[w].clip;
    int type = kWeapons[w].ammo1;
    bool nothing = (s.clip >= size && !st.varE) || (type > 0 && type < NUM_AMMO && st.ammo[type] <= 0);
    if (nothing) {
        if (HasSeq(DrySeq()))
            DryFire();
        else
            GoIdle();
        return;
    }
    if (st.varC && ReloadSeq() && HasSeq(ReloadSeq())) {
        st.wstate = WS_RELOAD;
        st.ready = 0;
        Play(ReloadSeq());
    } else
        GoIdle();
}

} /* namespace om */
