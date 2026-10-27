/* SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc player module - constant tables: weapons, ammunition, items, slot order, best-weapon lists.
 * Implements: SPEC-003 (constants), SPEC-600 (weapon table 1, descriptor values), SPEC-601 (slots, ammunition,
 *             items, best-weapon lists)
 */
#include "pm_local.h"

namespace om {

/* Weapon gameplay parameters (SPEC-600 tables 1 and 2): values chosen to match the weapons' observed behaviour
   (clip sizes, spread, kick, attack kinds) and the names in the game data (cvars, model directories). */
const WeaponInfo kWeapons[NUM_WEAPONS] = {
    /* cvar, dir, display, clip, weight, ammo p/a, mode value p/a, spread p/a, max p/a, kick p/a, attack p/a, msg,
       muzzle, converge, hasAlt, muzzle effects */
    {"nothing", "", "", -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, true, false, NULL, NULL},
    {"knife", "Knife", "Knife", 0, 0, AM_KNIFE, AM_KNIFE, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 1, false, true, true, NULL,
     NULL},
    {"pistol2", "Pistol2", "Desert Eagle", 9, 1, AM_44, 0, 900, 0, 0.2f, 0, 0.8f, 0, 5, 0, 4, 0, 4, false, true,
     false, "weapons/playermz/pistol2", NULL},
    {"pistol1", "Pistol1", "Glock 9mm", 18, 1, AM_9MM, 0, 600, 0, 0.3f, 0, 1.5f, 0, 1, 0, 3, 0, 2, false, true,
     false, "weapons/playermz/pistol1", NULL},
    {"mpistol", "MPistol", "Ingram m11", 32, 2, AM_9MM, 0, 10, 0, 0.6f, 0, 3.5f, 0, 0, 0, 5, 0, 2, false, true,
     false, "weapons/playermz/machinepistol", NULL},
    {"assault", "Assaultrifle", "HK53", 40, 2, AM_556, 0, 800, 0, 0.4f, 0, 1.8f, 0, 0, 0, 6, 0, 5, false, true,
     false, "weapons/playermz/assaultrifle", NULL},
    {"sniper", "SniperRifle", "Sig2000G", 6, 2, AM_556, 0, 0, 0, 0, 0, 0, 0, 2, 0, 7, 8, 5, false, true, true,
     "weapons/playermz/sniper", NULL},
    {"slugger", "Autoshotgun", "Jackhammer", 10, 2, AM_SLUG, AM_SLUG, 1200, 0, 1.5f, 0, 1.5f, 0, 4, 4, 9, 10, 9,
     true, true, true, "weapons/playermz/autoshotgun", NULL},
    {"shotgun", "Shotgun", "Spas15", 8, 2, AM_SHELLS, 0, 1200, 0, 0, 0, 0, 0, 5, 0, 11, 0, 3, false, true, false,
     "weapons/playermz/shotgun", NULL},
    {"machinegun", "Machinegun", "Minimi", 40, 2, AM_556, AM_556, 1500, 500, 0.1f, 0, 0.4f, 0, 0, 0, 12, 13, 5,
     false, true, true, "weapons/playermz/machinegun", NULL},
    {"rocket", "Rocket", "M202A2", 4, 3, AM_ROCKETS, AM_ROCKETS, 0, 0, 0, 0, 0, 0, 0, 0, 14, 15, 8, true, true,
     true, "weapons/playermz/rocket", "weapons/playermz/rocket_alt"},
    {"mpg", "MPG", "Microwave Pulse", 30, 3, AM_BATTERY, AM_BATTERY, 0, 0, 0, 0, 0, 0, 0, 0, 16, 17, 6, true,
     false, true, "weapons/playermz/mpg", NULL},
    {"flamegun", "Flamegun", "Flamethrower", 60, 3, AM_FUEL, AM_FUEL, 0, 0, 0, 0, 0, 0, 0, 0, 18, 19, 7, true,
     false, true, NULL, NULL},
    {NULL, "Hurthand", "Hurthand", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, true, false, NULL, NULL},
    {NULL, "Throwhand", "Throwhand", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, true, false, NULL, NULL},
};

/* SPEC-601 Behaviour 1: slot i holds weapon kSlotOrder[i] */
const int kSlotOrder[NUM_WSLOTS] = {W_KNIFE,      W_PISTOL1, W_PISTOL2, W_SHOTGUN, W_SNIPER, W_MPISTOL, W_ASSAULT,
                                    W_MACHINEGUN, W_SLUGGER, W_ROCKET,  W_FLAMEGUN, W_MPG,   W_HURTHAND, W_THROWHAND};

/* SPEC-003 persistence: weapons given in this order after loading */
const int kGiveOrder[12] = {1, 3, 2, 8, 6, 4, 5, 9, 7, 10, 11, 12};

const char *const kAmmoNames[NUM_AMMO] = {"nothing", "aknife", "a44", "a9mm", "shells", "a556",
                                          "rockets", "battery", "fuel", "slug", NULL};
const int kAmmoMax[NUM_AMMO] = {0, 6, 90, 200, 50, 300, 20, 150, 60, 100, 0};
const int kAmmoPickup[NUM_AMMO][2] = {{0, 0}, {1, 1}, {4, 9}, {5, 8}, {2, 4}, {15, 20},
                                      {1, 3}, {5, 10}, {5, 10}, {5, 10}, {0, 0}};

const char *const kItemNames[8] = {NULL, "fpak", "c4", "goggles", "claymore", "medkit", "grenade", NULL};
const char *const kItemDisplay[8] = {"", "Flash Packs", "C4", "Light Amplification Goggles", "Claymore", "Medkit",
                                     "Grenade", "Flag"};
const int kItemStringId[8] = {0, 0x301, 0x300, 0x303, 0x304, 0x335, 0x336, 0x339};

/* SPEC-601 Behaviour 5 */
const int kBestSafe[12] = {11, 9, 5, 4, 8, 2, 3, 6, 1, 10, 7, 12};
const int kBestUnsafe[12] = {11, 10, 7, 12, 9, 5, 4, 8, 2, 3, 6, 1};
const int kKeepExplosive[3] = {10, 7, 12};

int SlotOfWeapon(int weapon)
{
    for (int i = 0; i < NUM_WSLOTS; i++)
        if (kSlotOrder[i] == weapon)
            return i;
    return -1;
}

} /* namespace om */
