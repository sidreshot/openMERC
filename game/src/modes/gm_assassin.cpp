// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): Assassin (deathmatch 2) and Arsenal (deathmatch 3).
// Implements: SPEC-790 Behaviour 6 (Assassin), Behaviour 7 (Arsenal), Behaviour 3 (help / status layouts)
#include "gm_internal.h"

//===========================================================================================================
// Assassin

static int gm_playersingame(void)
{
	int n = 0;
	for (int i = 0; i < game.maxclients; i++)
		if (GM_InGame(&g_edicts[i + 1]))
			n++;
	return n;
}

static void gm_icons_pair(edict_t *a, edict_t *b);

// Choosing a target for P (SPEC-790 B6): none with fewer than 2 players in the game, else from client slot
// (r mod maxclients) + 1 cyclically the first slot in use, with a client, not a spectator and not P.
static edict_t *gm_choose(edict_t *p)
{
	if (gm_playersingame() < 2)
		return NULL;
	int mc = game.maxclients;
	int start = (GM_Rand() % mc) + 1;
	for (int k = 0; k < mc; k++) {
		int slot = ((start - 1 + k) % mc) + 1;
		edict_t *e = &g_edicts[slot];
		if (e != p && GM_InGame(e))
			return e;
	}
	return NULL;
}

// The choice on entering the game (the entering player and every other player without a target) becomes the
// target and the pending target at once; only the icons of that pair are updated (SPEC-AMEND-005 G9.2).
void GM_AssassinChoose(edict_t *p)
{
	gm_client_t *c = GM_Cl(p);
	if (!c)
		return;
	edict_t *old = c->target, *chosen = gm_choose(p);
	c->pending = c->target = chosen;
	gm_icons_pair(p, old);
	gm_icons_pair(p, chosen);
}

// After a disconnect or a spectator spawn the hunters choose only a new pending target; the target changes at their
// next end of frame with icons and status (SPEC-AMEND-005 G9.2).
static void gm_choosepending(edict_t *p)
{
	gm_client_t *c = GM_Cl(p);
	if (c)
		c->pending = gm_choose(p);
}

static void gm_icons_pair(edict_t *a, edict_t *b)
{
	if (!a || !b)
		return;
	GM_UpdateIcon(a, b);
	GM_UpdateIcon(b, a);
}

void GM_AssassinBegin(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	c->target = c->pending = NULL;
	// his own table for every slot in use including his own (all 0 for a spectator); the others' tables only
	// when he is not a spectator, so a spectator appears in nobody's table (SPEC-AMEND-005 G9.3)
	bool spectator = GM_IsSpectator(ent);
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *o = &g_edicts[i + 1];
		if (!o->inuse || !o->client)
			continue;
		GM_UpdateIcon(ent, o);
		if (!spectator && o != ent)
			GM_UpdateIcon(o, ent);
	}
	if (!spectator)
		GM_AssassinChoose(ent);
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *o = &g_edicts[i + 1];
		if (o == ent || !GM_InGame(o) || gm_cl[i].target)
			continue;
		GM_AssassinChoose(o);
		GM_StatusLayout(o);
	}
}

void GM_AssassinSpawn(edict_t *ent, bool wasspectating)
{
	gm_client_t *c = GM_Cl(ent);
	bool spectator = GM_IsSpectator(ent);
	if (!spectator && wasspectating)
		GM_AssassinBegin(ent);
	if (spectator) {
		for (int i = 0; i < game.maxclients; i++) {
			edict_t *o = &g_edicts[i + 1];
			if (o != ent && o->inuse && o->client && gm_cl[i].target == ent)
				gm_choosepending(o);
		}
		c->target = c->pending = NULL;
	}
	GM_DefaultKit(ent);
	if (!spectator)
		GM_StatusLayout(ent);
}

void GM_AssassinEndFrame(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	if (c->pending == c->target)
		return;
	edict_t *old = c->target;
	c->target = c->pending;
	gm_icons_pair(ent, old);
	gm_icons_pair(ent, c->target);
	GM_StatusLayout(ent);
}

void GM_AssassinScore(edict_t *self, edict_t *attacker)
{
	if (GM_IsSpectator(self))
		return;
	gm_client_t *v = GM_Cl(self);
	if (GM_IsClient(attacker) && attacker != self) {
		gm_client_t *a = GM_Cl(attacker);
		if (a->target == self) {
			GM_AddScore(attacker, 4);
			GM_LocalSound(attacker, "dm/assassin/killtarget.wav", 1, ATTN_NORM, CHAN_VOICE);
		} else if (v->target == attacker) {
			GM_AddScore(attacker, 1);
			GM_LocalSound(attacker, "dm/frag.wav", 1, ATTN_STATIC, CHAN_VOICE);
			return;
		} else {
			GM_AddScore(attacker, -1);
			GM_LocalSound(attacker, "dm/assassin/badkill.wav", 1, ATTN_NORM, CHAN_VOICE);
			GM_Print(attacker, SP_DM_ASSASSIN, ASN_TEXT_LOST_FRAG);
		}
		v->pending = attacker;
		// A takes over V's contract; when that contract was A himself, A chooses anew (V, still in use, is a
		// candidate: with two players A keeps V and nothing changes; SPEC-AMEND-006 G9.14)
		a->pending = v->target == attacker ? gm_choose(attacker) : v->target;
		return;
	}
	GM_AddScore(self, -1);
}

void GM_AssassinDisconnect(edict_t *ent)
{
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *o = &g_edicts[i + 1];
		if (o != ent && o->inuse && o->client && gm_cl[i].target == ent)
			gm_choosepending(o);
	}
}

static bool gm_refreshing = false; // a 32-frame refresh resends the layout without touching the timers

static void gm_openstatus(edict_t *ent, float seconds)
{
	if (gm_refreshing)
		return;
	ent->client->showscores = false;
	ent->client->showhelp = true;
	ent->client->showhelp_time = level.time + seconds;
}

static bool gm_assassin_status(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	gm_openstatus(ent, 5);
	if (c->target)
		GM_Print(ent, SP_DM_ASSASSIN, ASN_LAYOUT_TARGET, GM_EntNum(c->target));
	else
		GM_Print(ent, SP_DM_ASSASSIN, ASN_LAYOUT_NO_TARGET);
	int hunters = 0;
	for (int i = 0; i < game.maxclients; i++)
		if (gm_cl[i].target == ent && g_edicts[i + 1].inuse)
			hunters++;
	GM_Print(ent, SP_DM_ASSASSIN, ASN_LAYOUT_NUM_ENEMIES, hunters);
	return true;
}

//===========================================================================================================
// Arsenal

void GM_ArsenalDrawSet(void)
{
	int set[6], n = 0;
	int guard = 10000;
	while (n < 6 && guard-- > 0) {
		float u = gix.flrand ? gix.flrand(1.0f, 13.0f) : 1.0f + 12.0f * (GM_Rand() / 32768.0f);
		int id = (int)u;
		if (id > 12)
			id = 12;
		if (id < 1)
			id = 1;
		bool dup = false;
		for (int k = 0; k < n; k++)
			dup |= set[k] == id;
		if (!dup)
			set[n++] = id;
	}
	// ascending id order
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++)
			if (set[j] < set[i]) {
				int t = set[i];
				set[i] = set[j];
				set[j] = t;
			}
	for (int i = 0; i < 6; i++)
		gm.arsenal_set[i] = i < n ? set[i] : 0;
}

void GM_ArsenalKit(edict_t *ent, bool fresh)
{
	gclient_t *cl = ent->client;
	gm_client_t *c = GM_Cl(ent);
	if (fresh || !cl->inv)
		GM_NewInventory(ent);
	PlayerInventory *inv = cl->inv;
	if (!inv)
		return;
	// switching to the best weapon draws it at once, which needs the owner context; a fresh inventory has none
	// yet (the game core sets its own right after the spawn kit)
	static OwnerContext ctx[MAX_CLIENTS];
	int s = GM_Slot(ent);
	memset(&ctx[s], 0, sizeof(ctx[s]));
	ctx[s].inventory = inv;
	ctx[s].owner = ent;
	ctx[s].kick = cl->kick_angles;
	ctx[s].frametime10 = 1.0f;
	inv->SetOwnerContext(&ctx[s]);
	inv->RemoveAll(true);
	inv->SetVarA(0);
	if (!cl->pers.isbot) {
		inv->SetVarB(0);
		inv->SetVarC(0);
	}
	inv->SetTag("unsafe");
	inv->SetVarH(0);
	if (fresh || !c->arsenal_have) {
		for (int i = 0; i < 6; i++)
			if (gm.arsenal_set[i])
				inv->GiveWeapon(gm.arsenal_set[i], -1);
		inv->WriteInfo(c->arsenal_inv, true, false, false, false);
		c->arsenal_have = true;
	} else {
		unsigned mask = 0;
		inv->LoadInfo(&mask, c->arsenal_inv);
	}
	inv->GiveWeapon(WP_THROWHAND, 0);
	inv->SwitchToBest();
}

static bool gm_arsenal_status(edict_t *ent)
{
	gm_openstatus(ent, 5);
	int owned = ent->client->inv ? ent->client->inv->NumWeapons() : 0;
	GM_Print(ent, SP_DM_ARSENAL, ARS_LAYOUT_STATUS, owned);
	return true;
}

void GM_ArsenalScore(edict_t *self, edict_t *attacker)
{
	if (GM_IsSpectator(self))
		return;
	gm_client_t *v = GM_Cl(self);
	if (self->client->inv) {
		self->client->inv->WriteInfo(v->arsenal_inv, true, false, false, false);
		v->arsenal_have = true;
	}
	if (attacker == self) {
		float pen = sv_suicidepenalty ? sv_suicidepenalty->value : 2.0f;
		GM_SetScore(self, (int)((float)GM_GetScore(self) - pen));
		return;
	}
	if (!GM_IsClient(attacker)) {
		GM_AddScore(self, -1);
		return;
	}
	GM_AddScore(attacker, 1);
	int mod = meansOfDeath & ~MOD_FRIENDLY_FIRE;
	if (mod == MOD_TELEFRAG || mod == MOD_GRENADE || mod == MOD_C4)
		return;
	PlayerInventory *inv = attacker->client->inv;
	if (!inv)
		return;
	inv->RequestDrop();
	int owned = inv->NumWeapons();
	if (owned == 1) {
		GM_AddScore(attacker, 10);
		GM_SoundAll("dm/arsenal/bigwinner.wav", 0.6f, ATTN_NORM);
		GM_Print(NULL, SP_DM_ARSENAL, ARS_TEXT_BIG_WINNER, GM_EntNum(attacker));
		gm.arsenal_newround = true;
	} else if (owned == 2 && !gm.arsenal_announced) {
		gm.arsenal_announced = true;
		GM_SoundAll("dm/arsenal/lastweapon.wav", 1.0f, ATTN_NORM);
		GM_Print(NULL, SP_DM_ARSENAL, ARS_TEXT_LAST_WEAPON, GM_EntNum(attacker));
	}
}

void GM_ArsenalFrame(void)
{
	if (!gm.arsenal_newround)
		return;
	gm.arsenal_newround = false;
	GM_ArsenalDrawSet();
	gm.arsenal_announced = false;
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (!e->inuse || !e->client)
			continue;
		GM_ArsenalKit(e, true);
		if (!GM_IsSpectator(e) && !level.intermissiontime)
			GM_StatusLayout(e);
	}
}

//===========================================================================================================
// status layouts and `help` (SPEC-790 B3)

bool GM_StatusLayout(edict_t *ent)
{
	if (!GM_IsClient(ent) || level.intermissiontime || GM_IsSpectator(ent))
		return false;
	if (gm.mode == GM_ASSASSIN)
		return gm_assassin_status(ent);
	if (gm.mode == GM_ARSENAL)
		return gm_arsenal_status(ent);
	return false;
}

void GM_StatusRefresh(edict_t *ent)
{
	gm_refreshing = true;
	GM_StatusLayout(ent);
	gm_refreshing = false;
}

void GM_Help(edict_t *ent)
{
	if (!GM_IsClient(ent) || level.intermissiontime)
		return;
	gclient_t *cl = ent->client;
	cl->showscores = false;
	if (cl->showhelp && level.time < cl->showhelp_time) {
		cl->showhelp = false;
		return;
	}
	// without a status layout nothing is drawn, but the layout area stays flagged for the 30 s (capture orig_dm:
	// stats[9] keeps bit 0x1 after `help` and the scoreboard refreshes stop)
	GM_StatusLayout(ent);
	cl->showhelp = true;
	cl->showhelp_time = level.time + 30;
}
