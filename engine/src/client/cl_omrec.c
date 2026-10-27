/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc test aid (not in the original): automatic demo and debug log.
 *
 * om_autorecord 1: every time the client enters a level (map start, level
 * change, savegame load) a demo is recorded to
 *   <user dir>/demos/auto/<YYYYMMDD>_<HHMMSS>_<map>.dm2
 * and next to it a structured log <same name>.jsonl, one JSON object per
 * line:
 *   start   map, date, engine/game module, graphics and sound cvar snapshot
 *   s       a sample (om_autorecord_hz, default 10 Hz): position, velocity,
 *           horizontal speed, on ground, ground probe normal, pm_flags,
 *           pm_time, pm_type, water level, move command, fps and the worst
 *           frame of the interval, camera shake, kick angles, health, armour
 *   hitch   a client frame longer than om_autorecord_hitch ms (default 50)
 *   print   console text (lvl err / warn / snd / info); OMsnd debug lines
 *           (s_om_debug) go only to the log, not to the console
 *   cmd     map / save / load / level-change commands
 *   dmg     health change; death
 *   end     the demo stopped (level change, quit, error, `stop`)
 * Both files are flushed once a second (and at once on errors), so a crash
 * loses at most a second; the demo reader already treats a missing end
 * marker as the end of the demo. Old sessions are deleted so at most
 * om_autorecord_keep (50) sessions and om_autorecord_maxmb (2048) MB stay.
 * tests/X1/demo_analyze.py reads the logs.
 */

#include <time.h>

#include "header/client.h"
#include "header/sof_tables.h"
#include "header/sofclient.h"
#include "../common/header/sof/engine.h" /* FS_Userdir */
#include "../common/header/omrec.h"

extern float fx_shake; /* fx/fx_system.c */

static cvar_t *om_autorecord, *om_autorecord_hz, *om_autorecord_hitch;
static cvar_t *om_autorecord_keep, *om_autorecord_maxmb, *om_autorecord_quietsnd;

static FILE *rec_log;
static qboolean rec_active;          /* a session (demo + log) is open */
static int rec_servercount = -1;     /* level already recorded */
static int rec_start_ms, rec_last_sample, rec_last_flush, rec_last_frame;
static int rec_frames, rec_worst_ms;
static int rec_health = -9999;
static int rec_pmcount;
static int rec_inhook;
static char rec_base[MAX_OSPATH];

/* ---- JSON text ---------------------------------------------------------- */

/* console text is CP1251 (Russian release font) or ASCII: written as UTF-8 */
static void
Rec_JsonString(FILE *f, const char *s, int max)
{
	int n = 0;

	fputc('"', f);

	for (; *s && n < max; s++, n++)
	{
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\')
		{
			fputc('\\', f);
			fputc(c, f);
		}
		else if (c == '\n')
		{
			if (s[1])
			{
				fputs("\\n", f);
			}
		}
		else if (c < 0x20)
		{
			/* colour and font control bytes are dropped */
		}
		else if (c < 0x80)
		{
			fputc(c, f);
		}
		else
		{
			unsigned u;

			if (c >= 0xC0)
			{
				u = 0x410 + (c - 0xC0);
			}
			else if (c == 0xA8)
			{
				u = 0x401;
			}
			else if (c == 0xB8)
			{
				u = 0x451;
			}
			else
			{
				u = c;
			}

			if (u < 0x800)
			{
				fputc(0xC0 | (u >> 6), f);
				fputc(0x80 | (u & 0x3F), f);
			}
		}
	}

	fputc('"', f);
}

static int
Rec_Time(void)
{
	return Sys_Milliseconds() - rec_start_ms;
}

static void
Rec_EventHead(const char *kind)
{
	fprintf(rec_log, "{\"t\":%d,\"k\":\"%s\"", Rec_Time(), kind);
}

/* ---- old sessions ------------------------------------------------------- */

static int
Rec_CompareNames(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static long
Rec_FileSize(const char *path)
{
	FILE *f = Q_fopen(path, "rb");
	long l;

	if (!f)
	{
		return 0;
	}

	fseek(f, 0, SEEK_END);
	l = ftell(f);
	fclose(f);
	return l < 0 ? 0 : l;
}

static void
Rec_RemoveSession(const char *demo)
{
	char log[MAX_OSPATH];
	size_t l;

	Q_strlcpy(log, demo, sizeof(log));
	l = strlen(log);

	if (l > 4)
	{
		Q_strlcpy(log + l - 4, ".jsonl", sizeof(log) - (l - 4));
	}

	Sys_Remove(demo);
	Sys_Remove(log);
}

/* keep at most om_autorecord_keep - 1 old sessions and om_autorecord_maxmb in total
   before a new one starts */
static void
Rec_Rotate(void)
{
	char find[MAX_OSPATH];
	strlist_t files;
	double total = 0, limit;
	int i, keep, first = 0;

	Com_sprintf(find, sizeof(find), "%s/demos/auto/*.dm2", FS_Userdir());
	files = FS_ListFiles(find, 0, SFF_SUBDIR);

	if (files.num <= 0)
	{
		StrList_Free(&files);
		return;
	}

	qsort(files.data, files.num, sizeof(char *), Rec_CompareNames);

	keep = (int)om_autorecord_keep->value;

	if (keep < 1)
	{
		keep = 1;
	}

	if (files.num > keep - 1)
	{
		first = files.num - (keep - 1);
	}

	for (i = 0; i < first; i++)
	{
		Rec_RemoveSession(files.data[i]);
	}

	/* newest first until the size limit */
	limit = om_autorecord_maxmb->value * 1024.0 * 1024.0;

	for (i = files.num - 1; i >= first; i--)
	{
		char log[MAX_OSPATH];
		size_t l;

		Q_strlcpy(log, files.data[i], sizeof(log));
		l = strlen(log);
		Q_strlcpy(log + l - 4, ".jsonl", sizeof(log) - (l - 4));
		total += Rec_FileSize(files.data[i]) + Rec_FileSize(log);

		if ((limit > 0) && (total > limit))
		{
			Rec_RemoveSession(files.data[i]);
		}
	}

	StrList_Free(&files);
}

/* ---- session ------------------------------------------------------------ */

static const char *
Rec_MapName(void)
{
	static char name[64];
	const char *m = SV_OmRec_MapName();
	int i;

	if (!m[0])
	{
		/* a network game: the world model configstring */
		for (i = 0; i < 64 && !m[0]; i++)
		{
			const char *s = CL_SoF_ConfigString(i);

			if (s && !Q_strncasecmp(s, "maps/", 5))
			{
				m = s + 5;
			}
		}
	}

	if (!m[0])
	{
		m = "level";
	}

	for (i = 0; m[i] && (i < (int)sizeof(name) - 1); i++)
	{
		char c = m[i];

		if (c == '.')
		{
			break;
		}

		name[i] = ((c >= 'A') && (c <= 'Z')) ? (char)(c + 32) :
			((((c >= 'a') && (c <= 'z')) || ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-')) ? c : '_');
	}

	name[i] = 0;
	return name;
}

static void
Rec_SnapshotCvars(void)
{
	static const char *const prefixes[] = {"r_", "gl_", "ghl_", "vid_", "s_", "snd_", "om_", NULL};
	static const char *const names[] = {"fov", "horplus", "scr_aspect", "busywait", "cl_maxfps",
		"version", "game", "deathmatch", "coop", "skill", "maxclients", "timescale", "developer",
		"sv_violence", "cl_violence", "menu_layout", NULL};
	cvar_t *v;
	int i, n = 0;

	fputs(",\"cvars\":{", rec_log);

	for (v = cvar_vars; v; v = v->next)
	{
		qboolean want = false;

		for (i = 0; prefixes[i] && !want; i++)
		{
			want = !Q_strncasecmp(v->name, prefixes[i], (int)strlen(prefixes[i]));
		}

		for (i = 0; names[i] && !want; i++)
		{
			want = !Q_strcasecmp(v->name, names[i]);
		}

		if (!want)
		{
			continue;
		}

		fprintf(rec_log, "%s", n++ ? "," : "");
		Rec_JsonString(rec_log, v->name, 64);
		fputc(':', rec_log);
		Rec_JsonString(rec_log, v->string, 128);
	}

	fputc('}', rec_log);
}

static void
Rec_Start(void)
{
	char path[MAX_OSPATH], date[32];
	time_t now = time(NULL);
	struct tm *lt = localtime(&now);

	Rec_Rotate();

	strftime(date, sizeof(date), "%Y%m%d_%H%M%S", lt);
	Com_sprintf(rec_base, sizeof(rec_base), "%s_%s", date, Rec_MapName());

	/* the demo: the normal recorder (cl_demo.c) */
	rec_inhook++;
	Cmd_ExecuteString(va("record auto/%s", rec_base));
	rec_inhook--;

	rec_servercount = cl.servercount;

	if (!cls.demorecording)
	{
		return; /* recorder refused; try again on the next level */
	}

	Com_sprintf(path, sizeof(path), "%s/demos/auto/%s.jsonl", FS_Userdir(), rec_base);
	rec_log = Q_fopen(path, "wb");
	rec_active = true;
	rec_start_ms = Sys_Milliseconds();
	rec_last_sample = rec_last_flush = rec_start_ms;
	rec_last_frame = 0;
	rec_frames = 0;
	rec_worst_ms = 0;
	rec_health = -9999;
	rec_pmcount = omrec_pm.count;

	if (rec_log)
	{
		strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", lt);
		Rec_EventHead("start");
		fputs(",\"map\":", rec_log);
		Rec_JsonString(rec_log, Rec_MapName(), 64);
		fputs(",\"date\":", rec_log);
		Rec_JsonString(rec_log, date, 32);
		fputs(",\"demo\":", rec_log);
		Rec_JsonString(rec_log, va("demos/auto/%s.dm2", rec_base), 200);
		fprintf(rec_log, ",\"servercount\":%d,\"local_server\":%d,\"hz\":%g", cl.servercount,
				SV_OmRec_MapName()[0] != 0, om_autorecord_hz->value);
		fputs(",\"server\":", rec_log);
		Rec_JsonString(rec_log, cls.servername, 64);
		Rec_SnapshotCvars();
		fputs("}\n", rec_log);
		fflush(rec_log);
	}

	Com_Printf("om_autorecord: demos/auto/%s.dm2 + .jsonl\n", rec_base);
}

static void
Rec_End(const char *reason)
{
	if (!rec_active)
	{
		return;
	}

	rec_active = false;

	if (rec_log)
	{
		Rec_EventHead("end");
		fputs(",\"reason\":", rec_log);
		Rec_JsonString(rec_log, reason, 64);
		fputs("}\n", rec_log);
		fclose(rec_log);
		rec_log = NULL;
	}
}

/* ---- hooks -------------------------------------------------------------- */

static int
Rec_PrintHook(const char *msg)
{
	const char *lvl = "info";
	qboolean quiet = false;

	if (!rec_active || !rec_log || rec_inhook)
	{
		return 0;
	}

	rec_inhook++;

	if (!strncmp(msg, "OMsnd:", 6))
	{
		lvl = "snd";
		quiet = om_autorecord_quietsnd->value && (strstr(msg, "[p=") != NULL);
	}
	else if (strstr(msg, "ERROR") || strstr(msg, "Error") || strstr(msg, "error") || strstr(msg, "crashed"))
	{
		lvl = "err";
	}
	else if (strstr(msg, "WARNING") || strstr(msg, "Warning") || strstr(msg, "not found") ||
			strstr(msg, "Couldn't") || strstr(msg, "couldn't"))
	{
		lvl = "warn";
	}

	if (msg[0] && strcmp(msg, "\n"))
	{
		Rec_EventHead("print");
		fprintf(rec_log, ",\"lvl\":\"%s\",\"msg\":", lvl);
		Rec_JsonString(rec_log, msg, 400);
		fputs("}\n", rec_log);

		if (!strcmp(lvl, "err"))
		{
			fflush(rec_log);

			if (cls.demofile)
			{
				fflush(cls.demofile);
			}
		}
	}

	rec_inhook--;
	return quiet;
}

static void
Rec_CmdHook(const char *text)
{
	static const char *const watched[] = {"map", "gamemap", "changelevel", "load", "loadgame", "save",
		"savegame", "newgame", "demomap", "killserver", "disconnect", "quit", "restart", "record",
		"stop", "vid_restart", "snd_restart", "sv_setpos", NULL};
	const char *a0 = Cmd_Argv(0);
	int i;

	if (!rec_active || !rec_log || rec_inhook)
	{
		return;
	}

	for (i = 0; watched[i]; i++)
	{
		if (!Q_strcasecmp(a0, watched[i]))
		{
			Rec_EventHead("cmd");
			fputs(",\"text\":", rec_log);
			Rec_JsonString(rec_log, text, 200);
			fputs("}\n", rec_log);
			fflush(rec_log);
			return;
		}
	}
}

/* the demo is about to stop: CL_Stop_f, disconnect, level change */
void
CL_OmRec_DemoStopping(const char *reason)
{
	Rec_End(reason);
}

/* ---- per frame ---------------------------------------------------------- */

static void
Rec_Sample(int now)
{
	const sofps_t *sps = CL_SoF_CurrentPS();
	const player_state_t *ps = &cl.frame.playerstate;
	const omrec_pm_t *pm = &omrec_pm;
	float dt = (now - rec_last_sample) / 1000.0f;
	float hspeed;
	qboolean fresh = pm->count != rec_pmcount;

	hspeed = sqrtf(pm->velocity[0] * pm->velocity[0] + pm->velocity[1] * pm->velocity[1]);

	Rec_EventHead("s");

	if (fresh)
	{
		fprintf(rec_log, ",\"pos\":[%.1f,%.1f,%.1f],\"vel\":[%.1f,%.1f,%.1f],\"spd\":%.1f,\"ong\":%d,"
				"\"gn\":[%.3f,%.3f,%.3f],\"pmf\":%d,\"pmt\":%d,\"pmtype\":%d,\"wl\":%d,"
				"\"cmd\":[%d,%d,%d],\"btn\":%d,\"msec\":%d,\"moves\":%d,\"stagger\":%.2f",
				pm->origin[0], pm->origin[1], pm->origin[2], pm->velocity[0], pm->velocity[1],
				pm->velocity[2], hspeed, pm->onground, pm->probenormal[0], pm->probenormal[1],
				pm->probenormal[2], pm->pm_flags, pm->pm_time, pm->pm_type, pm->waterlevel,
				pm->forwardmove, pm->sidemove, pm->upmove, pm->buttons, pm->msec,
				pm->count - rec_pmcount, pm->stagger);
	}
	else
	{
		/* no local server move (network game, paused, cinematic): the client's view of the state */
		fprintf(rec_log, ",\"pos\":[%.1f,%.1f,%.1f],\"vel\":[%.1f,%.1f,%.1f],\"pmf\":%d,\"pmt\":%d,"
				"\"moves\":0", ps->pmove.origin[0] * 0.125f, ps->pmove.origin[1] * 0.125f,
				ps->pmove.origin[2] * 0.125f, ps->pmove.velocity[0] * 0.125f,
				ps->pmove.velocity[1] * 0.125f, ps->pmove.velocity[2] * 0.125f, ps->pmove.pm_flags,
				ps->pmove.pm_time);
	}

	fprintf(rec_log, ",\"fps\":%.1f,\"fmax\":%d,\"shake\":%.1f,\"kick\":[%.2f,%.2f,%.2f],"
			"\"view\":[%.2f,%.2f,%.2f],\"hp\":%d,\"ar\":%d,\"paused\":%d}\n",
			dt > 0 ? rec_frames / dt : 0.0f, rec_worst_ms, fx_shake, ps->kick_angles[0],
			ps->kick_angles[1], ps->kick_angles[2], cl.refdef.viewangles[0], cl.refdef.viewangles[1],
			cl.refdef.viewangles[2], sps->stats[1], sps->stats[5],
			(cl_paused && cl_paused->value) ? 1 : 0);

	rec_pmcount = pm->count;
	rec_frames = 0;
	rec_worst_ms = 0;
	rec_last_sample = now;
}

void
CL_OmRec_Frame(void)
{
	int now, ft, hz;

	if (!om_autorecord)
	{
		return;
	}

	/* the demo was stopped (level change, `stop`, disconnect) */
	if (rec_active && !cls.demorecording)
	{
		Rec_End("demo stopped");
	}

	if (!om_autorecord->value || CL_Demo_Playing())
	{
		if (rec_active && cls.demorecording)
		{
			Cmd_ExecuteString("stop");
		}

		return;
	}

	if (!rec_active)
	{
		if ((cls.state == ca_active) && cl.refresh_prepped && cl.frame.valid && !cls.demorecording &&
			!cls.disable_screen && (cl.servercount != rec_servercount) && CL_SoF_Active())
		{
			Rec_Start();
		}

		return;
	}

	/* a savegame load (or a server restart) sends new serverdata while the demo keeps recording: end this
	   session here and start a new demo + log for the reloaded level on a following frame */
	if (cl.servercount != rec_servercount)
	{
		Rec_End("level reloaded");
		Cmd_ExecuteString("stop");
		return;
	}

	if ((cls.state != ca_active) || !cl.refresh_prepped)
	{
		return; /* loading: no samples, no health changes */
	}

	if (!rec_log)
	{
		return;
	}

	now = Sys_Milliseconds();
	ft = rec_last_frame ? now - rec_last_frame : 0;
	rec_last_frame = now;
	rec_frames++;

	if (ft > rec_worst_ms)
	{
		rec_worst_ms = ft;
	}

	if ((ft > om_autorecord_hitch->value) && (cls.state == ca_active) && !cls.disable_screen)
	{
		Rec_EventHead("hitch");
		fprintf(rec_log, ",\"ms\":%d}\n", ft);
	}

	/* health */
	{
		int hp = CL_SoF_CurrentPS()->stats[1];

		if ((rec_health != -9999) && (hp != rec_health))
		{
			Rec_EventHead(((hp <= 0) && (rec_health > 0)) ? "death" : "dmg");
			fprintf(rec_log, ",\"from\":%d,\"to\":%d}\n", rec_health, hp);
		}

		rec_health = hp;
	}

	hz = (int)om_autorecord_hz->value;

	if (hz < 1)
	{
		hz = 1;
	}

	if (now - rec_last_sample >= 1000 / hz)
	{
		Rec_Sample(now);
	}

	if (now - rec_last_flush >= 1000)
	{
		fflush(rec_log);

		if (cls.demofile)
		{
			fflush(cls.demofile);
		}

		rec_last_flush = now;
	}
}

/* quit / fatal error */
void
CL_OmRec_Shutdown(void)
{
	if (rec_active && cls.demorecording)
	{
		Rec_End("shutdown");
		rec_inhook++;
		CL_Stop_f();
		rec_inhook--;
	}

	Rec_End("shutdown");
	com_printhook = NULL;
	com_cmdhook = NULL;
}

void
CL_OmRec_Init(void)
{
	om_autorecord = Cvar_Get("om_autorecord", "0", CVAR_ARCHIVE);
	om_autorecord_hz = Cvar_Get("om_autorecord_hz", "10", CVAR_ARCHIVE);
	om_autorecord_hitch = Cvar_Get("om_autorecord_hitch", "50", CVAR_ARCHIVE);
	om_autorecord_keep = Cvar_Get("om_autorecord_keep", "50", CVAR_ARCHIVE);
	om_autorecord_maxmb = Cvar_Get("om_autorecord_maxmb", "2048", CVAR_ARCHIVE);
	om_autorecord_quietsnd = Cvar_Get("om_autorecord_quietsnd", "1", CVAR_ARCHIVE);
	com_printhook = Rec_PrintHook;
	com_cmdhook = Rec_CmdHook;
}

/* crash report (backends/windows/crash.c, Com_CrashFlush): the JSONL ends with
   an "end" event naming the crash, the demo is flushed */
void
CL_OmRec_Crash(const char *reason)
{
	Rec_End(reason);

	if (cls.demofile)
	{
		fflush(cls.demofile);
	}
}
