// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - test-only no-op stand-ins for the hooks package G2 (ai/) provides to G18's files. Compiled only
// when "ai" is listed in -DOM_GAME_SKIP=... (game/CMakeLists.txt), so that the rest of gamex86.dll can be built and
// black-box tested while that package does not compile. Never part of a shipped gamex86.dll.
// Implements: (test scaffolding, no spec)

#include "../../src/g_local.h"
#include "../../src/ai/ai_api.h"

namespace om_ai {
void PathCornerSpawn(edict_t *) {}
void LevelStart(void) {}
void FrameBegin(void) {}
void EntityGroundLost(edict_t *) {}
}
