/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Cvar defaults and the menus' "Reset Section" (om_resetcvars), on the real cvar.c with the engine services it
 * calls replaced by small stand-ins below.
 *
 * The case this guards: a menu page named each of its cvars with Cvar_Get(name, "" or "0"), which replaced the
 * cvar's default, so "Reset Section" set every setting of the section to nothing - on a phone the touch buttons
 * went to size 0 and alpha 0 and touch input turned off. Menu widgets use Cvar_GetRef, which leaves the default
 * of an existing cvar alone.
 *
 * Usage: cvar_test   exit code = number of failed checks
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/common.h"

cvar_t *Cvar_Find(const char *name); /* header/sof/engine.h */

/* ---- stand-ins for the engine services cvar.c calls ---------------------------------------------------------- */

char userGivenGame[MAX_QPATH];

typedef struct
{
	size_t size;
	size_t pad; /* keeps the block 16-byte aligned on 64-bit */
} zhead_t;

void *
Z_Malloc(size_t size)
{
	zhead_t *h = calloc(1, sizeof(zhead_t) + size);

	if (!h)
	{
		abort();
	}

	h->size = size;
	return h + 1;
}

void
Z_Free(void *ptr)
{
	if (ptr)
	{
		free((zhead_t *)ptr - 1);
	}
}

size_t
Z_BlockSize(const void *ptr)
{
	return ((const zhead_t *)ptr - 1)->size;
}

void *
Z_Realloc(void *ptr, size_t size)
{
	void *n = Z_Malloc(size);

	if (ptr)
	{
		size_t old = Z_BlockSize(ptr);

		memcpy(n, ptr, old < size ? old : size);
		Z_Free(ptr);
	}

	return n;
}

char *
CopyString(const char *in)
{
	char *out = Z_Malloc(strlen(in) + 1);

	strcpy(out, in);
	return out;
}

void
Com_Printf(const char *fmt, ...)
{
	(void)fmt;
}

void
Com_sprintf(char *dest, int size, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(dest, (size_t)size, fmt, ap);
	va_end(ap);
}

int
Com_ServerState(void)
{
	return 0;
}

int
Q_stricmp(const char *s1, const char *s2)
{
	for (;;)
	{
		int c1 = (unsigned char)*s1++, c2 = (unsigned char)*s2++;

		if (c1 >= 'A' && c1 <= 'Z')
		{
			c1 += 'a' - 'A';
		}

		if (c2 >= 'A' && c2 <= 'Z')
		{
			c2 += 'a' - 'A';
		}

		if (c1 != c2 || !c1)
		{
			return c1 - c2;
		}
	}
}

int
Q_strlcpy(char *dst, const char *src, int size)
{
	int len = (int)strlen(src);

	if (size > 0)
	{
		int n = len < size - 1 ? len : size - 1;

		memcpy(dst, src, (size_t)n);
		dst[n] = 0;
	}

	return len;
}

int
Q_strlcat(char *dst, const char *src, int size)
{
	int d = (int)strlen(dst);

	return d + Q_strlcpy(dst + d, src, size - d);
}

FILE *
Q_fopen(const char *file, const char *mode)
{
	return fopen(file, mode);
}

void
Info_SetValueForKey(char *s, const char *key, const char *value)
{
	(void)s;
	(void)key;
	(void)value;
}

void
FS_BuildGameSpecificSearchPath(const char *dir)
{
	(void)dir;
}

/* commands: cvar.c registers its console commands here; Run() tokenizes a line on spaces and calls one */
#define MAXCMDS 32
#define MAXARGS 16

static struct
{
	const char *name;
	xcommand_t fn;
} cmds[MAXCMDS];
static int numcmds;
static char argbuf[512];
static char *argv_[MAXARGS];
static int argc_;

void
Cmd_AddCommand(const char *cmd_name, xcommand_t function)
{
	if (numcmds < MAXCMDS)
	{
		cmds[numcmds].name = cmd_name;
		cmds[numcmds].fn = function;
		numcmds++;
	}
}

void
Cmd_RemoveCommand(const char *cmd_name)
{
	(void)cmd_name;
}

int
Cmd_Argc(void)
{
	return argc_;
}

char *
Cmd_Argv(int arg)
{
	return (arg >= 0 && arg < argc_) ? argv_[arg] : "";
}

static int
Run(const char *line)
{
	char *p;
	int i;

	Q_strlcpy(argbuf, line, sizeof(argbuf));
	argc_ = 0;

	for (p = strtok(argbuf, " "); p && argc_ < MAXARGS; p = strtok(NULL, " "))
	{
		argv_[argc_++] = p;
	}

	for (i = 0; i < numcmds; i++)
	{
		if (argc_ && !strcmp(cmds[i].name, argv_[0]))
		{
			cmds[i].fn();
			return 1;
		}
	}

	return 0;
}

/* ---- checks -------------------------------------------------------------------------------------------------- */

static int failures, checks;

static void
ExpectStr(const char *what, const char *got, const char *want)
{
	checks++;

	if (!got || strcmp(got, want))
	{
		printf("FAIL %s: \"%s\", expected \"%s\"\n", what, got ? got : "(null)", want);
		failures++;
	}
}

static void
Expect(const char *what, int ok)
{
	checks++;

	if (!ok)
	{
		printf("FAIL %s\n", what);
		failures++;
	}
}

static const char *
Val(const char *name)
{
	return Cvar_VariableString(name);
}

int
main(void)
{
	cvar_t *v;

	Cvar_Init();
	Expect("om_resetcvars is a command", Run("om_resetcvars") == 1);

	/* 1. the phone case: the engine declares the touch cvars, the touch controls page names them ("" from the
	   cvar keyword of a slider or list), the player changes them, "Reset Section" brings back the declared values */
	Cvar_Get("touch_alpha", "0.35", CVAR_ARCHIVE);
	Cvar_Get("touch_size", "1", CVAR_ARCHIVE);
	Cvar_Get("in_touch", "1", CVAR_ARCHIVE);
	Cvar_GetRef("touch_alpha", "", 0, false);
	Cvar_GetRef("touch_size", "", 0, false);
	Cvar_GetRef("in_touch", "", 0, false);
	Cvar_Set("touch_alpha", "0.8");
	Cvar_Set("touch_size", "1.4");
	Cvar_Set("in_touch", "1");
	Run("om_resetcvars touch_alpha touch_size in_touch");
	ExpectStr("reset touch_alpha after the page named it", Val("touch_alpha"), "0.35");
	ExpectStr("reset touch_size after the page named it", Val("touch_size"), "1");
	ExpectStr("reset in_touch after the page named it", Val("in_touch"), "1");

	/* 2. a reference creates a missing cvar with its value (a menu-only cvar, the ckey "0") */
	v = Cvar_GetRef("menu_test_ckey", "0", 0, true);
	Expect("reference creates a missing cvar", v != NULL);
	ExpectStr("created by a reference: value", Val("menu_test_ckey"), "0");
	ExpectStr("created by a reference: default", v ? v->default_string : NULL, "0");

	/* 3. the page is read before the module that owns the cvar (om_throwprecise: the game registers it at the first
	   throw); the owner's value becomes the default afterwards */
	Cvar_GetRef("om_test_late", "", 0, false);
	v = Cvar_Get("om_test_late", "1", CVAR_ARCHIVE);
	ExpectStr("owner after a reference: default", v ? v->default_string : NULL, "1");
	Cvar_Set("om_test_late", "0");
	Run("om_resetcvars om_test_late");
	ExpectStr("owner after a reference: reset", Val("om_test_late"), "1");

	/* 4. config.cfg sets the cvar before its owner declares it; the player's value stays, the default is the
	   owner's, a page naming it afterwards changes neither */
	Cvar_Set("om_test_cfg", "0");
	Cvar_Get("om_test_cfg", "1", CVAR_ARCHIVE);
	ExpectStr("config before owner: value kept", Val("om_test_cfg"), "0");
	Cvar_GetRef("om_test_cfg", "", 0, false);
	ExpectStr("config before owner: value after the page", Val("om_test_cfg"), "0");
	Run("om_resetcvars om_test_cfg");
	ExpectStr("config before owner: reset", Val("om_test_cfg"), "1");

	/* 5. an owner's Cvar_Get still sets the default (Quake II behaviour the modules rely on) */
	Cvar_Get("om_test_owner", "1", 0);
	v = Cvar_Get("om_test_owner", "2", 0);
	ExpectStr("second owner declaration: default", v ? v->default_string : NULL, "2");

	/* 6. a reference that is not privileged does not see a hidden cvar while hidden cvars are locked (as
	   Cvar_GetModule), a privileged one does */
	Cvar_Get("om_test_hidden", "5", CVAR_HIDDEN);
	Expect("hidden cvar: no unprivileged reference", Cvar_GetRef("om_test_hidden", "", 0, false) == NULL);
	v = Cvar_GetRef("om_test_hidden", "", 0, true);
	Expect("hidden cvar: privileged reference", v != NULL);
	ExpectStr("hidden cvar: default kept", v ? v->default_string : NULL, "5");

	/* 7. reset of an unknown name and of a cvar already at its default changes nothing */
	Run("om_resetcvars no_such_cvar touch_size");
	ExpectStr("reset of a cvar at its default", Val("touch_size"), "1");
	Expect("reset does not create a cvar", Cvar_Find("no_such_cvar") == NULL);

	printf("cvar_test: %d checks, %d failed\n", checks, failures);
	return failures;
}
