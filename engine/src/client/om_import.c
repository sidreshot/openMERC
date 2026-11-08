/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * openMERC: game data from the player's own Soldier of Fortune disc image. When no folder with base/pak0.pak is found
 * at start, the player may pick a disc image (.iso) of the game; the files of its installer (an InstallShield 5
 * cabinet, SETUP/DATA1.HDR + SETUP/DATA1.CAB) that belong in base/ are unpacked into <game folder>/base. Nothing is
 * downloaded: the image is a file the player owns.
 *
 * Formats: ISO 9660 (primary volume descriptor, 2048-byte sectors, directory records); InstallShield 5 cabinet as
 * the open-source unshield reads it (common header "ISc(", cabinet descriptor, file table of directory name offsets
 * then file descriptors; a compressed file is a run of chunks, each a 16-bit length and a raw deflate stream).
 */

#include <SDL3/SDL.h>

#include "header/client.h"
#include "../common/unzip/miniz/miniz.h"

#define ISO_SECTOR 2048
#define IS_SIG 0x28635349 /* "ISc(" */
#define IS_FILE_SPLIT 1
#define IS_FILE_OBFUSCATED 2
#define IS_FILE_COMPRESSED 4
#define IS_FILE_INVALID 8

/* ------------------------------------------------------------------ */
/* ISO 9660                                                           */

typedef struct
{
	SDL_IOStream *io;
} iso_t;

static qboolean
ISO_Read(iso_t *iso, Sint64 pos, void *buf, size_t len)
{
	return SDL_SeekIO(iso->io, pos, SDL_IO_SEEK_SET) == pos && SDL_ReadIO(iso->io, buf, len) == len;
}

/* the record of name (case-insensitive, ";1" and a trailing '.' ignored) in the directory at extent/size */
static qboolean
ISO_FindInDir(iso_t *iso, Uint32 extent, Uint32 size, const char *name, Uint32 *outext, Uint32 *outsize,
		qboolean *isdir)
{
	byte sec[ISO_SECTOR];
	Uint32 s, off;

	for (s = 0; s * ISO_SECTOR < size; s++)
	{
		if (!ISO_Read(iso, (Sint64)(extent + s) * ISO_SECTOR, sec, ISO_SECTOR))
		{
			return false;
		}

		for (off = 0; off < ISO_SECTOR;)
		{
			byte len = sec[off];
			char n[256];
			int nl, i;

			if (len < 34 || off + len > ISO_SECTOR)
			{
				break; /* rest of the sector unused */
			}

			nl = sec[off + 32];

			if (nl > len - 33)
			{
				nl = len - 33;
			}

			for (i = 0; i < nl; i++)
			{
				n[i] = (char)sec[off + 33 + i];
			}

			n[nl] = 0;

			if (strchr(n, ';'))
			{
				*strchr(n, ';') = 0;
			}

			nl = (int)strlen(n);

			if (nl && n[nl - 1] == '.')
			{
				n[nl - 1] = 0;
			}

			if (!Q_stricmp(n, name))
			{
				*outext = sec[off + 2] | (sec[off + 3] << 8) | (sec[off + 4] << 16) | ((Uint32)sec[off + 5] << 24);
				*outsize = sec[off + 10] | (sec[off + 11] << 8) | (sec[off + 12] << 16) | ((Uint32)sec[off + 13] << 24);
				*isdir = (sec[off + 25] & 2) != 0;
				return true;
			}

			off += len;
		}
	}

	return false;
}

/* extent and size of a file by its path ("SETUP/DATA1.HDR") */
static qboolean
ISO_Find(iso_t *iso, const char *path, Uint32 *ext, Uint32 *size)
{
	byte pvd[ISO_SECTOR];
	char part[256];
	Uint32 e, s;
	qboolean dir = true;
	const char *p = path;

	if (!ISO_Read(iso, 16 * ISO_SECTOR, pvd, ISO_SECTOR) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5))
	{
		return false;
	}

	/* root directory record at 156 */
	e = pvd[158] | (pvd[159] << 8) | (pvd[160] << 16) | ((Uint32)pvd[161] << 24);
	s = pvd[166] | (pvd[167] << 8) | (pvd[168] << 16) | ((Uint32)pvd[169] << 24);

	while (*p)
	{
		const char *sl = strchr(p, '/');
		size_t n = sl ? (size_t)(sl - p) : strlen(p);

		if (!dir || n >= sizeof(part))
		{
			return false;
		}

		memcpy(part, p, n);
		part[n] = 0;

		if (!ISO_FindInDir(iso, e, s, part, &e, &s, &dir))
		{
			return false;
		}

		p += n + (sl ? 1 : 0);
	}

	*ext = e;
	*size = s;
	return !dir;
}

/* ------------------------------------------------------------------ */
/* progress window                                                     */

static SDL_Window *imp_win;
static SDL_Renderer *imp_ren;

static void
IMP_Progress(float f)
{
	static int last = -1;
	int pc = (int)(f * 100);
	SDL_FRect r;
	int w = 0, h = 0;

	SDL_PumpEvents();

	if (pc == last)
	{
		return;
	}

	last = pc;

	if (imp_win)
	{
		SDL_SetWindowTitle(imp_win, va("openMERC: %d%%", pc));
	}

	if (!imp_ren)
	{
		return;
	}

	SDL_GetCurrentRenderOutputSize(imp_ren, &w, &h);
	SDL_SetRenderDrawColor(imp_ren, 18, 22, 28, 255);
	SDL_RenderClear(imp_ren);
	r.x = w * 0.08f;
	r.w = w * 0.84f;
	r.h = h * 0.12f;
	r.y = (h - r.h) / 2;
	SDL_SetRenderDrawColor(imp_ren, 60, 66, 76, 255);
	SDL_RenderFillRect(imp_ren, &r);
	r.w *= f;
	SDL_SetRenderDrawColor(imp_ren, 240, 130, 30, 255);
	SDL_RenderFillRect(imp_ren, &r);
	SDL_RenderPresent(imp_ren);
}

/* ------------------------------------------------------------------ */
/* InstallShield 5 cabinet                                             */

static Uint32
RD32(const byte *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((Uint32)p[3] << 24);
}

/* the folder below base/ a file of the cabinet goes to ("" for base itself), or NULL: not game data */
static const char *
IS_BaseSubdir(const char *dir, const char *name, char *out, size_t size)
{
	char d[MAX_OSPATH];
	char *p, *b = NULL;
	size_t n = strlen(name);

	if (n > 4 && (!Q_stricmp(name + n - 4, ".exe") || !Q_stricmp(name + n - 4, ".dll") ||
			!Q_stricmp(name + n - 4, ".ico")))
	{
		return NULL; /* the original programs: openMERC brings its own */
	}

	Q_strlcpy(d, dir, sizeof(d));

	for (p = d; *p; p++)
	{
		if (*p == '\\')
		{
			*p = '/';
		}
	}

	/* the last path component named "base" */
	for (p = d; *p; p++)
	{
		if ((p == d || p[-1] == '/') && !Q_strncasecmp(p, "base", 4) && (p[4] == 0 || p[4] == '/'))
		{
			b = p;
		}
	}

	if (!b)
	{
		/* paks outside a base folder still belong there */
		if (n > 4 && !Q_stricmp(name + n - 4, ".pak"))
		{
			out[0] = 0;
			return out;
		}

		return NULL;
	}

	Q_strlcpy(out, b[4] ? b + 5 : "", size);
	return out;
}

static void
IMP_MkdirP(char *path)
{
	char *p;

	for (p = path + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = 0;
			Sys_Mkdir(path);
			*p = '/';
		}
	}
}

/* unpacks one file; false on a read or format error */
static qboolean
IS_Extract(iso_t *iso, Sint64 cabpos, Uint32 cabsize, Uint32 off, Uint32 csize, Uint32 esize, int flags,
		const char *dest, Uint64 *done, Uint64 total)
{
	FILE *f;
	byte *in = NULL, *out = NULL;
	Uint32 left = csize, written = 0;
	qboolean ok = true;

	if ((Uint64)off + csize > cabsize || (flags & (IS_FILE_SPLIT | IS_FILE_OBFUSCATED)))
	{
		Com_Printf("import: %s: split or obfuscated file not supported\n", dest);
		return false;
	}

	f = Q_fopen(dest, "wb");

	if (!f)
	{
		Com_Printf("import: cannot write %s\n", dest);
		return false;
	}

	in = malloc(65536 + 16);
	out = malloc(4 * 65536);

	if (!(flags & IS_FILE_COMPRESSED))
	{
		while (ok && left)
		{
			Uint32 n = left > 65536 ? 65536 : left;

			ok = ISO_Read(iso, cabpos + off + (csize - left), in, n) && fwrite(in, 1, n, f) == n;
			left -= n;
			written += n;
			*done += n;
		}
	}
	else
	{
		Sint64 pos = cabpos + off;

		while (ok && left >= 2)
		{
			byte lb[2];
			Uint32 n;
			size_t got;

			if (!ISO_Read(iso, pos, lb, 2))
			{
				ok = false;
				break;
			}

			n = lb[0] | (lb[1] << 8);
			pos += 2;
			left -= 2;

			if (n > left || n > 65536 || !ISO_Read(iso, pos, in, n))
			{
				ok = false;
				break;
			}

			pos += n;
			left -= n;
			*done += n + 2;

			/* a raw deflate stream per chunk (64 KB out at most); some cabinets end each with a zlib header */
			got = tinfl_decompress_mem_to_mem(out, 4 * 65536, in, n, 0);

			if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
			{
				got = tinfl_decompress_mem_to_mem(out, 4 * 65536, in, n, TINFL_FLAG_PARSE_ZLIB_HEADER);
			}

			if (got == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || fwrite(out, 1, got, f) != got)
			{
				ok = false;
				break;
			}

			written += (Uint32)got;
			IMP_Progress(total ? (float)((double)*done / (double)total) : 0);
		}
	}

	fclose(f);
	free(in);
	free(out);

	if (ok && written != esize)
	{
		Com_Printf("import: %s: %u bytes, expected %u\n", dest, written, esize);
		ok = false;
	}

	if (!ok)
	{
		Sys_Remove(dest);
	}

	return ok;
}

/* unpacks the game files of the image into gamedir/base; the number of files, or -1 */
static int
IMP_FromImage(const char *image, const char *gamedir, char *err, size_t errsize)
{
	iso_t iso;
	Uint32 hext, hsize, cext, csize, i;
	byte *h = NULL;
	Uint32 version, major, cabdesc, ftoff, dircount, filecount;
	Uint64 total = 0, done = 0;
	int count = 0, failed = 0;

	iso.io = SDL_IOFromFile(image, "rb");

	if (!iso.io)
	{
		Com_sprintf(err, errsize, "cannot open the image: %s", SDL_GetError());
		return -1;
	}

	if (!ISO_Find(&iso, "SETUP/DATA1.HDR", &hext, &hsize) || !ISO_Find(&iso, "SETUP/DATA1.CAB", &cext, &csize))
	{
		Com_sprintf(err, errsize, "not a Soldier of Fortune disc image (no SETUP/DATA1.HDR and DATA1.CAB)");
		SDL_CloseIO(iso.io);
		return -1;
	}

	h = malloc(hsize);

	if (!h || hsize < 64 || !ISO_Read(&iso, (Sint64)hext * ISO_SECTOR, h, hsize) || RD32(h) != IS_SIG)
	{
		Com_sprintf(err, errsize, "DATA1.HDR is not an InstallShield header");
		goto fail;
	}

	version = RD32(h + 4);
	major = 0;

	if ((version >> 24) == 1)
	{
		major = (version >> 12) & 0xf;
	}
	else if ((version >> 24) == 2 || (version >> 24) == 4)
	{
		major = (version & 0xffff) / 100;
	}

	if (major > 5)
	{
		Com_sprintf(err, errsize, "InstallShield %u cabinets are not supported", major);
		goto fail;
	}

	cabdesc = RD32(h + 12);

	if ((Uint64)cabdesc + 0x30 > hsize)
	{
		Com_sprintf(err, errsize, "DATA1.HDR is damaged");
		goto fail;
	}

	ftoff = cabdesc + RD32(h + cabdesc + 0x0c);
	dircount = RD32(h + cabdesc + 0x1c);
	filecount = RD32(h + cabdesc + 0x28);

	if ((Uint64)ftoff + 4ull * (dircount + filecount) > hsize)
	{
		Com_sprintf(err, errsize, "DATA1.HDR is damaged");
		goto fail;
	}

	/* pass 1: the size of what is unpacked (progress); pass 2: unpack */
	for (int pass = 0; pass < 2; pass++)
	{
		for (i = 0; i < filecount; i++)
		{
			Uint32 fdo = ftoff + RD32(h + ftoff + 4 * (dircount + i));
			const byte *fd = h + fdo;
			Uint32 nameoff, diridx, esize, cs, dataoff, dno;
			int flags;
			const char *name, *dir;
			char sub[MAX_OSPATH], dest[MAX_OSPATH];

			if ((Uint64)fdo + 42 > hsize)
			{
				continue;
			}

			nameoff = RD32(fd);
			diridx = RD32(fd + 4);
			flags = fd[8] | (fd[9] << 8);
			esize = RD32(fd + 10);
			cs = RD32(fd + 14);
			dataoff = RD32(fd + 38);

			if ((flags & IS_FILE_INVALID) || diridx >= dircount || ftoff + nameoff >= hsize)
			{
				continue;
			}

			dno = ftoff + RD32(h + ftoff + 4 * diridx);

			if (dno >= hsize || !memchr(h + ftoff + nameoff, 0, hsize - (ftoff + nameoff)) ||
				!memchr(h + dno, 0, hsize - dno))
			{
				continue;
			}

			name = (const char *)h + ftoff + nameoff;
			dir = (const char *)h + dno;

			if (!name[0] || strstr(name, "..") || !IS_BaseSubdir(dir, name, sub, sizeof(sub)) || strstr(sub, ".."))
			{
				continue;
			}

			if (pass == 0)
			{
				total += cs;
				continue;
			}

			Com_sprintf(dest, sizeof(dest), "%s/base/%s%s%s", gamedir, sub, sub[0] ? "/" : "", name);
			IMP_MkdirP(dest);
			Com_Printf("import: %s/%s -> %s (%u bytes)\n", dir, name, dest, esize);

			if (IS_Extract(&iso, (Sint64)cext * ISO_SECTOR, csize, dataoff, cs, esize, flags, dest, &done, total))
			{
				count++;
			}
			else
			{
				failed++;
			}
		}
	}

	free(h);
	SDL_CloseIO(iso.io);

	if (failed || !count)
	{
		Com_sprintf(err, errsize, failed ? "%d files could not be unpacked (see the console log)" :
				"no game files on the image", failed);
		return -1;
	}

	return count;

fail:
	free(h);
	SDL_CloseIO(iso.io);
	return -1;
}

/* ------------------------------------------------------------------ */
/* start-up check                                                      */

static qboolean
IMP_HasData(const char *dir)
{
	char p[MAX_OSPATH];
	FILE *f;

	if (!dir || !dir[0])
	{
		return false;
	}

	Com_sprintf(p, sizeof(p), "%s/base/pak0.pak", dir);
	f = Q_fopen(p, "rb");

	if (!f)
	{
		Com_sprintf(p, sizeof(p), "%s/BASE/PAK0.PAK", dir);
		f = Q_fopen(p, "rb");
	}

	if (f)
	{
		fclose(f);
		return true;
	}

	return false;
}

typedef struct
{
	SDL_AtomicInt done;
	char path[MAX_OSPATH];
} imp_pick_t;

static void SDLCALL
IMP_Picked(void *user, const char *const *files, int filter)
{
	imp_pick_t *p = user;

	(void)filter;
	p->path[0] = 0;

	if (files && files[0])
	{
		Q_strlcpy(p->path, files[0], sizeof(p->path));
	}

	SDL_SetAtomicInt(&p->done, 1);
}

static int
IMP_Ask(const char *title, const char *text, const char *b1, const char *b2)
{
	SDL_MessageBoxButtonData bt[2];
	SDL_MessageBoxData d;
	int hit = -1;

	memset(bt, 0, sizeof(bt));
	memset(&d, 0, sizeof(d));
	bt[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
	bt[0].buttonID = 1;
	bt[0].text = b1;
	bt[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
	bt[1].buttonID = 0;
	bt[1].text = b2;
	d.flags = SDL_MESSAGEBOX_INFORMATION;
	d.title = title;
	d.message = text;
	d.numbuttons = b2 ? 2 : 1;
	d.buttons = bt;
	SDL_ShowMessageBox(&d, &hit);
	return hit;
}

/*
 * Called before the filesystem starts. With game data in one of the folders the filesystem looks in, nothing
 * happens; otherwise the player may pick the disc image to unpack into the game folder (Windows: the folder of
 * openMerc.exe, Android: the app's data folder), or quit.
 */
/* om_importiso <image> [folder]: unpack a disc image by hand (default folder: the game folder of this start) */
static void
IMP_Import_f(void)
{
	char err[256];
	int n;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: om_importiso <image.iso> [game folder]\n");
		return;
	}

	n = IMP_FromImage(Cmd_Argv(1), Cmd_Argc() > 2 ? Cmd_Argv(2) : Cvar_VariableString("basedir"), err, sizeof(err));

	if (n > 0)
	{
		Com_Printf("import: %d files unpacked\n", n);
	}
	else
	{
		Com_Printf("import failed: %s\n", err);
	}
}

void
CL_OM_EnsureGameData(void)
{
	const char *bin = Sys_GetBinaryDir();
	const char *basedir = Cvar_VariableString("basedir");
	char cand[MAX_OSPATH], gamedir[MAX_OSPATH], err[256];
	imp_pick_t pick;
	static const SDL_DialogFileFilter filt[] = {{"Disc image (ISO)", "iso"}, {"All files", "*"}};
	int n;

	Cmd_AddCommand("om_importiso", IMP_Import_f);

	if (Cvar_VariableValue("dedicated") || Cvar_VariableValue("om_noimport"))
	{
		return;
	}

	if (!basedir[0])
	{
		basedir = ".";
	}

	if (IMP_HasData(Cvar_VariableString("cddir")) || IMP_HasData(basedir))
	{
		return;
	}

	Com_sprintf(cand, sizeof(cand), "%s/..", basedir);

	if (IMP_HasData(cand))
	{
		return;
	}

	if (bin && bin[0])
	{
		Com_sprintf(cand, sizeof(cand), "%s.", bin);

		if (IMP_HasData(cand))
		{
			return;
		}

		Com_sprintf(cand, sizeof(cand), "%s..", bin);

		if (IMP_HasData(cand))
		{
			return;
		}
	}

#ifdef __ANDROID__
	Q_strlcpy(gamedir, basedir, sizeof(gamedir));
#else
	Com_sprintf(gamedir, sizeof(gamedir), "%s.", (bin && bin[0]) ? bin : "./");
#endif

	SDL_InitSubSystem(SDL_INIT_VIDEO);

	for (;;)
	{
		if (IMP_Ask("openMERC",
				"Soldier of Fortune game data (base/pak0.pak) was not found.\n"
				"Choose the disc image (.iso) of your Soldier of Fortune copy: the game files are unpacked from it.\n\n"
				"\xD0\x9D\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8B \xD0\xB4\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 "
				"\xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B Soldier of Fortune (base/pak0.pak).\n"
				"\xD0\x92\xD1\x8B\xD0\xB1\xD0\xB5\xD1\x80\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 "
				"\xD0\xB4\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB0 (.iso) \xD0\xB2\xD0\xB0\xD1\x88\xD0\xB5\xD0\xB9 \xD0\xBA\xD0\xBE\xD0\xBF\xD0\xB8\xD0\xB8 "
				"\xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B: \xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB\xD1\x8B \xD0\xB1\xD1\x83\xD0\xB4\xD1\x83\xD1\x82 "
				"\xD1\x80\xD0\xB0\xD1\x81\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD1\x8B \xD0\xB8\xD0\xB7 \xD0\xBD\xD0\xB5\xD0\xB3\xD0\xBE.",
				"ISO...", "Quit / \xD0\x92\xD1\x8B\xD1\x85\xD0\xBE\xD0\xB4") != 1)
		{
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			return; /* the engine goes on and reports the missing data as before */
		}

		memset(&pick, 0, sizeof(pick));
		SDL_ShowOpenFileDialog(IMP_Picked, &pick, NULL, filt, 2, NULL, false);

		while (!SDL_GetAtomicInt(&pick.done))
		{
			SDL_PumpEvents();
			SDL_Delay(20);
		}

		if (!pick.path[0])
		{
			continue;
		}

		Com_Printf("import: %s -> %s/base\n", pick.path, gamedir);
		imp_win = SDL_CreateWindow("openMERC", 480, 96, 0);
		imp_ren = imp_win ? SDL_CreateRenderer(imp_win, NULL) : NULL;
		IMP_Progress(0);
		n = IMP_FromImage(pick.path, gamedir, err, sizeof(err));

		if (imp_ren)
		{
			SDL_DestroyRenderer(imp_ren);
		}

		if (imp_win)
		{
			SDL_DestroyWindow(imp_win);
		}

		imp_ren = NULL;
		imp_win = NULL;

		if (n > 0)
		{
			Com_Printf("import: %d files unpacked\n", n);
			break;
		}

		Com_Printf("import failed: %s\n", err);
		IMP_Ask("openMERC", va("The image could not be unpacked / \xD0\x9E\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 "
				"\xD0\xBD\xD0\xB5 \xD1\x80\xD0\xB0\xD1\x81\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD:\n%s", err),
				"OK", NULL);
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
