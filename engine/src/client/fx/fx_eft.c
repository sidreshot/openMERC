/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Reader and writer for .eft effect definitions. The file is read field by
 * field in the order of the SPEC-400 tables; older record versions skip the
 * fields they do not have and get the documented defaults.
 *
 * Implements: SPEC-142, SPEC-400 (".eft layout and field meanings")
 */

#include <string.h>
#include <stdio.h>

#include "fx_eft.h"

typedef struct
{
	const unsigned char *p;
	int len;
	int pos;
	int bad;
} fx_rd_t;

static unsigned
rd_u32(fx_rd_t *r)
{
	unsigned v;

	if (r->pos + 4 > r->len)
	{
		r->bad = 1;
		r->pos = r->len;
		return 0;
	}

	v = (unsigned)r->p[r->pos] | ((unsigned)r->p[r->pos + 1] << 8) |
		((unsigned)r->p[r->pos + 2] << 16) | ((unsigned)r->p[r->pos + 3] << 24);
	r->pos += 4;
	return v;
}

static int
rd_int(fx_rd_t *r)
{
	return (int)rd_u32(r);
}

static float
rd_float(fx_rd_t *r)
{
	unsigned u = rd_u32(r);
	float f;

	memcpy(&f, &u, sizeof(f));
	return f;
}

static void
rd_range(fx_rd_t *r, fx_range_t *out)
{
	out->min = rd_float(r);
	out->max = rd_float(r);
}

static void
rd_vec(fx_rd_t *r, float *v)
{
	v[0] = rd_float(r);
	v[1] = rd_float(r);
	v[2] = rd_float(r);
}

static void
rd_skip(fx_rd_t *r, int n)
{
	if (r->pos + n > r->len)
	{
		r->bad = 1;
		r->pos = r->len;
		return;
	}

	r->pos += n;
}

/* 64-byte name field: NUL-terminated, bytes after the NUL are garbage */
static void
rd_name(fx_rd_t *r, char *out, int strip_m32)
{
	int i, n;

	if (r->pos + FX_NAME_LEN > r->len)
	{
		r->bad = 1;
		r->pos = r->len;
		out[0] = 0;
		return;
	}

	for (i = 0; i < FX_NAME_LEN - 1; i++)
	{
		out[i] = (char)r->p[r->pos + i];

		if (!out[i])
		{
			break;
		}
	}

	out[i] = 0;
	r->pos += FX_NAME_LEN;

	if (strip_m32)
	{
		n = (int)strlen(out);

		if ((n >= 4) && (out[n - 4] == '.') &&
			((out[n - 3] | 0x20) == 'm') && (out[n - 2] == '3') && (out[n - 1] == '2'))
		{
			memset(out + n - 4, 0, 4);
		}
	}
}

static float
fx_minf(float a, float b)
{
	return a < b ? a : b;
}

static float
fx_clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static int
fx_fail(char *err, int errsize, const char *msg, int code)
{
	if (err && (errsize > 0))
	{
		snprintf(err, errsize, "%s", msg);
	}

	return code;
}

static int
FX_ParseParticle(fx_rd_t *r, fx_eft_particle_t *p, char *err, int errsize)
{
	int i;
	float ver = rd_float(r);

	if (ver > 7.0f)
	{
		return fx_fail(err, errsize, "particle record version above 7", -10);
	}

	p->version = (int)ver;

	for (i = 0; i < 3; i++)
	{
		rd_range(r, &p->vel[i]);
	}

	for (i = 0; i < 3; i++)
	{
		rd_range(r, &p->accel[i]);
	}

	p->ptype = rd_int(r);

	for (i = 0; i < 4; i++)
	{
		rd_range(r, &p->size[i]);
	}

	p->point2 = rd_int(r);
	rd_range(r, &p->endscale);

	if (p->version < 5)
	{
		rd_skip(r, 244); /* no meaning */
	}

	rd_name(r, p->graphic, 1);
	rd_range(r, &p->life);

	for (i = 0; i < 4; i++)
	{
		rd_range(r, &p->rgba[i]);
	}

	p->alpha_min_ratio = rd_float(r);
	p->trail_life = rd_float(r);
	rd_range(r, &p->start_angle);
	rd_range(r, &p->angle_vel);
	p->flags = rd_int(r);

	if (p->version >= 2)
	{
		rd_vec(r, p->bmin);
		rd_vec(r, p->bmax);
		p->elasticity = rd_float(r);
		rd_name(r, p->collide, 0);
	}
	else
	{
		p->bmin[0] = p->bmin[1] = p->bmin[2] = 0;
		p->bmax[0] = p->bmax[1] = p->bmax[2] = 0;
		p->elasticity = 0.7f;
		p->collide[0] = 0;
	}

	if (p->version >= 3)
	{
		p->trail_freq = rd_float(r);
		rd_name(r, p->trail, 0);
	}
	else
	{
		p->trail_freq = 60;
		p->trail[0] = 0;
	}

	if (p->version >= 4)
	{
		for (i = 0; i < 3; i++)
		{
			rd_range(r, &p->gang[i]);
		}

		for (i = 0; i < 3; i++)
		{
			rd_range(r, &p->gavel[i]);
		}
	}
	else
	{
		memset(p->gang, 0, sizeof(p->gang));
		memset(p->gavel, 0, sizeof(p->gavel));
		p->flags &= 0x3FFF;
	}

	if (p->version < 6)
	{
		p->flags &= ~(FXF_REMOVE_ON_HIT | FXF_NO_SCALING | FXF_ENTITIES);
	}

	if (p->version >= 7)
	{
		p->alpha_style = rd_int(r);
		p->color_style = rd_int(r);
	}
	else
	{
		p->alpha_style = 0;
		p->color_style = (p->flags & FXF_OLDCOLOR) ? 0 : 1;
		p->flags &= ~(FXF_DECELERATE | FXF_NO_ZBUFFER);
	}

	return 0;
}

static int
FX_ParseEmitter(fx_rd_t *r, fx_eft_emitter_t *e, char *err, int errsize)
{
	int ret;
	float ver = rd_float(r);
	fx_range_t limit;

	if (ver > 3.0f)
	{
		return fx_fail(err, errsize, "emitter record version above 3", -11);
	}

	e->version = (int)ver;
	rd_range(r, &e->delay);
	rd_range(r, &e->duration);
	e->duration.min = fx_clampf(e->duration.min, 0.005f, 99999.0f);
	e->duration.max = fx_clampf(e->duration.max, 0.005f, 99999.0f);
	e->style = rd_int(r);
	e->elements = rd_int(r);
	e->flags = rd_int(r);
	e->targets = rd_int(r);
	rd_vec(r, e->rmin);
	rd_vec(r, e->rmax);
	rd_skip(r, 12); /* 0x40: no runtime meaning */
	e->radius = rd_float(r);
	e->escale[0] = rd_float(r);
	e->escale[1] = rd_float(r);
	e->tscale[0] = rd_float(r);
	e->tscale[1] = rd_float(r);

	ret = FX_ParseParticle(r, &e->p, err, errsize);

	if (ret)
	{
		return ret;
	}

	if (e->version >= 2)
	{
		rd_range(r, &limit);
		e->delay.min = fx_minf(e->delay.min, limit.min);
		e->delay.max = fx_minf(e->delay.max, limit.max);
		rd_skip(r, 4); /* 0x228: no runtime meaning (0 after loading) */
	}

	if (e->version >= 3)
	{
		e->dist_per_unit = rd_float(r);
		rd_skip(r, 4); /* 0x230: no runtime meaning */
	}
	else
	{
		e->dist_per_unit = 0;

		if (e->style != 1)
		{
			e->rmin[0] = e->rmin[1] = e->rmin[2] = 0;
			e->rmax[0] = e->rmax[1] = e->rmax[2] = 0;
		}
	}

	return 0;
}

int
FX_ParseEft(const unsigned char *buf, int len, fx_eft_t *out, char *err, int errsize,
		int *bytes_left)
{
	fx_rd_t r;
	float count;
	int i, start, ret;
	fx_eft_prim_t *pr;

	memset(out, 0, sizeof(*out));
	r.p = buf;
	r.len = len;
	r.pos = 0;
	r.bad = 0;

	if (bytes_left)
	{
		*bytes_left = 0;
	}

	if (err && (errsize > 0))
	{
		err[0] = 0;
	}

	out->version = rd_float(&r);

	if (r.bad)
	{
		return fx_fail(err, errsize, "file too short", -1);
	}

	if (out->version > 3.0f)
	{
		return fx_fail(err, errsize, "file version above 3", -2);
	}

	out->repeat_mode = rd_int(&r);
	rd_range(&r, &out->repeat);
	count = rd_float(&r);

	if (r.bad)
	{
		return fx_fail(err, errsize, "file too short", -1);
	}

	if ((count < 0) || (count > FX_MAX_PRIMS))
	{
		return fx_fail(err, errsize, "more than 10 primitives", -3);
	}

	out->count = (int)count;

	for (i = 0; i < out->count; i++)
	{
		pr = &out->prims[i];
		start = r.pos;
		pr->type = (int)rd_float(&r);

		switch (pr->type)
		{
			case FXP_EMITTER:
				ret = FX_ParseEmitter(&r, &pr->u.emitter, err, errsize);

				if (ret)
				{
					return ret;
				}

				break;

			case FXP_SOUND:
				rd_name(&r, pr->u.sound.name, 0);
				rd_range(&r, &pr->u.sound.volume);
				rd_range(&r, &pr->u.sound.atten);
				break;

			case FXP_LIGHT:
				rd_range(&r, &pr->u.light.radius);
				rd_range(&r, &pr->u.light.rgb[0]);
				rd_range(&r, &pr->u.light.rgb[1]);
				rd_range(&r, &pr->u.light.rgb[2]);
				rd_range(&r, &pr->u.light.life);
				break;

			case FXP_DECAL:
				pr->u.decal.version = rd_int(&r);

				if (pr->u.decal.version > 1)
				{
					return fx_fail(err, errsize, "decal record version above 1", -12);
				}

				rd_name(&r, pr->u.decal.name, 1);
				pr->u.decal.start_size = rd_float(&r);
				pr->u.decal.end_size = rd_float(&r);
				pr->u.decal.accel = rd_float(&r);
				pr->u.decal.time = rd_float(&r);
				pr->u.decal.orientation = rd_int(&r);
				break;

			case FXP_FEEDBACK:
				rd_range(&r, &pr->u.feedback.blind);
				rd_range(&r, &pr->u.feedback.deaf);
				rd_range(&r, &pr->u.feedback.shake);
				pr->u.feedback.ff_id = rd_int(&r);
				pr->u.feedback.ff_copy = rd_int(&r);
				break;

			default:
				return fx_fail(err, errsize, "unknown primitive type", -4);
		}

		if (out->version >= 3.0f)
		{
			rd_range(&r, &pr->trigger);
		}
		else
		{
			pr->trigger.min = pr->trigger.max = 0;
		}

		pr->size = r.pos - start;

		if (r.bad)
		{
			return fx_fail(err, errsize, "file ends inside a primitive", -5);
		}
	}

	if (out->version >= 2.0f)
	{
		rd_range(&r, &out->limit);

		if (r.bad)
		{
			return fx_fail(err, errsize, "file ends inside the trailer", -6);
		}
	}
	else
	{
		out->limit = out->repeat;
	}

	out->repeat.min = fx_minf(out->repeat.min, out->limit.min);
	out->repeat.max = fx_minf(out->repeat.max, out->limit.max);

	if (bytes_left)
	{
		*bytes_left = len - r.pos;
	}

	if (r.pos != len)
	{
		return fx_fail(err, errsize, "bytes left after the trailer", -7);
	}

	return 0;
}

/* ---- writer (fx_save) ---------------------------------------------------- */

typedef struct
{
	unsigned char *p;
	int size;
	int pos;
	int bad;
} fx_wr_t;

static void
wr_u32(fx_wr_t *w, unsigned v)
{
	if (w->pos + 4 > w->size)
	{
		w->bad = 1;
		return;
	}

	w->p[w->pos++] = (unsigned char)(v & 0xFF);
	w->p[w->pos++] = (unsigned char)((v >> 8) & 0xFF);
	w->p[w->pos++] = (unsigned char)((v >> 16) & 0xFF);
	w->p[w->pos++] = (unsigned char)((v >> 24) & 0xFF);
}

static void
wr_float(fx_wr_t *w, float f)
{
	unsigned u;

	memcpy(&u, &f, sizeof(u));
	wr_u32(w, u);
}

static void
wr_range(fx_wr_t *w, const fx_range_t *r)
{
	wr_float(w, r->min);
	wr_float(w, r->max);
}

static void
wr_vec(fx_wr_t *w, const float *v)
{
	wr_float(w, v[0]);
	wr_float(w, v[1]);
	wr_float(w, v[2]);
}

static void
wr_name(fx_wr_t *w, const char *s)
{
	int i, done = 0;

	if (w->pos + FX_NAME_LEN > w->size)
	{
		w->bad = 1;
		return;
	}

	/* the full 64-byte buffer; after the NUL our buffers hold zeros */
	for (i = 0; i < FX_NAME_LEN; i++)
	{
		if (!done && (i < FX_NAME_LEN - 1) && s[i])
		{
			w->p[w->pos + i] = (unsigned char)s[i];
		}
		else
		{
			w->p[w->pos + i] = 0;
			done = 1;
		}
	}

	w->pos += FX_NAME_LEN;
}

int
FX_WriteEft(const fx_eft_t *in, unsigned char *buf, int size)
{
	fx_wr_t w;
	int i, j;
	const fx_eft_prim_t *pr;
	const fx_eft_emitter_t *e;
	const fx_eft_particle_t *p;
	static const float editor_unused[3] = {128, 0, 20};

	w.p = buf;
	w.size = size;
	w.pos = 0;
	w.bad = 0;

	wr_float(&w, 3.0f);
	wr_u32(&w, (unsigned)in->repeat_mode);
	wr_range(&w, &in->repeat);
	wr_float(&w, (float)in->count);

	for (i = 0; i < in->count; i++)
	{
		pr = &in->prims[i];
		wr_float(&w, (float)pr->type);

		switch (pr->type)
		{
			case FXP_EMITTER:
				e = &pr->u.emitter;
				p = &e->p;
				wr_float(&w, 3.0f);
				wr_range(&w, &e->delay);
				wr_range(&w, &e->duration);
				wr_u32(&w, (unsigned)e->style);
				wr_u32(&w, (unsigned)e->elements);
				wr_u32(&w, (unsigned)e->flags);
				wr_u32(&w, (unsigned)e->targets);
				wr_vec(&w, e->rmin);
				wr_vec(&w, e->rmax);
				wr_vec(&w, editor_unused);
				wr_float(&w, e->radius);
				wr_float(&w, e->escale[0]);
				wr_float(&w, e->escale[1]);
				wr_float(&w, e->tscale[0]);
				wr_float(&w, e->tscale[1]);
				wr_float(&w, 7.0f);

				for (j = 0; j < 3; j++)
				{
					wr_range(&w, &p->vel[j]);
				}

				for (j = 0; j < 3; j++)
				{
					wr_range(&w, &p->accel[j]);
				}

				wr_u32(&w, (unsigned)p->ptype);

				for (j = 0; j < 4; j++)
				{
					wr_range(&w, &p->size[j]);
				}

				wr_u32(&w, (unsigned)p->point2);
				wr_range(&w, &p->endscale);
				wr_name(&w, p->graphic);
				wr_range(&w, &p->life);

				for (j = 0; j < 4; j++)
				{
					wr_range(&w, &p->rgba[j]);
				}

				wr_float(&w, p->alpha_min_ratio);
				wr_float(&w, p->trail_life);
				wr_range(&w, &p->start_angle);
				wr_range(&w, &p->angle_vel);
				wr_u32(&w, (unsigned)p->flags);
				wr_vec(&w, p->bmin);
				wr_vec(&w, p->bmax);
				wr_float(&w, p->elasticity);
				wr_name(&w, p->collide);
				wr_float(&w, p->trail_freq);
				wr_name(&w, p->trail);

				for (j = 0; j < 3; j++)
				{
					wr_range(&w, &p->gang[j]);
				}

				for (j = 0; j < 3; j++)
				{
					wr_range(&w, &p->gavel[j]);
				}

				wr_u32(&w, (unsigned)p->alpha_style);
				wr_u32(&w, (unsigned)p->color_style);
				wr_range(&w, &e->delay); /* delay limit: the in-memory delay */
				wr_u32(&w, 0);
				wr_float(&w, e->dist_per_unit);
				wr_float(&w, 0.5f);
				break;

			case FXP_SOUND:
				wr_name(&w, pr->u.sound.name);
				wr_range(&w, &pr->u.sound.volume);
				wr_range(&w, &pr->u.sound.atten);
				break;

			case FXP_LIGHT:
				wr_range(&w, &pr->u.light.radius);
				wr_range(&w, &pr->u.light.rgb[0]);
				wr_range(&w, &pr->u.light.rgb[1]);
				wr_range(&w, &pr->u.light.rgb[2]);
				wr_range(&w, &pr->u.light.life);
				break;

			case FXP_DECAL:
				wr_u32(&w, 1);
				wr_name(&w, pr->u.decal.name);
				wr_float(&w, pr->u.decal.start_size);
				wr_float(&w, pr->u.decal.end_size);
				wr_float(&w, pr->u.decal.accel);
				wr_float(&w, pr->u.decal.time);
				wr_u32(&w, (unsigned)pr->u.decal.orientation);
				break;

			case FXP_FEEDBACK:
				wr_range(&w, &pr->u.feedback.blind);
				wr_range(&w, &pr->u.feedback.deaf);
				wr_range(&w, &pr->u.feedback.shake);
				wr_u32(&w, (unsigned)pr->u.feedback.ff_id);
				wr_u32(&w, (unsigned)pr->u.feedback.ff_id);
				break;

			default:
				return -1;
		}

		wr_range(&w, &pr->trigger);
	}

	/* trailer = the in-memory repeat-delay limit */
	wr_range(&w, &in->limit);

	return w.bad ? -1 : w.pos;
}
