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
 * The Quake II CVAR subsystem. Implements dynamic variable handling.
 *
 * =======================================================================
 *
 * OpenMerc: Soldier of Fortune 1.07f cvar semantics - newest-created
 * first, the SoF flag bits (integer, literal, hidden, protected,
 * inventory), change callbacks, the 13-column cvarlist, set with the
 * letters u s w i a m, sset, zero, the console toggles ! + - and the
 * "matrix" alias of "timescale". Yamagi's cvar renaming table and its
 * inc/dec/reset/resetall/toggle commands are removed (SoF configs use the
 * original names, e.g. gl_mode).
 * Implements: SPEC-302 (Cvar flags, cvarlist, Behaviour 1, 2, 7),
 *             SPEC-002 (cvar object), SPEC-001 #87/#91
 *
 * =======================================================================
 */

#include "header/common.h"

#define CVAR_HASH_SIZE 512

cvar_t *cvar_vars; /* newest-created first */
qboolean userinfo_modified;

static cvar_t *cvar_hash[CVAR_HASH_SIZE];
static int cvar_count;
static qboolean cvar_hidden_access; /* renderer slot 0x4c */

typedef enum
{
	SET_CONSOLE, /* console / Cvar_Set: no-set and latch apply */
	SET_FORCE,   /* engine code: ignores no-set and latch */
	SET_FORCE_UNPROTECTED /* save load, module force-set: protected wins */
} cvarset_t;

static unsigned
Cvar_HashName(const char *name)
{
	unsigned h = 0;

	while (*name)
	{
		h = h * 33 + (byte)*name++;
	}

	return h & (CVAR_HASH_SIZE - 1);
}

/* "matrix" is an alias of "timescale" in every lookup (SPEC-302 B1) */
static const char *
Cvar_AliasName(const char *name)
{
	if (name && (strcmp(name, "matrix") == 0))
	{
		return "timescale";
	}

	return name;
}

static char *
Cvar_CopyString(char *old_str, const char *new_value)
{
	size_t ol, nl;

	if (!new_value)
	{
		new_value = "";
	}

	if (!old_str)
	{
		return CopyString(new_value);
	}

	ol = Z_BlockSize(old_str);
	nl = strlen(new_value) + 1;

	if (nl > ol)
	{
		old_str = Z_Realloc(old_str, nl);
	}

	memmove(old_str, new_value, nl);

	return old_str;
}

static qboolean
Cvar_InfoValidate(const char *s)
{
	if (strchr(s, '\\') || strchr(s, '"') || strchr(s, ';'))
	{
		return false;
	}

	return true;
}

/* privileged lookup, names are case-sensitive */
cvar_t *
Cvar_Find(const char *var_name)
{
	cvar_t *var;

	if (!var_name)
	{
		return NULL;
	}

	var_name = Cvar_AliasName(var_name);

	for (var = cvar_hash[Cvar_HashName(var_name)]; var; var = var->hash_next)
	{
		if (!strcmp(var_name, var->name))
		{
			return var;
		}
	}

	return NULL;
}

/* console / module lookup: hidden cvars are invisible */
static cvar_t *
Cvar_FindVisible(const char *var_name)
{
	cvar_t *var = Cvar_Find(var_name);

	if (var && (var->flags & CVAR_HIDDEN) && !cvar_hidden_access)
	{
		return NULL;
	}

	return var;
}

int
Cvar_Count(void)
{
	return cvar_count;
}

void
Cvar_SetHiddenAccess(qboolean allow)
{
	cvar_hidden_access = allow ? true : false;
}

float
Cvar_VariableValue(const char *var_name)
{
	const cvar_t *var = Cvar_Find(var_name);

	if (!var)
	{
		return 0;
	}

	return (float)atof(var->string);
}

const char *
Cvar_VariableString(const char *var_name)
{
	const cvar_t *var = Cvar_Find(var_name);

	return var ? var->string : "";
}

/* value as stored: integer cvars keep the atoi result (SPEC-302 0x20) */
static const char *
Cvar_StoredValue(const cvar_t *var, int flags, const char *value, char *buf, size_t bufsize)
{
	if (flags & CVAR_INTEGER)
	{
		Com_sprintf(buf, bufsize, "%i", atoi(value));
		return buf;
	}

	(void)var;
	return value;
}

static cvar_t *
Cvar_Create(const char *var_name, const char *var_value, int flags,
		cvarcallback_t changed)
{
	cvar_t *var;
	char ibuf[32];
	unsigned h;

	var_value = Cvar_StoredValue(NULL, flags, var_value, ibuf, sizeof(ibuf));

	var = Z_Malloc(sizeof(*var));
	var->name = CopyString(var_name);
	var->string = CopyString(var_value);
	var->default_string = CopyString(var_value);
	var->flags = flags;
	var->changed = changed;
	var->modified = true;
	var->value = (float)atof(var->string);

	/* newest first, as cvarlist and config.cfg show them */
	var->next = cvar_vars;
	cvar_vars = var;

	h = Cvar_HashName(var->name);
	var->hash_next = cvar_hash[h];
	cvar_hash[h] = var;

	cvar_count++;

	if (changed)
	{
		changed(var);
	}

	return var;
}

static cvar_t *
Cvar_GetEx(const char *var_name, const char *var_value, int flags,
		cvarcallback_t changed, qboolean privileged)
{
	cvar_t *var;

	if (!var_name)
	{
		return NULL;
	}

	var_name = Cvar_AliasName(var_name);

	if (flags & CVAR_INFOMASK)
	{
		if (!Cvar_InfoValidate(var_name))
		{
			Com_Printf("Invalid info cvar name\n");
			return NULL;
		}
	}

	var = Cvar_Find(var_name);

	if (var)
	{
		if ((var->flags & CVAR_HIDDEN) && !privileged && !cvar_hidden_access)
		{
			return NULL;
		}

		var->flags |= flags;

		if (var_value)
		{
			var->default_string = Cvar_CopyString(var->default_string, var_value);
		}

		if (changed)
		{
			var->changed = changed;
			changed(var);
		}

		return var;
	}

	if (!var_value)
	{
		return NULL;
	}

	if ((flags & CVAR_HIDDEN) && !privileged && !cvar_hidden_access)
	{
		return NULL;
	}

	if (flags & CVAR_INFOMASK)
	{
		if (!Cvar_InfoValidate(var_value))
		{
			Com_Printf("Invalid info cvar value\n");
			return NULL;
		}
	}

	return Cvar_Create(var_name, var_value, flags, changed);
}

/*
 * If the variable already exists, the value will not be set
 * The flags will be or'ed in if the variable exists.
 */
cvar_t *
Cvar_Get(const char *var_name, const char *var_value, int flags)
{
	return Cvar_GetEx(var_name, var_value, flags, NULL, true);
}

cvar_t *
Cvar_GetCB(const char *var_name, const char *var_value, int flags,
		cvarcallback_t changed)
{
	return Cvar_GetEx(var_name, var_value, flags, changed, true);
}

cvar_t *
Cvar_GetModule(const char *var_name, const char *var_value, int flags,
		cvarcallback_t changed)
{
	return Cvar_GetEx(var_name, var_value, flags, changed, false);
}

/* applies a value change: game directory, callback */
static void
Cvar_Changed(cvar_t *var)
{
	var->modified = true;

	if (var->flags & CVAR_USERINFO)
	{
		userinfo_modified = true;
	}

	if (!strcmp(var->name, "game"))
	{
		FS_BuildGameSpecificSearchPath(var->string);
	}

	if (var->changed)
	{
		var->changed(var);
	}
}

static cvar_t *
Cvar_SetVar(cvar_t *var, const char *value, cvarset_t mode)
{
	char ibuf[32];

	if (!value)
	{
		value = "";
	}

	if (var->flags & CVAR_INFOMASK)
	{
		if (!Cvar_InfoValidate(value))
		{
			Com_Printf("invalid info cvar value\n");
			return var;
		}
	}

	if ((mode == SET_FORCE_UNPROTECTED) && (var->flags & CVAR_PROTECTED))
	{
		return var;
	}

	value = Cvar_StoredValue(var, var->flags, value, ibuf, sizeof(ibuf));

	if (mode == SET_CONSOLE)
	{
		if (var->flags & CVAR_NOSET)
		{
			Com_Printf("%s is write protected.\n", var->name);
			return var;
		}

		if (var->flags & CVAR_LATCH)
		{
			if (var->latched_string)
			{
				if (strcmp(value, var->latched_string) == 0)
				{
					return var;
				}

				Z_Free(var->latched_string);
				var->latched_string = NULL;
			}
			else if (strcmp(value, var->string) == 0)
			{
				return var;
			}

			if (Com_ServerState())
			{
				Com_Printf("%s will be changed for next game.\n", var->name);
				var->latched_string = CopyString(value);
				return var;
			}
		}
	}
	else if (var->latched_string)
	{
		Z_Free(var->latched_string);
		var->latched_string = NULL;
	}

	if (!strcmp(value, var->string))
	{
		/* SoF runs the change callback on every assignment
		   ("set sp_language 0" reloads the packages, SPEC-313 test 2) */
		if (var->changed)
		{
			var->changed(var);
		}

		return var;
	}

	var->string = Cvar_CopyString(var->string, value);
	var->value = (float)atof(var->string);

	Cvar_Changed(var);

	return var;
}

static cvar_t *
Cvar_Set2(const char *var_name, const char *value, cvarset_t mode)
{
	cvar_t *var = Cvar_Find(var_name);

	if (!var)
	{
		/* SPEC-302 B1: set on an unknown name creates the cvar (flags 0) */
		return Cvar_GetEx(var_name, value ? value : "", 0, NULL, true);
	}

	return Cvar_SetVar(var, value, mode);
}

cvar_t *
Cvar_ForceSet(const char *var_name, const char *value)
{
	return Cvar_Set2(var_name, value, SET_FORCE);
}

cvar_t *
Cvar_ForceSetUnprotected(const char *var_name, const char *value)
{
	return Cvar_Set2(var_name, value, SET_FORCE_UNPROTECTED);
}

cvar_t *
Cvar_Set(const char *var_name, const char *value)
{
	return Cvar_Set2(var_name, value, SET_CONSOLE);
}

/* creates or overwrites the cvar and replaces its flags */
cvar_t *
Cvar_FullSet(const char *var_name, const char *value, int flags)
{
	cvar_t *var;
	char ibuf[32];

	if (!value)
	{
		value = "";
	}

	var = Cvar_Find(var_name);

	if (!var)
	{
		return Cvar_GetEx(var_name, value, flags, NULL, true);
	}

	if (flags & CVAR_INFOMASK)
	{
		if (!Cvar_InfoValidate(var->name))
		{
			Com_Printf("Invalid info cvar name\n");
			return var;
		}

		if (!Cvar_InfoValidate(value))
		{
			Com_Printf("invalid info cvar value\n");
			return var;
		}
	}

	if (var->latched_string)
	{
		Z_Free(var->latched_string);
		var->latched_string = NULL;
	}

	var->flags = flags;
	value = Cvar_StoredValue(var, flags, value, ibuf, sizeof(ibuf));

	var->string = Cvar_CopyString(var->string, value);
	var->value = (float)atof(var->string);
	var->modified = true;

	if (var->flags & CVAR_USERINFO)
	{
		userinfo_modified = true;
	}

	if (var->changed)
	{
		var->changed(var);
	}

	return var;
}

/* numbers from code: "%i" when integral, "%f" otherwise (SPEC-302 B1) */
void
Cvar_SetValue(const char *var_name, float value)
{
	char val[64];

	if (value == (int)value)
	{
		Com_sprintf(val, sizeof(val), "%i", (int)value);
	}
	else
	{
		Com_sprintf(val, sizeof(val), "%f", value);
	}

	Cvar_Set(var_name, val);
}

/*
 * Any variables with latched values will now be updated
 */
void
Cvar_GetLatchedVars(void)
{
	cvar_t *var;

	for (var = cvar_vars; var; var = var->next)
	{
		if (!var->latched_string)
		{
			continue;
		}

		Z_Free(var->string);
		var->string = var->latched_string;
		var->latched_string = NULL;
		var->value = (float)atof(var->string);

		Cvar_Changed(var);
	}
}

/*
 * Handles variable inspection and changing from the console
 * (SPEC-302 Behaviour 2)
 */
qboolean
Cvar_Command(void)
{
	cvar_t *v;
	const char *value;
	char buf[32];

	v = Cvar_FindVisible(Cmd_Argv(0));

	if (!v)
	{
		return false;
	}

	if (Cmd_Argc() == 1)
	{
		Com_Printf("\"%s\" is \"%s\"\n", v->name, v->string);
		return true;
	}

	value = Cmd_Argv(1);

	if (!(v->flags & CVAR_LITERAL))
	{
		if (!strcmp(value, "!"))
		{
			value = (v->value == 0) ? "1" : "0";
		}
		else if (!strcmp(value, "+"))
		{
			Com_sprintf(buf, sizeof(buf), "%i", (int)(v->value + 1));
			value = buf;
		}
		else if (!strcmp(value, "-"))
		{
			Com_sprintf(buf, sizeof(buf), "%i", (int)(v->value - 1));
			value = buf;
		}
	}

	/* Yamagi: remember a game typed on the console */
	if (strcmp(v->name, "game") == 0)
	{
		Q_strlcpy(userGivenGame, value, sizeof(userGivenGame));
	}

	Cvar_SetVar(v, value, SET_CONSOLE);
	return true;
}

/*
 * set <variable> <value> [u / s / w / i / a / m]
 */
static void
Cvar_Set_f(void)
{
	int c;
	cvar_t *var;

	c = Cmd_Argc();

	if ((c != 3) && (c != 4))
	{
		Com_Printf("usage: set <variable> <value> [u / s / w / i / a / m]\n");
		return;
	}

	var = Cvar_Find(Cmd_Argv(1));

	if (var && (var->flags & CVAR_HIDDEN) && !cvar_hidden_access)
	{
		return; /* hidden cvars cannot be set from the console */
	}

	if (c == 4)
	{
		int flags;
		const char *f = Cmd_Argv(3);

		if (!strcmp(f, "u"))
		{
			flags = CVAR_USERINFO;
		}
		else if (!strcmp(f, "s"))
		{
			flags = CVAR_SERVERINFO;
		}
		else if (!strcmp(f, "w"))
		{
			flags = CVAR_WEAPON;
		}
		else if (!strcmp(f, "i"))
		{
			flags = CVAR_ITEM;
		}
		else if (!strcmp(f, "a"))
		{
			flags = CVAR_AMMO;
		}
		else if (!strcmp(f, "m"))
		{
			flags = CVAR_MISC;
		}
		else
		{
			Com_Printf("flags can only be 'u', 's', 'w', 'i', 'a' or 'm'\n");
			return;
		}

		Cvar_FullSet(Cmd_Argv(1), Cmd_Argv(2), flags);
	}
	else
	{
		Cvar_Set(Cmd_Argv(1), Cmd_Argv(2));
	}
}

/*
 * sset <variable> <parm1> [parm2] [parm3....]
 */
static void
Cvar_SSet_f(void)
{
	char value[MAX_STRING_CHARS];
	int i, c;
	cvar_t *var;

	c = Cmd_Argc();

	if (c < 3)
	{
		Com_Printf("usage: sset <variable> <parm1> [parm2] [parm3....]\n");
		return;
	}

	var = Cvar_Find(Cmd_Argv(1));

	if (var && (var->flags & CVAR_HIDDEN) && !cvar_hidden_access)
	{
		return;
	}

	value[0] = 0;

	for (i = 2; i < c; i++)
	{
		if (i > 2)
		{
			Q_strlcat(value, " ", sizeof(value));
		}

		Q_strlcat(value, Cmd_Argv(i), sizeof(value));
	}

	Cvar_Set(Cmd_Argv(1), value);
}

/*
 * zero <variable>
 */
static void
Cvar_Zero_f(void)
{
	cvar_t *var;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: clear <variable>\n");
		return;
	}

	var = Cvar_Find(Cmd_Argv(1));

	if (var && (var->flags & CVAR_HIDDEN) && !cvar_hidden_access)
	{
		return;
	}

	Cvar_Set(Cmd_Argv(1), "");
}

/*
 * add / or / and / xor <cvar> <n> (SPEC-302 Behaviour 7)
 */
static void
Cvar_Arith_f(void)
{
	const char *op = Cmd_Argv(0);
	const char *name = Cmd_Argv(1);
	cvar_t *var;
	float v;

	if (Cmd_Argc() < 3)
	{
		Com_Printf("Error: Too few parameters\n");
		return;
	}

	var = Cvar_Find(name);

	if (var && (var->flags & CVAR_HIDDEN) && !cvar_hidden_access)
	{
		return;
	}

	v = var ? var->value : 0;

	if (!Q_stricmp(op, "add"))
	{
		Cvar_SetValue(name, v + (float)atof(Cmd_Argv(2)));
	}
	else if (!Q_stricmp(op, "or"))
	{
		Cvar_SetValue(name, (float)((int)v | atoi(Cmd_Argv(2))));
	}
	else if (!Q_stricmp(op, "and"))
	{
		Cvar_SetValue(name, (float)((int)v & atoi(Cmd_Argv(2))));
	}
	else
	{
		Cvar_SetValue(name, (float)((int)v ^ atoi(Cmd_Argv(2))));
	}
}

/*
 * Appends lines containing "set variable value" for all variables
 * with the archive flag set to true (newest-created first).
 */
void
Cvar_WriteVariables(const char *path)
{
	const cvar_t *var;
	FILE *f;

	f = Q_fopen(path, "a");

	if (!f)
	{
		return;
	}

	for (var = cvar_vars; var; var = var->next)
	{
		if (var->flags & CVAR_ARCHIVE)
		{
			fprintf(f, "set %s \"%s\"\n", var->name, var->string);
		}
	}

	fclose(f);
}

/*
 * cvarlist: 13 flag columns, name, quoted value (SPEC-302 "cvarlist output")
 */
static void
Cvar_List_f(void)
{
	static const struct { int bit; char c; } columns[13] = {
		{CVAR_ARCHIVE, '*'}, {CVAR_USERINFO, 'U'}, {CVAR_SERVERINFO, 'S'},
		{CVAR_NOSET, '-'}, {CVAR_LATCH, 'L'}, {CVAR_INTEGER, 'I'},
		{CVAR_LITERAL, 'P'}, {CVAR_HIDDEN, 'N'}, {CVAR_PROTECTED, 'P'},
		{CVAR_WEAPON, 'W'}, {CVAR_ITEM, 'E'}, {CVAR_AMMO, 'A'},
		{CVAR_MISC, 'M'}
	};
	cvar_t *var;
	char col[14];
	int i, n;

	n = 0;

	for (var = cvar_vars; var; var = var->next)
	{
		n++;

		if (var->flags & (CVAR_HIDDEN | CVAR_PROTECTED))
		{
			continue;
		}

		for (i = 0; i < 13; i++)
		{
			col[i] = (var->flags & columns[i].bit) ? columns[i].c : ' ';
		}

		col[13] = 0;

		Com_Printf("%s %s \"%s\"\n", col, var->name, var->string);
	}

	Com_Printf("%i cvars\n", n);
}

/* info string of all cvars with any of the bits (hidden ones included:
   cl_violence is a hidden userinfo cvar) */
char *
Cvar_BitInfo(int bit)
{
	static char info[MAX_INFO_STRING];
	cvar_t *var;

	info[0] = 0;

	for (var = cvar_vars; var; var = var->next)
	{
		if (var->flags & bit)
		{
			Info_SetValueForKey(info, var->name, var->string);
		}
	}

	return info;
}

/*
 * returns an info string containing
 * all the CVAR_USERINFO cvars
 */
char *
Cvar_Userinfo(void)
{
	return Cvar_BitInfo(CVAR_USERINFO);
}

/*
 * returns an info string containing
 * all the CVAR_SERVERINFO cvars
 */
char *
Cvar_Serverinfo(void)
{
	return Cvar_BitInfo(CVAR_SERVERINFO);
}

/* console completion helper: first visible cvar starting with partial */
char *
Cvar_CompleteVariable(char *partial)
{
	cvar_t *cvar;
	size_t len;

	len = strlen(partial);

	if (!len)
	{
		return NULL;
	}

	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (!(cvar->flags & CVAR_HIDDEN) && !strcmp(partial, cvar->name))
		{
			return cvar->name;
		}
	}

	for (cvar = cvar_vars; cvar; cvar = cvar->next)
	{
		if (!(cvar->flags & CVAR_HIDDEN) && !strncmp(partial, cvar->name, len))
		{
			return cvar->name;
		}
	}

	return NULL;
}

void
Cvar_Init(void)
{
	cvar_vars = NULL;
	cvar_count = 0;
	memset(cvar_hash, 0, sizeof(cvar_hash));

	/* registration order of the original (cmdlist shows newest first) */
	Cmd_AddCommand("add", Cvar_Arith_f);
	Cmd_AddCommand("or", Cvar_Arith_f);
	Cmd_AddCommand("and", Cvar_Arith_f);
	Cmd_AddCommand("xor", Cvar_Arith_f);
	Cmd_AddCommand("set", Cvar_Set_f);
	Cmd_AddCommand("sset", Cvar_SSet_f);
	Cmd_AddCommand("zero", Cvar_Zero_f);
	Cmd_AddCommand("cvarlist", Cvar_List_f);
}

/*
 * Free list of cvars
 */
static void
Cvar_FreeList(void)
{
	cvar_t *var;

	for (var = cvar_vars; var;)
	{
		cvar_t *next;

		Z_Free(var->name);
		Z_Free(var->string);
		Z_Free(var->default_string);
		Z_Free(var->latched_string);

		next = var->next;
		Z_Free(var);
		var = next;
	}

	cvar_vars = NULL;
	cvar_count = 0;
	memset(cvar_hash, 0, sizeof(cvar_hash));
}

void
Cvar_Fini(void)
{
	Cvar_FreeList();

	Cmd_RemoveCommand("cvarlist");
	Cmd_RemoveCommand("zero");
	Cmd_RemoveCommand("sset");
	Cmd_RemoveCommand("set");
	Cmd_RemoveCommand("xor");
	Cmd_RemoveCommand("and");
	Cmd_RemoveCommand("or");
	Cmd_RemoveCommand("add");
}
