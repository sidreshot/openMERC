/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Stand-alone checker for the engine's .eft reader (fx_eft.c): parses every
 * .eft below a directory, prints counts per primitive type / record version,
 * checks the SPEC-400 data test vectors, and round-trips every file through
 * the version-3 writer (fx_save format).
 *
 * build (MSVC): cl /nologo /W3 /I.. eft_test.c ..\fx_eft.c
 * usage: eft_test <dir with effects/...>
 *
 * Implements: SPEC-142 (Validation), SPEC-400 (Test vectors 1-4)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "fx_eft.h"

static int files, failures, prim_count[5], emitter_kind[4][8][8];
static int roundtrip_fail;
static int checks_ok, checks_bad;

static unsigned char *
load(const char *path, int *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *b;
	long n;

	if (!f)
	{
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n + 1);

	if (fread(b, 1, n, f) != (size_t)n)
	{
		free(b);
		fclose(f);
		return NULL;
	}

	fclose(f);
	*len = (int)n;
	return b;
}

static int
same_prim(const fx_eft_prim_t *a, const fx_eft_prim_t *b)
{
	fx_eft_prim_t x = *a, y = *b;

	x.size = y.size = 0;

	if (x.type == FXP_EMITTER)
	{
		/* the writer upgrades record versions */
		x.u.emitter.version = y.u.emitter.version = 0;
		x.u.emitter.p.version = y.u.emitter.p.version = 0;
	}
	else if (x.type == FXP_DECAL)
	{
		x.u.decal.version = y.u.decal.version = 0;
	}
	else if (x.type == FXP_FEEDBACK)
	{
		x.u.feedback.ff_copy = y.u.feedback.ff_copy = 0;
	}

	if (memcmp(&x, &y, sizeof(x)))
	{
		const unsigned char *pa = (const unsigned char *)&x, *pb = (const unsigned char *)&y;
		size_t k;

		for (k = 0; k < sizeof(x); k++)
		{
			if (pa[k] != pb[k])
			{
				printf("  first difference at byte %u of the primitive record\n", (unsigned)k);
				break;
			}
		}

		return 0;
	}

	return 1;
}

static void
check(int cond, const char *what)
{
	if (cond)
	{
		checks_ok++;
	}
	else
	{
		checks_bad++;
		printf("CHECK FAILED: %s\n", what);
	}
}

static int
approx(float a, float b)
{
	return fabs(a - b) < 0.0005f;
}

static void
vectors(const char *rel, const fx_eft_t *e)
{
	const fx_eft_prim_t *p;

	if (!strcmp(rel, "effects/exphit.eft"))
	{
		/* SPEC-400 test vector 2 */
		check(e->version == 3.0f && e->repeat_mode == 0 && e->repeat.min == 0 &&
				e->repeat.max == 0 && e->count == 1, "exphit header");
		p = &e->prims[0];
		check(p->type == FXP_FEEDBACK && p->u.feedback.shake.max == 0 &&
				p->u.feedback.ff_id == 8, "exphit: feedback, no shake, force-feedback 8 (Jump)");
		printf("vector exphit: type %d shake (%g,%g) ff %d\n", p->type,
				p->u.feedback.shake.min, p->u.feedback.shake.max, p->u.feedback.ff_id);
	}
	else if (!strcmp(rel, "effects/weapons/world/rocketexplode.eft"))
	{
		/* SPEC-400 test vector 3 */
		p = &e->prims[0];
		check(p->type == FXP_LIGHT && p->u.light.radius.min == 500 &&
				p->u.light.rgb[0].min == 255 && p->u.light.rgb[1].min == 205 &&
				p->u.light.rgb[2].min == 205 && approx(p->u.light.life.min, 0.105f),
				"rocketexplode: light 500, (255,205,205), 0.105 s");
		printf("vector rocketexplode: p0 light r %g col (%g %g %g) life %g\n",
				p->u.light.radius.min, p->u.light.rgb[0].min, p->u.light.rgb[1].min,
				p->u.light.rgb[2].min, p->u.light.life.min);
		p = &e->prims[7];
		check(p->type == FXP_FEEDBACK && approx(p->u.feedback.shake.min, 0.4f),
				"rocketexplode: p7 feedback shake 0.4");
		printf("vector rocketexplode: p7 type %d shake (%g,%g)\n", p->type,
				p->u.feedback.shake.min, p->u.feedback.shake.max);
		p = &e->prims[8];
		check(p->type == FXP_DECAL && !strcmp(p->u.decal.name, "textures/sprites/scorch") &&
				p->u.decal.start_size == 32 && p->u.decal.orientation == 0,
				"rocketexplode: p8 decal scorch 32 forward");
		printf("vector rocketexplode: p8 type %d '%s' size %g..%g orient %d\n", p->type,
				p->u.decal.name, p->u.decal.start_size, p->u.decal.end_size,
				p->u.decal.orientation);
	}
	else if (!strcmp(rel, "effects/canthear.eft"))
	{
		/* SPEC-400 test vector 4 */
		const fx_eft_emitter_t *m = &e->prims[0].u.emitter;

		check(e->count == 1 && e->prims[0].type == FXP_EMITTER && m->style == 0 &&
				m->elements == 1 && approx(m->delay.min, 0.05f) && approx(m->duration.min, 0.005f) &&
				approx(m->duration.max, 0.005f), "canthear emitter: style 0, 1 element, delay 0.05, duration 0.005");
		check(!strcmp(m->p.graphic, "textures/sprites/snow") && m->p.size[0].min == 8 &&
				(m->p.flags & FXF_WIDTH_EQ_HEIGHT) && (m->p.flags & FXF_ADDITIVE) &&
				approx(m->p.life.min, 1.5f), "canthear particle: snow 8x8 w=h additive 1.5 s");
		check(m->p.vel[0].min == -10 && m->p.vel[0].max == 10 && m->p.vel[1].min == -10 &&
				m->p.vel[2].max == 10, "canthear velocity +-10");
		check(m->p.alpha_style == 3 && m->p.rgba[3].min == 155 && m->p.alpha_min_ratio == 0,
				"canthear alpha style 3 peak 155 ratio 0");
		check(m->p.color_style == 2 && m->p.rgba[0].min == 255 && m->p.rgba[1].min == 255 &&
				m->p.rgba[2].min == 5 && m->p.rgba[0].max == 255 && m->p.rgba[1].max == 5 &&
				m->p.rgba[2].max == 55, "canthear colour style 2 (255,255,5)->(255,5,55)");
		printf("vector canthear: style %d el %d delay %g dur %g..%g gfx '%s' size %g flags 0x%x "
				"life %g astyle %d peak %g cstyle %d col (%g %g %g)->(%g %g %g)\n",
				m->style, m->elements, m->delay.min, m->duration.min, m->duration.max,
				m->p.graphic, m->p.size[0].min, m->p.flags, m->p.life.min, m->p.alpha_style,
				m->p.rgba[3].min, m->p.color_style, m->p.rgba[0].min, m->p.rgba[1].min,
				m->p.rgba[2].min, m->p.rgba[0].max, m->p.rgba[1].max, m->p.rgba[2].max);
	}
	else if (!strcmp(rel, "effects/environ/puddle.eft"))
	{
		/* SPEC-400 decals item 5 */
		int i;

		for (i = 0; i < e->count; i++)
		{
			p = &e->prims[i];

			if (p->type == FXP_DECAL)
			{
				check(p->u.decal.start_size == 2 && p->u.decal.end_size == 25 &&
						approx(p->u.decal.accel, -3.1f) && approx(p->u.decal.time, 3.0f),
						"puddle decal 2 -> 25, accel -3.1, time 3");
				printf("vector puddle: '%s' %g -> %g accel %g time %g\n", p->u.decal.name,
						p->u.decal.start_size, p->u.decal.end_size, p->u.decal.accel,
						p->u.decal.time);
			}
		}
	}
}

static void
one(const char *path, const char *rel)
{
	unsigned char *b, out[16384];
	int len, left, i, n, ret;
	char err[128], lower[512];
	fx_eft_t e, e2;

	files++;
	b = load(path, &len);

	if (!b)
	{
		printf("FAIL %s: cannot read\n", rel);
		failures++;
		return;
	}

	ret = FX_ParseEft(b, len, &e, err, sizeof(err), &left);

	if (ret)
	{
		printf("FAIL %s: %s (code %d, %d bytes left)\n", rel, err, ret, left);
		failures++;
		free(b);
		return;
	}

	for (i = 0; i < e.count; i++)
	{
		prim_count[e.prims[i].type]++;

		if (e.prims[i].type == FXP_EMITTER)
		{
			int fv = (int)e.version, ev = e.prims[i].u.emitter.version,
				pv = e.prims[i].u.emitter.p.version;

			if ((fv >= 0) && (fv < 4) && (ev >= 0) && (ev < 8) && (pv >= 0) && (pv < 8))
			{
				emitter_kind[fv][ev][pv]++;
			}
		}
	}

	/* round trip through the v3 writer */
	n = FX_WriteEft(&e, out, sizeof(out));

	if ((n < 0) || FX_ParseEft(out, n, &e2, err, sizeof(err), &left))
	{
		printf("ROUNDTRIP FAIL %s: %s\n", rel, n < 0 ? "write" : err);
		roundtrip_fail++;
	}
	else
	{
		int same = (e2.count == e.count) && (e2.repeat_mode == e.repeat_mode) &&
			!memcmp(&e2.repeat, &e.repeat, sizeof(e.repeat));

		for (i = 0; same && (i < e.count); i++)
		{
			same = same_prim(&e.prims[i], &e2.prims[i]);
		}

		if (!same)
		{
			printf("ROUNDTRIP DIFF %s\n", rel);
			roundtrip_fail++;
		}
	}

	for (i = 0; rel[i] && (i < (int)sizeof(lower) - 1); i++)
	{
		lower[i] = (rel[i] >= 'A' && rel[i] <= 'Z') ? rel[i] + 32 : rel[i];
		lower[i] = (lower[i] == '\\') ? '/' : lower[i];
	}

	lower[i] = 0;
	vectors(lower, &e);
	free(b);
}

static int
is_eft(const char *name)
{
	size_t n = strlen(name);

	return (n > 4) && (name[n - 4] == '.') && ((name[n - 3] | 0x20) == 'e') &&
		((name[n - 2] | 0x20) == 'f') && ((name[n - 1] | 0x20) == 't');
}

static void
walk(const char *dir, const char *rel)
{
	char path[1024], sub[1024];
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE h;

	snprintf(path, sizeof(path), "%s\\*", dir);
	h = FindFirstFileA(path, &fd);

	if (h == INVALID_HANDLE_VALUE)
	{
		return;
	}

	do
	{
		if (fd.cFileName[0] == '.')
		{
			continue;
		}

		snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
		snprintf(sub, sizeof(sub), "%s%s%s", rel, rel[0] ? "/" : "", fd.cFileName);

		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			walk(path, sub);
		}
		else if (is_eft(fd.cFileName))
		{
			one(path, sub);
		}
	}
	while (FindNextFileA(h, &fd));

	FindClose(h);
#else
	DIR *d = opendir(dir);
	struct dirent *de;
	struct stat st;

	if (!d)
	{
		return;
	}

	while ((de = readdir(d)))
	{
		if (de->d_name[0] == '.')
		{
			continue;
		}

		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		snprintf(sub, sizeof(sub), "%s%s%s", rel, rel[0] ? "/" : "", de->d_name);

		if (!stat(path, &st) && S_ISDIR(st.st_mode))
		{
			walk(path, sub);
		}
		else if (is_eft(de->d_name))
		{
			one(path, sub);
		}
	}

	closedir(d);
#endif
}

#define R(r) (r).min, (r).max

/* eft_test dump <file.eft>: every decoded field (SPEC-400 names) */
static int
dump(const char *path)
{
	static fx_eft_t eft;
	char err[256];
	unsigned char *buf;
	int len, left, i, k;

	buf = load(path, &len);

	if (!buf || FX_ParseEft(buf, len, &eft, err, sizeof(err), &left))
	{
		printf("%s: %s\n", path, buf ? err : "cannot read");
		return 1;
	}

	printf("%s: version %g, repeat mode %d, repeat %g..%g, limit %g..%g, %d primitives\n", path, eft.version,
			eft.repeat_mode, R(eft.repeat), R(eft.limit), eft.count);

	for (i = 0; i < eft.count; i++)
	{
		const fx_eft_prim_t *pr = &eft.prims[i];

		printf("[%d] type %d trigger %g..%g\n", i, pr->type, R(pr->trigger));

		if (pr->type == FXP_EMITTER)
		{
			const fx_eft_emitter_t *em = &pr->u.emitter;
			const fx_eft_particle_t *p = &em->p;

			printf("  emitter v%d delay %g..%g duration %g..%g style %d elements %d flags 0x%x targets 0x%x\n",
					em->version, R(em->delay), R(em->duration), em->style, em->elements, em->flags, em->targets);
			printf("  region (%g %g %g)..(%g %g %g) radius %g escale %g..%g tscale %g..%g dist/unit %g\n",
					em->rmin[0], em->rmin[1], em->rmin[2], em->rmax[0], em->rmax[1], em->rmax[2], em->radius,
					em->escale[0], em->escale[1], em->tscale[0], em->tscale[1], em->dist_per_unit);
			printf("  particle v%d type %d graphic '%s' flags 0x%x alpha style %d color style %d\n", p->version,
					p->ptype, p->graphic, p->flags, p->alpha_style, p->color_style);
			printf("  vel f %g..%g r %g..%g u %g..%g  accel f %g..%g r %g..%g u %g..%g\n", R(p->vel[0]),
					R(p->vel[1]), R(p->vel[2]), R(p->accel[0]), R(p->accel[1]), R(p->accel[2]));
			printf("  height %g..%g -> %g..%g  width %g..%g -> %g..%g  point2 %d endscale %g..%g\n",
					R(p->size[0]), R(p->size[1]), R(p->size[2]), R(p->size[3]), p->point2, R(p->endscale));
			printf("  life %g..%g  rgba", R(p->life));

			for (k = 0; k < 4; k++)
			{
				printf(" %g..%g", R(p->rgba[k]));
			}

			printf("  alpha min ratio %g\n", p->alpha_min_ratio);
			printf("  angle %g..%g avel %g..%g trail '%s' freq %g life %g collide '%s' elasticity %g\n",
					R(p->start_angle), R(p->angle_vel), p->trail, p->trail_freq, p->trail_life, p->collide,
					p->elasticity);
			printf("  box (%g %g %g)..(%g %g %g)\n", p->bmin[0], p->bmin[1], p->bmin[2], p->bmax[0], p->bmax[1],
					p->bmax[2]);
		}
		else if (pr->type == FXP_SOUND)
		{
			printf("  sound '%s' volume %g..%g atten %g..%g\n", pr->u.sound.name, R(pr->u.sound.volume),
					R(pr->u.sound.atten));
		}
		else if (pr->type == FXP_LIGHT)
		{
			printf("  light radius %g..%g rgb %g..%g %g..%g %g..%g life %g..%g\n", R(pr->u.light.radius),
					R(pr->u.light.rgb[0]), R(pr->u.light.rgb[1]), R(pr->u.light.rgb[2]), R(pr->u.light.life));
		}
		else if (pr->type == FXP_DECAL)
		{
			printf("  decal '%s' size %g -> %g accel %g time %g orientation %d\n", pr->u.decal.name,
					pr->u.decal.start_size, pr->u.decal.end_size, pr->u.decal.accel, pr->u.decal.time,
					pr->u.decal.orientation);
		}
	}

	free(buf);
	return 0;
}

int
main(int argc, char **argv)
{
	int f, e, p;

	if ((argc >= 3) && !strcmp(argv[1], "dump"))
	{
		for (f = 2; f < argc; f++)
		{
			dump(argv[f]);
		}

		return 0;
	}

	if (argc < 2)
	{
		printf("usage: eft_test <dir containing effects/> | eft_test dump <file.eft> ...\n");
		return 2;
	}

	walk(argv[1], "");

	printf("files %d, parsed %d, failed %d, round-trip differences %d\n", files,
			files - failures, failures, roundtrip_fail);
	printf("primitives: emitter %d, sound %d, light %d, decal %d, feedback %d (total %d)\n",
			prim_count[0], prim_count[1], prim_count[2], prim_count[3], prim_count[4],
			prim_count[0] + prim_count[1] + prim_count[2] + prim_count[3] + prim_count[4]);

	for (f = 0; f < 4; f++)
	{
		for (e = 0; e < 8; e++)
		{
			for (p = 0; p < 8; p++)
			{
				if (emitter_kind[f][e][p])
				{
					printf("  emitters in file v%d, emitter v%d, particle v%d: %d\n", f, e, p,
							emitter_kind[f][e][p]);
				}
			}
		}
	}

	printf("test-vector checks: %d ok, %d failed\n", checks_ok, checks_bad);
	return (failures || checks_bad || roundtrip_fail) ? 1 : 0;
}
