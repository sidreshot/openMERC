/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1a (engine core). Other packages: include, do not edit.
 *
 * Soldier of Fortune 1.07f string packages (strip/<name>.sp, .spe) at run
 * time: loading with owners, the language rule, colour tokens, lookup by
 * number and by name.
 *
 * Implements: SPEC-131, SPEC-313 (§1 and "String-package keywords")
 */

#ifndef OM_SOF_STRINGS_H
#define OM_SOF_STRINGS_H

/* owners (SPEC-313 "Package owners") */
#define SP_OWNER_CLIENT 1  /* configstrings 1464..1492 */
#define SP_OWNER_SERVER 2  /* game import #47, savegame package list */
#define SP_OWNER_MENUS 4   /* <strings name> in menu pages */
#define SP_OWNER_ENGINE 8  /* items, weapons, general, dm_generic, str_server */

/* entry flags */
#define SP_FLAG_CENTERED 0x01
#define SP_FLAG_TYPEAMATIC 0x02
#define SP_FLAG_CAPTIONED 0x04
#define SP_FLAG_CREDIT 0x08
#define SP_FLAG_LAYOUT 0x10
#define SP_FLAG_ORIGINAL 0x20
#define SP_FLAG_ALWAYS_PRINT 0x40
#define SP_FLAG_BLINK 0x80

#define SP_MAX_PACKAGES 256
#define SP_MAX_ENTRIES 256

/* creates sp_language and sv_show_strip; called once at start */
void SP_Init(void);
void SP_Shutdown(void);

/* loads strip/<name>.spe or strip/<name>.sp (or adds the owner bit when a
   package with the same ID is loaded); a missing file drops the level with
   "Could not open string package '<name>'". Returns the package ID or -1. */
int SP_Register(const char *name, int owner);

/* releases the owner bit from every package; packages without owners are
   unloaded */
void SP_Unregister(int owner);

/* text of string id (package ID << 8) | slot; a package that is not
   loaded drops the level ("String package not registered for ID ..."),
   an empty slot gives "" */
const char *SP_GetStringByID(int id);
/* entry flags of a string id (0 when unknown) */
int SP_GetFlagsByID(int id);

/* text of a reference PACKAGE_ENTRY (the text between ^ ^ in menus); ""
   when nothing matches */
const char *SP_GetStringByName(const char *reference);

/* registered packages that have the owner bit (for the SPNM/SPID chunks):
   count, and the registration name of the i-th one */
int SP_CountOwned(int owner);
const char *SP_NameOwned(int owner, int index);

/* reloads every package (sp_language change) */
void SP_ReloadAll(void);

/* OpenMerc MENU2: true while Russian texts are taken (om_language) */
qboolean SP_RussianActive(void);

#endif
