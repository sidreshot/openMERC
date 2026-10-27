// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G6b: the DesignerScript system wired to the game's entities.
//
// Public interface of game/src/scriptglue/ (owner: G6b). Other packages read this header; they do not edit it
// (requests go to docs/impl/G6b.md). The VM itself is the om_script library (game/src/script, G6a); this package
// implements its om_script::Host on G18's entities and owns the level's one om_script::ScriptSystem.
//
// Hook calls (one call each, at the position given; G18's files carry a /* G6b */ comment there):
//   ScriptGlue_LevelStart()         SpawnEntities, right after the client data is saved and before level memory is
//                                   freed (SPEC-760 B9 step 1 "reset the level-script system"; SPEC-750 1.2)
//   ScriptGlue_RunFrame()           G_RunFrame, after all entities ran and after the game-rules frame hook, before the
//                                   deathmatch rule checks and the client end-of-frame updates (SPEC-750 3.1)
//   ScriptGlue_ClientBegin(ent)     ClientBegin: uses the worldspawn `script` runner with the client as activator
//                                   (observed: the level's precache script runs right after Begin, SPEC-752 vector 1)
//   ScriptGlue_ClientEndFrame(ent)  ClientEndServerFrame step 12/13 (SPEC-780 11): remote-camera refresh of SPEC-751
//                                   2.6 (camera counter < 1 -> remote view entity -1, else effects 0x4000 and the
//                                   active camera's entity/type) and the cinematic-freeze byte (SPEC-751 1.1)
//   ScriptGlue_ClientDamaged(ent)   the damage routine, when a client takes damage (SPEC-751 2.6: the camera the
//                                   client looks through is deactivated)
//   ScriptGlue_Shutdown()           ShutdownGame (SPEC-750 1.2)
//   ScriptGlue_WriteLevel/ReadLevel level section steps 4 and 5 of SPEC-752 1.2 (after the entity chunks, before the
//                                   AI chunks); ReadLevel also re-attaches every runner to its script
// Spawn functions (G18 spawn table): SP_script_runner (classname `script_runner`); SG_WorldScriptRunner is called by
// SP_worldspawn for the worldspawn `script` key (spawn-temporary keys of the world are still in `st`).
// func_remote_camera is G18's (g_sofents.cpp, SPEC-761 4 / SPEC-751 2); G6b only adds the hooks above.
//
// Cinematic state (SPEC-751 1.1, exports 0x54/0x58): the ScriptSystem holds it; `game.cinematic` (G18's exports)
// is kept equal to it: the engine's value is taken over at the start of ScriptGlue_RunFrame, the script system's
// value is written back after it. ScriptGlue_CinematicState() is the authoritative read for other packages.
//
// AI (G2, game/src/ai/ai_api.h) and helicopters/vehicles (G4, game/src/roster/roster_api.h) are called through
// their published functions; G6b sets om_ai::g_report_ai_done. Until those packages define them, weak fallbacks in
// sg_shims.cpp (MSVC /alternatename) answer "no AI". AI action records of the AI savegame chunks point at AI-done
// events through ScriptGlue_EncodeAiDone / ScriptGlue_DecodeAiDone (SPEC-752 2.1 "script event").
//
// Implements: SPEC-750 1, 3.1; SPEC-751 1, 2.6; SPEC-752 1.2 (steps 4-5), 4.4 (script link)
#pragma once

#include "../g_local.h"

namespace om_script {
class ScriptSystem;
}

// spawn functions
void SP_script_runner(edict_t *ent);
void SG_WorldScriptRunner(edict_t *ent);

// frame and level hooks
void ScriptGlue_LevelStart(void);
void ScriptGlue_RunFrame(void);
void ScriptGlue_Shutdown(void);
void ScriptGlue_ClientBegin(edict_t *ent);
void ScriptGlue_ClientEndFrame(edict_t *ent);
void ScriptGlue_ClientDamaged(edict_t *ent);

// cinematic state byte (SPEC-751 1.1)
int ScriptGlue_CinematicState(void);
void ScriptGlue_SetCinematicState(int state);

// savegames (SPEC-752 1.2 steps 4 and 5)
void ScriptGlue_WriteLevel(void);
void ScriptGlue_ReadLevel(void);
int ScriptGlue_EncodeAiDone(int done_id);
int ScriptGlue_DecodeAiDone(int code);

// the level's script system (tests, debugging)
om_script::ScriptSystem *ScriptGlue_System(void);
