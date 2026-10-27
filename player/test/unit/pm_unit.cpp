/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - host unit tests of rules that need no engine: the time-seeded random function, armor,
 * ammunition limits, item slots, the state-block layout.
 * Implements: SPEC-600 (Behaviour 1 test vectors), SPEC-601 (Behaviour 2-4 test vectors, Interface 3)
 */
#include <stdio.h>
#include <string.h>
#include "../../src/pm_local.h"

using namespace om;

static int g_fail;
#define CHECK(cond)                                                                                           \
    do {                                                                                                      \
        if (!(cond)) {                                                                                        \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);                                             \
            g_fail++;                                                                                         \
        }                                                                                                     \
    } while (0)

extern "C" PlayerExport *GetPlayerServerAPI(PlayerCommonImport *, PlayerServerImport *);

static float s_time;

int main()
{
    /* SPEC-600 Behaviour 1 vectors */
    CHECK(PRandomAt(1.0f, 0, 100) == 4);
    CHECK(PRandomAt(12.34f, 0, 100) == 61);
    CHECK(PRandomAt(5.0f, 3, 3) == 3);
    CHECK(PRandomAt(5.0f, 7, 2) == 7);

    PlayerServerImport sv;
    memset(&sv, 0, sizeof(sv));
    sv.time = &s_time;
    PlayerExport *ex = GetPlayerServerAPI(NULL, &sv);
    CHECK(ex && ex->version == 1);
    Inventory *inv = (Inventory *)ex->NewInventory();
    CHECK(inv != NULL);
    if (sizeof(void *) == 4) {
        /* the 32-bit object layout of SPEC-601 (the original game module reads it); 64-bit builds pair
           only with the open game module, which uses the natural layout of the same declarations */
        CHECK(sizeof(Inventory) == 668);
        CHECK((char *)&inv->st - (char *)inv == 0x1C);
        CHECK((char *)&inv->st.curWeapon - (char *)inv == 0x2C);
        CHECK((char *)&inv->st.inst - (char *)inv == 0x14C);
        CHECK((char *)&inv->st.spread - (char *)inv == 0x270);
    }

    /* SPEC-601 Behaviour 3 vectors */
    inv->st.armor = 50;
    CHECK(inv->AbsorbDamage(30, 0.f, 0.f) == 0 && inv->st.armor == 20);
    inv->st.armor = 20;
    CHECK(inv->AbsorbDamage(30, 1.f, 0.5f) == 5 && inv->st.armor == 0);
    CHECK(inv->AddArmor(-5) == 0);
    CHECK(inv->AddArmor(150) == 100);
    CHECK(inv->AddArmor(1) == 0);

    /* ammunition limits, fuel into the flamethrower slot */
    CHECK(inv->AddAmmo(AM_KNIFE, 10) && inv->st.ammo[AM_KNIFE] == 6);
    CHECK(!inv->AddAmmo(AM_KNIFE, 1));
    CHECK(inv->AddAmmo(AM_FUEL, 30) && inv->st.slots[SlotOfWeapon(W_FLAMEGUN)].clip == 30);
    CHECK(inv->st.ammo[AM_FUEL] == 0);

    /* give weapon */
    CHECK(inv->GiveWeapon(W_PISTOL1, -1) && inv->st.slots[1].clip == 18);
    CHECK(!inv->GiveWeapon(W_PISTOL1, 5));
    CHECK(inv->OwnsWeapon(W_PISTOL1));

    /* item slots: limit and refusal */
    CHECK(inv->AddItems(IT_GRENADE, 3, 4));
    CHECK(inv->AddItems(IT_GRENADE, 3, 4) && inv->st.items[0].count == 4);
    CHECK(!inv->AddItems(IT_GRENADE, 1, 4));

    /* item use wait: T = 12 (use at 12.9) -> next use at >= 12.4, i.e. at once at 13.0 */
    inv->st.lastItemUse = 12;
    s_time = 12.3f;
    inv->st.itemRequest = IREQ_USESLOT0;
    inv->ProcessItemRequest();
    CHECK(inv->st.itemRequest == IREQ_USESLOT0); /* still waiting */
    s_time = 13.0f;
    inv->ProcessItemRequest();
    CHECK(inv->st.itemRequest == 0 && inv->st.lastItemUse == 13);

    ex->DeleteInventory(inv);
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
