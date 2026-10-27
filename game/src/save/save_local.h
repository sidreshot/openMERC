// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G10 - savegames: internal declarations of game/src/save/.
// Implements: SPEC-752, SPEC-303 (game side), SPEC-001 (exports 0x10-0x1C, 0x40, 0x44)
#pragma once

#include "save_api.h"

#include <stdint.h>
#include <vector>

namespace g10 {

// ---------------------------------------------------------------------------------------------------------------
// chunk I/O (save_io.cpp). Normally the engine's imports 85/86; the self-test swaps in a memory stream for every
// package (they all call gix.WriteChunk / gix.ReadChunk).
void MemoryBegin(std::vector<unsigned char> *out, const std::vector<unsigned char> *in);
void MemoryEnd(void);
extern int g_loading;    // ReadGame / ReadLevel running
extern int g_warnings;   // encoding warnings of the current save

// ---------------------------------------------------------------------------------------------------------------
// module image (save_image.cpp): base address, size and build fingerprint of this DLL
struct ImageInfo {
	const unsigned char *base;
	uint32_t size;
	uint32_t timestamp;
	uint32_t checksum;
};
const ImageInfo &Image(void);

// ---------------------------------------------------------------------------------------------------------------
// records (save_records.cpp)
enum FieldKind {
	FK_STRING,   // char *: strlen + 1 / -1 in the image, STRG chunk after the record (SPEC-752 2.1)
	FK_ENTITY,   // edict_t *: entity number / -1
	FK_PTR,      // pointer into this module's image (callbacks, static tables): offset / -1
	FK_CLEAR,    // pointer not saved: 0 in the image, NULL on load (rebuilt by its owner)
	FK_PRESENCE, // object re-created by its own chunks: 1 / 0 in the image, NULL on load until then
	FK_CLIENT,   // gclient_t *: client slot (records of the game's client array) / -1
};
struct Field {
	int ofs;
	FieldKind kind;
	const char *name;
};

// entity records (EDCT): OpenMerc's edict_t image, pointer fields encoded
int EntityImageSize(void);
void WriteEntity(edict_t *e);              // EDCT + STRG
void ReadEntity(edict_t *e);               // into a cleared slot
// client records: CLNT (game section, SPEC-752 3.2 interface part) and OMCL (level section, private part)
void WriteClientGame(gclient_t *c);        // CLNT 1536 (+ INVN by the player module)
void ReadClientGame(gclient_t *c);
void WriteClientLevel(gclient_t *c);       // OMCL
void ReadClientLevel(gclient_t *c);
// GAME / LVLC
void WriteGameRecord(bool autosave);
bool ReadGameRecord(void);                 // returns the autosave flag
void WriteLevelRecord(void);
void ReadLevelRecord(void);
// tables for the self-test / diagnostics
const Field *EntityFields(int *n);
const Field *ClientFields(int *n);

// ---------------------------------------------------------------------------------------------------------------
// level section (save_level.cpp)
void WriteLevelSection(void);
void ReadLevelSection(void);
void SelfTest(void);


} // namespace g10

// entry points used by g_save.cpp
void G10_WriteGame(qboolean autosave);
qboolean G10_ReadGame(int unused);
void G10_WriteLevel(void);
void G10_ReadLevel(void);
qboolean G10_SaveAllowed(void);
int G10_SavesLeft(void); // manual saves left in this level, -1 unlimited
