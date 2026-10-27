// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - placeholder spawn for the classnames owned by later packages (characters and
// vehicles G2/G4, map objects and light fixtures G7, effects/ambience G7, scripts G6, game-mode objects
// G9, Ghoul glue entities): the classname is logged once per level and the entity stays inert (no model,
// not solid, not linked), so maps load and the classname can still be found by other code.
// Implements: SPEC-006 Behaviour 1 (dispatch), docs/impl/G18.md placeholder list

#include "g_local.h"

#define MAX_LOGGED 512
static const char *g_logged[MAX_LOGGED];
static int g_numlogged;
void G_PlaceholderReset(void) { g_numlogged = 0; }

void SP_Placeholder(edict_t *ent)
{
	bool seen = false;
	for (int i = 0; i < g_numlogged; i++)
		if (!strcmp(g_logged[i], ent->classname)) {
			seen = true;
			break;
		}
	if (!seen && g_numlogged < MAX_LOGGED) {
		g_logged[g_numlogged++] = ent->classname;
		gi.dprintf("G18 placeholder: %s (owned by a later package, kept inert)\n", ent->classname);
	}
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	ent->svflags |= SVF_NOCLIENT;
	ent->s.modelindex = 0;
}
