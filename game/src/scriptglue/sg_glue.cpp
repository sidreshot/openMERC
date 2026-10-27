// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G6b - the level's script system: `script_runner` entities, the frame and level
// hooks, the cinematic state byte (exports 0x54/0x58 through game.cinematic), the per-client cinematic freeze byte and
// remote-camera end-of-frame refresh, and the script chunks of the level savegame section.
// Implements: SPEC-750 1.1-1.3, 3.1; SPEC-751 1.1, 2.6; SPEC-752 1.2 steps 4-5, 2.1 (script event), 4.4 (script link);
//             SPEC-AMEND-005 G6b.1 (worldspawn runner: single player, end of ClientBegin), G6b.2 (effects bit 0x4000)

#include "sg_local.h"

#include <string>
#include <vector>

using namespace om_script;

namespace sg {

GameHost &Host()
{
	static GameHost host;
	return host;
}

ScriptSystem &System()
{
	static ScriptSystem sys(&Host());
	return sys;
}

// SPEC-750 2.4: a game error found by a script aborts the level (import 72)
void RaisePendingError(void)
{
	ScriptSystem &s = System();
	if (s.pending_error().empty())
		return;
	std::string msg = s.pending_error();
	s.clear_error();
	gix.error("%s", msg.c_str());
}

static int g_world_runner = -1; // entity number of the worldspawn `script` runner, -1 none

// OpenMerc diagnostic (not in the original): cvar g6b_debug 1 prints runner uses, AI commands and AI-done reports
int g_debug;
static cvar_t *g6b_debug;

static void report_ai_done(int done_id)
{
	if (g_debug)
		gi.dprintf("G6b: AI done %d at %.1f\n", done_id, level.time);
	System().ai_action_completed(done_id);
}

static void sync_from_engine(void)
{
	ScriptSystem &s = System();
	if ((game.cinematic & 0xFF) != s.cinematic_state())
		s.set_cinematic_state(game.cinematic & 0xFF);
}

static void sync_to_game(void) { game.cinematic = System().cinematic_state() & 0xFF; }

// ------------------------------------------------------------------------------------------------------------
// script_runner (SPEC-750 1.1-1.3)

static void runner_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	if (g_debug)
		gi.dprintf("G6b: use %s (%s) by %d at %.1f\n", self->targetname ? self->targetname : "-",
		           System().script_path(self->sg_script - 1), Num(activator), level.time);
	if (self->sg_script > 0)
		System().use(self->sg_script - 1, Num(activator));
}

static void runner_setup(edict_t *ent)
{
	// invisible to clients, not solid, not moving, used to run its script
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	ent->svflags |= SVF_NOCLIENT;
	ent->s.modelindex = 0;
	std::vector<std::string> parms;
	for (int i = 0; i < 10 && st.parm[i]; i++) // parm1 ... up to the first missing one
		parms.push_back(st.parm[i]);
	int h = System().create(Num(ent), st.script ? st.script : "", parms);
	ent->sg_script = h + 1;
	ent->use = runner_use;
}

} // namespace sg

using namespace sg;

void SP_script_runner(edict_t *ent) { runner_setup(ent); }

// SPEC-760 B1.4: worldspawn key `script` creates a runner; the level's precache script, used when a client begins
void SG_WorldScriptRunner(edict_t *ent)
{
	if (!ent->classname)
		ent->classname = (char *)"script_runner";
	runner_setup(ent);
	g_world_runner = Num(ent);
}

// ------------------------------------------------------------------------------------------------------------
// level and frame hooks

void ScriptGlue_LevelStart(void)
{
	// SPEC-750 1.2: script list, globals and ROF cache destroyed at level spawn; SPEC-751 1.1: state 0
	ScriptSystem &s = System();
	s.clear_level();
	s.clear_error();
	s.set_cinematic_state(0);
	game.cinematic = 0;
	g_world_runner = -1;
	om_ai::g_report_ai_done = report_ai_done;
	g6b_debug = gi.cvar("g6b_debug", "0", 0);
}

void ScriptGlue_Shutdown(void)
{
	System().clear_level();
	g_world_runner = -1;
}

// SPEC-750 3.1: once per server frame after entities and the game-rules hook, scripts in list order
void ScriptGlue_RunFrame(void)
{
	om_ai::g_report_ai_done = report_ai_done;
	g_debug = g6b_debug ? (int)g6b_debug->value : 0;
	sync_from_engine();
	System().run_frame();
	sync_to_game();
	// SPEC-751 1.1: the state is copied into every client's cinematic freeze byte at the end of the frame
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (p->inuse && p->client)
			p->client->ps.cinematicfreeze = (uint8_t)game.cinematic;
	}
	RaisePendingError();
}

// SPEC-AMEND-005 G6b.1: used once at the very end of a single-player ClientBegin (self = caller = the runner,
// activator = the client); every deathmatch mode takes the deathmatch path, which returns before that point
void ScriptGlue_ClientBegin(edict_t *ent)
{
	if (deathmatch && deathmatch->value)
		return;
	edict_t *r = EntInUse(g_world_runner);
	if (r && r->sg_script > 0)
		System().use(r->sg_script - 1, Num(ent));
}

// SPEC-751 2.6 (with SPEC-780 11 step 12: the bit is the entity effects bit 0x4000) and the freeze byte of 1.1
void ScriptGlue_ClientEndFrame(edict_t *ent)
{
	gclient_t *c = ent ? ent->client : NULL;
	if (!c)
		return;
	if (c->camera_nest < 1) {
		c->ps.remote_id = -1;
	} else {
		ent->s.effects |= 0x4000;
		if (c->camera && c->camera->inuse) {
			c->ps.remote_id = Num(c->camera);
			c->ps.remote_type = c->remote_type;
		}
	}
	c->ps.cinematicfreeze = (uint8_t)(System().cinematic_state() & 0xFF);
}

// SPEC-751 2.6: damage throws the client back to its own view (the camera's use toggles it off)
void ScriptGlue_ClientDamaged(edict_t *ent)
{
	if (!ent || !ent->client || ent->client->ps.remote_id < 0)
		return;
	edict_t *cam = EntInUse(ent->client->ps.remote_id);
	if (cam && cam->count && cam->use && cam->classname && !strcmp(cam->classname, "func_remote_camera"))
		cam->use(cam, NULL, NULL);
}

int ScriptGlue_CinematicState(void) { return System().cinematic_state(); }

void ScriptGlue_SetCinematicState(int state)
{
	System().set_cinematic_state(state & 0xFF);
	game.cinematic = state & 0xFF;
}

ScriptSystem *ScriptGlue_System(void) { return &System(); }

// ------------------------------------------------------------------------------------------------------------
// savegames (SPEC-752 1.2 steps 4 and 5, through the SPEC-303 chunk services of imports 85/86)

namespace {

int chunk_id(const char *id)
{
	// four letters packed big-endian into the integer (SPEC-303)
	return ((uint8_t)id[0] << 24) | ((uint8_t)id[1] << 16) | ((uint8_t)id[2] << 8) | (uint8_t)id[3];
}

class EngineWriter : public ChunkWriter {
public:
	void write(const char *id, const void *data, int size) override { gix.WriteChunk(chunk_id(id), data, size); }
};

class EngineReader : public ChunkReader {
public:
	bool read(const char *id, void *data, int size) override
	{
		// the engine drops the level itself on a wrong id, length, checksum or marker
		return gix.ReadChunk(chunk_id(id), data, size, NULL) == size;
	}
};

} // namespace

void ScriptGlue_WriteLevel(void)
{
	EngineWriter w;
	System().save(w);
}

void ScriptGlue_ReadLevel(void)
{
	// every entity's script link is cleared first; load re-attaches the runners (SPEC-752 4.4)
	for (int i = 0; i < globals.num_edicts; i++)
		g_edicts[i].sg_script = 0;
	g_world_runner = -1;
	EngineReader r;
	ScriptSystem &s = System();
	if (!s.load(r) && s.pending_error().empty())
		gix.error("ReadLevel: bad script chunks");
	RaisePendingError();
	// the cinematic byte comes from the GAME chunk (SPEC-752 3.1 byte 0x615, 0 in every valid save). The worldspawn
	// runner is not re-found: a restored level's precache script has run already (it is FINISHED).
	s.set_cinematic_state(game.cinematic & 0xFF);
}

int ScriptGlue_EncodeAiDone(int done_id) { return System().encode_ai_done(done_id); }
int ScriptGlue_DecodeAiDone(int code) { return System().decode_ai_done(code); }
