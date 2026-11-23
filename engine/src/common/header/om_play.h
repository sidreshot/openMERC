/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Startup reel, end-of-level title fade, and opening-cinematic spawn time.
 * No engine services. Callers pass the cvar value, the clock, and the names.
 */

#ifndef OM_PLAY_H
#define OM_PLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Archived cvar. Default on: the startup splash plays (disclaimer, then
   studio logos). Off skips that splash only. Mission cinematics always play. */
const char *OM_IntroCvarName(void);
int OM_IntroDefaultOn(void);
int OM_IntroArchived(void);

/* 1 when showintro is off and this menu is the startup splash
   (disclaimer or the studio intro). Mission menus and every other
   menu are left alone. */
int OM_IntroSkip(int showintro, const char *menu);

/* End-of-level title alpha, 0..255. age_ms and life_ms are milliseconds
   from the moment the title appeared. fade_ms is the fade-out length;
   0 selects the default evaporation. Alpha stays opaque, then falls
   smoothly to 0 at life_ms. It does not step from opaque to gone. */
#define OM_TITLE_FADE_MS 1500
int OM_TitleFadeAlpha(int age_ms, int life_ms, int fade_ms);

/* A character spawn whose time falls inside the opening cinematic
   [cine_start, cine_end) happens half a second earlier. A spawn outside
   that reel, or when opening is 0, keeps its time. */
#define OM_CINE_SPAWN_EARLY 0.5f
float OM_CineSpawnTime(float spawn_time, float cine_start, float cine_end, int opening);

#ifdef __cplusplus
}
#endif

#endif
