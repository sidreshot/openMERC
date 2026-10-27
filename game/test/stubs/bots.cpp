// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - test-only no-op stand-ins for the hooks package G5 (bots/) provides to G18's files. Compiled only when
// "bots" is listed in -DOM_GAME_SKIP=... (game/CMakeLists.txt), so that the rest of gamex86.dll can be built and
// black-box tested while that package does not compile. Never part of a shipped gamex86.dll.
// Implements: (test scaffolding, no spec)

#include "../../src/g_local.h"
#include "../../src/bots/bot_api.h"

void Bot_SetModeHooks(const BotModeHooks *) {}
void Bot_WorldSpawn(void) {}
void Bot_RunFrame(void) {}
void Bot_LevelExit(void) {}
int Bot_ClientCommand(edict_t *) { return 0; }
void Bot_SlotTaken(edict_t *) {}
void Bot_SlotFreed(edict_t *) {}
int Bot_IsBot(const edict_t *) { return 0; }
const char *Bot_Userinfo(const edict_t *) { return ""; }
void Bot_SpawnSpot(edict_t *, edict_t *) {}
void Bot_Killed(edict_t *, edict_t *, int) {}
void Bot_SoundNoise(edict_t *, float, int) {}
int Bot_DropSound(edict_t *, int) { return 0; }
void Bot_Damaged(edict_t *, edict_t *) {}
void Bot_FootstepNoise(edict_t *) {}
void Bot_PlayerNoise(edict_t *, float) {}
void Bot_Explosive(edict_t *, int, int) {}
void Bot_HeardChat(edict_t *, const char *) {}
