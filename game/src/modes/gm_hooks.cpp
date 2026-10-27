// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): the hooks other packages expose for the game modes and the Ghoul glue,
// installed at every level start: bot objectives (G5 BotModeHooks) and damage/gore (G3 G3Hooks).
// Implements: SPEC-790 4 (team rule, effective dmflags for damage), B6/B8/B9 (facts the bot objectives read),
//             SPEC-791 B1 (model records for gore bolt-ons)
#include "gm_internal.h"
#include "../bots/bot_api.h"
#include "../damage/damage_api.h"

static int hk_client_team(edict_t *p) { return GM_Team(p); }
static int hk_flag_team(edict_t *f) { return GM_CTFFlagTeam(f); }

static int hk_flag_at_base(edict_t *f)
{
	int team = GM_CTFFlagTeam(f);
	return team && GM_CTFBaseFlag(team) == f && GM_CTFFlagAtBase(team);
}

static edict_t *hk_flag_carrier(int team)
{
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (e->inuse && e->client && gm_cl[i].flagteam == team)
			return e;
	}
	return NULL;
}

static edict_t *hk_assassin_target(edict_t *p) { return GM_AssassinTarget(p); }

static int hk_assassin_hunts(edict_t *hunter, edict_t *victim)
{
	gm_client_t *c = GM_Cl(hunter);
	return gm.mode == GM_ASSASSIN && c && c->target == victim;
}

int GM_ControlOwner(edict_t *flag) { return (gm.mode == GM_CONTROL && flag) ? flag->count : 0; }
static int hk_control_owner(edict_t *flag) { return GM_ControlOwner(flag); }

edict_t *GM_AssassinTarget(edict_t *p)
{
	gm_client_t *c = GM_Cl(p);
	return (gm.mode == GM_ASSASSIN && c) ? c->target : NULL;
}

bool GM_Exhausted(edict_t *p)
{
	gm_client_t *c = GM_Cl(p);
	return gm.mode == GM_REALISTIC && c && c->exhausted;
}

static int hk_tired(edict_t *p) { return GM_Exhausted(p) ? 1 : 0; }

static bool hk_sameteam(edict_t *a, edict_t *b) { return GM_SameTeam(a, b); }
static void hk_playerhit(edict_t *targ, edict_t *attacker) { GM_ClientDamaged(targ, attacker); }

static ghoul::IGhoulObject *hk_modelobject(const char *dir, const char *model, const char *skin)
{
	// The body's bolt-on registry (SPEC-722 "Bolt-ons and skins", SPEC-AMEND-006 G9.16): the record always has
	// all-skins 0 and the skin name in its key; a named skin is registered by the any-material rule (materials in id
	// order, stopping at the first that provides it), none without a skin (every `Enemy/Bolt/w_*` weapon). Checked
	// against the original with G4's registration suite and `sptest.py` sp_reg_arm1/jpn1.
	GGModel *m;
	if (skin && skin[0]) {
		m = GG_FindModel(dir, model, false, skin);
		if (m && !GG_Prepared(m)) {
			GG_RegisterSkinAnyMaterial(m, skin);
			GG_Prepare(m);
		}
	} else {
		m = GG_FindModel(dir, model, false);
		if (m && !GG_Prepared(m))
			GG_Prepare(m);
	}
	return GG_Object(m);
}

void GM_InstallHooks(void)
{
	static BotModeHooks bh;
	bh.client_team = hk_client_team;
	bh.flag_team = hk_flag_team;
	bh.flag_at_base = hk_flag_at_base;
	bh.flag_carrier = hk_flag_carrier;
	bh.assassin_target = hk_assassin_target;
	bh.assassin_hunts = hk_assassin_hunts;
	bh.control_owner = hk_control_owner;
	bh.tired = hk_tired;
	Bot_SetModeHooks(&bh);
	g3hooks.dmflags = GM_DMFlags;
	g3hooks.sameTeam = hk_sameteam;
	g3hooks.dmPlayerHit = hk_playerhit;
	g3hooks.modelObject = hk_modelobject;
}
