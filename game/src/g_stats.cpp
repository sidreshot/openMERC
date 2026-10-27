// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - single-player mission statistics: the eight level counters and the level start time, their
// accumulation into the game record's mission totals, and the stat_* cvars the statistics menus show.
// Implements: SPEC-AMEND-007 G18.41, SPEC-AMEND-014 R14.5, SPEC-001 export 0x48, SPEC-780 20 (`updateinvfinal`)

#include "g_local.h"

static const char *stat_cvars[MSTAT_COUNT] = {"stat_guyskilled", "stat_friendlieskilled", "stat_throatshots",
                                              "stat_nutshots",   "stat_headshots",        "stat_gibs",
                                              "stat_savesused",  "stat_cashearned"};

// every single-player spawn (the loadout, before any other step) and after each accumulation
void Stats_LevelReset(void)
{
	memset(level.mstat, 0, sizeof(level.mstat));
	level.mstat_start = level.time;
}

void Stats_Count(int which, int n)
{
	if (which >= 0 && which < MSTAT_COUNT)
		level.mstat[which] += n;
}

// damage-code hook (g3hooks.statistic): 0 gibs, 1 head shots, 2 throat, 3 groin; the caller checks the cinematic
void Stats_DamageHook(int which)
{
	static const int map[4] = {MSTAT_GIBS, MSTAT_HEADSHOTS, MSTAT_THROATSHOTS, MSTAT_GROINSHOTS};
	if (which >= 0 && which < 4)
		Stats_Count(map[which], 1);
}

static float LevelPlayedTime(void)
{
	float t = level.time - level.mstat_start;
	return t > 0 ? t : 0;
}

// mission totals += level counters, played time += level time - start time (single player only)
void Stats_Accumulate(void)
{
	if (deathmatch->value)
		return;
	for (int i = 0; i < MSTAT_COUNT; i++)
		game.mstat_total[i] += level.mstat[i];
	game.mstat_time += LevelPlayedTime();
}

static void FormatTime(char *buf, size_t n, float t)
{
	int s = (int)t;
	snprintf(buf, n, "%d.%02d.%02d", s / 3600, (s / 60) % 60, s % 60);
}

// "level \x1f(level + total)", stat_playedtime_base level + total seconds; nothing is written when level + total time is 0
void Stats_Publish(void)
{
	float lt = LevelPlayedTime();
	if (lt + game.mstat_time == 0)
		return;
	char buf[64], a[24], b[24];
	for (int i = 0; i < MSTAT_COUNT; i++) {
		snprintf(buf, sizeof(buf), "%d \x1f(%d)", level.mstat[i], level.mstat[i] + game.mstat_total[i]);
		gi.cvar_set((char *)stat_cvars[i], buf);
	}
	FormatTime(a, sizeof(a), lt);
	FormatTime(b, sizeof(b), lt + game.mstat_time);
	snprintf(buf, sizeof(buf), "%s \x1f(%s)", a, b);
	gi.cvar_set((char *)"stat_playedtime", buf);
	// level + total, as the original shows it (mode A kos1: 45.799999 with no total yet, x1_stats.cfg)
	snprintf(buf, sizeof(buf), "%f", lt + game.mstat_time);
	gi.cvar_set((char *)"stat_playedtime_base", buf);
}

// SPEC-001 export 0x48 (engine command `getgamestats`)
void GameStats(void) { Stats_Publish(); }

// `updateinvfinal` (end-of-mission menus acc, acc2, blankscreen*): stat_moneyadd into cash, publish, save the
// inventory to the persistence cvars, accumulate, clear the level counters and the start time. With the start time 0 a
// later read counts this level's time again from the level start, as the original does (mode A kos1: 0.01.19 \x1f(0.02.35)
// after updateinvfinal at 76.5 s, x1_stats.cfg).
void Stats_UpdateInvFinal(edict_t *ent)
{
	if (deathmatch->value)
		return;
	cvar_t *add = gi.cvar((char *)"stat_moneyadd", (char *)"", 0);
	if (add && add->value) {
		level.mstat[MSTAT_CASH] += (int)add->value;
		gi.cvar_set((char *)"stat_moneyadd", (char *)"0");
	}
	Stats_Publish();
	if (ent && ent->client && ent->client->inv && ent->health > 0)
		ent->client->inv->SavePersist(true, ent->health);
	Stats_Accumulate();
	memset(level.mstat, 0, sizeof(level.mstat));
	level.mstat_start = 0;
}