// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - test-only no-op stand-ins for the hooks package G4 (roster/) provides to G18's files. Compiled only when
// "roster" is listed in -DOM_GAME_SKIP=... (game/CMakeLists.txt), so that the rest of gamex86.dll can be built and
// black-box tested while that package does not compile. Never part of a shipped gamex86.dll.
// Implements: (test scaffolding, no spec)

#include "../../src/g_local.h"
#include "../../src/roster/roster_api.h"

bool Roster_CallSpawn(edict_t *) { return false; }
void Roster_LevelStart(void) {}
