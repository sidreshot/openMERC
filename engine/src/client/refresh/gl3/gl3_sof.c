/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer as a Soldier of Fortune renderer module (ref_omgl.dll,
 * x86 Windows): GetRefAPI with the 108-byte import table and the 220-byte
 * export table of SPEC-002. The engine's services are adapted to the
 * renderer core's Quake II-style import structure; Init, Shutdown,
 * BeginFrame, EndFrame and AppActivate drive the Win32 window and context
 * (gl3_win32.c); card detection runs the driver configuration files.
 *
 * Implements: SPEC-002, SPEC-413 §1, §4, SPEC-411 §1, SPEC-AMEND-002 1.1
 */

#ifdef OM_SOFREF

#include <stdarg.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "header/local.h"

refimport_t ri;
static sofrefimport_t si;

/* used by gl3_sofcvar.c and gl3_win32.c */
cvar_t *(*gl3_CvarGetCB)(const char *name, const char *value, int flags, void (*cb)(cvar_t *));
void (*gl3_NewWindow)(int width, int height);

extern void GL3_Win32_SetEngine(void *hinstance, void *wndproc);
extern qboolean GL3_Win32_InitGraphics(int fullscreen, int *pwidth, int *pheight);
extern void GL3_Win32_Shutdown(void);
extern void GL3_Win32_AppActivate(int active);
extern void GL3_BeginFrame(float camera_separation);

/* ------------------------------------------------------------------
 * engine services adapted to refimport_t
 * ------------------------------------------------------------------ */

static void
Ri_Sys_Error(int err_level, const char *fmt, ...)
{
	char text[2048];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);
	si.Sys_Error(err_level, "%s", text);
}

static void
Ri_Cmd_AddCommand(const char *name, void (*cmd)(void))
{
	si.Cmd_AddCommand((char *)name, cmd);
}

static void
Ri_Cmd_RemoveCommand(const char *name)
{
	si.Cmd_RemoveCommand((char *)name);
}

static int
Ri_Cmd_Argc(void)
{
	return si.Cmd_Argc();
}

static char *
Ri_Cmd_Argv(int i)
{
	return si.Cmd_Argv(i);
}

static void
Ri_Cmd_ExecuteText(int exec_when, char *text)
{
	si.Cmd_ExecuteText(exec_when, text);
}

static void
Ri_Com_VPrintf(int level, const char *fmt, va_list ap)
{
	char text[4096];
	int sof;

	vsnprintf(text, sizeof(text), fmt, ap);

	/* Quake II print levels -> SPEC-002 levels (0 all, 1 developer, 3 alert) */
	switch (level)
	{
		case PRINT_DEVELOPER:
			sof = SOF_PRINT_DEVELOPER;
			break;
		case PRINT_ALERT:
			sof = SOF_PRINT_ALERT;
			break;
		default:
			sof = SOF_PRINT_ALL;
			break;
	}

	si.Con_Printf(sof, "%s", text);
}

static int
Ri_FS_LoadFile(const char *name, void **buf)
{
	void *tmp = NULL;
	int len;

	if (!buf)
	{
		len = si.FS_LoadFile((char *)name, &tmp, 0);

		if (tmp)
		{
			si.FS_FreeFile(tmp);
		}

		return len;
	}

	return si.FS_LoadFile((char *)name, buf, 0);
}

static void
Ri_FS_FreeFile(void *buf)
{
	si.FS_FreeFile(buf);
}

static const char *
Ri_FS_Gamedir(void)
{
	return si.FS_Gamedir();
}

static cvar_t *
Ri_Cvar_Get(const char *name, const char *value, int flags)
{
	return (cvar_t *)si.Cvar_Get((char *)name, (char *)value, flags, NULL);
}

static cvar_t *
CvarGetCB(const char *name, const char *value, int flags, void (*cb)(cvar_t *))
{
	return (cvar_t *)si.Cvar_Get((char *)name, (char *)value, flags, (void (*)(sofcvar_t *))cb);
}

static cvar_t *
Ri_Cvar_Set(const char *name, const char *value)
{
	return (cvar_t *)si.Cvar_Set((char *)name, (char *)value);
}

static void
Ri_Cvar_SetValue(const char *name, float value)
{
	si.Cvar_SetValue((char *)name, value);
}

static qboolean
Ri_Vid_GetModeInfo(int *width, int *height, int mode)
{
	return (si.Vid_GetModeInfo(width, height, mode) & 0xFF) != 0;
}

static void
Ri_Vid_MenuInit(void)
{
}

static void
Ri_Vid_WriteScreenshot(int width, int height, int comp, const void *data)
{
	(void)width; (void)height; (void)comp; (void)data;
}

static qboolean
Ri_GLimp_InitGraphics(int fullscreen, int *pwidth, int *pheight)
{
	Com_Printf("Initializing OpenGL display\n");
	Com_Printf("...setting mode %d: %d %d %s\n", (int)gl_mode->value, *pwidth, *pheight, fullscreen ? "FS" : "W");
	return GL3_Win32_InitGraphics(fullscreen, pwidth, pheight);
}

static qboolean
Ri_GLimp_GetDesktopMode(int *pwidth, int *pheight)
{
	(void)pwidth; (void)pheight;
	return false;
}

static void
Ri_Vid_RequestRestart(ref_restart_t rs)
{
	if (rs == RESTART_FULL)
	{
		GL3_SoF_RequestRestart();
	}
}

static void
NewWindow(int width, int height)
{
	si.Vid_NewWindow(width, height);
}

static const char *
UserDir(void)
{
	return si.FS_Userdir();
}

static qboolean
NextFreeName(char *path)
{
	return (si.FS_NextFreeName(path) & 0xFF) != 0;
}

/* SPEC-413 §6 Begin frame step 1: gl_mode / vid_fullscreen changes restart the renderer */
void
GL3_SoF_RequestRestart(void)
{
	cvar_t *vid_ref = ri.Cvar_Get("vid_ref", "gl", CVAR_ARCHIVE);

	gl_mode->modified = false;
	vid_fullscreen->modified = false;
	vid_ref->modified = true;
}

/* ------------------------------------------------------------------
 * card detection and driver configuration (SPEC-413 §4)
 * ------------------------------------------------------------------ */

static void
DetectCard(void)
{
	char *buf = NULL, *p, renderer[512];
	char *tokens[256];
	int len, n = 0, i;
	char oldcard[64], oldmem[32];

	len = ri.FS_LoadFile("drivers/cards.txt", (void **)&buf);

	if (!buf)
	{
		Com_Printf("Error: Could not find card definition file\n");
		return;
	}

	Q_strlcpy(oldcard, vid_card->string, sizeof(oldcard));
	Q_strlcpy(oldmem, cpu_memory_using->string, sizeof(oldmem));
	vid_card->modified = false;
	cpu_memory_using->modified = false;

	/* tokens separated by ';', CR, LF, tab or space */
	p = buf;

	while ((p < buf + len) && (n < 256))
	{
		while ((p < buf + len) && (*p == ';' || *p == 13 || *p == 10 || *p == 9 || *p == ' ' || *p == 0))
		{
			*p++ = 0;
		}

		if (p >= buf + len)
		{
			break;
		}

		tokens[n++] = p;

		while ((p < buf + len) && !(*p == ';' || *p == 13 || *p == 10 || *p == 9 || *p == ' ' || *p == 0))
		{
			p++;
		}
	}

	Q_strlcpy(renderer, gl3config.renderer_string ? gl3config.renderer_string : "", sizeof(renderer));
	Q_strlwr(renderer);

	for (i = 0; i + 1 < n; i += 2)
	{
		char pattern[256], *s, *comma;
		qboolean match = true;

		Q_strlcpy(pattern, tokens[i + 1], sizeof(pattern));
		Q_strlwr(pattern);

		for (s = pattern; s && *s; s = comma ? comma + 1 : NULL)
		{
			comma = strchr(s, ',');

			if (comma)
			{
				*comma = 0;
			}

			if (*s && !strstr(renderer, s))
			{
				match = false;
				break;
			}
		}

		if (match)
		{
			ri.Cvar_Set("vid_card", tokens[i]);
			break;
		}
	}

	ri.Cvar_Set("cpu_memory_using", cpu_memory_level->string);
	ri.FS_FreeFile(buf);

	if (strcmp(oldcard, vid_card->string) || strcmp(oldmem, cpu_memory_using->string))
	{
		char cmd[MAX_QPATH + 16];

		ri.Cmd_ExecuteText(EXEC_NOW, "exec drivers/alldefs.cfg\n");
		Com_sprintf(cmd, sizeof(cmd), "exec drivers/%s.cfg\n", vid_card->string);
		ri.Cmd_ExecuteText(EXEC_NOW, cmd);
		Com_sprintf(cmd, sizeof(cmd), "exec drivers/memory%d.cfg\n", (int)cpu_memory_level->value);
		ri.Cmd_ExecuteText(EXEC_NOW, cmd);
		Com_sprintf(cmd, sizeof(cmd), "exec drivers/cpu%d.cfg\n", (int)cpu_speed_level->value);
		ri.Cmd_ExecuteText(EXEC_NOW, cmd);
	}
}

/* ------------------------------------------------------------------
 * module-only export slots
 * ------------------------------------------------------------------ */

extern qboolean GL3_InitForSoF(void);

static int
SoFRef_Init(void *hinstance, void *wndproc, void *ghoul)
{
	GL3_Win32_SetEngine(hinstance, wndproc);
	gl3_ghoul = ghoul;

	if (!GL3_InitForSoF())
	{
		GL3_Win32_Shutdown();
		return -1;
	}

	DetectCard();
	GL3_SoF_GhoulStartup();
	return 1;
}

extern void GL3_Shutdown(void);

static void
SoFRef_Shutdown(void)
{
	Com_Printf("R_Shutdown() - Shutting down\n");
	GL3_SoF_GhoulShutdown();
	GL3_Shutdown();
	GL3_Win32_Shutdown();
}

static cvar_t *r_om_ghltrace;

static void
SoFRef_BeginFrame(void)
{
	/* debugging: r_om_ghltrace 1 logs the Ghoul system's GL calls of one frame */
	if (!r_om_ghltrace)
	{
		r_om_ghltrace = ri.Cvar_Get("r_om_ghltrace", "0", 0);
	}

	if (r_om_ghltrace->value)
	{
		GL3_Ghoul_Trace(true, "omgl_ghltrace.txt");
	}
	else
	{
		/* I1c (test aid): a request file "omghl.req" (first line = output path), written by a test harness
		   at a chosen moment, traces this frame's Ghoul GL calls too (the console is not reachable then) */
		FILE *rq = fopen("omghl.req", "rb");

		if (rq)
		{
			char path[MAX_PATH];
			size_t n = fread(path, 1, sizeof(path) - 1, rq);

			fclose(rq);
			DeleteFileA("omghl.req");

			while ((n > 0) && ((path[n - 1] == '\n') || (path[n - 1] == '\r') || (path[n - 1] == ' ')))
			{
				n--;
			}

			path[n] = 0;

			if (n > 0)
			{
				GL3_Ghoul_Trace(true, path);
				ri.Cvar_Set("r_om_ghltrace", "1"); /* EndFrame closes the trace and clears the cvar */
			}
		}
	}

	GL3_BeginFrame(0);
}

static void
SoFRef_EndFrame(void)
{
	GL3_EndFrame();

	if (r_om_ghltrace && r_om_ghltrace->value)
	{
		GL3_Ghoul_Trace(false, NULL);
		ri.Cvar_Set("r_om_ghltrace", "0");
	}
}

static void
SoFRef_AppActivate(int active)
{
	GL3_Win32_AppActivate(active);
}

/*
 * Crash report of this module: when an access violation or similar happens
 * at an address inside ref_omgl.dll, the offset is written to omgl_crash.txt
 * (map it with the linker map file). Only this module's addresses are
 * reported; the exception continues to the engine's handlers.
 */
static PVOID crashhandler;

static LONG CALLBACK
CrashHandler(PEXCEPTION_POINTERS info)
{
	HMODULE self = NULL;
	ULONG_PTR addr = (ULONG_PTR)info->ExceptionRecord->ExceptionAddress;
	DWORD code = info->ExceptionRecord->ExceptionCode;

	if ((code != EXCEPTION_ACCESS_VIOLATION) && (code != EXCEPTION_INT_DIVIDE_BY_ZERO) &&
		(code != EXCEPTION_ILLEGAL_INSTRUCTION) && (code != EXCEPTION_STACK_OVERFLOW))
	{
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCSTR)CrashHandler, &self))
	{
		FILE *f = fopen("omgl_crash.txt", "a");

		if (f)
		{
			ULONG_PTR base = (ULONG_PTR)self;
			int i;

			fprintf(f, "exception %08lx at %08lx (module base %08lx, offset %08lx%s)\n", (unsigned long)code,
				(unsigned long)addr, (unsigned long)base, (unsigned long)(addr - base),
				((addr >= base) && (addr - base < 0x1000000)) ? ", in ref_omgl" : "");

			/* return addresses on the stack that point into this module */
#ifdef _M_IX86
			{
				ULONG_PTR *sp = (ULONG_PTR *)info->ContextRecord->Esp;

				for (i = 0; i < 256; i++)
				{
					ULONG_PTR v;

					if (IsBadReadPtr(sp + i, sizeof(*sp)))
					{
						break;
					}

					v = sp[i];

					if ((v >= base) && (v - base < 0x200000))
					{
						fprintf(f, "  stack %d: offset %08lx\n", i, (unsigned long)(v - base));
					}
				}
			}
#endif
			fclose(f);
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI
DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
	(void)inst;
	(void)reserved;

	if ((reason == DLL_PROCESS_DETACH) && crashhandler)
	{
		RemoveVectoredExceptionHandler(crashhandler);
		crashhandler = NULL;
	}

	return TRUE;
}

/* SPEC-002: the import table by value, the export table returned by value */
__declspec(dllexport) sofrefexport_t
GetRefAPI(sofrefimport_t imp)
{
	sofrefexport_t re;

	if (!crashhandler)
	{
		crashhandler = AddVectoredExceptionHandler(1, CrashHandler);
	}

	si = imp;

	memset(&ri, 0, sizeof(ri));
	ri.Sys_Error = (void (*)(int, const char *, ...))Ri_Sys_Error;
	ri.Cmd_AddCommand = Ri_Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Ri_Cmd_RemoveCommand;
	ri.Cmd_Argc = Ri_Cmd_Argc;
	ri.Cmd_Argv = Ri_Cmd_Argv;
	ri.Cmd_ExecuteText = Ri_Cmd_ExecuteText;
	ri.Com_VPrintf = Ri_Com_VPrintf;
	ri.FS_LoadFile = Ri_FS_LoadFile;
	ri.FS_FreeFile = Ri_FS_FreeFile;
	ri.FS_Gamedir = Ri_FS_Gamedir;
	ri.Cvar_Get = Ri_Cvar_Get;
	ri.Cvar_Set = Ri_Cvar_Set;
	ri.Cvar_SetValue = Ri_Cvar_SetValue;
	ri.Vid_GetModeInfo = Ri_Vid_GetModeInfo;
	ri.Vid_MenuInit = Ri_Vid_MenuInit;
	ri.Vid_WriteScreenshot = Ri_Vid_WriteScreenshot;
	ri.GLimp_InitGraphics = Ri_GLimp_InitGraphics;
	ri.GLimp_GetDesktopMode = Ri_GLimp_GetDesktopMode;
	ri.Vid_RequestRestart = Ri_Vid_RequestRestart;

	gl3_CvarGetCB = CvarGetCB;
	gl3_NewWindow = NewWindow;
	gl3_userdir_fn = UserDir;
	gl3_nextfreename_fn = NextFreeName;
	gl3_flrand_fn = si.flrand; /* I4c: terrain sets draw from the engine's flrand (SPEC-410) */

	/* the engine's direction table: the 250 entries of SPEC-201 Appendix A (SPEC-AMEND-005 I4a.4; I4c);
	   indices 250-254 have no entry and sample straight down like 255 */
	GL3_SoF_SetDirTable(si.dirtable, 250);

	/* I4b: the engine's world ray test goes to the Ghoul shadow code (SPEC-411 §5 step 5) */
	{
		extern float (*gl3_ghoul_trace)(const float *start, const float *end);

		gl3_ghoul_trace = (float (*)(const float *, const float *))si.TraceFraction;
	}

	re = *GL3_SoF_ExportTable(true);
	re.Init = SoFRef_Init;
	re.Shutdown = SoFRef_Shutdown;
	re.BeginFrame = SoFRef_BeginFrame;
	re.EndFrame = SoFRef_EndFrame;
	re.AppActivate = SoFRef_AppActivate;

	return re;
}

#endif /* OM_SOFREF */
