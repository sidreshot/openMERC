/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750, SPEC-751, SPEC-752
 * Owner: package G6a (game/src/script). Internal to the VM.
 */
#ifndef OM_SCRIPT_INTERNAL_H
#define OM_SCRIPT_INTERNAL_H

#include "script_vm.h"

#include <string.h>

namespace om_script {

enum {
	SCRIPT_VERSION = 3,
	CODE_START = 4,
	MAX_SLOTS = 100,
	NAME_LEN = 64,
	STRING_LEN = 64,
	INSTRUCTION_BUDGET = 1000
};

/* script states (SPEC-750 2.2; values are saved) */
enum ScriptState { ST_READY = 0, ST_FINISHED = 1, ST_WAIT_ALL = 3, ST_WAIT_ANY = 4, ST_WAIT_TIME = 5 };

/* event kinds (SPEC-750 3.2; values are saved) */
enum EventKind {
	EV_MOVE_DONE = 0, EV_ROTATE_DONE = 1, EV_WAIT = 2, EV_EXECUTE = 3, EV_ROF_FRAME = 4,
	EV_AI_DONE = 5, EV_HELI_DONE = 6, EV_VEHICLE_DONE = 7
};

/* signal kinds (SPEC-750 9.1) */
enum SignalKind { SIG_MOVE = 0, SIG_ROTATE = 1, SIG_AI = 2, SIG_ROF = 3, SIG_HELI = 4, SIG_VEHICLE = 5 };

struct Data {
	int i = 0;
	float f = 0.0f;
	float v[3] = {0, 0, 0};
	int e = ENT_NONE;
	char s[STRING_LEN] = {0};
};

/* a declared variable (also level globals) */
struct Variable {
	char name[NAME_LEN] = {0};
	int type = VT_INT;
	int slot = -1; /* slot in the owning script's table, -1 for globals */
	Data d;
};

/* a value on the stack: a temporary, a variable reference or a field reference */
enum ValueKind { VK_TEMP = 0, VK_VAR = 1, VK_FIELD = 2 };
struct Value {
	int kind = VK_TEMP;
	int type = VT_INT;  /* temporaries: their type */
	int var = -1;       /* VK_VAR / VK_FIELD: variable slot */
	int field = -1;     /* VK_FIELD: field slot */
	Data d;
};

struct FieldDesc {
	bool present = false;
	char name[NAME_LEN] = {0};
	int declared_type = 0;
	FieldId id = F_NONE;
};

struct Event {
	int kind = EV_WAIT;
	float time = 0.0f;
	int ent = ENT_NONE;
	int caller = ENT_NONE;     /* EV_EXECUTE */
	int activator = ENT_NONE;  /* EV_EXECUTE */
	float dorg[3] = {0, 0, 0}; /* EV_ROF_FRAME */
	float dang[3] = {0, 0, 0};
	int done_id = 0;           /* EV_AI_DONE */
	bool completed = false;    /* EV_AI_DONE */
	int action_id = -1;        /* EV_HELI_DONE / EV_VEHICLE_DONE */
};

struct PendingSignal {
	int ent = ENT_NONE;
	int kind = 0;
	int var = -1; /* variable slot, -1 none */
};

struct Instance {
	std::string path;              /* "ds/<script>.os" */
	std::vector<uint8_t> code;
	int handle = -1;
	int state = ST_FINISHED;
	int clear_after_wait = 0;      /* 0x80 or 0 */
	int ip = CODE_START;
	int debug = 0;
	int runner = ENT_NONE, caller = ENT_NONE, activator = ENT_NONE;

	Variable *vars[MAX_SLOTS] = {nullptr};
	FieldDesc fields[MAX_SLOTS];
	std::vector<std::unique_ptr<Variable> > owned; /* locals and parameters */
	std::vector<int> local_slots, param_slots, global_slots;
	std::vector<std::string> parms; /* remaining parameter strings */
	std::vector<Value> stack;
	std::vector<int> waited;        /* variable slots of the current WAIT_SIGNALS */
	std::vector<PendingSignal> signals;
	std::vector<Event> events;
	std::vector<int> watches;       /* file offsets of armed WATCH instructions */

	bool in_handler = false;
	int saved_ip = 0, saved_state = 0;
	int running = 0;                /* activation depth */
};

/* field names (SPEC-750 4.4) */
FieldId field_by_name(const char *name);
int field_natural_type(FieldId f);

/* value helpers (script_value.cpp) */
int value_type(const Instance *s, const Value &v, ScriptSystem *sys);
int as_int(const Instance *s, const Value &v, ScriptSystem *sys);
float as_float(const Instance *s, const Value &v, ScriptSystem *sys);
void as_vector(const Instance *s, const Value &v, ScriptSystem *sys, float out[3]);
int as_entity(const Instance *s, const Value &v, ScriptSystem *sys);
void as_string(const Instance *s, const Value &v, ScriptSystem *sys, char out[STRING_LEN]);
/* arithmetic (op 0 + 1 - 2 * 3 /); false = no result ("Illegal push") */
bool arith(const Instance *s, int op, const Value &left, const Value &right, ScriptSystem *sys, Value &out);
bool compare(const Instance *s, int cmp, const Value &left, const Value &right, ScriptSystem *sys);
/* assignment to a reference (variable or field); temporaries ignore it */
void assign(Instance *s, const Value &target, const Value &value, ScriptSystem *sys);
Value temp_int(int i);
Value temp_float(float f);
Value temp_vector(const float v[3]);
Value temp_entity(int e);
Value temp_string(const char *text);
void default_data(int type, Data &d);

/* the interpreter (script_vm.cpp) */
void run(ScriptSystem *sys, Instance *s);
bool load_code(Host *host, Instance *s); /* reads s->path; READY at offset 4, or FINISHED with the 1.2 messages */
float quantize(float t);
void queue_event(Instance *s, const Event &ev);

inline void copy_name(char *dst, const char *src, size_t n)
{
	strncpy(dst, src, n - 1);
	dst[n - 1] = 0;
}

} /* namespace om_script */

#endif
