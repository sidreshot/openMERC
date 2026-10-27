// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc G18 - development diagnostic, not a behaviour of the original module: when the cvar g18_crashlog
// is 1, faults (access violation, illegal instruction, divide by zero, stack overflow) whose faulting
// address or stack holds code addresses of this module are appended to g18crash.log in the working
// directory, as module offsets that can be looked up in the linker map (build-.../gamex86.map).
// Implements: (diagnostics only, no spec)

#include "g_local.h"

#ifndef _WIN32
// Linux/other: no fault logger (the engine's own signal handler prints a backtrace); addresses are
// described as module+offset through dladdr.
#include <dlfcn.h>

void G_DescribeAddress(const void *addr, char *out, int len)
{
	Dl_info info;
	if (dladdr(addr, &info) && info.dli_fname) {
		const char *s = strrchr(info.dli_fname, '/');
		Com_sprintf(out, len, "%s+0x%lx", s ? s + 1 : info.dli_fname,
		            (unsigned long)((uintptr_t)addr - (uintptr_t)info.dli_fbase));
	} else
		Com_sprintf(out, len, "?+0x%lx", (unsigned long)(uintptr_t)addr);
}

void G_CrashLogInit(void) {}
void G_CrashLogShutdown(void) {}

#else

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static PVOID crash_handler;
static uintptr_t mod_base, mod_end;
static int crash_logged;

static bool InModule(uintptr_t a)
{
	return a >= mod_base && a < mod_end;
}

static void ModuleOf(uintptr_t a, char *out, int len, uintptr_t *base)
{
	HMODULE h = NULL;
	out[0] = 0;
	*base = 0;
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                       (LPCSTR)a, &h) && h) {
		char path[MAX_PATH];
		if (GetModuleFileNameA(h, path, sizeof(path))) {
			const char *s = strrchr(path, '\\');
			Com_sprintf(out, len, "%s", s ? s + 1 : path);
		}
		*base = (uintptr_t)h;
	}
}

// "module+offset" of a code address (diagnostics)
void G_DescribeAddress(const void *addr, char *out, int len)
{
	char mod[64];
	uintptr_t base;
	ModuleOf((uintptr_t)addr, mod, sizeof(mod), &base);
	Com_sprintf(out, len, "%s+0x%x", mod[0] ? mod : "?", (unsigned)((uintptr_t)addr - base));
}

static LONG CALLBACK G18_CrashHandler(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;
	if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
	    code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
	    code != EXCEPTION_PRIV_INSTRUCTION)
		return EXCEPTION_CONTINUE_SEARCH;
	if (crash_logged >= 6)
		return EXCEPTION_CONTINUE_SEARCH;

	CONTEXT *c = ep->ContextRecord;
	uintptr_t eip = c->Eip, esp = c->Esp;

	// return addresses into this module on the stack (readable part only, at most 32 KB)
	uintptr_t frames[32];
	int nframes = 0;
	MEMORY_BASIC_INFORMATION mbi;
	if (VirtualQuery((LPCVOID)esp, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT) {
		uintptr_t top = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
		if (top > esp + 0x8000)
			top = esp + 0x8000;
		for (uintptr_t p = esp; p + 4 <= top && nframes < 32; p += 4) {
			uintptr_t v = *(uintptr_t *)p;
			if (InModule(v))
				frames[nframes++] = v - mod_base;
		}
	}
	if (!InModule(eip) && !nframes)
		return EXCEPTION_CONTINUE_SEARCH;

	crash_logged++;
	FILE *f = fopen("g18crash.log", "a");
	if (!f)
		return EXCEPTION_CONTINUE_SEARCH;
	char name[64];
	uintptr_t base;
	ModuleOf(eip, name, sizeof(name), &base);
	fprintf(f, "exception %08lx at %08lx (%s+0x%lx)", (unsigned long)code, (unsigned long)eip,
	        name[0] ? name : "?", (unsigned long)(eip - base));
	if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
		fprintf(f, " %s %08lx", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
		        (unsigned long)ep->ExceptionRecord->ExceptionInformation[1]);
	fprintf(f, "\n  eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx\n",
	        c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
	fprintf(f, "  gamex86 base %08lx, stack:", (unsigned long)mod_base);
	for (int i = 0; i < nframes; i++)
		fprintf(f, " +%lx", (unsigned long)frames[i]);
	fprintf(f, "\n");
	fclose(f);
	return EXCEPTION_CONTINUE_SEARCH;
}

void G_CrashLogInit(void)
{
	cvar_t *v = gix.cvar("g18_crashlog", "0", 0, NULL);
	if (!v || !v->value || crash_handler)
		return;
	HMODULE h = NULL;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR)&G_CrashLogInit, &h))
		return;
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)h;
	IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)h + dos->e_lfanew);
	mod_base = (uintptr_t)h;
	mod_end = mod_base + nt->OptionalHeader.SizeOfImage;
	crash_handler = AddVectoredExceptionHandler(1, G18_CrashHandler);
}

void G_CrashLogShutdown(void)
{
	if (crash_handler)
		RemoveVectoredExceptionHandler(crash_handler);
	crash_handler = NULL;
}

#endif /* _WIN32 */
