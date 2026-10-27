// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G6b - weak fallbacks for the AI (G2) and roster (G4) functions the script host
// calls, so gamex86 links while those packages are not in yet. MSVC x86 only: /alternatename makes the linker use
// a fallback only when the real symbol is not defined anywhere; as soon as G2 or G4 define theirs, those win.
// The fallbacks answer "this entity has no AI" (SPEC-750 12.2/12.8: the commands then do nothing, a HELI/VEHICLE
// done event of an entity without AI fires at once, SPEC-AMEND-003 6).
// Implements: SPEC-750 12 (behaviour without an AI)

#include "../g_local.h"

namespace om_script {
struct AiCommand;
}

extern "C" {
bool sg_fb_HasAI(edict_t *) { return false; }
void sg_fb_ScriptCommand(edict_t *, const om_script::AiCommand &) {}
void sg_fb_ScriptReset(edict_t *) {}
void sg_fb_ClearCallForHelp(edict_t *) {}
void sg_fb_CinematicEndActors(void) {}
void (*sg_fb_report_ai_done)(int) = 0;
bool sg_fb_Roster_HasAI(edict_t *) { return false; }
int sg_fb_Roster_Command(edict_t *, int, edict_t *, const float *const, float) { return -2; }
bool sg_fb_Roster_ActionDone(edict_t *, int) { return true; }
}

#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/alternatename:?HasAI@om_ai@@YA_NPAUedict_s@@@Z=_sg_fb_HasAI")
#pragma comment(linker, "/alternatename:?ScriptCommand@om_ai@@YAXPAUedict_s@@ABUAiCommand@om_script@@@Z=_sg_fb_ScriptCommand")
#pragma comment(linker, "/alternatename:?ScriptReset@om_ai@@YAXPAUedict_s@@@Z=_sg_fb_ScriptReset")
#pragma comment(linker, "/alternatename:?ClearCallForHelp@om_ai@@YAXPAUedict_s@@@Z=_sg_fb_ClearCallForHelp")
#pragma comment(linker, "/alternatename:?CinematicEndActors@om_ai@@YAXXZ=_sg_fb_CinematicEndActors")
#pragma comment(linker, "/alternatename:?g_report_ai_done@om_ai@@3P6AXH@ZA=_sg_fb_report_ai_done")
#pragma comment(linker, "/alternatename:?Roster_HasHeliAI@@YA_NPAUedict_s@@@Z=_sg_fb_Roster_HasAI")
#pragma comment(linker, "/alternatename:?Roster_HasVehicleAI@@YA_NPAUedict_s@@@Z=_sg_fb_Roster_HasAI")
#pragma comment(linker, "/alternatename:?Roster_HeliCommand@@YAHPAUedict_s@@H0QBMM@Z=_sg_fb_Roster_Command")
#pragma comment(linker, "/alternatename:?Roster_VehicleCommand@@YAHPAUedict_s@@H0QBMM@Z=_sg_fb_Roster_Command")
#pragma comment(linker, "/alternatename:?Roster_VehicleActionDone@@YA_NPAUedict_s@@H@Z=_sg_fb_Roster_ActionDone")
#endif
