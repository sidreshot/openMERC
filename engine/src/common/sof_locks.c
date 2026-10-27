/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Violence settings. openMERC has no parental locks (docs/DEVIATIONS.md
 * "No parental locks"): the game always runs with full violence - deaths,
 * blood, gore zones, dismemberment, all textures. The six lock cvars that
 * game and renderer modules read (SPEC-313 "Parental-lock cvars") stay
 * registered, read-only and 0; nothing reads the original's lock store
 * (registry), a password or the system language, and configs or old saved
 * values cannot switch a lock on.
 *
 * Implements: SPEC-301 (Behaviour 1 step 11, replaced as above)
 */

#include "header/common.h"

static const char *lock_names[] = {
	"lock_deaths", "lock_blood", "lock_gorezones", "lock_sever",
	"lock_textures", "lock_pure"
};

void
SoF_InitLocks(void)
{
	int i;

	for (i = 0; i < (int)(sizeof(lock_names) / sizeof(lock_names[0])); i++)
	{
		Cvar_Get(lock_names[i], "0", CVAR_NOSET);
		Cvar_ForceSet(lock_names[i], "0");
	}

	Cvar_Get("low_violence", "0", CVAR_NOSET);
	Cvar_ForceSet("low_violence", "0");

	/* no lock bits: cl_violence 0; sv_violence 0 plus the pure-server bit the
	   server sets at the end of every spawn (SV_SetPureViolenceBit) */
	Cvar_Get("cl_violence", "0", CVAR_USERINFO | CVAR_NOSET);
	Cvar_ForceSet("cl_violence", "0");
	Cvar_Get("sv_violence", "0", CVAR_SERVERINFO | CVAR_LATCH);

	if (!Com_ServerState())
	{
		Cvar_ForceSet("sv_violence", "0");
	}
}
