// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G6b - internal declarations of game/src/scriptglue (not for other packages).
// Implements: SPEC-750, SPEC-751, SPEC-752
#pragma once

#include "g_scriptglue.h"
#include "../script/script_vm.h"

// G2 script interface: om_ai::HasAI, ScriptCommand, ScriptReset, ClearCallForHelp, CinematicEndActors, g_report_ai_done
#include "../ai/ai_api.h"

// G18 symbols used here that g_local.h does not declare
void ED_CallSpawn(edict_t *ent);
char *ED_NewString(const char *string);
void Use_Multi(edict_t *ent, edict_t *other, edict_t *activator);

namespace sg {

// entity number helpers: 0 = world, -1 = none
inline edict_t *Ent(int n) { return (n >= 0 && n < globals.num_edicts) ? &g_edicts[n] : nullptr; }
inline edict_t *EntInUse(int n)
{
	edict_t *e = Ent(n);
	return e && e->inuse ? e : nullptr;
}
inline int Num(const edict_t *e) { return e ? G_EdictNum(e) : om_script::ENT_NONE; }

// om_script::Host on G18's entities (sg_host.cpp)
class GameHost : public om_script::Host {
public:
	float level_time() override;
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
	int spawn_entity_typed(const std::vector<SpawnKey> &keys) override;
	void relink(int ent) override;
	int follower(int ent) override;
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

	void cinematic_remove_player_knives() override;
	bool player_has_client() override;
	void cinematic_weapon_hook() override;
	void cinematic_end_actors() override;

	void attach_script(int runner, int script_handle) override;
};

extern int g_debug; // cvar g6b_debug (OpenMerc diagnostic)
GameHost &Host();
om_script::ScriptSystem &System();
void RaisePendingError(void);

} // namespace sg
