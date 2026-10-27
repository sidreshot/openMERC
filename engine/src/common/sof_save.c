/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Soldier of Fortune 1.07f savegames: the .sof chunk container (id,
 * length, MD4-XOR checksum, data, end marker 0x1234ABCD), the engine
 * chunk sequence (COMM, SHOT, MPCM, CVCN + CVAR/VALU/FLGS, SPNM + SPID,
 * FRNM, CFST, PRTS around the game module's chunks), the save / load /
 * quicksave / quickload / newsave / wipe commands, the automatic save and
 * the staging file current.sof. Server and game module plug in through
 * sg_host_t (SG_SetHost).
 *
 * Implements: SPEC-303, SPEC-302 (saved cvar flags)
 */

#include "header/common.h"
#include <sys/stat.h>

static FILE *sg_file;
static qboolean sg_writing;
static int sg_written; /* bytes written to the open file */
static sg_host_t sg_host;
static int sg_savecount;              /* successful saves (timed autosave timer) */
static const byte *sg_shotoverride;   /* SHOT of the timed autosave */

static const char *sg_skillnames[6] = {
	"Effortless", "Easy", "Medium", "Challenging", "Unfair", "Custom"
};

/* ---------------------------------------------------------------------
 * container
 * --------------------------------------------------------------------- */

unsigned
SG_Checksum(const void *data, int length)
{
	return Com_BlockChecksum((void *)data, length);
}

const char *
SG_IdString(unsigned id, char *out)
{
	out[0] = (char)((id >> 24) & 0xff);
	out[1] = (char)((id >> 16) & 0xff);
	out[2] = (char)((id >> 8) & 0xff);
	out[3] = (char)(id & 0xff);
	out[4] = 0;
	return out;
}

void
SG_SavePath(const char *name, char *out, size_t size)
{
	Com_sprintf(out, size, "%s/save/%s.sof", FS_Userdir(), name);
}

qboolean
SG_IsOpen(void)
{
	return sg_file != NULL;
}

void
SG_Close(void)
{
	if (sg_file)
	{
		fclose(sg_file);
	}

	sg_file = NULL;
	sg_writing = false;
}

qboolean
SG_OpenWrite(const char *path)
{
	SG_Close();
	FS_CreatePath(path);
	sg_file = Q_fopen(path, "wb");
	sg_writing = true;
	sg_written = 0;

	return sg_file != NULL;
}

qboolean
SG_OpenRead(const char *path)
{
	SG_Close();
	sg_file = Q_fopen(path, "rb");
	sg_writing = false;

	return sg_file != NULL;
}

static void
SG_PutLong(unsigned v)
{
	byte b[4];

	b[0] = v & 0xff;
	b[1] = (v >> 8) & 0xff;
	b[2] = (v >> 16) & 0xff;
	b[3] = (v >> 24) & 0xff;
	fwrite(b, 1, 4, sg_file);
}

void
SG_WriteChunk(unsigned id, const void *data, int length)
{
	char name[5];

	if (!sg_file || !sg_writing)
	{
		return;
	}

	if (Cvar_VariableValue("sv_testsave"))
	{
		return; /* exercises the save path without data (SPEC-303 §1) */
	}

	if (length < 0)
	{
		length = 0;
	}

	SG_PutLong(id);
	SG_PutLong((unsigned)length);
	SG_PutLong(SG_Checksum(data, length));

	if (length)
	{
		fwrite(data, 1, length, sg_file);
	}

	SG_PutLong(SG_END_MARKER);
	sg_written += 16 + length;

	if (Com_DeveloperBit(4))
	{
		Com_Printf("Writing chunk %s (%i bytes)\n", SG_IdString(id, name), length);
	}
}

static qboolean
SG_GetLong(unsigned *v)
{
	byte b[4];

	if (fread(b, 1, 4, sg_file) != 4)
	{
		return false;
	}

	*v = b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24);
	return true;
}

unsigned
SG_PeekChunk(void)
{
	long pos;
	unsigned id;

	if (!sg_file || sg_writing)
	{
		return 0;
	}

	pos = ftell(sg_file);

	if (!SG_GetLong(&id))
	{
		id = 0;
	}

	fseek(sg_file, pos, SEEK_SET);
	return id;
}

/*
 * Reads a chunk: data == NULL with length < 0 allocates (any length).
 */
static void *
SG_ReadChunkEx(unsigned wanted, void *data, int length, int *outlen)
{
	unsigned id, len, sum, marker;
	char a[5], b[5];
	byte *buf;

	if (!sg_file || sg_writing)
	{
		Com_Error(ERR_DROP, "Error during loading chunk %s", SG_IdString(wanted, a));
		return NULL;
	}

	if (!SG_GetLong(&id) || !SG_GetLong(&len) || !SG_GetLong(&sum))
	{
		Com_Error(ERR_DROP, "Error during loading chunk %s", SG_IdString(wanted, a));
		return NULL;
	}

	if (id != wanted)
	{
		Com_Error(ERR_DROP, "Loaded chunk ID (%s) does not match requested chunk ID (%s)",
				SG_IdString(id, a), SG_IdString(wanted, b));
		return NULL;
	}

	if ((length > 0) && ((int)len != length))
	{
		Com_Error(ERR_DROP, "Loaded chunk (%s) has different length than requested",
				SG_IdString(id, a));
		return NULL;
	}

	if (len > 64 * 1024 * 1024)
	{
		Com_Error(ERR_DROP, "Error during loading chunk %s", SG_IdString(id, a));
		return NULL;
	}

	buf = (length > 0) ? (byte *)data : Z_Malloc((int)len + 1);

	if (len && (fread(buf, 1, len, sg_file) != len))
	{
		if (buf != data)
		{
			Z_Free(buf);
		}

		Com_Error(ERR_DROP, "Error during loading chunk %s", SG_IdString(id, a));
		return NULL;
	}

	if (SG_Checksum(buf, (int)len) != sum)
	{
		if (buf != data)
		{
			Z_Free(buf);
		}

		Com_Error(ERR_DROP, "Failed checksum check for chunk");
		return NULL;
	}

	if (!SG_GetLong(&marker))
	{
		if (buf != data)
		{
			Z_Free(buf);
		}

		Com_Error(ERR_DROP, "Error during loading chunk %s", SG_IdString(id, a));
		return NULL;
	}

	if (marker != SG_END_MARKER)
	{
		if (buf != data)
		{
			Z_Free(buf);
		}

		Com_Error(ERR_DROP, "Bad savegame magic for chunk %s", SG_IdString(id, a));
		return NULL;
	}

	if (Com_DeveloperBit(4))
	{
		Com_Printf("Reading chunk %s (%u bytes)\n", SG_IdString(id, a), len);
	}

	if (outlen)
	{
		*outlen = (int)len;
	}

	return buf;
}

int
SG_ReadChunk(unsigned id, void *data, int length)
{
	int len = 0;

	if (length <= 0)
	{
		void *p = SG_ReadChunkEx(id, NULL, -1, &len);

		Z_Free(p);
		return len;
	}

	SG_ReadChunkEx(id, data, length, &len);
	return len;
}

void *
SG_ReadChunkAlloc(unsigned id, int *length)
{
	return SG_ReadChunkEx(id, NULL, -1, length);
}

/* ---------------------------------------------------------------------
 * host defaults
 * --------------------------------------------------------------------- */

void
SG_SetHost(const sg_host_t *host)
{
	if (host)
	{
		sg_host = *host;
	}
	else
	{
		memset(&sg_host, 0, sizeof(sg_host));
	}
}

static qboolean
SG_InGame(void)
{
	return sg_host.InGame ? sg_host.InGame() : (Com_ServerState() != 0);
}

static const char *
SG_MapCommand(void)
{
	return sg_host.MapCommand ? sg_host.MapCommand() : Cvar_VariableString("mapname");
}

static const char *
SG_ConfigString(int i)
{
	const char *s = sg_host.ConfigString ? sg_host.ConfigString(i) : NULL;

	return s ? s : "";
}

/* ---------------------------------------------------------------------
 * OpenMerc: which game modules wrote a save (docs/impl/SAVE-COMPAT.md)
 *
 * The game chunks of a save are the private state of the game module that
 * wrote them: the original gamex86.dll cannot read those of openMERC's own
 * game module (mode B) and the other way round, and trying it hung the
 * load. openMERC saves carry the saved cvar om_savegame "openMERC 1 B" or
 * "openMERC 1 A" (written among the CVAR chunks, so the file stays a valid
 * 1.07f save); older openMERC mode B saves are recognised by the module's
 * own level chunk OMCN. A save without both was written by the original
 * modules (the original game, or openMERC mode A before the marker).
 * --------------------------------------------------------------------- */

#define SG_MARKER_CVAR "om_savegame"

static int
SG_FileKind(const char *path)
{
	FILE *f = Q_fopen(path, "rb");
	unsigned id, len, sum;
	int kind = SG_KIND_ANY, ncvar = -1, chunk = 0;
	qboolean level = false, wantvalue = false;

	if (!f)
	{
		return SG_KIND_UNREADABLE;
	}

	while (fread(&id, 1, 4, f) == 4 && fread(&len, 1, 4, f) == 4 && fread(&sum, 1, 4, f) == 4)
	{
		id = LittleLong(id);
		len = LittleLong(len);

		if (len > 64 * 1024 * 1024)
		{
			break;
		}

		if (chunk == 3 && id == SG_ID('C', 'V', 'C', 'N') && len == 4)
		{
			if (fread(&ncvar, 1, 4, f) != 4)
			{
				break;
			}

			ncvar = LittleLong(ncvar);
			fseek(f, 4, SEEK_CUR);
			chunk++;
			continue;
		}

		if (id == SG_ID('C', 'V', 'A', 'R') && len == sizeof(SG_MARKER_CVAR))
		{
			char name[sizeof(SG_MARKER_CVAR)];

			if (fread(name, 1, len, f) != len)
			{
				break;
			}

			wantvalue = !memcmp(name, SG_MARKER_CVAR, sizeof(SG_MARKER_CVAR));
			fseek(f, 4, SEEK_CUR);
			chunk++;
			continue;
		}

		if (id == SG_ID('V', 'A', 'L', 'U') && wantvalue && len > 0 && len < 64)
		{
			char value[64];

			if (fread(value, 1, len, f) != len)
			{
				break;
			}

			value[len - 1] = 0;
			wantvalue = false;

			if (!strncmp(value, "openMERC ", 9))
			{
				const char *m = strrchr(value, ' ');

				fclose(f);
				return (m && m[1] == 'B') ? SG_KIND_OPENMERC_B : SG_KIND_OPENMERC_A;
			}

			fseek(f, 4, SEEK_CUR);
			chunk++;
			continue;
		}

		if (id == SG_ID('L', 'V', 'L', 'C') || id == SG_ID('F', 'T', 'S', 'Z'))
		{
			level = true;
		}

		if (id == SG_ID('O', 'M', 'C', 'N'))
		{
			fclose(f);
			return SG_KIND_OPENMERC_B;
		}

		fseek(f, (long)len + 4, SEEK_CUR);
		chunk++;
	}

	fclose(f);

	if (chunk < 4)
	{
		return SG_KIND_UNREADABLE;
	}

	/* a level section without the marker and without OMCN: the original modules */
	if (level)
	{
		kind = SG_KIND_ORIGINAL;
	}

	return kind;
}

int
SG_SaveKind(const char *name)
{
	char path[MAX_OSPATH];

	SG_SavePath(name, path, sizeof(path));
	return SG_FileKind(path);
}

/* the game module the server loads (or has loaded): the first gamex86 in
   the loader's order (sv_game.c); openMERC's own module exports
   OpenMerc_Module, so the file holds that name */
static qboolean
SG_FileHasText(const char *path, const char *text)
{
	static char lastpath[MAX_OSPATH];
	static long lastsize = -1, lastmtime;
	static qboolean lastresult;
	struct stat st;
	FILE *f;
	byte *buf;
	long n, i, tl = (long)strlen(text);
	qboolean found = false;

	if (stat(path, &st) != 0)
	{
		return false;
	}

	if (!strcmp(path, lastpath) && lastsize == (long)st.st_size && lastmtime == (long)st.st_mtime)
	{
		return lastresult;
	}

	f = Q_fopen(path, "rb");

	if (!f)
	{
		return false;
	}

	n = (long)st.st_size;
	buf = malloc(n > 0 ? n : 1);

	if (buf && fread(buf, 1, n, f) == (size_t)n)
	{
		for (i = 0; i + tl <= n; i++)
		{
			if (buf[i] == (byte)text[0] && !memcmp(buf + i, text, tl))
			{
				found = true;
				break;
			}
		}
	}

	free(buf);
	fclose(f);
	Q_strlcpy(lastpath, path, sizeof(lastpath));
	lastsize = (long)st.st_size;
	lastmtime = (long)st.st_mtime;
	lastresult = found;
	return found;
}

qboolean
SG_OwnGameModule(void)
{
	char name[MAX_OSPATH], cwd[MAX_OSPATH];
	const char *path = NULL;
#ifdef _WIN32
	const char *lib = "gamex86.dll";
#else
	const char *lib = "gamex86.so";
#endif

	if (Sys_Realpath(".", cwd, sizeof(cwd)))
	{
		Com_sprintf(name, sizeof(name), "%s/final/%s", cwd, lib);

		if (Sys_IsFile(name))
		{
			return SG_FileHasText(name, "OpenMerc_Module");
		}

		Com_sprintf(name, sizeof(name), "%s/%s", cwd, lib);

		if (Sys_IsFile(name))
		{
			return SG_FileHasText(name, "OpenMerc_Module");
		}
	}

	while ((path = FS_NextPath(path)) != NULL)
	{
		Com_sprintf(name, sizeof(name), "%s/%s", path, lib);

		if (Sys_IsFile(name))
		{
			return SG_FileHasText(name, "OpenMerc_Module");
		}
	}

	return false;
}

qboolean
SG_KindLoads(int kind)
{
	if (kind == SG_KIND_ANY)
	{
		return true;
	}

	if (SG_OwnGameModule())
	{
		return kind == SG_KIND_OPENMERC_B;
	}

	return (kind == SG_KIND_ORIGINAL) || (kind == SG_KIND_OPENMERC_A);
}

const char *
SG_IncompatibleLabel(void)
{
	return SP_RussianActive() ? "\xCD\xE5\xF1\xEE\xE2\xEC\xE5\xF1\xF2\xE8\xEC\xEE" : "Not compatible";
}

const char *
SG_IncompatibleText(int kind)
{
	if (kind == SG_KIND_OPENMERC_B)
	{
		return SP_RussianActive() ? "\xD1\xEE\xF5\xF0\xE0\xED\xE5\xED\xE8\xE5 \xF1\xE4\xE5\xEB\xE0\xED\xEE \xE2 \xF0\xE5\xE6\xE8\xEC\xE5 B openMERC: \xEE\xF0\xE8\xE3\xE8\xED\xE0\xEB\xFC\xED\xFB\xE5 \xEC\xEE\xE4\xF3\xEB\xE8 \xE8\xE3\xF0\xFB (\xF0\xE5\xE6\xE8\xEC A) \xED\xE5 \xEC\xEE\xE3\xF3\xF2 \xE5\xE3\xEE \xE7\xE0\xE3\xF0\xF3\xE7\xE8\xF2\xFC." :
			"This save was made in openMERC mode B: the original game modules (mode A) cannot load it.";
	}

	return SP_RussianActive() ? "\xD1\xEE\xF5\xF0\xE0\xED\xE5\xED\xE8\xE5 \xEE\xF0\xE8\xE3\xE8\xED\xE0\xEB\xFC\xED\xEE\xE9 Soldier of Fortune: \xF1\xEE\xE1\xF1\xF2\xE2\xE5\xED\xED\xFB\xE5 \xEC\xEE\xE4\xF3\xEB\xE8 openMERC (\xF0\xE5\xE6\xE8\xEC B) \xED\xE5 \xEC\xEE\xE3\xF3\xF2 \xE5\xE3\xEE \xE7\xE0\xE3\xF0\xF3\xE7\xE8\xF2\xFC." :
		"This save was made with the original Soldier of Fortune game modules: openMERC's own modules (mode B) cannot load it.";
}

static void
SG_SetMarker(void)
{
	cvar_t *v = Cvar_Get(SG_MARKER_CVAR, "", CVAR_LATCH);

	(void)v;
	Cvar_FullSet(SG_MARKER_CVAR, SG_OwnGameModule() ? "openMERC 1 B" : "openMERC 1 A", CVAR_LATCH);
}

/* ---------------------------------------------------------------------
 * the engine chunks (SPEC-303 "Engine chunk sequence")
 * --------------------------------------------------------------------- */

static void
SG_WriteFile(qboolean autosave)
{
	char comment[SG_COMM_SIZE];
	char mpcm[SG_MPCM_SIZE];
	char timebuf[64];
	byte *shot;
	int skill, count, i;
	cvar_t *var;

	/* COMM */
	if (autosave)
	{
		Q_strlcpy(timebuf, "ENTERING", sizeof(timebuf));
	}
	else
	{
		time_t now = time(NULL);
		char *t = asctime(localtime(&now));
		size_t l;

		Q_strlcpy(timebuf, t ? t : "", sizeof(timebuf));
		l = strlen(timebuf);

		if (l && (timebuf[l - 1] == '\n'))
		{
			timebuf[l - 1] = 0;
		}
	}

	skill = (int)Cvar_VariableValue("skill");

	if ((skill < 0) || (skill > 5))
	{
		skill = 5;
	}

	memset(comment, 0, sizeof(comment));
	Com_sprintf(comment, sizeof(comment), "%s/%s/%s", timebuf,
			sg_skillnames[skill], SG_ConfigString(0));
	SG_WriteChunk(SG_ID('C', 'O', 'M', 'M'), comment, SG_COMM_SIZE);

	/* SHOT: client screenshot, or maps/<map>.raw for automatic saves */
	shot = Z_Malloc(SG_SHOT_SIZE);

	if (autosave)
	{
		byte *raw;
		int len = FS_LoadFile(va("maps/%s.raw", SG_MapCommand()), (void **)&raw);

		if (len == SG_SHOT_SIZE)
		{
			memcpy(shot, raw, SG_SHOT_SIZE);
		}

		if (len >= 0)
		{
			FS_FreeFile(raw);
		}
	}
	else if (sg_shotoverride)
	{
		memcpy(shot, sg_shotoverride, SG_SHOT_SIZE);
	}
	else if (sg_host.Screenshot)
	{
		if (!sg_host.Screenshot(shot))
		{
			memset(shot, 0, SG_SHOT_SIZE);
		}
	}

	SG_WriteChunk(SG_ID('S', 'H', 'O', 'T'), shot, SG_SHOT_SIZE);
	Z_Free(shot);

	/* MPCM */
	memset(mpcm, 0, sizeof(mpcm));
	Q_strlcpy(mpcm, SG_MapCommand(), sizeof(mpcm));
	SG_WriteChunk(SG_ID('M', 'P', 'C', 'M'), mpcm, SG_MPCM_SIZE);

	/* CVCN + CVAR/VALU/FLGS, newest-created first (with the openMERC marker) */
	SG_SetMarker();
	count = 0;

	for (var = cvar_vars; var; var = var->next)
	{
		if (var->flags & CVAR_SAVEMASK)
		{
			count++;
		}
	}

	{
		int le = LittleLong(count);

		SG_WriteChunk(SG_ID('C', 'V', 'C', 'N'), &le, 4);
	}

	for (var = cvar_vars; var; var = var->next)
	{
		int flags;

		if (!(var->flags & CVAR_SAVEMASK))
		{
			continue;
		}

		SG_WriteChunk(SG_ID('C', 'V', 'A', 'R'), var->name, (int)strlen(var->name) + 1);
		SG_WriteChunk(SG_ID('V', 'A', 'L', 'U'), var->string, (int)strlen(var->string) + 1);
		flags = LittleLong(var->flags);
		SG_WriteChunk(SG_ID('F', 'L', 'G', 'S'), &flags, 4);
	}

	/* the game module's "write game" */
	if (sg_host.WriteGame)
	{
		sg_host.WriteGame(autosave);
	}

	/* SPNM + SPID: the server's string packages */
	count = SP_CountOwned(SP_OWNER_SERVER);

	{
		int le = LittleLong(count);

		SG_WriteChunk(SG_ID('S', 'P', 'N', 'M'), &le, 4);
	}

	for (i = 0; i < count; i++)
	{
		const char *n = SP_NameOwned(SP_OWNER_SERVER, i);

		SG_WriteChunk(SG_ID('S', 'P', 'I', 'D'), n, (int)strlen(n) + 1);
	}

	if (autosave)
	{
		return; /* no level section */
	}

	/* FRNM, CFST, PRTS, then the game module's "write level" */
	{
		int fr = LittleLong(sg_host.FrameNumber ? sg_host.FrameNumber() : 0);
		char *cs;
		int ports[SG_PRTS_COUNT];

		SG_WriteChunk(SG_ID('F', 'R', 'N', 'M'), &fr, 4);

		cs = Z_Malloc(SG_CFST_SIZE);

		for (i = 0; i < SG_CFST_COUNT; i++)
		{
			Q_strlcpy(cs + i * 64, SG_ConfigString(i), 64);
		}

		SG_WriteChunk(SG_ID('C', 'F', 'S', 'T'), cs, SG_CFST_SIZE);
		Z_Free(cs);

		{
			qboolean used = false;

			cs = Z_Malloc(SG_CFSX_SIZE);

			for (i = 0; i < SG_CFSX_COUNT; i++)
			{
				Q_strlcpy(cs + i * 64, SG_ConfigString(SG_CFSX_FIRST + i), 64);
				used |= (cs[i * 64] != 0);
			}

			if (used)
			{
				SG_WriteChunk(SG_ID('C', 'F', 'S', 'X'), cs, SG_CFSX_SIZE);
			}

			Z_Free(cs);
		}

		memset(ports, 0, sizeof(ports));

		if (sg_host.GetPortals)
		{
			sg_host.GetPortals(ports);
		}
		else
		{
			CM_GetPortalStates(ports, SG_PRTS_COUNT);
		}

		for (i = 0; i < SG_PRTS_COUNT; i++)
		{
			ports[i] = LittleLong(ports[i]);
		}

		SG_WriteChunk(SG_ID('P', 'R', 'T', 'S'), ports, sizeof(ports));
	}

	if (sg_host.WriteLevel)
	{
		sg_host.WriteLevel();
	}
}

static qboolean
SG_CopyFile(const char *from, const char *to, int *copied)
{
	FILE *in, *out;
	byte buf[65536];
	size_t n;

	*copied = 0;
	in = Q_fopen(from, "rb");

	if (!in)
	{
		return false;
	}

	FS_CreatePath(to);
	out = Q_fopen(to, "wb");

	if (!out)
	{
		fclose(in);
		return false;
	}

	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
	{
		if (fwrite(buf, 1, n, out) != n)
		{
			fclose(in);
			fclose(out);
			return false;
		}

		*copied += (int)n;
	}

	fclose(in);
	fclose(out);
	return true;
}

/* writes <name>.sof through current.sof (SPEC-303 §1, after the checks) */
static qboolean
SG_WriteSave(const char *name, qboolean autosave)
{
	char cur[MAX_OSPATH], dst[MAX_OSPATH];
	int copied;

	SG_SavePath("current", cur, sizeof(cur));
	SG_SavePath(name, dst, sizeof(dst));

	Sys_Remove(cur);

	if (!SG_OpenWrite(cur))
	{
		Com_Printf("Failed to create new savegame file current\n");
		Com_Printf("Failed to create savegame\n");
		return false;
	}

	SG_WriteFile(autosave);
	SG_Close();

	if (!SG_CopyFile(cur, dst, &copied) || !copied)
	{
		Sys_Remove(cur);
		Sys_Remove(dst);
		Com_Printf("Failed to copy savegame %s - deleting source and destination\n", name);
		return false;
	}

	Sys_Remove(cur);
	sg_savecount++;
	return true;
}

static qboolean
SG_BadName(const char *name)
{
	return strstr(name, "..") || strchr(name, '/') || strchr(name, '\\');
}

static void
SG_Save_f(void)
{
	const char *name;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: savegame <name>\n");
		return;
	}

	name = Cmd_Argv(1);

	if (Cvar_VariableValue("deathmatch"))
	{
		Com_Printf("Can't savegame in a deathmatch\n");
		return;
	}

	if (!Q_stricmp(name, "current"))
	{
		Com_Printf("Can't save to 'current'\n");
		return;
	}

	if (!Q_stricmp(name, "auto"))
	{
		Com_Printf("Can't save to 'auto'\n");
		return;
	}

	/* OpenMerc: the timed autosave's slots (om_autosave_interval) */
	if (SG_IsTimedSlot(name))
	{
		Com_Printf("Can't save to '%s' (reserved for the timed autosave)\n", name);
		return;
	}

	if (!SG_InGame())
	{
		Com_Printf("You must be in a game to save.\n");
		return;
	}

	if (sg_host.PlayerHealth && (sg_host.PlayerHealth() < 1))
	{
		Com_Printf("Can't savegame while dead!\n");
		return;
	}

	if (SG_BadName(name))
	{
		Com_Printf("Bad savegame name.\n");
		return;
	}

	if (sg_host.InCinematic && sg_host.InCinematic())
	{
		Com_Printf("Cannot save during cinematic.\n");
		return;
	}

	if (sg_host.AllowSave && !sg_host.AllowSave())
	{
		return; /* the game module refused (it prints its own text) */
	}

	Com_Printf("Saving game \"%s\"...\n", name);
	SG_WriteSave(name, false);
	Com_Printf("Done.\n");
}

void
SG_AutoSave(qboolean deletefirst)
{
	char path[MAX_OSPATH];

	if (Cvar_VariableValue("deathmatch"))
	{
		return;
	}

	SG_SavePath("auto", path, sizeof(path));

	if (deletefirst)
	{
		Sys_Remove(path);
	}

	FS_CreatePath(path);
	SG_WriteSave("auto", true);
}

/* ---------------------------------------------------------------------
 * OpenMerc timed autosave (om_autosave_interval; the moment is chosen by
 * the server, sv_save.c): a full save (level section, real time in COMM)
 * to the older of autotime1 / autotime2, with the client's last game
 * frame as the thumbnail. docs/impl/AUTOSAVE.md
 * --------------------------------------------------------------------- */

static byte sg_tshot[SG_SHOT_SIZE];
static int sg_tshotstate;
static int sg_tshotms;
static int sg_clientframe = -100000; /* Sys_Milliseconds of the last in-game frame */

qboolean
SG_IsTimedSlot(const char *name)
{
	return name && !Q_strncasecmp(name, SG_TIMED_PREFIX, (int)strlen(SG_TIMED_PREFIX));
}

const char *
SG_AutosaveLabel(void)
{
	/* "Avtosokhranenie" in CP1251 */
	return SP_RussianActive() ?
		"\xC0\xE2\xF2\xEE\xF1\xEE\xF5\xF0\xE0\xED\xE5\xED\xE8\xE5" : "Autosave";
}

int
SG_SaveCount(void)
{
	return sg_savecount;
}

void
SG_RequestTimedShot(void)
{
	if (sg_tshotstate == 0)
	{
		sg_tshotstate = 1;
	}
}

void
SG_CancelTimedShot(void)
{
	sg_tshotstate = 0;
}

int
SG_TimedShotState(void)
{
	return sg_tshotstate;
}

void
SG_ClientEndFrame(qboolean ingame, qboolean (*grab)(byte *rgba65536))
{
	if (!ingame)
	{
		sg_clientframe = -100000;
		return;
	}

	sg_clientframe = Sys_Milliseconds();

	if ((sg_tshotstate == 1) && grab)
	{
		int t0 = Sys_Milliseconds();

		sg_tshotstate = grab(sg_tshot) ? 2 : 3;
		sg_tshotms = Sys_Milliseconds() - t0;
	}
}

qboolean
SG_ClientInGame(void)
{
	int d = Sys_Milliseconds() - sg_clientframe;

	return (d >= 0) && (d < 250);
}

qboolean
SG_TimedAutoSave(char *slot, size_t slotsize, int *writems, int *shotms)
{
	char a[MAX_OSPATH], b[MAX_OSPATH];
	struct stat sa, sb;
	qboolean ea, eb, ok;
	byte *shot;
	int t0;

	SG_SavePath(SG_TIMED_PREFIX "1", a, sizeof(a));
	SG_SavePath(SG_TIMED_PREFIX "2", b, sizeof(b));
	ea = (stat(a, &sa) == 0);
	eb = (stat(b, &sb) == 0);

	/* the empty slot first, then the older one */
	if (!ea)
	{
		Q_strlcpy(slot, SG_TIMED_PREFIX "1", slotsize);
	}
	else if (!eb)
	{
		Q_strlcpy(slot, SG_TIMED_PREFIX "2", slotsize);
	}
	else
	{
		Q_strlcpy(slot, (sa.st_mtime <= sb.st_mtime) ? SG_TIMED_PREFIX "1" : SG_TIMED_PREFIX "2", slotsize);
	}

	/* the client's frame, else the level picture maps/<map>.raw (as "auto") */
	shot = Z_Malloc(SG_SHOT_SIZE);

	if (sg_tshotstate == 2)
	{
		memcpy(shot, sg_tshot, SG_SHOT_SIZE);
		*shotms = sg_tshotms;
	}
	else
	{
		byte *raw;
		int len = FS_LoadFile(va("maps/%s.raw", SG_MapCommand()), (void **)&raw);

		if (len == SG_SHOT_SIZE)
		{
			memcpy(shot, raw, SG_SHOT_SIZE);
		}

		if (len >= 0)
		{
			FS_FreeFile(raw);
		}

		*shotms = -1;
	}

	sg_tshotstate = 0;
	FS_CreatePath(a);
	sg_shotoverride = shot;
	t0 = Sys_Milliseconds();
	ok = SG_WriteSave(slot, false);
	*writems = Sys_Milliseconds() - t0;
	sg_shotoverride = NULL;
	Z_Free(shot);

	return ok;
}

/* reads the engine part of current.sof and hands over to the host */
static void
SG_Load_f(void)
{
	char src[MAX_OSPATH], cur[MAX_OSPATH];
	char comment[SG_COMM_SIZE + 1];
	char mapcmd[SG_MPCM_SIZE + 1];
	const char *name;
	int copied, count, i, len;
	qboolean level;
	FILE *f;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: loadgame <file>\n");
		return;
	}

	name = Cmd_Argv(1);

	if (SG_BadName(name))
	{
		Com_Printf("Bad loadgame name.\n");
		return;
	}

	if (!Q_stricmp(name, "current"))
	{
		Com_Printf("Can't load from 'current'\n");
		return;
	}

	SG_SavePath(name, src, sizeof(src));

	if (!Q_stricmp(name, "quick") && !FS_HostFileExists(src))
	{
		Com_Printf("No quicksave to load\n");
		return;
	}

	/* OpenMerc: a save of the other game modules is refused instead of hanging */
	{
		int kind = SG_FileKind(src);

		if ((kind != SG_KIND_UNREADABLE) && !SG_KindLoads(kind))
		{
			Com_Printf("Can't load \"%s\": %s\n", name, SG_IncompatibleText(kind));
			return;
		}
	}

	Com_Printf("Loading game \"%s\"...\n", name);

	/* 1. */
	Cvar_Set("sv_restartlevel", "0");
	Cvar_Set("menu_nostats", "1");
	SG_SavePath("current", cur, sizeof(cur));
	Sys_Remove(cur);
	SG_CopyFile(src, cur, &copied);

	/* 2. */
	f = Q_fopen(cur, "rb");

	if (!f)
	{
		Com_Printf("Failed to open savegame file current\n");
		Com_Printf("Failed to open savegame\n");
		return;
	}

	fseek(f, 0, SEEK_END);
	len = (int)ftell(f);
	fclose(f);

	if (len == 0)
	{
		Com_Printf("Illegal zero length savegame file current - deleting\n");
		Sys_Remove(cur);
		return;
	}

	if (!SG_OpenRead(cur))
	{
		Com_Printf("Failed to open savegame file current\n");
		Com_Printf("Failed to open savegame\n");
		return;
	}

	/* 3. */
	memset(comment, 0, sizeof(comment));
	SG_ReadChunk(SG_ID('C', 'O', 'M', 'M'), comment, SG_COMM_SIZE);
	Com_DPrintf("Reading: %s\n", comment);
	SG_ReadChunk(SG_ID('S', 'H', 'O', 'T'), NULL, 0);
	memset(mapcmd, 0, sizeof(mapcmd));
	SG_ReadChunk(SG_ID('M', 'P', 'C', 'M'), mapcmd, SG_MPCM_SIZE);
	SG_ReadChunk(SG_ID('C', 'V', 'C', 'N'), &count, 4);
	count = LittleLong(count);

	for (i = 0; i < count; i++)
	{
		char *cname = SG_ReadChunkAlloc(SG_ID('C', 'V', 'A', 'R'), NULL);
		char *cvalue = SG_ReadChunkAlloc(SG_ID('V', 'A', 'L', 'U'), NULL);
		int flags;

		SG_ReadChunk(SG_ID('F', 'L', 'G', 'S'), &flags, 4);
		flags = LittleLong(flags);

		if (!Cvar_Find(cname))
		{
			Cvar_Get(cname, "", flags);
		}

		Cvar_ForceSetUnprotected(cname, cvalue);

		Z_Free(cname);
		Z_Free(cvalue);
	}

	/* 4. new server, game chunks, map in "loading a save" mode */
	if (sg_host.StartLoaded)
	{
		level = sg_host.StartLoaded(mapcmd);
	}
	else
	{
		/* no game-module host: the level restarts from its map, at once
		   (as "map" would) */
		Cmd_ExecuteString(va("map %s", mapcmd));
		level = false;
	}

	if (SG_PeekChunk() == SG_ID('S', 'P', 'N', 'M'))
	{
		SG_ReadChunk(SG_ID('S', 'P', 'N', 'M'), &count, 4);
		count = LittleLong(count);

		for (i = 0; i < count; i++)
		{
			char *pkg = SG_ReadChunkAlloc(SG_ID('S', 'P', 'I', 'D'), NULL);

			SP_Register(pkg, SP_OWNER_SERVER);
			Z_Free(pkg);
		}
	}

	/* 5. level section */
	if (level || (!sg_host.StartLoaded && (SG_PeekChunk() == SG_ID('F', 'R', 'N', 'M'))))
	{
		int frame;
		char *cs;
		int ports[SG_PRTS_COUNT];

		SG_ReadChunk(SG_ID('F', 'R', 'N', 'M'), &frame, 4);
		frame = LittleLong(frame);

		cs = Z_Malloc(SG_CFST_SIZE + SG_CFSX_SIZE + 1);
		SG_ReadChunk(SG_ID('C', 'F', 'S', 'T'), cs, SG_CFST_SIZE);

		if (SG_PeekChunk() == SG_ID('C', 'F', 'S', 'X'))
		{
			SG_ReadChunk(SG_ID('C', 'F', 'S', 'X'), cs + SG_CFST_SIZE, SG_CFSX_SIZE);
		}

		SG_ReadChunk(SG_ID('P', 'R', 'T', 'S'), ports, sizeof(ports));

		for (i = 0; i < SG_PRTS_COUNT; i++)
		{
			ports[i] = LittleLong(ports[i]);
		}

		if (level)
		{
			if (sg_host.SetFrameNumber)
			{
				sg_host.SetFrameNumber(frame);
			}

			if (sg_host.SetConfigString)
			{
				for (i = 0; i < SG_CFST_COUNT; i++)
				{
					cs[i * 64 + 63] = 0;
					sg_host.SetConfigString(i, cs + i * 64);
				}

				for (i = 0; i < SG_CFSX_COUNT; i++)
				{
					char *x = cs + SG_CFST_SIZE + i * 64;

					x[63] = 0;
					sg_host.SetConfigString(SG_CFSX_FIRST + i, x);
				}
			}

			if (sg_host.SetPortals)
			{
				sg_host.SetPortals(ports);
			}
			else
			{
				CM_SetPortalStates(ports, SG_PRTS_COUNT);
			}

			if (sg_host.ReadLevel)
			{
				sg_host.ReadLevel();
			}
		}

		Z_Free(cs);
	}

	/* 6. */
	SG_Close();
	Sys_Remove(cur);
	Cvar_Set("paused", "1");
	Com_Printf("Done.\n");
}

static qboolean
SG_SaveExists(const char *name)
{
	char path[MAX_OSPATH];

	SG_SavePath(name, path, sizeof(path));
	return FS_HostFileExists(path);
}

static void
SG_QuickSave_f(void)
{
	int i = ((int)Cvar_VariableValue("sv_quicksave_index") + 1) % 4;

	if (i < 0)
	{
		i = 0;
	}

	Cmd_ExecuteString(va("save quick%i", i));
	Cvar_SetValue("sv_quicksave_index", (float)i);
}

static void
SG_QuickLoad_f(void)
{
	int i = (int)Cvar_VariableValue("sv_quicksave_index");
	int tries;

	for (tries = 0; tries < 4; tries++)
	{
		/* checked through the search path (SPEC-303 §4) */
		if (FS_LoadFile2(va("save/quick%i.sof", (i % 4 + 4) % 4), NULL, 0) >= 0)
		{
			Cbuf_AddText(va("load quick%i\n", (i % 4 + 4) % 4));
			return;
		}

		i = (i + 1) % 4;
	}

	Cbuf_AddText("load auto\n");
}

static void
SG_NewSave_f(void)
{
	int i;

	for (i = 0; i < 100; i++)
	{
		char n[16];

		Com_sprintf(n, sizeof(n), "save%02i", i);

		if (!SG_SaveExists(n))
		{
			Cmd_ExecuteString(va("save %s", n));
			return;
		}
	}

	Com_Printf("No free save slot available\n");
}

static void
SG_Wipe_f(void)
{
	char path[MAX_OSPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf("USAGE: wipe <name>\n");
		return;
	}

	if (!Q_stricmp(Cmd_Argv(1), "auto"))
	{
		Com_Printf("Can't wipe 'auto'\n");
		return;
	}

	if (SG_BadName(Cmd_Argv(1)))
	{
		return;
	}

	SG_SavePath(Cmd_Argv(1), path, sizeof(path));
	Sys_Remove(path);
	Com_Printf("%s has been wiped\n", Cmd_Argv(1));
}

/* COMM text and SHOT image of a save (menus, SPEC-303 §5) */
qboolean
SG_ReadInfo(const char *name, char *comment, size_t commentsize, byte *shot)
{
	char path[MAX_OSPATH];
	FILE *f;
	unsigned id, len, sum;
	qboolean ok = false;

	Q_strlcpy(comment, "Invalid/savegame/", commentsize);
	SG_SavePath(name, path, sizeof(path));
	f = Q_fopen(path, "rb");

	if (!f)
	{
		return false;
	}

	sg_file = f;
	sg_writing = false;

	if (SG_GetLong(&id) && (id == SG_ID('C', 'O', 'M', 'M')) && SG_GetLong(&len) &&
		(len == SG_COMM_SIZE) && SG_GetLong(&sum))
	{
		char c[SG_COMM_SIZE + 1];

		if (fread(c, 1, SG_COMM_SIZE, f) == SG_COMM_SIZE)
		{
			c[SG_COMM_SIZE] = 0;
			Q_strlcpy(comment, c, commentsize);
			ok = true;
		}

		fseek(f, 4, SEEK_CUR);

		if (shot && SG_GetLong(&id) && (id == SG_ID('S', 'H', 'O', 'T')) &&
			SG_GetLong(&len) && (len == SG_SHOT_SIZE) && SG_GetLong(&sum))
		{
			if (fread(shot, 1, SG_SHOT_SIZE, f) != SG_SHOT_SIZE)
			{
				memset(shot, 0, SG_SHOT_SIZE);
			}
		}
	}

	fclose(f);
	sg_file = NULL;

	return ok;
}

/* ---------------------------------------------------------------------
 * OpenMerc diagnostics: om_savecheck <file> verifies every chunk
 * --------------------------------------------------------------------- */

static void
SG_Check_f(void)
{
	char path[MAX_OSPATH];
	FILE *f;
	int chunks = 0, bad = 0;
	long total = 0;
	unsigned id, len, sum, marker;
	byte *buf;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("usage: om_savecheck <name or path>\n");
		return;
	}

	if (strchr(Cmd_Argv(1), '/') || strchr(Cmd_Argv(1), '\\') || strchr(Cmd_Argv(1), ':'))
	{
		Q_strlcpy(path, Cmd_Argv(1), sizeof(path));
	}
	else
	{
		SG_SavePath(Cmd_Argv(1), path, sizeof(path));
	}

	f = Q_fopen(path, "rb");

	if (!f)
	{
		Com_Printf("om_savecheck: can't open %s\n", path);
		return;
	}

	sg_file = f;
	sg_writing = false;

	while (SG_GetLong(&id))
	{
		char n[5];

		if (!SG_GetLong(&len) || !SG_GetLong(&sum) || (len > 64 * 1024 * 1024))
		{
			bad++;
			break;
		}

		buf = Z_Malloc((int)len + 1);

		if (len && (fread(buf, 1, len, f) != len))
		{
			Z_Free(buf);
			bad++;
			break;
		}

		if (!SG_GetLong(&marker) || (marker != SG_END_MARKER) ||
			(SG_Checksum(buf, (int)len) != sum))
		{
			Com_Printf("chunk %i %s: bad %s\n", chunks, SG_IdString(id, n),
					(marker != SG_END_MARKER) ? "marker" : "checksum");
			bad++;
		}

		if ((id == SG_ID('C', 'O', 'M', 'M')) || (id == SG_ID('M', 'P', 'C', 'M')) ||
			(id == SG_ID('S', 'P', 'I', 'D')))
		{
			buf[len] = 0;
			Com_Printf("%s \"%s\"\n", SG_IdString(id, n), (char *)buf);
		}

		Z_Free(buf);
		chunks++;
		total += 16 + len;
	}

	fclose(f);
	sg_file = NULL;

	Com_Printf("%s: %i chunks, %li bytes, %i bad\n", path, chunks, total, bad);
}

void
SG_InitCommands(void)
{
	/* the SoF commands replace Quake II's save/load of the server */
	if (Cmd_Exists("save"))
	{
		Cmd_RemoveCommand("save");
	}

	if (Cmd_Exists("load"))
	{
		Cmd_RemoveCommand("load");
	}

	Cmd_AddCommand("wipe", SG_Wipe_f);
	Cmd_AddCommand("load", SG_Load_f);
	Cmd_AddCommand("quickload", SG_QuickLoad_f);
	Cmd_AddCommand("quicksave", SG_QuickSave_f);
	Cmd_AddCommand("save", SG_Save_f);
	Cmd_AddCommand("newsave", SG_NewSave_f);
	Cmd_AddCommand("om_savecheck", SG_Check_f);
}
