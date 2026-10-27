/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: string packages used by menu pages - <strings name> loads a
 * package with the "menus" owner bit, ^REF^ references and localised key
 * names are looked up by name, and every menu-owned package is released when
 * the menu system closes. The string-package runtime itself is the engine's
 * (package I1a, common/sof_strings.c).
 *
 * Implements: SPEC-800 (section 5.2 <strings>), SPEC-313 (section 1, menu owner)
 */

#include "header/menu.h"
#include "../../common/header/sof/strings.h"

void
MSP_Register(const char *name)
{
	SP_Register(name, SP_OWNER_MENUS);
}

/*
 * OpenMerc MENU2 (docs/DEVIATIONS.md "Menu text fallback", "Own menu wording"):
 * OpenMerc's own strip/menu_omovr.sp and strip/menu_omfill.sp (texts written
 * from scratch) are
 * registered on first need with the menu owner. A reference is looked up
 * first as OMOVR_<reference> (own wording that replaces a stock text, e.g.
 * the settings tab tips) and, when it is missing in the packages of the
 * install, once more as OMFILL_<reference> (bot menu, the unshipped
 * Control/Conquer/CTB modes, player dossiers). developer 2 prints every
 * looked-up reference.
 */
static qboolean msp_fill_tried; /* own packages registered */

static void
MSP_LoadOwn(void)
{
	if (msp_fill_tried)
	{
		return;
	}

	msp_fill_tried = true;

	/* SP_Register drops when the file is missing */
	if (FS_FileExists("strip/menu_omovr.sp"))
	{
		SP_Register("menu_omovr", SP_OWNER_MENUS);
	}

	if (FS_FileExists("strip/menu_omfill.sp"))
	{
		SP_Register("menu_omfill", SP_OWNER_MENUS);
	}

	/* MENU-SCRATCH: labels of the wide layout (m_wide.c), also on screens
	   without the navigation rail (whose page loads it as well) */
	if (FS_FileExists("strip/menu_omset.sp"))
	{
		SP_Register("menu_omset", SP_OWNER_MENUS);
	}

	/* MENU-SCRATCH: the own folder pages, dialogs and key actions
	   (MENU_OMSET_PG_*; a package holds 256 entries) */
	if (FS_FileExists("strip/menu_ompg.sp"))
	{
		SP_Register("menu_ompg", SP_OWNER_MENUS);
	}

	/* MENU-SCRATCH: the server list's Pure column (m_data.c) reads
	   MENU_DM_GENERIC_PURE_YES/NO, and the 1.07f join page loads no package
	   with them: the column stayed empty */
	if (FS_FileExists("strip/menu_dm_generic.sp"))
	{
		SP_Register("menu_dm_generic", SP_OWNER_MENUS);
	}
}

const char *
MSP_Find(const char *ref)
{
	/* callers pass va() results: keep the reference before va() is used again */
	char name[256], key[272];
	const char *s;

	if (!ref || !ref[0])
	{
		return "";
	}

	Q_strlcpy(name, ref, sizeof(name));
	MSP_LoadOwn();

	Com_sprintf(key, sizeof(key), "OMOVR_%s", name);
	s = SP_GetStringByName(key);

	if (!s || !s[0])
	{
		s = SP_GetStringByName(name);
	}

	if (!s || !s[0])
	{
		Com_sprintf(key, sizeof(key), "OMFILL_%s", name);
		s = SP_GetStringByName(key);
	}

	if (developer && developer->value >= 2)
	{
		Com_Printf("MSP %s = %s\n", name, s ? s : "");
	}

	return s ? s : "";
}
void
MSP_ReleaseMenu(void)
{
	SP_Unregister(SP_OWNER_MENUS);
	msp_fill_tried = false;
}

void
MSP_Init(void)
{
}
