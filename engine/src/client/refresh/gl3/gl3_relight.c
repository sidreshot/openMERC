/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: lightmaps re-baked on the player's machine.
 *
 * omlight (src/omlight, built next to the engine) re-lights a map from the
 * player's own game data and writes the new lighting lump to
 * <user dir>/lightmaps/<map>.oml. When a SoF map is loaded, a valid cache
 * file replaces the map's lighting lump in memory; the BSP itself is never
 * changed (the map checksum and the server side stay those of the original).
 * Without a cache, the baker is started once in the background at low
 * priority; its result is used from the next load of the map on.
 *
 * Cache file: "OMLT", int version 2, int lump length, uint32 hash lo, hi
 * (FNV-1a 64 of the original lighting lump), then the new lump, then the
 * light directions in the same layout (deluxe map: per sample the mean
 * direction the light comes from, world space, byte = 128 + 127 x; its length
 * is the directional share of the light). A map whose lighting differs from
 * the one baked from (another version of the map) is re-baked, and so is a
 * version-1 cache without directions.
 *
 * The directions are uploaded as one texture per lightmap page (GL_TEXTURE10)
 * for the relief lighting of the world surfaces (r_om_bump, gl3_shaders.c).
 *
 * cvar r_om_relight: 0 original lighting, 1 use the cache and bake missing
 * maps (default), 2 use the cache only.
 */

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <time.h>

#include "header/local.h"

enum { RELIGHT_VERSION = 2, RELIGHT_HEADER = 20, RELIGHT_MAXSPAWN = 64 };

/* light directions of the current world (same layout as its lighting lump), NULL without a cache */
static byte *deluxe;
static int deluxesize;
static const byte *deluxebase; /* the world's lighting data the surfaces' samples point into */
static GLuint dxtex[MAX_LIGHTMAPS], dxneutral;
static byte *dxbuf; /* the page being built, BLOCK_WIDTH x BLOCK_HEIGHT RGBA */

/* a running bake is not started again before this many seconds (its log file is fresh) */
#define RELIGHT_BUSY_SECONDS (20 * 60)

static cvar_t *r_om_relight;
static char spawned[RELIGHT_MAXSPAWN][MAX_QPATH];
static int numspawned;

static unsigned long long
Relight_Hash(const byte *p, int n)
{
	unsigned long long h = 1469598103934665603ULL;
	int i;

	for (i = 0; i < n; i++)
	{
		h ^= p[i];
		h *= 1099511628211ULL;
	}

	return h;
}

/* "maps/dm/nycdm4.bsp" -> "dm_nycdm4" */
static qboolean
Relight_CacheName(const char *mapname, char *out, size_t size)
{
	const char *s = mapname;
	size_t n = 0;

	if (!Q_strncasecmp(s, "maps/", 5))
	{
		s += 5;
	}

	while (*s && (n + 1 < size))
	{
		if (!Q_strcasecmp(s, ".bsp"))
		{
			break;
		}

		out[n++] = (*s == '/' || *s == '\\') ? '_' : *s;
		s++;
	}

	out[n] = 0;
	return (n > 0) && !strstr(out, "..") && !strchr(out, ':');
}

static qboolean
Relight_Exists(const char *path, time_t *mtime)
{
	struct stat st;

	if (stat(path, &st) != 0)
	{
		return false;
	}

	if (mtime)
	{
		*mtime = st.st_mtime;
	}

	return true;
}

static void
Relight_Mkdir(const char *path)
{
#ifdef _WIN32
	_mkdir(path);
#else
	mkdir(path, 0755);
#endif
}

/* the directory of the running executable, with a trailing slash */
static qboolean
Relight_BinDir(char *out, size_t size)
{
	char *slash;
#ifdef _WIN32
	DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size);

	if ((n == 0) || (n >= size))
	{
		return false;
	}

	slash = strrchr(out, '\\');
#else
	ssize_t n = readlink("/proc/self/exe", out, size - 1);

	if (n <= 0)
	{
		return false;
	}

	out[n] = 0;
	slash = strrchr(out, '/');
#endif

	if (!slash)
	{
		return false;
	}

	slash[1] = 0;
	return true;
}

/* the game directories holding paks, in the engine's search order (later ones override) */
static int
Relight_GameDirs(const char *bindir, char dirs[4][MAX_OSPATH])
{
	cvar_t *basedir = ri.Cvar_Get("basedir", ".", 0);
	cvar_t *cddir = ri.Cvar_Get("cddir", "", 0);
	cvar_t *game = ri.Cvar_Get("game", "", 0);
	int n = 0;

	if (cddir->string[0])
	{
		Com_sprintf(dirs[n++], MAX_OSPATH, "%s/base", cddir->string);
	}

	Com_sprintf(dirs[n++], MAX_OSPATH, "%s/base", basedir->string);
	Com_sprintf(dirs[n++], MAX_OSPATH, "%sopenmerc", bindir);

	if (game->string[0] && Q_stricmp(game->string, "base"))
	{
		Com_sprintf(dirs[n++], MAX_OSPATH, "%s/%s", basedir->string, game->string);
	}

	return n;
}

static void
Relight_Spawn(const char *mapname, const char *cachename)
{
	char bindir[MAX_OSPATH], exe[MAX_OSPATH], dir[MAX_OSPATH], out[MAX_OSPATH], logpath[MAX_OSPATH];
	char dirs[4][MAX_OSPATH];
	time_t mtime;
	int i, ndirs;

	for (i = 0; i < numspawned; i++)
	{
		if (!strcmp(spawned[i], cachename))
		{
			return;
		}
	}

	if (numspawned < RELIGHT_MAXSPAWN)
	{
		Q_strlcpy(spawned[numspawned++], cachename, MAX_QPATH);
	}

	if (!Relight_BinDir(bindir, sizeof(bindir)))
	{
		return;
	}

#ifdef _WIN32
	Com_sprintf(exe, sizeof(exe), "%somlight.exe", bindir);
#else
	Com_sprintf(exe, sizeof(exe), "%somlight", bindir);
#endif

	if (!Relight_Exists(exe, NULL))
	{
		R_Printf(PRINT_DEVELOPER, "relight: %s not found\n", exe);
		return;
	}

	Com_sprintf(dir, sizeof(dir), "%s/lightmaps", ri.FS_Gamedir());
	Relight_Mkdir(dir);
	Com_sprintf(out, sizeof(out), "%s/%s.oml", dir, cachename);
	Com_sprintf(logpath, sizeof(logpath), "%s/%s.log", dir, cachename);

	/* a bake of this map started recently (by an earlier session) may still run */
	if (Relight_Exists(logpath, &mtime) && (time(NULL) - mtime < RELIGHT_BUSY_SECONDS))
	{
		R_Printf(PRINT_ALL, "relight: %s is being baked\n", mapname);
		return;
	}

	ndirs = Relight_GameDirs(bindir, dirs);

#ifdef _WIN32
	{
		char cmd[4096];
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		SECURITY_ATTRIBUTES sa;
		HANDLE log;

		Com_sprintf(cmd, sizeof(cmd), "\"%s\" -map \"%s\"", exe, mapname);

		for (i = 0; i < ndirs; i++)
		{
			Q_strlcat(cmd, " -base \"", sizeof(cmd));
			Q_strlcat(cmd, dirs[i], sizeof(cmd));
			Q_strlcat(cmd, "\"", sizeof(cmd));
		}

		Q_strlcat(cmd, " -out \"", sizeof(cmd));
		Q_strlcat(cmd, out, sizeof(cmd));
		Q_strlcat(cmd, "\"", sizeof(cmd));

		memset(&sa, 0, sizeof(sa));
		sa.nLength = sizeof(sa);
		sa.bInheritHandle = TRUE;
		log = CreateFileA(logpath, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL, NULL);

		memset(&si, 0, sizeof(si));
		si.cb = sizeof(si);

		if (log != INVALID_HANDLE_VALUE)
		{
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = NULL;
			si.hStdOutput = log;
			si.hStdError = log;
		}

		if (CreateProcessA(NULL, cmd, NULL, NULL, log != INVALID_HANDLE_VALUE,
				CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi))
		{
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			R_Printf(PRINT_ALL, "relight: baking %s in the background (used from the next load)\n", mapname);
		}
		else
		{
			R_Printf(PRINT_ALL, "relight: cannot start %s (error %lu)\n", exe, GetLastError());
		}

		if (log != INVALID_HANDLE_VALUE)
		{
			CloseHandle(log);
		}
	}
#else
	{
		const char *argv[4 + 2 * 4 + 3];
		int argc = 0;
		pid_t pid;

		argv[argc++] = exe;
		argv[argc++] = "-map";
		argv[argc++] = mapname;

		for (i = 0; i < ndirs; i++)
		{
			argv[argc++] = "-base";
			argv[argc++] = dirs[i];
		}

		argv[argc++] = "-out";
		argv[argc++] = out;
		argv[argc] = NULL;

		/* double fork: the baker is not a child the engine has to reap */
		pid = fork();

		if (pid == 0)
		{
			if (fork() == 0)
			{
				int fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				setsid();

				if (fd >= 0)
				{
					dup2(fd, 1);
					dup2(fd, 2);
					close(fd);
				}

				if (nice(10) == -1)
				{
					/* keep the normal priority */
				}

				execv(exe, (char *const *)argv);
			}

			_exit(0);
		}

		if (pid > 0)
		{
			waitpid(pid, NULL, 0);
			R_Printf(PRINT_ALL, "relight: baking %s in the background (used from the next load)\n", mapname);
		}
	}
#endif
}

void
GL3_Relight_Apply(const char *mapname, byte *lightdata, int size)
{
	char cachename[MAX_QPATH], path[MAX_QPATH];
	unsigned long long hash, stored;
	byte *buf;
	int len;

	if (!r_om_relight)
	{
		r_om_relight = ri.Cvar_Get("r_om_relight", "1", CVAR_ARCHIVE);
	}

	/* a new world: the light directions of the previous one are gone */
	free(deluxe);
	deluxe = NULL;
	deluxesize = 0;
	deluxebase = NULL;

	if (!r_om_relight->value || !Relight_CacheName(mapname, cachename, sizeof(cachename)))
	{
		return;
	}

	hash = Relight_Hash(lightdata, size);
	Com_sprintf(path, sizeof(path), "lightmaps/%s.oml", cachename);
	len = ri.FS_LoadFile(path, (void **)&buf);

	if (buf)
	{
		if ((len == RELIGHT_HEADER + 2 * size) && !memcmp(buf, "OMLT", 4))
		{
			int version = LittleLong(((int *)buf)[1]);
			int ll = LittleLong(((int *)buf)[2]);

			stored = (unsigned long long)(unsigned int)LittleLong(((int *)buf)[3]) |
				((unsigned long long)(unsigned int)LittleLong(((int *)buf)[4]) << 32);

			if ((version == RELIGHT_VERSION) && (ll == size) && (stored == hash))
			{
				memcpy(lightdata, buf + RELIGHT_HEADER, size);
				/* the light directions (deluxe map) follow, same layout */
				deluxe = malloc(size);
				if (deluxe)
				{
					memcpy(deluxe, buf + RELIGHT_HEADER + size, size);
					deluxesize = size;
					deluxebase = lightdata;
				}
				ri.FS_FreeFile(buf);
				R_Printf(PRINT_ALL, "relight: %s uses the re-baked lightmaps\n", mapname);
				return;
			}
		}

		ri.FS_FreeFile(buf);
		R_Printf(PRINT_ALL, "relight: %s is out of date\n", path);
	}

	if (r_om_relight->value == 1)
	{
		Relight_Spawn(mapname, cachename);
	}
}

/* ---------------------------------------------------------------------
 * light-direction pages (r_om_bump): built beside the lightmap pages
 * --------------------------------------------------------------------- */

static void
Deluxe_ClearBuffer(void)
{
	int i;

	for (i = 0; i < BLOCK_WIDTH * BLOCK_HEIGHT; i++)
	{
		dxbuf[i * 4 + 0] = dxbuf[i * 4 + 1] = dxbuf[i * 4 + 2] = 128; /* no direction */
		dxbuf[i * 4 + 3] = 255;
	}
}

/* lightmap building starts: the old pages are freed */
void
GL3_Deluxe_BeginBuild(void)
{
	int i;

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (dxtex[i])
		{
			glDeleteTextures(1, &dxtex[i]);
			dxtex[i] = 0;
		}
	}

	if (!deluxe)
	{
		free(dxbuf);
		dxbuf = NULL;
		return;
	}

	if (!dxbuf)
	{
		dxbuf = malloc((size_t)BLOCK_WIDTH * BLOCK_HEIGHT * 4);
	}

	if (dxbuf)
	{
		Deluxe_ClearBuffer();
	}
}

/* a surface got its place on the current page: its style-0 directions go to the same place */
void
GL3_Deluxe_Surface(const msurface_t *surf)
{
	int smax = surf->lmsize[0], tmax = surf->lmsize[1], i, j;
	ptrdiff_t ofs;
	const byte *src;

	if (!dxbuf || !deluxe || !surf->samples || !deluxebase)
	{
		return;
	}

	ofs = surf->samples - deluxebase;

	if ((ofs < 0) || (ofs + (ptrdiff_t)smax * tmax * 3 > deluxesize))
	{
		return;
	}

	src = deluxe + ofs;

	for (j = 0; j < tmax; j++)
	{
		byte *dst = dxbuf + ((size_t)(surf->light_t + j) * BLOCK_WIDTH + surf->light_s) * 4;

		for (i = 0; i < smax; i++, src += 3, dst += 4)
		{
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
		}
	}
}

/* the current page is complete */
void
GL3_Deluxe_UploadPage(int page)
{
	if (!dxbuf || (page < 0) || (page >= MAX_LIGHTMAPS))
	{
		return;
	}

	glGenTextures(1, &dxtex[page]);
	GL3_SelectTMU(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, dxtex[page]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BLOCK_WIDTH, BLOCK_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, dxbuf);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	Deluxe_ClearBuffer();
}

/* binds the directions of a lightmap page (or "no direction") to texture unit 10 */
void
GL3_Deluxe_BindPage(int page)
{
	if (!dxneutral)
	{
		static const byte none[4] = {128, 128, 128, 255};

		glGenTextures(1, &dxneutral);
		GL3_SelectTMU(GL_TEXTURE10);
		glBindTexture(GL_TEXTURE_2D, dxneutral);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, none);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	GL3_SelectTMU(GL_TEXTURE10);
	glBindTexture(GL_TEXTURE_2D, ((page >= 0) && (page < MAX_LIGHTMAPS) && dxtex[page]) ? dxtex[page] : dxneutral);
}
