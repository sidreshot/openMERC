/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 15 (game-mode objectives), 6 (Assassin rule), 11 (Realistic walking)
 * Owner: package G5 (game/src/bots). Private to bot_*.cpp; the hooks are set through bot_api.h.
 */
#ifndef OM_BOT_MODES_H
#define OM_BOT_MODES_H

#include "bot_local.h"

bool BotMode_AssassinAllows(edict_t *bot, edict_t *other);
bool BotMode_FlagWanted(edict_t *bot, edict_t *flag);
bool BotMode_IsEnemyFlag(edict_t *bot, edict_t *flag);
int BotMode_FlagTeam(edict_t *flag);
void BotMode_Objectives(Bot *b, edict_t *ent);
bool BotMode_Tired(Bot *b);

#endif
