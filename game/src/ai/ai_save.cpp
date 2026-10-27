/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700 "Savegame chunks of the AI" (ids, part 1 tables, part 2 per-object data, character chunks,
 * the AIAI record), SPEC-701 sense chunks, SPEC-710 decision records, SPEC-711 action records, SPEC-752 1.2 step 6
 * Owner: package G2.
 *
 * The AI block of a level save. Chunk ids, sizes and order are those of the specs; field offsets follow the specs'
 * record tables. Where a record has no slot for OpenMerc run-time state, offset 0 (documented as "ignored on load"
 * by the original) carries it. Animations, entities, script events and component references use the encodings of
 * SPEC-752 with OpenMerc's own animation tables (allowed for a module that reads its own saves, SPEC-752 2.1).
 * The Ghoul tables are G9's (GG_*), the bodies G3's (Body_*), the vehicle chunks G4's (Roster_*VehicleChunks).
 *
 * Self-test (no engine save needed): cvar `ai_savetest 1` saves the AI objects (without the Ghoul and body parts)
 * into memory at level time 20 s, releases them, loads them back and prints `ai savetest: ...` with the number of
 * objects and of differing record bytes (0 = round trip exact).
 */
#include "ai_decisions.h"
#include "../ghoulglue/gg_ghoul.h"
#include "../roster/roster_api.h"
#include "../scriptglue/g_scriptglue.h"

#include <algorithm>
#include <unordered_map>

namespace om_ai {

AI *NewAIForCode(int code); /* ai_object.cpp */
void ManagerReset(void);    /* ai_manager.cpp */
void ManagerAdd(AI *ai);    /* ai_manager.cpp */
std::vector<AI *> &ManagerAll(void);

/* ------------------------------------------------------------------------------------------------------------------
 * chunk I/O: the engine's container (imports 85/86), or memory for the self-test
 * ---------------------------------------------------------------------------------------------------------------- */
static std::vector<unsigned char> *mem_out;
static const std::vector<unsigned char> *mem_in;
static size_t mem_pos;

static int ChunkId(const char *id)
{
	return ((uint8_t)id[0] << 24) | ((uint8_t)id[1] << 16) | ((uint8_t)id[2] << 8) | (uint8_t)id[3];
}

static void PutChunk(const char *id, const void *data, int size)
{
	if (mem_out) {
		int h[2] = {ChunkId(id), size};
		const unsigned char *p = (const unsigned char *)h;
		mem_out->insert(mem_out->end(), p, p + 8);
		mem_out->insert(mem_out->end(), (const unsigned char *)data, (const unsigned char *)data + size);
		return;
	}
	if (gix.WriteChunk)
		gix.WriteChunk(ChunkId(id), data, size);
}

static bool GetChunk(const char *id, void *data, int size)
{
	if (mem_in) {
		if (mem_pos + 8 > mem_in->size())
			return false;
		int h[2];
		memcpy(h, &(*mem_in)[mem_pos], 8);
		if (h[0] != ChunkId(id) || h[1] != size || mem_pos + 8 + size > mem_in->size()) {
			gi.dprintf("ai savegame: expected chunk %.4s (%d bytes)\n", id, size);
			return false;
		}
		memcpy(data, &(*mem_in)[mem_pos + 8], size);
		mem_pos += 8 + size;
		return true;
	}
	if (!gix.ReadChunk)
		return false;
	return gix.ReadChunk(ChunkId(id), data, size, nullptr) == size;
}

void SaveChunk(const char *id, const void *data, int size) { PutChunk(id, data, size); }
bool LoadChunk(const char *id, void *data, int size) { return GetChunk(id, data, size); }

static void RosterWrite(void *, const char *id, const void *data, int size) { PutChunk(id, data, size); }
static bool RosterRead(void *, const char *id, void *data, int size) { return GetChunk(id, data, size); }

/* a vehicle chunk read ahead of AIAI, served to G4 afterwards */
struct PreChunk {
	const char *id;
	unsigned char data[64];
	int size;
	bool ok;
};
static bool PreRead(void *ctx, const char *id, void *data, int size)
{
	PreChunk *p = (PreChunk *)ctx;
	if (!p->ok || strcmp(id, p->id) || size != p->size)
		return false;
	memcpy(data, p->data, size);
	return true;
}

/* ------------------------------------------------------------------------------------------------------------------
 * ids (SPEC-700: one counter for all kinds, starts at 1, never reset; after a load max + 1)
 * ---------------------------------------------------------------------------------------------------------------- */
static uint32_t id_counter = 1;
static std::unordered_map<const void *, uint32_t> ids;       /* writing */
static std::unordered_map<uint32_t, Action *> load_act;      /* reading */
static std::unordered_map<uint32_t, Decision *> load_dec;
static std::unordered_map<uint32_t, Sense *> load_sense;
static uint32_t max_read;

static uint32_t IdOf(const void *p)
{
	if (!p)
		return 0;
	auto it = ids.find(p);
	return it == ids.end() ? 0 : it->second;
}

/* ------------------------------------------------------------------------------------------------------------------
 * SaveRec
 * ---------------------------------------------------------------------------------------------------------------- */
SaveRec::SaveRec(bool rd, int sz) : size(sz), reading(rd) { memset(data, 0, sizeof(data)); }

void SaveRec::Raw(int off, void *p, int n)
{
	if (off < 0 || off + n > size || off + n > (int)sizeof(data))
		return;
	if (reading)
		memcpy(p, data + off, n);
	else
		memcpy(data + off, p, n);
}

void SaveRec::I(int off, int &v) { Raw(off, &v, 4); }
void SaveRec::F(int off, float &v) { Raw(off, &v, 4); }
void SaveRec::V(int off, float *v) { Raw(off, v, 12); }

void SaveRec::B(int off, int &v)
{
	int32_t w = v ? 1 : 0;
	if (reading) {
		Raw(off, &w, 4);
		v = w & 0xFF;
	} else
		Raw(off, &w, 4);
}

void SaveRec::Ent(int off, edict_t *&e, int none)
{
	int32_t n = none;
	if (!reading) {
		n = e ? G_EdictNum(e) : none;
		Raw(off, &n, 4);
		return;
	}
	Raw(off, &n, 4);
	e = (n != none && n >= 0 && n < globals.max_edicts) ? &g_edicts[n] : nullptr;
}

void SaveRec::Anim(int off, const G3Anim *&a)
{
	static const struct {
		const G3Anim *table;
		int count;
	} tabs[3] = {{g3_humanAnims, g3_numHumanAnims}, {g3_dogAnims, g3_numDogAnims}, {g3_cowAnims, g3_numCowAnims}};
	int32_t ref = -1;
	if (!reading) {
		for (int k = 0; k < 3 && a; k++)
			if (a >= tabs[k].table && a < tabs[k].table + tabs[k].count)
				ref = (k + 1) * 65536 + (int)(a - tabs[k].table);
		Raw(off, &ref, 4);
		return;
	}
	Raw(off, &ref, 4);
	int t = ref < 0 ? 0 : ref / 65536, i = ref < 0 ? 0 : ref % 65536;
	a = (t >= 1 && t <= 3 && i < tabs[t - 1].count) ? &tabs[t - 1].table[i] : nullptr;
}

void SaveRec::Event(int off, int &done_token)
{
	int32_t code = -1;
	if (!reading) {
		code = done_token ? ScriptGlue_EncodeAiDone(done_token) : -1;
		Raw(off, &code, 4);
		return;
	}
	Raw(off, &code, 4);
	done_token = code == -1 ? 0 : ScriptGlue_DecodeAiDone(code);
}

void SaveRec::Act(int off, Action *&a)
{
	int32_t id = 0;
	if (!reading) {
		id = (int32_t)IdOf(a);
		Raw(off, &id, 4);
		return;
	}
	Raw(off, &id, 4);
	auto it = load_act.find((uint32_t)id);
	a = it == load_act.end() ? nullptr : it->second;
}

void SaveRec::Dec(int off, Decision *&d)
{
	int32_t id = 0;
	if (!reading) {
		id = (int32_t)IdOf(d);
		Raw(off, &id, 4);
		return;
	}
	Raw(off, &id, 4);
	auto it = load_dec.find((uint32_t)id);
	d = it == load_dec.end() ? nullptr : it->second;
}

/* ------------------------------------------------------------------------------------------------------------------
 * chunk ids and sizes by class code
 * ---------------------------------------------------------------------------------------------------------------- */
static const char *ActionChunk(int cls, int *size)
{
	switch (cls) {
	case 2: *size = 112; return "AIPA";
	case 3: *size = 124; return "AIDA";
	case 4: case 5: *size = 124; return "AIXA";
	case 6: *size = 112; return "AIEA";
	case 7: *size = 112; return "AIWA";
	case 8: *size = 124; return "AIDW";
	case 9: *size = 124; return "AIJA";
	case 10: *size = 120; return "AIFA";
	case 12: *size = 112; return "AICA";
	case 13: *size = 120; return "AIAA";
	case 14: *size = 124; return "AIRA";
	case 15: case 16: case 17: case 18: case 19: *size = 132; return "AIHE";
	case 20: case 21: *size = 132; return "AITK";
	default: *size = 112; return "AISA"; /* 1, 11 (and inert codes) */
	}
}

static const char *DecisionChunk(int kind, int *size)
{
	switch (kind) {
	case 2: case 11: case 12: *size = 160; return "AIPD";
	case 3: *size = 216; return "AIOD";
	case 4: *size = 232; return "AISD";
	case 5: *size = 200; return "AIDD";
	case 6: *size = 192; return "AIRD";
	case 7: *size = 184; return "AIHD";
	case 8: *size = 184; return "AIPD";
	case 9: *size = 220; return "AISD";
	case 13: case 14: *size = 164; return "AIPD";
	default: *size = 36; return "AIBD"; /* 1 and inert codes */
	}
}

static const char *SenseChunk(int code, int *size)
{
	switch (code) {
	case 2: *size = 88; return "AISS";
	case 3: case 5: *size = 88; return "AINS";
	case 6: *size = 92; return "AINS";
	case 4: *size = 448; return "AISO";
	default: *size = 88; return "AIAS";
	}
}

/* ------------------------------------------------------------------------------------------------------------------
 * records
 * ---------------------------------------------------------------------------------------------------------------- */
static void ActionRecord(Action *a, SaveRec &r)
{
	r.I(0, a->script_flags); /* OpenMerc: flag word of a scripted action (SPEC-750 12.1) */
	r.Anim(4, a->record);
	r.V(8, a->facing);
	r.V(20, a->destination);
	r.F(32, a->created);
	r.B(36, a->interrupted);
	r.I(40, a->stopped);
	r.Dec(44, a->owner);
	r.Act(48, a->linked);
	r.F(52, a->move_scale);
	r.F(56, a->turn_allowance);
	r.I(60, a->move_flags);
	r.F(64, a->start_time);
	r.F(68, a->duration);
	r.F(72, a->end_time);
	r.V(76, a->jump_impulse);
	r.F(88, a->landing_check);
	r.I(92, a->ends_with_anim);
	r.Event(96, a->done_token);
	r.B(100, a->lethal);
	r.I(104, a->target_bolt);
	r.Ent(108, a->target);
	a->SaveTail(r);
}

static void DecisionRecord(Decision *d, SaveRec &r)
{
	r.F(4, d->created);
	r.I(8, d->bonus);
	r.I(12, d->base_priority);
	r.F(16, d->success);
	r.F(20, d->time_limit);
	r.I(24, d->has_time_limit);
	r.F(28, d->clear_shot_time);
	r.I(32, d->clear_shot_result);
	d->SaveTail(r);
}

static void PerceptRecord(SaveRec &r, int off, Percept &p)
{
	r.Ent(off, p.ent, 0);
	r.V(off + 4, p.pos);
	r.F(off + 16, p.time);
	r.I(off + 20, p.kind);
}

static void SenseRecord(Sense *s, SaveRec &r)
{
	r.F(16, s->created);
	PerceptRecord(r, 20, s->character);
	PerceptRecord(r, 44, s->player);
	s->SaveTail(r);
}

static void CharacterRecord(AI *ai, SaveRec &r)
{
	int priv = (ai->dead ? 1 : 0) | (ai->spawner_made ? 2 : 0);
	r.I(0, priv); /* OpenMerc: run-time flags (death rule ran, made by a spawner) */
	if (r.reading) {
		ai->dead = priv & 1;
		ai->spawner_made = (priv & 2) != 0;
	}
	r.V(4, ai->velocity);
	r.V(16, ai->death_angles);
	r.F(28, ai->heel_r);
	r.F(32, ai->heel_l);
	r.I(36, ai->first_think_done);
	for (int i = 0; i < AI::NUM_BBOX; i++) {
		r.V(44 + 12 * i, ai->bbox_mins[i]);
		r.V(152 + 12 * i, ai->bbox_maxs[i]);
	}
	r.I(260, ai->bbox_current);
	r.F(264, ai->floor_height);
	r.I(268, ai->alerted);
	r.F(272, ai->alert_time);
	r.F(276, ai->reaction_time);
	if (r.reading)
		ai->had_player_record = 1; /* not in the record: a loaded character keeps its reaction moment */
	r.I(280, ai->perceive_teammates);
	r.F(284, ai->search_value);
	r.F(288, ai->recently_called);
	r.Raw(292, &ai->nav, 96);
	r.I(348, ai->too_close_latch); /* in the cache's unused words (SPEC-710) */
	r.I(352, ai->too_far_latch);
	r.Ent(388, ai->owner);
	r.Ent(392, ai->target);
	r.Act(396, ai->current);
	r.Act(400, ai->previous);
	r.I(404, ai->ground_linkcount);
	r.I(408, ai->awake);
	r.I(412, ai->call_for_help);
	r.I(416, ai->hostage);
	r.I(420, ai->buddy);
	r.I(424, ai->face_player);
	r.V(428, ai->retreat_dir);
	r.F(440, ai->scale);
	r.I(444, ai->sense_mask);
	r.F(448, ai->jump_reach);
	r.F(452, ai->jump_height);
	r.F(456, ai->vehicle_456);
	r.F(460, ai->step_height);
	r.I(464, ai->vehicle_464);
	r.I(468, ai->fire_flags);
	r.I(472, ai->low_priority);
	r.I(476, ai->sense_stamp);
	r.I(480, ai->view_mode);
	r.I(484, ai->frame_flag);
	r.I(488, ai->corpse_check);
	r.I(492, ai->rank);
	for (int i = 0; i < AI::NUM_PROFILE; i++)
		r.F(496 + 4 * i, ai->profile[i]);
	int32_t body = ai->body ? (int32_t)ai->body->saveId : 0;
	r.I(556, body);
	if (r.reading)
		ai->body = body ? Body_ById((uint32_t)body) : nullptr;
	int32_t ghoul = 0; /* the model object comes back with the entity's instance (G9's chunks) */
	r.I(560, ghoul);
	r.V(564, ai->aim_angles);
	r.V(576, ai->look_angles);
}

/* ------------------------------------------------------------------------------------------------------------------
 * collection (characters in manager order; their components in list order)
 * ---------------------------------------------------------------------------------------------------------------- */
struct Collected {
	std::vector<AI *> chars;
	std::vector<Action *> acts;
	std::vector<Decision *> decs;
	std::vector<Sense *> senses;
};

static void AddAct(Collected &c, Action *a)
{
	if (!a || ids.count(a))
		return;
	ids[a] = id_counter++;
	c.acts.push_back(a);
	AddAct(c, a->linked);
}

static void Collect(Collected &c)
{
	for (AI *ai : ManagerAll()) {
		edict_t *e = ai->owner;
		if (!e || !e->inuse || e->ai != ai)
			continue;
		c.chars.push_back(ai);
	}
	for (AI *ai : c.chars) {
		AddAct(c, ai->current);
		AddAct(c, ai->previous);
		for (Action *a : ai->queue)
			AddAct(c, a);
	}
	for (AI *ai : c.chars)
		for (Decision *d : ai->decisions)
			if (!ids.count(d)) {
				ids[d] = id_counter++;
				c.decs.push_back(d);
			}
	for (AI *ai : c.chars)
		for (Sense *s : ai->senses)
			if (!ids.count(s)) {
				ids[s] = id_counter++;
				c.senses.push_back(s);
			}
	for (AI *ai : c.chars)
		ids[ai] = id_counter++;
}

static void WriteTable(int n, const std::vector<std::pair<uint32_t, int>> &rows)
{
	int32_t count = n;
	PutChunk("AICT", &count, 4);
	for (const auto &row : rows) {
		int32_t c[2] = {(int32_t)row.first, (int32_t)row.second};
		PutChunk("AICC", c, 8);
	}
}

/* ------------------------------------------------------------------------------------------------------------------
 * write
 * ---------------------------------------------------------------------------------------------------------------- */
static void WriteAll(bool with_external)
{
	ids.clear();
	Collected c;
	if (with_external)
		GG_AssignIds(&id_counter);
	Collect(c);
	if (with_external)
		Body_AssignIds(&id_counter);

	/* part 1: Ghoul models, bolt-ons, object instances, actions, decisions, senses, bodies, characters */
	if (with_external) {
		GG_WriteTable(GG_KIND_MODEL);
		GG_WriteTable(GG_KIND_BOLT);
		GG_WriteTable(GG_KIND_INST);
	}
	std::vector<std::pair<uint32_t, int>> rows;
	for (Action *a : c.acts)
		rows.push_back({ids[a], a->ClassCode()});
	WriteTable((int)rows.size(), rows);
	rows.clear();
	for (Decision *d : c.decs)
		rows.push_back({ids[d], d->Kind()});
	WriteTable((int)rows.size(), rows);
	rows.clear();
	for (Sense *s : c.senses)
		rows.push_back({ids[s], s->ClassCode()});
	WriteTable((int)rows.size(), rows);
	if (with_external)
		Body_WriteTable();
	rows.clear();
	for (AI *ai : c.chars)
		rows.push_back({ids[ai], ai->type_code});
	WriteTable((int)rows.size(), rows);

	/* part 2: models, object instances, bolt-ons, actions, decisions, senses, bodies, characters */
	if (with_external) {
		GG_WriteData(GG_KIND_MODEL);
		GG_WriteData(GG_KIND_INST);
		GG_WriteData(GG_KIND_BOLT);
	}
	for (Action *a : c.acts) {
		int size;
		const char *id = ActionChunk(a->ClassCode(), &size);
		SaveRec r(false, size);
		ActionRecord(a, r);
		PutChunk(id, r.data, size);
	}
	for (Decision *d : c.decs) {
		int size;
		const char *id = DecisionChunk(d->Kind(), &size);
		SaveRec r(false, size);
		DecisionRecord(d, r);
		PutChunk(id, r.data, size);
		if (d->Kind() == DK_SCRIPTED)
			ScriptedSaveExtra(d, false);
	}
	for (Sense *s : c.senses) {
		int size;
		const char *id = SenseChunk(s->ClassCode(), &size);
		SaveRec r(false, size);
		SenseRecord(s, r);
		PutChunk(id, r.data, size);
	}
	if (with_external)
		Body_WriteData();
	for (AI *ai : c.chars) {
		if (with_external)
			Roster_WriteVehicleChunks(ai->owner, 0, RosterWrite, nullptr);
		SaveRec r(false, 588);
		CharacterRecord(ai, r);
		PutChunk("AIAI", r.data, 588);
		std::vector<int32_t> list;
		for (Sense *s : ai->senses)
			list.push_back((int32_t)ids[s]);
		int32_t n = (int32_t)list.size();
		PutChunk("AISL", &n, 4);
		if (n)
			PutChunk("AISE", list.data(), n * 4);
		list.clear();
		for (Decision *d : ai->decisions)
			list.push_back((int32_t)ids[d]);
		n = (int32_t)list.size();
		PutChunk("AIDL", &n, 4);
		if (n)
			PutChunk("AIDE", list.data(), n * 4);
		list.clear();
		for (Action *a : ai->queue)
			list.push_back((int32_t)ids[a]);
		n = (int32_t)list.size();
		PutChunk("AIAL", &n, 4);
		if (n)
			PutChunk("AIAE", list.data(), n * 4);
		if (with_external)
			Roster_WriteVehicleChunks(ai->owner, 1, RosterWrite, nullptr);
	}
	ids.clear();
}

void WriteLevelAI(void) { WriteAll(true); }

/* ------------------------------------------------------------------------------------------------------------------
 * read
 * ---------------------------------------------------------------------------------------------------------------- */
static std::vector<std::pair<uint32_t, int>> ReadTable(void)
{
	std::vector<std::pair<uint32_t, int>> rows;
	int32_t count = 0;
	if (!GetChunk("AICT", &count, 4))
		return rows;
	for (int i = 0; i < count; i++) {
		int32_t c[2] = {0, 0};
		if (!GetChunk("AICC", c, 8))
			break;
		rows.push_back({(uint32_t)c[0], c[1]});
		if ((uint32_t)c[0] > max_read)
			max_read = (uint32_t)c[0];
	}
	return rows;
}

static void ReadList(const char *lid, const char *eid, std::vector<int32_t> &out)
{
	int32_t n = 0;
	out.clear();
	if (!GetChunk(lid, &n, 4) || n <= 0 || n > 4096)
		return;
	out.resize(n);
	if (!GetChunk(eid, out.data(), n * 4))
		out.clear();
}

/* a loaded character that is not taken over: its components are released with the other unused ones */
static void DropAI(AI *ai)
{
	ai->current = ai->previous = nullptr;
	ai->queue.clear();
	ai->decisions.clear();
	ai->senses.clear();
	delete ai;
}

static void ReadAll(bool with_external)
{
	/* every AI object of the current level is released; the entities' references are cleared */
	ManagerReset();
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		e->ai = nullptr;
		if (e->inuse && e->g3body)
			e->g3body->owner = nullptr; /* re-linked to the loaded AI object below */
		/* helper entities ("Nug Buddy", SPEC-700 rule 16) keep their restored entity records (the original keeps
		 * every entity across a load; G10) and are re-linked to their character's AI object below */
	}
	load_act.clear();
	load_dec.clear();
	load_sense.clear();
	max_read = 0;

	/* part 1 */
	if (with_external) {
		GG_ReadTable(GG_KIND_MODEL);
		GG_ReadTable(GG_KIND_BOLT);
		GG_ReadTable(GG_KIND_INST);
	}
	std::vector<std::pair<uint32_t, Action *>> acts;
	for (const auto &row : ReadTable()) {
		Action *a = NewAction(row.second);
		a->Init(nullptr, nullptr, nullptr, nullptr, nullptr, 0, false);
		load_act[row.first] = a;
		acts.push_back({row.first, a});
	}
	std::vector<std::pair<uint32_t, Decision *>> decs;
	for (const auto &row : ReadTable()) {
		Decision *d = MakeDecision(row.second);
		load_dec[row.first] = d;
		decs.push_back({row.first, d});
	}
	std::vector<std::pair<uint32_t, Sense *>> senses;
	for (const auto &row : ReadTable()) {
		Sense *s = MakeSense(row.second, 75.0f);
		load_sense[row.first] = s;
		senses.push_back({row.first, s});
	}
	if (with_external)
		Body_ReadTable();
	std::vector<std::pair<uint32_t, AI *>> chars;
	for (const auto &row : ReadTable())
		chars.push_back({row.first, NewAIForCode(row.second)});

	/* part 2 */
	if (with_external) {
		GG_ReadData(GG_KIND_MODEL);
		GG_ReadData(GG_KIND_INST);
		GG_ReadData(GG_KIND_BOLT);
	}
	for (auto &p : acts) {
		int size;
		const char *id = ActionChunk(p.second->ClassCode(), &size);
		SaveRec r(true, size);
		if (GetChunk(id, r.data, size))
			ActionRecord(p.second, r);
	}
	for (auto &p : decs) {
		int size;
		const char *id = DecisionChunk(p.second->Kind(), &size);
		SaveRec r(true, size);
		if (GetChunk(id, r.data, size))
			DecisionRecord(p.second, r);
		if (p.second->Kind() == DK_SCRIPTED)
			ScriptedSaveExtra(p.second, true);
	}
	for (auto &p : senses) {
		int size;
		const char *id = SenseChunk(p.second->ClassCode(), &size);
		SaveRec r(true, size);
		if (GetChunk(id, r.data, size))
			SenseRecord(p.second, r);
	}
	if (with_external)
		Body_ReadData();
	std::vector<int32_t> list;
	for (auto &p : chars) {
		AI *ai = p.second;
		/* G4's chunks written before AIAI (AIHA 48 / AITA 52) are read now and handed to G4 once the owner is
		 * known from AIAI */
		SaveRec r(true, 588);
		bool heli = ai->type_code == TYPE_HELICOPTER;
		bool vehicle = ai->type_code == TYPE_TANK || ai->type_code == TYPE_SNOWCAT;
		PreChunk pre = {heli ? "AIHA" : "AITA", {0}, heli ? 48 : 52, false};
		if (with_external && (heli || vehicle))
			pre.ok = GetChunk(pre.id, pre.data, pre.size);
		if (!GetChunk("AIAI", r.data, 588)) {
			DropAI(ai);
			continue;
		}
		CharacterRecord(ai, r);
		ReadList("AISL", "AISE", list);
		for (int32_t id : list) {
			auto it = load_sense.find((uint32_t)id);
			if (it != load_sense.end())
				ai->senses.push_back(it->second);
		}
		ReadList("AIDL", "AIDE", list);
		for (int32_t id : list) {
			auto it = load_dec.find((uint32_t)id);
			if (it != load_dec.end())
				ai->decisions.push_back(it->second);
		}
		ReadList("AIAL", "AIAE", list);
		for (int32_t id : list) {
			auto it = load_act.find((uint32_t)id);
			if (it != load_act.end())
				ai->queue.push_back(it->second);
		}
		edict_t *e = ai->owner;
		if (with_external && e && pre.ok)
			Roster_ReadVehicleChunks(e, 0, PreRead, &pre);
		if (with_external && e)
			Roster_ReadVehicleChunks(e, 1, RosterRead, nullptr);
		if (!e || !e->inuse) {
			DropAI(ai);
			continue;
		}
		e->ai = ai;
		e->think = CharacterThink;
		if (ai->body)
			ai->body->owner = ai;
		if (e->ghoulInst)
			ai->ghoul_object = e->ghoulInst->GetGhoulObject();
		ManagerAdd(ai);
		for (Decision *d : ai->decisions)
			if (d->Kind() == DK_SCRIPTED)
				ScriptedAfterLoad(e, ai, d);
	}
	/* components no character took over are released (each exactly once) */
	std::unordered_map<Action *, bool> used_a;
	std::unordered_map<Decision *, bool> used_d;
	std::unordered_map<Sense *, bool> used_s;
	for (AI *ai : ManagerAll()) {
		for (Action *a : {ai->current, ai->previous})
			for (Action *x = a; x && !used_a[x]; x = x->linked)
				used_a[x] = true;
		for (Action *a : ai->queue)
			for (Action *x = a; x && !used_a[x]; x = x->linked)
				used_a[x] = true;
		for (Decision *d : ai->decisions)
			used_d[d] = true;
		for (Sense *s : ai->senses)
			used_s[s] = true;
	}
	for (auto &p : acts)
		if (!used_a[p.second])
			delete p.second;
	for (auto &p : decs)
		if (!used_d[p.second])
			delete p.second;
	for (auto &p : senses)
		if (!used_s[p.second])
			delete p.second;
	/* helpers: re-linked to the AI object of their owner; one whose owner has none is released.
	   Clear every helperlink first: the field was not FK_ENTITY-encoded (MODEB-GAPS crash A), so a
	   loaded absolute pointer from the saving session would make G_HelperUnlink fault. */
	for (int i = 0; i < globals.num_edicts; i++)
		g_edicts[i].helperlink = NULL;
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *h = &g_edicts[i];
		if (!h->inuse || !(h->flags & FL_AI_HELPER))
			continue;
		edict_t *o = h->owner;
		if (o && o->inuse && o->ai && !o->ai->helper) {
			o->ai->helper = h;
			G_HelperAppend(o, h); /* SPEC-AMEND-005 G2.8 (unlinks first, so a restored link is not doubled) */
		}
		else if (!(o && o->inuse && o->ai && o->ai->helper == h))
			G_FreeEdict(h);
	}
	if (max_read + 1 > id_counter)
		id_counter = max_read + 1;
	if (with_external) {
		uint32_t m = std::max(GG_MaxId(), Body_MaxId());
		if (m + 1 > id_counter)
			id_counter = m + 1;
	}
	load_act.clear();
	load_dec.clear();
	load_sense.clear();
}

void ReadLevelAI(void) { ReadAll(true); }

/* ------------------------------------------------------------------------------------------------------------------
 * self-test: memory round trip of the AI objects (ai_savetest 1)
 * ---------------------------------------------------------------------------------------------------------------- */
void SaveSelfTest(void)
{
	std::vector<unsigned char> first, second;
	uint32_t body_ids = 1000000; /* bodies need ids to be re-linked; a separate range keeps the images comparable */
	Body_AssignIds(&body_ids);
	uint32_t keep = id_counter;
	id_counter = 1;
	mem_out = &first;
	WriteAll(false);
	mem_out = nullptr;
	size_t nchars = ManagerAll().size();
	gi.cprintf(nullptr, PRINT_HIGH, "ai savetest: written %d bytes\n", (int)first.size());
	mem_in = &first;
	mem_pos = 0;
	ReadAll(false);
	mem_in = nullptr;
	size_t nloaded = ManagerAll().size();
	gi.cprintf(nullptr, PRINT_HIGH, "ai savetest: read back, %d characters\n", (int)nloaded);
	id_counter = 1; /* same ids as the first write, so the two images can be compared byte for byte */
	mem_out = &second;
	WriteAll(false);
	mem_out = nullptr;
	if (keep > id_counter)
		id_counter = keep;
	size_t diff = 0;
	int shown = 0;
	for (size_t i = 0, chunk = 0; i < first.size() && i < second.size(); i++) {
		/* locate the chunk of byte i (8-byte headers: id, size) */
		while (chunk + 8 <= first.size()) {
			int sz;
			memcpy(&sz, &first[chunk + 4], 4);
			if (i < chunk + 8 + (size_t)sz)
				break;
			chunk += 8 + sz;
		}
		if (first[i] == second[i])
			continue;
		diff++;
		if (shown++ < 12 && chunk + 8 <= first.size()) {
			char id[5];
			id[0] = (char)first[chunk + 3];
			id[1] = (char)first[chunk + 2];
			id[2] = (char)first[chunk + 1];
			id[3] = (char)first[chunk];
			id[4] = 0;
			gi.cprintf(nullptr, PRINT_HIGH, "ai savetest: differs in %s at +%d (%02x/%02x)\n", id,
			           (int)(i - chunk - 8), first[i], second[i]);
		}
	}
	gi.cprintf(nullptr, PRINT_HIGH, "ai savetest: %d characters saved, %d loaded, image %d / %d bytes, %d differ\n",
	           (int)nchars, (int)nloaded, (int)first.size(), (int)second.size(),
	           (int)(diff + (first.size() > second.size() ? first.size() - second.size()
	                                                    : second.size() - first.size())));
}

} /* namespace om_ai */
