/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * OpenMerc Ghoul model system - host services and C entry points for the engine.
 *
 * The Ghoul library depends on nothing else in the engine. Everything it needs from its host
 * (files, console, memory, time, fatal errors) comes through om_ghoul_host_t; the engine (C) or a
 * test program fills one in and passes it to OMGhoul_Init.
 *
 * Implements: SPEC-010 (system roles, game imports #61-#66, gserver), SPEC-012 (caches), SPEC-001 (#60-#66),
 *             SPEC-013 (engine bit buffer of the network methods)
 */
#ifndef OM_GHOUL_HOST_H
#define OM_GHOUL_HOST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct om_ghoul_host_s
{
	void *ctx; /* passed back to every callback */

	/* Load a whole file by game path ("ghoul/enemy/meso/MESO_PLAYER.ghb"; case-insensitive, '/' or '\').
	   Returns the length and sets *data (released with free_file), or -1 and *data = NULL.
	   Opening a file this way also records it as used (download list, SPEC-010 system method 22). */
	int (*load_file)(void *ctx, const char *path, void **data);
	void (*free_file)(void *ctx, void *data);

	/* Console output (text already formatted, may contain newlines). */
	void (*print)(void *ctx, const char *text);

	/* Fatal error, text = "Ghoul :<message>". Must not return (Sys_Error, or a C++ throw in test hosts).
	   If it returns, the library aborts the process. */
	void (*fatal)(void *ctx, const char *text);

	/* Memory for model data and caches; NULL entries mean malloc/free. */
	void *(*alloc)(void *ctx, size_t size);
	void (*release)(void *ctx, void *ptr);

	/* Real time in milliseconds (diagnostics only); may be NULL. */
	double (*milliseconds)(void *ctx);
} om_ghoul_host_t;

/* Engine bit buffer passed to the engine-only network methods (system methods 16, 18, 24, 25).
   Bits are written/read least-significant-bit first (SPEC-201 B.1).
   Writer: data = buffer, maxbits = byte budget * 8, bit = 0. After the call the payload is
           (bit + 7) / 8 bytes (zero padded); the engine sends it as svc 26 / svc 27.
   Reader: data = payload, maxbits = payload bytes * 8, bit = 0. */
typedef struct om_ghoul_bits_s
{
	unsigned char *data;
	int maxbits;
	int bit;
	int overflowed; /* writer: something did not fit; reader: read past the end */
} om_ghoul_bits_t;

/* system roles (SPEC-010 A) */
#define OM_GHOUL_SERVER 0
#define OM_GHOUL_CLIENT 1
#define OM_GHOUL_MENU 2

/* Install the host (copied). Must be called before any other function. */
void OMGhoul_Init(const om_ghoul_host_t *host);
/* Destroy every object and free every cache. */
void OMGhoul_Shutdown(void);

/* The three system objects (SPEC-010 A). Return the C++ interface pointer (ghoul::IGhoulSystem *). */
void *OMGhoul_GetSystem(int role);

/* Game import #61: new player-model description (ghoul::IGhoulPlayerModel *), loaded from
   Ghoul/pmodels/<file>. The caller deletes it with its method 18 (flags 1). */
void *OMGhoul_NewPlayerModel(const char *file);
/* Game import #62: load Ghoul/<folder>/<file> (else Ghoul/comskin/<file>) into the shared text cache.
   Returns the length or -1; *buffer is not NUL-terminated and must not be freed. */
int OMGhoul_LoadFile(const char *folder, const char *file, void **buffer);
/* Game import #63: next CR LF line (see SPEC-010 for the last-byte quirk). Returns 1 when a line feed
   was found. */
int OMGhoul_GetLine(int *remaining, char **cursor, char *line);
/* Game import #64: pre-load <name>.gsq. */
void OMGhoul_PreloadList(const char *folder, const char *name);
/* Game import #65: register the sequences of a .gsq list with a model object (ghoul::IGhoulObject *). */
int OMGhoul_RegisterList(const char *folder, const char *name, void *object);
/* Game import #66: hide the parts of a "parts off" list on an instance. */
void OMGhoul_PartsOff(const char *folder, const char *name, void *object, void *instance);

/* Console command "gserver <sub-command> [argument]" (SPEC-012 section 10); argv[0] is "gserver". */
void OMGhoul_Command(int argc, const char *const *argv);
/* ENG2: the shared size-matters value for the movement routine (SPEC-314 Inputs) */
float OMGhoul_SizeMatters(void);

#ifdef __cplusplus
}
#endif

#endif
