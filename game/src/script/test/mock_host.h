/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750, SPEC-751 (test host)
 * Owner: package G6a. Mock game for the script VM: entities with the script-visible fields, a trace of every
 * host call, files from extracted data directories or from memory.
 */
#ifndef OM_SCRIPT_TEST_MOCK_HOST_H
#define OM_SCRIPT_TEST_MOCK_HOST_H

#include "script_vm.h"

#include <map>
#include <string>
#include <vector>

struct MockEnt {
	bool inuse = true;
	std::string classname, targetname, scripttarget;
	om_script::FieldValue f[om_script::F_NUM_FIELDS];
	bool has_ai = false, has_use = true;
	float move_dest[3] = {0, 0, 0};
	int uses = 0;
	int heli_last_done = -1;
};

class MockHost : public om_script::Host {
public:
	MockHost();
	om_script::ScriptSystem *sys = nullptr;
	int framenum = 0;
	std::vector<MockEnt> ents;
	std::vector<std::string> roots;                     /* directories searched for files, first wins */
	std::map<std::string, std::vector<uint8_t> > memfiles;
	std::map<std::string, std::string> cvars;
	std::vector<std::string> trace;
	bool trace_on = true;
	bool auto_entities = false; /* create an entity for every unknown targetname/scripttarget lookup */
	int ai_complete_delay = 1;   /* frames until a signalled AI command completes */
	std::vector<std::pair<int, int> > ai_pending; /* (frame, done_id) */
	int next_action = 1;
	int irand_calls = 0;
	unsigned rnd = 12345;

	int add(const char *classname, const char *targetname = "", bool ai = false);
	void frame(); /* advances level time by one server frame and runs the scripts */

	/* Host */
	float level_time() override { return (float)framenum * 0.1f; }
	void dprint(const char *text) override;
	bool load_file(const char *path, std::vector<uint8_t> &out) override;
	float cvar_value(const char *name) override;
	void cvar_set(const char *name, const char *value) override;
	void cvar_set_value(const char *name, float value) override;
	void add_command_text(const char *text) override;
	int irand(int min, int max) override;
	bool entity_in_use(int ent) override;
	int find_by_targetname(const char *name) override;
	int find_by_scripttarget(const char *name) override;
	int find_by_classname(const char *name) override;
	const char *entity_name(int ent) override;
	bool read_field(int ent, om_script::FieldId f, om_script::FieldValue &out) override;
	void write_field(int ent, om_script::FieldId f, const om_script::FieldValue &v) override;
	bool entity_has_use(int ent) override;
	void use_entity(int ent, int other, int activator) override;
	void free_entity(int ent) override;
	int spawn_entity(const std::vector<std::pair<std::string, std::string> > &keys) override;
	void relink(int ent) override;
	int follower(int ent) override { (void)ent; return om_script::ENT_NONE; }
	void set_move_destination(int ent, const float dest[3]) override;
	void finish_move(int ent) override;
	void finish_rotate(int ent) override;
	void make_trigger(int ent) override;
	void disable_trigger(int ent) override;
	void set_view_yaw(int player, float yaw) override;
	void set_music_state(int value) override;
	int sound_index(const char *name) override;
	void unload_sound(const char *name) override;
	void play_sound(int ent, int channel, int sound, float volume, float attenuation, float timeofs) override;
	void print_text(int target, int level, const char *text) override;
	void sp_print(int target, int string_id) override;
	void register_string_package(const char *name) override;
	bool has_ai(int ent) override;
	void ai_command(int ent, const om_script::AiCommand &cmd) override;
	void ai_reset(int ent) override;
	void ai_clear_call_for_help(int ent) override;
	int heli_command(int ent, int command, int target, const float vec[3], float scalar) override;
	int vehicle_command(int ent, int command, int target, const float vec[3], float scalar) override;
	bool vehicle_action_done(int ent, int action_id) override;
	void cinematic_remove_player_knives() override { log("knives"); }
	bool player_has_client() override { return ents.size() > 1; }
	void cinematic_weapon_hook() override { log("weaponhook"); }
	void cinematic_end_actors() override { log("cinematic_end_actors"); }
	void attach_script(int runner, int handle) override;

	void log(const std::string &s);
};

#endif
