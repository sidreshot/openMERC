/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The Quake II file system, implements generic file system operations
 * as well as the .pak file format.
 *
 * =======================================================================
 *
 * OpenMerc: the Soldier of Fortune 1.07f virtual filesystem. Mounts
 * <cddir>/base, <basedir>/base, the mod and the user directory (each
 * mount above the ones before it; inside a directory: loose files, pak0..
 * pak9, then the other *.pak in directory order), normalises names (upper
 * case, '\' -> '/', "/BASE/" and "/GHOUL/" prefixes), 20 file handles,
 * FS_LoadFile buffers with "\n\0" appended, links, path/dir/link, the
 * fs_createpak list and the mod change rules. All files the engine writes
 * go below the user directory. Yamagi's raw paths, pk3/zip support and
 * home directory are not used.
 * Implements: SPEC-300, SPEC-301 (Behaviour 4), SPEC-AMEND-001 A
 *
 * =======================================================================
 */

#include "header/common.h"
#include "header/glob.h"
#include <ctype.h> /* toupper */

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define FS_MAX_HANDLES 20          /* handles 1..20 (SPEC-300) */
#define FS_MAX_PACK_ENTRIES 10000
#define FS_PACK_NAME 56
#define FS_PACK_HASH 4096

typedef struct
{
	char name[FS_PACK_NAME];      /* normalised (upper case) */
	char origname[FS_PACK_NAME];  /* as stored in the archive */
	int offset;
	int size;
	int hashnext;
} fsPackFile_t;

typedef struct
{
	char filename[MAX_OSPATH];
	FILE *handle;
	int numfiles;
	fsPackFile_t *files;
	int hash[FS_PACK_HASH];
} fsPack_t;

typedef struct fsSearchPath_s
{
	char dirname[MAX_OSPATH];     /* directory mount, or "" */
	fsPack_t *pack;               /* archive mount, or NULL */
	struct fsSearchPath_s *next;  /* lower priority */
} fsSearchPath_t;

typedef struct
{
	qboolean used;
	char name[MAX_OSPATH];
	FILE *file;
	qboolean frompack;
	int remaining;                /* bytes left in a pack entry */
} fsHandle_t;

typedef struct fsLink_s
{
	char *from;
	char *to;
	int length;
	struct fsLink_s *next;
} fsLink_t;

typedef struct fsCreatePak_s
{
	char *name;
	struct fsCreatePak_s *next;
} fsCreatePak_t;

static fsHandle_t fs_handles[FS_MAX_HANDLES];
static fsLink_t *fs_links;
static fsSearchPath_t *fs_searchpaths;   /* highest priority first */
static fsSearchPath_t *fs_base_searchpaths; /* first mount of the base layer */
static fsCreatePak_t *fs_createpak_list, *fs_createpak_last;

static char fs_gamedir[MAX_OSPATH];       /* most recently mounted directory */
static char fs_userdir[MAX_OSPATH];
static char fs_binbase[MAX_OSPATH];       /* <binary dir>/base, module lookup only */

char datadir[MAX_OSPATH];                 /* Yamagi -datadir (= -basedir) */
qboolean file_from_protected_pak;
/* FS_LoadFileShadowed: matches still to pass over in the search path */
static int fs_skipmatches;

cvar_t *fs_basedir;
cvar_t *fs_cddir;
cvar_t *fs_gamedirvar;
cvar_t *fs_userdirvar;
cvar_t *fs_gamedircvar;
cvar_t *fs_createpak;

/* ---------------------------------------------------------------------
 * names (SPEC-300 Behaviour 3)
 * --------------------------------------------------------------------- */

/*
 * Normalises a requested name. upper receives the lookup form, display
 * the same characters in their original case.
 */
static void
FS_NormalizeName(const char *in, char *upper, char *display, size_t size)
{
	size_t i, n;
	char *p;

	Q_strlcpy(display, in, size);

	for (i = 0; display[i]; i++)
	{
		if (display[i] == '\\')
		{
			display[i] = '/';
		}
	}

	n = strlen(display);

	while ((n > 0) && (display[n - 1] == ' '))
	{
		display[--n] = 0;
	}

	for (i = 0; i <= n; i++)
	{
		upper[i] = (char)toupper((unsigned char)display[i]);
	}

	if ((p = strstr(upper, "/BASE/")) != NULL)
	{
		size_t skip = (p - upper) + 6;

		memmove(upper, upper + skip, strlen(upper + skip) + 1);
		memmove(display, display + skip, strlen(display + skip) + 1);
	}

	if ((p = strstr(upper, "/GHOUL/")) != NULL)
	{
		size_t skip = (p - upper) + 1;

		memmove(upper, upper + skip, strlen(upper + skip) + 1);
		memmove(display, display + skip, strlen(display + skip) + 1);
	}
}

/* normalisation of a path given with a dash switch (SPEC-300) */
static void
FS_NormalizeSwitchValue(char *s, size_t size)
{
	char upper[MAX_OSPATH], display[MAX_OSPATH];

	FS_NormalizeName(s, upper, display, sizeof(upper));

#ifdef _WIN32
	Q_strlcpy(s, upper, size);
#else
	/* case-sensitive hosts keep the case of directory names */
	Q_strlcpy(s, display, size);
#endif
}

static unsigned
FS_HashName(const char *s)
{
	unsigned h = 0;

	while (*s)
	{
		h = h * 31 + (byte)*s++;
	}

	return h & (FS_PACK_HASH - 1);
}

/* ---------------------------------------------------------------------
 * host file access
 * --------------------------------------------------------------------- */

static FILE *
FS_OpenHost(const char *path)
{
	FILE *f = Q_fopen(path, "rb");

#ifndef _WIN32
	/* case-insensitive match of loose files on case-sensitive hosts */
	if (!f)
	{
		char built[MAX_OSPATH], part[MAX_OSPATH];
		const char *s = path;
		const char *slash;

		built[0] = 0;

		if (*s == '/')
		{
			Q_strlcpy(built, "/", sizeof(built));
			s++;
		}

		while (*s)
		{
			DIR *d;
			struct dirent *e;
			qboolean found = false;
			size_t l;

			slash = strchr(s, '/');
			l = slash ? (size_t)(slash - s) : strlen(s);

			if (l >= sizeof(part))
			{
				return NULL;
			}

			memcpy(part, s, l);
			part[l] = 0;

			d = opendir(built[0] ? built : ".");

			if (!d)
			{
				return NULL;
			}

			while ((e = readdir(d)) != NULL)
			{
				if (!Q_stricmp(e->d_name, part))
				{
					Q_strlcpy(part, e->d_name, sizeof(part));
					found = true;
					break;
				}
			}

			closedir(d);

			if (!found)
			{
				return NULL;
			}

			if (built[0] && (built[strlen(built) - 1] != '/'))
			{
				Q_strlcat(built, "/", sizeof(built));
			}

			Q_strlcat(built, part, sizeof(built));
			s += l;

			while (*s == '/')
			{
				s++;
			}
		}

		f = Q_fopen(built, "rb");
	}
#endif

	return f;
}

static int
FS_FileLength(FILE *f)
{
	long pos, end;

	pos = ftell(f);
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	fseek(f, pos, SEEK_SET);

	return (int)end;
}

qboolean
FS_HostFileExists(const char *path)
{
#ifdef _WIN32
	WCHAR wpath[MAX_OSPATH];
	DWORD attr;

	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_OSPATH);
	attr = GetFileAttributesW(wpath);

	if (attr == INVALID_FILE_ATTRIBUTES)
	{
		return false;
	}

	/* hidden, system and directory entries do not count (SPEC-301 B4) */
	return (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN |
				FILE_ATTRIBUTE_SYSTEM)) ? false : true;
#else
	FILE *f = FS_OpenHost(path);

	if (f)
	{
		fclose(f);
		return true;
	}

	return false;
#endif
}

/*
 * Creates any directories needed to store the given filename.
 */
void
FS_CreatePath(const char *path)
{
	char *cur; /* Current '/'. */
	char *old; /* Old '/'. */
	char dir_path[MAX_OSPATH];

	/* no debug print here: the console log creates its path with this */

	if (strstr(path, "..") != NULL)
	{
		Com_Printf("WARNING: refusing to create relative path '%s'.\n", path);
		return;
	}

	Q_strlcpy(dir_path, path, sizeof(dir_path));

	for (cur = dir_path; *cur; cur++)
	{
		if (*cur == '\\')
		{
			*cur = '/';
		}
	}

	cur = old = dir_path;

	while (cur != NULL)
	{
		if ((cur - old) > 1)
		{
			*cur = '\0';

			if (!Sys_IsDir(dir_path))
			{
#ifdef _WIN32
				WCHAR wpath[MAX_OSPATH];

				MultiByteToWideChar(CP_UTF8, 0, dir_path, -1, wpath, MAX_OSPATH);
				CreateDirectoryW(wpath, NULL);
#else
				mkdir(dir_path, 0755);
#endif
			}

			*cur = '/';
		}

		old = cur;
		cur = strchr(old + 1, '/');
	}
}

void
FS_DPrintf(const char *format, ...)
{
	char msg[1024];
	va_list argPtr;

	if (!Com_DeveloperBit(4))
	{
		return;
	}

	va_start(argPtr, format);
	vsnprintf(msg, sizeof(msg), format, argPtr);
	va_end(argPtr);

	Com_Printf("%s", msg);
}

const char *
FS_Gamedir(void)
{
	return fs_gamedir;
}

const char *
FS_Userdir(void)
{
	return fs_userdir;
}

/* ---------------------------------------------------------------------
 * handles
 * --------------------------------------------------------------------- */

static fsHandle_t *
FS_GetFileByHandle(fileHandle_t f)
{
	if ((f < 1) || (f > FS_MAX_HANDLES))
	{
		return NULL;
	}

	return &fs_handles[f - 1];
}

static fsHandle_t *
FS_AllocHandle(fileHandle_t *f)
{
	int i;

	for (i = 0; i < FS_MAX_HANDLES; i++)
	{
		if (!fs_handles[i].used)
		{
			memset(&fs_handles[i], 0, sizeof(fs_handles[i]));
			fs_handles[i].used = true;
			*f = i + 1;
			return &fs_handles[i];
		}
	}

	*f = 0;
	return NULL;
}

/*
 * Other dll's can't just call fclose() on files returned by FS_FOpenFile.
 */
void
FS_FCloseFile(fileHandle_t f)
{
	fsHandle_t *handle = FS_GetFileByHandle(f);

	if (!handle || !handle->used)
	{
		return; /* 0 and unknown handles are ignored */
	}

	if (handle->file)
	{
		fclose(handle->file);
	}

	memset(handle, 0, sizeof(*handle));
}

/* ---------------------------------------------------------------------
 * fs_createpak (SPEC-300 Behaviour 8)
 * --------------------------------------------------------------------- */

static void
FS_RecordCreatePak(const char *normname)
{
	fsCreatePak_t *c;

	if (!fs_createpak || !fs_createpak->value)
	{
		return;
	}

	for (c = fs_createpak_list; c; c = c->next)
	{
		if (!strcmp(c->name, normname))
		{
			return;
		}
	}

	c = Z_Malloc(sizeof(*c));
	c->name = CopyString(normname);

	if (fs_createpak_last)
	{
		fs_createpak_last->next = c;
	}
	else
	{
		fs_createpak_list = c;
	}

	fs_createpak_last = c;
}

void
FS_WritePakList(void)
{
	fsCreatePak_t *c;
	FILE *f;

	if (!fs_createpak || !fs_createpak->value || !fs_createpak_list)
	{
		return;
	}

	f = Q_fopen("paklist.txt", "w");

	if (!f)
	{
		return;
	}

	for (c = fs_createpak_list; c; c = c->next)
	{
		fprintf(f, "$pak %s\n", c->name);
	}

	fclose(f);
}

/* ---------------------------------------------------------------------
 * lookup (SPEC-300 Behaviour 2)
 * --------------------------------------------------------------------- */

static int
FS_PackFind(const fsPack_t *pack, const char *normname)
{
	int i;

	for (i = pack->hash[FS_HashName(normname)]; i >= 0; i = pack->files[i].hashnext)
	{
		if (!strcmp(pack->files[i].name, normname))
		{
			return i;
		}
	}

	return -1;
}

/*
 * Finds the file in the search path. Returns its length and an open
 * handle, or -1 and handle 0.
 *   resolved: when not NULL receives the host path (loose file) or the
 *   normalised name (archive entry).
 */
static int
FS_OpenFileEx(const char *rawname, fileHandle_t *f, qboolean gamedir_only,
		char *resolved, size_t resolvedsize)
{
	char upper[MAX_OSPATH], display[MAX_OSPATH], path[MAX_OSPATH];
	fsSearchPath_t *search;
	fsHandle_t *handle;
	fsLink_t *link;
	FILE *file;

	*f = 0;
	file_from_protected_pak = false;

	if (!rawname || !rawname[0])
	{
		return -1;
	}

	/* links, matched on the name as given (Behaviour 6); a shadowed lookup
	   only walks the search path */
	for (link = fs_skipmatches ? NULL : fs_links; link; link = link->next)
	{
		if (!strncmp(rawname, link->from, link->length))
		{
			Com_sprintf(path, sizeof(path), "%s%s", link->to,
					rawname + link->length);
			file = FS_OpenHost(path);

			if (!file)
			{
				return -1;
			}

			handle = FS_AllocHandle(f);

			if (!handle)
			{
				fclose(file);
				return -1;
			}

			Q_strlcpy(handle->name, path, sizeof(handle->name));
			handle->file = file;

			if (resolved)
			{
				Q_strlcpy(resolved, path, resolvedsize);
			}

			return FS_FileLength(file);
		}
	}

	FS_NormalizeName(rawname, upper, display, sizeof(upper));

	for (search = fs_searchpaths; search; search = search->next)
	{
		if (gamedir_only && (search->pack || strcmp(search->dirname, fs_gamedir)))
		{
			continue;
		}

		if (search->pack)
		{
			fsPack_t *pack = search->pack;
			int i = FS_PackFind(pack, upper);

			if (i < 0)
			{
				continue;
			}

			if (fs_skipmatches > 0)
			{
				fs_skipmatches--;
				continue;
			}

			file = Q_fopen(pack->filename, "rb");

			if (!file)
			{
				Com_Error(ERR_FATAL, "Couldn't reopen %s", pack->filename);
				return -1;
			}

			handle = FS_AllocHandle(f);

			if (!handle)
			{
				fclose(file);
				return -1;
			}

			fseek(file, pack->files[i].offset, SEEK_SET);
			Q_strlcpy(handle->name, pack->files[i].origname, sizeof(handle->name));
			handle->file = file;
			handle->frompack = true;
			handle->remaining = pack->files[i].size;
			file_from_protected_pak = true;

			FS_DPrintf("FindFile: %s found in %s\n", rawname, pack->filename);

			if (resolved)
			{
				Q_strlcpy(resolved, upper, resolvedsize);
			}

			return pack->files[i].size;
		}

		/* loose file */
#ifdef _WIN32
		Com_sprintf(path, sizeof(path), "%s/%s", search->dirname, upper);
#else
		Com_sprintf(path, sizeof(path), "%s/%s", search->dirname, display);
#endif
		file = FS_OpenHost(path);

		if (!file)
		{
			continue;
		}

		if (fs_skipmatches > 0)
		{
			fs_skipmatches--;
			fclose(file);
			continue;
		}

		handle = FS_AllocHandle(f);

		if (!handle)
		{
			fclose(file);
			return -1;
		}

		Com_sprintf(handle->name, sizeof(handle->name), "%s/%s",
				search->dirname, display);
		handle->file = file;

		FS_RecordCreatePak(upper);
		FS_DPrintf("FindFile: %s found in %s\n", rawname, search->dirname);

		if (resolved)
		{
			if (fs_createpak && fs_createpak->value)
			{
				Q_strlcpy(resolved, upper, resolvedsize);
			}
			else
			{
				Q_strlcpy(resolved, handle->name, resolvedsize);
			}
		}

		return FS_FileLength(file);
	}

	FS_DPrintf("FindFile: can't find %s\n", rawname);

	return -1;
}

int
FS_FOpenFile(const char *rawname, fileHandle_t *f, qboolean gamedir_only)
{
	return FS_OpenFileEx(rawname, f, gamedir_only, NULL, 0);
}

/*
 * Exported FS_FOpenFile (SPEC-AMEND-001 A): two arguments, integer
 * handle; a name starting with 0x01 receives the resolved name.
 */
int
FS_SoFOpenHandle(const char *name, int *handle)
{
	fileHandle_t f;
	int len;

	if (!name || !handle)
	{
		return -1;
	}

	if (name[0] == 0x01)
	{
		char resolved[MAX_OSPATH];

		len = FS_OpenFileEx(name + 1, &f, false, resolved, sizeof(resolved));

		if (len >= 0)
		{
			/* the caller's buffer holds at least 128 bytes */
			Q_strlcpy((char *)name, resolved, 128);
		}
	}
	else
	{
		len = FS_OpenFileEx(name, &f, false, NULL, 0);
	}

	*handle = f;
	return len;
}

void
FS_SoFCloseHandle(int handle)
{
	FS_FCloseFile(handle);
}

/*
 * Reads up to size bytes. Pack entries end at their length; a loose file
 * that reads 0 bytes twice is a fatal error (SPEC-300 Behaviour 9).
 */
int
FS_Read(void *buffer, int size, fileHandle_t f)
{
	fsHandle_t *handle = FS_GetFileByHandle(f);
	byte *buf = (byte *)buffer;
	int remaining = size;
	qboolean tried = false;

	if (!handle || !handle->used || !handle->file || (size <= 0))
	{
		return 0;
	}

	if (handle->frompack)
	{
		int r;

		if (remaining > handle->remaining)
		{
			remaining = handle->remaining;
		}

		r = (int)fread(buf, 1, remaining, handle->file);

		if (r < 0)
		{
			r = 0;
		}

		handle->remaining -= r;
		return r;
	}

	while (remaining)
	{
		int r = (int)fread(buf, 1, remaining, handle->file);

		if (r == 0)
		{
			if (!tried)
			{
				tried = true;
				continue;
			}

			if (ferror(handle->file))
			{
				Com_Error(ERR_FATAL, "FS_Read: -1 bytes read");
			}

			if (size - remaining > 0)
			{
				break; /* reading past the end returns fewer bytes */
			}

			Com_Error(ERR_FATAL, "FS_Read: 0 bytes read");
			return 0;
		}

		remaining -= r;
		buf += r;
	}

	return size - remaining;
}

/*
 * Reads count blocks of size bytes; no error if it can't read.
 */
int
FS_FRead(void *buffer, int size, int count, fileHandle_t f)
{
	fsHandle_t *handle = FS_GetFileByHandle(f);
	byte *buf = (byte *)buffer;
	int total = 0;

	if (!handle || !handle->used || !handle->file || (size <= 0))
	{
		return 0;
	}

	while (count-- > 0)
	{
		int want = size;
		int r;

		if (handle->frompack && (want > handle->remaining))
		{
			want = handle->remaining;
		}

		r = want ? (int)fread(buf, 1, want, handle->file) : 0;

		if (r <= 0)
		{
			break;
		}

		if (handle->frompack)
		{
			handle->remaining -= r;
		}

		total += r;
		buf += r;

		if (r < size)
		{
			break;
		}
	}

	return total ? size : 0;
}

/*
 * Whole-file read: the buffer has length + 2 bytes, the file, '\n' and NUL
 * (SPEC-300). A null buffer pointer just returns the length.
 */
int
FS_LoadFile2(const char *path, void **buffer, int pad)
{
	fileHandle_t f;
	fsHandle_t *handle;
	byte *buf;
	int size, got;

	(void)pad;

	size = FS_FOpenFile(path, &f, false);

	if (size < 0)
	{
		if (buffer)
		{
			*buffer = NULL;
		}

		return -1;
	}

	if (!buffer)
	{
		FS_FCloseFile(f);
		return size;
	}

	buf = Z_Malloc(size + 2);
	handle = FS_GetFileByHandle(f);
	got = 0;

	if (size > 0)
	{
		if (handle->frompack)
		{
			got = FS_Read(buf, size, f);
		}
		else
		{
			got = (int)fread(buf, 1, size, handle->file);
		}
	}

	if (got < size)
	{
		memset(buf + (got > 0 ? got : 0), 0, size - (got > 0 ? got : 0));
	}

	buf[size] = '\n';
	buf[size + 1] = 0;
	*buffer = buf;

	FS_FCloseFile(f);

	return size;
}

int
FS_LoadFile(const char *path, void **buffer)
{
	return FS_LoadFile2(path, buffer, 2);
}

/* the copy of path that the first `shadowed` higher-priority copies hide
   (0 = the normal lookup); -1 when there are not that many copies */
int
FS_LoadFileShadowed(const char *path, int shadowed, void **buffer)
{
	int size;

	fs_skipmatches = shadowed > 0 ? shadowed : 0;
	size = FS_LoadFile2(path, buffer, 2);
	fs_skipmatches = 0;
	return size;
}

/* exported FS_LoadFile: third argument accepted and ignored */
int
FS_SoFLoadFile(const char *name, void **buffer, int unused)
{
	(void)unused;
	return FS_LoadFile2(name, buffer, 2);
}

void
FS_FreeFile(void *buffer)
{
	if (buffer == NULL)
	{
		return;
	}

	Z_Free(buffer);
}

/* returns the filename used to open f (archive entries in stored case) */
const char *
FS_GetFilenameForHandle(fileHandle_t f)
{
	fsHandle_t *fsh = FS_GetFileByHandle(f);

	if (fsh && fsh->used)
	{
		return fsh->name;
	}

	return NULL;
}

/* ---------------------------------------------------------------------
 * mounting (SPEC-300 Behaviour 1)
 * --------------------------------------------------------------------- */

/* NULL when the file is not a valid archive (silently ignored) */
static fsPack_t *
FS_LoadPAK(const char *packPath)
{
	dpackheader_t header;
	dpackfile_t *info;
	fsPack_t *pack;
	FILE *handle;
	int i, numFiles;

	handle = Q_fopen(packPath, "rb");

	if (handle == NULL)
	{
		return NULL;
	}

	if (fread(&header, 1, sizeof(header), handle) != sizeof(header) ||
		(LittleLong(header.ident) != IDPAKHEADER))
	{
		fclose(handle);
		return NULL;
	}

	header.dirofs = LittleLong(header.dirofs);
	header.dirlen = LittleLong(header.dirlen);
	numFiles = header.dirlen / (int)sizeof(dpackfile_t);

	if ((header.dirlen < 0) || (header.dirofs < 0) ||
		(numFiles > FS_MAX_PACK_ENTRIES))
	{
		fclose(handle);
		return NULL;
	}

	info = malloc(numFiles ? numFiles * sizeof(dpackfile_t) : 1);

	if (!info)
	{
		fclose(handle);
		return NULL;
	}

	fseek(handle, header.dirofs, SEEK_SET);

	if (numFiles && (fread(info, sizeof(dpackfile_t), numFiles, handle) != (size_t)numFiles))
	{
		free(info);
		fclose(handle);
		return NULL;
	}

	pack = Z_Malloc(sizeof(fsPack_t));
	Q_strlcpy(pack->filename, packPath, sizeof(pack->filename));
	pack->handle = handle;
	pack->numfiles = numFiles;
	pack->files = Z_Malloc((numFiles ? numFiles : 1) * sizeof(fsPackFile_t));

	for (i = 0; i < FS_PACK_HASH; i++)
	{
		pack->hash[i] = -1;
	}

	for (i = 0; i < numFiles; i++)
	{
		fsPackFile_t *pf = &pack->files[i];
		char upper[MAX_OSPATH], display[MAX_OSPATH], raw[FS_PACK_NAME + 1];
		unsigned h;

		memcpy(raw, info[i].name, FS_PACK_NAME);
		raw[FS_PACK_NAME] = 0;
		FS_NormalizeName(raw, upper, display, sizeof(upper));
		Q_strlcpy(pf->name, upper, sizeof(pf->name));
		Q_strlcpy(pf->origname, raw, sizeof(pf->origname));
		pf->offset = LittleLong(info[i].filepos);
		pf->size = LittleLong(info[i].filelen);

		/* the first directory entry of a name wins: keep it at the
		   head of its chain by appending later ones */
		h = FS_HashName(pf->name);
		pf->hashnext = -1;

		if (pack->hash[h] < 0)
		{
			pack->hash[h] = i;
		}
		else
		{
			int j = pack->hash[h];

			while (pack->files[j].hashnext >= 0)
			{
				j = pack->files[j].hashnext;
			}

			pack->files[j].hashnext = i;
		}
	}

	free(info);

	/* every open reopens the archive; the directory is all we need */
	fclose(handle);
	pack->handle = NULL;

	Com_DPrintf("Added packfile %s (%i files)\n", pack->filename, numFiles);

	return pack;
}

static void
FS_PushSearchPath(const char *dirname, fsPack_t *pack)
{
	fsSearchPath_t *search = Z_Malloc(sizeof(fsSearchPath_t));

	if (dirname)
	{
		Q_strlcpy(search->dirname, dirname, sizeof(search->dirname));
	}

	search->pack = pack;
	search->next = fs_searchpaths;
	fs_searchpaths = search;
}

static void
FS_MountPak(const char *path)
{
	fsPack_t *pack = FS_LoadPAK(path);

	if (pack)
	{
		FS_PushSearchPath(NULL, pack);
	}
}

/* names whose last 8 characters are PAK<digit>.PAK are never scanned */
static qboolean
FS_IsNumberedPakName(const char *name)
{
	size_t l = strlen(name);
	const char *t;

	if (l < 8)
	{
		return false;
	}

	t = name + l - 8;

	return (toupper((unsigned char)t[0]) == 'P') &&
		(toupper((unsigned char)t[1]) == 'A') &&
		(toupper((unsigned char)t[2]) == 'K') &&
		(t[3] >= '0') && (t[3] <= '9') &&
		!Q_stricmp(t + 4, ".PAK");
}

/*
 * Mounting a game directory: the directory itself, pak0..pak9, then every
 * other *.pak returned by a directory scan (SPEC-300 Behaviour 1).
 */
static void
FS_AddGameDirectory(const char *dir)
{
	char path[MAX_OSPATH];
	int i;

	Q_strlcpy(fs_gamedir, dir, sizeof(fs_gamedir));

	FS_PushSearchPath(dir, NULL);

	for (i = 0; i < 10; i++)
	{
		Com_sprintf(path, sizeof(path), "%s/pak%i.pak", dir, i);
		FS_MountPak(path);
	}

#ifdef _WIN32
	{
		WIN32_FIND_DATAW fd;
		WCHAR wpattern[MAX_OSPATH];
		HANDLE h;

		Com_sprintf(path, sizeof(path), "%s/*.pak", dir);
		MultiByteToWideChar(CP_UTF8, 0, path, -1, wpattern, MAX_OSPATH);
		h = FindFirstFileW(wpattern, &fd);

		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				char name[MAX_OSPATH];

				if (fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM))
				{
					continue;
				}

				WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name,
						sizeof(name), NULL, NULL);

				if (!strcmp(name, ".") || !strcmp(name, "..") ||
					FS_IsNumberedPakName(name))
				{
					continue;
				}

				Com_sprintf(path, sizeof(path), "%s/%s", dir, name);
				FS_MountPak(path);
			}
			while (FindNextFileW(h, &fd));

			FindClose(h);
		}
	}
#else
	{
		struct dirent **list;
		int n, k;

		/* NTFS enumerates alphabetically; do the same on other hosts */
		n = scandir(dir, &list, NULL, alphasort);

		for (k = 0; k < n; k++)
		{
			const char *name = list[k]->d_name;
			size_t l = strlen(name);
			struct stat st;

			if ((l > 4) && !Q_stricmp(name + l - 4, ".pak") &&
				!FS_IsNumberedPakName(name))
			{
				Com_sprintf(path, sizeof(path), "%s/%s", dir, name);

				if (!stat(path, &st) && S_ISREG(st.st_mode))
				{
					FS_MountPak(path);
				}
			}

			free(list[k]);
		}

		if (n >= 0)
		{
			free(list);
		}
	}
#endif
}

static fsSearchPath_t *
FS_FreeSearchPaths(fsSearchPath_t *start, fsSearchPath_t *end)
{
	fsSearchPath_t *cur = start;

	while (cur && (cur != end))
	{
		fsSearchPath_t *next = cur->next;

		if (cur->pack)
		{
			if (cur->pack->handle)
			{
				fclose(cur->pack->handle);
			}

			Z_Free(cur->pack->files);
			Z_Free(cur->pack);
		}

		Z_Free(cur);
		cur = next;
	}

	return cur;
}

/* directory mount just below the given one (for FS_Gamedir) */
static void
FS_UpdateGamedir(void)
{
	fsSearchPath_t *s;

	for (s = fs_searchpaths; s; s = s->next)
	{
		if (!s->pack)
		{
			Q_strlcpy(fs_gamedir, s->dirname, sizeof(fs_gamedir));
			return;
		}
	}

	fs_gamedir[0] = 0;
}

/*
 * Allows enumerating all of the directories in the search path, highest
 * priority first. OpenMerc adds <binary dir>/base at the end so that the
 * engine's own modules are found (module lookup only, not a mount).
 */
const char *
FS_NextPath(const char *prevPath)
{
	fsSearchPath_t *s;
	qboolean take = (prevPath == NULL);

	for (s = fs_searchpaths; s; s = s->next)
	{
		if (s->pack)
		{
			continue;
		}

		if (take)
		{
			return s->dirname;
		}

		if (prevPath == s->dirname)
		{
			take = true;
		}
	}

	if (take && fs_binbase[0])
	{
		return fs_binbase;
	}

	return NULL;
}

/* ---------------------------------------------------------------------
 * console commands (SPEC-300 Behaviour 6, 7)
 * --------------------------------------------------------------------- */

static void
FS_Path_f(void)
{
	fsSearchPath_t *s;
	fsLink_t *l;

	Com_Printf("Current search path:\n");

	for (s = fs_searchpaths; s; s = s->next)
	{
		if (s == fs_base_searchpaths)
		{
			Com_Printf("----------\n");
		}

		if (s->pack)
		{
			Com_Printf("\n");
		}
		else
		{
			Com_Printf("%s\n", s->dirname);
		}
	}

	Com_Printf("\nLinks:\n");

	for (l = fs_links; l; l = l->next)
	{
		Com_Printf("%s : %s\n", l->from, l->to);
	}
}

/*
 * Creates a filelink_t.
 */
static void
FS_Link_f(void)
{
	fsLink_t *l, **prev;

	if (Cmd_Argc() != 3)
	{
		Com_Printf("USAGE: link <from> <to>\n");
		return;
	}

	/* See if the link already exists. */
	prev = &fs_links;

	for (l = fs_links; l != NULL; l = l->next)
	{
		if (strcmp(l->from, Cmd_Argv(1)) == 0)
		{
			Z_Free(l->to);

			if (strlen(Cmd_Argv(2)) == 0)
			{
				/* Delete it. */
				*prev = l->next;
				Z_Free(l->from);
				Z_Free(l);
				return;
			}

			l->to = CopyString(Cmd_Argv(2));
			return;
		}

		prev = &l->next;
	}

	/* Create a new link. */
	l = Z_Malloc(sizeof(*l));
	l->next = fs_links;
	fs_links = l;
	l->from = CopyString(Cmd_Argv(1));
	l->length = (int)strlen(l->from);
	l->to = CopyString(Cmd_Argv(2));
}

/* archive wildcards: * any run (also '/'), ? one character, \* \? literal,
   case-insensitive */
static qboolean
FS_PakWildcard(const char *pat, const char *name)
{
	while (*pat)
	{
		if ((pat[0] == '\\') && ((pat[1] == '*') || (pat[1] == '?')))
		{
			if (toupper((unsigned char)*name) != toupper((unsigned char)pat[1]))
			{
				return false;
			}

			pat += 2;
			name++;
			continue;
		}

		if (*pat == '*')
		{
			while (*pat == '*')
			{
				pat++;
			}

			if (!*pat)
			{
				return true;
			}

			for (; *name; name++)
			{
				if (FS_PakWildcard(pat, name))
				{
					return true;
				}
			}

			return false;
		}

		if (!*name)
		{
			return false;
		}

		if ((*pat != '?') &&
			(toupper((unsigned char)*pat) != toupper((unsigned char)*name)))
		{
			return false;
		}

		pat++;
		name++;
	}

	return *name == 0;
}

static void
FS_Dir_f(void)
{
	/* output layout observed in the original (black box): a line of 44
	   '=', per mount a header (11 spaces + the directory, or 11 spaces for
	   an archive) and its matches as "%8i <name>" (archive names in their
	   normalised form), a blank line, the totals, the '=' line again */
	static const char *bar = "============================================";
	const char *pattern = (Cmd_Argc() > 1) ? Cmd_Argv(1) : "*.*";
	fsSearchPath_t *s;
	long long bytes = 0;
	int files = 0, folders = 0, paths = 0;

	Com_Printf("%s\n", bar);

	for (s = fs_searchpaths; s; s = s->next)
	{
		paths++;

		if (s->pack)
		{
			int i;

			Com_Printf("           \n");

			for (i = 0; i < s->pack->numfiles; i++)
			{
				if (FS_PakWildcard(pattern, s->pack->files[i].name))
				{
					Com_Printf("%8i %s\n", s->pack->files[i].size,
							s->pack->files[i].name);
					bytes += s->pack->files[i].size;
					files++;
				}
			}

			continue;
		}

		Com_Printf("           %s\n", s->dirname);

#ifdef _WIN32
		{
			char path[MAX_OSPATH];
			WCHAR wpattern[MAX_OSPATH];
			WIN32_FIND_DATAW fd;
			HANDLE h;

			Com_sprintf(path, sizeof(path), "%s/%s", s->dirname, pattern);
			MultiByteToWideChar(CP_UTF8, 0, path, -1, wpattern, MAX_OSPATH);
			h = FindFirstFileW(wpattern, &fd);

			if (h == INVALID_HANDLE_VALUE)
			{
				continue;
			}

			do
			{
				char name[MAX_OSPATH];

				WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name,
						sizeof(name), NULL, NULL);

				if (!strcmp(name, ".") || !strcmp(name, ".."))
				{
					continue;
				}

				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					/* folder line: format UNVERIFIED (OPEN_QUESTIONS.md) */
					Com_Printf("   <DIR> %s\n", name);
					folders++;
				}
				else
				{
					Com_Printf("%8u %s\n", (unsigned)fd.nFileSizeLow, name);
					bytes += fd.nFileSizeLow;
					files++;
				}
			}
			while (FindNextFileW(h, &fd));

			FindClose(h);
		}
#else
		{
			DIR *d = opendir(s->dirname);
			struct dirent *e;

			if (!d)
			{
				continue;
			}

			while ((e = readdir(d)) != NULL)
			{
				char path[MAX_OSPATH];
				struct stat st;

				if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
				{
					continue;
				}

				if (!FS_PakWildcard(pattern, e->d_name) && strcmp(pattern, "*.*"))
				{
					continue;
				}

				Com_sprintf(path, sizeof(path), "%s/%s", s->dirname, e->d_name);

				if (stat(path, &st))
				{
					continue;
				}

				if (S_ISDIR(st.st_mode))
				{
					Com_Printf("   <DIR> %s\n", e->d_name);
					folders++;
				}
				else
				{
					Com_Printf("%8u %s\n", (unsigned)st.st_size, e->d_name);
					bytes += st.st_size;
					files++;
				}
			}

			closedir(d);
		}
#endif
	}

	Com_Printf("\n%lld bytes in %i files and %i folders from %i paths\n",
			bytes, files, folders, paths);
	Com_Printf("%s\n", bar);
}

/* ---------------------------------------------------------------------
 * listing helpers kept from Yamagi
 * --------------------------------------------------------------------- */

/*
 * Create a list of host files that match a criteria.
 */
strlist_t
FS_ListFiles(const char *findname,
		unsigned musthave, unsigned canthave)
{
	strlist_t list;
	const char *s;

	StrList_Init(&list, 0);

	s = Sys_FindFirst(findname, musthave, canthave);

	while (s)
	{
		if (s[strlen(s) - 1] != '.')
		{
			StrList_Append(&list, s);
		}

		s = Sys_FindNext(musthave, canthave);
	}

	Sys_FindClose();

	return list;
}

/*
 * Create a list of files that match a criteria.
 * Searchs are relative to the game directory and use all the search paths
 * including .pak files (archive names compared case-insensitively).
 */
strlist_t
FS_ListFiles2(const char *findname,
		unsigned musthave, unsigned canthave)
{
	strlist_t list;
	fsSearchPath_t *search;

	StrList_Init(&list, 0);

	for (search = fs_searchpaths; search != NULL; search = search->next)
	{
		strlist_t tmplist;
		char path[MAX_OSPATH];
		size_t splen;
		int i;

		if (search->pack)
		{
			if (canthave & SFF_INPACK)
			{
				continue;
			}

			for (i = 0; i < search->pack->numfiles; i++)
			{
				const char *name = search->pack->files[i].origname;

				if (FS_PakWildcard(findname, name) && !StrList_Contains(&list, name))
				{
					StrList_Append(&list, name);
				}
			}

			continue;
		}

		if (musthave & SFF_INPACK)
		{
			continue;
		}

		/* files from host filesystem */
		Com_sprintf(path, sizeof(path), "%s/%s", search->dirname, findname);
		tmplist = FS_ListFiles(path, musthave, canthave);

		splen = strlen(search->dirname);

		for (i = 0; i < tmplist.num; i++)
		{
			const char *s = tmplist.data[i] + splen + 1;

			if (!StrList_Contains(&list, s))
			{
				StrList_Append(&list, s);
			}
		}

		StrList_Free(&tmplist);
	}

	return list;
}

/* the directory names below basedir that hold *.pak files */
strlist_t
FS_ListMods(void)
{
	strlist_t list, dirs;
	char pattern[MAX_OSPATH];
	int i;

	StrList_Init(&list, 0);
	Com_sprintf(pattern, sizeof(pattern), "%s/*", fs_basedir->string);
	dirs = FS_ListFiles(pattern, 0, 0);

	for (i = 0; i < dirs.num; i++)
	{
		strlist_t paks;
		const char *name;

		Com_sprintf(pattern, sizeof(pattern), "%s/*.pak", dirs.data[i]);
		paks = FS_ListFiles(pattern, 0, 0);

		name = strrchr(dirs.data[i], '/');
		name = name ? name + 1 : dirs.data[i];

		if (paks.num && !StrList_Contains(&list, name))
		{
			StrList_Append(&list, name);
		}

		StrList_Free(&paks);
	}

	StrList_Free(&dirs);

	return list;
}

/*
 * This function returns true if a real file (e.g. not something
 * in a pak, something in the file system itself) exists in the
 * current gamedir.
 */
qboolean
FS_FileInGamedir(const char *file)
{
	char name[MAX_OSPATH];

	Com_sprintf(name, sizeof(name), "%s/%s", fs_gamedir, file);

	return FS_HostFileExists(name);
}

/* downloads of archives are mounted on top */
qboolean
FS_AddPAKFromGamedir(const char *pak)
{
	char path[MAX_OSPATH];
	fsPack_t *pack;

	Com_sprintf(path, sizeof(path), "%s/%s", fs_gamedir, pak);
	pack = FS_LoadPAK(path);

	if (!pack)
	{
		return false;
	}

	FS_PushSearchPath(NULL, pack);
	return true;
}

/* Yamagi raw paths are not used; the binary dir is the only one */
const char *
FS_GetNextRawPath(const char *lastRawPath)
{
	if (lastRawPath == NULL)
	{
		return Sys_GetBinaryDir();
	}

	return NULL;
}

/* ---------------------------------------------------------------------
 * mod change (SPEC-300 Behaviour 5)
 * --------------------------------------------------------------------- */

#ifndef DEDICATED_ONLY
void CleanCachedMapsList(void);
#endif

void
FS_BuildGameSpecificSearchPath(const char *dir)
{
	char path[MAX_OSPATH];

	if (!dir)
	{
		dir = "";
	}

	if (strstr(dir, "..") || strchr(dir, '/') || strchr(dir, '\\') ||
		strchr(dir, ':'))
	{
		Com_Printf("Gamedir should be a single filename, not a path\n");
		return;
	}

	/* every mount above the base layer goes, the user directory too */
	fs_searchpaths = FS_FreeSearchPaths(fs_searchpaths, fs_base_searchpaths);

	/* open handles stay readable: each has its own FILE */

	if (dedicated && !dedicated->value)
	{
		Cbuf_AddText("vid_restart\nsnd_restart\n");
	}

	if (!dir[0] || !Q_stricmp(dir, BASEDIRNAME))
	{
		Cvar_FullSet("gamedir", "", CVAR_SERVERINFO | CVAR_NOSET);
		Cvar_FullSet("game", "", CVAR_SERVERINFO | CVAR_LATCH);
	}
	else
	{
		Cvar_FullSet("gamedir", dir, CVAR_SERVERINFO | CVAR_NOSET);
		Com_sprintf(path, sizeof(path), "%s/%s", fs_basedir->string, dir);
		FS_AddGameDirectory(path);
	}

	FS_UpdateGamedir();

#ifndef DEDICATED_ONLY
	CleanCachedMapsList();
#endif

	/* the autoexec rule of SPEC-301 */
	Com_ExecAutoexec();
}

/* ---------------------------------------------------------------------
 * start
 * --------------------------------------------------------------------- */

/* -basedir/-cddir/-game/-user: the value is the next word (normalised) */
static void
FS_SwitchToCvar(const char *sw, const char *cvarname)
{
	int i;

	for (i = 1; i < COM_Argc() - 1; i++)
	{
		if (!strcmp(COM_Argv(i), sw))
		{
			char value[MAX_OSPATH];

			Q_strlcpy(value, COM_Argv(i + 1), sizeof(value));
			FS_NormalizeSwitchValue(value, sizeof(value));
			Cvar_FullSet(cvarname, value, 0);
		}
	}
}

void
FS_InitFilesystem(void)
{
	char path[MAX_OSPATH];
	const char *bindir;

	/* Register FS commands. */
	Cmd_AddCommand("path", FS_Path_f);
	Cmd_AddCommand("link", FS_Link_f);
	Cmd_AddCommand("dir", FS_Dir_f);

	/* dash switches set the values before the cvars are registered */
	FS_SwitchToCvar("-basedir", "basedir");
	FS_SwitchToCvar("-cddir", "cddir");
	FS_SwitchToCvar("-game", "game");
	FS_SwitchToCvar("-user", "user");

	/* Yamagi -datadir: same as -basedir (not normalised) */
	if (datadir[0])
	{
		Cvar_FullSet("basedir", datadir, 0);
	}

	fs_basedir = Cvar_Get("basedir", ".", CVAR_NOSET);
	fs_cddir = Cvar_Get("cddir", "", CVAR_NOSET);
	fs_gamedirvar = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);
	fs_userdirvar = Cvar_Get("user", "./user", CVAR_SERVERINFO | CVAR_NOSET);
	/* "gamedir" is created when a mod is mounted (SPEC-302: set by the
	   engine; absent from the reference cvarlist of a plain start) */
	fs_createpak = Cvar_Get("fs_createpak", "0", 0);

	if (!Q_stricmp(fs_gamedirvar->string, BASEDIRNAME))
	{
		Cvar_FullSet("game", "", CVAR_LATCH | CVAR_SERVERINFO);
	}

	bindir = Sys_GetBinaryDir();

	if (bindir && bindir[0])
	{
		Com_sprintf(fs_binbase, sizeof(fs_binbase), "%s%s", bindir, BASEDIRNAME);
	}

	/* install layout: openMERC unpacked as its own folder inside the Soldier
	   of Fortune folder (<SoF>/openMERC/openMerc.exe). Without -cddir and
	   without game data in <basedir>/base, the parent folder is the game folder
	   when it holds base/pak0.pak. It is only read, never written. */
	if (!fs_cddir->string[0])
	{
		Com_sprintf(path, sizeof(path), "%s/%s/pak0.pak", fs_basedir->string, BASEDIRNAME);

		/* FS_HostFileExists matches the name case-insensitively on
		   case-sensitive hosts (PAK0.PAK of a CD install) */
		if (!FS_HostFileExists(path))
		{
			char parent[MAX_OSPATH];

			Com_sprintf(path, sizeof(path), "%s/..", fs_basedir->string);

			if (Sys_Realpath(path, parent, sizeof(parent)))
			{
				Com_sprintf(path, sizeof(path), "%s/%s/pak0.pak", parent, BASEDIRNAME);

				if (FS_HostFileExists(path))
				{
					FS_NormalizeSwitchValue(parent, sizeof(parent));
					Cvar_FullSet("cddir", parent, CVAR_NOSET);
					Com_Printf("Game data: %s\n", fs_cddir->string);
				}
			}
		}
	}

	/* 1. <cddir>/base, 2. <basedir>/base */
	if (fs_cddir->string[0])
	{
		Com_sprintf(path, sizeof(path), "%s/%s", fs_cddir->string, BASEDIRNAME);
		FS_AddGameDirectory(path);
	}

	Com_sprintf(path, sizeof(path), "%s/%s", fs_basedir->string, BASEDIRNAME);
	FS_AddGameDirectory(path);

	/* 3. the base layer */
	fs_base_searchpaths = fs_searchpaths;

	/* 4. the mod */
	if (fs_gamedirvar->string[0])
	{
		const char *dir = fs_gamedirvar->string;

		if (strstr(dir, "..") || strchr(dir, '/') || strchr(dir, '\\') ||
			strchr(dir, ':'))
		{
			Com_Printf("Gamedir should be a single filename, not a path\n");
		}
		else
		{
			Cvar_FullSet("gamedir", dir, CVAR_SERVERINFO | CVAR_NOSET);
			Com_sprintf(path, sizeof(path), "%s/%s", fs_basedir->string, dir);
			FS_AddGameDirectory(path);
		}
	}

	/* 5. the user directory, used verbatim */
	Q_strlcpy(fs_userdir, fs_userdirvar->string, sizeof(fs_userdir));
	FS_AddGameDirectory(fs_userdir);
}

void
FS_ShutdownFilesystem(void)
{
	int i;

	for (i = 1; i <= FS_MAX_HANDLES; i++)
	{
		FS_FCloseFile(i);
	}

	fs_searchpaths = FS_FreeSearchPaths(fs_searchpaths, NULL);
	fs_base_searchpaths = NULL;
}
