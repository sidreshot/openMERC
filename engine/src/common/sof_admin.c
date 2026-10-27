/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f server administration commands of the engine:
 * IP filter (filter_add / filter_remove / filter_list / filter_write and
 * the connection test), deathmatch flags (list_dmflags / set_dmflags /
 * unset_dmflags), bots (bot_add / bot_rem / hackkillbots), getgamestats,
 * updateinven, savesleft, gserver, adddownload and the deathmatch map list.
 * The engine string package str_server is loaded here (server start).
 *
 * Implements: SPEC-313 (§3, "Package owners" bit 8), SPEC-302 (command
 *             list: engine (server))
 */

#include "header/common.h"
#include <ctype.h> /* toupper */
#include "../server/header/server.h"

#define FILTER_MAX 1024

typedef struct
{
	unsigned mask;
	unsigned compare;
	qboolean used;
} ipfilter_t;

static ipfilter_t ipfilters[FILTER_MAX];
static int numipfilters;

/* game-module answers (filled by the game-module host, SPEC-001 exports
   0x44 / 0x48 / 0x4C); NULL = no SoF game module loaded */
void (*sof_game_savesleft)(void);
void (*sof_game_getgamestats)(void);
void (*sof_game_updateinven)(void);
/* model-system debug command interpreter (SPEC-010), argument upper-cased */
void (*sof_ghoul_command)(const char *text);

/* ---------------------------------------------------------------------
 * IP filter
 * --------------------------------------------------------------------- */

/* a.b.c.d with missing trailing parts or 0 parts as wildcards */
static qboolean
Filter_Parse(const char *s, ipfilter_t *f)
{
	byte b[4] = {0, 0, 0, 0};
	byte m[4] = {0, 0, 0, 0};
	char num[128];
	int i, j;

	for (i = 0; i < 4; i++)
	{
		if ((*s < '0') || (*s > '9'))
		{
			Com_Printf("Bad filter address: %s\n", s);
			return false;
		}

		j = 0;

		while ((*s >= '0') && (*s <= '9') && (j < (int)sizeof(num) - 1))
		{
			num[j++] = *s++;
		}

		num[j] = 0;
		b[i] = (byte)atoi(num);

		if (b[i] != 0)
		{
			m[i] = 255;
		}

		if (!*s)
		{
			break;
		}

		s++;
	}

	f->mask = *(unsigned *)m;
	f->compare = *(unsigned *)b;

	return true;
}

qboolean
SoF_FilterRefused(const byte ip[4])
{
	unsigned in;
	int i;
	int filterban = (int)Cvar_VariableValue("filterban");

	memcpy(&in, ip, 4);

	for (i = 0; i < numipfilters; i++)
	{
		if (ipfilters[i].used &&
			((in & ipfilters[i].mask) == ipfilters[i].compare))
		{
			return filterban ? true : false;
		}
	}

	return filterban ? false : true;
}

static void
Filter_Add_f(void)
{
	const char *arg;
	char ipbuf[64];
	int i;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage:  filter_add X\nWhere x can be <ip-mask> or <id>\n");
		return;
	}

	arg = Cmd_Argv(1);

	/* a name: the connected client with exactly that name */
	if ((arg[0] < '0') || (arg[0] > '9'))
	{
		const char *ip = NULL;

		for (i = 0; svs.clients && (i < (int)maxclients->value); i++)
		{
			client_t *cl = &svs.clients[i];

			if ((cl->state >= cs_connected) && !strcmp(cl->name, arg))
			{
				ip = Info_ValueForKey(cl->userinfo, "ip");
				break;
			}
		}

		if (!ip || !ip[0])
		{
			Com_Printf("Name %s not found in player list\n", arg);
			return;
		}

		/* OpenMerc uses the found player's address for the new entry;
		   the original's slot-number quirk (SPEC-313 §3) is not copied */
		Q_strlcpy(ipbuf, ip, sizeof(ipbuf));

		if (strchr(ipbuf, ':'))
		{
			*strchr(ipbuf, ':') = 0;
		}

		arg = ipbuf;
	}

	/* a free slot is reused before the list grows */
	for (i = 0; i < numipfilters; i++)
	{
		if (!ipfilters[i].used)
		{
			break;
		}
	}

	if (i == numipfilters)
	{
		if (numipfilters == FILTER_MAX)
		{
			Com_Printf("Filter list is full\n");
			return;
		}

		numipfilters++;
	}

	if (!Filter_Parse(arg, &ipfilters[i]))
	{
		ipfilters[i].used = false; /* the slot stays free */
		return;
	}

	ipfilters[i].used = true;
}

static void
Filter_Remove_f(void)
{
	ipfilter_t f;
	int i;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage:  Filter_remove <ip-mask>\n");
		return;
	}

	if (!Filter_Parse(Cmd_Argv(1), &f))
	{
		return;
	}

	for (i = 0; i < numipfilters; i++)
	{
		if (ipfilters[i].used && (ipfilters[i].mask == f.mask) &&
			(ipfilters[i].compare == f.compare))
		{
			ipfilters[i].used = false;

			while (numipfilters && !ipfilters[numipfilters - 1].used)
			{
				numipfilters--;
			}

			Com_Printf("Removed.\n");
			return;
		}
	}

	Com_Printf("Didn't find %s.\n", Cmd_Argv(1));
}

static void
Filter_List_f(void)
{
	int i;

	Com_Printf("Filter list:\n");

	for (i = 0; i < numipfilters; i++)
	{
		byte b[4];

		if (!ipfilters[i].used)
		{
			continue;
		}

		memcpy(b, &ipfilters[i].compare, 4);
		Com_Printf("%3i.%3i.%3i.%3i\n", b[0], b[1], b[2], b[3]);
	}
}

static void
Filter_Write_f(void)
{
	char name[MAX_OSPATH];
	const char *game = Cvar_VariableString("game");
	FILE *f;
	int i;

	/* relative to the working directory */
	Com_sprintf(name, sizeof(name), "%s/listip.cfg", game[0] ? game : "SOF RETAIL");
	Com_Printf("Writing %s.\n", name);

	f = Q_fopen(name, "wb");

	if (!f)
	{
		Com_Printf("Couldn't open %s\n", name);
		return;
	}

	fprintf(f, "set filterban %d\n", (int)Cvar_VariableValue("filterban"));

	for (i = 0; i < numipfilters; i++)
	{
		byte b[4];

		if (!ipfilters[i].used)
		{
			continue;
		}

		memcpy(b, &ipfilters[i].compare, 4);
		fprintf(f, "Filter_add %i.%i.%i.%i\n", b[0], b[1], b[2], b[3]);
	}

	fclose(f);
}

/* ---------------------------------------------------------------------
 * deathmatch flags: table = string (5, deathmatch - 1) of SV_TEXT
 * --------------------------------------------------------------------- */

static qboolean
DM_Text(const char **text, int *nflags)
{
	int dm = (int)Cvar_VariableValue("deathmatch");
	const char *t, *star;

	if (!dm)
	{
		Com_Printf("%s", SP_GetStringByID(0x050C));
		return false;
	}

	t = SP_GetStringByID(0x0500 | ((dm - 1) & 0xFF));
	star = strrchr(t, '*');
	*nflags = star ? atoi(star + 1) : 0;
	*text = t;

	return true;
}

static void
DM_List(const char *text, int nflags)
{
	int dmflags = (int)Cvar_VariableValue("dmflags");
	char out[8192];
	const char *s;
	int bit = 0;
	size_t o = 0;

	for (s = text; *s && (*s != '*') && (o < sizeof(out) - 2); s++)
	{
		if ((s[0] == '%') && (s[1] == 's'))
		{
			out[o++] = ((bit < nflags) && (dmflags & (1 << bit))) ? '1' : '0';
			bit++;
			s++;
			continue;
		}

		out[o++] = *s;
	}

	out[o] = 0;
	Com_Printf("%s", out);
}

static void
DM_ListFlags_f(void)
{
	const char *text;
	int n;

	if (DM_Text(&text, &n))
	{
		DM_List(text, n);
	}
}

static void
DM_ChangeFlags(qboolean set)
{
	const char *text;
	int n, i, dmflags;

	if (!DM_Text(&text, &n))
	{
		return;
	}

	if (Cmd_Argc() < 2)
	{
		Com_Printf("%s", SP_GetStringByID(set ? 0x050A : 0x050B));
	}
	else
	{
		dmflags = (int)Cvar_VariableValue("dmflags");

		for (i = 1; i < Cmd_Argc(); i++)
		{
			int f = atoi(Cmd_Argv(i));

			if ((f < 1) || (f > n))
			{
				continue;
			}

			if (set)
			{
				dmflags |= 1 << (f - 1);
			}
			else
			{
				dmflags &= ~(1 << (f - 1));
			}
		}

		Cvar_SetValue("dmflags", (float)dmflags);
	}

	DM_List(text, n);
}

static void
DM_SetFlags_f(void)
{
	DM_ChangeFlags(true);
}

static void
DM_UnsetFlags_f(void)
{
	DM_ChangeFlags(false);
}

/* ---------------------------------------------------------------------
 * bots: bot_name0..31, "???" prefix = free slot
 * --------------------------------------------------------------------- */

static qboolean
Bot_SlotFree(int i)
{
	const char *v = Cvar_VariableString(va("bot_name%i", i));

	return !Q_strncasecmp(v, "???", 3);
}

static void
Bot_RandomName(char *out, size_t size)
{
	char *buf, *p;
	const char *tok;
	char names[64][64];
	int n = 0;

	Q_strlcpy(out, "??????", size);

	if (FS_LoadFile("bot/botlist.txt", (void **)&buf) < 0)
	{
		return;
	}

	p = buf;

	while (n < 64)
	{
		tok = COM_Parse(&p);

		if (!p || !tok[0])
		{
			break;
		}

		Q_strlcpy(names[n++], tok, sizeof(names[0]));
	}

	FS_FreeFile(buf);

	if (n)
	{
		Q_strlcpy(out, names[irand(0, n - 1)], size);
	}
}

static void
Bot_Add_f(void)
{
	int count = (int)Cvar_VariableValue("maxclients");
	char name[64];
	int i;

	if (Cmd_Argc() > 1)
	{
		count = atoi(Cmd_Argv(1));
	}

	if (Cmd_Argc() > 2)
	{
		Q_strlcpy(name, Cmd_Argv(2), sizeof(name));
	}
	else
	{
		Bot_RandomName(name, sizeof(name));
	}

	for (i = 0; (i <= count - 3) && (i < 32); i++)
	{
		if (Bot_SlotFree(i))
		{
			Cvar_Set(va("bot_name%i", i), name);
			Com_Printf("Added bot \"%s\".\n", name);
			return;
		}
	}

	Com_Printf("No free slots to add bot.\n");
}

static void
Bot_Rem_f(void)
{
	int count = (int)Cvar_VariableValue("maxclients");
	const char *name = NULL;
	int i;

	if (Cmd_Argc() > 1)
	{
		count = atoi(Cmd_Argv(1));
	}

	if (Cmd_Argc() > 2)
	{
		name = Cmd_Argv(2);
	}

	for (i = count - 2; i >= 0; i--)
	{
		const char *v;
		char removed[64];

		if (i >= 32)
		{
			continue;
		}

		v = Cvar_VariableString(va("bot_name%i", i));

		if (Bot_SlotFree(i))
		{
			continue;
		}

		if (name && Q_stricmp(v, name))
		{
			continue;
		}

		Q_strlcpy(removed, v, sizeof(removed));
		Cvar_Set(va("bot_name%i", i), "??????");
		Com_Printf("Removed bot \"%s\".\n", removed);
		return;
	}

	Com_Printf("No bots to remove.\n");
}

static void
Bot_HackKill_f(void)
{
	int dm = (int)Cvar_VariableValue("deathmatch");
	int i;

	if ((dm == 1) || (dm == 3))
	{
		return;
	}

	for (i = 0; i < 30; i++)
	{
		Cvar_Set(va("bot_name%i", i), "??????");
	}
}

/* ---------------------------------------------------------------------
 * game-module answered commands
 * --------------------------------------------------------------------- */

static void
SV_GetGameStats_f(void)
{
	static const char *stats[10] = {
		"stat_playedtime_base", "stat_guyskilled", "stat_friendlieskilled",
		"stat_throatshots", "stat_nutshots", "stat_headshots", "stat_gibs",
		"stat_savesused", "stat_playedtime", "stat_cashearned"
	};
	int i;

	if (sof_game_getgamestats)
	{
		sof_game_getgamestats();
		return;
	}

	for (i = 0; i < 10; i++)
	{
		Cvar_Set(stats[i], "");
	}
}

static void
SV_UpdateInven_f(void)
{
	if (sof_game_updateinven)
	{
		sof_game_updateinven();
	}
}

static void
SV_SavesLeft_f(void)
{
	if (sof_game_savesleft)
	{
		sof_game_savesleft();
	}
}

static void
SV_GServer_f(void)
{
	char text[1024];
	int i;

	Q_strlcpy(text, Cmd_Args(), sizeof(text));

	for (i = 0; text[i]; i++)
	{
		text[i] = (char)toupper((unsigned char)text[i]);
	}

	if (sof_ghoul_command)
	{
		sof_ghoul_command(text);
	}
}

/* adddownload <file>: kept in a list (the download path is the server's;
   SPEC-302 lists the command without further detail) */
static char *adddownload_list[64];
static int adddownload_count;

static void
SV_AddDownload_f(void)
{
	if (Cmd_Argc() < 2)
	{
		/* usage text observed in the original (black box) */
		Com_Printf("Usage: adddownload <file>\n");
		return;
	}

	if (adddownload_count < 64)
	{
		adddownload_list[adddownload_count++] = CopyString(Cmd_Argv(1));
	}
}

/* ---------------------------------------------------------------------
 * deathmatch map list (SPEC-313 §3 "Map list")
 * --------------------------------------------------------------------- */

void
SoF_BuildMapList(void)
{
	const char *file = Cvar_VariableString("sv_maplistfile");
	char path[MAX_QPATH];
	char list[512];
	char *buf, *p;

	if (!Cvar_VariableValue("deathmatch") || !file[0])
	{
		return;
	}

	Com_sprintf(path, sizeof(path), "maplists/%s.lst", file);

	if (FS_LoadFile(path, (void **)&buf) < 0)
	{
		Com_Error(ERR_DROP, "*************************\nCould not open %s.lst\n*************************\n",
				file);
		return;
	}

	list[0] = 0;
	p = buf;

	while (1)
	{
		char map[MAX_QPATH];
		const char *tok;

		tok = COM_Parse(&p);

		if (!p || !tok[0])
		{
			break;
		}

		Q_strlcpy(map, tok, sizeof(map));
		tok = COM_Parse(&p);

		if (!tok[0])
		{
			if (!p)
			{
				break;
			}

			continue; /* empty description */
		}

		if (FS_LoadFile2(va("maps/%s.bsp", map), NULL, 0) < 0)
		{
			Com_Printf("WARNING: Could not find map %s.bsp\n", map);
			continue;
		}

		if (strlen(list) + 1 + strlen(map) > 511)
		{
			break;
		}

		Q_strlcat(list, ",", sizeof(list));
		Q_strlcat(list, map, sizeof(list));

		if (!p)
		{
			break;
		}
	}

	FS_FreeFile(buf);
	Cvar_Set("sv_maplist", list);
}

/* ---------------------------------------------------------------------
 * registration (after SV_Init)
 * --------------------------------------------------------------------- */

void
SoF_InitAdminCommands(void)
{
	Cmd_AddCommand("updateinven", SV_UpdateInven_f);
	Cmd_AddCommand("getgamestats", SV_GetGameStats_f);
	Cmd_AddCommand("filter_add", Filter_Add_f);
	Cmd_AddCommand("filter_remove", Filter_Remove_f);
	Cmd_AddCommand("filter_list", Filter_List_f);
	Cmd_AddCommand("filter_write", Filter_Write_f);
	Cmd_AddCommand("bot_add", Bot_Add_f);
	Cmd_AddCommand("bot_rem", Bot_Rem_f);
	Cmd_AddCommand("hackkillbots", Bot_HackKill_f);
	Cmd_AddCommand("list_dmflags", DM_ListFlags_f);
	Cmd_AddCommand("set_dmflags", DM_SetFlags_f);
	Cmd_AddCommand("unset_dmflags", DM_UnsetFlags_f);
	Cmd_AddCommand("gserver", SV_GServer_f);
	Cmd_AddCommand("savesleft", SV_SavesLeft_f);
	Cmd_AddCommand("adddownload", SV_AddDownload_f);

	/* the engine-owned server package (SPEC-313 "Package owners") */
	SP_Register("str_server", SP_OWNER_ENGINE);
}
