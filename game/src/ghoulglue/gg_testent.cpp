// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the Ghoul glue test entity `ghoul_model` (G9).
// Implements: SPEC-791 Behaviour 14
#include "gg_internal.h"

// flag and count are shared by every ghoul_model entity and reset at each spawn
static bool gm_seqended = false;
static int gm_seqcount = 0;

class GGTestSeqEnd : public ghoul::IGhoulCallback {
public:
	bool Event(uint32_t, void *, float, const void *) override
	{
		gm_seqended = true;
		return true;
	}
};
static GGTestSeqEnd gm_seqend_cb;

static void ghoul_model_think(edict_t *self)
{
	self->nextthink = level.time + 0.1f;
	if (!gm_seqended)
		return;
	gm_seqended = false;
	gg_devprint("my sequence has ended! hooha!\n");
	GGInst *oi = GG_ObjInstOfEntity(self);
	GGModel *m = GG_InstModel(oi);
	GhoulID seq = 0;
	if (gm_seqcount < 7)
		seq = GG_FindSequence(m, (gm_seqcount & 1) ? "std_istand_n_a_n" : "std_istretch_n_a_a");
	if (seq) {
		GG_Play(oi, seq, level.time, 0, true, ghoul::kPlayOnce);
		gm_seqcount++;
		return;
	}
	if (self->ghoulInst && m && m->obj)
		self->ghoulInst->RemoveNoteCallback(&gm_seqend_cb, m->obj->FindNote("EOS"));
	gg_devprint("getting rid of seq-end callback\n");
	GG_Play(oi, GG_FindSequence(m, "std_ilooking_n_a_a"), level.time, 0, true, ghoul::kPlayLoop);
}

void SP_ghoul_model(edict_t *ent)
{
	gm_seqended = false;
	gm_seqcount = 0;
	ent->movetype = MOVETYPE_STEP;
	ent->solid = SOLID_BBOX;
	ent->s.renderfx = RF_GHOUL;
	VectorSet(ent->mins, -16, -16, -32);
	VectorSet(ent->maxs, 16, 16, 16);
	GGModel *officer = GG_FindModel("enemy/ecto", "officer", true);
	GGModel *briefcase = GG_FindModel("enemy/bolt", "w_briefcase", true);
	GGInst *oi = GG_NewInst(officer, ent);
	if (oi) {
		GG_Play(oi, GG_FindSequence(officer, "std_ilooking_n_a_a"), level.time, 0, true, ghoul::kPlayOnce);
		if (officer->obj)
			oi->inst->AddNoteCallback(&gm_seqend_cb, officer->obj->FindNote("EOS"));
		if (briefcase && briefcase->obj) {
			GGBolt *b = GG_AddBolt(oi, officer->obj->FindPart("wbolt_hand_l"), briefcase,
			                       briefcase->obj->FindPart("to_wbolt_hand_l"));
			GG_PlayBolt(b, GG_FindSequence(briefcase, "w_briefcase"), level.time, 0, true, ghoul::kPlayStatic);
		}
	}
	ent->think = ghoul_model_think;
	ent->nextthink = level.time + 0.1f;
	gi.linkentity(ent);
}
