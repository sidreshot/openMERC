/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Client / Server interactions
 *
 * =======================================================================
 *
 * OpenMerc: Soldier of Fortune console printing and errors - 8192-byte
 * print buffer, developer bit 1 for Com_DPrintf, <user>/sof.log in append
 * mode without control characters (logfile 1 keeps it open, 2+ reopens it
 * for every print), every error framed by '*' lines and deleting
 * <user>/save/current.sof, error level 3 ("won error").
 * Implements: SPEC-301 (Behaviour 10, 12), SPEC-AMEND-001 A (Com_Printf,
 *             Com_DPrintf, Com_Error), SPEC-303 (current.sof on errors)
 *
 * =======================================================================
 */

#include "header/common.h"
#include "header/omrec.h"
#include <stdlib.h>
#include <setjmp.h>

#define MAXPRINTMSG 8192

FILE *logfile;
cvar_t *logfile_active;  /* 1 = buffer log, 2 = flush after each print */
jmp_buf abortframe; /* an ERR_DROP occured, exit the entire frame */
int server_state;
cvar_t *color_terminal;
char com_errortext[MAXPRINTMSG]; /* last error, for "Error during initialization" */
static qboolean com_inlog;       /* inside the console-log code */

static int rd_target;
static char *rd_buffer;
static int rd_buffersize;
static void (*rd_flush)(int target, char *buffer);

void
Com_BeginRedirect(int target, char *buffer, int buffersize, void (*flush)(int, char *))
{
	if (!target || !buffer || !buffersize || !flush)
	{
		return;
	}

	rd_target = target;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

void
Com_EndRedirect(void)
{
	rd_flush(rd_target, rd_buffer);

	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

/*
 * Both client and server can use this, and it will output
 * to the apropriate place.
 */
int (*com_printhook)(const char *msg); /* OpenMerc om_autorecord (header/omrec.h) */

void
Com_VPrintf(int print_level, const char *fmt, va_list argptr)
{
	int i, msgLen;
	char msg[MAXPRINTMSG];
	char term[MAXPRINTMSG];

	if ((print_level == PRINT_DEVELOPER) && !Com_DeveloperBit(1))
	{
		return; /* don't confuse non-developers with techie stuff... */
	}

	msgLen = vsnprintf(msg, MAXPRINTMSG, fmt, argptr);

	if (msgLen >= MAXPRINTMSG || msgLen < 0)
	{
		/* SoF overflows its buffer here; OpenMerc truncates */
		msgLen = MAXPRINTMSG - 1;
		msg[msgLen] = '\0';
	}

	if (rd_target)
	{
		if ((msgLen + strlen(rd_buffer)) > (rd_buffersize - 1))
		{
			rd_flush(rd_target, rd_buffer);
			*rd_buffer = 0;
		}

		strcat(rd_buffer, msg);
		return;
	}

#ifndef DEDICATED_ONLY
	if (!com_printhook || !com_printhook(msg))
	{
		Con_Print(msg);
	}
#endif

	/* terminal copy: line characters mapped, colour bytes dropped */
	for (i = 0, msgLen = 0; msg[i]; i++)
	{
		char c = msg[i];

		if ((c > 0) && (c < ' ') && (c != '\n') && (c != '\r') && (c != '\t'))
		{
			if ((c == 0x1D) || (c == 0x1F))
			{
				term[msgLen++] = '-';
			}
			else if (c == 0x1E)
			{
				term[msgLen++] = '=';
			}

			continue;
		}

		term[msgLen++] = c;
	}

	term[msgLen] = 0;

	/* also echo to debugging console */
	Sys_ConsoleOutput(term);

	/* logfile: <user>/sof.log, appended, without the control bytes
	   1..31 except line feed and carriage return (SPEC-301 B10) */
	if (logfile_active && logfile_active->value && !com_inlog)
	{
		char name[MAX_OSPATH];
		int j, k;

		com_inlog = true; /* prints while opening the log are not logged */

		for (j = 0, k = 0; msg[j]; j++)
		{
			unsigned char c = (unsigned char)msg[j];

			if ((c >= 1) && (c <= 31) && (c != '\n') && (c != '\r'))
			{
				continue;
			}

			term[k++] = (char)c;
		}

		term[k] = 0;

		if (!logfile)
		{
			const char *udir = FS_Userdir();

			Com_sprintf(name, sizeof(name), "%s/sof.log",
					(udir && udir[0]) ? udir : ".");
			FS_CreatePath(name);
			logfile = Q_fopen(name, "a");
		}

		if (logfile)
		{
			fprintf(logfile, "%s", term);

			if (logfile_active->value >= 2)
			{
				fclose(logfile);
				logfile = NULL;
			}
			else
			{
				fflush(logfile);
			}
		}

		com_inlog = false;
	}
	else if (logfile)
	{
		fclose(logfile);
		logfile = NULL;
	}
}

/*
 * Both client and server can use this, and it will output
 * to the apropriate place.
 */
void
Com_Printf(const char *fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	Com_VPrintf(PRINT_ALL, fmt, argptr);
	va_end(argptr);
}

/*
 * A Com_Printf that only shows up if the "developer" cvar has bit 1
 */
void
Com_DPrintf(const char *fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	Com_VPrintf(PRINT_DEVELOPER, fmt, argptr);
	va_end(argptr);
}

/*
 * A Com_Printf that only shows up when either the "modder" or "developer"
 * cvars is set
 */
void
Com_MDPrintf(const char *fmt, ...)
{
	va_list argptr;
	char msg[MAXPRINTMSG];

	if ((!modder || !modder->value) && (!developer || !developer->value))
	{
		return;
	}

	va_start(argptr, fmt);
	vsnprintf(msg, MAXPRINTMSG, fmt, argptr);
	va_end(argptr);

	Com_Printf("%s", msg);
}

/*
 * Both client and server can use this, and it will
 * do the apropriate things.
 *
 * Levels: 0 fatal, 1 drop, 2 disconnect, 3 "won error" (handled as drop).
 */
void
Com_Error(int code, const char *fmt, ...)
{
	va_list argptr;
	static char msg[MAXPRINTMSG];
	static qboolean recursive;

	if (recursive)
	{
		Sys_Error("Recursive Com_Error after: %s", msg);
	}

	recursive = true;

	va_start(argptr, fmt);
	vsnprintf(msg, MAXPRINTMSG, fmt, argptr);
	va_end(argptr);

	Q_strlcpy(com_errortext, msg, sizeof(com_errortext));

	/* every level: the framed message, current.sof deleted */
	Com_Printf("********************\nERROR: %s\n********************\n", msg);

	{
		char cur[MAX_OSPATH];
		const char *udir = FS_Userdir();

		SG_Close();

		if (udir && udir[0])
		{
			Com_sprintf(cur, sizeof(cur), "%s/save/current.sof", udir);
			Sys_Remove(cur);
		}
	}

	if (code == ERR_DISCONNECT)
	{
#ifndef DEDICATED_ONLY
		CL_Drop();
#endif
		recursive = false;
		YQ2_LONGJMP(abortframe, 1);
	}
	else if ((code == ERR_DROP) || (code == 3))
	{
		SV_Shutdown(va("Server crashed: %s\n", msg), false);
#ifndef DEDICATED_ONLY
		CL_Drop();
#endif
		recursive = false;
		YQ2_LONGJMP(abortframe, 1);
	}
	else
	{
		SV_Shutdown(va("Server fatal crashed: %s\n", msg), false);
#ifndef DEDICATED_ONLY
		CL_Shutdown();
#endif
	}

	if (logfile)
	{
		fclose(logfile);
		logfile = NULL;
	}

	Sys_Error("%s", msg);
	recursive = false;
}

/*
 * Both client and server can use this, and it will
 * do the apropriate things.
 */
void
Com_Quit(void)
{
	Com_Printf("\n----------- shutting down ----------\n");
	SV_Shutdown("Server quit\n", false);
	Sys_Quit();
}

int
Com_ServerState(void)
{
	return server_state;
}

void
Com_SetServerState(int state)
{
	server_state = state;
}

/* stringlist_t API */

void
StringList_Free(stringlist_t *sl)
{
	char **cs, **end;

	cs = sl->lst;

	if (cs)
	{
		end = &sl->lst[sl->cap];

		for (; cs < end; cs++)
		{
			if (*cs)
			{
				Z_Free(*cs);
			}
		}

		Z_Free(sl->lst);
	}

	sl->lst = NULL;
	sl->n = 0;
	sl->cap = 0;
}

int
StringList_Find(const stringlist_t *sl, const char *s)
{
	char **cs, **end;

	cs = sl->lst;

	if (!cs)
	{
		return sl->cap + 1;
	}

	end = &cs[sl->cap];

	for (; cs < end; cs++)
	{
		if ((!s && !(*cs)) || (s && *cs && !strcmp(*cs, s)))
		{
			return cs - sl->lst;
		}
	}

	return sl->cap + 1;
}

qboolean
StringList_IsInList(const stringlist_t *sl, const char *s)
{
	return (StringList_Find(sl, s) < sl->cap) ? true : false;
}

void
StringList_Add(stringlist_t *sl, const char *s)
{
	char **cs;
	int nf, n;

	nf = StringList_Find(sl, NULL);

	if (nf < sl->cap)
	{
		cs = &sl->lst[nf];
	}
	else
	{
		n = NextPow2gt(sl->cap);
		sl->lst = Z_Realloc(sl->lst, n * sizeof(*sl->lst));

		cs = &sl->lst[sl->cap];

		sl->cap = n;
	}

	sl->n++;

	*cs = Z_Malloc(strlen(s) + 1);
	strcpy(*cs, s);
}

/* OpenMerc crash report (backends/windows/crash.c): what is buffered goes to
   disk before the process ends - the om_autorecord JSONL gets its "end" event */
#ifndef DEDICATED_ONLY
void CL_OmRec_Crash(const char *reason);
#endif

void
Com_CrashFlush(const char *reason)
{
#ifndef DEDICATED_ONLY
	CL_OmRec_Crash(reason);
#endif

	if (logfile)
	{
		fflush(logfile);
	}

	fflush(stdout);
	fflush(stderr);
}
