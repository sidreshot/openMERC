/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenMerc: small portability layer shared by the open modules (gamex86 / game, player, OMsnd) and
 * engine code that was written against the MSVC C runtime. On Windows it is empty apart from
 * OM_EXPORT; elsewhere it maps the MSVC names used by the modules to POSIX ones.
 * (Linux build, docs/BUILD-linux.md; no spec)
 */
#ifndef OM_PLATFORM_H
#define OM_PLATFORM_H

/* layout checks of the 32-bit module interfaces (SPEC-001/003/010/760 sizes and offsets): they hold
   wherever pointers are 4 bytes (Windows x86 and Linux i386) and are skipped on 64-bit targets, where
   the engine and the open modules share the natural layout of the same declarations instead. Use it only
   for records that hold pointers and never reach a file or the network. */
#ifdef __cplusplus
#define OM_ABI32_ASSERT(c, m) static_assert((c) || sizeof(void *) != 4, m)
#else
#define OM_ABI32_ASSERT(c, m) _Static_assert((c) || sizeof(void *) != 4, m)
#endif

#if defined(_WIN32)
#define OM_EXPORT __declspec(dllexport)
#else
#define OM_EXPORT __attribute__((visibility("default")))

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* MSVC's _snprintf/_vsnprintf may leave the buffer unterminated on truncation; C99 snprintf/vsnprintf
   always terminate, so mapping to them is never less safe (the return value on truncation differs:
   MSVC returns -1, C99 the would-be length; callers here only test it for < 0 or ignore it). */
#ifndef _snprintf
#define _snprintf snprintf
#endif
#ifndef _vsnprintf
#define _vsnprintf vsnprintf
#endif
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif
#ifndef _strdup
#define _strdup strdup
#endif
#ifndef _unlink
#define _unlink unlink
#endif
#ifndef _access
#define _access access
#endif
#ifndef _mkdir
#define _mkdir(p) mkdir((p), 0755)
#endif

/* _snprintf_s(buf, size, _TRUNCATE, fmt, ...): truncating, always terminated, as snprintf */
#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
#ifndef _snprintf_s
#define _snprintf_s(buf, size, count, ...) snprintf((buf), (size), __VA_ARGS__)
#endif
#ifndef _vsnprintf_s
#define _vsnprintf_s(buf, size, count, fmt, ap) vsnprintf((buf), (size), (fmt), (ap))
#endif

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#endif /* !_WIN32 */

#endif /* OM_PLATFORM_H */
