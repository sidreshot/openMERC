/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750 (sections 1-3, 5-12), SPEC-751 (sections 1, 3, 4), SPEC-140 (instruction encoding);
 *             SPEC-AMEND-005 G6b.7 (built-in 5 passes the value types)
 * Owner: package G6a (game/src/script).
 *
 * Level script system and the DesignerScript interpreter. No exceptions (the game module is built without
 * them): a game error sets ScriptSystem::error_, the running activation stops and the host raises the error.
 */
#include "script_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace om_script {

static const char STARS[] = "***********************************************\n"; /* 47 stars */

struct AiTableEntry {
	int id, kind, speed;
	const char *animation;
};
static const AiTableEntry ai_table[] = {
#include "script_ai_table.inc"
};

static const AiTableEntry *ai_entry(int id)
{
	for (const auto &e : ai_table)
		if (e.id == id)
			return &e;
	return nullptr;
}

static std::string fmt(const char *f, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, f);
	vsnprintf(buf, sizeof buf, f, ap);
	va_end(ap);
	return buf;
}

float quantize(float t)
{
	return floorf((t + 0.05f) * 10.0f) * 0.1f;
}

void queue_event(Instance *s, const Event &ev)
{
	/* SPEC-750 3.4: inserting an event inside a watch handler abandons everything the script waited for */
	if (s->in_handler) {
		s->events.clear();
		s->in_handler = false;
	}
	size_t pos = 0;
	while (pos < s->events.size() && s->events[pos].time <= ev.time)
		pos++;
	s->events.insert(s->events.begin() + pos, ev);
}

/* ------------------------------------------------------------------------------------------------------ */
/* ScriptSystem                                                                                           */

ScriptSystem::ScriptSystem(Host *host) : host_(host) {}
ScriptSystem::~ScriptSystem() {}

void ScriptSystem::clear_level()
{
	scripts_.clear();
	globals_.clear();
	rof_cache_.clear();
}

void ScriptSystem::fail(const std::string &msg)
{
	if (error_.empty())
		error_ = msg;
}

Variable *ScriptSystem::find_global(const char *name)
{
	for (auto &g : globals_)
		if (!strcmp(g->name, name))
			return g.get();
	return nullptr;
}

Variable *ScriptSystem::add_global(std::unique_ptr<Variable> v)
{
	globals_.push_back(std::move(v));
	return globals_.back().get();
}

bool load_code(Host *host, Instance *s)
{
	s->code.clear();
	if (!host->load_file(s->path.c_str(), s->code)) {
		host->dprint(STARS);
		host->dprint(fmt("Could not open script %s\n", s->path.c_str()).c_str());
		host->dprint(STARS);
		s->state = ST_FINISHED;
		return false;
	}
	int version = 0;
	if (s->code.size() >= 4)
		memcpy(&version, s->code.data(), 4);
	if (version != SCRIPT_VERSION) {
		host->dprint(STARS);
		host->dprint(fmt("Bad script version for %s: found %d, expecting %d\n", s->path.c_str(), version,
		                 SCRIPT_VERSION).c_str());
		host->dprint(STARS);
		s->state = ST_FINISHED;
		return false;
	}
	s->state = ST_READY;
	s->ip = CODE_START;
	return true;
}

int ScriptSystem::create(int runner, const char *script, const std::vector<std::string> &parms)
{
	std::unique_ptr<Instance> s(new Instance);
	s->path = std::string("ds/") + script + ".os";
	s->runner = runner;
	s->parms = parms;
	s->handle = (int)scripts_.size();
	load_code(host_, s.get());
	scripts_.push_back(std::move(s));
	return scripts_.back()->handle;
}

void ScriptSystem::use(int handle, int activator)
{
	if (handle < 0 || handle >= (int)scripts_.size())
		return;
	Instance *s = scripts_[handle].get();
	Event ev;
	ev.kind = EV_EXECUTE;
	ev.time = quantize(host_->level_time());
	ev.caller = s->runner;
	ev.activator = activator;
	queue_event(s, ev);
}

void ScriptSystem::ai_action_completed(int done_id)
{
	if (!done_id)
		return;
	for (auto &s : scripts_)
		for (auto &ev : s->events)
			if (ev.kind == EV_AI_DONE && ev.done_id == done_id)
				ev.completed = true;
}

int ScriptSystem::encode_ai_done(int done_id) const
{
	for (size_t i = 0; i < scripts_.size(); i++)
		for (size_t j = 0; j < scripts_[i]->events.size(); j++)
			if (scripts_[i]->events[j].kind == EV_AI_DONE && scripts_[i]->events[j].done_id == done_id)
				return (int)(i * 65536 + j);
	return -1;
}

int ScriptSystem::decode_ai_done(int code) const
{
	if (code < 0)
		return 0;
	size_t i = (size_t)code / 65536, j = (size_t)code % 65536;
	if (i >= scripts_.size() || j >= scripts_[i]->events.size() || scripts_[i]->events[j].kind != EV_AI_DONE)
		return 0;
	return scripts_[i]->events[j].done_id;
}

bool ScriptSystem::skipping_cinematic()
{
	return cinematic_ != 0 && host_->cvar_value("sv_jumpcinematic") != 0.0f;
}

void ScriptSystem::cinematic_start()
{
	/* SPEC-751 1.2 */
	host_->cinematic_remove_player_knives();
	if (host_->player_has_client() && cinematic_ == 0) {
		cinematic_ = 1;
		host_->add_command_text("-attack\n");
		host_->add_command_text("-altattack\n");
		host_->add_command_text("-weaponExtra1\n");
		host_->add_command_text("-weaponExtra2\n");
		host_->add_command_text("-use\n");
		host_->cinematic_weapon_hook();
	}
}

void ScriptSystem::cinematic_end()
{
	/* SPEC-751 1.4 */
	if (host_->cvar_value("sv_jumpcinematic") == 2.0f) {
		host_->cvar_set("sv_jumpcinematic", "0");
		host_->cvar_set("s_disable", "0");
	}
	cinematic_ = 0;
	stopsound_latch_ = true;
	host_->cinematic_end_actors();
}

bool ScriptSystem::rof_load(const std::string &name, const std::vector<uint8_t> **data)
{
	for (auto &c : rof_cache_)
		if (c.first == name) {
			*data = &c.second;
			return true;
		}
	std::vector<uint8_t> bytes;
	if (!host_->load_file(("ds/roff/" + name).c_str(), bytes)) {
		host_->dprint(STARS);
		host_->dprint(fmt("Could not open .ROF file '%s'\n", name.c_str()).c_str());
		host_->dprint(STARS);
		return false;
	}
	rof_cache_.push_back(std::make_pair(name, std::move(bytes)));
	*data = &rof_cache_.back().second;
	return true;
}

void ScriptSystem::rof_release(const std::string &name)
{
	for (size_t i = 0; i < rof_cache_.size(); i++)
		if (rof_cache_[i].first == name) {
			rof_cache_.erase(rof_cache_.begin() + i);
			return;
		}
}

/* WAIT_SIGNALS condition (SPEC-750 9.3) */
static bool wait_condition(ScriptSystem *sys, Instance *s)
{
	int set = 0;
	for (int slot : s->waited) {
		Value r;
		r.kind = VK_VAR;
		r.var = slot;
		if (as_int(s, r, sys) != 0)
			set++;
	}
	if (s->state == ST_WAIT_ALL)
		return set == (int)s->waited.size();
	return set == 1; /* WAIT_ANY: exactly one (two set never satisfies it) */
}

static void finish_wait(ScriptSystem *sys, Instance *s)
{
	if (s->clear_after_wait)
		for (int slot : s->waited) {
			Value r;
			r.kind = VK_VAR;
			r.var = slot;
			assign(s, r, temp_int(0), sys);
		}
	s->waited.clear();
	s->state = ST_READY;
}

/* READY, or waiting for signals whose condition now holds (which ends the wait) */
static bool runnable(ScriptSystem *sys, Instance *s)
{
	if (s->state == ST_READY)
		return true;
	if ((s->state == ST_WAIT_ALL || s->state == ST_WAIT_ANY) && wait_condition(sys, s)) {
		finish_wait(sys, s);
		return true;
	}
	return false;
}

void ScriptSystem::broadcast_signal(int ent, int kind)
{
	/* SPEC-750 9.2: delivered to every script of the level */
	for (size_t n = 0; n < scripts_.size() && !failed(); n++) {
		Instance *s = scripts_[n].get();
		bool matched = false;
		for (size_t i = 0; i < s->signals.size();) {
			const PendingSignal &p = s->signals[i];
			if (p.ent == ent && p.kind == kind) {
				Variable *v = (p.var >= 0 && p.var < MAX_SLOTS) ? s->vars[p.var] : nullptr;
				if (v && v->type == VT_INT)
					v->d.i++;
				s->signals.erase(s->signals.begin() + i);
				matched = true;
			} else {
				i++;
			}
		}
		if (matched && (s->state == ST_WAIT_ALL || s->state == ST_WAIT_ANY) && wait_condition(this, s)) {
			finish_wait(this, s);
			s->caller = ent;
			run_instance(s);
		}
	}
}

void ScriptSystem::run_instance(Instance *s)
{
	run(this, s);
}

static bool event_fires(ScriptSystem *sys, const Event &ev, float now)
{
	switch (ev.kind) {
	case EV_AI_DONE: return ev.completed;
	case EV_HELI_DONE:
	case EV_VEHICLE_DONE: return sys->host()->vehicle_action_done(ev.ent, ev.action_id);
	default: return now >= ev.time;
	}
}

static bool is_end_marker(const float d[3])
{
	return d[0] == 10000.0f && d[1] == 10000.0f && d[2] == 10000.0f;
}

void ScriptSystem::process(Instance *s)
{
	/* 3.1 step 1: the stopsound latch while a cinematic is being fast-forwarded */
	if (host_->cvar_value("sv_jumpcinematic") == 2.0f && stopsound_latch_) {
		stopsound_latch_ = false;
		host_->add_command_text("stopsound\n");
	}
	/* step 2: watches */
	check_watches(s);
	/* step 3: events; the scan restarts at the head after each firing event */
	while (!failed()) {
		float now = host_->level_time();
		size_t i = 0;
		while (i < s->events.size() && !event_fires(this, s->events[i], now))
			i++;
		if (i == s->events.size())
			break;
		Event ev = s->events[i];
		s->events.erase(s->events.begin() + i);
		switch (ev.kind) {
		case EV_MOVE_DONE:
			host_->finish_move(ev.ent);
			broadcast_signal(ev.ent, SIG_MOVE);
			break;
		case EV_ROTATE_DONE:
			host_->finish_rotate(ev.ent);
			broadcast_signal(ev.ent, SIG_ROTATE);
			break;
		case EV_WAIT:
			if (s->state == ST_WAIT_TIME)
				s->state = ST_READY;
			if (runnable(this, s))
				run(this, s);
			break;
		case EV_EXECUTE:
			if (runnable(this, s)) {
				s->caller = ev.caller;
				s->activator = ev.activator;
				run(this, s);
			}
			break;
		case EV_ROF_FRAME:
			if (is_end_marker(ev.dorg)) {
				broadcast_signal(ev.ent, SIG_ROF);
			} else {
				FieldValue o, a;
				if (host_->read_field(ev.ent, F_ORIGIN, o) && host_->read_field(ev.ent, F_ANGLES, a)) {
					for (int k = 0; k < 3; k++) {
						o.v[k] += ev.dorg[k];
						a.v[k] += ev.dang[k];
					}
					host_->write_field(ev.ent, F_ORIGIN, o);
					host_->write_field(ev.ent, F_ANGLES, a);
					host_->relink(ev.ent);
				}
				if (s->debug & 0x10)
					host_->dprint(fmt("ROF frame on entity %d\n", ev.ent).c_str());
			}
			break;
		case EV_AI_DONE: broadcast_signal(ev.ent, SIG_AI); break;
		case EV_HELI_DONE: broadcast_signal(ev.ent, SIG_HELI); break;
		case EV_VEHICLE_DONE: broadcast_signal(ev.ent, SIG_VEHICLE); break;
		}
	}
}

void ScriptSystem::run_frame()
{
	for (size_t i = 0; i < scripts_.size() && !failed(); i++)
		process(scripts_[i].get());
}

int ScriptSystem::script_state(int h) const
{
	return (h >= 0 && h < (int)scripts_.size()) ? scripts_[h]->state : -1;
}

int ScriptSystem::script_ip(int h) const
{
	return (h >= 0 && h < (int)scripts_.size()) ? scripts_[h]->ip : -1;
}

const char *ScriptSystem::script_path(int h) const
{
	return (h >= 0 && h < (int)scripts_.size()) ? scripts_[h]->path.c_str() : "";
}

int ScriptSystem::queued_events(int h) const
{
	return (h >= 0 && h < (int)scripts_.size()) ? (int)scripts_[h]->events.size() : 0;
}

bool ScriptSystem::variable_by_name(int h, const char *name, FieldValue &out, int &type) const
{
	if (h < 0 || h >= (int)scripts_.size())
		return false;
	const Instance *s = scripts_[h].get();
	for (int i = 0; i < MAX_SLOTS; i++)
		if (s->vars[i] && !strcmp(s->vars[i]->name, name)) {
			const Variable *v = s->vars[i];
			type = v->type;
			out.i = v->d.i;
			out.f = v->d.f;
			memcpy(out.v, v->d.v, sizeof out.v);
			out.e = v->d.e;
			return true;
		}
	return false;
}

/* ------------------------------------------------------------------------------------------------------ */
/* The interpreter                                                                                        */

namespace {

enum Step { STEP_CONTINUE, STEP_YIELD };

struct Ctx {
	ScriptSystem *sys;
	Host *host;
	Instance *s;
	int pos; /* read position inside the current instruction */
	bool bad; /* truncated instruction */

	uint8_t u8()
	{
		if (pos + 1 > (int)s->code.size()) {
			bad = true;
			return 0;
		}
		return s->code[pos++];
	}
	int32_t i32()
	{
		int32_t v = 0;
		if (pos + 4 > (int)s->code.size()) {
			bad = true;
			pos = (int)s->code.size();
			return 0;
		}
		memcpy(&v, &s->code[pos], 4);
		pos += 4;
		return v;
	}
	float f32()
	{
		int32_t i = i32();
		float f;
		memcpy(&f, &i, 4);
		return f;
	}
	void str(char *out, size_t n)
	{
		size_t k = 0;
		while (pos < (int)s->code.size() && s->code[pos]) {
			if (k + 1 < n)
				out[k++] = (char)s->code[pos];
			pos++;
		}
		out[k] = 0;
		if (pos >= (int)s->code.size())
			bad = true;
		else
			pos++; /* NUL */
	}

	void error(const std::string &m) { sys->fail(m); }
	bool pop(Value &v)
	{
		if (s->stack.empty())
			return false;
		v = s->stack.back();
		s->stack.pop_back();
		return true;
	}
	bool pop_n(Value *out, int n, const char *name)
	{
		/* out[0] receives the value pushed first */
		if ((int)s->stack.size() < n) {
			error(fmt("Invalid stack for %s", name));
			return false;
		}
		for (int i = n - 1; i >= 0; i--)
			pop(out[i]);
		return true;
	}
	void push(const Value &v) { s->stack.push_back(v); }

	int i(const Value &v) { return as_int(s, v, sys); }
	float f(const Value &v) { return as_float(s, v, sys); }
	void vec(const Value &v, float out[3]) { as_vector(s, v, sys, out); }
	int ent(const Value &v) { return as_entity(s, v, sys); }
	std::string text(const Value &v)
	{
		char b[STRING_LEN];
		as_string(s, v, sys, b);
		return b;
	}
	bool usable(int e) { return e != ENT_NONE && host->entity_in_use(e); }
	float player_health()
	{
		FieldValue fv;
		return host->read_field(1, F_HEALTH, fv) ? (float)fv.i : 0.0f;
	}
};

bool register_signal(Ctx &c, int ent, int kind, const Value &sig)
{
	if (sig.kind != VK_VAR)
		return true; /* not a variable: nothing to signal */
	/* SPEC-AMEND-003 1: "Renner Error #1" is never raised in 1.07f; a registration always appends a new
	 * entry, older entries for the same variable stay pending */
	PendingSignal p;
	p.ent = ent;
	p.kind = kind;
	p.var = sig.var;
	c.s->signals.push_back(p);
	return true;
}

/* -- declarations (SPEC-750 5) -- */
void read_init(Ctx &c, int type, Data &d)
{
	switch (type) {
	case VT_INT: d.i = c.i32(); break;
	case VT_FLOAT: d.f = c.f32(); break;
	case VT_VECTOR:
		for (int k = 0; k < 3; k++)
			d.v[k] = c.f32();
		break;
	case VT_ENTITY: d.e = c.i32(); break;
	case VT_STRING: c.str(d.s, STRING_LEN); break;
	}
}

Variable *local_var(Instance *s, int slot)
{
	for (auto &o : s->owned)
		if (o.get() == s->vars[slot])
			return o.get();
	s->owned.emplace_back(new Variable);
	return s->owned.back().get();
}

bool declaration(Ctx &c, int op)
{
	char name[NAME_LEN];
	c.str(name, sizeof name);
	int type = c.u8();
	int index = c.i32();
	Data init;
	default_data(type, init);
	bool has_init = op == 0x01 || op == 0x03 || op == 0x05;
	if (has_init)
		read_init(c, type, init);
	if (c.bad)
		return false;
	Instance *s = c.s;

	if (op == 0x06) {
		if (index < 0 || index >= MAX_SLOTS) {
			c.error("Index for field out of range");
			return false;
		}
		FieldDesc &fd = s->fields[index];
		fd.present = true;
		copy_name(fd.name, name, NAME_LEN);
		fd.declared_type = type;
		fd.id = field_by_name(name);
		return true;
	}
	if (index < 0 || index >= MAX_SLOTS) {
		c.error(fmt("Index out of range: %d > %d", index, MAX_SLOTS - 1));
		return false;
	}
	if (op <= 0x01) { /* global: shared by name */
		Variable *g = c.sys->find_global(name);
		if (!g) {
			std::unique_ptr<Variable> v(new Variable);
			copy_name(v->name, name, NAME_LEN);
			v->type = type;
			v->d = init;
			g = c.sys->add_global(std::move(v));
		}
		s->vars[index] = g;
		bool known = false;
		for (int k : s->global_slots)
			known |= k == index;
		if (!known)
			s->global_slots.push_back(index);
		return true;
	}
	if (op <= 0x03) { /* local */
		Variable *v = local_var(s, index);
		copy_name(v->name, name, NAME_LEN);
		v->type = type;
		v->slot = index;
		v->d = init;
		s->vars[index] = v;
		bool known = false;
		for (int k : s->local_slots)
			known |= k == index;
		if (!known)
			s->local_slots.push_back(index);
		return true;
	}
	/* parameter (5.3): bound once, from the runner's next parameter string */
	for (int k : s->param_slots)
		if (s->vars[k] && !strcmp(s->vars[k]->name, name)) {
			/* executed again (SPEC-AMEND-003 4): a fresh variable with the default or inline initial value,
			 * no parameter string consumed */
			s->owned.emplace_back(new Variable);
			Variable *v = s->owned.back().get();
			copy_name(v->name, name, NAME_LEN);
			v->type = type;
			v->slot = index;
			v->d = init;
			s->vars[index] = v;
			return true;
		}
	if (s->parms.empty()) {
		c.error("Missing Parameter");
		return false;
	}
	std::string p = s->parms.front();
	s->parms.erase(s->parms.begin());
	Variable *v = local_var(s, index);
	copy_name(v->name, name, NAME_LEN);
	v->type = type;
	v->slot = index;
	v->d = init;
	switch (type) {
	case VT_INT: v->d.i = atoi(p.c_str()); break;
	case VT_FLOAT: v->d.f = (float)atof(p.c_str()); break;
	case VT_VECTOR: sscanf(p.c_str(), "%f %f %f", &v->d.v[0], &v->d.v[1], &v->d.v[2]); break;
	case VT_ENTITY: v->d.e = c.host->find_by_targetname(p.c_str()); break;
	default: break; /* strings cannot be passed: the value stays empty */
	}
	s->vars[index] = v;
	s->param_slots.push_back(index);
	return true;
}

/* -- built-in functions (SPEC-750 8) -- */
void mark_cinematic_actor(Ctx &c, int e)
{
	if (c.sys->cinematic_state() && e != ENT_NONE && c.host->has_ai(e)) {
		FieldValue fv;
		fv.i = 255;
		c.host->write_field(e, F_COUNT, fv);
	}
}

/* built-in arguments; a missing argument means the push produced no value ("Illegal push") */
bool pop_args(Ctx &c, Value *out, int n)
{
	if ((int)c.s->stack.size() < n)
		return false;
	for (int i = n - 1; i >= 0; i--)
		c.pop(out[i]);
	return true;
}

bool builtin(Ctx &c, int id, int n, Value &out)
{
	Value a[2];
	switch (id) {
	case 0:
	case 3: {
		if (!pop_args(c, a, 1))
			return false;
		std::string name = c.text(a[0]);
		int e = id == 0 ? c.host->find_by_targetname(name.c_str()) : c.host->find_by_scripttarget(name.c_str());
		mark_cinematic_actor(c, e);
		out = temp_entity(e);
		return true;
	}
	case 1:
	case 2: {
		if (!pop_args(c, a, 1))
			return false;
		float r = c.f(a[0]) * 3.14159265358979f / 180.0f;
		out = temp_float(id == 1 ? sinf(r) : cosf(r));
		return true;
	}
	case 4: out = temp_entity(c.host->find_by_classname("player")); return true;
	case 5: {
		std::vector<Host::SpawnKey> keys;
		for (int k = 0; k < n; k++) {
			Value key, value;
			if (!c.pop(key) || !c.pop(value)) {
				c.error("Invalid stack for HandleSpawn()");
				return false;
			}
			/* SPEC-AMEND-005 G6b.7: the value keeps its script type for the host */
			Host::SpawnKey sk;
			sk.key = c.text(key);
			sk.text = c.text(value);
			sk.type = value_type(c.s, value, c.sys);
			sk.v[0] = sk.v[1] = sk.v[2] = 0;
			sk.i = 0;
			sk.f = 0;
			if (sk.type == VT_VECTOR)
				as_vector(c.s, value, c.sys, sk.v);
			else if (sk.type == VT_FLOAT) {
				sk.f = as_float(c.s, value, c.sys);
				sk.i = (int)sk.f;
			} else if (sk.type == VT_INT || sk.type == VT_ENTITY) {
				sk.i = as_int(c.s, value, c.sys);
				sk.f = (float)sk.i;
			}
			keys.push_back(sk);
		}
		out = temp_entity(c.host->spawn_entity_typed(keys));
		return true;
	}
	case 6: out = temp_entity(c.s->caller); return true;
	case 7: out = temp_entity(c.s->activator); return true;
	case 8: {
		/* args a, b in push order; the engine random is called with (b, a): min = b, max = a */
		if (!pop_args(c, a, 2))
			return false;
		out = temp_float((float)c.host->irand(c.i(a[1]), c.i(a[0])));
		return true;
	}
	case 9: {
		if (!pop_args(c, a, 1))
			return false;
		out = temp_entity(c.i(a[0]) + 1);
		return true;
	}
	default: return false;
	}
}

/* PUSH operand at c.pos (opcode byte already consumed) */
bool do_push(Ctx &c)
{
	int kind = c.u8();
	Value v;
	switch (kind) {
	case 0: v = temp_int(c.i32()); break;
	case 1: v = temp_float(c.f32()); break;
	case 2: {
		float x[3];
		for (int k = 0; k < 3; k++)
			x[k] = c.f32();
		v = temp_vector(x);
		break;
	}
	case 3: v = temp_entity(c.i32()); break;
	case 4: {
		char t[STRING_LEN];
		c.str(t, sizeof t);
		v = temp_string(t);
		break;
	}
	case 5:
	case 6: {
		int slot = c.i32();
		int field = kind == 6 ? c.i32() : -1;
		if (c.bad)
			return false;
		if (slot < 0 || slot >= MAX_SLOTS) {
			c.error(fmt("Index out of range: %d > %d", slot, MAX_SLOTS - 1));
			return false;
		}
		if (kind == 6 && (field < 0 || field >= MAX_SLOTS)) {
			c.error("Index for field out of range");
			return false;
		}
		if (!c.s->vars[slot]) {
			c.error("Illegal push");
			return false;
		}
		v.kind = kind == 5 ? VK_VAR : VK_FIELD;
		v.var = slot;
		v.field = field;
		v.type = c.s->vars[slot]->type;
		break;
	}
	case 7: {
		int id = c.u8();
		int n = id == 5 ? c.u8() : 0;
		if (c.bad)
			return false;
		if (!builtin(c, id, n, v)) {
			c.error("Illegal push");
			return false;
		}
		break;
	}
	default:
		c.error("Illegal push");
		return false;
	}
	if (c.bad)
		return false;
	c.push(v);
	return true;
}

/* -- MOVE / ROTATE (SPEC-750 11) -- */
void move_rotate(Ctx &c, bool rotate, int flags, int e, const float vec[3], bool has_dur, float dur, bool has_speed,
                 float speed, const Value *sig)
{
	if (!c.usable(e))
		return;
	Host *h = c.host;
	FieldId pf = rotate ? F_ANGLES : F_ORIGIN;
	FieldValue cur;
	h->read_field(e, pf, cur);
	if (!has_dur && !has_speed) {
		/* instant form: no event, no signal, no relink */
		float delta[3], dst[3];
		for (int k = 0; k < 3; k++) {
			if (!rotate && (flags & 0x08)) {
				dst[k] = vec[k];
				delta[k] = vec[k] - cur.v[k];
			} else {
				dst[k] = cur.v[k] + vec[k];
				delta[k] = vec[k];
			}
		}
		FieldValue nv = cur;
		memcpy(nv.v, dst, sizeof dst);
		h->write_field(e, pf, nv);
		int fol = h->follower(e);
		if (c.usable(fol)) {
			FieldValue fv;
			if (h->read_field(fol, pf, fv)) {
				for (int k = 0; k < 3; k++)
					fv.v[k] += delta[k];
				h->write_field(fol, pf, fv);
			}
		}
		return;
	}
	float D[3], diff[3];
	for (int k = 0; k < 3; k++)
		D[k] = (flags & 0x08) ? vec[k] : cur.v[k] + vec[k];
	for (int k = 0; k < 3; k++)
		diff[k] = D[k] - cur.v[k];
	float d = sqrtf(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
	float s = has_speed ? speed : d / dur; /* may be infinite or not-a-number, as in the original */
	if (has_speed && has_dur && d > 0.0f) {
		/* the original computes the destination away from the requested point (SPEC-750 11) */
		for (int k = 0; k < 3; k++)
			D[k] = cur.v[k] - diff[k] / d * (speed * dur);
		for (int k = 0; k < 3; k++)
			diff[k] = D[k] - cur.v[k];
		d = sqrtf(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
	}
	/* frame count in floating point, unguarded (SPEC-AMEND-003 5): MOVE tests the distance, ROTATE the speed.
	 * Speed 0 makes a MOVE's N infinite (its done event never fires) and completes a ROTATE at once. Where the
	 * original's velocity would be meaningless or not-a-number (N <= 0, infinite, d = 0) velocity 0 is used. */
	float N;
	if (!rotate)
		N = d > 0.0f ? floorf(10.0f * d / s) + 1.0f : 0.0f;
	else
		N = s > 0.0f ? floorf(10.0f * d / s) + 1.0f : 0.0f;
	FieldValue vel;
	bool finite_n = N > 0.0f && N <= 1.0e30f;
	for (int k = 0; k < 3; k++)
		vel.v[k] = finite_n && d > 0.0f ? diff[k] * 10.0f / N : 0.0f;
	h->write_field(e, rotate ? F_ANGLE_VELOCITY : F_VELOCITY, vel);
	if (!rotate)
		h->set_move_destination(e, D);
	if (c.s->debug & (rotate ? 0x04 : 0x02))
		h->dprint(fmt("%s Entity %d From (%g %g %g) To (%g %g %g)\n", rotate ? "Rotating" : "Moving", e, cur.v[0],
		              cur.v[1], cur.v[2], D[0], D[1], D[2]).c_str());
	Event ev;
	ev.kind = rotate ? EV_ROTATE_DONE : EV_MOVE_DONE;
	ev.time = quantize(h->level_time() + N * 0.1f); /* +infinity: never due */
	ev.ent = e;
	queue_event(c.s, ev);
	if (sig)
		register_signal(c, e, rotate ? SIG_ROTATE : SIG_MOVE, *sig);
}

/* -- ROF (SPEC-751 3) -- */
void play_rof(Ctx &c, int e, const std::string &name, const Value *sig)
{
	if (!c.usable(e))
		return;
	Host *h = c.host;
	const std::vector<uint8_t> *data = nullptr;
	if (!c.sys->rof_load(name, &data))
		return;
	const std::vector<uint8_t> &d = *data;
	auto bad = [&](const std::string &m) {
		h->dprint(STARS);
		h->dprint((m + "\n").c_str());
		h->dprint(STARS);
	};
	if (d.size() < 12 || memcmp(d.data(), "ROFF", 4)) {
		bad(fmt("Bad header in .ROF file '%s'", name.c_str()));
		return;
	}
	int version;
	float count;
	memcpy(&version, &d[4], 4);
	memcpy(&count, &d[8], 4);
	if (version != 1) {
		bad(fmt("Bad version (%d) in .ROF file '%s'", version, name.c_str()));
		return;
	}
	if (!(count > 0.0f)) {
		bad(fmt("Bad count %g in .ROF file '%s'", count, name.c_str()));
		return;
	}
	if (sig && !register_signal(c, e, SIG_ROF, *sig))
		return;
	int C = (int)count;
	float t0 = h->level_time();
	for (int i = 0; i < C; i++) {
		Event ev;
		ev.kind = EV_ROF_FRAME;
		ev.ent = e;
		ev.time = quantize(t0 + (float)i * 0.1f);
		size_t off = 12 + (size_t)i * 24;
		if (off + 24 <= d.size()) { /* missing frames (count larger than the file) are zero deltas */
			memcpy(ev.dorg, &d[off], 12);
			memcpy(ev.dang, &d[off + 12], 12);
		}
		queue_event(c.s, ev);
	}
	if (sig) {
		Event ev;
		ev.kind = EV_ROF_FRAME;
		ev.ent = e;
		ev.time = quantize(t0 + (float)C * 0.1f);
		ev.dorg[0] = ev.dorg[1] = ev.dorg[2] = 10000.0f;
		queue_event(c.s, ev);
	}
}

/* -- WATCH (SPEC-750 10): evaluate the condition stored after the opcode at `off`. Returns -1 on error. -- */
int eval_watch(Ctx &c, int off, bool &result, int &target)
{
	c.pos = off;
	if (c.u8() != 0x33) {
		c.error("Invalid opcode when handling on statement");
		return -1;
	}
	for (int k = 0; k < 2; k++) {
		if (c.u8() != 0x11) {
			c.error("Expecting PUSH for on statement");
			return -1;
		}
		if (!do_push(c))
			return -1;
	}
	int cmp = c.u8();
	target = c.i32();
	Value v[2];
	if (c.bad || !c.pop_n(v, 2, "If"))
		return -1;
	result = compare(c.s, cmp, v[0], v[1], c.sys);
	return c.pos;
}

const char *invalid_name(int op)
{
	switch (op) {
	case 0x07: return "Assignment";
	case 0x08: return "Add";
	case 0x09: return "Subtract";
	case 0x0A: return "Multiply";
	case 0x0B: return "Divide";
	case 0x0C: return "AddAssignment";
	case 0x0D: return "SubtractAssignment";
	case 0x0E: return "MultiplyAssignment";
	case 0x0F: return "DivideAssignment";
	case 0x13: return "If";
	case 0x16: return "Time Wait";
	case 0x17:
	case 0x18: return "HandleWait";
	case 0x1D: return "Print";
	case 0x1E: return "PlaySound";
	case 0x23: return "HandleChacheSound";
	case 0x25: return "HandleSetViewAngles()";
	case 0x2B: return "HandleSetCvar()";
	case 0x2C: return "HandleUnloadSound";
	case 0x2F: return "HandleCacheROFF";
	case 0x30: return "HandleCacheStringPackage";
	case 0x31: return "HandleConsoleCommand()";
	case 0x32: return "HandleUnloadROFF";
	case 0x35: return "HandleResetAI()";
	default: return "statement";
	}
}

/* pops the optional arguments selected by `flags`, given in push order */
bool pop_optional(Ctx &c, int flags, const int *bits, int nbits, Value *out, const char *name)
{
	for (int k = nbits - 1; k >= 0; k--)
		if (flags & bits[k]) {
			if (!c.pop(out[k])) {
				c.error(fmt("Invalid stack for %s", name));
				return false;
			}
		}
	return true;
}

Step step(Ctx &c)
{
	Instance *s = c.s;
	Host *h = c.host;
	int start = s->ip;
	c.pos = start;
	c.bad = false;
	int op = c.u8();
	Step result = STEP_CONTINUE;
	Value a[4];

	switch (op) {
	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
		declaration(c, op);
		break;
	case 0x07:
		if (c.pop_n(a, 2, invalid_name(op)))
			assign(s, a[1], a[0], c.sys); /* value pushed first, target last */
		break;
	case 0x08: case 0x09: case 0x0A: case 0x0B:
		if (c.pop_n(a, 2, invalid_name(op))) {
			Value r;
			if (arith(s, op - 0x08, a[1], a[0], c.sys, r)) /* left = pushed last */
				c.push(r);
			else
				c.error("Illegal push");
		}
		break;
	case 0x0C: case 0x0D: case 0x0E: case 0x0F:
		if (c.pop_n(a, 2, invalid_name(op))) {
			Value r;
			if (arith(s, op - 0x0C, a[1], a[0], c.sys, r))
				assign(s, a[1], r, c.sys);
			else
				c.error("Illegal push");
		}
		break;
	case 0x10: {
		int t = c.i32();
		if (!c.bad) {
			s->ip = t;
			return STEP_CONTINUE;
		}
		break;
	}
	case 0x11: do_push(c); break;
	case 0x12: c.pop(a[0]); break;
	case 0x13: {
		int cmp = c.u8();
		int t = c.i32();
		if (!c.bad && c.pop_n(a, 2, "If") && !compare(s, cmp, a[0], a[1], c.sys)) {
			s->ip = t;
			return STEP_CONTINUE;
		}
		break;
	}
	case 0x14:
		s->state = ST_FINISHED;
		result = STEP_YIELD;
		break;
	case 0x16:
		if (c.pop_n(a, 1, "Time Wait")) {
			float secs = c.f(a[0]);
			if (secs > 0.0f) {
				Event ev;
				ev.kind = EV_WAIT;
				ev.time = quantize(h->level_time() + secs);
				queue_event(s, ev);
				s->state = ST_WAIT_TIME;
				result = STEP_YIELD;
			}
		}
		break;
	case 0x17:
	case 0x18: {
		int b = c.u8();
		int n = b & 0x7F;
		if ((int)s->stack.size() < n) {
			c.error("Invalid stack for HandleWait");
			break;
		}
		s->waited.clear();
		for (int k = 0; k < n; k++) {
			Value v;
			c.pop(v);
			if (v.kind == VK_VAR)
				s->waited.insert(s->waited.begin(), v.var);
		}
		s->clear_after_wait = b & 0x80;
		s->state = op == 0x17 ? ST_WAIT_ALL : ST_WAIT_ANY;
		if (wait_condition(c.sys, s))
			finish_wait(c.sys, s);
		else
			result = STEP_YIELD;
		break;
	}
	case 0x19:
	case 0x1C: {
		int flags = c.u8();
		static const int bits[3] = {0x01, 0x02, 0x04};
		Value opt[3];
		if (!pop_optional(c, flags, bits, 3, opt, op == 0x19 ? "Move" : "Rotate") ||
		    !c.pop_n(a, 2, op == 0x19 ? "Move" : "Rotate"))
			break;
		float v[3];
		c.vec(a[1], v);
		move_rotate(c, op == 0x1C, flags, c.ent(a[0]), v, (flags & 0x01) != 0, c.f(opt[0]), (flags & 0x02) != 0,
		            c.f(opt[1]), (flags & 0x04) ? &opt[2] : nullptr);
		break;
	}
	case 0x1A: {
		int b = c.u8();
		if (b == 0) {
			h->dprint(fmt("script %s ip %d\n", s->path.c_str(), start).c_str());
		} else if (b & 1) {
			s->debug |= b & ~1;
		} else {
			s->debug &= ~b;
		}
		break;
	}
	case 0x1B: {
		if (!c.pop_n(a, 1, "Use"))
			break;
		int e = c.ent(a[0]);
		if (c.sys->cinematic_state() && c.player_health() < 1.0f) {
			c.sys->cinematic_end();
			result = STEP_YIELD;
			break;
		}
		if (c.usable(e) && h->entity_has_use(e)) {
			int before = c.sys->cinematic_state();
			h->use_entity(e, s->caller, s->activator);
			if (h->has_ai(e))
				h->ai_clear_call_for_help(e);
			if (c.sys->cinematic_state() != before && c.player_health() < 1.0f) {
				c.sys->cinematic_end();
				result = STEP_YIELD;
			}
		}
		break;
	}
	case 0x1D: {
		/* SPEC-751 4: push order [0x01 target] [0x04 level] message */
		int flags = c.u8();
		Value msg, lvl, tgt;
		if (!c.pop(msg) || ((flags & 0x04) && !c.pop(lvl)) || ((flags & 0x01) && !c.pop(tgt))) {
			c.error("Invalid stack for Print");
			break;
		}
		if (c.sys->skipping_cinematic())
			break;
		int target = (flags & 0x01) ? c.ent(tgt) : ENT_NONE;
		if (value_type(s, msg, c.sys) == VT_STRING)
			h->print_text(target, (flags & 0x04) ? c.i(lvl) : 2, c.text(msg).c_str());
		else
			h->sp_print(target, c.i(msg));
		break;
	}
	case 0x1E: {
		/* SPEC-750 12.3: push order [0x01 ent] [0x02 vol] [0x04 attn] [0x08 chan] [0x10 timeofs] file */
		int flags = c.u8();
		Value file;
		static const int bits[5] = {0x01, 0x02, 0x04, 0x08, 0x10};
		Value opt[5];
		if (!c.pop(file)) {
			c.error("Invalid stack for PlaySound");
			break;
		}
		if (!pop_optional(c, flags, bits, 5, opt, "PlaySound") || c.sys->skipping_cinematic())
			break;
		int e = (flags & 0x01) ? c.ent(opt[0]) : ENT_NONE;
		float vol = (flags & 0x02) ? c.f(opt[1]) : 1.0f;
		float attn = (flags & 0x04) ? c.f(opt[2]) : 1.0f;
		int chan = (flags & 0x08) ? c.i(opt[3]) : 2;
		float tofs = (flags & 0x10) ? c.f(opt[4]) : 0.0f;
		h->play_sound(e, chan, h->sound_index(c.text(file).c_str()), vol, attn, tofs);
		break;
	}
	case 0x1F:
	case 0x20: {
		int flags = c.u8();
		if (flags == 0) {
			if (!c.pop_n(a, 1, op == 0x1F ? "Enable" : "Disable"))
				break;
			int e = c.ent(a[0]);
			if (c.usable(e)) {
				if (op == 0x1F)
					h->make_trigger(e);
				else
					h->disable_trigger(e);
			}
		} else if (flags == 2) {
			if (op == 0x1F)
				c.sys->cinematic_start();
			else
				c.sys->cinematic_end();
		}
		break;
	}
	case 0x21: {
		/* SPEC-750 12.1: push order entity, command, 0x40, 0x01, 0x02, 0x04, 0x08, 0x10, 0x80, 0x200, 0x400 */
		int flags = c.i32();
		static const int bits[9] = {0x40, 0x01, 0x02, 0x04, 0x08, 0x10, 0x80, 0x200, 0x400};
		Value opt[9];
		if (c.bad || !pop_optional(c, flags, bits, 9, opt, "Animate") || !c.pop_n(a, 2, "Animate"))
			break;
		int e = c.ent(a[0]);
		if (!c.usable(e) || !h->has_ai(e)) {
			h->dprint(fmt("entity %s had no ai--aborting HandleAnimate!\n", c.usable(e) ? h->entity_name(e) : "")
			              .c_str());
			break;
		}
		AiCommand cmd;
		cmd.flags = flags;
		cmd.command = c.i(a[1]);
		cmd.expression = (flags & 0x400) ? c.i(opt[8]) : 0;
		cmd.anim_param = (flags & 0x200) ? c.i(opt[7]) : 0;
		cmd.duration = (flags & 0x40) ? c.f(opt[0]) : (cmd.expression ? 9999.9f : 50.0f);
		cmd.count = (flags & 0x01) ? c.i(opt[1]) : 1;
		if (flags & 0x02)
			c.vec(opt[2], cmd.facing);
		if (flags & 0x04)
			c.vec(opt[3], cmd.destination);
		if (flags & 0x80)
			cmd.target = c.ent(opt[6]);
		if (!cmd.expression) {
			const AiTableEntry *t = ai_entry(cmd.command);
			if (t) {
				cmd.kind = t->kind;
				cmd.speed = t->speed;
				cmd.animation = t->animation;
			}
		}
		if (flags & 0x08) {
			if (!register_signal(c, e, SIG_AI, opt[4]))
				break;
			if (opt[4].kind == VK_VAR) {
				Event ev;
				ev.kind = EV_AI_DONE;
				ev.time = 0.0f;
				ev.ent = e;
				ev.done_id = c.sys->next_done_id();
				cmd.done_id = ev.done_id;
				queue_event(s, ev);
			}
		}
		h->ai_command(e, cmd);
		break;
	}
	case 0x22: {
		char t[256];
		c.str(t, sizeof t);
		if (!c.bad) {
			h->dprint(t);
			h->dprint("\n");
		}
		break;
	}
	case 0x23:
		if (c.pop_n(a, 1, invalid_name(op)) && !c.sys->skipping_cinematic())
			h->sound_index(c.text(a[0]).c_str());
		break;
	case 0x24: c.pop(a[0]); c.pop(a[1]); break;
	case 0x25:
		if (c.pop_n(a, 2, invalid_name(op))) {
			float v[3];
			c.vec(a[1], v);
			int e = c.ent(a[0]);
			if (c.usable(e))
				h->set_view_yaw(e, v[1]);
		}
		break;
	case 0x26: c.pop(a[0]); break;
	case 0x27:
		if (c.pop_n(a, 1, "Remove")) {
			int e = c.ent(a[0]);
			if (c.usable(e))
				h->free_entity(e);
		}
		break;
	case 0x28:
	case 0x2D: {
		/* SPEC-750 12.8: push order entity, command, [0x01 entity] [0x02 vector] [0x04 scalar] [0x08 signal] */
		int flags = c.u8();
		static const int bits[4] = {0x01, 0x02, 0x04, 0x08};
		Value opt[4];
		if (!pop_optional(c, flags, bits, 4, opt, op == 0x28 ? "Heli" : "Vehicle") ||
		    !c.pop_n(a, 2, op == 0x28 ? "Heli" : "Vehicle"))
			break;
		int e = c.ent(a[0]);
		if (!c.usable(e))
			break;
		float v[3] = {0, 0, 0};
		if (flags & 0x02)
			c.vec(opt[1], v);
		int target = (flags & 0x01) ? c.ent(opt[0]) : ENT_NONE;
		float scalar = (flags & 0x04) ? c.f(opt[2]) : 0.0f;
		int id = op == 0x28 ? h->heli_command(e, c.i(a[1]), target, v, scalar)
		                    : h->vehicle_command(e, c.i(a[1]), target, v, scalar);
		if (id == -2)
			break; /* no helicopter (vehicle) AI: nothing happens */
		if ((flags & 0x08) && !register_signal(c, e, op == 0x28 ? SIG_HELI : SIG_VEHICLE, opt[3]))
			break;
		Event ev;
		ev.kind = op == 0x28 ? EV_HELI_DONE : EV_VEHICLE_DONE;
		ev.time = quantize(h->level_time() + 1000.0f);
		ev.ent = e;
		ev.action_id = id;
		queue_event(s, ev);
		break;
	}
	case 0x29: {
		int flags = c.u8();
		Value sig;
		if (((flags & 0x01) && !c.pop(sig)) || !c.pop_n(a, 2, "ROF")) {
			if (!c.sys->failed())
				c.error("Invalid stack for ROF");
			break;
		}
		play_rof(c, c.ent(a[0]), c.text(a[1]), (flags & 0x01) ? &sig : nullptr);
		break;
	}
	case 0x2A:
		if (c.pop_n(a, 1, "Music")) {
			int n = c.i(a[0]);
			h->set_music_state(n < 0 ? 2 : n + 3);
		}
		break;
	case 0x2B:
		if (c.pop_n(a, 2, invalid_name(op))) { /* name pushed first, value last */
			std::string name = c.text(a[0]);
			if (value_type(s, a[1], c.sys) == VT_STRING)
				h->cvar_set(name.c_str(), c.text(a[1]).c_str());
			else
				h->cvar_set_value(name.c_str(), c.f(a[1]));
		}
		break;
	case 0x2C:
		if (c.pop_n(a, 1, invalid_name(op)))
			h->unload_sound(c.text(a[0]).c_str());
		break;
	case 0x2F:
		if (c.pop_n(a, 1, invalid_name(op))) {
			const std::vector<uint8_t> *d;
			c.sys->rof_load(c.text(a[0]), &d);
		}
		break;
	case 0x30:
		if (c.pop_n(a, 1, invalid_name(op)))
			h->register_string_package(c.text(a[0]).c_str());
		break;
	case 0x31:
		if (c.pop_n(a, 1, invalid_name(op))) {
			std::string t = c.text(a[0]) + "\n";
			h->add_command_text(t.c_str());
		}
		break;
	case 0x32:
		if (c.pop_n(a, 1, invalid_name(op)))
			c.sys->rof_release(c.text(a[0]));
		break;
	case 0x33: {
		bool r;
		int t;
		int end = eval_watch(c, start, r, t);
		if (end >= 0) {
			s->watches.push_back(start);
			c.pos = end;
		}
		break;
	}
	case 0x34:
		if (!s->in_handler) {
			c.error("Resume command executed outside of ON branch");
			break;
		}
		s->in_handler = false;
		s->ip = s->saved_ip;
		s->state = s->saved_state;
		return STEP_YIELD;
	case 0x35:
		if (c.pop_n(a, 1, invalid_name(op))) {
			int e = c.ent(a[0]);
			if (c.usable(e) && h->has_ai(e))
				h->ai_reset(e);
			else
				h->dprint(fmt("entity %s had no ai--aborting HandleResetAI!\n", c.usable(e) ? h->entity_name(e) : "")
				              .c_str());
		}
		break;
	default:
		/* SUSPEND (0x15) and every opcode without a handler: yield, state stays READY */
		result = STEP_YIELD;
		break;
	}
	if (c.bad && !c.sys->failed()) {
		/* truncated instruction at the end of the file: treat as end of code */
		s->ip = (int)s->code.size();
		s->state = ST_FINISHED;
		return STEP_YIELD;
	}
	s->ip = c.pos;
	return result;
}

} /* namespace */

void run(ScriptSystem *sys, Instance *s)
{
	if (s->running || sys->failed())
		return;
	s->running++;
	Ctx c;
	c.sys = sys;
	c.host = sys->host();
	c.s = s;
	c.pos = 0;
	c.bad = false;
	int executed = 0;
	while (!sys->failed()) {
		if (s->ip >= (int)s->code.size()) {
			s->state = ST_FINISHED;
			break;
		}
		if (++executed > INSTRUCTION_BUDGET) {
			sys->fail("Runaway loop for script OR more than 1000 instructions");
			break;
		}
		if (step(c) == STEP_YIELD)
			break;
	}
	s->running--;
}

bool ScriptSystem::check_watches(Instance *s)
{
	Ctx c;
	c.sys = this;
	c.host = host_;
	c.s = s;
	c.pos = 0;
	c.bad = false;
	for (size_t i = 0; i < s->watches.size() && !failed(); i++) {
		bool r = false;
		int target = 0;
		if (eval_watch(c, s->watches[i], r, target) < 0)
			return false;
		if (!r)
			continue;
		s->watches.erase(s->watches.begin() + i);
		s->saved_ip = s->ip;
		s->saved_state = s->state;
		s->in_handler = true;
		s->state = ST_READY;
		s->ip = target;
		run(this, s);
		return true;
	}
	return false;
}

/* ------------------------------------------------------------------------------------------------------ */
/* Structural decoder (SPEC-140), mirrors tools/formats/os_script.py                                      */

bool decode_script(const uint8_t *d, size_t n, std::vector<DecodedInstr> &out, std::string &err)
{
	out.clear();
	if (n < 5) {
		err = "file too short";
		return false;
	}
	int version;
	memcpy(&version, d, 4);
	if (version != SCRIPT_VERSION) {
		err = fmt("version %d != 3", version);
		return false;
	}
	size_t p = 4;
	bool cond_pending = false, code_seen = false;
	auto need = [&](size_t k) { return p + k <= n; };
	auto skip_str = [&]() {
		while (p < n && d[p])
			p++;
		if (p >= n)
			return false;
		p++;
		return true;
	};
	while (p < n) {
		size_t start = p;
		int op = d[p++];
		DecodedInstr ins;
		ins.offset = (int)start;
		ins.op = op;
		ins.declaration = false;
		if (cond_pending && op <= 6) {
			if (!need(4)) {
				err = fmt("truncated WATCH tail at 0x%x", (int)start);
				return false;
			}
			p += 4;
			ins.op = 0x100;
			ins.size = (int)(p - start);
			out.push_back(ins);
			cond_pending = false;
			continue;
		}
		if (op <= 6) {
			if (code_seen) {
				err = fmt("declaration after code at 0x%x", (int)start);
				return false;
			}
			if (!skip_str() || !need(5)) {
				err = fmt("bad declaration at 0x%x", (int)start);
				return false;
			}
			int t = d[p];
			p += 5;
			if (op == 1 || op == 3 || op == 5) {
				size_t k = t == VT_VECTOR ? 12 : 4;
				if (t == VT_STRING) {
					if (!skip_str()) {
						err = "bad string initial value";
						return false;
					}
				} else if (!need(k)) {
					err = "truncated initial value";
					return false;
				} else {
					p += k;
				}
			}
			ins.declaration = true;
			ins.size = (int)(p - start);
			out.push_back(ins);
			continue;
		}
		code_seen = true;
		switch (op) {
		case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
		case 0x12: case 0x14: case 0x15: case 0x16: case 0x1B: case 0x23: case 0x24: case 0x25: case 0x26:
		case 0x27: case 0x2A: case 0x2B: case 0x2C: case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x32:
		case 0x34: case 0x35:
			break;
		case 0x33: cond_pending = true; break;
		case 0x10:
		case 0x21:
			if (!need(4)) {
				err = "truncated operand";
				return false;
			}
			p += 4;
			break;
		case 0x13:
			if (!need(5)) {
				err = "truncated IF";
				return false;
			}
			p += 5;
			break;
		case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1C: case 0x1D: case 0x1E: case 0x1F: case 0x20:
		case 0x28: case 0x29: case 0x2D:
			if (!need(1)) {
				err = "truncated operand";
				return false;
			}
			p += 1;
			break;
		case 0x22:
			if (!skip_str()) {
				err = "bad debug string";
				return false;
			}
			break;
		case 0x11: {
			if (!need(1)) {
				err = "truncated PUSH";
				return false;
			}
			int kind = d[p++];
			size_t k = 0;
			switch (kind) {
			case 0: case 1: case 3: case 5: k = 4; break;
			case 2: case 6: k = kind == 2 ? 12 : 8; break;
			case 4:
				if (!skip_str()) {
					err = "bad string constant";
					return false;
				}
				break;
			case 7:
				if (!need(1)) {
					err = "truncated function id";
					return false;
				}
				k = d[p] == 5 ? 2 : 1;
				break;
			default:
				err = fmt("bad push kind %d at 0x%x", kind, (int)start);
				return false;
			}
			if (!need(k)) {
				err = "truncated PUSH operand";
				return false;
			}
			p += k;
			break;
		}
		default:
			err = fmt("unknown opcode 0x%02x at 0x%x", op, (int)start);
			return false;
		}
		ins.size = (int)(p - start);
		out.push_back(ins);
	}
	if (cond_pending) {
		err = "WATCH without condition tail";
		return false;
	}
	return true;
}

} /* namespace om_script */
