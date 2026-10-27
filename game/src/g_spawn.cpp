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
 * OpenMerc: changed for the SoF 1.07f game module (G18): the 96-key table, item table before the
 * classname tables, placeholders for classnames owned by later packages, SoF level spawn order,
 * difficulty/mode spawnflags, worldspawn and the material-flag configstring.
 * Implements: SPEC-006 Behaviour 1/2, SPEC-760 B1, SPEC-760 4
 */

#include "g_local.h"
#include "damage/damage_api.h" /* G3: bodies freed at level spawn */
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */
#include "scriptglue/g_scriptglue.h" /* G6b */
#include "nav/nav_api.h" /* G5 */
#include "bots/bot_api.h" /* G5 */
#include "ai/ai_hooks.h" /* G2 */

typedef struct {
	const char *name;
	void (*spawn)(edict_t *ent);
} spawn_t;

#define SPAWN(x) void SP_##x(edict_t *ent);
SPAWN(item_health) SPAWN(info_player_start) SPAWN(info_player_deathmatch) SPAWN(info_player_intermission)
SPAWN(func_plat) SPAWN(func_rotating) SPAWN(func_button) SPAWN(func_door) SPAWN(func_door_secret)
SPAWN(func_door_rotating) SPAWN(func_water) SPAWN(func_train) SPAWN(func_conveyor) SPAWN(func_wall)
SPAWN(func_object) SPAWN(func_timer) SPAWN(func_areaportal) SPAWN(func_clock) SPAWN(func_killbox)
SPAWN(trigger_always) SPAWN(trigger_once) SPAWN(trigger_multiple) SPAWN(trigger_relay) SPAWN(trigger_push)
SPAWN(trigger_hurt) SPAWN(trigger_key) SPAWN(trigger_counter) SPAWN(trigger_elevator) SPAWN(trigger_gravity)
SPAWN(trigger_monsterjump) SPAWN(target_temp_entity) SPAWN(target_speaker) SPAWN(target_explosion)
SPAWN(target_changelevel) SPAWN(target_secret) SPAWN(target_goal) SPAWN(target_splash) SPAWN(target_spawner)
SPAWN(target_crosslevel_trigger) SPAWN(target_crosslevel_target) SPAWN(target_laser) SPAWN(target_help)
SPAWN(target_lightramp) SPAWN(target_earthquake) SPAWN(target_character) SPAWN(target_string)
SPAWN(worldspawn) SPAWN(light) SPAWN(info_null) SPAWN(info_notnull) SPAWN(path_corner) SPAWN(point_combat)
SPAWN(func_door_portcullis) SPAWN(func_door_rotating_smart) SPAWN(func_breakable_brush) SPAWN(func_trigger_brush)
SPAWN(func_pushkillbox) SPAWN(func_alarm) SPAWN(func_ignite) SPAWN(func_Dekker_marker) SPAWN(func_Dekker_split)
SPAWN(func_hide_the_players_gun) SPAWN(func_snipercam_zoom) SPAWN(func_mission_status) SPAWN(func_player_health)
SPAWN(func_player_armor) SPAWN(func_player_item) SPAWN(func_score) SPAWN(func_camerashake) SPAWN(func_fade)
SPAWN(func_remote_camera) SPAWN(trigger_activate) SPAWN(trigger_conquer) SPAWN(trigger_countdown)
SPAWN(trigger_useable) SPAWN(trigger_heal) SPAWN(trigger_movedown) SPAWN(trigger_keep_using) SPAWN(trigger_safezone)
SPAWN(trigger_sound) SPAWN(trigger_objectives) SPAWN(spawner) SPAWN(spawner_monster) SPAWN(spawner_boosterpack)
SPAWN(dm_KOTH_targetzone) SPAWN(test_model) SPAWN(marker)
#undef SPAWN

static void SP_func_group(edict_t *ent) { G_FreeEdict(ent); }

static const spawn_t spawns[] = {
	{"info_player_start", SP_info_player_start},
	{"info_player_deathmatch", SP_info_player_deathmatch},
	{"info_player_team1", SP_info_player_deathmatch},
	{"info_player_team2", SP_info_player_deathmatch},
	{"info_player_intermission", SP_info_player_intermission},
	{"func_plat", SP_func_plat},
	{"func_button", SP_func_button},
	{"func_door", SP_func_door},
	{"func_door_secret", SP_func_door_secret},
	{"func_door_rotating", SP_func_door_rotating},
	{"func_rotating", SP_func_rotating},
	{"func_train", SP_func_train},
	{"func_water", SP_func_water},
	{"func_conveyor", SP_func_conveyor},
	{"func_areaportal", SP_func_areaportal},
	{"func_clock", SP_func_clock},
	{"func_wall", SP_func_wall},
	{"func_object", SP_func_object},
	{"func_timer", SP_func_timer},
	{"func_killbox", SP_func_killbox},
	{"func_group", SP_func_group},
	{"trigger_always", SP_trigger_always},
	{"trigger_once", SP_trigger_once},
	{"trigger_multiple", SP_trigger_multiple},
	{"trigger_relay", SP_trigger_relay},
	{"trigger_push", SP_trigger_push},
	{"trigger_hurt", SP_trigger_hurt},
	{"trigger_key", SP_trigger_key},
	{"trigger_counter", SP_trigger_counter},
	{"trigger_elevator", SP_trigger_elevator},
	{"trigger_gravity", SP_trigger_gravity},
	{"trigger_monsterjump", SP_trigger_monsterjump},
	{"target_temp_entity", SP_target_temp_entity},
	{"target_speaker", SP_target_speaker},
	{"target_explosion", SP_target_explosion},
	{"target_changelevel", SP_target_changelevel},
	{"target_secret", SP_target_secret},
	{"target_goal", SP_target_goal},
	{"target_splash", SP_target_splash},
	{"target_spawner", SP_target_spawner},
	{"target_crosslevel_trigger", SP_target_crosslevel_trigger},
	{"target_crosslevel_target", SP_target_crosslevel_target},
	{"target_laser", SP_target_laser},
	{"target_help", SP_target_help},
	{"target_lightramp", SP_target_lightramp},
	{"target_earthquake", SP_target_earthquake},
	{"target_character", SP_target_character},
	{"target_string", SP_target_string},
	{"worldspawn", SP_worldspawn},
	{"light", SP_light},
	{"info_null", SP_info_null},
	{"info_notnull", SP_info_notnull},
	{"path_corner", SP_path_corner},
	{"point_combat", SP_point_combat},
	// SPEC-761
	{"func_door_portcullis", SP_func_door_portcullis},
	{"func_door_rotating_smart", SP_func_door_rotating_smart},
	{"func_breakable_brush", SP_func_breakable_brush},
	{"func_trigger_brush", SP_func_trigger_brush},
	{"func_pushkillbox", SP_func_pushkillbox},
	{"func_alarm", SP_func_alarm},
	{"func_ignite", SP_func_ignite},
	{"func_Dekker_console", SP_func_Dekker_marker},
	{"func_Dekker_diehere", SP_func_Dekker_marker},
	{"func_Dekker_jumphere", SP_func_Dekker_marker},
	{"func_Dekker_split", SP_func_Dekker_split},
	{"func_hide_the_players_gun", SP_func_hide_the_players_gun},
	{"func_snipercam_zoom", SP_func_snipercam_zoom},
	{"func_mission_status", SP_func_mission_status},
	{"func_player_health", SP_func_player_health},
	{"func_player_armor", SP_func_player_armor},
	{"func_player_item", SP_func_player_item},
	{"func_score", SP_func_score},
	{"func_camerashake", SP_func_camerashake},
	{"func_fade", SP_func_fade},
	{"func_remote_camera", SP_func_remote_camera},
	{"trigger_activate", SP_trigger_activate},
	{"trigger_conquer", SP_trigger_conquer},
	{"trigger_countdown", SP_trigger_countdown},
	{"trigger_useable", SP_trigger_useable},
	{"trigger_heal", SP_trigger_heal},
	{"trigger_movedown", SP_trigger_movedown},
	{"trigger_keep_using", SP_trigger_keep_using},
	{"trigger_safezone", SP_trigger_safezone},
	{"trigger_sound", SP_trigger_sound},
	{"trigger_objectives", SP_trigger_objectives},
	{"spawner", SP_spawner},
	{"spawner_monster", SP_spawner_monster},
	{"spawner_boosterpack", SP_spawner_boosterpack},
	{"dm_KOTH_targetzone", SP_dm_KOTH_targetzone},
	{"test_model", SP_test_model},
	{"misc_exploding_heli", SP_marker},
	{"test_rj", SP_marker},
	{"script_runner", SP_script_runner}, /* G6b */
	{"misc_ctf_base", SP_misc_ctf_base}, /* G9 */
	{"ghoul_model", SP_ghoul_model}, /* G9 */
	{NULL, NULL}};

// every classname of the classname tables; the ones without an entry above belong to later packages
// and get the placeholder spawn
static const char *all_classnames[] = {
#include "g_classlist.inc"
	NULL};

static int g_inhibit;

/*
===============
ED_CallSpawn
===============
*/
void ED_CallSpawn(edict_t *ent)
{
	if (!ent->classname) {
		gi.dprintf("ED_CallSpawn: NULL classname\n");
		return;
	}
	if (ent->classname[0] == '_') {
		G_FreeEdict(ent);
		return;
	}
	if (gl_pictip->value && !strncmp(ent->classname, "misc_", 5) && (ent->spawnflags & 64)) {
		G_FreeEdict(ent);
		return;
	}

	// the item table first (SPEC-006 1.4)
	gitem_t *item = FindItemByClassname(ent->classname);
	if (!item && !strncmp(ent->classname, "ctf", 3))
		item = FindItemByClassname("item_ctf_flag");
	if (item) {
		if (SpawnItemCheck(ent, item))
			SpawnItem(ent, item);
		return;
	}

	for (const spawn_t *s = spawns; s->name; s++) {
		if (!strcmp(s->name, ent->classname)) {
			s->spawn(ent);
			return;
		}
	}
	{ extern bool Roster_CallSpawn(edict_t *ent); if (Roster_CallSpawn(ent)) return; } /* G4: m_* characters, helicopters, vehicles */
	{ extern bool G7_CallSpawn(edict_t *ent); if (G7_CallSpawn(ent)) return; } /* G7: props, environ_*, ambient sets */
	for (const char **c = all_classnames; *c; c++) {
		if (!strcmp(*c, ent->classname)) {
			SP_Placeholder(ent);
			return;
		}
	}
	gi.dprintf("%s doesn't have a spawn function\n", ent->classname);
	G_FreeEdict(ent);
}

/*
=============
ED_NewString
=============
*/
char *ED_NewString(const char *string)
{
	int l = (int)strlen(string) + 1;
	char *newb = (char *)gi.TagMalloc(l, TAG_LEVEL);
	char *new_p = newb;
	for (int i = 0; i < l; i++) {
		if (string[i] == '\\' && i < l - 1) {
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		} else
			*new_p++ = string[i];
	}
	return newb;
}

// SPEC-760 4: the 96 keys (case-insensitive)
field_t fields[] = {
	{"classname", FOFS(classname), F_LSTRING},
	{"origin", FOFS(s.origin), F_VECTOR},
	{"model", FOFS(model), F_LSTRING},
	{"spawnflags", FOFS(spawnflags), F_INT},
	{"speed", FOFS(speed), F_FLOAT},
	{"accel", FOFS(accel), F_FLOAT},
	{"decel", FOFS(decel), F_FLOAT},
	{"target", FOFS(target), F_LSTRING},
	{"targetname", FOFS(targetname), F_LSTRING},
	{"scripttarget", FOFS(scripttarget), F_LSTRING},
	{"pathtarget", FOFS(pathtarget), F_LSTRING},
	{"deathtarget", FOFS(deathtarget), F_LSTRING},
	{"killtarget", FOFS(killtarget), F_LSTRING},
	{"killfacing", FOFS(killfacing), F_LSTRING},
	{"combattarget", FOFS(combattarget), F_LSTRING},
	{"soundname", FOFS(soundname), F_LSTRING},
	{"spawn1", FOFS(spawn1), F_LSTRING},
	{"spawn2", FOFS(spawn2), F_LSTRING},
	{"spawn3", FOFS(spawn3), F_LSTRING},
	{"message", FOFS(message), F_LSTRING},
	{"sp_message", FOFS(sp_message), F_SHORT},
	{"team", FOFS(team), F_LSTRING},
	{"wait", FOFS(wait), F_FLOAT},
	{"delay", FOFS(delay), F_FLOAT},
	{"random", FOFS(random), F_FLOAT},
	{"style", FOFS(style), F_INT},
	{"count", FOFS(count), F_INT},
	{"health", FOFS(health), F_INT},
	{"sounds", FOFS(sounds), F_INT},
	{"light", 0, F_IGNORE},
	{"dmg", FOFS(dmg), F_INT},
	{"angles", FOFS(s.angles), F_VECTOR},
	{"angle", FOFS(s.angles), F_ANGLEHACK},
	{"mass", FOFS(mass), F_INT},
	{"volume", FOFS(volume), F_FLOAT},
	{"attenuation", FOFS(attenuation), F_FLOAT},
	{"map", FOFS(map), F_LSTRING},
	{"skin", FOFS(s.skinnum), F_INT},
	{"alertradius", FOFS(alertradius), F_FLOAT},
	{"personality", FOFS(personality), F_INT},

	{"lip", STOFS(lip), F_INT, FFL_SPAWNTEMP},
	{"distance", STOFS(distance), F_INT, FFL_SPAWNTEMP},
	{"height", STOFS(height), F_INT, FFL_SPAWNTEMP},
	{"noise", STOFS(noise), F_LSTRING, FFL_SPAWNTEMP},
	{"pausetime", STOFS(pausetime), F_FLOAT, FFL_SPAWNTEMP},
	{"item", STOFS(item), F_LSTRING, FFL_SPAWNTEMP},
	{"gravity", STOFS(gravity), F_LSTRING, FFL_SPAWNTEMP},
	{"sky", STOFS(sky), F_LSTRING, FFL_SPAWNTEMP},
	{"skyrotate", STOFS(skyrotate), F_FLOAT, FFL_SPAWNTEMP},
	{"skyaxis", STOFS(skyaxis), F_VECTOR, FFL_SPAWNTEMP},
	{"terrainset", STOFS(terrainset), F_LSTRING, FFL_SPAWNTEMP},
	{"musicSet", STOFS(musicset), F_LSTRING, FFL_SPAWNTEMP},
	{"color", STOFS(color), F_VECTOR, FFL_SPAWNTEMP},
	{"endpoint", STOFS(endpoint), F_VECTOR, FFL_SPAWNTEMP},
	{"minyaw", STOFS(minyaw), F_FLOAT, FFL_SPAWNTEMP},
	{"maxyaw", STOFS(maxyaw), F_FLOAT, FFL_SPAWNTEMP},
	{"minpitch", STOFS(minpitch), F_FLOAT, FFL_SPAWNTEMP},
	{"maxpitch", STOFS(maxpitch), F_FLOAT, FFL_SPAWNTEMP},
	{"nextmap", STOFS(nextmap), F_LSTRING, FFL_SPAWNTEMP},
	{"script", STOFS(script), F_LSTRING, FFL_SPAWNTEMP},
	{"killedvalue", STOFS(killedvalue), F_INT, FFL_SPAWNTEMP},
	{"survivalvalue", STOFS(survivalvalue), F_INT, FFL_SPAWNTEMP},
	{"material", STOFS(material), F_INT, FFL_SPAWNTEMP},
	{"scale", STOFS(scale), F_FLOAT, FFL_SPAWNTEMP},
	{"merctype", STOFS(merctype), F_INT, FFL_SPAWNTEMP},
	{"mercnum", STOFS(mercnum), F_INT, FFL_SPAWNTEMP},
	{"waitaction1", STOFS(waitaction[0]), F_LSTRING, FFL_SPAWNTEMP},
	{"waitaction2", STOFS(waitaction[1]), F_LSTRING, FFL_SPAWNTEMP},
	{"waitaction3", STOFS(waitaction[2]), F_LSTRING, FFL_SPAWNTEMP},
	{"waitaction4", STOFS(waitaction[3]), F_LSTRING, FFL_SPAWNTEMP},
	{"waitaction5", STOFS(waitaction[4]), F_LSTRING, FFL_SPAWNTEMP},
	{"waitaction6", STOFS(waitaction[5]), F_LSTRING, FFL_SPAWNTEMP},
	{"moveaction", STOFS(moveaction), F_LSTRING, FFL_SPAWNTEMP},
	{"screeneffect", STOFS(screeneffect), F_INT, FFL_SPAWNTEMP},
	{"spawnfrequency", STOFS(spawnfrequency), F_FLOAT, FFL_SPAWNTEMP},
	{"parm1", STOFS(parm[0]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm2", STOFS(parm[1]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm3", STOFS(parm[2]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm4", STOFS(parm[3]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm5", STOFS(parm[4]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm6", STOFS(parm[5]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm7", STOFS(parm[6]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm8", STOFS(parm[7]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm9", STOFS(parm[8]), F_LSTRING, FFL_SPAWNTEMP},
	{"parm10", STOFS(parm[9]), F_LSTRING, FFL_SPAWNTEMP},
	{"surfacetype", STOFS(surfacetype), F_INT, FFL_SPAWNTEMP},
	{"noweapons", STOFS(noweapons), F_INT, FFL_SPAWNTEMP},
	{"setweapons", STOFS(setweapons), F_INT, FFL_SPAWNTEMP},
	{"maxDeadHostages", STOFS(maxdeadhostages), F_INT, FFL_SPAWNTEMP},
	{"ambientSet", STOFS(ambientset), F_LSTRING, FFL_SPAWNTEMP},
	{"startmusic", STOFS(startmusic), F_INT, FFL_SPAWNTEMP},
	{"forceHUD", STOFS(forcehud), F_INT, FFL_SPAWNTEMP},
	{"weaponsAvailable", STOFS(weaponsavailable), F_INT, FFL_SPAWNTEMP},
	{"ainame", STOFS(ainame), F_LSTRING, FFL_SPAWNTEMP},
	{"distcull", STOFS(distcull), F_INT, FFL_SPAWNTEMP},
	{"chopsky", STOFS(distcull), F_INT, FFL_SPAWNTEMP},
	{NULL, 0, F_INT, 0}};

/*
===============
ED_ParseField
===============
*/
static void ED_ParseField(const char *key, const char *value, edict_t *ent)
{
	for (field_t *f = fields; f->name; f++) {
		if (Q_stricmp((char *)f->name, (char *)key))
			continue;
		if (f->type == F_IGNORE)
			return;
		uint8_t *b = (f->flags & FFL_SPAWNTEMP) ? (uint8_t *)&st : (uint8_t *)ent;
		vec3_t vec;
		switch (f->type) {
		case F_LSTRING:
			*(char **)(b + f->ofs) = ED_NewString(value);
			break;
		case F_VECTOR:
			vec[0] = vec[1] = vec[2] = 0;
			sscanf(value, "%f %f %f", &vec[0], &vec[1], &vec[2]);
			((float *)(b + f->ofs))[0] = vec[0];
			((float *)(b + f->ofs))[1] = vec[1];
			((float *)(b + f->ofs))[2] = vec[2];
			break;
		case F_INT:
			*(int *)(b + f->ofs) = atoi(value);
			break;
		case F_SHORT:
			*(int *)(b + f->ofs) = (short)atoi(value);
			break;
		case F_FLOAT:
			*(float *)(b + f->ofs) = (float)atof(value);
			break;
		case F_ANGLEHACK: {
			float v = (float)atof(value);
			((float *)(b + f->ofs))[0] = 0;
			((float *)(b + f->ofs))[1] = v;
			((float *)(b + f->ofs))[2] = 0;
			break;
		}
		default:
			break;
		}
		return;
	}
	gi.dprintf("%s is not a field\n", key);
}

/*
====================
ED_ParseEdict
====================
*/
static const char *ED_ParseEdict(const char *data, edict_t *ent)
{
	qboolean init = false;
	char keyname[256];
	char *com_token;

	memset(&st, 0, sizeof(st));

	while (1) {
		com_token = COM_Parse((char **)&data);
		if (com_token[0] == '}')
			break;
		if (!data)
			gi.error("ED_ParseEntity: EOF without closing brace");

		strncpy(keyname, com_token, sizeof(keyname) - 1);
		keyname[sizeof(keyname) - 1] = 0;

		com_token = COM_Parse((char **)&data);
		if (!data)
			gi.error("ED_ParseEntity: EOF without closing brace");
		if (com_token[0] == '}')
			gi.error("ED_ParseEntity: closing brace without data");

		init = true;
		if (keyname[0] == '_')
			continue;
		ED_ParseField(keyname, com_token, ent);
	}

	if (!init)
		memset(ent, 0, sizeof(*ent));

	return data;
}

/*
================
G_FindTeams
================
*/
static void G_FindTeams(void)
{
	edict_t *e, *e2, *chain;
	int i, j, c = 0, c2 = 0;

	for (i = 1, e = g_edicts + i; i < globals.num_edicts; i++, e++) {
		if (!e->inuse || !e->team || (e->flags & FL_TEAMSLAVE))
			continue;
		chain = e;
		e->teammaster = e;
		c++;
		c2++;
		for (j = i + 1, e2 = e + 1; j < globals.num_edicts; j++, e2++) {
			if (!e2->inuse || !e2->team || (e2->flags & FL_TEAMSLAVE))
				continue;
			if (!strcmp(e->team, e2->team)) {
				c2++;
				chain->teamchain = e2;
				e2->teammaster = e;
				chain = e2;
				e2->flags |= FL_TEAMSLAVE;
			}
		}
	}
	gi.dprintf("%i teams with %i entities\n", c, c2);
}

// SPEC-760 B1.5
void G_MaterialFlags(int material, int surfacetype)
{
	auto set = [](int a, int b) {
		for (int i = a; i <= b; i++)
			level.materialflags[i] = 'y';
	};
	if (material == 1 || (material >= 3 && material <= 6))
		set(0, 0);
	else if (material == 7)
		set(1, 3);
	else if (material >= 8 && material <= 11)
		set(4, 6);
	else if ((material >= 12 && material <= 16) || material == 32 || material == 34)
		set(7, 10);
	else if (material >= 17 && material <= 20)
		set(15, 17);
	else if ((material >= 21 && material <= 31) || material == 33)
		set(11, 14);
	switch (surfacetype) {
	case 32: set(11, 11); break;
	case 37: set(1, 1); break;
	case 38: set(1, 1); set(11, 11); break;
	case 39: set(21, 21); break;
	case 41: set(22, 22); break;
	}
}

// a spawn function or precache that writes its own material-flag position (AMEND-005 G18.27: human body precache 10,
// prop classes, `func_remote_camera` 24 = 'p')
void G_SetMaterialFlag(int pos, char c)
{
	if (pos >= 0 && pos < 25)
		level.materialflags[pos] = c;
}

static void GameRulesReset(void)
{
	GM_LevelInit(); /* G9: SPEC-790 B1.1 */
	return;
	if (deathmatch->value)
		gix.SP_Register("dm_generic");
}

static void GameRulesLevelStart(void)
{
	GM_LevelSetup(); /* G9: SPEC-790 B1.3 */
	return;
	if (deathmatch->value) {
		gi.configstring(32, "0");
		gi.configstring(33, "0");
	}
}

/*
==============
SpawnEntities
==============
*/
void SpawnEntities(const char *mapname, const char *entities, const char *spawnpoint)
{
	edict_t *ent;
	int i;

	SaveClientData();
	ScriptGlue_LevelStart(); /* G6b: reset the level-script system (SPEC-760 B9 step 1) */

	// free the bodies of players and corpses before the array is cleared (SPEC-760 B1.3 step 1; G3 bodies are
	// heap objects); bodies owned by an AI object are released with it (om_ai::LevelStart, G2)
	for (int k = 0; k < globals.num_edicts; k++)
		if (g_edicts[k].g3body && !g_edicts[k].g3body->owner)
			Body_Destroy(&g_edicts[k]);
	// level-tagged memory after the bodies (G9 finding: bodies used to be TAG_LEVEL and were freed twice);
	// G3 bodies now come from the module heap with explicit release, so the order is only a safeguard
	gi.FreeTags(TAG_LEVEL);
	memset(&level, 0, sizeof(level));
	memset(g_edicts, 0, game.maxentities * sizeof(g_edicts[0]));

	strncpy(level.mapname, mapname, sizeof(level.mapname) - 1);
	strncpy(game.spawnpoint, spawnpoint, sizeof(game.spawnpoint) - 1);

	for (i = 0; i < game.maxclients; i++) {
		g_edicts[i + 1].client = game.clients + i;
		g_edicts[i + 1].health = 100;
	}

	gix.loadingprogress(0.05f, 1);
	GG_FreeAll(); /* G9: SPEC-791 B5 */
	GhoulResetLevel();
	GG_LevelSpawn(); /* G9: SPEC-791 B5 gore models */
	{ extern void Gore_LevelStart(void); Gore_LevelStart(); } /* G3: gib counters, hint zone */
	{ extern void Roster_LevelStart(void); Roster_LevelStart(); } /* G4: roster records, helicopter globals */
	PlayerModuleForgetViewModels(); // the in-view model objects of the previous level are gone with the registries
	gix.loadingprogress(0.075f, 1);
	memset(level.materialflags, 'n', 25);
	G_DebrisReset();
	level.materialflags[25] = 0;
	GameRulesReset();
	gix.loadingprogress(0.1f, 1);
	gix.SP_Register("items");
	gix.SP_Register("weapons");
	gix.SP_Register("general");
	Nav_LevelReset(); /* G5: reset navigation (SPEC-760 B1.3 step 3) */
	om_ai::LevelStart(); /* G2: character manager reset, AI objects of the previous level released (SPEC-700) */
	gix.loadingprogress(0.15f, 1);
	// reset bodies and items (B1.3 step 3): the weapon/equipment precache, the deathmatch player models and their
	// body precache temporaries (SPEC-780 16, AMEND-005 G18.25 rule 1), the item model records (AMEND-005 G18.28).
	// Order observed on jpndm2 (OPEN_QUESTIONS G18 36): records 14-19 projectiles + armour and sounds 292-341 of the
	// weapon precache come before the first player model (record 20, its voices from sound 353).
	WeaponsLevelPrecache();
	PModel_LevelStart();
	ItemsLevelStart();
	G_PlaceholderReset();
	G_MoverSoundsReset();
	level.precache_all = fs_createpak->value != 0;

	ent = NULL;
	g_inhibit = 0;
	int T = game.skill_toughness;

	while (1) {
		char *com_token = COM_Parse((char **)&entities);
		if (!entities)
			break;
		if (com_token[0] != '{')
			gi.error("ED_LoadFromFile: found %s when expecting {", com_token);

		if (!ent)
			ent = g_edicts;
		else
			ent = G_Spawn();
		entities = ED_ParseEdict(entities, ent);

		if (ent != g_edicts) {
			bool remove = false;
			if (deathmatch->value) {
				if (ent->spawnflags & SPAWNFLAG_NOT_DEATHMATCH)
					remove = true;
			} else {
				if ((T <= 1 && (ent->spawnflags & SPAWNFLAG_NOT_EASY)) ||
				    (T == 2 && (ent->spawnflags & SPAWNFLAG_NOT_MEDIUM)) ||
				    (T >= 3 && (ent->spawnflags & SPAWNFLAG_NOT_HARD)))
					remove = true;
			}
			if (remove) {
				G_FreeEdict(ent);
				g_inhibit++;
				continue;
			}
			ent->spawnflags &= ~(SPAWNFLAG_NOT_EASY | SPAWNFLAG_NOT_MEDIUM | SPAWNFLAG_NOT_HARD |
			                     SPAWNFLAG_NOT_DEATHMATCH);
		}

		ED_CallSpawn(ent);
		if (ent->inuse) {
			G_MaterialFlags(st.material ? st.material : ent->material,
			                st.surfacetype ? st.surfacetype : ent->surfacetype);
			G_DebrisSurface(st.surfacetype ? st.surfacetype : ent->surfacetype);
		}
	}

	// material-flag position 10: every human character body precache (AMEND-005 G18.27) — the characters the map
	// spawned (oracle: ger1, irq1b … have it in single player)
	for (int n = game.maxclients + 1; n < globals.num_edicts; n++) {
		edict_t *e = &g_edicts[n];
		if (!e->inuse || !e->g3body)
			continue;
		int fam = e->g3body->family;
		if (fam == BODY_HUMAN || fam == BODY_MESO || fam == BODY_ECTO || fam == BODY_FEMALE || fam == BODY_MESO_ARMOURED)
			G_SetMaterialFlag(10, 'y');
	}

	ItemsFinalize();
	GameRulesLevelStart();
	gix.loadingprogress(0.25f, 1);
	gi.configstring(CS_MATERIALS, level.materialflags);

	static const char *fx[] = {"environ/waterspurt", "environ/oilspurt",     "environ/chemspurt",
	                           "environ/metal_computer", "environ/glass_computer", "environ/metal_steam",
	                           "environ/bulletsplash", "environ/splatgreen",   "environ/splatorange",
	                           "environ/splatred",   "environ/splatbrown",   "environ/splatblack"};
	for (const char *f : fx)
		gi.effectindex(f);

	gi.dprintf("%i entities inhibited\n", g_inhibit);

	G_FindTeams();
	PlayerTrail_Init();
	gix.loadingprogress(0.28f, 1);
	G_MarkSurfaceSounds(); // material sounds of the map's surfaces (SPEC-760 B1.3 step 3, g_material.cpp)
	{ extern void G7_LevelSpawned(void); G7_LevelSpawned(); } /* G7: ambient sound init (SPEC-760 B1.3 step 5) */
	PlayerModuleLevelStart();
	if (deathmatch->value)
		gix.ghoulregisterall();
	gix.loadingprogress(0.33f, 1);
	level.precache_all = 0;
}

//===================================================================

// SPEC-760 B1.4
void SP_worldspawn(edict_t *ent)
{
	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	ent->inuse = true;
	ent->s.modelindex = 1;
	Nav_WorldSpawn(); /* G5: clear the per-node "seen" bytes (SPEC-740) */

	if (ent->message && ent->message[0]) {
		gi.configstring(CS_NAME, ent->message);
		strncpy(level.level_name, ent->message, sizeof(level.level_name) - 1);
	} else
		strncpy(level.level_name, level.mapname, sizeof(level.level_name) - 1);

	gi.configstring(CS_SKY, st.sky && st.sky[0] ? st.sky : "none");
	if (st.terrainset && st.terrainset[0])
		gi.configstring(CS_TERRAINSET, st.terrainset);
	gi.configstring(CS_SKYROTATE, va("%f", st.skyrotate));
	gi.configstring(CS_SKYAXIS, va("%f %f %f", st.skyaxis[0], st.skyaxis[1], st.skyaxis[2]));
	gi.configstring(CS_CDTRACK, va("%i", ent->sounds));
	gi.configstring(CS_MAXCLIENTS, va("%i", (int)(maxclients->value)));
	gi.configstring(CS_SCREENEFFECT, va("%i", st.screeneffect));
	if (st.musicset && st.musicset[0])
		gi.configstring(CS_MUSICSET, st.musicset);
	gi.configstring(CS_AMBSET, st.ambientset && st.ambientset[0] ? st.ambientset : "sound/sound.ams");
	level.startmusic = st.startmusic;

	if (st.nextmap)
		strncpy(level.nextmap, st.nextmap, sizeof(level.nextmap) - 1);
	if (st.ainame)
		strncpy(level.ainame, st.ainame, sizeof(level.ainame) - 1);
	level.forcehud = st.forcehud;
	level.weaponsavailable = st.weaponsavailable;
	level.weaponmode = st.noweapons ? 0 : (st.setweapons > 0 ? st.setweapons : -1);
	level.maxdeadhostages = st.maxdeadhostages ? st.maxdeadhostages : 3;
	level.countdown_expired = 0;
	level.missionstatus = 0;
	level.alarm = 0;

	// AMEND-005 G18.25 rule 2: the bot system's level start (with its waypoint loader entity), then the body
	// queue, then script_runner
	cvar_t *gr = gi.cvar("sv_globalrankings", "0", 0);
	if (!gr->value && deathmatch->value) {
		// the waypoint loader: `noclass`, never linked, think at + 10 s (G5 loads the waypoints at that time)
		edict_t *wl = G_Spawn();
		wl->think = Bot_WaypointLoaderThink; /* G5: loads the waypoints in this think, then frees it (AMEND-005 G5.2) */
		wl->nextthink = level.time + 10;
		gix.SP_Register("bot");
		Bot_WorldSpawn(); /* G5: bot system level start (SPEC-741 1.1) */
	}

	InitBodyQue();

	if (st.script && st.script[0]) {
		edict_t *s = G_Spawn();
		s->classname = (char *)"script_runner";
		SG_WorldScriptRunner(s); /* G6b */
	}

	for (int i = 1; i <= 4; i++)
		gi.imageindex(va("pics/scope/arrows128_%d", i));

	gix.cvar_set("sv_gravity", st.gravity ? st.gravity : "800");
	gix.cvar_set("timescale", "1.0");

	sm_meat_index = 0;
	snd_talk = gi.soundindex("misc/talk.wav");

	gi.configstring(CS_LIGHTS + 0, "m");
	gi.configstring(CS_LIGHTS + 1, "mmnmmommommnonmmonqnmmo");
	gi.configstring(CS_LIGHTS + 2, "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcb");
	gi.configstring(CS_LIGHTS + 3, "mmmmmaaaaammmmmaaaaaabcdefgabcdefg");
	gi.configstring(CS_LIGHTS + 4, "mamamamamama");
	gi.configstring(CS_LIGHTS + 5, "jklmnopqrstuvwxyzyxwvutsrqponmlkj");
	gi.configstring(CS_LIGHTS + 6, "nmonqnmomnmomomno");
	gi.configstring(CS_LIGHTS + 7, "mmmaaaabcdefgmmmmaaaammmaamm");
	gi.configstring(CS_LIGHTS + 8, "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa");
	gi.configstring(CS_LIGHTS + 9, "aaaaaaaazzzzzzzz");
	gi.configstring(CS_LIGHTS + 10, "mmamammmmammamamaaamammma");
	gi.configstring(CS_LIGHTS + 11, "abcdefghijklmnopqrrqponmlkjihgfedcba");
	gi.configstring(CS_LIGHTS + 12, "mkigegik");
	gi.configstring(CS_LIGHTS + 13, "ex");
	gi.configstring(CS_LIGHTS + 63, "a");

	// spawn1..3: spawned once and freed to precache (SPEC-760 B1.4)
	const char *sp[3] = {ent->spawn1, ent->spawn2, ent->spawn3};
	for (int i = 0; i < 3; i++) {
		level.spawn_classes[i][0] = 0;
		if (!sp[i] || !sp[i][0])
			continue;
		strncpy(level.spawn_classes[i], sp[i], MAX_QPATH - 1);
		int saved = level.precache_all;
		level.precache_all = 1;
		edict_t *e = G_Spawn();
		e->classname = (char *)sp[i];
		spawn_temp_t save = st;
		memset(&st, 0, sizeof(st));
		ED_CallSpawn(e);
		st = save;
		if (e->inuse)
			G_FreeEdict(e);
		level.precache_all = saved;
	}

}

// spawn functions with no own behaviour (SPEC-006 Behaviour 2)
void SP_marker(edict_t *ent) { (void)ent; }
