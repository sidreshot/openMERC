/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc: the engine's fixed direction set and string-coding weights, computed at run time.
 * No table is stored: the 250 directions are the vertices of a frequency-5 geodesic subdivision of an
 * icosahedron (poles excluded), produced by spherical interpolation; the Huffman weights are OpenMerc's own
 * character classes. See docs/impl/LEGAL-tables.md (design) and docs/DEVIATIONS.md ("Fixed tables").
 *
 * Implements: SPEC-201 'dir' (250 entries, index 0..249; contents generated, see LEGAL-tables.md),
 *             SPEC-014 section 13 (six axes followed by the 250 directions), SPEC-202 s6.3 (salt bytes)
 */

#ifndef OM_GEODESIC_H
#define OM_GEODESIC_H

#include <math.h>
#include <string.h>

#define OM_NUMDIRS 250

static void
OM_Slerp(const double *a, const double *b, double t, double *out)
{
	double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	double w, s, k1, k2;
	int i;

	if (d > 1.0)
	{
		d = 1.0;
	}

	if (d < -1.0)
	{
		d = -1.0;
	}

	w = acos(d);

	if (w < 1e-12)
	{
		for (i = 0; i < 3; i++)
		{
			out[i] = a[i];
		}

		return;
	}

	s = sin(w);
	k1 = sin((1.0 - t) * w);
	k2 = sin(t * w);

	for (i = 0; i < 3; i++)
	{
		out[i] = (k1 * a[i] + k2 * b[i]) / s;
	}
}

static int
OM_DirSeen(const double (*pts)[3], int n, const double *p)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if ((fabs(pts[i][0] - p[0]) < 1e-6) && (fabs(pts[i][1] - p[1]) < 1e-6) && (fabs(pts[i][2] - p[2]) < 1e-6))
		{
			return 1;
		}
	}

	return 0;
}

/*
 * Fills out[250][3]. Faces are walked in four bands (upper cap, the two middle bands, lower cap), five faces each;
 * each face is subdivided into rows 0..5 from its first corner, and a point is emitted the first time it appears.
 */
static void
OM_BuildDirs(float (*out)[3])
{
	static const int band2[5] = {4, 0, 1, 2, 3};
	double up[5][3], dn[5][3], pts[OM_NUMDIRS + 2][3];
	double north[3] = {0, 0, 1}, south[3] = {0, 0, -1};
	double z = 1.0 / sqrt(5.0), r = 2.0 / sqrt(5.0), deg = 3.14159265358979323846 / 180.0;
	int n = 2, band, f, k, i, j, c;

	memcpy(pts[0], north, sizeof(north));
	memcpy(pts[1], south, sizeof(south));

	for (k = 0; k < 5; k++)
	{
		up[k][0] = r * cos(72.0 * k * deg);
		up[k][1] = r * sin(72.0 * k * deg);
		up[k][2] = z;
		dn[k][0] = r * cos((72.0 * k + 36.0) * deg);
		dn[k][1] = r * sin((72.0 * k + 36.0) * deg);
		dn[k][2] = -z;
	}

	for (band = 0; band < 4; band++)
	{
		for (f = 0; f < 5; f++)
		{
			const double *A, *B, *C;

			k = (band == 1) ? band2[f] : f;

			switch (band)
			{
				case 0: A = north; B = up[k]; C = up[(k + 1) % 5]; break;
				case 1: A = up[(k + 1) % 5]; B = dn[k]; C = dn[(k + 1) % 5]; break;
				case 2: A = dn[k]; B = up[(k + 1) % 5]; C = up[k]; break;
				default: A = south; B = dn[(k + 1) % 5]; C = dn[k]; break;
			}

			for (i = 0; i <= 5; i++)
			{
				double e1[3], e2[3], p[3];

				OM_Slerp(A, B, i / 5.0, e1);
				OM_Slerp(A, C, i / 5.0, e2);

				for (j = 0; j <= i; j++)
				{
					if (i)
					{
						OM_Slerp(e1, e2, (double)j / i, p);
					}
					else
					{
						memcpy(p, A, sizeof(p));
					}

					if ((n < OM_NUMDIRS + 2) && !OM_DirSeen((const double (*)[3])pts, n, p))
					{
						memcpy(pts[n++], p, sizeof(p));
					}
				}
			}
		}
	}

	for (i = 0; i < OM_NUMDIRS; i++)
	{
		for (c = 0; c < 3; c++)
		{
			out[i][c] = (float)pts[i + 2][c];
		}
	}
}

/* the 250 directions as little-endian float bytes (3000 bytes; also the SPEC-202 s6.3 salt) */
static const unsigned char *
OM_DirBytes(void)
{
	static unsigned char bytes[OM_NUMDIRS * 12];
	static int built;

	if (!built)
	{
		float dirs[OM_NUMDIRS][3];
		int i;

		OM_BuildDirs(dirs);

		for (i = 0; i < OM_NUMDIRS * 3; i++)
		{
			unsigned u;

			memcpy(&u, &dirs[i / 3][i % 3], 4);
			bytes[i * 4 + 0] = (unsigned char)(u & 0xFF);
			bytes[i * 4 + 1] = (unsigned char)((u >> 8) & 0xFF);
			bytes[i * 4 + 2] = (unsigned char)((u >> 16) & 0xFF);
			bytes[i * 4 + 3] = (unsigned char)(u >> 24);
		}

		built = 1;
	}

	return bytes;
}

/* OpenMerc's own Huffman weights for the ghoul string coder: integer class weights (names of model parts,
   bolts and paths are lowercase words joined by '_' and '/'); every code has a non-zero weight */
static float
OM_HuffWeight(int c)
{
	static const char common[] = "eaotinsrlhdc";

	if (c >= 'a' && c <= 'z')
	{
		return (strchr(common, c) ? 100.0f : 50.0f);
	}

	if (c == '_')
	{
		return 90.0f;
	}

	if (c == '/' || c == '.')
	{
		return 30.0f;
	}

	if (c >= '0' && c <= '9')
	{
		return 20.0f;
	}

	if (c >= 'A' && c <= 'Z')
	{
		return 10.0f;
	}

	if (c == ' ' || c == '-')
	{
		return 5.0f;
	}

	return 1.0f;
}

#endif