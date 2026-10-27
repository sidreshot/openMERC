// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): what the single-player mode itself decides: the status meter and the
// `defaultweapons` cheat.
// Implements: SPEC-790 Behaviour 11
#include <ctype.h>

#include "gm_internal.h"

void GM_SetStats(edict_t *ent)
{
	if (gm.mode != GM_SP || !GM_IsClient(ent))
		return;
	ent->client->ps.stats[STAT_FLAGS] |= 0x200;
	ent->client->ps.stats[STAT_METER] = (short)(int)(G_StealthValue() * 100.0f);
}

struct gm_spkit {
	const char *prefix;
	char fifth;              // 0 = any
	int weapons[6];
	int flash, c4, grenades;
};

static const gm_spkit gm_kits[] = {
	{"tsr", 0, {1, 3, 8}, 5, 2, 0},
	{"trn", 0, {1, 3, 8}, 5, 2, 0},
	{"kos", 0, {1, 3, 8, 5}, 10, 5, 0},
	{"sib", 0, {1, 3, 6, 4, 5}, 10, 5, 0},
	{"irq", 'a', {1, 3, 6, 4}, 10, 5, 0},
	{"irq", 'b', {1, 3, 9, 10}, 10, 5, 5},
	{"irq", '*', {0}, 0, 0, 0},
	{"nyc", 0, {1, 3, 8, 5}, 10, 5, 5},
	{"sud", 0, {1, 3, 2, 6}, 10, 5, 5},
	{"jpn", 0, {1, 3, 4, 5}, 10, 5, 5},
	{"ger", 0, {1, 3, 10, 9}, 10, 5, 5},
};
static const gm_spkit gm_kit_default = {"", 0, {1, 3, 8}, 5, 2, 0};

static const gm_spkit *gm_kitfor(const char *map)
{
	char m[8] = {0};
	for (int i = 0; i < 5 && map[i]; i++)
		m[i] = (char)tolower((unsigned char)map[i]);
	for (const gm_spkit &k : gm_kits) {
		if (strncmp(m, k.prefix, 3))
			continue;
		if (k.fifth == 0 || k.fifth == m[4])
			return &k;
		if (k.fifth == '*')
			return &k;
	}
	return &gm_kit_default;
}

void GM_DefaultWeapons(edict_t *ent)
{
	if (!GM_IsClient(ent))
		return;
	gclient_t *cl = ent->client;
	if (!cl->inv) {
		gi.dprintf("hey! no inventory!\n");
		return;
	}
	GM_NewInventory(ent);
	PlayerInventory *inv = cl->inv;
	if (!inv)
		return;
	inv->RemoveAll(true);
	inv->SetVarA(-1);
	inv->SetTag(cl->pers.bestweap);
	static const int ammo[][2] = {{AM_9MM, 128}, {AM_KNIFE, 6},  {AM_556, 120},    {AM_44, 36},     {AM_SLUG, 40},
	                              {AM_SHELLS, 32}, {AM_ROCKETS, 8}, {AM_FUEL, 30}, {AM_BATTERY, 60}};
	for (auto &a : ammo)
		inv->AddAmmo(a[0], a[1]);
	const gm_spkit *k = gm_kitfor(level.mapname);
	for (int i = 0; i < 6 && k->weapons[i]; i++)
		inv->GiveWeapon(k->weapons[i], 0);
	if (k->flash)
		inv->AddItems(IT_FLASHPACK, k->flash, -1);
	if (k->c4)
		inv->AddItems(IT_C4, k->c4, -1);
	if (k->grenades)
		inv->AddItems(IT_GRENADE, k->grenades, -1);
	inv->GiveWeapon(WP_HURTHAND, 0);
	inv->GiveWeapon(WP_THROWHAND, 0);
	inv->TopUpClips();
	unsigned mask = 0;
	for (int w = 1; w <= 14; w++)
		if (inv->OwnsWeapon(w))
			mask |= 1u << w;
	if (pe && pe->PrecacheViewModels)
		pe->PrecacheViewModels(mask);
	inv->SwitchToWeapon(WP_PISTOL1);
	inv->AddArmor(1000);
}
