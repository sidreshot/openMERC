// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - weak fallbacks for the item-list accessors requested from G18
// (docs/impl/G10.md "Needs from G18"). MSVC x86: /alternatename makes the linker use a fallback only while the real
// function is not defined anywhere (the same scheme as game/src/scriptglue/sg_shims.cpp). Without the accessors the
// savegame takes the item list from the entities flagged FL_ITEMLIST and cannot re-register restored items in G18's
// list (RevealHiddenItems / listpickups then miss them after a load).
// Implements: SPEC-781 "Savegame chunks" (PLNM/PLPI fallbacks)

#include "../g_local.h"

extern "C" {
int g10_fb_ItemList_Count(void) { return -1; }
edict_t *g10_fb_ItemList_Get(int) { return NULL; }
void g10_fb_ItemList_Clear(void) {}
void g10_fb_ItemList_Add(edict_t *ent)
{
	if (ent)
		ent->flags |= FL_ITEMLIST;
}
}

#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/alternatename:_ItemList_Count=_g10_fb_ItemList_Count")
#pragma comment(linker, "/alternatename:_ItemList_Get=_g10_fb_ItemList_Get")
#pragma comment(linker, "/alternatename:_ItemList_Clear=_g10_fb_ItemList_Clear")
#pragma comment(linker, "/alternatename:_ItemList_Add=_g10_fb_ItemList_Add")
#endif
