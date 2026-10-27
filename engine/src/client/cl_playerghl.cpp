/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc client (I1d): the C++ side of the player-module host - calls into the inventory object's method
 * table (MSVC thiscall, SPEC-003 "Inventory object", 79 slots) and the engine's own note handlers on the view
 * weapon's Ghoul instance (SPEC-010 event callback, SPEC-011 methods 23/24/45). Plain C entry points for
 * cl_player.c.
 *
 * Implements: SPEC-003 (inventory slots 3, 4, 23, 61, 68, 69, 70, 77), SPEC-011 (note callbacks, method 45),
 *             SPEC-400 ("View weapon": notes `sound`, `effect`, `fire`, `altfire`)
 */

#include <cstdint>

#include "../ghoul/ghoul_api.h"
#include "header/cl_player.h"

using namespace ghoul;

/* The inventory object's method table in slot order (SPEC-003). Only the slots the engine calls carry their
   argument lists; the others are placeholders that are never called. Not in the anonymous namespace: the
   object comes from the player module and nothing in this program derives from the class, so with internal
   linkage GCC treats every call as a pure virtual call (release builds aborted on the first one). */
namespace clp_host
{
class PlayerInventory
{
public:
	virtual void s00(int alsoFlag) = 0;
	virtual void s01(int itemType) = 0;
	virtual void s02() = 0;
	virtual void Think() = 0;                                  /* 3 */
	virtual int Command(const char *cmd, int force) = 0;        /* 4 */
	virtual void s05() = 0;
	virtual void s06() = 0;
	virtual void s07() = 0;
	virtual void s08() = 0;
	virtual void s09() = 0;
	virtual void s10() = 0;
	virtual void s11() = 0;
	virtual void s12() = 0;
	virtual void s13() = 0;
	virtual void s14() = 0;
	virtual void s15() = 0;
	virtual void s16() = 0;
	virtual void s17() = 0;
	virtual void s18() = 0;
	virtual void s19() = 0;
	virtual void s20() = 0;
	virtual void s21() = 0;
	virtual void s22() = 0;
	virtual int CurrentWeapon() = 0;                            /* 23 */
	virtual void s24() = 0;
	virtual void s25() = 0;
	virtual void s26() = 0;
	virtual void s27() = 0;
	virtual void s28() = 0;
	virtual void s29() = 0;
	virtual void s30() = 0;
	virtual void s31() = 0;
	virtual void s32() = 0;
	virtual void s33() = 0;
	virtual void s34() = 0;
	virtual void s35() = 0;
	virtual void s36() = 0;
	virtual void s37() = 0;
	virtual void s38() = 0;
	virtual void s39() = 0;
	virtual void s40() = 0;
	virtual void s41() = 0;
	virtual void s42() = 0;
	virtual void s43() = 0;
	virtual void s44() = 0;
	virtual void s45() = 0;
	virtual void s46() = 0;
	virtual void s47() = 0;
	virtual void s48() = 0;
	virtual void s49() = 0;
	virtual void s50() = 0;
	virtual void s51() = 0;
	virtual void s52() = 0;
	virtual void s53() = 0;
	virtual void s54() = 0;
	virtual void s55() = 0;
	virtual void s56() = 0;
	virtual void s57() = 0;
	virtual void s58() = 0;
	virtual void s59() = 0;
	virtual void s60() = 0;
	virtual void SetOwner(void *ownerContext) = 0;              /* 61 */
	virtual void s62() = 0;
	virtual void s63() = 0;
	virtual void s64() = 0;
	virtual void s65() = 0;
	virtual void s66() = 0;
	virtual void s67() = 0;
	virtual void ClearEvents() = 0;                             /* 68 */
	virtual void ReadEvents() = 0;                              /* 69 */
	virtual void ApplyEvents() = 0;                             /* 70 */
	virtual void s71() = 0;
	virtual void s72() = 0;
	virtual void s73() = 0;
	virtual void s74() = 0;
	virtual void s75() = 0;
	virtual void s76() = 0;
	virtual void ReadFullState(void *sizebuf) = 0;              /* 77 */
	virtual void s78() = 0;
};
} // namespace clp_host
using clp_host::PlayerInventory;

namespace
{
PlayerInventory *Inv(void *p) { return static_cast<PlayerInventory *>(p); }

/* the engine's note handlers on the view weapon (SPEC-400 "View weapon", SPEC-003 client imports 2/3/4/6) */
enum
{
	VN_SOUND,
	VN_EFFECT,
	VN_FIRE,
	VN_ALTFIRE,
	VN_COUNT
};
const char *const kNoteNames[VN_COUNT] = {"sound", "effect", "fire", "altfire"};

class ViewNote : public IGhoulCallback
{
public:
	explicit ViewNote(int kind) : kind_(kind) {}
	bool Event(uint32_t, void *, float, const void *data) override
	{
		const char *text = data ? static_cast<const char *>(data) : "";

		switch (kind_)
		{
			case VN_SOUND: CL_Player_NoteSound(text); break;
			case VN_EFFECT: CL_Player_NoteEffect(text); break;
			case VN_FIRE: CL_Player_FireEffects(-1, 0); break;
			case VN_ALTFIRE: CL_Player_FireEffects(-1, 1); break;
		}

		return true;
	}

private:
	int kind_;
};

ViewNote g_notes[VN_COUNT] = {ViewNote(VN_SOUND), ViewNote(VN_EFFECT), ViewNote(VN_FIRE), ViewNote(VN_ALTFIRE)};
IGhoulInst *g_attached;
}

extern "C" void CLP_InvSetOwner(void *inv, void *ctx) { if (inv) Inv(inv)->SetOwner(ctx); }
extern "C" void CLP_InvThink(void *inv) { if (inv) Inv(inv)->Think(); }
extern "C" int CLP_InvCommand(void *inv, const char *cmd, int force) { return inv ? Inv(inv)->Command(cmd, force) : 0; }
extern "C" int CLP_InvCurrentWeapon(void *inv) { return inv ? Inv(inv)->CurrentWeapon() : 0; }
extern "C" void CLP_InvClearEvents(void *inv) { if (inv) Inv(inv)->ClearEvents(); }
extern "C" void CLP_InvReadEvents(void *inv) { if (inv) Inv(inv)->ReadEvents(); }
extern "C" void CLP_InvApplyEvents(void *inv) { if (inv) Inv(inv)->ApplyEvents(); }
extern "C" void CLP_InvReadFullState(void *inv, void *sizebuf) { if (inv) Inv(inv)->ReadFullState(sizebuf); }

/* Registers the engine's handlers for `sound`, `effect`, `fire` and `altfire` on the view weapon's instance
   (NULL: none). Called every frame: existing registrations of the same handlers are removed first, so an
   instance is served exactly once even when a new instance took the memory and the handle of the previous one
   (the server reuses instance ids, and the client allocator reuses the memory). */
extern "C" void
CLP_ViewNotesAttach(void *instance)
{
	IGhoulInst *inst = static_cast<IGhoulInst *>(instance);
	IGhoulObject *obj;
	int k;

	g_attached = inst;

	if (!inst || !(obj = inst->GetGhoulObject()))
	{
		return;
	}

	for (k = 0; k < VN_COUNT; k++)
	{
		GhoulID note = obj->FindNote(kNoteNames[k]);

		if (note)
		{
			inst->RemoveNoteCallback(&g_notes[k], note);
			inst->AddNoteCallback(&g_notes[k], note);
		}
	}
}

/* method 45: the events of the instance up to `time` (SPEC-012 section 5) */
extern "C" void
CLP_ViewNotesProcess(void *instance, float time)
{
	IGhoulInst *inst = static_cast<IGhoulInst *>(instance);

	if (inst && inst == g_attached)
	{
		inst->ProcessEvents(time);
	}
}
