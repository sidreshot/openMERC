/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Owner: I1a (engine core). Other packages: include, do not edit.
 *
 * Soldier of Fortune 1.07f savegame files (.sof): the chunk container, the
 * engine chunks and the save / load / quicksave / quickload / newsave /
 * wipe commands. The server and the game-module host plug in through
 * sg_host_t; the game module writes and reads its own chunks with
 * SG_WriteChunk / SG_ReadChunk (the engine's write-chunk / read-chunk
 * services of SPEC-001).
 *
 * Implements: SPEC-303
 */

#ifndef OM_SOF_SAVEGAME_H
#define OM_SOF_SAVEGAME_H

/* four letters packed big-endian; stored little-endian on disk, so the
   bytes in the file are the letters reversed ("COMM" -> "MMOC") */
#define SG_ID(a, b, c, d) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | \
		((unsigned)(c) << 8) | (unsigned)(d))
#define SG_END_MARKER 0x1234ABCDu

#define SG_COMM_SIZE 64
#define SG_SHOT_SIZE 65536
#define SG_MPCM_SIZE 256
#define SG_CFST_COUNT 3555
#define SG_CFST_SIZE (SG_CFST_COUNT * 64)
/* OpenMerc: "CFSX" after CFST, only when the sound extension (game_api.h OM_MAX_SOUNDS, configstrings
   3555..3710) is in use; older saves simply do not have it */
#define SG_CFSX_FIRST 3555
#define SG_CFSX_COUNT 156
#define SG_CFSX_SIZE (SG_CFSX_COUNT * 64)
#define SG_PRTS_COUNT 1024

/* checksum of chunk data: MD4 digest as four little-endian words, XORed */
unsigned SG_Checksum(const void *data, int length);

/* "COMM" from an id; out must hold 5 bytes */
const char *SG_IdString(unsigned id, char *out);

/* the open savegame (one at a time) */
qboolean SG_OpenWrite(const char *path);
qboolean SG_OpenRead(const char *path);
void SG_Close(void);
qboolean SG_IsOpen(void);

/* write one chunk; with cvar sv_testsave set nothing is written */
void SG_WriteChunk(unsigned id, const void *data, int length);

/* read the next chunk, which must have the given id and length (a
   mismatch, a bad checksum or end marker, or the end of the file drops
   the game with the SoF messages). Returns the length. */
int SG_ReadChunk(unsigned id, void *data, int length);
/* any length: the engine allocates the buffer (Z_Free it) */
void *SG_ReadChunkAlloc(unsigned id, int *length);
/* id of the next chunk without reading it (0 at the end of the file) */
unsigned SG_PeekChunk(void);

/*
 * What the server / game-module host provides. Every member may be NULL;
 * the engine then uses its defaults (see sof_save.c).
 */
typedef struct
{
	/* a game is running that can be saved (server active, not a demo) */
	qboolean (*InGame)(void);
	/* health of the local player (saving needs >= 1) */
	int (*PlayerHealth)(void);
	/* game module: a cinematic is playing */
	qboolean (*InCinematic)(void);
	/* game module export 0x40: a manual save may proceed */
	qboolean (*AllowSave)(void);
	/* 128x128 RGBA thumbnail of the client view; false = none */
	qboolean (*Screenshot)(byte *rgba65536);
	/* map command text of the running level, e.g. "kos1" */
	const char *(*MapCommand)(void);
	/* configstring in SoF numbering (0..3554), "" when unset */
	const char *(*ConfigString)(int index);
	int (*FrameNumber)(void);
	void (*GetPortals)(int states[SG_PRTS_COUNT]);
	/* game module "write game" (autosave flag) and "write level" */
	void (*WriteGame)(qboolean autosave);
	void (*WriteLevel)(void);

	/* load: start a new server for the map command in "loading a save"
	   mode; the game module reads its game chunks and reports whether
	   the save contains a level section */
	qboolean (*StartLoaded)(const char *mapcommand);
	void (*SetFrameNumber)(int frame);
	void (*SetConfigString)(int index, const char *value);
	void (*SetPortals)(const int states[SG_PRTS_COUNT]);
	void (*ReadLevel)(void);
} sg_host_t;

void SG_SetHost(const sg_host_t *host);

/* registers save, load, quicksave, quickload, newsave, wipe */
void SG_InitCommands(void);

/* the automatic save of a single-player level start ("auto", no level
   section); deletefirst: map deletes auto.sof first, gamemap does not */
void SG_AutoSave(qboolean deletefirst);

/* <user>/save/<name>.sof */
void SG_SavePath(const char *name, char *out, size_t size);

/* reads COMM and SHOT of a save (menus); false when unreadable */
qboolean SG_ReadInfo(const char *name, char *comment, size_t commentsize,
		byte *shot65536);

#endif
