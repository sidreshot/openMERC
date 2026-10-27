/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750 (sections 4.1-4.4: value types, conversions, arithmetic, comparison, entity fields)
 * Owner: package G6a (game/src/script).
 */
#include "script_internal.h"

#include <math.h>
#include <stdio.h>

namespace om_script {

static const struct {
	const char *name;
	FieldId id;
	int type;
} field_table[] = {
	{"x", F_X, VT_FLOAT}, {"y", F_Y, VT_FLOAT}, {"z", F_Z, VT_FLOAT},
	{"origin", F_ORIGIN, VT_VECTOR},
	{"origin_x", F_ORIGIN_X, VT_FLOAT}, {"origin_y", F_ORIGIN_Y, VT_FLOAT}, {"origin_z", F_ORIGIN_Z, VT_FLOAT},
	{"movetype", F_MOVETYPE, VT_INT},
	{"start_origin", F_START_ORIGIN, VT_VECTOR}, {"start_angles", F_START_ANGLES, VT_VECTOR},
	{"distance", F_DISTANCE, VT_FLOAT},
	{"state", F_STATE, VT_INT},
	{"owner", F_OWNER, VT_ENTITY},
	{"wait", F_WAIT, VT_FLOAT},
	{"velocity", F_VELOCITY, VT_VECTOR}, {"angle_velocity", F_ANGLE_VELOCITY, VT_VECTOR},
	{"team_chain", F_TEAM_CHAIN, VT_ENTITY},
	{"yaw_speed", F_YAW_SPEED, VT_FLOAT},
	{"modelindex", F_MODELINDEX, VT_INT},
	{"count", F_COUNT, VT_INT},
	{"solid", F_SOLID, VT_INT},
	{"angles", F_ANGLES, VT_VECTOR},
	{"health", F_HEALTH, VT_INT},
	{"loopsound", F_LOOPSOUND, VT_INT},
};

FieldId field_by_name(const char *name)
{
	for (const auto &f : field_table)
		if (!strcmp(f.name, name))
			return f.id;
	return F_NONE;
}

int field_natural_type(FieldId id)
{
	for (const auto &f : field_table)
		if (f.id == id)
			return f.type;
	return -1;
}

void default_data(int type, Data &d)
{
	d = Data();
	if (type == VT_ENTITY)
		d.e = ENT_WORLD; /* SPEC-750 4.1: a declared entity variable holds the world */
}

Value temp_int(int i)
{
	Value v;
	v.type = VT_INT;
	v.d.i = i;
	return v;
}

Value temp_float(float f)
{
	Value v;
	v.type = VT_FLOAT;
	v.d.f = f;
	return v;
}

Value temp_vector(const float x[3])
{
	Value v;
	v.type = VT_VECTOR;
	v.d.v[0] = x[0];
	v.d.v[1] = x[1];
	v.d.v[2] = x[2];
	return v;
}

Value temp_entity(int e)
{
	Value v;
	v.type = VT_ENTITY;
	v.d.e = e;
	return v;
}

Value temp_string(const char *text)
{
	Value v;
	v.type = VT_STRING;
	copy_name(v.d.s, text, STRING_LEN);
	return v;
}

static const Variable *var_of(const Instance *s, const Value &v)
{
	if (v.var < 0 || v.var >= MAX_SLOTS)
		return nullptr;
	return s->vars[v.var];
}

static FieldId field_of(const Instance *s, const Value &v)
{
	if (v.field < 0 || v.field >= MAX_SLOTS || !s->fields[v.field].present)
		return F_NONE;
	return s->fields[v.field].id;
}

/* Reads a field reference as a temporary of its natural type. False for "no field". */
static bool read_field_ref(const Instance *s, const Value &v, ScriptSystem *sys, Value &out)
{
	FieldId f = field_of(s, v);
	int type = field_natural_type(f);
	if (f == F_NONE || type < 0)
		return false;
	out = Value();
	out.type = type;
	const Variable *var = var_of(s, v);
	if (f == F_X || f == F_Y || f == F_Z) {
		if (var && var->type == VT_VECTOR)
			out.d.f = var->d.v[f - F_X];
		return true;
	}
	int ent = (var && var->type == VT_ENTITY) ? var->d.e : ENT_NONE;
	FieldValue fv;
	if (ent == ENT_NONE || !sys->host()->read_field(ent, f, fv))
		return true; /* zero value */
	switch (type) {
	case VT_INT: out.d.i = fv.i; break;
	case VT_FLOAT: out.d.f = fv.f; break;
	case VT_VECTOR: memcpy(out.d.v, fv.v, sizeof out.d.v); break;
	case VT_ENTITY: out.d.e = fv.e; break;
	}
	return true;
}

/* resolves references to a temporary (variable value, or field read as its natural type) */
static bool resolve(const Instance *s, const Value &v, ScriptSystem *sys, Value &out)
{
	if (v.kind == VK_TEMP) {
		out = v;
		return true;
	}
	if (v.kind == VK_VAR) {
		const Variable *var = var_of(s, v);
		if (!var)
			return false;
		out = Value();
		out.type = var->type;
		out.d = var->d;
		return true;
	}
	return read_field_ref(s, v, sys, out);
}

int value_type(const Instance *s, const Value &v, ScriptSystem *sys)
{
	(void)sys;
	if (v.kind == VK_TEMP)
		return v.type;
	if (v.kind == VK_VAR) {
		const Variable *var = var_of(s, v);
		return var ? var->type : -1;
	}
	return field_natural_type(field_of(s, v));
}

int as_int(const Instance *s, const Value &v, ScriptSystem *sys)
{
	Value t;
	if (!resolve(s, v, sys, t))
		return 0;
	bool field = v.kind == VK_FIELD;
	switch (t.type) {
	case VT_INT: return t.d.i;
	case VT_FLOAT:
		/* through a field reference only x/y/z (float components) convert, truncated */
		if (field) {
			FieldId f = field_of(s, v);
			if (f != F_X && f != F_Y && f != F_Z)
				return 0;
		}
		return (int)t.d.f;
	case VT_ENTITY: return field ? 0 : t.d.e;
	default: return 0;
	}
}

float as_float(const Instance *s, const Value &v, ScriptSystem *sys)
{
	Value t;
	if (!resolve(s, v, sys, t))
		return 0.0f;
	switch (t.type) {
	case VT_INT: return v.kind == VK_FIELD ? 0.0f : (float)t.d.i;
	case VT_FLOAT: return t.d.f;
	default: return 0.0f;
	}
}

void as_vector(const Instance *s, const Value &v, ScriptSystem *sys, float out[3])
{
	Value t;
	out[0] = out[1] = out[2] = 0.0f;
	if (resolve(s, v, sys, t) && t.type == VT_VECTOR)
		memcpy(out, t.d.v, sizeof t.d.v);
}

int as_entity(const Instance *s, const Value &v, ScriptSystem *sys)
{
	Value t;
	if (resolve(s, v, sys, t) && t.type == VT_ENTITY)
		return t.d.e;
	return ENT_NONE;
}

void as_string(const Instance *s, const Value &v, ScriptSystem *sys, char out[STRING_LEN])
{
	out[0] = 0;
	if (v.kind == VK_FIELD)
		return;
	Value t;
	if (resolve(s, v, sys, t) && t.type == VT_STRING)
		copy_name(out, t.d.s, STRING_LEN);
}

static float vlen(const float v[3])
{
	return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

bool arith(const Instance *s, int op, const Value &left, const Value &right, ScriptSystem *sys, Value &out)
{
	Value l;
	if (!resolve(s, left, sys, l))
		return false;
	int rtype = value_type(s, right, sys);
	out = Value();
	out.type = l.type;
	switch (l.type) {
	case VT_INT: {
		int a = l.d.i, b = as_int(s, right, sys);
		switch (op) {
		case 0: out.d.i = a + b; break;
		case 1: out.d.i = a - b; break;
		case 2: out.d.i = a * b; break;
		default:
			if (b == 0) {
				/* undefined in the original (division fault); a reimplementation must not crash */
				sys->host()->dprint("script: integer division by zero\n");
				out.d.i = 0;
			} else {
				out.d.i = a / b;
			}
		}
		return true;
	}
	case VT_FLOAT: {
		float a = l.d.f, b = as_float(s, right, sys);
		switch (op) {
		case 0: out.d.f = a + b; break;
		case 1: out.d.f = a - b; break;
		case 2: out.d.f = a * b; break;
		default: out.d.f = a / b; break;
		}
		return true;
	}
	case VT_VECTOR: {
		float b[3] = {0, 0, 0};
		if (rtype == VT_INT || rtype == VT_FLOAT) {
			float k = rtype == VT_INT ? (float)as_int(s, right, sys) : as_float(s, right, sys);
			b[0] = b[1] = b[2] = k;
		} else if (rtype == VT_VECTOR) {
			as_vector(s, right, sys, b);
		}
		for (int i = 0; i < 3; i++) {
			switch (op) {
			case 0: out.d.v[i] = l.d.v[i] + b[i]; break;
			case 1: out.d.v[i] = l.d.v[i] - b[i]; break;
			case 2: out.d.v[i] = l.d.v[i] * b[i]; break;
			default: out.d.v[i] = l.d.v[i] / b[i]; break;
			}
		}
		return true;
	}
	default:
		return false; /* entity, string: no result */
	}
}

template <typename T> static bool cmp_values(int cmp, T a, T b)
{
	switch (cmp) {
	case 0: return a == b;
	case 1: return a < b;
	case 2: return a <= b;
	case 3: return a > b;
	case 4: return a >= b;
	case 5: return a != b;
	default: return false;
	}
}

bool compare(const Instance *s, int cmp, const Value &left, const Value &right, ScriptSystem *sys)
{
	Value l;
	if (!resolve(s, left, sys, l) || cmp < 0 || cmp > 5)
		return false;
	int rtype = value_type(s, right, sys);
	switch (l.type) {
	case VT_INT: return cmp_values(cmp, l.d.i, as_int(s, right, sys));
	case VT_FLOAT: return cmp_values(cmp, l.d.f, as_float(s, right, sys));
	case VT_VECTOR:
		if (rtype == VT_INT || rtype == VT_FLOAT) {
			float k = rtype == VT_INT ? (float)as_int(s, right, sys) : as_float(s, right, sys);
			return cmp_values(cmp, vlen(l.d.v), k);
		}
		if (rtype == VT_VECTOR) {
			float b[3];
			as_vector(s, right, sys, b);
			bool same = l.d.v[0] == b[0] && l.d.v[1] == b[1] && l.d.v[2] == b[2];
			if (cmp == 0)
				return same;
			if (cmp == 5)
				return !same;
			return cmp_values(cmp, vlen(l.d.v), vlen(b));
		}
		return false;
	case VT_ENTITY:
		if (cmp != 0 && cmp != 5)
			return false;
		if (rtype == VT_INT)
			return cmp_values(cmp, l.d.e, as_int(s, right, sys));
		if (rtype == VT_ENTITY)
			return cmp_values(cmp, l.d.e, as_entity(s, right, sys));
		return false;
	default:
		return false;
	}
}

static void convert(const Instance *s, int type, const Value &value, ScriptSystem *sys, Data &d)
{
	switch (type) {
	case VT_INT: d.i = as_int(s, value, sys); break;
	case VT_FLOAT: d.f = as_float(s, value, sys); break;
	case VT_VECTOR: as_vector(s, value, sys, d.v); break;
	case VT_ENTITY: d.e = as_entity(s, value, sys); break;
	case VT_STRING: as_string(s, value, sys, d.s); break;
	}
}

void assign(Instance *s, const Value &target, const Value &value, ScriptSystem *sys)
{
	if (target.kind == VK_VAR) {
		Variable *var = (target.var >= 0 && target.var < MAX_SLOTS) ? s->vars[target.var] : nullptr;
		if (!var)
			return;
		Data d = var->d;
		convert(s, var->type, value, sys, d);
		var->d = d;
		return;
	}
	if (target.kind != VK_FIELD)
		return;
	FieldId f = field_of(s, target);
	Variable *var = (target.var >= 0 && target.var < MAX_SLOTS) ? s->vars[target.var] : nullptr;
	if (f == F_NONE || !var)
		return;
	if (f == F_X || f == F_Y || f == F_Z) {
		if (var->type == VT_VECTOR)
			var->d.v[f - F_X] = as_float(s, value, sys);
		return;
	}
	if (var->type != VT_ENTITY || var->d.e == ENT_NONE)
		return;
	FieldValue fv;
	Data d;
	int type = field_natural_type(f);
	convert(s, type, value, sys, d);
	fv.i = d.i;
	fv.f = d.f;
	memcpy(fv.v, d.v, sizeof fv.v);
	fv.e = d.e;
	sys->host()->write_field(var->d.e, f, fv);
}

} /* namespace om_script */
