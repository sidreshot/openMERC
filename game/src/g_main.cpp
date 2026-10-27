/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
 * OpenMerc: changed for the SoF 1.07f game module (G18): SPEC-001 export table (28 fields) and the
 * 99-entry import table, InitGame cvars of SPEC-006, skill presets and the difficulty record
 * (SPEC-760 B9.2), player-module loading (SPEC-003), RunFrame with SoF's frame order (SPEC-760 B9.3).
 * Implements: SPEC-001, SPEC-006, SPEC-760 B9
 */

#include "g_local.h"
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */
#include "scriptglue/g_scriptglue.h" /* G6b */
#include "nav/nav_api.h" /* G5 */
#include "bots/bot_api.h" /* G5 */
#include "ai/ai_hooks.h" /* G2 */

void G_CrashLogInit(void);     // g_crash.cpp (diagnostics, cvar g18_crashlog)
void G_CrashLogShutdown(void);

game_locals_t game;
level_locals_t level;
game_import_t gix;
gi_wrap gi;
game_export_t globals;
spawn_temp_t st;

int sm_meat_index;
int snd_talk;
int meansOfDeath;

edict_t *g_edicts;
edict_t *world;

cvar_t *deathmatch;
cvar_t *dmflags;
cvar_t *skill;
cvar_t *fraglimit;
cvar_t *timelimit;
cvar_t *password;
cvar_t *spectator_password;
cvar_t *maxclients;
cvar_t *maxspectators;
cvar_t *maxentities;
cvar_t *dedicated;
cvar_t *sv_maxvelocity;
cvar_t *sv_gravity, *sv_gravityx, *sv_gravityy, *sv_gravityz;
cvar_t *sv_rollspeed;
cvar_t *sv_rollangle;
cvar_t *run_pitch;
cvar_t *run_roll;
cvar_t *bob_up;
cvar_t *bob_pitch;
cvar_t *bob_roll;
cvar_t *sv_cheats;
cvar_t *flood_msgs;
cvar_t *flood_persecond;
cvar_t *flood_waitdelay;
cvar_t *flood_killdelay;
cvar_t *sv_maplist;
cvar_t *g_movescale;
cvar_t *sv_suicidepenalty;
cvar_t *ghl_specular, *ghl_light_method, *ghl_mip, *ghl_no_server_update;
cvar_t *ghl_precache_verts, *ghl_precache_texture;
cvar_t *freezeworld;
cvar_t *ctf_control_delay_time, *ctf_flag_respawn_time;
cvar_t *ai_spawnfrequency;
cvar_t *ei_show;
cvar_t *pickupinfo;
cvar_t *gl_pictip;
cvar_t *fs_createpak;
cvar_t *thirdpersoncam, *followenemy, *camera_viewdist;
cvar_t *sv_pmodlistfile, *sv_altpmodlistfile;

void SpawnEntities(const char *mapname, const char *entities, const char *spawnpoint);
void G_RunFrame(void);

//===================================================================
// variadic import helpers

static char g_printbuf[4096];
#define VFMT(fmt)                                                         \
	do {                                                              \
		va_list ap;                                               \
		va_start(ap, fmt);                                        \
		_vsnprintf(g_printbuf, sizeof(g_printbuf) - 1, fmt, ap);  \
		va_end(ap);                                               \
		g_printbuf[sizeof(g_printbuf) - 1] = 0;                   \
	} while (0)

void gi_wrap::bprintf(int lvl, const char *fmt, ...)
{
	VFMT(fmt);
	gix.bprintf(lvl, "%s", g_printbuf);
}
void gi_wrap::dprintf(const char *fmt, ...)
{
	VFMT(fmt);
	gix.dprintf("%s", g_printbuf);
}
void gi_wrap::cprintf(edict_t *e, int lvl, const char *fmt, ...)
{
	VFMT(fmt);
	gix.cprintf(e, lvl, "%s", g_printbuf);
}
void gi_wrap::centerprintf(edict_t *e, const char *fmt, ...)
{
	VFMT(fmt);
	gix.centerprintf(e, "%s", g_printbuf);
}
void gi_wrap::error(const char *fmt, ...)
{
	VFMT(fmt);
	gix.error("%s", g_printbuf);
}
void gi_wrap::sound(edict_t *e, int chan, int idx, float vol, float att, float ofs, int local)
{
	if (!e)
		return;
	if (Bot_DropSound(e, local)) /* G5: SPEC-741 6 / SPEC-760 B9.1 bot hearing */
		return;
	Bot_SoundNoise(e, vol, local); /* G5 */
	gix.sound(e, chan, idx, vol, att, ofs, local);
}
void gi_wrap::positioned_sound(const float *org, edict_t *e, int chan, int idx, float vol, float att, float ofs,
                               int local)
{
	if (e && Bot_DropSound(e, local)) /* G5: SPEC-741 6 / SPEC-760 B9.1 bot hearing */
		return;
	Bot_SoundNoise(e, vol, local); /* G5 */
	gix.positioned_sound(org, e, chan, idx, vol, att, ofs, local);
}
trace_t gi_wrap::trace(const float *start, const float *mins, const float *maxs, const float *end, edict_t *pass,
                       int mask)
{
	trace_t tr;
	memset(&tr, 0, sizeof(tr));
	gix.trace(start, mins, maxs, end, pass, mask, &tr);
	return tr;
}

void Sys_Error(char *error, ...)
{
	VFMT(error);
	gix.error("%s", g_printbuf);
}

void Com_Printf(char *msg, ...)
{
	VFMT(msg);
	gix.dprintf("%s", g_printbuf);
}

//===================================================================

static void ShutdownGame(void)
{
	gi.dprintf("==== ShutdownGame ====\n");
	ScriptGlue_Shutdown(); /* G6b */
	Bot_LevelExit(); /* G5 */
	Nav_Shutdown(); /* G5 */
	PModel_Shutdown();
	/* SPEC-AMEND-013: never keep inventory / player-export pointers across
	   ShutdownGame → InitGame. Drop every client inventory while the player
	   module is still loaded, then unload so InitGame re-queries GetPlayer*API. */
	if (g_edicts && game.maxclients > 0) {
		for (int i = 0; i < game.maxclients; i++) {
			edict_t *e = &g_edicts[i + 1];
			if (e->client && e->client->inv) {
				PlayerDeleteInventory(e->client->inv);
				e->client->inv = NULL;
			}
		}
	}
	PlayerModuleUnload();
	GG_Shutdown(); /* G9: SPEC-791 B5 */
	ghoul::IGhoulSystem *gs = GhoulSys();
	if (gs)
		gs->DestroyAllObjects();
	{ extern void Body_ReleaseAll(void); Body_ReleaseAll(); } /* G3: bodies are module-heap objects (before the edicts go) */
	gi.FreeTags(TAG_LEVEL);
	gi.FreeTags(TAG_GAME);
	G_CrashLogShutdown();
}

static int g_skill_presets[5][5] = {
	// toughness, spawning, saving, cheats, maxencum
	{0, 0, -1, 1, 0}, {1, 1, 8, 1, 1}, {2, 2, 5, 1, 2}, {4, 3, 2, 0, 3}, {3, 4, 0, 0, 4}};

// SPEC-760 B9.2 difficulty rows
static const float g_skillrows[5][17] = {
	{0.3f, 0.25f, 0.4f, 4.0f, 0.5f, 0.5f, 0.1f, 2.0f, 1, 2.0f, 0, 0, 0, 0, 0, 0, 0},
	{0.5f, 0.4f, 0.5f, 2.0f, 1.0f, 0.5f, 0.2f, 1.0f, 1, 1.0f, 0.15f, 10, 4, 2, 2, 15, 10},
	{1.0f, 1.0f, 0.8f, 1.0f, 1.0f, 1.0f, 0.3f, 1.0f, 0, 1.0f, 0.4f, 10, 4, 1, 3, 8, 7},
	{3.0f, 1.5f, 1.0f, 0.5f, 1.0f, 1.5f, 0.5f, 1.0f, 0, 1.0f, 0.5f, 12, 6, 1, 3, 6, 5},
	{99, 2.0f, 2.5f, 0.3f, 99, 3.0f, 1.0f, 0.5f, 0, 5.0f, 1.5f, 4, 12, 0.5f, 4, 2, 5}};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void SetupDifficulty(void)
{
	// the skill cvars carry the latch flag 0x10 in the original, so savegames store them (SPEC-303 vector 5:
	// `sk_saving` / 2 / 0x10; G10: the original's save has 50 cvars, OpenMerc's had 46 without these four). The
	// presets are therefore force-set (a latched cvar set while a server runs would only be stored for the next game)
	static const char *const latched[] = {"sk_toughness", "sk_spawning", "sk_saving", "sk_maxencum"};
	for (const char *n : latched)
		gix.cvar(n, "0", CVAR_LATCH, NULL);
	if (!deathmatch->value) {
		int s = (int)floor(skill->value);
		if (s < 0)
			s = 0;
		if (s <= 4) {
			char buf[16];
			sprintf(buf, "%d", g_skill_presets[s][0]);
			gix.cvar_forceset("sk_toughness", buf);
			sprintf(buf, "%d", g_skill_presets[s][1]);
			gix.cvar_forceset("sk_spawning", buf);
			sprintf(buf, "%d", g_skill_presets[s][2]);
			gix.cvar_forceset("sk_saving", buf);
			sprintf(buf, "%d", g_skill_presets[s][3]);
			gix.cvar_set("cheats", buf);
			sprintf(buf, "%d", g_skill_presets[s][4]);
			gix.cvar_forceset("sk_maxencum", buf);
		}
	}
	game.saves_allowed = (int)gix.cvar_value("sk_saving");
	game.cheats = (int)gix.cvar_value("cheats");
	game.skill_spawning = clampi((int)gix.cvar_value("sk_spawning"), 0, 4);
	game.skill_toughness = clampi((int)gix.cvar_value("sk_toughness"), 0, 4);
	game.skill_encumbrance = clampi((int)gix.cvar_value("sk_maxencum"), 0, 4);
	const float *rt = g_skillrows[game.skill_toughness], *rs = g_skillrows[game.skill_spawning];
	for (int i = 0; i < 9; i++)
		game.tparm[i] = rt[i];
	game.sparm = rs[9];
	game.spawn_alertgain = rs[10];
	game.spawn_alertfade = rs[11];
	game.spawn_maxalert = rs[12];
	game.spawn_maxinterval = rs[13];
	game.spawn_maxnear = rs[14];
	game.spawn_interval = rs[15];
	game.encumbrance = (int)g_skillrows[game.skill_encumbrance][16];

	float d;
	if (game.saves_allowed == -1)
		d = 0.4f;
	else if (game.saves_allowed == 0)
		d = 2.0f;
	else
		d = 1.0f - game.saves_allowed * 0.05f;
	if (game.cheats)
		d *= 0.5f;
	if (game.skill_spawning == 0)
		d *= 0.3f;
	else if (game.skill_spawning == 4)
		d *= 4;
	else
		d += game.skill_spawning * 0.3f;
	if (game.skill_toughness == 0)
		d *= 0.3f;
	else if (game.skill_toughness == 4)
		d *= 3;
	else
		d += game.skill_toughness * 0.4f;
	if (game.encumbrance == 0)
		d *= 0.5f;
	else
		d *= 10.0f / game.encumbrance;
	if (d < 0.2f)
		d = 0.2f;
	game.difficulty_score = d;
}

static void InitGame(void)
{
	gi.dprintf("==== InitGame ====\n");
	G_InstallG3Hooks();

	gi.cvar("hostname", "", 0);
	ctf_flag_respawn_time = gi.cvar("ctf_flag_respawn_time", "90", CVAR_ARCHIVE);
	static const char *botcv[][2] = {{"bot_waypoints", "1"}, {"bot_wp_edit", "0"}, {"bot_listfile", "botlist.txt"},
	                                 {"bot_random", "1"},    {"bot_fillserv", "0"}, {"bot_configexec", "1"},
	                                 {"bot_chat", "0"},      {"bot_wppref", "1"},   {"bot_hunt", "1"},
	                                 {"bot_notarget", "0"},  {"bot_runtact", "1"},  {"bot_timing", "0"},
	                                 {"bot_clientstatus", "0"}};
	for (auto &c : botcv)
		gi.cvar(c[0], c[1], 0);
	sv_rollspeed = gi.cvar("sv_rollspeed", "200", 0);
	sv_rollangle = gi.cvar("sv_rollangle", "2", 0);
	sv_maxvelocity = gi.cvar("sv_maxvelocity", "2000", 0);
	sv_gravity = gi.cvar("sv_gravity", "800", 0);
	sv_gravityx = gi.cvar("sv_gravityx", "0", 0);
	sv_gravityy = gi.cvar("sv_gravityy", "0", 0);
	sv_gravityz = gi.cvar("sv_gravityz", "1", 0);
	g_movescale = gi.cvar("g_movescale", "1.0", CVAR_ARCHIVE | CVAR_SERVERINFO);
	static const char *aicv[][3] = {{"ai_freeze", "0", "0"},      {"ai_goretest", "0", "0"},
	                                {"ai_pathtest", "0", "0"},    {"ai_highpriority", "0", "0"},
	                                {"ei_show", "0", "0"},        {"aipoints_show", "0", "0"},
	                                {"ai_nonodes", "0", "0"},     {"aidec_show", "0", "0"},
	                                {"ai_dumb", "0", "0"},        {"ai_editdir", "p:", "1"},
	                                {"ai_maxcorpses", "5", "1"},  {"ai_corpselife", "5", "1"},
	                                {"ai_spawnfrequency", "1.0", "2048"}};
	for (auto &c : aicv)
		gi.cvar(c[0], c[1], atoi(c[2]));
	ai_spawnfrequency = gi.cvar("ai_spawnfrequency", "1.0", CVAR_MISC);
	ei_show = gi.cvar("ei_show", "0", 0);
	ghl_specular = gi.cvar("ghl_specular", "1", CVAR_ARCHIVE);
	ghl_light_method = gi.cvar("ghl_light_method", "2", CVAR_ARCHIVE);
	ghl_precache_verts = gi.cvar("ghl_precache_verts", "0", 0);
	ghl_precache_texture = gi.cvar("ghl_precache_texture", "1", 0);
	ghl_no_server_update = gi.cvar("ghl_no_server_update", "0", 0);
	followenemy = gi.cvar("followenemy", "0", 0);
	thirdpersoncam = gi.cvar("thirdpersoncam", "0", 0);

	dedicated = gi.cvar("dedicated", "0", CVAR_NOSET);
	gi.cvar("gamename", GAMEVERSION, CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar("gamedate", "Jun 14 2001", CVAR_SERVERINFO | CVAR_LATCH);
	maxclients = gi.cvar("maxclients", "4", CVAR_SERVERINFO | CVAR_LATCH);
	maxspectators = gi.cvar("maxspectators", "4", CVAR_SERVERINFO | CVAR_LATCH);
	deathmatch = gi.cvar("deathmatch", "0", CVAR_SERVERINFO | CVAR_LATCH);
	maxentities = gi.cvar("maxentities", "1024", CVAR_LATCH);
	gi.cvar("ctf_loops", "3", CVAR_ARCHIVE | CVAR_SERVERINFO);
	ctf_control_delay_time = gi.cvar("ctf_control_delay_time", "20", CVAR_ARCHIVE);
	gi.cvar("control_limit", "300", 0);
	gi.cvar("control_bonus", "1", 0);
	gi.cvar("mskins_expression_limit", "0", CVAR_ARCHIVE | CVAR_LATCH);
	gi.cvar("mskins_variety_limit", "0", CVAR_ARCHIVE | CVAR_LATCH);
	dmflags = gi.cvar("dmflags", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
	fraglimit = gi.cvar("fraglimit", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
	timelimit = gi.cvar("timelimit", "0", CVAR_ARCHIVE | CVAR_SERVERINFO);
	sv_suicidepenalty = gi.cvar("sv_suicidepenalty", "2", CVAR_ARCHIVE | CVAR_SERVERINFO);
	password = gi.cvar("password", "", CVAR_USERINFO);
	spectator_password = gi.cvar("spectator_password", "", CVAR_USERINFO);
	skill = gi.cvar("skill", "2", CVAR_SERVERINFO | CVAR_LATCH);
	freezeworld = gi.cvar("freezeworld", "0", 0);
	sv_maplist = gi.cvar("sv_maplist", "", 0);
	sv_pmodlistfile = gi.cvar("sv_pmodlistfile", "ghoul/pmodels/everybody", CVAR_ARCHIVE | CVAR_LATCH);
	sv_altpmodlistfile = gi.cvar("sv_altpmodlistfile", "custom", CVAR_ARCHIVE | CVAR_LATCH);
	run_pitch = gi.cvar("run_pitch", "0.002", 0);
	run_roll = gi.cvar("run_roll", "0.005", 0);
	bob_up = gi.cvar("bob_up", "0.005", 0);
	bob_pitch = gi.cvar("bob_pitch", "0.002", 0);
	bob_roll = gi.cvar("bob_roll", "0.002", 0);
	camera_viewdist = gi.cvar("camera_viewdist", "60.0", 0);
	{ extern void Gore_ReadLocks(void); Gore_ReadLocks(); } /* G3: parental locks read once (SPEC-721 13) */
	static const char *logcv[][2] = {{"log_file_name", ""},
	                                 {"log_file_footer", ""},
	                                 {"log_file_header", ""},
	                                 {"log_file_line_header", ""},
	                                 {"log_file_mode", "a"}};
	for (auto &c : logcv)
		gi.cvar(c[0], c[1], CVAR_ARCHIVE);
	gi.cvar("sv_restartlevel", "0", 0);
	gi.cvar("stealth", "1", 0);
	pickupinfo = gi.cvar("pickupinfo", "0", CVAR_SERVERINFO | CVAR_LATCH);
	gi.cvar("weaponarena", "0", CVAR_SERVERINFO | CVAR_LATCH);
	flood_msgs = gi.cvar("flood_msgs", "4", CVAR_ARCHIVE);
	flood_persecond = gi.cvar("flood_persecond", "4", CVAR_ARCHIVE);
	flood_waitdelay = gi.cvar("flood_waitdelay", "10", CVAR_ARCHIVE);
	flood_killdelay = gi.cvar("flood_killdelay", "10", CVAR_ARCHIVE);
	gi.cvar("cpu_mmx", "0", 0);
	gi.cvar("cpu_amd3d", "0", 0);
	gi.cvar("use_mmx", "1", 0);
	gi.cvar("use_amd3d", "1", 0);
	gl_pictip = gi.cvar("gl_pictip", "0", 0);
	fs_createpak = gi.cvar("fs_createpak", "0", 0);
	sv_cheats = gi.cvar("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH);

	game.lock_deaths = gix.cvar_value("lock_deaths") != 0;

	SetupDifficulty();
	InitItems();

	game.helpmessage1[0] = game.helpmessage2[0] = 0;

	game.maxentities = (int)maxentities->value;
	if (game.maxentities > MAX_EDICTS)
		game.maxentities = MAX_EDICTS;
	g_edicts = (edict_t *)gi.TagMalloc(game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
	globals.edicts = g_edicts;
	world = g_edicts;
	globals.max_edicts = game.maxentities;

	game.maxclients = (int)maxclients->value;
	game.clients = (gclient_t *)gi.TagMalloc(game.maxclients * sizeof(game.clients[0]), TAG_GAME);
	globals.num_edicts = game.maxclients + 1;

	PlayerModuleLoad();
}

//===================================================================

static const char *GetGameType(void)
{
	return GM_GameTypeName(); /* G9: SPEC-790 1 */
	switch ((int)deathmatch->value) {
	case 0: return "single player";
	case 1: return ((int)dmflags->value & 0x80) ? "Team DM" : "DM";
	case 2: return "Assassin";
	case 3: return "Arsenal";
	case 4: return "CTF";
	case 5: return ((int)dmflags->value & 0x80) ? "Team Real" : "Realistic";
	case 6: return "Control";
	case 7: return "Conquer";
	}
	return "Unknown";
}

static int GetCinematicState(void) { return game.cinematic & 0xFF; }
static void SetCinematicState(int s) { game.cinematic = s & 0xFF; }

// The game counts its own frames: every call advances level.framenum by one (Quake II), whatever the engine's
// counter says. The engine calls RunFrame(0) and RunFrame(1) while loading and then RunFrame(1), RunFrame(2) вЂ¦,
// so the game time is (argument + 2) Г— 0.1 during play. Measured with game/probe/rfproxy.cpp on the original module
// (capture rf_orig: call n returns n Г— 0.1; RunFrame(41) в†’ 4.3, also G6b's proxy log); SPEC-760 B9.3 "time =
// framenumВ·0.1" refers to the game's own counter (OPEN_QUESTIONS G18 30).
static float G_RunFrameExport(int framenum)
{
	(void)framenum;
	// SPEC-AMEND-006 G18.30: the game's own frame counter (cleared with the level state at SpawnEntities) gives the
	// time; freezeworld makes the frame do nothing; bots run just before the counter and time are set
	if (freezeworld && freezeworld->value)
		return level.time;
	Bot_RunFrame(); /* G5: bot management and bot user commands (SPEC-741 2, 5) */
	level.framenum++;
	G_RunFrame();
	return level.time;
}

/* OpenMerc marker export: the engine counts a game/player module pair that both
   carry it as "pure" for sv_violence bit 0x20 (docs/DEVIATIONS.md, pure server). */
extern "C" OM_EXPORT const char *OpenMerc_Module(void)
{
	return "OpenMerc game module";
}

extern "C" OM_EXPORT game_export_t *GetGameAPI(game_import_t *import)
{
	gix = *import;
	G_CrashLogInit();

	// the original game sets the Ghoul texture detail from ghl_mip before Init (SPEC-001 test 1)
	ghl_mip = gix.cvar("ghl_mip", "0", CVAR_ARCHIVE, NULL);
	ghoul::IGhoulSystem *gs = gix.GetGhoul ? gix.GetGhoul() : NULL;
	if (gs) {
		int mip = (int)ghl_mip->value;
		gs->SetTextureDetail(mip < 0 ? 0 : mip > 2 ? 2 : mip);
	}

	globals.apiversion = GAME_API_VERSION;
	globals.Init = InitGame;
	globals.Shutdown = ShutdownGame;
	globals.SpawnEntities = SpawnEntities;
	globals.WriteGame = WriteGame;
	globals.ReadGame = ReadGame;
	globals.WriteLevel = WriteLevel;
	globals.ReadLevel = ReadLevel;
	globals.ClientPreConnect = ClientPreConnect;
	globals.ClientConnect = ClientConnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientCommand = ClientCommand;
	globals.ClientThink = ClientThink;
	globals.ClientDropped = ClientDropped;
	globals.SaveAllowed = SaveAllowed;
	globals.SavesLeft = SavesLeft;
	globals.GameStats = GameStats;
	globals.UpdateInven = UpdateInven;
	globals.GetGameType = GetGameType;
	globals.GetCinematicState = GetCinematicState;
	globals.SetCinematicState = SetCinematicState;
	globals.RunFrame = G_RunFrameExport;
	globals.edict_size = sizeof(edict_t);
	return &globals;
}

//======================================================================

static void ClientEndServerFrames(void)
{
	for (int i = 0; i < maxclients->value; i++) {
		edict_t *ent = g_edicts + 1 + i;
		if (!ent->inuse || !ent->client)
			continue;
		ClientEndServerFrame(ent);
	}
}

static edict_t *CreateTargetChangeLevel(const char *map)
{
	edict_t *ent = G_Spawn();
	ent->classname = (char *)"target_changelevel";
	Com_sprintf(level.nextmap, sizeof(level.nextmap), "%s", map);
	ent->map = level.nextmap;
	return ent;
}

void EndDMLevel(void)
{
	GM_EndDMLevel(); /* G9: SPEC-790 B2 map rotation with the mode's same-map flag */
	return;
	edict_t *ent;
	char *s, *t, *f;
	static const char *seps = " ,\n\r";

	if (*sv_maplist->string) {
		s = _strdup(sv_maplist->string);
		f = NULL;
		t = strtok(s, seps);
		while (t != NULL) {
			if (Q_stricmp(t, level.mapname) == 0) {
				t = strtok(NULL, seps);
				if (t == NULL) {
					if (f == NULL)
						BeginIntermission(CreateTargetChangeLevel(level.mapname));
					else
						BeginIntermission(CreateTargetChangeLevel(f));
				} else
					BeginIntermission(CreateTargetChangeLevel(t));
				free(s);
				return;
			}
			if (!f)
				f = t;
			t = strtok(NULL, seps);
		}
		free(s);
	}

	if (level.nextmap[0])
		BeginIntermission(CreateTargetChangeLevel(level.nextmap));
	else {
		ent = G_Find(NULL, FOFS(classname), "target_changelevel");
		if (!ent) {
			BeginIntermission(CreateTargetChangeLevel(level.mapname));
			return;
		}
		BeginIntermission(ent);
	}
}

// SPEC-760 B9.5: time limit per player from the entry frame, frag limit per player
static void CheckDMRules(void)
{
	GM_RunFrame(); /* G9: SPEC-790 B2 mode frame rule and end rules */
	return;
	if (level.intermissiontime)
		return;
	if (!deathmatch->value)
		return;

	for (int i = 0; i < maxclients->value; i++) {
		gclient_t *cl = game.clients + i;
		if (!g_edicts[i + 1].inuse || !cl->pers.connected)
			continue;
		if (timelimit->value && level.framenum - cl->resp.enterframe >= timelimit->value * 600) {
			gix.SP_Print(NULL, 0x003C);
			EndDMLevel();
			return;
		}
		if (fraglimit->value && cl->resp.score >= fraglimit->value) {
			gix.SP_Print(NULL, 0x003B);
			EndDMLevel();
			return;
		}
	}
}

static void ExitLevel(void)
{
	char command[256];

	GM_LevelExit(); /* G9: SPEC-790 B2 level-exit rule */

	Com_sprintf(command, sizeof(command), "gamemap \"%s\"\n", level.changemap);
	gi.AddCommandString(command);
	level.changemap = NULL;
	level.exitintermission = 0;
	level.intermissiontime = 0;
	game.cinematic = 0;
	gix.cvar_set("sv_jumpcinematic", "0");
	Bot_LevelExit(); /* G5: SPEC-741 1.4 */
	ClientEndServerFrames();

	for (int i = 0; i < maxclients->value; i++) {
		edict_t *ent = g_edicts + 1 + i;
		if (!ent->inuse)
			continue;
		if (ent->health > ent->client->pers.max_health)
			ent->health = ent->client->pers.max_health;
	}
}

// SPEC-760 B9.3 deathmatch ranking
static void RankPlayers(void)
{
	int n = 0;
	gclient_t *order[MAX_CLIENTS];
	for (int i = 0; i < game.maxclients && i < MAX_CLIENTS; i++) {
		if (!g_edicts[i + 1].inuse || !game.clients[i].pers.connected || game.clients[i].resp.spectator)
			continue;
		order[n++] = &game.clients[i];
	}
	for (int i = 0; i < n; i++) {
		int rank = 1;
		for (int j = 0; j < n; j++)
			if (order[j]->resp.score > order[i]->resp.score)
				rank++;
		order[i]->ps.dmrank = (uint8_t)rank;
		order[i]->ps.dmrankcount = (uint8_t)n;
	}
}

void G_RunFrame(void)
{
	level.time = level.framenum * FRAMETIME;

	if (level.exitintermission) {
		ExitLevel();
		return;
	}

	G_MoverSoundsFrame();
	G_WorldSpawnerFrame();

	// single player: fade to black while player 1 is dead, then respawn (SPEC-760 B9.3)
	if (!deathmatch->value && game.maxclients >= 1) {
		edict_t *p = g_edicts + 1;
		if (p->inuse && p->client && p->health <= 0 && p->client->pers.connected) {
			level.fadealpha += 0.02f;
			if (level.fadealpha >= 1.0f) {
				level.fadealpha = 1.0f;
				gi.AddCommandString("respawn\n");
			}
		} else
			level.fadealpha = 0;
	}

	ghoul::IGhoulSystem *gs = GhoulSys();
	if (gs)
		gs->BeginFrame();

	om_ai::FrameBegin(); /* G2: sight client/character and character manager before entities run (SPEC-700 rule 7) */

	edict_t *ent = &g_edicts[0];
	for (int i = 0; i < globals.num_edicts; i++, ent++) {
		if (!ent->inuse)
			continue;

		level.current_entity = ent;

		if (ent->groundentity && ent->groundentity->linkcount != ent->groundentity_linkcount) {
			ent->groundentity = NULL;
			om_ai::EntityGroundLost(ent); /* G2: characters re-check their ground (SPEC-760 B9.3) */
		}

		if (i > 0 && i <= maxclients->value) {
			// SPEC-700 rule 7.4 / §11: while the dead-hostage limit (not -1) is reached, every player is killed at the
			// start of its frame (mission failure)
			if (!deathmatch->value && level.maxdeadhostages != -1 && level.deadhostages >= level.maxdeadhostages &&
			    ent->client && ent->health > 0 && !ent->deadflag) {
				ent->health = 0;
				meansOfDeath = MOD_SUICIDE;
				player_die(ent, ent, ent, 100000, vec3_origin);
			}
			ClientBeginServerFrame(ent);
			continue;
		}

		// SPEC-700 rule 7.4 (SPEC-AMEND-006, G2's report): each non-player entity first advances its Ghoul instance's
		// event processing to the level time (due animation events), then its physics and think run
		GhoulEntityEvents(ent);
		G_RunEntity(ent);
	}

	if (gs)
		gs->EndFrame();

	ItemsFrame();

	ScriptGlue_RunFrame(); /* G6b: scripts after entities and the game-rules hook, before the DM checks (SPEC-750 3.1) */

	CheckDMRules();

	if (deathmatch->value)
		RankPlayers();

	ClientEndServerFrames();
}

#if defined(_M_IX86) || defined(__i386__) /* the SoF 32-bit x86 ABI (MSVC or GCC/Clang i386) */
static_assert(offsetof(edict_t, client) == 0x74, "entity prefix: client");
static_assert(offsetof(edict_t, svflags) == 0x118, "entity prefix: svflags");
static_assert(offsetof(edict_t, mins) == 0x11C, "entity prefix: mins");
static_assert(offsetof(edict_t, solid) == 0x158, "entity prefix: solid");
static_assert(offsetof(edict_t, owner) == 0x160, "entity prefix: owner");
static_assert(offsetof(edict_t, ghoulInst) == 0x164, "entity prefix: ghoul instance");
static_assert(offsetof(edict_t, movetype) == 0x19C, "entity prefix ends at 0x19C");
static_assert(offsetof(gclient_t, ping) == 200, "client record: ping at 200");
#endif
