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
 * The renderer does not use the directions (they lit the removed r_om_bump
 * relief); the format keeps them so existing caches stay valid.
 *
 * A cache is checked against the map's own lighting before use (Relight_Guard):
 * a bake that lost the lamps is not used, darker face blocks are lifted to 80%.
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

/* a running bake is not started again before this many seconds (its log file is fresh) */
#define RELIGHT_BUSY_SECONDS (20 * 60)

static cvar_t *r_om_relight;
static cvar_t *r_om_relight_minfit; /* lowest R2 of the bake's fit ("fit 2" line of the log) that is used */
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

/* the bake is checked against the map's own lighting, per face lightmap block (faces: the v46 face lump).
   A fit that lost a kind of light (jpn2, irq1a, sud1: the lamps came out at 0) leaves whole rooms dark:
   a third of the lit texels below 45% and the map at 2/3 of its light -> the cache is not used. Otherwise
   a block darker than 80% of the original is scaled back up to it: the bake keeps its shape (shadows), the
   designer's light level stays. */
static qboolean
Relight_Guard(const char *mapname, const byte *orig, byte *lit, int size, const dface46_t *faces, int numfaces)
{
	double sumo = 0, sumn = 0, lit25 = 0, crushed = 0;
	int f, s, i, lifted = 0;

	for (f = 0; f < numfaces; f++)
	{
		const dface46_t *in = faces + f;
		int ofs = LittleLong(in->lightofs), n = in->lm_width * in->lm_height * 3;

		for (s = 0; (s < MAXLIGHTMAPS) && (in->styles[s] != 255); s++)
		{
			const byte *o;
			double a = 0, c = 0;

			if ((ofs < 0) || (n <= 0) || (ofs + (s + 1) * n > size))
			{
				break;
			}

			o = orig + ofs + s * n;

			for (i = 0; i < n; i++)
			{
				a += o[i];
				c += lit[ofs + s * n + i];
			}

			sumo += a;
			sumn += c;
			a /= n;
			c /= n;

			if (a > 25)
			{
				lit25 += n;
				crushed += (c < 0.45 * a) ? n : 0;
			}
		}
	}

	if ((sumn < 0.7 * sumo) && (crushed > 0.3 * lit25))
	{
		R_Printf(PRINT_ALL, "relight: the re-bake of %s is broken (%.0f%% of the light, %.0f%% of the lit area crushed), "
			"the map's own lighting is used\n", mapname, 100.0 * sumn / Q_max(sumo, 1), 100.0 * crushed / Q_max(lit25, 1));
		return false;
	}

	for (f = 0; f < numfaces; f++)
	{
		const dface46_t *in = faces + f;
		int ofs = LittleLong(in->lightofs), n = in->lm_width * in->lm_height * 3;

		for (s = 0; (s < MAXLIGHTMAPS) && (in->styles[s] != 255); s++)
		{
			byte *b;
			const byte *o;
			double a = 0, c = 0, k;

			if ((ofs < 0) || (n <= 0) || (ofs + (s + 1) * n > size))
			{
				break;
			}

			b = lit + ofs + s * n;
			o = orig + ofs + s * n;

			for (i = 0; i < n; i++)
			{
				a += o[i];
				c += b[i];
			}

			if ((a < 8.0 * n) || (c >= 0.8 * a))
			{
				continue;
			}

			lifted++;

			if (c < 0.2 * a)
			{
				/* nothing usable left: the original, as dim as the floor */
				for (i = 0; i < n; i++)
				{
					b[i] = (byte)(o[i] * 0.8f);
				}
				continue;
			}

			k = 0.8 * a / c;

			for (i = 0; i < n; i++)
			{
				b[i] = (byte)Q_min(b[i] * k + 0.5, 255.0);
			}
		}
	}

	if (lifted)
	{
		R_Printf(PRINT_DEVELOPER, "relight: %s: %d lightmap blocks lifted to 80%% of the original\n", mapname, lifted);
	}

	return true;
}

void
GL3_Relight_Apply(const char *mapname, byte *lightdata, int size, const byte *faces, int faceslen)
{
	char cachename[MAX_QPATH], path[MAX_QPATH];
	unsigned long long hash, stored;
	byte *buf;
	int len;

	if (!r_om_relight)
	{
		r_om_relight = ri.Cvar_Get("r_om_relight", "1", CVAR_ARCHIVE);
	}

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
				float fp, fs, fsun, fspot, r2;

				/* a bake whose light model fits the map's own lightmaps badly lights each face of a
				   rock wall differently (owner on sib1, R2 0.50: "the drawing on the snow levels is
				   disgusting"); such maps keep their own lighting. The cache stays on disk */
				if (!r_om_relight_minfit)
				{
					r_om_relight_minfit = ri.Cvar_Get("r_om_relight_minfit", "0.7", CVAR_ARCHIVE);
				}

				if (GL3_Relight_Fit(mapname, &fp, &fs, &fsun, &fspot, &r2) && (r2 < r_om_relight_minfit->value))
				{
					R_Printf(PRINT_ALL, "relight: the re-bake of %s fits its lighting too loosely (R2 %.2f < "
						"r_om_relight_minfit %.2f), the map's own lighting is used\n", mapname, r2,
						r_om_relight_minfit->value);
					ri.FS_FreeFile(buf);
					return;
				}

				if (!Relight_Guard(mapname, lightdata, buf + RELIGHT_HEADER, size, (const dface46_t *)faces,
					faceslen / (int)sizeof(dface46_t)))
				{
					/* kept on disk: the same broken bake is not started again */
					ri.FS_FreeFile(buf);
					return;
				}

				memcpy(lightdata, buf + RELIGHT_HEADER, size);
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

/* the light model weights omlight fitted to the map's own lightmaps (the "fit 2" line of its log: point, surface,
   sun, sky, spot, in lightmap units), false without a bake log. A shadow takes away a lamp's light at the
   weight the map was lit with (gl3_lshadow.c, gl3_csm.c) */
qboolean
GL3_Relight_Fit(const char *mapname, float *point, float *surf, float *sun, float *spot, float *r2)
{
	char cachename[MAX_QPATH], path[MAX_OSPATH], line[512];
	qboolean found = false;
	FILE *f;

	if (!mapname || !Relight_CacheName(mapname, cachename, sizeof(cachename)))
	{
		return false;
	}

	Com_sprintf(path, sizeof(path), "%s/lightmaps/%s.log", ri.FS_Gamedir(), cachename);
	f = fopen(path, "r");

	if (!f)
	{
		return false;
	}

	while (fgets(line, sizeof(line), f))
	{
		float w[6], q;

		if (strncmp(line, "fit 2", 5))
		{
			continue;
		}

		if (sscanf(line, "fit 2 point %f surf %f sun %f sky %f spot %f bounce %f R2 %f", &w[0], &w[1], &w[2],
			&w[3], &w[4], &w[5], &q) == 7)
		{
			*point = w[0];
			*surf = w[1];
			*sun = w[2];
			*spot = w[4];
			*r2 = q;
			found = true;
		}
	}

	fclose(f);
	return found;
}