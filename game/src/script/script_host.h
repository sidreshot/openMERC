/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750, SPEC-751, SPEC-752; SPEC-AMEND-005 G6b.7 (typed spawn values)
 * Owner: package G6a (game/src/script). G18 implements Host on top of its entities.
 *
 * Host interface of the DesignerScript VM. The VM (script_vm.h) contains the whole run-time semantics of
 * SPEC-750 (interpreter, events, signals, watches), the ROF playback and cinematic state of SPEC-751 and the
 * script chunks of SPEC-752 section 4. Everything that touches entities, the engine or the AI goes through
 * this interface, so the VM can be tested with a mock host and later wired to the game module.
 *
 * Entities are entity numbers: 0 is the world, -1 is "none". The VM never calls an engine error itself: a
 * game error found while running a script is returned by ScriptSystem::pending_error() and the host raises
 * it (SPEC-750 section 2.4).
 */
#ifndef OM_SCRIPT_HOST_H
#define OM_SCRIPT_HOST_H

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace om_script {

enum { ENT_NONE = -1, ENT_WORLD = 0 };

/* value types (SPEC-750 4.1) */
enum ValueType { VT_INT = 0, VT_FLOAT = 1, VT_VECTOR = 2, VT_ENTITY = 3, VT_STRING = 4, VT_REFERENCE = 5 };

/* entity fields a script can name (SPEC-750 4.4). F_X/F_Y/F_Z are components of the variable's own vector
 * and never reach the host. */
enum FieldId {
	F_NONE = 0,
	F_X, F_Y, F_Z,
	F_ORIGIN, F_ORIGIN_X, F_ORIGIN_Y, F_ORIGIN_Z,
	F_MOVETYPE, F_START_ORIGIN, F_START_ANGLES, F_DISTANCE, F_STATE, F_OWNER, F_WAIT,
	F_VELOCITY, F_ANGLE_VELOCITY, F_TEAM_CHAIN, F_YAW_SPEED, F_MODELINDEX, F_COUNT, F_SOLID,
	F_ANGLES, F_HEALTH, F_LOOPSOUND,
	F_NUM_FIELDS
};

/* one entity field value; only the member of the field's natural type is meaningful */
struct FieldValue {
	int i = 0;
	float f = 0.0f;
	float v[3] = {0.0f, 0.0f, 0.0f};
	int e = ENT_NONE;
};

/* an AI command (opcode 0x21) as decoded by the VM (SPEC-750 12.1, 12.2, 12.5) */
struct AiCommand {
	int flags = 0;          /* the int32 flags word of the opcode */
	int command = 0;        /* command id */
	int kind = 0;           /* action kind from the 12.7 table (0 when not in the table or expression form) */
	int speed = 0;          /* default speed from the table */
	const char *animation = nullptr; /* animation from the table, nullptr = none */
	float duration = 50.0f; /* 0x40 or default */
	int count = 1;          /* 0x01 repeat count; <= 0 queues nothing */
	float facing[3] = {0, 0, 0};      /* 0x02 */
	float destination[3] = {0, 0, 0}; /* 0x04, relative unless flags & 0x20 */
	int target = ENT_NONE;  /* 0x80 */
	int anim_param = 0;     /* 0x200 */
	int expression = 0;     /* 0x400, non-zero = expression form */
	/* AI-done token (0 = none). The AI calls ScriptSystem::ai_action_completed(done_id) when the last queued copy
	 * completes; it never calls it when the actions are removed by RESET_AI or death. */
	int done_id = 0;
};

/* chunk I/O of the savegame container (SPEC-303); ids are four letters as written in SPEC-752 */
class ChunkWriter {
public:
	virtual ~ChunkWriter() {}
	virtual void write(const char *id, const void *data, int size) = 0;
};

class ChunkReader {
public:
	virtual ~ChunkReader() {}
	/* reads the next chunk; false when its id or size differs or the stream ended */
	virtual bool read(const char *id, void *data, int size) = 0;
};

class Host {
public:
	virtual ~Host() {}

	/* engine services */
	virtual float level_time() = 0;
	virtual void dprint(const char *text) = 0;   /* console, text as given */
	virtual bool load_file(const char *path, std::vector<uint8_t> &out) = 0; /* game file system */
	virtual float cvar_value(const char *name) = 0;
	virtual void cvar_set(const char *name, const char *value) = 0;
	virtual void cvar_set_value(const char *name, float value) = 0;
	virtual void add_command_text(const char *text) = 0; /* server command buffer */
	virtual int irand(int min, int max) = 0;             /* engine integer random (SPEC-001) */

	/* entities */
	virtual bool entity_in_use(int ent) = 0;
	virtual int find_by_targetname(const char *name) = 0;   /* lowest entity number, or ENT_NONE */
	virtual int find_by_scripttarget(const char *name) = 0;
	virtual int find_by_classname(const char *name) = 0;
	virtual const char *entity_name(int ent) = 0;           /* for console diagnostics */
	virtual bool read_field(int ent, FieldId f, FieldValue &out) = 0; /* false: entity not usable */
	virtual void write_field(int ent, FieldId f, const FieldValue &v) = 0; /* not relinked */
	virtual bool entity_has_use(int ent) = 0;
	virtual void use_entity(int ent, int other, int activator) = 0;
	virtual void free_entity(int ent) = 0;
	/* spawn with (key, value) pairs in the order given; returns the entity (SPEC-750 8.1) */
	virtual int spawn_entity(const std::vector<std::pair<std::string, std::string> > &keys) = 0;
	/* SPEC-AMEND-005 G6b.7: the same with each value's script type (ValueType) and value, because the game reads
	 * every value with the key's own type (a string value read as a vector gives (0, 0, 0), read as a number 0).
	 * text: the string value ("" for other types); v / i / f: the vector, integer and float forms of a vector, int,
	 * float or entity value. The default drops the types and calls spawn_entity with the text. */
	struct SpawnKey {
		std::string key, text;
		int type;
		float v[3];
		int i;
		float f;
	};
	virtual int spawn_entity_typed(const std::vector<SpawnKey> &keys)
	{
		std::vector<std::pair<std::string, std::string> > p;
		for (const auto &k : keys)
			p.push_back(std::make_pair(k.key, k.text));
		return spawn_entity(p);
	}
	virtual void relink(int ent) = 0;
	virtual int follower(int ent) = 0; /* entity receiving the same relative change in an instant MOVE/ROTATE */
	/* timed MOVE: remember the destination in the entity; finish_move: velocity 0, origin := the destination of
	 * the entity's most recent timed MOVE (an earlier move's done event snaps to the later one, SPEC-AMEND-003 2e) */
	virtual void set_move_destination(int ent, const float dest[3]) = 0;
	virtual void finish_move(int ent) = 0;
	virtual void finish_rotate(int ent) = 0; /* angular velocity 0, angles not snapped */
	virtual void make_trigger(int ent) = 0;    /* opcode 0x1F form 0 */
	virtual void disable_trigger(int ent) = 0; /* opcode 0x20 form 0 */
	virtual void set_view_yaw(int player, float yaw) = 0; /* opcode 0x25 */
	virtual void set_music_state(int value) = 0;          /* opcode 0x2A: player music byte */

	/* sounds and text */
	virtual int sound_index(const char *name) = 0;
	virtual void unload_sound(const char *name) = 0;
	virtual void play_sound(int ent, int channel, int sound, float volume, float attenuation, float timeofs) = 0;
	virtual void print_text(int target, int level, const char *text) = 0; /* target ENT_NONE = all clients */
	virtual void sp_print(int target, int string_id) = 0;
	virtual void register_string_package(const char *name) = 0;

	/* AI */
	virtual bool has_ai(int ent) = 0;
	virtual void ai_command(int ent, const AiCommand &cmd) = 0; /* behaviour, queue, activation (12.2) */
	virtual void ai_reset(int ent) = 0;
	virtual void ai_clear_call_for_help(int ent) = 0;
	/* HELI / VEHICLE (12.8): returns the action identifier, -1 when the command creates no action, -2 when the
	 * entity has no helicopter (vehicle) AI */
	virtual int heli_command(int ent, int command, int target, const float vec[3], float scalar) = 0;
	virtual int vehicle_command(int ent, int command, int target, const float vec[3], float scalar) = 0;
	/* vehicle AI has no current action and its last completed action is action_id; true at once when the entity
	 * no longer has an AI (SPEC-AMEND-003 6) */
	virtual bool vehicle_action_done(int ent, int action_id) = 0;

	/* cinematics (SPEC-751 1.2, 1.4) */
	virtual void cinematic_remove_player_knives() = 0;
	virtual bool player_has_client() = 0;
	virtual void cinematic_weapon_hook() = 0; /* player inventory method 58, if the player has an inventory */
	virtual void cinematic_end_actors() = 0;  /* count 255 -> 0 for cinematic actors, thaw frozen actors */

	/* savegames: the runner entity's script link (SPEC-752 4.4) */
	virtual void attach_script(int runner, int script_handle) = 0;
};

} /* namespace om_script */

#endif
