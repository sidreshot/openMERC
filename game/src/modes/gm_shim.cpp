// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - default implementations of the game-core services the modes use (G9). Each is bound
// with the MSVC linker option /alternatename, so a real definition elsewhere in the module (G18) replaces it
// without a link conflict. Other compilers use weak symbols.
// Implements: SPEC-780 5 (player information configstring, default composition), SPEC-790 B11 (meter source)
#include "gm_internal.h"

extern "C" {

float GM_Default_StealthValue(void) { return 0.0f; }

// name\team\model\N (SPEC-780 5) from the client fields of the game core
void GM_Default_SetPlayerInfo(edict_t *ent)
{
	if (!GM_IsClient(ent))
		return;
	gclient_t *cl = ent->client;
	const char *model = cl->modelname[0] ? cl->modelname : "mullins";
	const char *team = cl->teamname[0] ? cl->teamname : model;
	char s[MAX_QPATH * 4];
	Com_sprintf(s, sizeof(s), "%s\\%s\\%s\\%d", cl->pers.netname, team, model, cl->resp.team - 1);
	gix.configstring(CS_PLAYERSKINS + GM_Slot(ent), s);
}

} // extern "C"

#if defined(_MSC_VER)
#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_G_StealthValue=_GM_Default_StealthValue")
#pragma comment(linker, "/alternatename:_G_SetPlayerInfo=_GM_Default_SetPlayerInfo")
#else
#pragma comment(linker, "/alternatename:G_StealthValue=GM_Default_StealthValue")
#pragma comment(linker, "/alternatename:G_SetPlayerInfo=GM_Default_SetPlayerInfo")
#endif
#else
extern "C" __attribute__((weak)) float G_StealthValue(void) { return GM_Default_StealthValue(); }
extern "C" __attribute__((weak)) void G_SetPlayerInfo(edict_t *ent) { GM_Default_SetPlayerInfo(ent); }
#endif
