// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G6b - om_script::Host implemented on G18's entities: engine services, entity
// lookup and fields, movers, triggers, sounds and captions, the AI / helicopter / vehicle commands (forwarded to
// G2 and G4) and the cinematic side effects on the player.
// Implements: SPEC-750 (4.4 fields, 8 built-ins, 11 MOVE/ROTATE, 12 commands), SPEC-751 (1.2, 1.4, 4),
//             SPEC-AMEND-003 (2e most recent MOVE destination, 6 vehicle done without AI),
//             SPEC-AMEND-005 G6b.4 (move-done snap without relink), G6b.7 (spawn values read with the key's type)

#include "sg_local.h"
#include "../roster/roster_api.h"

#include <stdio.h>
#include <string.h>

using namespace om_script;

namespace sg {

// ------------------------------------------------------------------------------------------------------------
// engine services

float GameHost::level_time() { return level.time; }

void GameHost::dprint(const char *text) { gix.dprintf("%s", text); }

bool GameHost::load_file(const char *path, std::vector<uint8_t> &out)
{
	void *buf = NULL;
	int len = gix.FS_LoadFile(path, &buf, 0);
	if (len < 0 || !buf)
		return false;
	out.assign((const uint8_t *)buf, (const uint8_t *)buf + len);
	gix.FS_FreeFile(buf);
	return true;
}

float GameHost::cvar_value(const char *name) { return gix.cvar_value(name); }
void GameHost::cvar_set(const char *name, const char *value) { gix.cvar_set(name, value); }
void GameHost::cvar_set_value(const char *name, float value) { gix.cvar_setvalue(name, value); }
void GameHost::add_command_text(const char *text) { gix.AddCommandString(text); }
int GameHost::irand(int min, int max) { return gix.irand(min, max); }

// ------------------------------------------------------------------------------------------------------------
// entities

bool GameHost::entity_in_use(int ent) { return EntInUse(ent) != nullptr; }

static int find(int ofs, const char *name)
{
	if (!name)
		return ENT_NONE;
	return Num(G_Find(NULL, ofs, name));
}

int GameHost::find_by_targetname(const char *name) { return find(FOFS(targetname), name); }
int GameHost::find_by_scripttarget(const char *name) { return find(FOFS(scripttarget), name); }
int GameHost::find_by_classname(const char *name) { return find(FOFS(classname), name); }

const char *GameHost::entity_name(int ent)
{
	edict_t *e = Ent(ent);
	if (!e)
		return "";
	if (e->targetname)
		return e->targetname;
	return e->classname ? e->classname : "";
}

// SPEC-750 4.4: natural types int (movetype, state, modelindex, count, solid, health, loopsound), float (origin_x/y/z,
// distance, wait, yaw_speed), vector (origin, start_origin, start_angles, velocity, angle_velocity, angles),
// entity (owner, team_chain). The VM converts; the host only stores the member of the natural type.
bool GameHost::read_field(int ent, FieldId f, FieldValue &out)
{
	edict_t *e = EntInUse(ent);
	if (!e)
		return false;
	switch (f) {
	case F_ORIGIN: VectorCopy(e->s.origin, out.v); break;
	case F_ORIGIN_X: out.f = e->s.origin[0]; break;
	case F_ORIGIN_Y: out.f = e->s.origin[1]; break;
	case F_ORIGIN_Z: out.f = e->s.origin[2]; break;
	case F_MOVETYPE: out.i = e->movetype; break;
	case F_START_ORIGIN: VectorCopy(e->moveinfo.start_origin, out.v); break;
	case F_START_ANGLES: VectorCopy(e->moveinfo.start_angles, out.v); break;
	case F_DISTANCE: out.f = e->moveinfo.distance; break;
	case F_STATE: out.i = e->moveinfo.state; break;
	case F_OWNER: out.e = Num(e->owner); break;
	case F_WAIT: out.f = e->wait; break;
	case F_VELOCITY: VectorCopy(e->velocity, out.v); break;
	case F_ANGLE_VELOCITY: VectorCopy(e->avelocity, out.v); break;
	case F_TEAM_CHAIN: out.e = Num(e->teamchain); break;
	case F_YAW_SPEED: out.f = e->yaw_speed; break;
	case F_MODELINDEX: out.i = e->s.modelindex; break;
	case F_COUNT: out.i = e->count; break;
	case F_SOLID: out.i = e->solid; break;
	case F_ANGLES: VectorCopy(e->s.angles, out.v); break;
	case F_HEALTH: out.i = e->health; break;
	case F_LOOPSOUND: out.i = e->s.sound; break;
	default: break;
	}
	return true;
}

// not relinked (SPEC-750 4.4)
void GameHost::write_field(int ent, FieldId f, const FieldValue &v)
{
	edict_t *e = EntInUse(ent);
	if (!e)
		return;
	switch (f) {
	case F_ORIGIN: VectorCopy(v.v, e->s.origin); break;
	case F_ORIGIN_X: e->s.origin[0] = v.f; break;
	case F_ORIGIN_Y: e->s.origin[1] = v.f; break;
	case F_ORIGIN_Z: e->s.origin[2] = v.f; break;
	case F_MOVETYPE: e->movetype = v.i; break;
	case F_START_ORIGIN: VectorCopy(v.v, e->moveinfo.start_origin); break;
	case F_START_ANGLES: VectorCopy(v.v, e->moveinfo.start_angles); break;
	case F_DISTANCE: e->moveinfo.distance = v.f; break;
	case F_STATE: e->moveinfo.state = v.i; break;
	case F_OWNER: e->owner = Ent(v.e); break;
	case F_WAIT: e->wait = v.f; break;
	case F_VELOCITY: VectorCopy(v.v, e->velocity); break;
	case F_ANGLE_VELOCITY: VectorCopy(v.v, e->avelocity); break;
	case F_TEAM_CHAIN: e->teamchain = Ent(v.e); break;
	case F_YAW_SPEED: e->yaw_speed = v.f; break;
	case F_MODELINDEX: e->s.modelindex = v.i; break;
	case F_COUNT: e->count = v.i; break;
	case F_SOLID: e->solid = v.i; break;
	case F_ANGLES: VectorCopy(v.v, e->s.angles); break;
	case F_HEALTH: e->health = v.i; break;
	case F_LOOPSOUND: e->s.sound = v.i; break;
	default: break;
	}
}

bool GameHost::entity_has_use(int ent)
{
	edict_t *e = EntInUse(ent);
	return e && e->use;
}

void GameHost::use_entity(int ent, int other, int activator)
{
	edict_t *e = EntInUse(ent);
	if (e && e->use)
		e->use(e, Ent(other), Ent(activator));
}

void GameHost::free_entity(int ent)
{
	edict_t *e = EntInUse(ent);
	if (e)
		G_FreeEdict(e);
}

// SPEC-750 8.1: keys looked up case-insensitively in the spawn key table (SPEC-760 4), stored with their type
// (`angle` = yaw only), spawn-time keys into the spawn-temporary record; unknown keys ignored; then the spawn function.
// tv: the typed script value (NULL = parse the text). SPEC-AMEND-005 G6b.7: the value is read with the key's type,
// so a string value gives (0, 0, 0) for a vector key and 0 for a number key; an int or float value for a vector key
// is UNVERIFIED, taken as (0, 0, 0) as well; a number value for a string key gives "".
static void set_key(edict_t *e, const char *key, const char *value, const Host::SpawnKey *tv = NULL)
{
	for (field_t *f = fields; f->name; f++) {
		if (Q_stricmp((char *)f->name, (char *)key))
			continue;
		uint8_t *b = (f->flags & FFL_SPAWNTEMP) ? (uint8_t *)&st : (uint8_t *)e;
		switch (f->type) {
		case F_LSTRING: *(char **)(b + f->ofs) = ED_NewString(value); break;
		case F_VECTOR: {
			float *v = (float *)(b + f->ofs);
			v[0] = v[1] = v[2] = 0;
			if (!tv)
				sscanf(value, "%f %f %f", &v[0], &v[1], &v[2]);
			else if (tv->type == VT_VECTOR)
				VectorCopy(tv->v, v);
			break;
		}
		case F_INT: *(int *)(b + f->ofs) = tv ? tv->i : atoi(value); break;
		case F_SHORT: *(int *)(b + f->ofs) = (short)(tv ? tv->i : atoi(value)); break;
		case F_FLOAT: *(float *)(b + f->ofs) = tv ? tv->f : (float)atof(value); break;
		case F_ANGLEHACK: {
			float *v = (float *)(b + f->ofs);
			v[0] = 0;
			v[1] = tv ? tv->f : (float)atof(value);
			v[2] = 0;
			break;
		}
		default: break;
		}
		return;
	}
}

int GameHost::spawn_entity_typed(const std::vector<SpawnKey> &keys)
{
	edict_t *e = G_Spawn();
	spawn_temp_t saved = st;
	memset(&st, 0, sizeof(st));
	for (const auto &kv : keys)
		set_key(e, kv.key.c_str(), kv.text.c_str(), &kv);
	if (e->classname)
		ED_CallSpawn(e);
	st = saved;
	return Num(e);
}

int GameHost::spawn_entity(const std::vector<std::pair<std::string, std::string> > &keys)
{
	edict_t *e = G_Spawn();
	spawn_temp_t saved = st;
	memset(&st, 0, sizeof(st));
	for (const auto &kv : keys)
		set_key(e, kv.first.c_str(), kv.second.c_str());
	if (e->classname)
		ED_CallSpawn(e);
	st = saved;
	return Num(e);
}

void GameHost::relink(int ent)
{
	edict_t *e = EntInUse(ent);
	if (e)
		gi.linkentity(e);
}

// SPEC-750 11 / open question 3: which entity follows an instant MOVE/ROTATE is not specified; none
int GameHost::follower(int ent)
{
	(void)ent;
	return ENT_NONE;
}

// SPEC-AMEND-003 2e: every timed MOVE replaces the destination; any move-done event of the entity snaps to it
void GameHost::set_move_destination(int ent, const float dest[3])
{
	edict_t *e = EntInUse(ent);
	if (e)
		VectorCopy(dest, e->sg_movedest);
}

void GameHost::finish_move(int ent)
{
	edict_t *e = EntInUse(ent);
	if (!e)
		return;
	VectorClear(e->velocity);
	VectorCopy(e->sg_movedest, e->s.origin);
	// SPEC-AMEND-005 G6b.4: the move-done event does not relink; the world link stays where the last physics move
	// left it until something relinks the entity (a later MOVE/ROTATE or ROF frame, a push, setmodel)
}

void GameHost::finish_rotate(int ent)
{
	edict_t *e = EntInUse(ent);
	if (e)
		VectorClear(e->avelocity);
}

// opcode 0x1F form 0: trigger solid, used like a multiple trigger, relinked
void GameHost::make_trigger(int ent)
{
	edict_t *e = EntInUse(ent);
	if (!e)
		return;
	e->solid = SOLID_TRIGGER;
	e->use = Use_Multi;
	gi.linkentity(e);
}

// opcode 0x20 form 0: not solid, no use, not relinked
void GameHost::disable_trigger(int ent)
{
	edict_t *e = EntInUse(ent);
	if (!e)
		return;
	e->solid = SOLID_NOT;
	e->use = NULL;
}

// opcode 0x25: view pitch 0, yaw, roll 0 through the movement delta angles
void GameHost::set_view_yaw(int player, float yaw)
{
	edict_t *p = EntInUse(player);
	if (!p || !p->client)
		return;
	const float want[3] = {0, yaw, 0};
	for (int i = 0; i < 3; i++)
		p->client->ps.pmove.delta_angles[i] = (short)ANGLE2SHORT(want[i] - p->client->resp.cmd_angles[i]);
	gi.linkentity(p);
}

// opcode 0x2A: the music byte of the AI sight target (single player: the player)
void GameHost::set_music_state(int value)
{
	edict_t *p = level.sight_client;
	if (!p || !p->inuse || !p->client)
		p = G_FirstPlayer();
	if (p && p->client)
		p->client->ps.musicvalue = (uint8_t)value;
}

// ------------------------------------------------------------------------------------------------------------
// sounds and text

int GameHost::sound_index(const char *name) { return gi.soundindex(name); }
void GameHost::unload_sound(const char *name) { gix.soundremove(name); }

// SPEC-750 12.3: import 22, not local. Stock scripts always name an entity; without one nothing is played.
void GameHost::play_sound(int ent, int channel, int sound, float volume, float attenuation, float timeofs)
{
	edict_t *e = EntInUse(ent);
	if (!e)
		return;
	gix.sound(e, channel, sound, volume, attenuation, timeofs, 0);
}

// SPEC-751 4: text to all clients or one. The original uses the text as a format string; it is printed literally
// here (no stock script prints text; a stray %s would read garbage in the original) - DEVIATIONS G6b-1.
void GameHost::print_text(int target, int lvl, const char *text)
{
	edict_t *t = target == ENT_NONE ? nullptr : Ent(target);
	if (t)
		gix.cprintf(t, lvl, "%s", text);
	else
		gix.bprintf(lvl, "%s", text);
}

void GameHost::sp_print(int target, int string_id)
{
	edict_t *t = target == ENT_NONE ? nullptr : Ent(target);
	gix.SP_Print(t, string_id);
}

void GameHost::register_string_package(const char *name) { gix.SP_Register(name); }

// ------------------------------------------------------------------------------------------------------------
// AI (G2), helicopters and vehicles (G4)

bool GameHost::has_ai(int ent)
{
	edict_t *e = EntInUse(ent);
	return e && om_ai::HasAI(e);
}

void GameHost::ai_command(int ent, const AiCommand &cmd)
{
	edict_t *e = EntInUse(ent);
	if (g_debug)
		gi.dprintf("G6b: ai %s cmd %d kind %d count %d flags 0x%x done %d at %.1f\n", entity_name(ent), cmd.command,
		           cmd.kind, cmd.count, cmd.flags, cmd.done_id, level.time);
	if (e)
		om_ai::ScriptCommand(e, cmd);
}

void GameHost::ai_reset(int ent)
{
	edict_t *e = EntInUse(ent);
	if (e)
		om_ai::ScriptReset(e);
}

void GameHost::ai_clear_call_for_help(int ent)
{
	edict_t *e = EntInUse(ent);
	if (e)
		om_ai::ClearCallForHelp(e);
}

int GameHost::heli_command(int ent, int command, int target, const float vec[3], float scalar)
{
	edict_t *e = EntInUse(ent);
	if (!e || !Roster_HasHeliAI(e))
		return -2;
	return Roster_HeliCommand(e, command, Ent(target), vec, scalar);
}

int GameHost::vehicle_command(int ent, int command, int target, const float vec[3], float scalar)
{
	edict_t *e = EntInUse(ent);
	if (!e || !Roster_HasVehicleAI(e))
		return -2;
	return Roster_VehicleCommand(e, command, Ent(target), vec, scalar);
}

// used for HELI and VEHICLE done events; true at once without the AI (SPEC-AMEND-003 6)
bool GameHost::vehicle_action_done(int ent, int action_id)
{
	edict_t *e = EntInUse(ent);
	if (!e || !(Roster_HasHeliAI(e) || Roster_HasVehicleAI(e)))
		return true;
	return Roster_VehicleActionDone(e, action_id);
}

// ------------------------------------------------------------------------------------------------------------
// cinematics (SPEC-751 1.2, 1.4)

// thrown knives of the first player: projectiles of attack 2 (SPEC-782 attack table) owned by entity 1
void GameHost::cinematic_remove_player_knives()
{
	edict_t *player = &g_edicts[1];
	for (int i = game.maxclients + 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (e->inuse && e->owner == player && e->attack == 2)
			G_FreeEdict(e);
	}
}

bool GameHost::player_has_client() { return game.maxclients >= 1 && g_edicts[1].client != NULL; }

// player inventory: owner context = the player, then method 58 (UNVERIFIED effect: stops the current weapon action)
void GameHost::cinematic_weapon_hook()
{
	edict_t *p = &g_edicts[1];
	gclient_t *c = p->client;
	if (!c || !c->inv)
		return;
	static OwnerContext oc;
	memset(&oc, 0, sizeof(oc));
	oc.inventory = c->inv;
	oc.owner = p;
	oc.cinematicfreeze = 1;
	oc.remoteview = c->ps.remote_type != 0;
	oc.kick = c->kick_angles;
	oc.frametime10 = 1.0f;
	c->inv->SetOwnerContext(&oc);
	c->inv->WeaponHook1();
}

// step 3 of the end: cinematic actors (count 255) back to 0, frozen actors thawed - the AI framework does both
void GameHost::cinematic_end_actors() { om_ai::CinematicEndActors(); }

// ------------------------------------------------------------------------------------------------------------
// savegames: the runner's script link (SPEC-752 4.4)

void GameHost::attach_script(int runner, int script_handle)
{
	edict_t *e = Ent(runner);
	if (e)
		e->sg_script = script_handle + 1;
}

} // namespace sg
