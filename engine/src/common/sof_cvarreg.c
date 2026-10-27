/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * The cvars and forwarded client commands of the Soldier of Fortune 1.07f
 * engine with their specified defaults and flags (table generated from
 * SPEC-302 by tools/build/gen_sof_cvartable.py). The table is registered
 * after the configuration files (values set there are kept, as with any
 * registration) and before the subsystems register their own cvars, so the
 * SoF defaults win; once everything is up the flags of every table cvar
 * are set back to exactly the specified ones (a Yamagi subsystem may have
 * ORed in others, e.g. archive).
 *
 * Implements: SPEC-302 ("Complete cvar list", "Complete command list",
 *             Behaviour 1, 5)
 */

#include "header/common.h"

typedef struct
{
	const char *name;
	const char *value;
	int flags;
	int clientonly;
	int dedflags;   /* flags on a dedicated server */
} sofcvardef_t;

#include "sof_cvartable.h"

#define NUM_SOF_CVARS ((int)(sizeof(sof_engine_cvars) / sizeof(sof_engine_cvars[0])))

static qboolean sof_cvars_dedicated;

/* Quake II / Yamagi client commands that SoF does not have (SPEC-302
   "Quake II commands absent from a 1.07f client cmdlist"); unknown commands
   still reach the server while connected */
static const char *q2_only_forwards[] = {
	"inven", "use", "drop", "prog", "give", "god", "notarget", "noclip",
	"invuse", "invprev", "invnext", "invdrop", "listentities", "teleport",
	"spawnentity", "spawnonstart", "cycleweap"
};

void
SoF_RegisterEngineCvars(qboolean dedicated)
{
	int i;

	sof_cvars_dedicated = dedicated;

	for (i = 0; i < NUM_SOF_CVARS; i++)
	{
		const sofcvardef_t *d = &sof_engine_cvars[i];

		if (dedicated && d->clientonly)
		{
			continue;
		}

		/* OpenMerc deviation (docs/DEVIATIONS.md "Console always available"):
		   the console is enabled by default ("console" 0 in 1.07f needed
		   +set console 1 on the command line); +set console 0 still turns
		   it off. This does not enable cheats. */
		Cvar_Get(d->name, strcmp(d->name, "console") ? d->value : "1",
			dedicated ? d->dedflags : d->flags);
	}
}

void
SoF_EnforceCvarFlags(void)
{
	int i;

	for (i = 0; i < NUM_SOF_CVARS; i++)
	{
		const sofcvardef_t *d = &sof_engine_cvars[i];
		cvar_t *var;

		if (sof_cvars_dedicated && d->clientonly)
		{
			continue;
		}

		var = Cvar_Find(d->name);

		if (var)
		{
			var->flags = sof_cvars_dedicated ? d->dedflags : d->flags;
		}
	}

	/* lock cvars and cl_violence: always 0 and read-only (sof_locks.c,
	   no parental locks) */
	{
		static const char *locks[] = {
			"lock_deaths", "lock_blood", "lock_gorezones", "lock_sever",
			"lock_textures", "lock_pure", "low_violence"
		};
		cvar_t *var;

		for (i = 0; i < 7; i++)
		{
			if ((var = Cvar_Find(locks[i])) != NULL)
			{
				var->flags = CVAR_NOSET;
			}
		}

		if ((var = Cvar_Find("cl_violence")) != NULL)
		{
			var->flags = CVAR_USERINFO | CVAR_NOSET;
		}
	}
}

void
SoF_RegisterClientForwardCommands(void)
{
	int i;

	if (sof_cvars_dedicated || (dedicated && dedicated->value))
	{
		return;
	}

	for (i = 0; i < (int)(sizeof(q2_only_forwards) / sizeof(q2_only_forwards[0])); i++)
	{
		if (Cmd_Exists(q2_only_forwards[i]))
		{
			Cmd_RemoveCommand(q2_only_forwards[i]);
		}
	}

	for (i = 0; i < (int)(sizeof(sof_forward_commands) / sizeof(sof_forward_commands[0])); i++)
	{
		if (!Cmd_Exists(sof_forward_commands[i]))
		{
			Cmd_AddCommand(sof_forward_commands[i], NULL);
		}
	}
}
