// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G7 — ambient sound sets and music (public interface).
//
// The game-side copy of `sound/sound.ams` (SPEC-112 layout) loaded at every level spawn, the three group lookups,
// and the music value rules of SPEC-771 B.5. Users:
//  - G18 movers / spawners (SPEC-760 B5.1): MoverSound(soundName, k) for k = 0 start, 1 moving, 2 stop.
//  - G18 player client frame (SPEC-780 §6): MusicAuto(player) at the start of each client frame.
//  - G3 damage (SPEC-720 §4) / G2 characters (SPEC-722, SPEC-782): MusicDanger(player).
//  - G2: set g_angry_count to report the characters within 600 units of a player that are alive and show the
//    angry face (expression state 3); without it the automatic rule only sees the player's health.
//
// Implements: SPEC-771 B.1-B.5
#pragma once

#include "../g_local.h"

namespace ambient {

void LevelReset(void);
void LevelSpawned(void); // load sound/sound.ams and precache the sets named by entities in use

int AreaSetIndex(const char *name);       // group 0 (general), -1 = not found
int PointSetIndex(const char *name);      // group 1 (local), -1 = not found
int MoverSound(const char *name, int k);  // group 2: sound index of listed wave k, -1 = unset/unknown

// music value (player-state field 28)
void MusicAuto(edict_t *player);
void MusicDanger(edict_t *player);
void MusicSet(edict_t *player, int value);
typedef int (*angry_count_fn)(edict_t *player);
extern angry_count_fn g_angry_count;

} // namespace ambient
