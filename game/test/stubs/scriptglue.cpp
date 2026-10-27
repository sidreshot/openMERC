// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - test-only no-op stand-ins for the hooks package G6b (scriptglue/) provides to G18's files. Compiled only when
// "scriptglue" is listed in -DOM_GAME_SKIP=... (game/CMakeLists.txt), so that the rest of gamex86.dll can be built and
// black-box tested while that package does not compile. Never part of a shipped gamex86.dll.
// Implements: (test scaffolding, no spec)

#include "../../src/g_local.h"
#include "../../src/scriptglue/g_scriptglue.h"

void SP_script_runner(edict_t *ent) { SP_Placeholder(ent); }
void SG_WorldScriptRunner(edict_t *ent) { SP_Placeholder(ent); }
void ScriptGlue_LevelStart(void) {}
void ScriptGlue_RunFrame(void) {}
void ScriptGlue_Shutdown(void) {}
void ScriptGlue_ClientBegin(edict_t *) {}
void ScriptGlue_ClientEndFrame(edict_t *) {}
void ScriptGlue_ClientDamaged(edict_t *) {}
int ScriptGlue_CinematicState(void) { return game.cinematic; }
void ScriptGlue_SetCinematicState(int s) { game.cinematic = s; }
