/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750, SPEC-751, SPEC-752
 * Owner: package G6a (game/src/script).
 *
 * The level-wide DesignerScript system: script instances in spawn order, level globals, the ROF cache and
 * the cinematic state. The game module owns one ScriptSystem and calls:
 *   create()        when a script_runner spawns (SPEC-750 1.1)
 *   use()           when a runner is used (1.3)
 *   run_frame()     once per server frame after all entities ran (3.1)
 *   ai_action_completed()  from the AI when the last copy of a signalled AI command completes (12.6)
 *   clear_level()   on level spawn, before loading a level section and at shutdown (1.2)
 *   save()/load()   for the level section chunks 4 and 5 (SPEC-752 1.2)
 * After each call the host checks pending_error() and raises it as a game error.
 */
#ifndef OM_SCRIPT_VM_H
#define OM_SCRIPT_VM_H

#include "script_host.h"

#include <memory>
#include <string>
#include <vector>

namespace om_script {

struct Instance;
struct Variable;

class ScriptSystem {
public:
	explicit ScriptSystem(Host *host);
	~ScriptSystem();

	void clear_level();

	/* create the script of a runner; parms = the runner's parm1, parm2 ... up to the first missing one.
	 * Returns the script handle (position in the level list). */
	int create(int runner, const char *script, const std::vector<std::string> &parms);
	void use(int handle, int activator);
	void run_frame();
	void ai_action_completed(int done_id);

	/* cinematic state byte (SPEC-751 1.1; game exports 0x54/0x58) */
	int cinematic_state() const { return cinematic_; }
	void set_cinematic_state(int v) { cinematic_ = v; }

	/* game error raised by a script (SPEC-750 2.4); empty when none */
	const std::string &pending_error() const { return error_; }
	void clear_error() { error_.clear(); }

	/* level section chunks 4 (globals) and 5 (scripts) of SPEC-752 */
	void save(ChunkWriter &w);
	bool load(ChunkReader &r);
	/* AI action records point at AI-done events as script number * 65536 + position in its event queue */
	int encode_ai_done(int done_id) const;
	int decode_ai_done(int code) const;

	/* inspection (tests, debugging) */
	int script_count() const { return (int)scripts_.size(); }
	int script_state(int handle) const;
	int script_ip(int handle) const;
	const char *script_path(int handle) const;
	bool variable_by_name(int handle, const char *name, FieldValue &out, int &type) const;
	int queued_events(int handle) const;

	/* internal, used by the interpreter */
	Host *host() const { return host_; }
	void fail(const std::string &msg);
	bool failed() const { return !error_.empty(); }
	Variable *find_global(const char *name);
	Variable *add_global(std::unique_ptr<Variable> v);
	bool rof_load(const std::string &name, const std::vector<uint8_t> **data);
	void rof_release(const std::string &name);
	void broadcast_signal(int ent, int kind);
	int next_done_id() { return ++done_counter_; }
	bool skipping_cinematic();
	void cinematic_start();
	void cinematic_end();
	void run_instance(Instance *s);

private:
	void process(Instance *s);
	bool check_watches(Instance *s);

	Host *host_;
	std::vector<std::unique_ptr<Instance> > scripts_;
	std::vector<std::unique_ptr<Variable> > globals_;
	std::vector<std::pair<std::string, std::vector<uint8_t> > > rof_cache_;
	int cinematic_ = 0;
	bool stopsound_latch_ = true;
	int done_counter_ = 0;
	std::string error_;
};

/* structural decoder of a .os file (SPEC-140), used by tools and tests: calls back once per instruction with
 * its file offset, opcode and size (WATCH tails are reported as opcode 0x100). Returns false with a message on
 * the first inconsistency. */
struct DecodedInstr {
	int offset;
	int op;
	int size;
	bool declaration;
};
bool decode_script(const uint8_t *data, size_t size, std::vector<DecodedInstr> &out, std::string &err);

} /* namespace om_script */

#endif
