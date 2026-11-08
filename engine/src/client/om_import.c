/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * openMERC: game data from the player's own Soldier of Fortune disc image. When no folder with base/pak0.pak is found
 * at start, the player may pick a disc image (.iso) of the game; the files of its installer (an InstallShield 5
 * cabinet, SETUP/DATA1.HDR + SETUP/DATA1.CAB) that belong in base/ are unpacked into <game folder>/base.
 *
 * A private build may also fetch its owner's image: when an address is given (a file om_image.url next to the game or
 * in its folder, or OM_IMAGE_URL built in from the untracked engine/om_image.url), the image is downloaded first
 * (WinHTTP on Windows, java.net.HttpURLConnection on Android; resumed after a break), unpacked and deleted. The
 * sources name no address.
 *
 * Formats: ISO 9660 (primary volume descriptor, 2048-byte sectors, directory records); InstallShield 5 cabinet as
 * the open-source unshield reads it (common header "ISc(", cabinet descriptor, file table of directory name offsets
 * then file descriptors; a compressed file is a run of chunks, each a 16-bit length and a raw deflate stream).
 */

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif

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
static const char *imp_stage = "openMERC"; /* line above the bar (SDL debug font: ASCII only) */
static Sint64 imp_done, imp_total;         /* bytes shown under the bar when imp_total > 0 */

static void
IMP_Text(float cx, float y, float sc, const char *txt)
{
	SDL_SetRenderScale(imp_ren, sc, sc);
	SDL_RenderDebugText(imp_ren, (cx - strlen(txt) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * sc / 2) / sc, y / sc, txt);
	SDL_SetRenderScale(imp_ren, 1, 1);
}

static void
IMP_Progress(float f)
{
	static int last = -1;
	static Uint64 lastms;
	int pc = (int)(f * 100);
	SDL_FRect r;
	int w = 0, h = 0;
	float sc, ch;
	char line[96];

	SDL_PumpEvents();

	if (pc == last && SDL_GetTicks() - lastms < 300)
	{
		return;
	}

	last = pc;
	lastms = SDL_GetTicks();

	if (imp_win)
	{
		SDL_SetWindowTitle(imp_win, va("openMERC: %d%%", pc));
	}

	if (!imp_ren)
	{
		return;
	}

	SDL_GetCurrentRenderOutputSize(imp_ren, &w, &h);
	sc = (float)(int)(h / 70 > 2 ? (h / 70 < (w / 50) ? h / 70 : w / 50) : 2);
	sc = sc > 6 ? 6 : sc;
	ch = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * sc;
	SDL_SetRenderDrawColor(imp_ren, 18, 22, 28, 255);
	SDL_RenderClear(imp_ren);
	r.x = w * 0.08f;
	r.w = w * 0.84f;
	r.h = ch * 1.6f;
	r.y = (h - r.h) / 2;
	SDL_SetRenderDrawColor(imp_ren, 230, 232, 236, 255);
	IMP_Text(w / 2.0f, r.y - ch * 2.2f, sc, imp_stage);
	SDL_SetRenderDrawColor(imp_ren, 60, 66, 76, 255);
	SDL_RenderFillRect(imp_ren, &r);
	r.w *= f;
	SDL_SetRenderDrawColor(imp_ren, 240, 130, 30, 255);
	SDL_RenderFillRect(imp_ren, &r);

	if (imp_total > 0)
	{
		Com_sprintf(line, sizeof(line), "%d%%   %d / %d MB", pc, (int)(imp_done >> 20), (int)(imp_total >> 20));
	}
	else
	{
		Com_sprintf(line, sizeof(line), "%d%%", pc);
	}

	SDL_SetRenderDrawColor(imp_ren, 200, 204, 212, 255);
	IMP_Text(w / 2.0f, r.y + r.h + ch * 1.2f, sc, line);
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
static qboolean imp_listonly; /* om_importiso ... list: print what would be unpacked */

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

			if (imp_listonly)
			{
				Com_Printf("import list: %s/%s -> base/%s%s%s (%u bytes, packed %u, flags %d, at %u)\n", dir, name, sub,
						sub[0] ? "/" : "", name, esize, cs, flags, dataoff);
				count++;

				if ((Uint64)dataoff + cs > csize)
				{
					failed++;
				}

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

/* Message box with up to three buttons; returns 1..3 for the button, 0 when closed otherwise. */
static int
IMP_Ask3(const char *title, const char *text, const char *b1, const char *b2, const char *b3)
{
	SDL_MessageBoxButtonData bt[3];
	SDL_MessageBoxData d;
	const char *b[3] = {b1, b2, b3};
	int hit = 0, n = 0, i;

	memset(bt, 0, sizeof(bt));
	memset(&d, 0, sizeof(d));

	for (i = 0; i < 3; i++)
	{
		if (b[i])
		{
			bt[n].buttonID = i + 1;
			bt[n].text = b[i];
			n++;
		}
	}

	bt[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
	bt[n - 1].flags |= n > 1 ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0;
	d.flags = SDL_MESSAGEBOX_INFORMATION;
	d.title = title;
	d.message = text;
	d.numbuttons = n;
	d.buttons = bt;

	if (!SDL_ShowMessageBox(&d, &hit) || hit < 0)
	{
		hit = 0;
	}

	return hit;
}

static int
IMP_Ask(const char *title, const char *text, const char *b1, const char *b2)
{
	return IMP_Ask3(title, text, b1, b2, NULL);
}

/* om_importiso <image> [folder]: unpack a disc image by hand (default folder: the game folder of this start) */
static void
IMP_Import_f(void)
{
	char err[256];
	int n;

	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: om_importiso <image.iso> [game folder] [list]\n");
		return;
	}

	imp_listonly = Cmd_Argc() > 3 && !Q_stricmp(Cmd_Argv(3), "list");
	n = IMP_FromImage(Cmd_Argv(1), Cmd_Argc() > 2 ? Cmd_Argv(2) : Cvar_VariableString("basedir"), err, sizeof(err));
	imp_listonly = false;

	if (n > 0)
	{
		Com_Printf("import: %d files unpacked\n", n);
	}
	else
	{
		Com_Printf("import failed: %s\n", err);
	}
}

/* ------------------------------------------------------------------ */
/* Fetching the owner's image (private builds)                        */

#ifndef OM_IMAGE_URL
#define OM_IMAGE_URL ""
#endif

#define DL_BUF (256 * 1024)

/* The file grows at path.part; a later call resumes it (Range) and renames it to path when complete. */
typedef struct
{
	SDL_IOStream *out;
	Sint64 have, total;
} dl_t;

static qboolean
DL_Write(dl_t *d, const void *buf, size_t n)
{
	if (SDL_WriteIO(d->out, buf, n) != n)
	{
		return false;
	}

	d->have += n;

	imp_done = d->have;
	imp_total = d->total;

	if (d->total > 0)
	{
		IMP_Progress((float)((double)d->have / (double)d->total));
	}
	else
	{
		SDL_PumpEvents();
	}

	return true;
}

#ifdef _WIN32
static qboolean
DL_Http(const char *url, dl_t *d, char *part, char *err, size_t errsize)
{
	wchar_t wurl[2048], host[256], upath[2048], range[64];
	URL_COMPONENTS uc;
	HINTERNET ses = NULL, con = NULL, req = NULL;
	DWORD status = 0, sz = sizeof(status), got;
	wchar_t lenbuf[32];
	DWORD lensz = sizeof(lenbuf);
	byte *buf = NULL;
	qboolean ok = false;

	MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 2048);
	memset(&uc, 0, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = host;
	uc.dwHostNameLength = 256;
	uc.lpszUrlPath = upath;
	uc.dwUrlPathLength = 2048;

	if (!WinHttpCrackUrl(wurl, 0, 0, &uc))
	{
		Com_sprintf(err, errsize, "bad address");
		return false;
	}

	ses = WinHttpOpen(L"openMERC", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS, 0);

	if (!ses)
	{
		ses = WinHttpOpen(L"openMERC", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS, 0);
	}

	con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : NULL;
	req = con ? WinHttpOpenRequest(con, L"GET", upath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;

	if (!req)
	{
		Com_sprintf(err, errsize, "no connection (%lu)", GetLastError());
		goto done;
	}

	if (d->have > 0)
	{
		swprintf(range, 64, L"Range: bytes=%lld-", (long long)d->have);
		WinHttpAddRequestHeaders(req, range, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
	}

	if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
			!WinHttpReceiveResponse(req, NULL))
	{
		Com_sprintf(err, errsize, "request failed (%lu)", GetLastError());
		goto done;
	}

	WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
			&status, &sz, WINHTTP_NO_HEADER_INDEX);

	if (status != 200 && status != 206)
	{
		Com_sprintf(err, errsize, "server answered %lu", status);
		goto done;
	}

	if (status == 200 && d->have > 0) /* no resume: start over */
	{
		SDL_CloseIO(d->out);
		d->out = SDL_IOFromFile(part, "wb");
		d->have = 0;

		if (!d->out)
		{
			Com_sprintf(err, errsize, "cannot write %s", part);
			goto done;
		}
	}

	if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenbuf, &lensz,
			WINHTTP_NO_HEADER_INDEX))
	{
		d->total = d->have + _wtoi64(lenbuf);
	}

	buf = malloc(DL_BUF);

	for (;;)
	{
		if (!WinHttpReadData(req, buf, DL_BUF, &got))
		{
			Com_sprintf(err, errsize, "connection lost (%lu)", GetLastError());
			goto done;
		}

		if (!got)
		{
			break;
		}

		if (!DL_Write(d, buf, got))
		{
			Com_sprintf(err, errsize, "cannot write %s (disk full?)", part);
			goto done;
		}
	}

	ok = d->total <= 0 || d->have == d->total;

	if (!ok)
	{
		Com_sprintf(err, errsize, "transfer cut short");
	}

done:
	free(buf);

	if (req)
	{
		WinHttpCloseHandle(req);
	}

	if (con)
	{
		WinHttpCloseHandle(con);
	}

	if (ses)
	{
		WinHttpCloseHandle(ses);
	}

	return ok;
}
#elif defined(__ANDROID__)
static qboolean
DL_JniFailed(JNIEnv *env)
{
	if ((*env)->ExceptionCheck(env))
	{
		(*env)->ExceptionClear(env);
		return true;
	}

	return false;
}

static qboolean
DL_Http(const char *url, dl_t *d, char *part, char *err, size_t errsize)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jclass cu, cc, ci;
	jobject u, c, in = NULL;
	jbyteArray arr;
	jint status, n;
	jlong len;
	char range[64];
	byte *buf = NULL;
	qboolean ok = false;

	if (!env || (*env)->PushLocalFrame(env, 32) < 0)
	{
		Com_sprintf(err, errsize, "no Java environment");
		return false;
	}

	cu = (*env)->FindClass(env, "java/net/URL");
	cc = (*env)->FindClass(env, "java/net/HttpURLConnection");
	ci = (*env)->FindClass(env, "java/io/InputStream");

	if (DL_JniFailed(env) || !cu || !cc || !ci)
	{
		Com_sprintf(err, errsize, "no java.net");
		goto done;
	}

	u = (*env)->NewObject(env, cu, (*env)->GetMethodID(env, cu, "<init>", "(Ljava/lang/String;)V"),
			(*env)->NewStringUTF(env, url));
	c = DL_JniFailed(env) ? NULL : (*env)->CallObjectMethod(env, u,
			(*env)->GetMethodID(env, cu, "openConnection", "()Ljava/net/URLConnection;"));

	if (DL_JniFailed(env) || !c)
	{
		Com_sprintf(err, errsize, "bad address");
		goto done;
	}

	(*env)->CallVoidMethod(env, c, (*env)->GetMethodID(env, cc, "setConnectTimeout", "(I)V"), 30000);
	(*env)->CallVoidMethod(env, c, (*env)->GetMethodID(env, cc, "setReadTimeout", "(I)V"), 60000);

	if (d->have > 0)
	{
		Com_sprintf(range, sizeof(range), "bytes=%lld-", (long long)d->have);
		(*env)->CallVoidMethod(env, c, (*env)->GetMethodID(env, cc, "setRequestProperty",
				"(Ljava/lang/String;Ljava/lang/String;)V"), (*env)->NewStringUTF(env, "Range"),
				(*env)->NewStringUTF(env, range));
	}

	status = (*env)->CallIntMethod(env, c, (*env)->GetMethodID(env, cc, "getResponseCode", "()I"));

	if (DL_JniFailed(env))
	{
		Com_sprintf(err, errsize, "no connection");
		goto done;
	}

	if (status != 200 && status != 206)
	{
		Com_sprintf(err, errsize, "server answered %d", (int)status);
		goto done;
	}

	if (status == 200 && d->have > 0)
	{
		SDL_CloseIO(d->out);
		d->out = SDL_IOFromFile(part, "wb");
		d->have = 0;

		if (!d->out)
		{
			Com_sprintf(err, errsize, "cannot write %s", part);
			goto done;
		}
	}

	len = (*env)->CallLongMethod(env, c, (*env)->GetMethodID(env, cc, "getContentLengthLong", "()J"));

	if (!DL_JniFailed(env) && len > 0)
	{
		d->total = d->have + len;
	}

	in = (*env)->CallObjectMethod(env, c, (*env)->GetMethodID(env, cc, "getInputStream", "()Ljava/io/InputStream;"));
	arr = (DL_JniFailed(env) || !in) ? NULL : (*env)->NewByteArray(env, DL_BUF);

	if (!in || !arr)
	{
		in = NULL;
		Com_sprintf(err, errsize, "no data stream");
		goto done;
	}

	buf = malloc(DL_BUF);

	{
		jmethodID rd = (*env)->GetMethodID(env, ci, "read", "([B)I");

		for (;;)
		{
			n = (*env)->CallIntMethod(env, in, rd, arr);

			if (DL_JniFailed(env))
			{
				Com_sprintf(err, errsize, "connection lost");
				goto done;
			}

			if (n < 0)
			{
				break;
			}

			(*env)->GetByteArrayRegion(env, arr, 0, n, (jbyte *)buf);

			if (n && !DL_Write(d, buf, n))
			{
				Com_sprintf(err, errsize, "cannot write %s (storage full?)", part);
				goto done;
			}
		}
	}

	ok = d->total <= 0 || d->have == d->total;

	if (!ok)
	{
		Com_sprintf(err, errsize, "transfer cut short");
	}

done:
	free(buf);

	if (in)
	{
		(*env)->CallVoidMethod(env, in, (*env)->GetMethodID(env, ci, "close", "()V"));
		DL_JniFailed(env);
	}

	(*env)->PopLocalFrame(env, NULL);
	return ok;
}
#else
static qboolean
DL_Http(const char *url, dl_t *d, char *part, char *err, size_t errsize)
{
	Com_sprintf(err, errsize, "downloading is not built for this system");
	return false;
}
#endif

/* Downloads url to path (via path.part, resumed after a break, up to 5 tries per call). */
static qboolean
DL_Fetch(const char *url, const char *path, char *err, size_t errsize)
{
	char part[MAX_OSPATH];
	dl_t d;
	int tries;
	qboolean ok = false;

	Com_sprintf(part, sizeof(part), "%s.part", path);

	for (tries = 0; tries < 5 && !ok; tries++)
	{
		memset(&d, 0, sizeof(d));
		d.out = SDL_IOFromFile(part, "ab");

		if (!d.out)
		{
			Com_sprintf(err, errsize, "cannot write %s", part);
			return false;
		}

		d.have = SDL_GetIOSize(d.out);
		Com_Printf("download: %s (%lld bytes here)\n", url, (long long)d.have);
		ok = DL_Http(url, &d, part, err, errsize);

		if (d.out)
		{
			SDL_CloseIO(d.out);
		}

		if (!ok)
		{
			Com_Printf("download: %s\n", err);
		}
	}

	if (!ok)
	{
		return false;
	}

	SDL_RemovePath(path);

	if (!SDL_RenamePath(part, path))
	{
		Com_sprintf(err, errsize, "cannot rename %s", part);
		return false;
	}

	Com_Printf("download: %lld bytes -> %s\n", (long long)d.have, path);
	return true;
}

/* The owner's image address: om_image.url (first line) in one of the folders, else the built-in one. */
static qboolean
DL_ImageUrl(const char *const *dirs, int ndirs, char *out, size_t size)
{
	char path[MAX_OSPATH];
	int i;

	for (i = 0; i < ndirs; i++)
	{
		size_t len = 0;
		char *txt;

		if (!dirs[i] || !dirs[i][0])
		{
			continue;
		}

		Com_sprintf(path, sizeof(path), "%s/om_image.url", dirs[i]);
		txt = SDL_LoadFile(path, &len);

		if (txt)
		{
			char *e = txt;

			while (*e && *e != '\r' && *e != '\n')
			{
				e++;
			}

			*e = 0;
			Q_strlcpy(out, txt, size);
			SDL_free(txt);

			if (out[0])
			{
				return true;
			}
		}
	}

	Q_strlcpy(out, OM_IMAGE_URL, size);
	return out[0] != 0;
}

/* om_fetchiso <url> <file>: the downloader alone */
static void
DL_Fetch_f(void)
{
	char err[256];

	if (Cmd_Argc() < 3)
	{
		Com_Printf("Usage: om_fetchiso <url> <file>\n");
		return;
	}

	if (!DL_Fetch(Cmd_Argv(1), Cmd_Argv(2), err, sizeof(err)))
	{
		Com_Printf("download failed: %s\n", err);
	}
}

/* progress window for a download and/or an unpack */
static void
IMP_WinOpen(void)
{
	imp_win = SDL_CreateWindow("openMERC", 640, 200, 0);
	imp_ren = imp_win ? SDL_CreateRenderer(imp_win, NULL) : NULL;
}

static void
IMP_WinClose(void)
{
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
	imp_stage = "openMERC";
	imp_total = 0;
}

/* Unpacks image into gamedir with the progress window; the number of files, or 0 (err set) */
static int
IMP_Unpack(const char *image, const char *gamedir, char *err, size_t errsize)
{
	imp_stage = "Unpacking the game files";
	imp_total = 0;
	IMP_Progress(0);
	return IMP_FromImage(image, gamedir, err, errsize);
}

/* Download + unpack + delete; true when the game data is in place. */
static qboolean
DL_AutoImport(const char *url, const char *gamedir)
{
	char iso[MAX_OSPATH], err[256];
	int n = 0;

	Com_sprintf(iso, sizeof(iso), "%s/openmerc_image.iso", gamedir);
	Com_Printf("import: fetching the disc image -> %s\n", iso);
	IMP_WinOpen();
	imp_stage = "Downloading the disc image";
	IMP_Progress(0);

	if (DL_Fetch(url, iso, err, sizeof(err)))
	{
		n = IMP_Unpack(iso, gamedir, err, sizeof(err));
	}

	IMP_WinClose();

	if (n > 0)
	{
		Com_Printf("import: %d files unpacked, image removed\n", n);
		SDL_RemovePath(iso);
		return true;
	}

	Com_Printf("auto import failed: %s\n", err);
	IMP_Ask("openMERC", va("The disc image could not be downloaded / \xD0\x9E\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 \xD0\xBD\xD0\xB5 \xD0\xB7\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB6\xD0\xB5\xD0\xBD:\n%s", err), "OK", NULL);
	return false;
}

/*
 * Called before the filesystem starts. With game data in one of the folders the filesystem looks in, the game just
 * starts. Otherwise the player is asked: download the image (when an address is known), pick a disc image to unpack
 * into the game folder (Windows: the folder of openMerc.exe, Android: the app's data folder), or quit.
 */
void
CL_OM_EnsureGameData(void)
{
	const char *bin = Sys_GetBinaryDir();
	const char *basedir = Cvar_VariableString("basedir");
	char cand[MAX_OSPATH], gamedir[MAX_OSPATH], err[256], url[1024];
	const char *dirs[3];
	imp_pick_t pick;
	static const SDL_DialogFileFilter filt[] = {{"Disc image (ISO)", "iso"}, {"All files", "*"}};
	qboolean haveurl;
	int n, choice;

	Cmd_AddCommand("om_importiso", IMP_Import_f);
	Cmd_AddCommand("om_fetchiso", DL_Fetch_f);

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

	/* next to the executable; on Android Sys_GetBinaryDir is the app's external files folder (base/, openmerc/) -
	   basedir is "." there and the process cwd is not that folder */
	Com_sprintf(gamedir, sizeof(gamedir), "%s.", (bin && bin[0]) ? bin : "./");
	dirs[0] = gamedir;
	dirs[1] = basedir;
	dirs[2] = bin;
	haveurl = DL_ImageUrl(dirs, 3, url, sizeof(url));

	SDL_InitSubSystem(SDL_INIT_VIDEO);

	for (;;)
	{
		if (haveurl)
		{
			choice = IMP_Ask3("openMERC", "Soldier of Fortune game data (base/pak0.pak) was not found.\nDownload the disc image of your copy and unpack the game files from it, or choose a disc image (.iso) on this device.\n\n\xD0\x9D\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8B \xD0\xB4\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B Soldier of Fortune (base/pak0.pak).\n\xD0\x97\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xB8\xD1\x82\xD1\x8C \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 \xD0\xB4\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB0 \xD0\xB2\xD0\xB0\xD1\x88\xD0\xB5\xD0\xB9 \xD0\xBA\xD0\xBE\xD0\xBF\xD0\xB8\xD0\xB8 \xD0\xB8 \xD1\x80\xD0\xB0\xD1\x81\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB8\xD0\xB7 \xD0\xBD\xD0\xB5\xD0\xB3\xD0\xBE \xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB\xD1\x8B \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B \xD0\xB8\xD0\xBB\xD0\xB8 \xD0\xB2\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 \xD0\xB4\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB0 (.iso) \xD0\xBD\xD0\xB0 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xBC \xD1\x83\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD1\x81\xD1\x82\xD0\xB2\xD0\xB5?",
					"\xD0\xA1\xD0\xBA\xD0\xB0\xD1\x87\xD0\xB0\xD1\x82\xD1\x8C", "\xD0\x92\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C ISO", "\xD0\x92\xD1\x8B\xD1\x85\xD0\xBE\xD0\xB4");
		}
		else
		{
			choice = IMP_Ask3("openMERC", "Soldier of Fortune game data (base/pak0.pak) was not found.\nChoose the disc image (.iso) of your Soldier of Fortune copy: the game files are unpacked from it.\n\n\xD0\x9D\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8B \xD0\xB4\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B Soldier of Fortune (base/pak0.pak).\n\xD0\x92\xD1\x8B\xD0\xB1\xD0\xB5\xD1\x80\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xBE\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 \xD0\xB4\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB0 (.iso) \xD0\xB2\xD0\xB0\xD1\x88\xD0\xB5\xD0\xB9 \xD0\xBA\xD0\xBE\xD0\xBF\xD0\xB8\xD0\xB8 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B: \xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB\xD1\x8B \xD0\xB1\xD1\x83\xD0\xB4\xD1\x83\xD1\x82 \xD1\x80\xD0\xB0\xD1\x81\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD1\x8B \xD0\xB8\xD0\xB7 \xD0\xBD\xD0\xB5\xD0\xB3\xD0\xBE.",
					"\xD0\x92\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C ISO", "\xD0\x92\xD1\x8B\xD1\x85\xD0\xBE\xD0\xB4", NULL);
			choice = choice == 1 ? 2 : 3;
		}

		if (choice == 1)
		{
			if (DL_AutoImport(url, gamedir))
			{
				break;
			}

			continue;
		}

		if (choice != 2)
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
		IMP_WinOpen();
		n = IMP_Unpack(pick.path, gamedir, err, sizeof(err));
		IMP_WinClose();

		if (n > 0)
		{
			Com_Printf("import: %d files unpacked\n", n);
			break;
		}

		Com_Printf("import failed: %s\n", err);
		IMP_Ask("openMERC", va("The image could not be unpacked / \xD0\x9E\xD0\xB1\xD1\x80\xD0\xB0\xD0\xB7 \xD0\xBD\xD0\xB5 \xD1\x80\xD0\xB0\xD1\x81\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD:\n%s", err), "OK", NULL);
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
