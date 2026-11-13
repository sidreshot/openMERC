// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - weapon swap in the manner of Crysis 2 (openMERC, owner request 2026-11-13; not in SoF, see
// docs/DEVIATIONS.md).
//
// A weapon lying within reach that the player cannot carry (the weapons' weight would pass the carry limit:
// single player sk_maxencum, Realistic) is offered for the weapon in hand: the HUD shows "hold USE: replace <in hand>"
// (engine cl_hud.c), and holding the use key for half a second drops the weapon in hand (the player module's own
// drop: the throw-hand animation, the weapon falls with its clip) and takes the one on the ground with its pickup
// message and sound; the new weapon is raised as the hand ends. A weapon that fits is still picked up by walking over
// it (g_items.cpp Touch_Item), as in SoF.
//
// Offered: a weapon item in view (or under the player) within 96 units of the eye, not hidden by a wall, not one the player
// dropped in the last two seconds. Possible: the weapon in hand may be dropped (not the knife, not a kept weapon,
// dropping allowed) and dropping it makes room. When it does not, the HUD says so and the key does nothing: "hold a
// heavier weapon" when another weapon of the player would make room, "too heavy" when none would.
//
// The HUD gets the offer in free bits of stats[13] (STAT_OM_GRENADES, whose low bits are the grenades): bits 7-10 the
// weapon on the ground, 0x800 the swap is possible, 0x1000 the use key is held for it, 0x2000 not possible with the
// weapon in hand but with another one; the weapon in hand is
// STAT_WEAPON. Only this module (mode B) sets them; the original game module leaves stats[13] at 0.

#include "g_local.h"

#define SWAP_REACH 96.0f      /* units from the eye: about 80 along the floor (the eye is some 46 above it) */
#define SWAP_UNDERFOOT 40.0f  /* closer than this (flat distance) the weapon need not be in view */
#define SWAP_VIEWDOT 0.64f    /* about 50 degrees off the view at most */
#define SWAP_HOLD 0.5f        /* seconds the use key is held */
#define SWAP_TIMEOUT 3.0f     /* a drop or a raise that does not come is given up */

enum { SW_NONE, SW_DROPPING, SW_RAISING };

typedef struct {
	/* the offer of this frame */
	edict_t *item;
	int weapon;
	bool possible;
	bool other; /* not possible, but another weapon of the player in hand would make room */
	float holdstart; /* level.time the use key went down for this offer, -1 = not held */
	bool needrelease; /* after a swap the key must be let go before it counts again */
	/* a swap under way */
	int stage;
	edict_t *pitem;
	int pweapon, pheld;
	float pstart;
} swapstate_t;

static swapstate_t swaps[MAX_CLIENTS];

static swapstate_t *State(edict_t *ent)
{
	int c = G_EdictNum(ent) - 1;
	return (c >= 0 && c < MAX_CLIENTS) ? &swaps[c] : NULL;
}

static void ClearOffer(swapstate_t *s)
{
	s->item = NULL;
	s->weapon = 0;
	s->possible = false;
	s->other = false;
	s->holdstart = -1;
}

/* whether the player can be offered anything now */
static bool MayOffer(edict_t *ent)
{
	gclient_t *c = ent->client;
	return c && c->inv && !c->pers.isbot && !c->resp.spectator && ent->health > 0 && !ent->deadflag &&
	       !level.intermissiontime && !game.cinematic && !c->ps.cinematicfreeze && !c->ps.remote_type &&
	       WeaponCarryLimit() > 0;
}

static void EyeAndView(edict_t *ent, vec3_t eye, vec3_t fwd)
{
	VectorCopy(ent->s.origin, eye);
	eye[2] += ent->viewheight;
	AngleVectors(ent->client->v_angle, fwd, NULL, NULL);
}

/* why weapon item e is not offered to the player (NULL: it may be; *score: how well it is looked at) */
static const char *OfferReject(edict_t *ent, edict_t *e, PlayerInventory *inv, int limit, const vec3_t eye,
                               const vec3_t fwd, float *score)
{
	if (!WeaponItemAvailable(e))
		return "not available";
	int w = e->item->value;
	if (inv->OwnsWeapon(w))
		return "owned (its ammunition is taken by touch)";
	if (inv->TotalWeight() + inv->WeaponWeight(w) <= limit)
		return "fits (picked up by touch)";
	if (e->owner == ent && level.time < e->timestamp + 2)
		return "just dropped by the player"; /* as Touch_Item */
	vec3_t c, d;
	VectorCopy(e->s.origin, c);
	c[2] += 4;
	VectorSubtract(c, eye, d);
	if (VectorLength(d) > SWAP_REACH)
		return "out of reach";
	float flat = sqrtf(d[0] * d[0] + d[1] * d[1]);
	VectorNormalize(d);
	float dot = DotProduct(d, fwd);
	if (flat > SWAP_UNDERFOOT && dot < SWAP_VIEWDOT)
		return "not in view";
	trace_t tr = gi.trace(eye, NULL, NULL, c, ent, MASK_SOLID);
	if (tr.fraction < 1.0f && tr.ent != e)
		return "hidden by a wall";
	*score = dot + (flat <= SWAP_UNDERFOOT ? 1.0f : 0.0f); /* what is looked at wins, under the feet next */
	return NULL;
}

/* the weapon item the player looks at (or stands on) that does not fit; NULL when there is none */
static edict_t *FindOffer(edict_t *ent, PlayerInventory *inv, int limit)
{
	vec3_t eye, fwd, mins, maxs;
	edict_t *list[256];

	EyeAndView(ent, eye, fwd);
	for (int i = 0; i < 3; i++) {
		mins[i] = eye[i] - SWAP_REACH;
		maxs[i] = eye[i] + SWAP_REACH;
	}
	int n = gi.BoxEdicts(mins, maxs, list, 256, AREA_TRIGGERS);
	edict_t *best = NULL;
	float bestscore = -1;
	for (int i = 0; i < n; i++) {
		float score;
		if (!OfferReject(ent, list[i], inv, limit, eye, fwd, &score) && score > bestscore) {
			best = list[i];
			bestscore = score;
		}
	}
	return best;
}

// omswap (cheats, test aid): the carry limit, the weight, the weapon in hand and why each weapon item within 300
// units is or is not offered
void WeaponSwap_Debug(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (!c || !c->inv)
		return;
	PlayerInventory *inv = c->inv;
	int limit = WeaponCarryLimit();
	vec3_t eye, fwd;
	EyeAndView(ent, eye, fwd);
	swapstate_t *s = State(ent);
	gi.dprintf("omswap: limit %d weight %d in hand %d (weight %d) may offer %d stage %d offer %d possible %d other %d\n",
	           limit,
	           inv->TotalWeight(), inv->CurrentWeapon(), inv->WeaponWeight(inv->CurrentWeapon()), MayOffer(ent) ? 1 : 0,
	           s ? s->stage : -1, s ? s->weapon : -1, s && s->possible ? 1 : 0,
	           s && s->other ? 1 : 0);
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || !e->item || e->item->kind != IK_WEAPON)
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, eye, d);
		if (VectorLength(d) > 300)
			continue;
		float score = 0;
		const char *why = OfferReject(ent, e, inv, limit, eye, fwd, &score);
		gi.dprintf("omswap:   ent %d %s weapon %d at %.0f units (eye-relative z %.0f): %s\n", i, e->classname,
		           e->item->value, VectorLength(d), d[2], why ? why : "offered");
	}
}

/* the weapon in hand may be dropped for weapon w and doing so makes room for it */
static bool SwapPossible(PlayerInventory *inv, int held, int w, int limit)
{
	if (held <= 0 || held >= 32 || inv->WeaponWeight(held) <= 0)
		return false; /* nothing in hand, or the knife (weight 0: dropping it frees nothing) */
	if (!inv->GetVarB() || (inv->GetVarI() & (1 << held)))
		return false; /* dropping is not allowed now, or the weapon is a kept one */
	return inv->TotalWeight() - inv->WeaponWeight(held) + inv->WeaponWeight(w) <= limit;
}

/* another weapon of the player, taken in hand, would make room for weapon w */
static bool SwapPossibleOther(PlayerInventory *inv, int held, int w, int limit)
{
	for (int x = 1; x <= 12; x++)
		if (x != held && inv->OwnsWeapon(x) && SwapPossible(inv, x, w, limit))
			return true;
	return false;
}

// ClientThink (p_client.cpp), after the use key: the offer, the hold, the start of a swap
void WeaponSwap_ClientThink(edict_t *ent)
{
	swapstate_t *s = State(ent);
	if (!s)
		return;
	gclient_t *c = ent->client;
	bool use = c && (c->buttons & BUTTON_USE);
	if (!use)
		s->needrelease = false;
	if (s->stage != SW_NONE && level.time < s->pstart)
		s->stage = SW_NONE; /* a new level */
	if (s->stage != SW_NONE || !MayOffer(ent)) {
		ClearOffer(s);
		return;
	}

	PlayerInventory *inv = c->inv;
	int limit = WeaponCarryLimit();
	edict_t *e = FindOffer(ent, inv, limit);
	if (!e) {
		ClearOffer(s);
		return;
	}
	int w = e->item->value;
	int held = inv->CurrentWeapon();
	bool possible = SwapPossible(inv, held, w, limit);
	if (e != s->item || w != s->weapon || possible != s->possible)
		s->holdstart = -1; /* another offer: the hold starts again */
	s->item = e;
	s->weapon = w;
	s->possible = possible;
	s->other = !possible && SwapPossibleOther(inv, held, w, limit);

	if (!use || s->needrelease || !possible) {
		s->holdstart = -1;
		return;
	}
	if (s->holdstart < 0 || s->holdstart > level.time)
		s->holdstart = level.time;
	if (level.time - s->holdstart + 0.001f < SWAP_HOLD)
		return;

	/* the swap: the player module drops the weapon in hand at its next think (DropOrLose: the weapon falls with its
	   clip through DropWeaponItem, the throw hand plays); WeaponSwap_InventoryThink then gives the new one */
	if (!inv->RequestDrop())
		return;
	s->stage = SW_DROPPING;
	s->pitem = e;
	s->pweapon = w;
	s->pheld = held;
	s->pstart = level.time;
	s->needrelease = true;
	ClearOffer(s);
}

// PlayerThinkInventory (g_player.cpp), around the inventory think: the swap after the drop and the raise
void WeaponSwap_InventoryThink(edict_t *ent, bool after)
{
	swapstate_t *s = State(ent);
	if (!s || s->stage == SW_NONE)
		return;
	PlayerInventory *inv = ent->client ? ent->client->inv : NULL;
	if (!inv || ent->health <= 0 || level.time < s->pstart || level.time > s->pstart + SWAP_TIMEOUT) {
		s->stage = SW_NONE;
		return;
	}

	if (s->stage == SW_DROPPING) {
		if (!after)
			return;
		if (inv->OwnsWeapon(s->pheld)) {
			if (inv->CurrentWeapon() != s->pheld && inv->CurrentWeapon() != 0)
				s->stage = SW_NONE; /* the player switched weapons before the drop: no swap */
			return;
		}
		/* dropped: take the weapon on the ground (it may be gone - picked up by another player, removed) */
		if (s->pitem->item && s->pitem->item->value == s->pweapon && WeaponItemTake(s->pitem, ent))
			s->stage = SW_RAISING;
		else
			s->stage = SW_NONE;
		return;
	}

	/* SW_RAISING: while the throw hand plays (no weapon current) the new weapon is requested before each think. At the
	   hand's end the player module asks for the next weapon (HandFinished: "weapnext"), which is processed only at the
	   next think - this request replaces it, so the new weapon comes up instead of the next one in the list. */
	if (after)
		return;
	if (!inv->OwnsWeapon(s->pweapon)) {
		s->stage = SW_NONE;
		return;
	}
	int cur = inv->CurrentWeapon();
	if (cur == 0) {
		inv->RequestWeapon(s->pweapon);
		return;
	}
	if (cur != s->pweapon)
		inv->RequestWeapon(s->pweapon); /* another weapon came up all the same: switch to the new one */
	s->stage = SW_NONE;
}

// p_hud.cpp: the offer for the HUD, in the bits of stats[13] above the grenade count
int WeaponSwap_Stat(edict_t *ent)
{
	swapstate_t *s = State(ent);
	if (!s || !s->item || s->weapon <= 0 || s->weapon > 15)
		return 0;
	int v = s->weapon << OM_SWAP_SHIFT;
	if (s->possible)
		v |= OM_SWAP_POSSIBLE;
	else if (s->other)
		v |= OM_SWAP_OTHER;
	if (s->holdstart >= 0)
		v |= OM_SWAP_HOLDING;
	return v;
}
