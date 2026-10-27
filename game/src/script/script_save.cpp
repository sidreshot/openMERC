/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-752 (section 4: script system chunks; level section steps 4 and 5)
 * Owner: package G6a (game/src/script).
 */
#include "script_internal.h"

#include <stdio.h>

namespace om_script {

namespace {

enum ObjKind {
	OK_INT = 1, OK_FLOAT = 2, OK_VECTOR = 3, OK_ENTITY = 4, OK_STRING = 5, OK_VARREF = 6, OK_FIELDREF = 7,
	OK_SIGNAL = 8, OK_MOVE_DONE = 9, OK_ROTATE_DONE = 10, OK_EXECUTE = 11, OK_WAIT = 12, OK_SCRIPT = 13,
	OK_FIELD = 14, OK_ROF_FRAME = 15, OK_AI_DONE = 16, OK_HELI_DONE = 17, OK_VEHICLE_DONE = 18
};

struct ObjInfo {
	int kind;
	const char *id;
	int size;
};
const ObjInfo obj_info[] = {
	{OK_INT, "SINT", 80}, {OK_FLOAT, "SFLT", 80}, {OK_VECTOR, "SVEC", 88}, {OK_ENTITY, "SENT", 80},
	{OK_STRING, "SSTR", 140}, {OK_VARREF, "SVVR", 80}, {OK_FIELDREF, "SFVV", 84}, {OK_SIGNAL, "SSIG", 16},
	{OK_MOVE_DONE, "SMDE", 20}, {OK_ROTATE_DONE, "SRDE", 20}, {OK_EXECUTE, "SEEX", 24}, {OK_WAIT, "SWEV", 16},
	{OK_FIELD, "SFDF", 84}, {OK_ROF_FRAME, "SMRE", 44}, {OK_AI_DONE, "SADE", 24}, {OK_HELI_DONE, "SHDE", 24},
	{OK_VEHICLE_DONE, "STDE", 24},
};

const ObjInfo *info(int kind)
{
	for (const auto &o : obj_info)
		if (o.kind == kind)
			return &o;
	return nullptr;
}

struct Buf {
	uint8_t b[260];
	explicit Buf(int n) : size(n) { memset(b, 0, sizeof b); }
	int size;
	void i32(int off, int v) { memcpy(b + off, &v, 4); }
	void f32(int off, float v) { memcpy(b + off, &v, 4); }
	void str(int off, const char *s, int n) { copy_name((char *)b + off, s, (size_t)n); }
	int gi(int off) const
	{
		int v;
		memcpy(&v, b + off, 4);
		return v;
	}
	float gf(int off) const
	{
		float v;
		memcpy(&v, b + off, 4);
		return v;
	}
	void gs(int off, char *out, int n) const
	{
		memcpy(out, b + off, (size_t)n);
		out[n - 1] = 0;
	}
};

void put_i32(ChunkWriter &w, const char *id, int v)
{
	w.write(id, &v, 4);
}

bool get_i32(ChunkReader &r, const char *id, int &v)
{
	return r.read(id, &v, 4);
}

void put_scid(ChunkWriter &w, int kind)
{
	put_i32(w, "SCID", kind);
}

/* value object header: 4..67 name, 68 type, 72 slot */
void value_header(Buf &b, const char *name, int type, int slot)
{
	b.str(4, name, NAME_LEN);
	b.i32(68, type);
	b.i32(72, slot);
}

void write_data(Buf &b, int type, const Data &d)
{
	switch (type) {
	case VT_INT: b.i32(76, d.i); break;
	case VT_FLOAT: b.f32(76, d.f); break;
	case VT_VECTOR:
		for (int k = 0; k < 3; k++)
			b.f32(76 + 4 * k, d.v[k]);
		break;
	case VT_ENTITY: b.i32(76, d.e); break;
	case VT_STRING: b.str(76, d.s, STRING_LEN); break;
	}
}

void read_data(const Buf &b, int type, Data &d)
{
	switch (type) {
	case VT_INT: d.i = b.gi(76); break;
	case VT_FLOAT: d.f = b.gf(76); break;
	case VT_VECTOR:
		for (int k = 0; k < 3; k++)
			d.v[k] = b.gf(76 + 4 * k);
		break;
	case VT_ENTITY: d.e = b.gi(76); break;
	case VT_STRING: b.gs(76, d.s, STRING_LEN); break;
	}
}

void write_variable(ChunkWriter &w, const Variable &v, int slot)
{
	const ObjInfo *oi = info(v.type + 1);
	Buf b(oi->size);
	value_header(b, v.name, v.type, slot);
	write_data(b, v.type, v.d);
	put_scid(w, oi->kind);
	w.write(oi->id, b.b, b.size);
}

void write_varref(ChunkWriter &w, int ref_slot)
{
	Buf b(80);
	value_header(b, "", VT_REFERENCE, -1);
	b.i32(76, ref_slot);
	put_scid(w, OK_VARREF);
	w.write("SVVR", b.b, b.size);
}

void write_stack_value(ChunkWriter &w, const Value &v)
{
	if (v.kind == VK_VAR) {
		write_varref(w, v.var);
		return;
	}
	if (v.kind == VK_FIELD) {
		Buf b(84);
		value_header(b, "", VT_REFERENCE, -1);
		b.i32(76, v.var);
		b.i32(80, v.field);
		put_scid(w, OK_FIELDREF);
		w.write("SFVV", b.b, b.size);
		return;
	}
	Variable t;
	t.type = v.type;
	t.d = v.d;
	write_variable(w, t, -1);
}

/* reads one value object; kind already read. Fills var (for kinds 1-5) or val (any kind). */
bool read_value_obj(ChunkReader &r, int kind, Variable &var, Value &val, int &slot)
{
	const ObjInfo *oi = info(kind);
	if (!oi || kind > OK_FIELDREF)
		return false;
	Buf b(oi->size);
	if (!r.read(oi->id, b.b, b.size))
		return false;
	b.gs(4, var.name, NAME_LEN);
	var.type = b.gi(68);
	slot = b.gi(72);
	val = Value();
	if (kind == OK_VARREF) {
		val.kind = VK_VAR;
		val.var = b.gi(76);
		return true;
	}
	if (kind == OK_FIELDREF) {
		val.kind = VK_FIELD;
		val.var = b.gi(76);
		val.field = b.gi(80);
		return true;
	}
	var.type = kind - 1;
	read_data(b, var.type, var.d);
	val.type = var.type;
	val.d = var.d;
	return true;
}

void write_event(ChunkWriter &w, const Event &ev)
{
	static const int kinds[] = {OK_MOVE_DONE, OK_ROTATE_DONE, OK_WAIT, OK_EXECUTE, OK_ROF_FRAME,
	                            OK_AI_DONE, OK_HELI_DONE, OK_VEHICLE_DONE};
	const ObjInfo *oi = info(kinds[ev.kind]);
	Buf b(oi->size);
	b.f32(4, ev.kind == EV_AI_DONE ? 0.0f : ev.time);
	b.i32(8, ev.kind);
	b.i32(12, (ev.kind == EV_WAIT || ev.kind == EV_EXECUTE) ? 0 : 10);
	switch (ev.kind) {
	case EV_MOVE_DONE:
	case EV_ROTATE_DONE: b.i32(16, ev.ent); break;
	case EV_EXECUTE:
		b.i32(16, ev.caller);
		b.i32(20, ev.activator);
		break;
	case EV_ROF_FRAME:
		b.i32(16, ev.ent);
		for (int k = 0; k < 3; k++) {
			b.f32(20 + 4 * k, ev.dorg[k]);
			b.f32(32 + 4 * k, ev.dang[k]);
		}
		break;
	case EV_AI_DONE:
		b.i32(16, ev.completed ? 1 : 0);
		b.i32(20, ev.ent);
		break;
	case EV_HELI_DONE:
	case EV_VEHICLE_DONE:
		b.i32(16, ev.ent);
		b.i32(20, ev.action_id);
		break;
	}
	put_scid(w, oi->kind);
	w.write(oi->id, b.b, b.size);
}

bool read_event(ChunkReader &r, int kind, Event &ev, ScriptSystem *sys)
{
	const ObjInfo *oi = info(kind);
	if (!oi || kind < OK_MOVE_DONE || kind == OK_SCRIPT || kind == OK_FIELD || kind == OK_SIGNAL)
		return false;
	Buf b(oi->size);
	if (!r.read(oi->id, b.b, b.size))
		return false;
	ev = Event();
	ev.time = b.gf(4);
	ev.kind = b.gi(8);
	switch (ev.kind) {
	case EV_MOVE_DONE:
	case EV_ROTATE_DONE: ev.ent = b.gi(16); break;
	case EV_EXECUTE:
		ev.caller = b.gi(16);
		ev.activator = b.gi(20);
		break;
	case EV_ROF_FRAME:
		ev.ent = b.gi(16);
		for (int k = 0; k < 3; k++) {
			ev.dorg[k] = b.gf(20 + 4 * k);
			ev.dang[k] = b.gf(32 + 4 * k);
		}
		break;
	case EV_AI_DONE:
		ev.completed = b.gi(16) != 0;
		ev.ent = b.gi(20);
		ev.done_id = sys->next_done_id();
		break;
	case EV_HELI_DONE:
	case EV_VEHICLE_DONE:
		ev.ent = b.gi(16);
		ev.action_id = b.gi(20);
		break;
	case EV_WAIT: break;
	default: return false;
	}
	return true;
}

void write_var_list(ChunkWriter &w, const char *id, const Instance *s, const std::vector<int> &slots)
{
	int n = 0;
	for (int k : slots)
		n += s->vars[k] != nullptr;
	put_i32(w, id, n);
	for (int k : slots)
		if (s->vars[k])
			write_variable(w, *s->vars[k], k);
}

} /* namespace */

void ScriptSystem::save(ChunkWriter &w)
{
	/* step 4: level globals */
	put_i32(w, "SGNM", (int)globals_.size());
	for (const auto &g : globals_)
		write_variable(w, *g, -1);

	/* step 5: scripts */
	put_i32(w, "SCVR", SCRIPT_VERSION);
	put_i32(w, "SCSZ", (int)scripts_.size());
	for (const auto &sp : scripts_) {
		const Instance *s = sp.get();
		put_scid(w, OK_SCRIPT);
		Buf name(260);
		name.str(0, s->path.c_str(), 260);
		w.write("SNAM", name.b, 260);
		int scrp[8] = {s->state, s->clear_after_wait, (int)s->code.size(), s->ip, s->debug, s->runner, s->caller,
		               s->activator};
		w.write("SCRP", scrp, sizeof scrp);

		int nf = 0;
		for (const auto &f : s->fields)
			nf += f.present;
		put_i32(w, "SFNM", nf);
		for (int k = 0; k < MAX_SLOTS; k++) {
			const FieldDesc &f = s->fields[k];
			if (!f.present)
				continue;
			Buf b(84);
			b.str(4, f.name, NAME_LEN);
			b.i32(68, f.declared_type);
			b.i32(72, 0); /* entity offset: re-resolved from the name on load (SPEC-752 4.3) */
			b.i32(76, (int)f.id);
			b.i32(80, k);
			put_scid(w, OK_FIELD);
			w.write("SFDF", b.b, b.size);
		}

		put_i32(w, "SGNM", (int)s->global_slots.size());
		for (int k : s->global_slots) {
			Buf b(68);
			b.i32(0, k);
			b.str(4, s->vars[k] ? s->vars[k]->name : "", NAME_LEN);
			w.write("SGLB", b.b, b.size);
		}
		write_var_list(w, "SLNM", s, s->local_slots);
		write_var_list(w, "SPNM", s, s->param_slots);

		put_i32(w, "SSNM", (int)s->stack.size());
		for (const auto &v : s->stack)
			write_stack_value(w, v);

		put_i32(w, "SWNM", (int)s->waited.size());
		for (int k : s->waited)
			write_varref(w, k);

		put_i32(w, "SINM", (int)s->signals.size());
		for (const auto &p : s->signals) {
			Buf b(16);
			b.i32(4, p.ent);
			b.i32(8, p.var >= 0 ? 1 : 0);
			b.i32(12, p.kind);
			put_scid(w, OK_SIGNAL);
			w.write("SSIG", b.b, b.size);
			if (p.var >= 0)
				write_varref(w, p.var);
		}

		put_i32(w, "SVNM", (int)s->parms.size());
		for (const auto &p : s->parms) {
			Variable v;
			copy_name(v.name, "parm", NAME_LEN);
			v.type = VT_STRING;
			copy_name(v.d.s, p.c_str(), STRING_LEN);
			write_variable(w, v, -1);
		}

		put_i32(w, "SENM", (int)s->events.size());
		for (const auto &ev : s->events)
			write_event(w, ev);

		put_i32(w, "SCNM", (int)s->watches.size());
		w.write("SCND", s->watches.data(), (int)(s->watches.size() * 4));
	}
}

bool ScriptSystem::load(ChunkReader &r)
{
	clear_level();
	int n;
	if (!get_i32(r, "SGNM", n))
		return false;
	for (int i = 0; i < n; i++) {
		int kind, slot;
		std::unique_ptr<Variable> v(new Variable);
		Value tmp;
		if (!get_i32(r, "SCID", kind) || !read_value_obj(r, kind, *v, tmp, slot) || kind > OK_STRING)
			return false;
		v->slot = -1;
		add_global(std::move(v));
	}

	int version, count;
	if (!get_i32(r, "SCVR", version))
		return false;
	if (version != SCRIPT_VERSION) {
		fail(std::string("LoadScripts(): Expecting version 3, found version ") + std::to_string(version));
		return false;
	}
	if (!get_i32(r, "SCSZ", count))
		return false;
	for (int i = 0; i < count; i++) {
		int kind;
		if (!get_i32(r, "SCID", kind) || kind != OK_SCRIPT)
			return false;
		std::unique_ptr<Instance> s(new Instance);
		Instance *sp = s.get();
		char path[260];
		if (!r.read("SNAM", path, 260))
			return false;
		path[259] = 0;
		sp->path = path;
		sp->handle = (int)scripts_.size();
		/* the file is read again from disk; a missing file or wrong version prints the SPEC-750 1.2 messages */
		load_code(host_, sp);
		int scrp[8];
		if (!r.read("SCRP", scrp, sizeof scrp))
			return false;
		sp->state = scrp[0];
		sp->clear_after_wait = scrp[1];
		sp->ip = scrp[3];
		sp->debug = scrp[4];
		sp->runner = scrp[5];
		sp->caller = scrp[6];
		sp->activator = scrp[7];

		int nf;
		if (!get_i32(r, "SFNM", nf))
			return false;
		for (int k = 0; k < nf; k++) {
			Buf b(84);
			if (!get_i32(r, "SCID", kind) || kind != OK_FIELD || !r.read("SFDF", b.b, b.size))
				return false;
			int slot = b.gi(80);
			if (slot < 0 || slot >= MAX_SLOTS)
				continue;
			FieldDesc &f = sp->fields[slot];
			f.present = true;
			b.gs(4, f.name, NAME_LEN);
			f.declared_type = b.gi(68);
			f.id = field_by_name(f.name);
		}

		int ng;
		if (!get_i32(r, "SGNM", ng))
			return false;
		for (int k = 0; k < ng; k++) {
			Buf b(68);
			if (!r.read("SGLB", b.b, b.size))
				return false;
			int slot = b.gi(0);
			char gname[NAME_LEN];
			b.gs(4, gname, NAME_LEN);
			if (slot >= 0 && slot < MAX_SLOTS) {
				sp->vars[slot] = find_global(gname); /* unknown name leaves the slot empty */
				sp->global_slots.push_back(slot);
			}
		}

		for (int list = 0; list < 2; list++) {
			int nv;
			if (!get_i32(r, list ? "SPNM" : "SLNM", nv))
				return false;
			for (int k = 0; k < nv; k++) {
				int slot;
				std::unique_ptr<Variable> v(new Variable);
				Value tmp;
				if (!get_i32(r, "SCID", kind) || !read_value_obj(r, kind, *v, tmp, slot) || kind > OK_STRING)
					return false;
				if (slot < 0 || slot >= MAX_SLOTS)
					continue;
				v->slot = slot;
				sp->vars[slot] = v.get();
				(list ? sp->param_slots : sp->local_slots).push_back(slot);
				sp->owned.push_back(std::move(v));
			}
		}

		int ns;
		if (!get_i32(r, "SSNM", ns))
			return false;
		for (int k = 0; k < ns; k++) {
			int slot;
			Variable v;
			Value val;
			if (!get_i32(r, "SCID", kind) || !read_value_obj(r, kind, v, val, slot))
				return false;
			sp->stack.push_back(val);
		}

		int nw;
		if (!get_i32(r, "SWNM", nw))
			return false;
		for (int k = 0; k < nw; k++) {
			int slot;
			Variable v;
			Value val;
			if (!get_i32(r, "SCID", kind) || kind != OK_VARREF || !read_value_obj(r, kind, v, val, slot))
				return false;
			sp->waited.push_back(val.var);
		}

		int nsig;
		if (!get_i32(r, "SINM", nsig))
			return false;
		for (int k = 0; k < nsig; k++) {
			Buf b(16);
			if (!get_i32(r, "SCID", kind) || kind != OK_SIGNAL || !r.read("SSIG", b.b, b.size))
				return false;
			PendingSignal p;
			p.ent = b.gi(4);
			p.kind = b.gi(12);
			if (b.gi(8)) {
				int slot;
				Variable v;
				Value val;
				if (!get_i32(r, "SCID", kind) || kind != OK_VARREF || !read_value_obj(r, kind, v, val, slot))
					return false;
				p.var = val.var;
			}
			sp->signals.push_back(p);
		}

		int np;
		if (!get_i32(r, "SVNM", np))
			return false;
		for (int k = 0; k < np; k++) {
			int slot;
			Variable v;
			Value val;
			if (!get_i32(r, "SCID", kind) || kind != OK_STRING || !read_value_obj(r, kind, v, val, slot))
				return false;
			sp->parms.push_back(v.d.s);
		}

		int ne;
		if (!get_i32(r, "SENM", ne))
			return false;
		for (int k = 0; k < ne; k++) {
			Event ev;
			if (!get_i32(r, "SCID", kind) || !read_event(r, kind, ev, this)) {
				host_->dprint("Warning: Unable to RestoreObject\n");
				return false;
			}
			sp->events.push_back(ev);
		}

		int nc;
		if (!get_i32(r, "SCNM", nc))
			return false;
		sp->watches.resize((size_t)nc);
		if (!r.read("SCND", sp->watches.data(), nc * 4))
			return false;

		scripts_.push_back(std::move(s));
		host_->attach_script(sp->runner, sp->handle);
	}
	return true;
}

} /* namespace om_script */
