/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - persistence of the inventory across levels (cvars) and as info strings.
 * Implements: SPEC-003 (persistence), SPEC-601 (Behaviour 7, 8)
 */
#include <stdio.h>
#include <stdlib.h>
#include "pm_local.h"

namespace om {

int WeaponByCvarName(const char *name)
{
    for (int w = 1; w <= 12; w++)
        if (!Q_stricmp(name, kWeapons[w].cvar))
            return w;
    return 0;
}

int AmmoByName(const char *name)
{
    for (int a = 1; a <= 9; a++)
        if (!Q_stricmp(name, kAmmoNames[a]))
            return a;
    return 0;
}

int ItemByName(const char *name)
{
    for (int i = 1; i <= 6; i++)
        if (kItemNames[i] && !Q_stricmp(name, kItemNames[i]))
            return i;
    return 0;
}

static void cbuf(const char *fmt, const char *name, int value)
{
    if (!g_haveSv || !g_sv.Cbuf_AddText)
        return;
    char text[256];
    if (g_com.Com_sprintf)
        g_com.Com_sprintf(text, sizeof(text), fmt, name, value);
    else
        _snprintf(text, sizeof(text), fmt, name, value);
    g_sv.Cbuf_AddText(text);
}

/* Parses "\name\value\name\value..." (leading backslash optional); calls fn for every pair. */
template <class Fn> static void for_pairs(const char *s, Fn fn)
{
    if (!s)
        return;
    if (*s == '\\')
        s++;
    char name[128], value[128];
    while (*s) {
        int n = 0;
        while (*s && *s != '\\' && n < 127)
            name[n++] = *s++;
        name[n] = 0;
        if (*s == '\\')
            s++;
        int v = 0;
        while (*s && *s != '\\' && v < 127)
            value[v++] = *s++;
        value[v] = 0;
        if (*s == '\\')
            s++;
        if (name[0])
            fn(name, value);
    }
}

/* slot 64 */
int Inventory::LoadFromCvars(unsigned *maskOut, int *healthOut)
{
    if (IsClient() || !g_haveSv || !g_sv.Cvar_BitInfo)
        return 0;
    const char *info = g_sv.Cvar_BitInfo(0xF00);
    if (!info || !info[0])
        return 0;
    unsigned mask = 0;
    for_pairs(info, [&](const char *name, const char *value) {
        int v = atoi(value);
        int w = WeaponByCvarName(name);
        if (w) {
            if (v) {
                mask |= 1u << w;
                cbuf("set %s 0 w;", name, 0);
            }
            return;
        }
        int a = AmmoByName(name);
        if (a) {
            AddAmmo(a, v);
            if (a == AM_FUEL)
                st.ammo[AM_FUEL] = v;
            cbuf("set %s 0 a;", name, 0);
            return;
        }
        int it = ItemByName(name);
        if (it) {
            if (v) {
                AddItems(it, v, -1);
                cbuf("set %s 0 i;", name, 0);
            }
            return;
        }
        if (!Q_stricmp(name, "armor"))
            AddArmor(v);
        cbuf("set %s 0 i;", "armor", 0);
        if (!Q_stricmp(name, "health") && v) {
            if (healthOut)
                *healthOut = v;
            cbuf("set %s 0 m;", "health", 0);
        }
    });
    for (int i = 0; i < 12; i++)
        if (mask & (1u << kGiveOrder[i]))
            GiveWeapon(kGiveOrder[i], 0);
    if (maskOut)
        *maskOut |= mask; /* the game's mask already holds other weapons */
    return st.ammo[AM_KNIFE];
}

/* slot 65 */
void Inventory::SaveToCvars(bool addClips, int health)
{
    if (IsClient())
        return;
    for (int i = 0; i < NUM_SELECTABLE; i++) {
        int w = st.slots[i].weapon;
        if (w <= 0 || w > 12)
            continue;
        if (addClips) {
            int type = kWeapons[w].ammo1;
            if (type > 0 && type < NUM_AMMO)
                st.ammo[type] += st.slots[i].clip;
        }
        cbuf("set %s %d w;", kWeapons[w].cvar, 1);
    }
    for (int a = 1; a <= 9; a++)
        cbuf("set %s %d a;", kAmmoNames[a], st.ammo[a]);
    for (int i = 0; i < NUM_ISLOTS; i++) {
        const ItemSlot &s = st.items[i];
        if (s.type > 0 && s.type <= 6 && s.count > 0)
            cbuf("set %s %d i;", kItemNames[s.type], s.count);
    }
    cbuf("set %s %d i;", "armor", st.armor);
    cbuf("set %s %d m;", "health", health);
}

/* slot 66 */
int Inventory::LoadFromInfo(unsigned *maskOut, const char *info)
{
    g_context = 0; /* SPEC-601 Behaviour 7 */
    if (!info || !info[0])
        return false;
    unsigned mask = 0;
    for_pairs(info, [&](const char *name, const char *value) {
        int v = atoi(value);
        int w = WeaponByCvarName(name);
        if (w) {
            if (v)
                mask |= 1u << w;
            return;
        }
        int a = AmmoByName(name);
        if (a) {
            AddAmmo(a, v);
            if (a == AM_FUEL)
                st.ammo[AM_FUEL] = v;
            return;
        }
        int it = ItemByName(name);
        if (it) {
            if (v)
                AddItems(it, v, -1);
            return;
        }
        if (!Q_stricmp(name, "armor"))
            AddArmor(v);
    });
    for (int i = 0; i < 12; i++)
        if (mask & (1u << kGiveOrder[i]))
            GiveWeapon(kGiveOrder[i], 0);
    if (maskOut)
        *maskOut |= mask;
    return true;
}

/* slot 67 */
void Inventory::WriteInfo(char *out, bool weapons, bool ammo, bool items, bool armor)
{
    if (!out)
        return;
    out[0] = 0;
    char part[128];
    size_t len = 0;
    auto add = [&](const char *s) {
        size_t n = strlen(s);
        memcpy(out + len, s, n + 1);
        len += n;
    };
    if (weapons) {
        for (int i = 0; i < NUM_SELECTABLE; i++) {
            int w = st.slots[i].weapon;
            if (w <= 0 || w > 12)
                continue;
            int type = kWeapons[w].ammo1;
            if (type > 0 && type < NUM_AMMO)
                st.ammo[type] += st.slots[i].clip;
            _snprintf(part, sizeof(part), "%s\\1\\", kWeapons[w].cvar);
            add(part);
        }
    }
    if (ammo) {
        for (int a = 1; a <= 9; a++) {
            _snprintf(part, sizeof(part), "%s\\%d\\", kAmmoNames[a], st.ammo[a]);
            add(part);
        }
    }
    if (items) {
        for (int i = 0; i < NUM_ISLOTS; i++) {
            const ItemSlot &s = st.items[i];
            if (s.type > 0 && s.type <= 6) {
                _snprintf(part, sizeof(part), "%s\\%d\\", kItemNames[s.type], s.count);
                add(part);
            }
        }
    }
    if (armor) {
        _snprintf(part, sizeof(part), "armor\\%d\\", st.armor);
        add(part);
    }
}

} /* namespace om */
