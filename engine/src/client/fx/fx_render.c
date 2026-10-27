/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Client effects: INTERIM drawing through the Quake II particle list while
 * the OpenMerc renderer interface has no SPEC-002 alpha quads / quad chains
 * (package I4a). Every submitted alpha quad becomes a few coloured points
 * (nearest colour of pics/colormap.pcx) spread over its extent, so effect
 * positions, sizes, colours and timing can be checked in game. Cvar
 * om_fxinterim (default 1) switches it; it changes nothing in the scene
 * lists of CLFX_SceneAlphaQuads / CLFX_SceneQuadChains.
 *
 * Implements: SPEC-400 (Rendering hand-off; interim visualisation only)
 */

#include "fx_local.h"

cvar_t *fx_interim;

static byte fx_pal[256][3];
static qboolean fx_pal_loaded, fx_pal_tried;
static byte fx_lut[32 * 32 * 32];
static qboolean fx_lut_valid[32 * 32 * 32 / 8 + 1];

static void
FX_LoadPalette(void)
{
	byte *buf;
	int len, i;

	fx_pal_tried = true;
	len = FS_LoadFile("pics/colormap.pcx", (void **)&buf);

	if (!buf)
	{
		return;
	}

	if ((len >= 769) && (buf[len - 769] == 0x0C))
	{
		for (i = 0; i < 256; i++)
		{
			fx_pal[i][0] = buf[len - 768 + i * 3];
			fx_pal[i][1] = buf[len - 768 + i * 3 + 1];
			fx_pal[i][2] = buf[len - 768 + i * 3 + 2];
		}

		fx_pal_loaded = true;
	}

	FS_FreeFile(buf);
	memset(fx_lut_valid, 0, sizeof(fx_lut_valid));
}

static int
FX_PaletteIndex(int r, int g, int b)
{
	int key = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3), i, best = 0, bd = 1 << 30, d, dr, dg,
		db;

	if (!fx_pal_tried)
	{
		FX_LoadPalette();
	}

	if (!fx_pal_loaded)
	{
		return 15;
	}

	if (fx_lut_valid[key >> 3] & (1 << (key & 7)))
	{
		return fx_lut[key];
	}

	for (i = 0; i < 255; i++)
	{
		dr = fx_pal[i][0] - r;
		dg = fx_pal[i][1] - g;
		db = fx_pal[i][2] - b;
		d = dr * dr + dg * dg + db * db;

		if (d < bd)
		{
			bd = d;
			best = i;
		}
	}

	fx_lut[key] = (byte)best;
	fx_lut_valid[key >> 3] |= (byte)(1 << (key & 7));
	return best;
}

static void
FX_InterimQuad(const sof_alphaquad_t *q)
{
	vec3_t a, b, p, d;
	float alpha, ea, eb, len, t;
	int i, j, n, m, c;
	float r = q->color[0], g = q->color[1], bl = q->color[2];

	alpha = q->color[3] / 255.0f;

	if (q->flags & (AQ_ADDITIVE | AQ_DARKEN))
	{
		/* premultiplied in the renderer: show brightness as alpha */
		r = r * alpha;
		g = g * alpha;
		bl = bl * alpha;
		alpha = 0.8f;
	}

	if (q->flags & AQ_DARKEN)
	{
		r = 255 - r;
		g = 255 - g;
		bl = 255 - bl;
	}

	c = FX_PaletteIndex((int)r, (int)g, (int)bl);

	if (q->flags & AQ_BEAM)
	{
		VectorSubtract(q->v0c, q->origin, d);
		len = VectorLength(d);
		n = (int)(len / 4) + 1;

		if (n > 32)
		{
			n = 32;
		}

		for (i = 0; i <= n; i++)
		{
			t = (float)i / n;
			VectorMA(q->origin, t, d, p);
			V_AddParticle(p, (unsigned)c, alpha);
		}

		return;
	}

	if (q->flags & AQ_ORIENTED)
	{
		FX_Perpendicular(q->normal, b);
		CrossProduct(q->normal, b, a);
	}
	else
	{
		VectorCopy(cl.v_up, a);
		VectorCopy(cl.v_right, b);
	}

	ea = q->v0c[0];
	eb = q->v0c[1];
	n = (int)(ea / 3) + 1;
	m = (int)(eb / 3) + 1;

	if (n > 4)
	{
		n = 4;
	}

	if (m > 4)
	{
		m = 4;
	}

	for (i = 0; i < n; i++)
	{
		for (j = 0; j < m; j++)
		{
			VectorMA(q->origin, (n > 1) ? ea * (2.0f * i / (n - 1) - 1) * 0.7f : 0, a, p);
			VectorMA(p, (m > 1) ? eb * (2.0f * j / (m - 1) - 1) * 0.7f : 0, b, p);
			V_AddParticle(p, (unsigned)c, alpha);
		}
	}
}

void
FX_InterimDraw(void)
{
	sof_alphaquad_t **quads, *q;
	sof_quadchain_t **chains;
	int nq, nc, i;

	if (!fx_interim)
	{
		fx_interim = Cvar_Get("om_fxinterim", "1", 0);
	}

	if (!fx_interim->value || FX_SceneActive())
	{
		return; /* the renderer draws the alpha quads and quad chains itself */
	}

	nq = CLFX_SceneAlphaQuads(&quads);

	for (i = 0; i < nq; i++)
	{
		FX_InterimQuad(quads[i]);
	}

	nc = CLFX_SceneQuadChains(&chains);

	for (i = 0; i < nc; i++)
	{
		for (q = chains[i]->first; q; q = q->next)
		{
			FX_InterimQuad(q);
		}
	}
}
