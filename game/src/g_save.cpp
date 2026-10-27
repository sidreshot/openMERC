// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - save/load entry points and the engine console hooks of SPEC-001 exports 0x40-0x4C.
// The savegame itself is package G10's (game/src/save/); this file forwards the exports to it.
// Implements: SPEC-001 (exports 0x10-0x1C, 0x40-0x4C), SPEC-752 (through game/src/save)

#include "g_local.h"
#include "save/save_local.h"

void WriteGame(qboolean autosave) { G10_WriteGame(autosave); }
qboolean ReadGame(int unused) { return G10_ReadGame(unused); }
void WriteLevel(void) { G10_WriteLevel(); }
void ReadLevel(void) { G10_ReadLevel(); }
qboolean SaveAllowed(void) { return G10_SaveAllowed(); }

void SavesLeft(void)
{
	char buf[16];
	int left = G10_SavesLeft(); // per level (game/src/save)
	if (left < 0)
		strcpy(buf, "unlimited");
	else
		sprintf(buf, "%d", left);
	gix.cvar_set("menu_savesleft", buf);
	gi.dprintf("Saves left : %s\n", buf);
}

static const char *inven_weapons[] = {"nothing", "knife",  "pistol2",    "pistol1", "mpistol", "assault", "sniper",
                                      "slugger", "shotgun", "machinegun", "rocket",  "mpg",     "flamegun"};

void UpdateInven(void)
{
	edict_t *p = G_FirstPlayer();
	if (!p || !p->client || !p->client->inv)
		return;
	PlayerInventory *inv = p->client->inv;
	for (int w = 1; w <= 12; w++)
		gix.cvar_set(inven_weapons[w], inv->OwnsWeapon(w) ? "1" : "0");
}
