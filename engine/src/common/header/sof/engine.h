/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1a (engine core). Other packages: include, do not edit.
 *
 * Soldier of Fortune 1.07f engine core services that have no Quake II
 * counterpart: cvar extensions, the shared random generator, the user
 * (write) directory, configuration files, the parental locks, CPU info and
 * the server administration helpers.
 *
 * Implements: SPEC-300, SPEC-301, SPEC-302, SPEC-313
 */

#ifndef OM_SOF_ENGINE_H
#define OM_SOF_ENGINE_H

#include "../shared.h"

/* ---- cvars (SPEC-302) ------------------------------------------------- */

/* Cvar_Get with the SoF change callback (game import #87, renderer slot
   0x40). The callback runs when the cvar is created, when it is registered
   again with a callback, and after every change of its value. */
cvar_t *Cvar_GetCB(const char *name, const char *value, int flags,
		cvarcallback_t changed);

/* module-facing lookups: hidden cvars (0x80) stay invisible unless the
   module switched hidden access on (renderer slot 0x4c) */
cvar_t *Cvar_GetModule(const char *name, const char *value, int flags,
		cvarcallback_t changed);
void Cvar_SetHiddenAccess(qboolean allow);

/* forced set that respects CVAR_PROTECTED (save loading, module force-set) */
cvar_t *Cvar_ForceSetUnprotected(const char *name, const char *value);

/* find a cvar by name ("matrix" = "timescale"); privileged, sees hidden */
cvar_t *Cvar_Find(const char *name);

/* info string of all cvars having any of the flag bits (game import #91) */
char *Cvar_BitInfo(int mask);

/* number of cvars (including hidden and protected ones) */
int Cvar_Count(void);

/* the table of SoF engine cvars (SPEC-302 "Complete cvar list", rows
   "Registered by engine"): registered after the config files with their
   specified defaults and flags; SoF_EnforceCvarFlags sets the flags of
   every table cvar back to exactly the specified ones once all
   subsystems have registered theirs */
void SoF_RegisterEngineCvars(qboolean dedicated);
void SoF_EnforceCvarFlags(void);
/* client commands that are forwarded to the server ("cmd <line>") */
void SoF_RegisterClientForwardCommands(void);

/* console TAB cycling (SPEC-313 §10): the match after previous, NULL =
   first; exact matches, then prefix matches, commands / aliases / cvars,
   no hidden cvars, wrapping; NULL when nothing matches */
const char *Cmd_CompleteNext(const char *prefix, const char *previous);

/* ---- random numbers (SPEC-313 §9, exports flrand / irand) -------------- */

void SoF_SeedRandom(unsigned seed);
float flrand(float min, float max);
int irand(int min, int max);

/* ---- filesystem extensions (SPEC-300) ----------------------------------- */

/* the user directory: write directory of configs, saves and the log */
const char *FS_Userdir(void);
/* last directory mounted (screenshots, demos, downloads) */
const char *FS_Gamedir(void);
/* the exported SoF file services (SPEC-004 / SPEC-AMEND-001) */
int FS_SoFOpenHandle(const char *name, int *handle);
void FS_SoFCloseHandle(int handle);
int FS_SoFLoadFile(const char *name, void **buffer, int unused);
/* paklist.txt for fs_createpak (called at program exit) */
void FS_WritePakList(void);
/* host path of a file name in the given root (case-insensitive on hosts
   with case-sensitive filesystems); returns false when there is no file */
qboolean FS_HostFileExists(const char *path);

/* ---- startup / configuration (SPEC-301) ---------------------------------- */

#define SOF_VERSION_STRING "1.07fx86F"

/* writes <user>/config.cfg (keysonly: <user>/configs/CURRENT.cfg) */
void Com_WriteConfig(qboolean keysonly);
/* the autoexec.cfg rule: queued when <basedir>/<gamedir>/autoexec.cfg exists */
void Com_ExecAutoexec(void);

/* developer bits (SPEC-302 developer cvar): 1, 2, 4 */
qboolean Com_DeveloperBit(int bit);

/* commands of the start sequence */
void Com_Error_f(void);
void Com_Subliminal_f(void);
void SoF_InitSystemCommands(void);
void SoF_InitClientCommands(void);
void SoF_InitDedicatedKeys(void); /* DEDICATED_ONLY builds */
void SoF_PrintLocalAddresses(void);
/* om_delay test helper: queued lines that are due go to the command buffer */
void SoF_RunDelayed(void);

/* ---- violence settings: no parental locks (sof_locks.c) ------------------ */

void SoF_InitLocks(void);

/* ---- CPU report (SPEC-302 §8) -------------------------------------------- */

void SoF_CpuInfo(qboolean print);

/* ---- server administration (SPEC-313 §3) --------------------------------- */

void SoF_InitAdminCommands(void);
/* address filter: true when the address (a.b.c.d bytes) is refused */
qboolean SoF_FilterRefused(const byte ip[4]);
/* map list of the deathmatch rotation (called when a map starts) */
void SoF_BuildMapList(void);

#endif
