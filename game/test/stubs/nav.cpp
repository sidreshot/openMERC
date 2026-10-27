// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - test-only no-op stand-ins for the hooks package G5 (nav/) provides to G18's files. Compiled only when
// "nav" is listed in -DOM_GAME_SKIP=... (game/CMakeLists.txt), so that the rest of gamex86.dll can be built and
// black-box tested while that package does not compile. Never part of a shipped gamex86.dll.
// Implements: (test scaffolding, no spec)

#include "../../src/g_local.h"
#include "../../src/nav/nav_api.h"

void Nav_LevelReset(void) {}
void Nav_WorldSpawn(void) {}
void Nav_Shutdown(void) {}
int Nav_ClientCommand(edict_t *) { return 0; }
