/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Functions the Soldier of Fortune 1.07f executable exports by name for
 * its sound modules (Defsnd.dll, EAXSnd.dll, A3Dsnd.dll import them from
 * "SoF.exe"; OpenMerc's OMsnd looks them up in the executable). The
 * OpenMerc engine executable exports the same names with the argument
 * lists of SPEC-AMEND-001 A, so that it can host the original modules when
 * the executable is named SoF.exe, and OMsnd under any name.
 *
 * Implements: SPEC-004 ("Engine functions imported by name"),
 *             SPEC-AMEND-001 A
 */

#include "header/common.h"

#ifndef DEDICATED_ONLY

/* ---- wrappers where the exported form differs from the engine's ----- */

/* Cvar_Get(name, default, flags): module-facing, hidden cvars invisible */
OM_SOF_VISIBLE cvar_t *
SoFExp_Cvar_Get(const char *name, const char *value, int flags)
{
	return Cvar_GetModule(name, value, flags, NULL);
}

/* FS_LoadFile(name, buffer, unused): the third argument is ignored */
OM_SOF_VISIBLE int
SoFExp_FS_LoadFile(const char *name, void **buffer, int unused)
{
	return FS_SoFLoadFile(name, buffer, unused);
}

/* FS_FOpenFile(name, int *handle): exactly two arguments */
OM_SOF_VISIBLE int
SoFExp_FS_FOpenFile(const char *name, int *handle)
{
	return FS_SoFOpenHandle(name, handle);
}

OM_SOF_VISIBLE void
SoFExp_FS_FCloseFile(int handle)
{
	FS_SoFCloseHandle(handle);
}

/* Z_Malloc(int size): zero-filled, fatal on failure */
OM_SOF_VISIBLE void *
SoFExp_Z_Malloc(int size)
{
	if (size < 0)
	{
		Com_Error(ERR_FATAL, "Z_Malloc: failed on allocation of %i bytes", size + 16);
	}

	/* Z_Malloc is zero-filled and fatal on failure */
	return Z_Malloc(size ? (size_t)size : 1);
}

OM_SOF_VISIBLE void
SoFExp_Z_Free(void *ptr)
{
	if (!ptr)
	{
		return;
	}

	if (!Z_BlockSize(ptr))
	{
		Com_Error(ERR_FATAL, "Z_Free: bad magic");
	}

	Z_Free(ptr);
}

OM_SOF_VISIBLE void
SoFExp_Z_Touch(void *ptr)
{
	if (ptr && !Z_BlockSize(ptr))
	{
		Com_Error(ERR_FATAL, "Z_Touch: bad magic");
	}
}

/* ---- the export table of the executable ------------------------------ */

#if defined(_MSC_VER)
/* x86 C names carry a leading underscore */
#if defined(_M_IX86)
#define OM_EXPORT(name, internal) __pragma(comment(linker, "/EXPORT:" name "=_" internal))
#else
#define OM_EXPORT(name, internal) __pragma(comment(linker, "/EXPORT:" name "=" internal))
#endif

OM_EXPORT("Com_Printf", "Com_Printf")
OM_EXPORT("Com_DPrintf", "Com_DPrintf")
OM_EXPORT("Com_Error", "Com_Error")
OM_EXPORT("Com_sprintf", "Com_sprintf")
OM_EXPORT("Cmd_AddCommand", "Cmd_AddCommand")
OM_EXPORT("Cmd_RemoveCommand", "Cmd_RemoveCommand")
OM_EXPORT("Cmd_Argc", "Cmd_Argc")
OM_EXPORT("Cmd_Argv", "Cmd_Argv")
OM_EXPORT("Cvar_Set", "Cvar_Set")
OM_EXPORT("Cvar_SetValue", "Cvar_SetValue")
OM_EXPORT("FS_FreeFile", "FS_FreeFile")
OM_EXPORT("Sys_Error", "Sys_Error")
OM_EXPORT("irand", "irand")
OM_EXPORT("flrand", "flrand")
OM_EXPORT("VectorLength", "VectorLength")
OM_EXPORT("VectorNormalize", "VectorNormalize")
OM_EXPORT("CL_GetEntitySoundOrigin", "CL_GetEntitySoundOrigin")
OM_EXPORT("Cvar_Get", "SoFExp_Cvar_Get")
OM_EXPORT("FS_LoadFile", "SoFExp_FS_LoadFile")
OM_EXPORT("FS_FOpenFile", "SoFExp_FS_FOpenFile")
OM_EXPORT("FS_FCloseFile", "SoFExp_FS_FCloseFile")
OM_EXPORT("Z_Malloc", "SoFExp_Z_Malloc")
OM_EXPORT("Z_Free", "SoFExp_Z_Free")
OM_EXPORT("Z_Touch", "SoFExp_Z_Touch")
#endif

#endif /* DEDICATED_ONLY */
