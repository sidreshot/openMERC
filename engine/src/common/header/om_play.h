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

/* Archived cvar. Default on: studio logos and the campaign intro play.
   Off skips that startup reel only. */
const char *OM_IntroCvarName(void);
int OM_IntroDefaultOn(void);
int OM_IntroArchived(void);

/* 1 when showintro is off and this menu is the startup reel
   (disclaimer or the studio intro). Any other menu, including a
   cinematic inside a level, is left alone. */
int OM_IntroSkip(int showintro, const char *menu);

/* 1 when this script is the campaign opening reel. Other cinematics play. */
int OM_IntroIsStartupScript(const char *path);

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
