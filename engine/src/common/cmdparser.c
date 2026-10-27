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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * This file implements the Quake II command processor. Every command
 * which is send via the command line at startup, via the console and
 * via rcon is processed here and send to the apropriate subsystem.
 *
 * =======================================================================
 *
 * OpenMerc: Soldier of Fortune 1.07f command semantics - 8192-byte command
 * buffer, "wait" waits one frame, exec appends ".cfg", $name and #name
 * macro expansion, 1023-character lines, at most 80 arguments, commands
 * listed newest first, the SoF error texts, and the "cpuid" command.
 * Yamagi's "vstr" command is removed.
 * Implements: SPEC-302 (Behaviour 3, 4, 5, 6, 8), SPEC-301 (+ commands)
 *
 * =======================================================================
 */

#include <ctype.h>

#include "header/common.h"
#include "header/omrec.h"

void (*com_cmdhook)(const char *text);

#define MAX_ALIAS_NAME 32
#define ALIAS_LOOP_COUNT 16
/* SPEC-302 "Command buffer" is 8192 bytes; OpenMerc deviates (docs/DEVIATIONS.md):
   the config.cfg the engine writes itself passes 8 KB with a full set of binds and
   cvars, and an exec of a file that does not fit was dropped whole ("Cbuf_AddText:
   overflow", every saved setting lost) */
#define CBUF_SIZE 32768
#define CMD_MAX_LINE 1024     /* lines of 1024+ characters are discarded */
#define CMD_MAX_ARGS 80
#define CMD_MAX_MACROS 100

typedef struct cmd_function_s
{
	struct cmd_function_s *next;
	char *name;
	xcommand_t function;
} cmd_function_t;

static cmd_function_t *cmd_functions; /* possible commands to execute, newest first */

typedef struct cmdalias_s
{
	struct cmdalias_s *next;
	char name[MAX_ALIAS_NAME];
	char *value;
} cmdalias_t;

char retval[256];
int alias_count; /* for detecting runaway loops */
cmdalias_t *cmd_alias;
static qboolean cmd_wait;
static int cmd_argc;
static char *cmd_argv[CMD_MAX_ARGS];
static char *cmd_null_string = "";
static char cmd_args[MAX_STRING_CHARS];
sizebuf_t cmd_text;
byte cmd_text_buf[CBUF_SIZE];
char defer_text_buf[CBUF_SIZE + 1];

/*
 * Causes execution of the remainder of the command buffer to be delayed
 * until next frame.  This allows commands like: bind g "impulse 5 ;
 * +attack ; wait ; -attack ; impulse 2"
 */
static void
Cmd_Wait_f(void)
{
	cmd_wait = true;
}

void
Cbuf_Init(void)
{
	SZ_Init(&cmd_text, cmd_text_buf, sizeof(cmd_text_buf));
}

/*
 * Adds command text at the end of the buffer
 */
void
Cbuf_AddText(const char *text)
{
	size_t l;

	l = strlen(text);

	if (cmd_text.cursize + l >= cmd_text.maxsize)
	{
		Com_Printf("Cbuf_AddText: overflow\n");
		return;
	}

	SZ_Write(&cmd_text, text, (int)l);
}

/*
 * Adds command text immediately after the current command
 * Adds a \n to the text
 */
void
Cbuf_InsertText(char *text)
{
	char *temp;
	int templen;

	/* copy off any commands still remaining in the exec buffer */
	templen = cmd_text.cursize;

	if (templen)
	{
		temp = Z_Malloc(templen);
		memcpy(temp, cmd_text.data, templen);
		SZ_Clear(&cmd_text);
	}
	else
	{
		temp = NULL;
	}

	/* add the entire text of the file */
	Cbuf_AddText(text);

	/* add the copied off data */
	if (templen)
	{
		if (cmd_text.cursize + templen >= cmd_text.maxsize)
		{
			Com_Printf("Cbuf_AddText: overflow\n");
		}
		else
		{
			SZ_Write(&cmd_text, temp, templen);
		}

		Z_Free(temp);
	}
}

void
Cbuf_CopyToDefer(void)
{
	memcpy(defer_text_buf, cmd_text_buf, cmd_text.cursize);
	defer_text_buf[cmd_text.cursize] = 0;
	cmd_text.cursize = 0;
}

void
Cbuf_InsertFromDefer(void)
{
	Cbuf_InsertText(defer_text_buf);
	defer_text_buf[0] = 0;
}

/*
 * Immediate execution requested by a module: the line runs, then everything
 * that line queued, without touching text that was already waiting
 * (SPEC-302 Behaviour 3).
 */
static void
Cbuf_ExecuteNow(char *text)
{
	char *saved;
	int savedlen;

	savedlen = cmd_text.cursize;
	saved = NULL;

	if (savedlen)
	{
		saved = Z_Malloc(savedlen);
		memcpy(saved, cmd_text.data, savedlen);
		SZ_Clear(&cmd_text);
	}

	Cmd_ExecuteString(text);
	Cbuf_Execute();

	if (saved)
	{
		if (cmd_text.cursize + savedlen < cmd_text.maxsize)
		{
			SZ_Write(&cmd_text, saved, savedlen);
		}
		else
		{
			Com_Printf("Cbuf_AddText: overflow\n");
		}

		Z_Free(saved);
	}
}

void
Cbuf_ExecuteText(int exec_when, char *text)
{
	switch (exec_when)
	{
		case EXEC_NOW:
			Cbuf_ExecuteNow(text);
			break;
		case EXEC_INSERT:
			Cbuf_InsertText(text);
			break;
		case EXEC_APPEND:
			Cbuf_AddText(text);
			break;
		default:
			Com_Error(ERR_FATAL, "Cbuf_ExecuteText: bad exec_when");
	}
}

void
Cbuf_Execute(void)
{
	int i;
	char *text;
	char line[CBUF_SIZE + 1];
	int quotes;
	qboolean comment;

	alias_count = 0; /* don't allow infinite alias loops */

	while (cmd_text.cursize)
	{
		/* find a \n or ; line break */
		text = (char *)cmd_text.data;

		quotes = 0;
		comment = false;

		for (i = 0; i < cmd_text.cursize; i++)
		{
			/* a // comment (outside quotes, at the start of a word) runs to the
			   end of the line: a ';' in it does not start a new command
			   (OpenMerc deviation, a comment "...; the same" ran "the") */
			if (!comment && !(quotes & 1) && (text[i] == '/') &&
				(i + 1 < cmd_text.cursize) && (text[i + 1] == '/') &&
				((i == 0) || (text[i - 1] == ' ') || (text[i - 1] == '\t') ||
				 (text[i - 1] == ';') || (text[i - 1] == '\r')))
			{
				comment = true;
			}

			if (!comment && (text[i] == '"'))
			{
				quotes++;
			}

			if (!comment && !(quotes & 1) && (text[i] == ';'))
			{
				break; /* don't break if inside a quoted string */
			}

			if (text[i] == '\n')
			{
				break;
			}
		}

		memcpy(line, text, i);
		line[i] = 0;

		/* delete the text from the command buffer and move remaining
		   commands down this is necessary because commands (exec,
		   alias) can insert data at the beginning of the text buffer */
		if (i == cmd_text.cursize)
		{
			cmd_text.cursize = 0;
		}
		else
		{
			i++;
			cmd_text.cursize -= i;
			memmove(text, text + i, cmd_text.cursize);
		}

		/* execute the command line */
		Cmd_ExecuteString(line);

		if (cmd_wait)
		{
			/* skip out while text still remains in buffer,
			   leaving it for the next frame */
			cmd_wait = false;
			break;
		}
	}
}

/*
 * Adds command line parameters as script statements Commands lead with
 * a +, and continue until another +
 *
 * Set commands are added early, so they are guaranteed to be set before
 * the client and server initialize for the first time.
 *
 * Other commands are added late, after all initialization is complete.
 *
 * SoF: a value with a space is quoted (SPEC-301 Behaviour 3).
 */
void
Cbuf_AddEarlyCommands(qboolean clear)
{
	int i;
	char *s;

	for (i = 0; i < COM_Argc(); i++)
	{
		s = COM_Argv(i);

		if (strcmp(s, "+set") != 0)
		{
			continue;
		}

		if (strchr(COM_Argv(i + 2), ' '))
		{
			Cbuf_AddText(va("set %s \"%s\"\n", COM_Argv(i + 1), COM_Argv(i + 2)));
		}
		else
		{
			Cbuf_AddText(va("set %s %s\n", COM_Argv(i + 1), COM_Argv(i + 2)));
		}

		if (clear)
		{
			COM_ClearArgv(i);
			COM_ClearArgv(i + 1);
			COM_ClearArgv(i + 2);
		}

		i += 2;
	}
}

/*
 * Adds command line parameters as script statements
 * Commands lead with a + and continue until another + or -
 * quake +developer 1 +map amlev1
 *
 * Returns true if any late commands were added, which
 * will keep the demoloop from immediately starting
 *
 * SoF (Quake II behaviour, kept): a piece ends at the next '+' or '-'
 * character, so "+map dm-cargo" queues "map dm" (SPEC-301).
 */
qboolean
Cbuf_AddLateCommands(void)
{
	int i, j;
	size_t s;
	char *text, c;
	int argc;
	qboolean has_args = false;

	/* build the combined string to parse from */
	s = 0;
	argc = COM_Argc();

	for (i = 1; i < argc; i++)
	{
		s += strlen(COM_Argv(i)) + 1;
	}

	if (!s)
	{
		return false;
	}

	text = Z_Malloc((int)s + 1);
	text[0] = 0;

	for (i = 1; i < argc; i++)
	{
		strcat(text, COM_Argv(i));

		if (i != argc - 1)
		{
			strcat(text, " ");
		}
	}

	/* pull out the commands */
	for (i = 0; i < (int)s - 1; i++)
	{
		if (text[i] == '+')
		{
			i++;

			for (j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++)
			{
			}

			c = text[j];
			text[j] = 0;

			Cbuf_AddText(text + i);
			Cbuf_AddText("\n");

			has_args = true;
			text[j] = c;
			i = j - 1;
		}
	}

	Z_Free(text);

	return has_args;
}

/*
 * exec <filename>: ".cfg" is appended to names longer than 4 characters
 * that do not end in it (SPEC-302 Behaviour 3)
 */
static void
Cmd_Exec_f(void)
{
	char name[MAX_OSPATH];
	char *f;
	int len;
	size_t l;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("exec <filename> : execute a script file\n");
		return;
	}

	Q_strlcpy(name, Cmd_Argv(1), sizeof(name) - 4);
	l = strlen(name);

	if ((l > 4) && Q_stricmp(name + l - 4, ".cfg"))
	{
		Q_strlcat(name, ".cfg", sizeof(name));
	}

	len = FS_LoadFile(name, (void **)&f);

	if (!f || (len < 0))
	{
		Com_Printf("Unable to exec %s\n", name);
		return;
	}

	Com_Printf("execing %s\n", name);

	/* the buffer has len + 2 bytes: data, '\n', NUL (SPEC-300) */
	Cbuf_InsertText(f);

	FS_FreeFile(f);
}

/*
 * Just prints the rest of the line to the console
 */
static void
Cmd_Echo_f(void)
{
	int i;

	for (i = 1; i < Cmd_Argc(); i++)
	{
		Com_Printf("%s ", Cmd_Argv(i));
	}

	Com_Printf("\n");
}

/*
 * Creates a new command that executes
 * a command string (possibly ; seperated)
 */
static void
Cmd_Alias_f(void)
{
	cmdalias_t *a;
	char cmd[1024];
	int i, c;
	char *s;

	if (Cmd_Argc() == 1)
	{
		Com_Printf("Current alias commands:\n");

		for (a = cmd_alias; a; a = a->next)
		{
			Com_Printf("%s : %s\n", a->name, a->value);
		}

		return;
	}

	s = Cmd_Argv(1);

	if (strlen(s) >= MAX_ALIAS_NAME)
	{
		Com_Printf("Alias name is too long\n");
		return;
	}

	/* if the alias already exists, reuse it */
	for (a = cmd_alias; a; a = a->next)
	{
		if (!strcmp(s, a->name))
		{
			Z_Free(a->value);
			break;
		}
	}

	if (!a)
	{
		a = Z_Malloc(sizeof(cmdalias_t));
		a->next = cmd_alias;
		cmd_alias = a;
	}

	strcpy(a->name, s);

	/* copy the rest of the command line */
	cmd[0] = 0; /* start out with a null string */
	c = Cmd_Argc();

	for (i = 2; i < c; i++)
	{
		Q_strlcat(cmd, Cmd_Argv(i), sizeof(cmd));

		if (i != (c - 1))
		{
			Q_strlcat(cmd, " ", sizeof(cmd));
		}
	}

	Q_strlcat(cmd, "\n", sizeof(cmd));

	a->value = CopyString(cmd);
}

int
Cmd_Argc(void)
{
	return cmd_argc;
}

char *
Cmd_Argv(int arg)
{
	if ((unsigned)arg >= (unsigned)cmd_argc)
	{
		return cmd_null_string;
	}

	return cmd_argv[arg];
}

/*
 * Returns a single string containing argv(1) to argv(argc()-1)
 */
char *
Cmd_Args(void)
{
	return cmd_args;
}

/* console value of a macro: hidden cvars are invisible */
static const char *
Cmd_MacroValue(const char *name)
{
	cvar_t *var = Cvar_Find(name);

	if (!var || (var->flags & CVAR_HIDDEN))
	{
		return "";
	}

	return var->string;
}

/*
 * $name -> value, #name -> "value"; an empty replacement becomes ""
 * (SPEC-302 Behaviour 4)
 */
static char *
Cmd_MacroExpandString(char *text)
{
	static char expanded[CMD_MAX_LINE];
	char temporary[CMD_MAX_LINE * 2 + 8];
	char token[CMD_MAX_LINE];
	char repl[CMD_MAX_LINE + 2];
	size_t len, i, tl, rl;
	int count;
	qboolean inquote;
	char *scan;
	const char *value;

	inquote = false;
	scan = text;

	len = strlen(scan);

	if (len >= CMD_MAX_LINE)
	{
		Com_Printf("Line exceeded %i chars, discarded.\n", CMD_MAX_LINE);
		return NULL;
	}

	count = 0;

	for (i = 0; i < len; i++)
	{
		char mc = scan[i];

		if (mc == '"')
		{
			inquote ^= 1;
		}

		if (inquote)
		{
			continue; /* don't expand inside quotes */
		}

		if ((mc != '$') && (mc != '#'))
		{
			continue;
		}

		/* the token is the following run of non-space characters */
		tl = 0;

		while ((i + 1 + tl < len) && ((byte)scan[i + 1 + tl] > ' ') &&
				(tl < sizeof(token) - 1))
		{
			token[tl] = scan[i + 1 + tl];
			tl++;
		}

		token[tl] = 0;

		if (!tl)
		{
			continue;
		}

		value = Cmd_MacroValue(token);

		if (!value[0])
		{
			Q_strlcpy(repl, "\"\"", sizeof(repl));
		}
		else if (mc == '#')
		{
			Com_sprintf(repl, sizeof(repl), "\"%s\"", value);
		}
		else
		{
			Q_strlcpy(repl, value, sizeof(repl));
		}

		rl = strlen(repl);

		if (len - tl - 1 + rl >= CMD_MAX_LINE)
		{
			Com_Printf("Expanded line exceeded %i chars, discarded.\n",
					CMD_MAX_LINE);
			return NULL;
		}

		memcpy(temporary, scan, i);
		memcpy(temporary + i, repl, rl);
		strcpy(temporary + i + rl, scan + i + 1 + tl);

		Q_strlcpy(expanded, temporary, sizeof(expanded));
		scan = expanded;
		len = strlen(scan);

		/* scanning continues at the replacement */
		i--;

		if (++count >= CMD_MAX_MACROS)
		{
			Com_Printf("Macro expansion loop, discarded.\n");
			return NULL;
		}
	}

	if (inquote)
	{
		Com_Printf("Line has unmatched quote, discarded.\n");
		return NULL;
	}

	return scan;
}

/*
 * Parses the given string into command line tokens.
 * $Cvars will be expanded unless they are in a quoted token
 */
void
Cmd_TokenizeString(char *text, qboolean macroExpand)
{
	int i;
	const char *com_token;

	/* clear the args from the last string */
	for (i = 0; i < cmd_argc; i++)
	{
		Z_Free(cmd_argv[i]);
	}

	cmd_argc = 0;
	cmd_args[0] = 0;

	/* macro expand the text */
	if (macroExpand)
	{
		text = Cmd_MacroExpandString(text);
	}
	else if (strlen(text) >= CMD_MAX_LINE)
	{
		Com_Printf("Line exceeded %i chars, discarded.\n", CMD_MAX_LINE);
		return;
	}

	if (!text)
	{
		return;
	}

	while (1)
	{
		/* skip whitespace up to a /n */
		while (*text && (unsigned char)*text <= ' ' && *text != '\n')
		{
			text++;
		}

		if (*text == '\n')
		{
			/* a newline seperates commands in the buffer */
			text++;
			break;
		}

		if (!*text)
		{
			return;
		}

		/* set cmd_args to everything after the first arg */
		if (cmd_argc == 1)
		{
			int l;

			Q_strlcpy(cmd_args, text, sizeof(cmd_args));

			/* strip off any trailing whitespace */
			l = (int)strlen(cmd_args) - 1;

			for ( ; l >= 0; l--)
			{
				if ((unsigned char)cmd_args[l] <= ' ')
				{
					cmd_args[l] = 0;
				}

				else
				{
					break;
				}
			}
		}

		com_token = COM_Parse(&text);

		if (!text)
		{
			return;
		}

		if (cmd_argc < CMD_MAX_ARGS)
		{
			cmd_argv[cmd_argc] = Z_Malloc((int)strlen(com_token) + 1);
			strcpy(cmd_argv[cmd_argc], com_token);
			cmd_argc++;
		}
	}
}

void
Cmd_AddCommand(const char *cmd_name, xcommand_t function)
{
	cmd_function_t *cmd;

	/* fail if the command is a variable name */
	if (Cvar_Find(cmd_name))
	{
		Com_Printf("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
		return;
	}

	/* fail if the command already exists */
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp(cmd_name, cmd->name))
		{
			Com_Printf("Cmd_AddCommand: %s already defined\n", cmd_name);
			return;
		}
	}

	cmd = Z_Malloc(sizeof(cmd_function_t));
	cmd->name = CopyString(cmd_name);
	cmd->function = function;

	/* newest first, as cmdlist shows them */
	cmd->next = cmd_functions;
	cmd_functions = cmd;
}

void
Cmd_RemoveCommand(const char *cmd_name)
{
	cmd_function_t *cmd, **back;

	back = &cmd_functions;

	while (1)
	{
		cmd = *back;

		if (!cmd)
		{
			Com_Printf("Cmd_RemoveCommand: %s not added\n", cmd_name);
			return;
		}

		if (!strcmp(cmd_name, cmd->name))
		{
			*back = cmd->next;
			Z_Free(cmd->name);
			Z_Free(cmd);
			return;
		}

		back = &cmd->next;
	}
}

qboolean
Cmd_Exists(const char *cmd_name)
{
	cmd_function_t *cmd;

	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp(cmd_name, cmd->name))
		{
			return true;
		}
	}

	return false;
}

const char *
Cmd_CompleteCommand(const char *partial)
{
	cmd_function_t *cmd;
	size_t len;
	int i, o, p;
	cmdalias_t *a;
	cvar_t *cvar;
	const char **pmatch;
	qboolean diff = false;

	len = strlen(partial);

	if (!len)
	{
		return NULL;
	}

	i = 0;

	/* check for exact match */
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp(partial, cmd->name))
		{
			return cmd->name;
		}

		i++;
	}

	for (a = cmd_alias; a; a = a->next)
	{
		if (!strcmp(partial, a->name))
		{
			return a->name;
		}

		i++;
	}

	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (cvar->flags & CVAR_HIDDEN)
		{
			continue; /* never offered (SPEC-313 §10) */
		}

		if (!strcmp(partial, cvar->name))
		{
			return cvar->name;
		}

		i++;
	}

	if (!i)
	{
		return NULL;
	}

	pmatch = malloc(i * sizeof(char *));
	YQ2_COM_CHECK_OOM(pmatch, "malloc()", i * sizeof(char *))
	if (!pmatch)
	{
		/* unaware about YQ2_ATTR_NORETURN_FUNCPTR? */
		return NULL;
	}

	memset(pmatch, 0, i * sizeof(char *));

	i = 0;

	/* check for partial match */
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strncmp(partial, cmd->name, len))
		{
			pmatch[i] = cmd->name;
			i++;
		}
	}

	for (a = cmd_alias; a; a = a->next)
	{
		if (!strncmp(partial, a->name, len))
		{
			pmatch[i] = a->name;
			i++;
		}
	}

	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (cvar->flags & CVAR_HIDDEN)
		{
			continue;
		}

		if (!strncmp(partial, cvar->name, len))
		{
			pmatch[i] = cvar->name;
			i++;
		}
	}

	if (i)
	{
		if (i == 1)
		{
			Q_strlcpy(retval, pmatch[0], sizeof(retval));
		}
		else
		{
			/* Sort it */
			qsort(pmatch, i, sizeof(pmatch[0]), Q_sort_strcomp);

			Com_Printf("\n\n");

			for (o = 0; o < i; o++)
			{
				Com_Printf("  %s\n", pmatch[o]);
			}

			strcpy(retval, "");
			p = 0;

			while (!diff && p < 256)
			{
				retval[p] = pmatch[0][p];

				for (o = 0; o < i; o++)
				{
					if (p > strlen(pmatch[o]))
					{
						continue;
					}

					if (retval[p] != pmatch[o][p])
					{
						retval[p] = 0;
						diff = true;
					}
				}

				p++;
			}
		}

		/* remove list */
		free(pmatch);

		return retval;
	}

	/* remove list */
	free(pmatch);

	return NULL;
}

/*
 * Console TAB cycling (SPEC-313 §10): the match after previous (NULL = the
 * first one). Matches are the exact ones, then the prefix matches, each in
 * list order commands, aliases, cvars; hidden cvars are never offered; the
 * cycle wraps. Returns NULL when nothing matches.
 */
const char *
Cmd_CompleteNext(const char *prefix, const char *previous)
{
	const char *first = NULL;
	qboolean take = (previous == NULL);
	size_t len;
	int pass;

	if (!prefix)
	{
		return NULL;
	}

	if ((*prefix == '/') || (*prefix == '\\'))
	{
		prefix++;
	}

	len = strlen(prefix);

	if (!len)
	{
		return NULL;
	}

	for (pass = 0; pass < 2; pass++)
	{
		cmd_function_t *cmd;
		cmdalias_t *a;
		cvar_t *var;

#define OM_MATCH(n) ((pass == 0) ? !Q_strcasecmp((n), prefix) : \
		(Q_strncasecmp((n), prefix, len) == 0 && Q_strcasecmp((n), prefix)))
#define OM_OFFER(n) \
		do { \
			if (!first) first = (n); \
			if (take) return (n); \
			if (!strcmp((n), previous)) take = true; \
		} while (0)

		for (cmd = cmd_functions; cmd; cmd = cmd->next)
		{
			if (OM_MATCH(cmd->name))
			{
				OM_OFFER(cmd->name);
			}
		}

		for (a = cmd_alias; a; a = a->next)
		{
			if (OM_MATCH(a->name))
			{
				OM_OFFER(a->name);
			}
		}

		for (var = cvar_vars; var; var = var->next)
		{
			if (!(var->flags & CVAR_HIDDEN) && OM_MATCH(var->name))
			{
				OM_OFFER(var->name);
			}
		}

#undef OM_OFFER
#undef OM_MATCH
	}

	/* previous was the last match (or unknown): wrap to the first */
	return first;
}

const char *
Cmd_CompleteMapCommand(const char *partial)
{
	strlist_t mapNames;

	mapNames = FS_ListFiles2("maps/*.bsp", 0, 0);

	if (mapNames.num)
	{
		size_t len;
		int i, j, k, nbMatches;
		char *mapName, *lastsep;
		const char **pmatch;
		qboolean partialFillContinue = true;

		len = strlen(partial);
		nbMatches = 0;
		memset(retval, 0, sizeof(retval));

		pmatch = malloc(mapNames.num * sizeof(char*));
		YQ2_COM_CHECK_OOM(pmatch, "malloc()", (mapNames.num + 1) * sizeof(char*))
		if (!pmatch)
		{
			/* unaware about YQ2_ATTR_NORETURN_FUNCPTR? */
			StrList_Free(&mapNames);
			return retval;
		}

		for (i = 0; i < mapNames.num; i++)
		{
			if ((lastsep = strrchr(mapNames.data[i], '/')))
			{
				mapName = lastsep + 1;
			}
			else
			{
				mapName = mapNames.data[i];
			}

			mapName = strtok(mapName, ".");

			/* check for exact match */
			if (!Q_strcasecmp(partial, mapName))
			{
				Q_strlcpy(retval, partial, sizeof(retval));
			}

			/* check for partial match */
			else if (!Q_strncasecmp(partial, mapName, len))
			{
				pmatch[nbMatches] = mapName;
				nbMatches++;
			}
		}

		if (nbMatches == 1)
		{
			Q_strlcpy(retval, pmatch[0], sizeof(retval));
		}
		else if (nbMatches > 1)
		{
			Com_Printf("\n=================\n\n");

			/* Sort it */
			qsort(pmatch, nbMatches, sizeof(pmatch[0]), Q_sort_strcomp);
			size_t pmatchlen = strlen(pmatch[0]);

			for (j = 0; j < nbMatches; j++)
			{
				Com_Printf("%s\n", pmatch[j]);
			}

			//partial complete
			for (j = 0; j < pmatchlen; j++)
			{
				for (k = 1; k < nbMatches; k++)
				{
					if (j >= strlen(pmatch[k]) || tolower((unsigned char)pmatch[0][j]) != tolower((unsigned char)pmatch[k][j]))
					{
						partialFillContinue = false;
						break;
					}
				}

				if (partialFillContinue)
				{
					retval[j] = pmatch[0][j];
				}
				else
				{
					break;
				}
			}
		}

		free(pmatch);
		StrList_Free(&mapNames);
	}

	return retval;
}

qboolean
Cmd_IsComplete(const char *command)
{
	cmd_function_t *cmd;
	cmdalias_t *a;
	cvar_t *cvar;

	/* check for exact match */
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!strcmp(command, cmd->name))
		{
			return true;
		}
	}

	for (a = cmd_alias; a; a = a->next)
	{
		if (!strcmp(command, a->name))
		{
			return true;
		}
	}

	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (!(cvar->flags & CVAR_HIDDEN) && !strcmp(command, cvar->name))
		{
			return true;
		}
	}

	return false;
}

/* ugly hack to suppress warnings from default.cfg in Key_Bind_f() */
qboolean doneWithDefaultCfg;

/*
 * A complete command line has been parsed, so try to execute it
 * (SPEC-302 Behaviour 5)
 */
void
Cmd_ExecuteString(char *text)
{
	cmd_function_t *cmd;
	cmdalias_t *a;

	Cmd_TokenizeString(text, true);

	/* execute the command line */
	if (!Cmd_Argc())
	{
		return; /* no tokens */
	}

	if (com_cmdhook)
	{
		com_cmdhook(text); /* OpenMerc om_autorecord (header/omrec.h) */
	}

	if (Cmd_Argc() > 1 && Q_strcasecmp(cmd_argv[0], "exec") == 0 &&
		Q_strcasecmp(cmd_argv[1], "config.cfg") == 0)
	{
		/* exec config.cfg runs directly after exec default.cfg */
		doneWithDefaultCfg = true;
	}

	/* check functions */
	for (cmd = cmd_functions; cmd; cmd = cmd->next)
	{
		if (!Q_strcasecmp(cmd_argv[0], cmd->name))
		{
			if (!cmd->function)
			{
				/* forward to server command */
				Cmd_ExecuteString(va("cmd %s", text));
			}
			else
			{
				cmd->function();
			}

			return;
		}
	}

	/* check alias */
	for (a = cmd_alias; a; a = a->next)
	{
		if (!Q_strcasecmp(cmd_argv[0], a->name))
		{
			if (++alias_count == ALIAS_LOOP_COUNT)
			{
				Com_Printf("ALIAS_LOOP_COUNT\n");
				return;
			}

			Cbuf_InsertText(a->value);
			return;
		}
	}

	/* check cvars */
	if (Cvar_Command())
	{
		return;
	}

#ifndef DEDICATED_ONLY
	/* send it as a server command if we are connected */
	Cmd_ForwardToServer();
#else
	Com_Printf("Unknown command \"%s\"\n", Cmd_Argv(0));
#endif
}

static void
Cmd_List_f(void)
{
	cmd_function_t *cmd;
	int i;

	i = 0;

	for (cmd = cmd_functions; cmd; cmd = cmd->next, i++)
	{
		Com_Printf("%s\n", cmd->name);
	}

	Com_Printf("%i commands\n", i);
}

static void
Cmd_CpuId_f(void)
{
	SoF_CpuInfo(true);
}

void
Cmd_Init(void)
{
	/* register our commands (registration order of the original) */
	Cmd_AddCommand("cmdlist", Cmd_List_f);
	Cmd_AddCommand("exec", Cmd_Exec_f);
	Cmd_AddCommand("echo", Cmd_Echo_f);
	Cmd_AddCommand("alias", Cmd_Alias_f);
	Cmd_AddCommand("wait", Cmd_Wait_f);
	Cmd_AddCommand("cpuid", Cmd_CpuId_f);
}

void
Cmd_Shutdown(void)
{
	cmdalias_t *next;

	while (cmd_alias != NULL)
	{
		next = cmd_alias->next;
		Z_Free(cmd_alias->value);
		Z_Free(cmd_alias);
		cmd_alias = next;
	}
}
