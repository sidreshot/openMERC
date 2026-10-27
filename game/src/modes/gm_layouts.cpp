// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): scoreboards (default, CTF, Control, Conquer with numbered teams) and the
// end-game log file.
// Implements: SPEC-790 7, Behaviour 12, Behaviour 13; SPEC-780 19 (default scoreboard)
#ifdef _WIN32
#include <direct.h>
#endif

#include "gm_internal.h"

struct gm_row {
	edict_t *ent;
	int score;
};

static int gm_minutes(edict_t *e) { return (level.framenum - e->client->resp.enterframe) / 600; }
static int gm_ping(edict_t *e) { return e->client->ping; }

// in-use clients by score descending; ties keep slot order. spectators: false = players, true = spectators
static int gm_sorted(gm_row *rows, bool spectators, int teamfilter /* -1 any, else team number */)
{
	int n = 0;
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (!e->inuse || !e->client)
			continue;
		if ((e->client->resp.spectator != 0) != spectators)
			continue;
		if (teamfilter >= 0 && e->client->resp.team != teamfilter)
			continue;
		int s = e->client->resp.score;
		int j = 0;
		while (j < n && rows[j].score >= s)
			j++;
		memmove(rows + j + 1, rows + j, sizeof(gm_row) * (n - j));
		rows[j].ent = e;
		rows[j].score = s;
		n++;
	}
	return n;
}

struct gm_teamrow {
	edict_t *first;
	int sum;
};

// teams formed by the same-team rule over the given players; sorted by sum descending
static int gm_teams(gm_row *rows, int n, gm_teamrow *teams, int max, bool sort)
{
	int nt = 0;
	for (int i = 0; i < n; i++) {
		int k = 0;
		for (; k < nt; k++)
			if (GM_SameTeam(teams[k].first, rows[i].ent))
				break;
		if (k == nt) {
			if (nt >= max)
				continue;
			teams[nt].first = rows[i].ent;
			teams[nt].sum = 0;
			nt++;
		}
		teams[k].sum += rows[i].score;
	}
	if (sort) {
		for (int i = 0; i < nt; i++)
			for (int j = i + 1; j < nt; j++)
				if (teams[j].sum > teams[i].sum) {
					gm_teamrow t = teams[i];
					teams[i] = teams[j];
					teams[j] = t;
				}
	}
	return nt;
}

static void gm_sp(edict_t *to, int pkg, int entry, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0, int f = 0,
                  int g = 0)
{
	gix.SP_Print(to, SPID(pkg, entry), a, b, c, d, e, f, g);
}

// The players that form team rows: those whose team name differs from their skin (SPEC-780 19). The player model
// writes an unusable request back as `*<actual value>` (SPEC-780 1); the original then shows no team row for
// teamname `blue` with skin mullins (capture orig_teamdm2), so the actual values are compared.
static int gm_teamed(const gm_row *players, int n, gm_row *out)
{
	int m = 0;
	for (int i = 0; i < n; i++) {
		const char *ui = players[i].ent->client->pers.userinfo;
		char tn[MAX_INFO_STRING];
		gm_strncpyz(tn, Info_ValueForKey(ui, "teamname"), sizeof(tn));
		const char *sk = Info_ValueForKey(ui, "skin");
		const char *a = tn[0] == '*' ? tn + 1 : tn;
		if (*sk == '*')
			sk++;
		if (strcmp(a, sk))
			out[m++] = players[i];
	}
	return m;
}

// SPEC-780 19
void GM_DefaultScoreboard(edict_t *ent, edict_t *killer)
{
	(void)killer;
	gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_RESET);
	int nteams = 0;
	if (GM_TeamPlay()) {
		gm_row players[MAX_CLIENTS], teamed[MAX_CLIENTS];
		int n = gm_sorted(players, false, -1);
		int m = gm_teamed(players, n, teamed);
		gm_teamrow teams[6];
		nteams = gm_teams(teamed, m, teams, 6, true);
		for (int t = 0; t < nteams; t++)
			gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_SCOREBOARD_TEAM, t < 3 ? 0 : 160, -64 + 32 * (t % 3),
			      GM_Slot(teams[t].first), teams[t].sum);
	}
	int base = nteams >= 3 ? 64 : 32;
	gm_row rows[MAX_CLIENTS];
	int n = gm_sorted(rows, false, -1);
	int row = 0;
	for (int i = 0; i < n && row < 12; i++, row++)
		gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_SCOREBAORD_CLIENT, row < 6 ? 0 : 160, base + 32 * (row % 6),
		      GM_Slot(rows[i].ent), rows[i].score, gm_ping(rows[i].ent), gm_minutes(rows[i].ent));
	n = gm_sorted(rows, true, -1);
	for (int i = 0; i < n && row < 12; i++, row++)
		gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_SCOREBAORD_SPECTATOR, row < 6 ? 0 : 160, base + 32 * (row % 6),
		      GM_Slot(rows[i].ent), gm_ping(rows[i].ent), gm_minutes(rows[i].ent));
}

// SPEC-790 B12: CTF, Control, Conquer with numbered teams and team play
void GM_TeamColumnsScoreboard(edict_t *ent)
{
	gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_RESET);
	gm_row all[MAX_CLIENTS], players[MAX_CLIENTS];
	int na = gm_sorted(all, false, -1), np = 0;
	for (int i = 0; i < na; i++)
		if (all[i].ent->client->resp.team)
			players[np++] = all[i];
	// Team rows: players grouped in client-slot order, not sorted (orig_ctf: Blue first while a Red player leads and
	// slot 0 is Blue; orig_ctf_forcejoin with all scores 0 and slot 0 Red: Red first). OPEN_QUESTIONS G9 7.
	gm_row byslot[MAX_CLIENTS];
	int nb = 0;
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *e = &g_edicts[i + 1];
		if (e->inuse && e->client && !e->client->resp.spectator && e->client->resp.team) {
			byslot[nb].ent = e;
			byslot[nb].score = e->client->resp.score;
			nb++;
		}
	}
	gm_teamrow teams[MAX_CLIENTS];
	int nt = gm_teams(byslot, nb, teams, MAX_CLIENTS, false);
	for (int t = 0; t < nt; t++) {
		int team = teams[t].first->client->resp.team;
		int x = team * 180 - 180;
		if (gm.mode == GM_CTF)
			gm_sp(ent, SP_DM_CTF, CTF_TEAM_SCORE_LAYOUT, x, -64, team - 1, teams[t].sum, GM_CTFCaptures(team));
		else if (gm.mode == GM_CONTROL)
			gm_sp(ent, SP_DM_CONTROL, CTL_TEAM_SCORE_LAYOUT, x, -64, team - 1, teams[t].sum, GM_ControlScore(team));
		else
			gm_sp(ent, SP_DM_GENERIC, DMG_CONQUER_LAYOUT_SCOREBOARD_TEAM, x, -64, team - 1, teams[t].sum);
	}
	int longest = 0;
	for (int team = 1; team <= 2; team++) {
		int rowsn = 0;
		for (int i = 0; i < np && rowsn < 12; i++) {
			edict_t *e = players[i].ent;
			if (e->client->resp.team != team)
				continue;
			int x = team == 1 ? 0 : 180, y = 32 * rowsn;
			if (gm.mode == GM_CTF)
				gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_CTF_CLIENT, x, y, GM_Slot(e), players[i].score, gm_ping(e),
				      gm_minutes(e), GM_CarriedFlag(e));
			else
				gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_SCOREBAORD_CLIENT, x, y, GM_Slot(e), players[i].score,
				      gm_ping(e), gm_minutes(e));
			rowsn++;
		}
		if (rowsn > longest)
			longest = rowsn;
	}
	if (longest < 12) {
		gm_row specs[MAX_CLIENTS];
		int ns = gm_sorted(specs, true, -1);
		for (int k = 0; k < ns && k < 12; k++) {
			int x = k < 3 ? 0 : 180;
			int kk = k < 3 ? k : k - 3;
			gm_sp(ent, SP_DM_GENERIC, DMG_LAYOUT_SCOREBAORD_SPECTATOR, x, longest * 32 + 32 * kk, GM_Slot(specs[k].ent),
			      gm_ping(specs[k].ent), gm_minutes(specs[k].ent));
		}
	}
}

static bool gm_teamcolumns(void)
{
	return gm.mode == GM_CTF || gm.mode == GM_CONTROL ||
	       (gm.mode == GM_CONQUER && GM_Flag(DF_NUMBERED_TEAMS) && GM_TeamPlay());
}

void GM_Scoreboard(edict_t *ent, edict_t *killer)
{
	if (!GM_Deathmatch() || !GM_IsClient(ent))
		return;
	if (level.intermissiontime && !gm.logwritten) {
		gm.logwritten = true;
		GM_WriteGameLog();
	}
	if (ent->client->pers.isbot)
		return;
	if (gm_teamcolumns())
		GM_TeamColumnsScoreboard(ent);
	else
		GM_DefaultScoreboard(ent, killer);
}

//-----------------------------------------------------------------------------------------------------------
// end-game log file (SPEC-790 B13)

static const char *gm_cvarstr(const char *name)
{
	cvar_t *cv = gix.cvar(name, "", 0, NULL);
	return (cv && cv->string) ? cv->string : "";
}

static void gm_mkdirs(const char *path)
{
	char buf[128];
	gm_strncpyz(buf, path, sizeof(buf));
	for (char *p = buf; *p; p++) {
		if (*p == '/' || *p == '\\') {
			char c = *p;
			*p = 0;
			if (buf[0])
				_mkdir(buf);
			*p = c;
		}
	}
}

static const char *gm_teamlabel(edict_t *first)
{
	int team = first->client->resp.team;
	if (gm.mode == GM_CTF || gm.mode == GM_CONTROL || gm.mode == GM_CONQUER) {
		if (team == 1)
			return "Blue";
		if (team == 2)
			return "Red";
	}
	return GM_TeamName(first);
}

void GM_WriteGameLog(void)
{
	const char *fname = gm_cvarstr("log_file_name");
	if (!fname[0])
		return;
	char name[64];
	gm_strncpyz(name, fname, sizeof(name));
	int counter = (int)gix.cvar_value("log_file_counter");
	if (counter != 0) {
		char base[64], ext[64];
		gm_strncpyz(base, name, sizeof(base));
		ext[0] = 0;
		char *dot = strrchr(base, '.');
		char *slash = strrchr(base, '/');
		if (dot && (!slash || dot > slash)) {
			gm_strncpyz(ext, dot, sizeof(ext));
			*dot = 0;
		}
		Com_sprintf(name, sizeof(name), "%s%d%s", base, counter, ext);
		char v[16];
		Com_sprintf(v, sizeof(v), "%d", counter + 1);
		gix.cvar_set("log_file_counter", v);
	}
	gm_mkdirs(name);
	gi.dprintf("Dumping end game log to %s\n", name);
	const char *mode = gm_cvarstr("log_file_mode");
	FILE *f = fopen(name, (mode[0] == 'o' || mode[0] == 'O') ? "w" : "a");
	if (!f) {
		gi.dprintf("ERROR: couldn't open game log-file.\n");
		return;
	}
	char L[256];
	gm_strncpyz(L, gm_cvarstr("log_file_line_header"), sizeof(L));
	int secs = (int)level.time;
	fprintf(f, "%s\n", gm_cvarstr("log_file_header"));
	fprintf(f, "%sMap Name : %s\n", L, level.mapname);
	fprintf(f, "%sHost Name : %s\n", L, gm_cvarstr("hostname"));
	fprintf(f, "%sGame type : %s\n", L, GM_GameTypeName());
	fprintf(f, "%sGame Duration : %02d:%02d:%02d\n", L, secs / 3600, (secs / 60) % 60, secs % 60);
	fprintf(f, "%s\n", L);

	gm_row rows[MAX_CLIENTS];
	int n = gm_sorted(rows, false, -1);
	bool cols = gm.mode == GM_CONTROL || gm.mode == GM_CONQUER;
	if (gm.mode == GM_CTF) {
		// CTF never writes the team lines (the team name the original tests is never filled); the players follow
		// team by team, team 1 first, each by descending score (SPEC-AMEND-006 G9.15)
		gm_row byteam[MAX_CLIENTS];
		int k = 0;
		for (int team = 1; team <= 2; team++)
			for (int i = 0; i < n; i++)
				if (rows[i].ent->client->resp.team == team)
					byteam[k++] = rows[i];
		for (int i = 0; i < n; i++)
			if (rows[i].ent->client->resp.team != 1 && rows[i].ent->client->resp.team != 2)
				byteam[k++] = rows[i];
		memcpy(rows, byteam, sizeof(gm_row) * k);
	} else if (cols || GM_TeamPlay()) {
		// Control and Conquer: the teams by team number as grouped; team play in the other modes: the default
		// scoreboard's team set (team name differs from the skin), sorted by sum
		gm_row teamed[MAX_CLIENTS];
		int m = cols ? n : gm_teamed(rows, n, teamed);
		gm_teamrow tr[MAX_CLIENTS];
		int nt = gm_teams(cols ? rows : teamed, m, tr, MAX_CLIENTS, !cols);
		for (int t = 0; t < nt; t++) {
			int team = tr[t].first->client->resp.team;
			fprintf(f, "%sTeam %s\n", L, gm_teamlabel(tr[t].first));
			if (cols) {
				fprintf(f, "%sFrags %d\n", L, tr[t].sum);
				fprintf(f, "%sControl Score %d\n", L, GM_ControlScore(team));
			} else
				fprintf(f, "%sScore %d\n", L, tr[t].sum);
			fprintf(f, "\n");
		}
	}
	const char *scorelabel = (gm.mode == GM_CONTROL || gm.mode == GM_CONQUER) ? "Frags" : "Score";
	for (int i = 0; i < n; i++) {
		edict_t *e = rows[i].ent;
		fprintf(f, "%sClient %s\n", L, e->client->pers.netname);
		fprintf(f, "%s%s %d\n", L, scorelabel, rows[i].score);
		fprintf(f, "%sPing %d\n", L, gm_ping(e));
		fprintf(f, "%sTime %d\n", L, gm_minutes(e));
		fprintf(f, "%s\n", L);
	}
	n = gm_sorted(rows, true, -1);
	for (int i = 0; i < n; i++) {
		edict_t *e = rows[i].ent;
		fprintf(f, "%sClient %s\n", L, e->client->pers.netname);
		fprintf(f, "%s spectator\n", L);
		fprintf(f, "%sPing %d\n", L, gm_ping(e));
		fprintf(f, "%sTime %d\n", L, gm_minutes(e));
		fprintf(f, "%s\n", L);
	}
	fprintf(f, "%s\n", gm_cvarstr("log_file_footer"));
	fclose(f);
}
