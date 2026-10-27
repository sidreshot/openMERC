// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - savegames: the public interface for other game packages.
//
// Owner: G10 (game/src/save/). Other packages read this header and call it; requests go to docs/impl/G10.md.
//
// What G10 does (SPEC-752): the engine's exports 0x10 WriteGame / 0x14 ReadGame / 0x18 WriteLevel / 0x1C ReadLevel
// and 0x40 SaveAllowed / 0x44 SavesLeft (g_save.cpp forwards to game/src/save/). The level section is written in the
// order of SPEC-752 1.2: FTSZ, LVLC, NMED + EDNM/EDCT/STRG per entity, the script chunks (G6b), the AI block (G2, which
// drives G9's Ghoul tables, G3's bodies and G4's vehicle chunks), OINM (G9), PLNM/PLPI/PLWA (items), and then
// OpenMerc's extension records (below) - state that the original module keeps inside its entity, level or client
// records but OpenMerc keeps in package tables.
//
// Packages with level state outside edict_t / gclient_t / level_locals_t register an extension record once, from a
// static object in their own source (the registration order does not matter; records are written in id order):
//
//     static void MyWrite(void) { G10_WriteChunk("XYZW", &state, sizeof(state)); }
//     static void MyRead(void)  { G10_ReadChunk("XYZW", &state, sizeof(state)); }
//     static G10Extension my_ext("XYZ1", MyWrite, MyRead);
//
// read() is called after every entity, script, AI, Ghoul and item chunk is restored (so entity numbers, Ghoul
// instances and bodies are valid), before the client entities are re-attached. A package must read exactly what
// its write() wrote. Ids are four characters, unique per package.
//
// Pointer encodings for package records (SPEC-752 2.1): entities as numbers (-1 none); pointers into the game
// module's own image - functions (callbacks) and static tables - with G10_EncodePtr/G10_DecodePtr. A save is read
// only by the same build of the module that wrote it (FTSZ carries the build fingerprint; another build or the
// original module stops with the game error `Function index tables have changed size`).
//
// Implements: SPEC-752 1.2 (sequence), 2.1 (encodings), 2.2 (STRG)
#pragma once

#include "../g_local.h"

// --- chunk services (SPEC-303 container through imports 85/86; SPEC-752 2.2) -----------------------------------
int G10_ChunkId(const char *four);                          // 'ABCD' packed big-endian (SPEC-303)
void G10_WriteChunk(const char *id, const void *data, int size);
bool G10_ReadChunk(const char *id, void *data, int size);   // exact size; the engine drops the level on a mismatch
int G10_ReadChunkAny(const char *id, void **data);          // any length; free with G10_Free; returns the length
void G10_Free(void *p);
void G10_WriteInt(const char *id, int v);
int G10_ReadInt(const char *id);
// string field: the int written into the record (strlen + 1, -1 none) and later the STRG chunk (SPEC-752 2.1/2.2)
int G10_StringMark(const char *s);
void G10_WriteString(const char *s);                        // STRG with the text and its NUL; nothing for NULL
char *G10_ReadString(int mark);                             // mark -1: NULL; else reads STRG into level memory

// --- encodings -------------------------------------------------------------------------------------------------
int G10_EncodeEnt(const edict_t *e);                        // entity number, -1 none
edict_t *G10_DecodeEnt(int n);                              // NULL for -1 or out of range
int G10_EncodePtr(const void *p);                           // offset in the module image, -1 none (or not ours)
void *G10_DecodePtr(int code);                              // NULL for -1 or out of range

// --- state ------------------------------------------------------------------------------------------------------
bool G10_Loading(void);  // true while ReadGame / ReadLevel run

// --- extension records ------------------------------------------------------------------------------------------
struct G10Extension {
	G10Extension(const char *id, void (*write)(void), void (*read)(void));
	char id[5];
	void (*write)(void);
	void (*read)(void);
	G10Extension *next;
};
