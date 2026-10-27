/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - weapon and item console commands, item slots, item use, dropping, goggles.
 * Implements: SPEC-003 (slot 4 commands), SPEC-601 (Behaviour 4, 6; Interface 4)
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "pm_local.h"

namespace om {

/* ---------------------------------------------------------------------------------------------- */
/* slot 4: commands (server context only)                                                         */
/* ---------------------------------------------------------------------------------------------- */
static void first_token(const char *s, char *out, int size)
{
    while (s && *s == ' ')
        s++;
    int i = 0;
    while (s && s[i] && s[i] != ' ' && i < size - 1) {
        out[i] = s[i];
        i++;
    }
    out[i] = 0;
}

static int item_type_by_use_name(const char *name)
{
    static const struct {
        const char *name;
        int type;
    } names[] = {{"fpak", IT_FPAK}, {"c4", IT_C4}, {"goggles", IT_GOGGLES}, {"medkit", IT_MEDKIT},
                 {"grenade", IT_GRENADE}, {"ctf_flag", IT_FLAG}};
    for (const auto &n : names)
        if (!Q_stricmp(name, n.name))
            return n.type;
    return 0;
}

int Inventory::Command(const char *cmd, int force)
{
    if (!cmd)
        return false;
    /* the arguments are fetched first, also in client context, where nothing else happens (observed) */
    const char *args = g_com.Cmd_Args ? g_com.Cmd_Args() : "";
    if (IsClient())
        return false;
    if (!args)
        args = "";
    const bool may = force || st.varH;
    int code = -1;
    if (!Q_stricmp(cmd, "reload"))
        code = REQ_RELOAD;
    else if (!Q_stricmp(cmd, "weapnext"))
        code = may ? REQ_NEXT : 0;
    else if (!Q_stricmp(cmd, "weapprev"))
        code = may ? REQ_PREV : 0;
    else if (!Q_stricmp(cmd, "weaponbestsafe"))
        code = may ? REQ_BESTSAFE : 0;
    else if (!Q_stricmp(cmd, "weaponbestunsafe"))
        code = may ? REQ_BESTUNSAFE : 0;
    else if (!Q_stricmp(cmd, "weaponlose"))
        code = st.varB ? REQ_LOSE : 0;
    else if (!Q_stricmp(cmd, "weapondrop"))
        code = st.varB ? REQ_DROP : 0;
    else if (!Q_stricmp(cmd, "weaponselect")) {
        char tok[32];
        first_token(args, tok, sizeof(tok));
        code = (may && tok[0]) ? 7 + atoi(tok) : 0;
    } else if (!Q_stricmp(cmd, "itemuse")) {
        char tok[64];
        first_token(args, tok, sizeof(tok));
        if (!tok[0]) {
            st.itemRequest = IREQ_USE;
            return true;
        }
        int type = item_type_by_use_name(tok);
        if (!type)
            return false;
        if (st.ready < 0)
            return true;
        int slot = FindItemSlot(type);
        if (slot >= 0)
            st.itemRequest = IREQ_USESLOT0 + slot;
        return true;
    } else if (!Q_stricmp(cmd, "itemnext")) {
        st.itemRequest = IREQ_NEXT;
        return true;
    } else if (!Q_stricmp(cmd, "itemprev")) {
        st.itemRequest = IREQ_PREV;
        return true;
    } else if (!Q_stricmp(cmd, "itemdrop")) {
        st.itemRequest = IREQ_DROP;
        return true;
    } else
        return false;
    if (st.ready < 0)
        return true; /* a hand pseudo-weapon is finishing: swallowed */
    if (code > 0)
        st.pendingRequest = code;
    return true;
}

/* ---------------------------------------------------------------------------------------------- */
/* item slots                                                                                     */
/* ---------------------------------------------------------------------------------------------- */
int Inventory::FindItemSlot(int type)
{
    for (int i = 0; i < NUM_ISLOTS; i++)
        if (st.items[i].type == type && st.items[i].count > 0)
            return i;
    return -1;
}

int Inventory::AddItems(int type, int count, int max)
{
    if (type <= 0 || type > IT_FLAG)
        return false;
    const bool selEmpty = st.selItem < 0 || st.selItem >= NUM_ISLOTS || st.items[st.selItem].count <= 0;
    int slot = -1;
    for (int i = 0; i < NUM_ISLOTS && slot < 0; i++)
        if (st.items[i].type == type)
            slot = i;
    if (slot < 0) {
        for (int i = 0; i < NUM_ISLOTS && slot < 0; i++)
            if (!st.items[i].type || st.items[i].count <= 0)
                slot = i;
        if (slot < 0)
            return false;
        memset(&st.items[slot], 0, sizeof(ItemSlot));
        st.items[slot].type = type;
    }
    ItemSlot &s = st.items[slot];
    if (max >= 0 && s.count >= max)
        return false;
    s.count += count;
    if (max >= 0 && s.count > max)
        s.count = max;
    if (selEmpty)
        st.itemRequest = IREQ_NEXT;
    return true;
}

void Inventory::AdvanceItemSelection(int dir)
{
    int start = st.selItem;
    if (start < 0 || start >= NUM_ISLOTS)
        start = 0;
    for (int k = 1; k <= NUM_ISLOTS; k++) {
        int i = ((start + dir * k) % NUM_ISLOTS + NUM_ISLOTS) % NUM_ISLOTS;
        if (st.items[i].type && st.items[i].count > 0) {
            st.selItem = i;
            return;
        }
    }
}

/* goggles switch-off rule (also used by slot 59 with the selected slot, SPEC-601 Behaviour 4 quirk) */
static void goggles_off(Inventory *inv, ItemSlot *slot)
{
    slot->state = 0;
    if (!IsClient() && g_haveSv && g_sv.Goggles)
        g_sv.Goggles(inv->Owner(), slot);
}

void Inventory::ItemsSwitchOff()
{
    if (st.selItem < 0 || st.selItem >= NUM_ISLOTS)
        return;
    for (int i = 0; i < NUM_ISLOTS; i++)
        if (st.items[i].type == IT_GOGGLES && st.items[i].count >= 0)
            goggles_off(this, &st.items[st.selItem]);
}

static void consumed_message(Inventory *inv, int type, int left)
{
    if (IsClient() || !g_haveSv || !g_sv.PrintId)
        return;
    if (left <= 0)
        g_sv.PrintId(inv->Owner(), 0x30D, kItemDisplay[type]);
    else
        g_sv.PrintId(inv->Owner(), 0x30E, left, kItemDisplay[type]);
}

void Inventory::UseItemSlot(int slot)
{
    if (slot < 0 || slot >= NUM_ISLOTS)
        return;
    ItemSlot &s = st.items[slot];
    if (!s.type || s.count <= 0)
        return;
    if (st.holstered && s.type != IT_MEDKIT)
        ToggleHolster();
    if (s.type == IT_FLAG)
        return;
    const bool client = IsClient();
    bool used = true;
    if (!client && g_haveSv) {
        if (g_sv.ItemUsed)
            g_sv.ItemUsed(Owner(), s.type);
        switch (s.type) {
        case IT_FPAK: used = g_sv.UseFlashpack && g_sv.UseFlashpack(Owner(), &s); break;
        case IT_C4: used = g_sv.UseC4 && g_sv.UseC4(Owner(), &s); break;
        case IT_CLAYMORE: used = g_sv.UseClaymore && g_sv.UseClaymore(Owner(), &s); break;
        case IT_MEDKIT: used = g_sv.UseMedkit && g_sv.UseMedkit(Owner(), &s); break;
        case IT_GRENADE: used = g_sv.UseGrenade && g_sv.UseGrenade(Owner(), &s); break;
        case IT_GOGGLES: {
            float now = Now();
            s.state = s.state ? 0 : 1;
            s.nextTime = (float)((double)now + (s.state ? 0.3 : 0.5)); /* double, then single (I7.11) */
            if (g_sv.Goggles)
                g_sv.Goggles(Owner(), &s);
            used = false;
            break;
        }
        default: break;
        }
    }
    if (used && s.type != IT_GOGGLES) {
        s.count--;
        consumed_message(this, s.type, s.count);
        if (s.count <= 0) {
            memset(&s, 0, sizeof(ItemSlot));
            if (slot == st.selItem)
                AdvanceItemSelection(1);
        }
    }
}

void Inventory::DropSelectedItem()
{
    int slot = st.selItem;
    if (slot < 0 || slot >= NUM_ISLOTS)
        return;
    ItemSlot &s = st.items[slot];
    if (!s.type || s.count <= 0)
        return;
    if (s.type == IT_GOGGLES)
        goggles_off(this, &s);
    bool whole = s.type == IT_GOGGLES || s.type == IT_FLAG;
    int n = whole ? s.count : 1;
    if (!IsClient() && g_haveSv && g_sv.DropItem)
        g_sv.DropItem(Owner(), s.type, n);
    s.count -= n;
    if (!whole)
        consumed_message(this, s.type, s.count);
    if (s.count <= 0) {
        memset(&s, 0, sizeof(ItemSlot));
        AdvanceItemSelection(1);
    }
}

void Inventory::ProcessItemRequest()
{
    int req = st.itemRequest;
    if (!req)
        return;
    switch (req) {
    case IREQ_NEXT:
        st.itemRequest = 0;
        AdvanceItemSelection(1);
        return;
    case IREQ_PREV:
        st.itemRequest = 0;
        AdvanceItemSelection(-1);
        return;
    case IREQ_DROP:
        st.itemRequest = 0;
        DropSelectedItem();
        return;
    default: break;
    }
    /* use: wait until now >= T + 0.4 (T = previous use attempt, whole seconds) */
    float now = Now();
    if (now < (float)st.lastItemUse + 0.4f)
        return;
    st.itemRequest = 0;
    UseItemSlot(req == IREQ_USE ? st.selItem : req - IREQ_USESLOT0);
    st.lastItemUse = (int)now;
}

void Inventory::ItemsThink()
{
    ProcessItemRequest();
    if (IsClient())
        return;
    float now = Now();
    for (int i = 0; i < NUM_ISLOTS; i++) {
        ItemSlot &s = st.items[i];
        if (s.type != IT_GOGGLES)
            continue;
        if (!(s.nextTime < now)) /* strictly less (SPEC-AMEND-006 I7.11) */
            continue;
        if (s.state) {
            s.count--;
            s.nextTime = (float)((double)now + 0.3);
            if (s.count <= 0) {
                s.count = 0;
                goggles_off(this, &s);
            }
        } else {
            if (s.count < 100)
                s.count++;
            s.nextTime = (float)((double)now + 0.5);
        }
    }
}

} /* namespace om */
