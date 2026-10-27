// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): CTF flags and Control points as items. G18's item code spawns the
// `item_ctf_flag` / `item_control_flag` entities (model, box, filters) and leaves their rules to the modes.
// Implements: SPEC-781 Behaviour 9 (CTF flags: base flags, pickup, recovery, drop, timed and inaccessible return),
//             Behaviour 10 (Control flags), SPEC-790 8 (flag returns at a capture)
#include "gm_internal.h"

enum {
	CTF_CAPTURE_FLAG_RED = 0,
	CTF_RECOVER_FLAG_RED = 1,
	CTF_INACCESSIBLE_RED = 7,
	CTF_RESPAWN_FLAG_RED = 11,
	CTF_CAPTURE_FLAG_BLUE = 14,
	CTF_RECOVER_FLAG_BLUE = 15,
	CTF_INACCESSIBLE_BLUE = 20,
	CTF_RESPAWN_FLAG_BLUE = 21,
	CTL_SWITCH_RED = 0,
	CTL_SWITCH_BLUE = 1
};

#define ITEM_CTF_FLAG 36
#define ITEM_CONTROL_FLAG 39
#define FLAG_LOOP_VOLUME 248

static edict_t *gm_baseflag[3];
static edict_t *gm_ctrlflag[5];
static int gm_nctrl;
static float gm_ctrlsound[5];
static float gm_droptime[MAX_EDICTS];
static float gm_basetouch[MAX_CLIENTS];

static bool gm_isflagitem(edict_t *e, int index)
{
	return e && e->inuse && e->item && (e->item - itemlist) + 1 == index;
}

static void gm_flagcs(int team, const char *v)
{
	gix.configstring(team == 1 ? CS_CTF_BLUE : CS_CTF_RED, v);
}

// light parts of the flag model; the stock flag models have none of them and the original prints nothing for the
// missing parts (capture orig_ctf), so a missing part is skipped silently here
static void gm_flagpart(edict_t *flag, const char *part, bool shown)
{
	ghoul::IGhoulObject *obj = flag->ghoulInst->GetGhoulObject();
	GhoulID id = obj ? obj->FindPart(part) : 0;
	if (id)
		flag->ghoulInst->SetPartOnOff(id, shown);
}

static void gm_flaglights(edict_t *flag, int team)
{
	if (!flag->ghoulInst)
		return;
	gm_flagpart(flag, "world_light", false);
	gm_flagpart(flag, "world_light_red", team == 2);
	gm_flagpart(flag, "world_light_blue", team == 1);
}

static void gm_flagskin(edict_t *flag, const char *skin)
{
	ghoul::IGhoulInst *inst = flag->ghoulInst;
	if (!inst)
		return;
	ghoul::IGhoulObject *obj = inst->GetGhoulObject();
	if (!obj)
		return;
	int n = obj->NumMaterials();
	for (int m = 1; m <= n; m++) {
		GhoulID s = obj->FindSkin((GhoulID)m, skin, 0);
		if (!s)
			s = obj->RegisterSkin((GhoulID)m, skin, 0);
		if (s)
			inst->SetSkin((GhoulID)m, s, 0);
	}
}

static void gm_capturesounds(int team)
{
	int tri = gix.soundindex("dm/ctf/triumphant.wav"), grab = gix.soundindex("dm/ctf/flaggrab.wav");
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (!e->inuse || !e->client)
			continue;
		gix.sound(e, CHAN_VOICE, e->client->resp.team == team ? tri : grab, 1, ATTN_NORM, 0, 1);
	}
}

static void gm_hideflag(edict_t *f)
{
	f->svflags |= SVF_NOCLIENT;
	f->solid = SOLID_NOT;
	gi.linkentity(f);
}

static void gm_showflag(edict_t *f)
{
	f->svflags &= ~SVF_NOCLIENT;
	f->solid = SOLID_TRIGGER;
	gi.linkentity(f);
}

//-----------------------------------------------------------------------------------------------------------
// returning and dropping

static void gm_takefromcarrier(edict_t *e)
{
	gm_client_t *c = GM_Cl(e);
	if (!c || !c->flagteam)
		return;
	c->flagteam = 0;
	if (e->s.sound == gix.soundindex("player/flaglp.wav"))
		e->s.sound = 0;
	if (e->client->inv)
		e->client->inv->RemoveItemType(IT_CTFFLAG);
}

static void gm_returnflag(int team)
{
	if (team < 1 || team > 2)
		return;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse)
			continue;
		if (GM_IsClient(e) && GM_Cl(e)->flagteam == team)
			gm_takefromcarrier(e);
		else if (e->classname && !strcmp(e->classname, "ctf_dropped_flag") && e->count == team)
			G_FreeEdict(e);
	}
	if (gm_baseflag[team])
		gm_showflag(gm_baseflag[team]);
	gm_flagcs(team, "h");
}

static void gm_flag_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);

static bool gm_in_hurt_trigger(edict_t *f)
{
	edict_t *list[64];
	int n = gi.BoxEdicts(f->absmin, f->absmax, list, 64, 2);
	for (int i = 0; i < n; i++)
		if (list[i]->inuse && list[i]->classname && !strcmp(list[i]->classname, "trigger_hurt"))
			return true;
	return false;
}

static void gm_droppedflag_think(edict_t *self)
{
	self->nextthink = level.time + 1.0f;
	int team = self->count;
	int n = G_EdictNum(self);
	int contents = gi.pointcontents(self->s.origin);
	float respawn = ctf_flag_respawn_time ? ctf_flag_respawn_time->value : 90.0f;
	int msg = -1;
	if ((contents & (CONTENTS_LAVA | CONTENTS_SLIME)) || gm_in_hurt_trigger(self))
		msg = team == 1 ? CTF_INACCESSIBLE_BLUE : CTF_INACCESSIBLE_RED;
	else if (n >= 0 && n < MAX_EDICTS && level.time - gm_droptime[n] >= respawn)
		msg = team == 1 ? CTF_RESPAWN_FLAG_BLUE : CTF_RESPAWN_FLAG_RED;
	if (msg < 0)
		return;
	GM_Print(NULL, SP_DM_CTF, msg);
	gix.positioned_sound(self->s.origin, self, CHAN_AUTO, gix.soundindex("dm/ctf/flagret.wav"), 1, ATTN_NONE, 0, 0);
	gm_returnflag(team);
}

// turn a dropped flag item into `ctf_dropped_flag` of the carrier's team
static void gm_adoptdrop(edict_t *drop, int team)
{
	drop->classname = (char *)"ctf_dropped_flag";
	drop->count = team;
	drop->touch = gm_flag_touch;
	drop->think = gm_droppedflag_think;
	drop->nextthink = level.time + 1.0f;
	int n = G_EdictNum(drop);
	if (n >= 0 && n < MAX_EDICTS)
		gm_droptime[n] = level.time;
	gm_flagskin(drop, team == 1 ? "ctf_flag_blue" : "ctf_flag_red");
	gm_flaglights(drop, team);
	gm_flagcs(team, "d");
}

static void gm_dropflag(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	if (!c || !c->flagteam)
		return;
	int team = c->flagteam;
	// the original writes `h` and then `d` for a flag dropped by a dying carrier, in the same frame, while the base
	// flag stays hidden (capture orig_ctf_drop); the `h` is reproduced
	gm_flagcs(team, "h");
	edict_t *drop = DropEquipItem(ent, IT_CTFFLAG, 1);
	gm_takefromcarrier(ent);
	if (drop)
		gm_adoptdrop(drop, team);
	else
		gm_returnflag(team); // nothing could be dropped (no model instance): the flag goes home
}

//-----------------------------------------------------------------------------------------------------------
// touches

static void gm_flag_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	(void)surf;
	if (gm.mode != GM_CTF || !GM_IsClient(other) || other->health <= 0 || !GM_TouchAllowed())
		return;
	other->client->spawnprotect_time = 0;
	bool dropped = self->classname && !strcmp(self->classname, "ctf_dropped_flag");
	if (dropped && self->owner == other && level.time < self->timestamp + 2)
		return;
	gm_client_t *c = GM_Cl(other);
	int team = self->count, own = other->client->resp.team;
	if ((own & team) == 0) {
		if (c->flagteam || !other->client->inv)
			return;
		other->client->inv->AddItems(IT_CTFFLAG, 1, -1);
		c->flagteam = team;
		if (!other->s.sound) {
			other->s.sound = gix.soundindex("player/flaglp.wav");
			other->s.sounddata = FLAG_LOOP_VOLUME;
		}
		if (dropped)
			G_FreeEdict(self);
		else
			gm_hideflag(self);
		char cs[4] = {'r', (char)G_EdictNum(other), 0, 0};
		gm_flagcs(team, cs);
		GM_Print(NULL, SP_DM_CTF, team == 1 ? CTF_CAPTURE_FLAG_BLUE : CTF_CAPTURE_FLAG_RED, GM_EntNum(other));
		gm_capturesounds(own);
		return;
	}
	if (dropped) {
		GM_AddScore(other, 2);
		GM_Print(NULL, SP_DM_CTF, team == 1 ? CTF_RECOVER_FLAG_BLUE : CTF_RECOVER_FLAG_RED, GM_EntNum(other), 2);
		gix.sound(other, CHAN_AUTO, gix.soundindex("dm/ctf/flagret.wav"), 1, ATTN_NONE, 0, 0);
		gm_returnflag(team);
		return;
	}
	int s = GM_Slot(other);
	if (s >= 0)
		gm_basetouch[s] = level.time + 5.0f;
}

static int gm_ctrlindex(edict_t *f)
{
	for (int i = 0; i < gm_nctrl; i++)
		if (gm_ctrlflag[i] == f)
			return i;
	return -1;
}

static void gm_control_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	(void)surf;
	if (gm.mode != GM_CONTROL || !GM_IsClient(other) || !GM_TouchAllowed())
		return;
	int team = other->client->resp.team;
	int old = self->count;
	if (team == old)
		return;
	GM_ControlSwitch(self, old, team);
	self->count = team;
	const char *name = (self->message && self->message[0]) ? self->message : NULL;
	GM_Print(NULL, SP_DM_CONTROL, team == 1 ? CTL_SWITCH_BLUE : CTL_SWITCH_RED, GM_EntNum(other), name ? name : "");
	int n = gm_ctrlindex(self);
	if (n >= 0) {
		char cs[80];
		Com_sprintf(cs, sizeof(cs), "%c%s", team == 1 ? 'B' : 'R', name ? name : "0");
		gix.configstring(CS_CONTROLPOINTS + n, cs);
		if (level.time - gm_ctrlsound[n] >= 0.5f) {
			gm_ctrlsound[n] = level.time;
			gm_capturesounds(team);
		}
	}
	gm_flagskin(self, team == 1 ? "ctf_flag_blue" : "ctf_flag_red");
	gm_flaglights(self, team);
}

//-----------------------------------------------------------------------------------------------------------
// level setup and the per-frame adoption of flags dropped by the player module

void GM_FlagsSetup(void)
{
	gm_baseflag[0] = gm_baseflag[1] = gm_baseflag[2] = NULL;
	gm_nctrl = 0;
	for (int i = 0; i < 5; i++)
		gm_ctrlsound[i] = -100;
	for (int i = 0; i < MAX_CLIENTS; i++)
		gm_basetouch[i] = 0;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (gm.mode == GM_CTF && gm_isflagitem(e, ITEM_CTF_FLAG)) {
			// The map's `count` is the team and is not defaulted (SPEC-AMEND-005 G9.10): 1 = Blue, anything else a
			// Red-looking flag whose team number stays the count (0 without the key: nobody's own flag).
			int team = e->count == 1 ? 1 : 2;
			e->classname = (char *)(team == 1 ? "ctf_flag_blue" : "ctf_flag_red");
			e->touch = gm_flag_touch;
			gm_flaglights(e, team);
			// the original sets the Blue flag's skin by names without a material name, which only prints the
			// warning (capture orig_ctf: one "WARNING: Can't find material:(null)" per CTF level)
			if (team == 1)
				GG_SetSkinInst(e->ghoulInst, "Items/ctf_flag", "flag_ground_idle", NULL, "ctf_flag_blue");
			gm_flagcs(team, "h");
			if (!gm_baseflag[team])
				gm_baseflag[team] = e;
		} else if (gm.mode == GM_CONTROL && gm_isflagitem(e, ITEM_CONTROL_FLAG) && gm_nctrl < 5) {
			e->count = 0;
			e->touch = gm_control_touch;
			gm_flaglights(e, 0);
			char cs[80];
			Com_sprintf(cs, sizeof(cs), "G%s", e->message ? e->message : "");
			gix.configstring(CS_CONTROLPOINTS + gm_nctrl, cs);
			gm_ctrlflag[gm_nctrl++] = e;
			GM_ControlRegisterPoint(e);
		}
	}
}

void GM_FlagsFrame(void)
{
	if (gm.mode != GM_CTF)
		return;
	for (int i = game.maxclients + 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!gm_isflagitem(e, ITEM_CTF_FLAG) || e->touch == gm_flag_touch || !(e->spawnflags & DROPPED_ITEM))
			continue;
		// a flag dropped through the player module (itemdrop): adopt it with its dropper's team
		edict_t *from = e->owner;
		int team = GM_Cl(from) ? GM_Cl(from)->flagteam : 0;
		if (!team) {
			G_FreeEdict(e);
			continue;
		}
		gm_takefromcarrier(from);
		gm_adoptdrop(e, team);
	}
}

//-----------------------------------------------------------------------------------------------------------
// services used by the CTF rules

void GM_CTFReturnFlag(int team) { gm_returnflag(team); }
edict_t *GM_CTFBaseFlag(int team) { return (team >= 1 && team <= 2) ? gm_baseflag[team] : NULL; }
bool GM_CTFFlagAtBase(int team)
{
	edict_t *f = GM_CTFBaseFlag(team);
	return f && f->inuse && !(f->svflags & SVF_NOCLIENT) && f->solid == SOLID_TRIGGER;
}
void GM_CTFDropCarriedFlag(edict_t *ent) { gm_dropflag(ent); }

int GM_CTFFlagTeam(edict_t *e)
{
	if (!e || !e->inuse || !e->classname)
		return 0;
	// the team number of a flag is its count (SPEC-AMEND-005 G9.10: 0 for a count-less, Red-looking flag)
	if (!strcmp(e->classname, "ctf_flag_blue") || !strcmp(e->classname, "ctf_flag_red") ||
	    !strcmp(e->classname, "ctf_dropped_flag"))
		return e->count;
	return 0;
}
