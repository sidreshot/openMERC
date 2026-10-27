/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-002 (export table), test harness: call tracing
 *
 * Logging wrappers for every SPEC-002 export slot, used by refcap.c. While a
 * trace is active (one frame, BeginFrame to EndFrame), each call is written to
 * the trace file with its arguments, then forwarded to the wrapped renderer.
 */

static FILE *tracefile;
static sofrefexport_t real;

#define T(...) do { if (tracefile) { fprintf(tracefile, __VA_ARGS__); } } while (0)

static const char *
S(const char *s)
{
	return s ? s : "(null)";
}

static void
RGBAp(const unsigned char *c)
{
	if (c)
	{
		T(" rgba(%d,%d,%d,%d)", c[0], c[1], c[2], c[3]);
	}
	else
	{
		T(" rgba(null)");
	}
}

static void
RGBAv(unsigned int c)
{
	T(" rgba(%u,%u,%u,%u)", c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24);
}

static void
Str(const char *s)
{
	int n = 0;

	T(" \"");

	if (!s)
	{
		T("(null)\"");
		return;
	}

	for (; *s && (n < 200); s++, n++)
	{
		unsigned char c = (unsigned char)*s;

		if ((c < 32) || (c == '"') || (c >= 127))
		{
			T("<%d>", c);
		}
		else
		{
			T("%c", c);
		}
	}

	T("\"");
}

static void tr_TouchAllImages(void) { T("TouchAllImages\n"); real.TouchAllImages(); }
static void tr_BeginRegistration(char *m) { T("BeginRegistration %s\n", S(m)); real.BeginRegistration(m); }
static void *tr_RegisterModel(char *n) { void *r = real.RegisterModel(n); T("RegisterModel %s -> %p\n", S(n), r); return r; }
static void *tr_RegisterSkin(char *n) { void *r = real.RegisterSkin(n); T("RegisterSkin %s -> %p\n", S(n), r); return r; }
static void tr_UnregisterSkin(char *n) { T("UnregisterSkin %s\n", S(n)); real.UnregisterSkin(n); }
static void *tr_RegisterPic(char *n) { void *r = real.RegisterPic(n); T("RegisterPic %s -> %p\n", S(n), r); return r; }
static void tr_UnregisterPic(char *n) { T("UnregisterPic %s\n", S(n)); real.UnregisterPic(n); }
static void *tr_RegisterSprite(char *n) { void *r = real.RegisterSprite(n); T("RegisterSprite %s -> %p\n", S(n), r); return r; }
static void tr_UnregisterSprite(char *n) { T("UnregisterSprite %s\n", S(n)); real.UnregisterSprite(n); }
static void tr_SetSky(char *n, float r, float *a) { T("SetSky %s %g (%g %g %g)\n", S(n), r, a ? a[0] : 0, a ? a[1] : 0, a ? a[2] : 0); real.SetSky(n, r, a); }
static void tr_SetCloud(float a, float b, float c, float d, float e, float f, float g) { T("SetCloud %g %g %g %g %g %g %g\n", a, b, c, d, e, f, g); real.SetCloud(a, b, c, d, e, f, g); }
static void tr_SetTerrain(char *n) { T("SetTerrain %s\n", S(n)); real.SetTerrain(n); }
static void tr_EndRegistration(void) { T("EndRegistration\n"); real.EndRegistration(); }

static void
tr_RenderFrame(sofrefdef_t *fd)
{
	int i;

	if (tracefile && fd)
	{
		T("RenderFrame view(%d %d %d %d) fov(%g %g) org(%g %g %g) ang(%g %g %g) blend(%g %g %g %g) time %g game %g/%g/%g rdflags %d ents %d dlights %d chains %d quads %d\n",
			fd->x, fd->y, fd->width, fd->height, fd->fov_x, fd->fov_y, fd->vieworg[0], fd->vieworg[1], fd->vieworg[2],
			fd->viewangles[0], fd->viewangles[1], fd->viewangles[2], fd->blend[0], fd->blend[1], fd->blend[2], fd->blend[3],
			fd->time, fd->oldgametime, fd->gametime_delta, fd->gametime, fd->rdflags, fd->num_entities, fd->num_dlights,
			fd->num_quadchains, fd->num_alphaquads);

		for (i = 0; fd->entities && (i < fd->num_entities); i++)
		{
			sofentity_t *e = fd->entities[i];

			if (e)
			{
				T("  ent %d model %p org(%g %g %g) ang(%g %g %g) frame %d skin %p flags %08x alpha %g\n", i, e->model,
					e->origin[0], e->origin[1], e->origin[2], e->angles[0], e->angles[1], e->angles[2], e->frame,
					e->skin, e->flags, e->alpha);

				/* I4b: the fields without a known renderer reader, raw (SPEC-002 open question) */
				{
					const unsigned int *w = (const unsigned int *)e;

					T("    raw 10-24 %08x %08x %08x %08x %08x %08x 38 %08x 44 %08x 48 %08x 4c %08x 58 %08x 5c %08x\n",
						w[4], w[5], w[6], w[7], w[8], w[9], w[14], w[17], w[18], w[19], w[22], w[23]);
				}
			}
		}

		for (i = 0; fd->alphaquads && (i < fd->num_alphaquads); i++)
		{
			sofalphaquad_t *q = fd->alphaquads[i];

			if (q)
			{
				T("  quad %d %s flags %04x org(%g %g %g) v(%g %g %g) f18 %g f1c %g rgba(%d %d %d %d) leaf %d\n", i,
					S(q->texname), q->flags, q->origin[0], q->origin[1], q->origin[2], q->v0c[0], q->v0c[1], q->v0c[2],
					q->f18, q->f1c, q->color[0], q->color[1], q->color[2], q->color[3], q->leaf);
			}
		}

		for (i = 0; fd->quadchains && (i < fd->num_quadchains); i++)
		{
			sofquadchain_t *c = fd->quadchains[i];

			if (c)
			{
				T("  chain %d count %d weight %g first %s flags %04x\n", i, c->count, c->weight,
					c->first ? S(c->first->texname) : "-", c->first ? c->first->flags : 0);

				/* OpenMerc (I5): every quad of the chain (sizes, colours, flags of emitter particles) */
				{
					const sofalphaquad_t *q = c->first;
					int k;

					T("    box (%g %g %g)-(%g %g %g)\n", c->mins[0], c->mins[1], c->mins[2], c->maxs[0], c->maxs[1],
						c->maxs[2]);

					for (k = 0; q && (k < c->count) && (k < 1024); k++, q = q->next)
					{
						T("    q %d %s flags %04x org(%g %g %g) v(%g %g %g) f18 %g f1c %g rgba(%d %d %d %d)\n", k,
							S(q->texname), q->flags, q->origin[0], q->origin[1], q->origin[2], q->v0c[0], q->v0c[1],
							q->v0c[2], q->f18, q->f1c, q->color[0], q->color[1], q->color[2], q->color[3]);
					}
				}
			}
		}
	}

	real.RenderFrame(fd);

	/* I4b: the dynamic lights after the call, including those the renderer appended (SPEC-411 §3) */
	if (tracefile && fd && fd->dlights)
	{
		/* the light colour the renderer wrote for Ghoul entities (SPEC-002 entity 0x58) */
		for (i = 0; fd->entities && (i < fd->num_entities); i++)
		{
			sofentity_t *e = fd->entities[i];

			if (e && e->lightout)
			{
				T("  ent %d lightout(%g %g %g)\n", i, e->lightout[0], e->lightout[1], e->lightout[2]);
			}
		}

		/* the original works on a copy of the record: lights it appended lie past num_dlights */
		for (i = 0; (i < fd->num_dlights + 4) && (i < 32); i++)
		{
			const sofdlight_t *d = &fd->dlights[i];

			T("  dlight %d%s org(%g %g %g) rgb(%g %g %g) intensity %g\n", i, (i >= fd->num_dlights) ? " (past count)" : "",
				d->origin[0], d->origin[1], d->origin[2], d->color[0], d->color[1], d->color[2], d->intensity);
		}
	}
}

static void tr_DrawGetPicSize(int *w, int *h, char *n) { real.DrawGetPicSize(w, h, n); T("DrawGetPicSize %s -> %d %d\n", S(n), w ? *w : 0, h ? *h : 0); }
static void tr_DrawPic(int x, int y, char *n, unsigned int c) { T("DrawPic %d %d %s", x, y, S(n)); RGBAv(c); T("\n"); real.DrawPic(x, y, n, c); }
static void tr_DrawPicScaled(int x, int y, float sx, float sy, const unsigned char *c, char *n) { T("DrawPicScaled %d %d %g %g %s", x, y, sx, sy, S(n)); RGBAp(c); T("\n"); real.DrawPicScaled(x, y, sx, sy, c, n); }
static void tr_DrawPicPart(int x, int y, int l, int t, int r, int b, const unsigned char *c, char *n) { T("DrawPicPart %d %d %d %d %d %d %s", x, y, l, t, r, b, S(n)); RGBAp(c); T("\n"); real.DrawPicPart(x, y, l, t, r, b, c, n); }
static void tr_DrawStretchPic(int x, int y, int w, int h, const unsigned char *c, char *n, int rot) { T("DrawStretchPic %d %d %d %d %s rot %d", x, y, w, h, S(n), rot); RGBAp(c); T("\n"); real.DrawStretchPic(x, y, w, h, c, n, rot); }
static void tr_DrawChar(int x, int y, int ch, const unsigned char *c) { T("DrawChar %d %d %d", x, y, ch); RGBAp(c); T("\n"); real.DrawChar(x, y, ch, c); }
static void tr_DrawString(int x, int y, char *s, unsigned int c, int m) { T("DrawString %d %d", x, y); Str(s); RGBAv(c); T(" max %d\n", m); real.DrawString(x, y, s, c, m); }
static void tr_DrawConsoleString(int x, int y, char *s, int m) { T("DrawConsoleString %d %d", x, y); Str(s); T(" max %d\n", m); real.DrawConsoleString(x, y, s, m); }
static void tr_DrawConsoleStringColor(int x, int y, char *s, int m, const unsigned char *c) { T("DrawConsoleStringColor %d %d", x, y); Str(s); T(" max %d", m); RGBAp(c); T("\n"); real.DrawConsoleStringColor(x, y, s, m, c); }
static void tr_DrawCharFloat(float x, float y, float sc, const unsigned char *c, int ch) { T("DrawCharFloat %g %g %g %d", x, y, sc, ch); RGBAp(c); T("\n"); real.DrawCharFloat(x, y, sc, c, ch); }
static void tr_DrawTileClear(int x, int y, int w, int h, char *n) { T("DrawTileClear %d %d %d %d %s\n", x, y, w, h, S(n)); real.DrawTileClear(x, y, w, h, n); }
static void tr_DrawFill(int x, int y, int w, int h, const unsigned char *c) { T("DrawFill %d %d %d %d", x, y, w, h); RGBAp(c); T("\n"); real.DrawFill(x, y, w, h, c); }
static void tr_DrawBevelBox(int x, int y, int w, int h, const unsigned char *c) { T("DrawBevelBox %d %d %d %d", x, y, w, h); RGBAp(c); T("\n"); real.DrawBevelBox(x, y, w, h, c); }
static void tr_DrawLine(float a, float b, float c, float d, float w, const unsigned char *col) { T("DrawLine %g %g %g %g %g", a, b, c, d, w); RGBAp(col); T("\n"); real.DrawLine(a, b, c, d, w, col); }
static void tr_DrawFadeScreen(const unsigned char *c) { T("DrawFadeScreen"); RGBAp(c); T("\n"); real.DrawFadeScreen(c); }
static void tr_DrawPolygon(int a, int b, int n, sofvec3_t *v, const unsigned char *c) { T("DrawPolygon %d", n); RGBAp(c); T("\n"); real.DrawPolygon(a, b, n, v, c); }
static void tr_SetClipRect(int x, int y, int w, int h) { T("SetClipRect %d %d %d %d\n", x, y, w, h); real.SetClipRect(x, y, w, h); }
static void tr_ResetClipRect(void) { T("ResetClipRect\n"); real.ResetClipRect(); }
static void tr_ClipRightTo(int x) { T("ClipRightTo %d\n", x); real.ClipRightTo(x); }
static void tr_ClipLeftFrom(int x) { T("ClipLeftFrom %d\n", x); real.ClipLeftFrom(x); }
static void tr_ReapplyClipRect(void) { T("ReapplyClipRect\n"); real.ReapplyClipRect(); }
static void tr_SetRawImage(char *n, int w, int h, unsigned char *p) { T("SetRawImage %s %d %d\n", S(n), w, h); real.SetRawImage(n, w, h, p); }
static void tr_LightPoint(float *p, float *o, int d) { real.LightPoint(p, o, d); T("LightPoint (%g %g %g) dir %d -> (%g %g %g)\n", p ? p[0] : 0, p ? p[1] : 0, p ? p[2] : 0, d, o ? o[0] : 0, o ? o[1] : 0, o ? o[2] : 0); }
static void tr_DamageTexture(int t, int u) { T("DamageTexture %d %d\n", (short)(t & 0xFFFF), u); real.DamageTexture(t, u); }
static void tr_DrawWorldLabel(float *o, char *t, char *p, unsigned int c) { T("DrawWorldLabel (%g %g %g)", o ? o[0] : 0, o ? o[1] : 0, o ? o[2] : 0); Str(t); T(" pic %s", S(p)); RGBAv(c); T("\n"); real.DrawWorldLabel(o, t, p, c); }
static void tr_Set2D(int r) { T("Set2D %d\n", r & 255); real.Set2D(r); }
static void tr_Set3DInMenu(int f) { T("Set3DInMenu %d\n", f & 255); real.Set3DInMenu(f); }
static void tr_SetColorCorrection(float a, float b, float c, float d) { T("SetColorCorrection %g %g %g %g\n", a, b, c, d); real.SetColorCorrection(a, b, c, d); }
static void tr_AppActivate(int a) { T("AppActivate %d\n", a); real.AppActivate(a); }
static void tr_DrawFontString(int x, int y, char *t, unsigned int c, const om_fontref_t *f, int k) { T("DrawFontString %d %d", x, y); Str(t); RGBAv(c); T(" font %s keep %d\n", f ? S(f->chars) : "(null)", k & 255); real.DrawFontString(x, y, t, c, f, k); }
static int tr_FontStringWidth(char *t, const om_fontref_t *f) { int r = real.FontStringWidth(t, f); T("FontStringWidth"); Str(t); T(" font %s -> %d\n", f ? S(f->chars) : "(null)", r); return r; }
static int tr_FontLineHeight(const om_fontref_t *f) { int r = real.FontLineHeight(f); T("FontLineHeight %s -> %d\n", f ? S(f->chars) : "(null)", r); return r; }
static void tr_ReadThumbnail(unsigned char *o, int w, int h) { T("ReadThumbnail %d %d\n", w, h); real.ReadThumbnail(o, w, h); }

/* wraps every slot except Init, Shutdown, BeginFrame, EndFrame (handled by refcap.c) and the A3D slot */
static void
TraceWrap(sofrefexport_t *re)
{
	real = *re;
#define W(f) if (re->f) re->f = tr_##f
	W(TouchAllImages); W(BeginRegistration); W(RegisterModel); W(RegisterSkin); W(UnregisterSkin);
	W(RegisterPic); W(UnregisterPic); W(RegisterSprite); W(UnregisterSprite); W(SetSky); W(SetCloud);
	W(SetTerrain); W(EndRegistration); W(RenderFrame); W(DrawGetPicSize); W(DrawPic); W(DrawPicScaled);
	W(DrawPicPart); W(DrawStretchPic); W(DrawChar); W(DrawString); W(DrawConsoleString);
	W(DrawConsoleStringColor); W(DrawCharFloat); W(DrawTileClear); W(DrawFill); W(DrawBevelBox); W(DrawLine);
	W(DrawFadeScreen); W(DrawPolygon); W(SetClipRect); W(ResetClipRect); W(ClipRightTo); W(ClipLeftFrom);
	W(ReapplyClipRect); W(SetRawImage); W(LightPoint); W(DamageTexture); W(DrawWorldLabel); W(Set2D);
	W(Set3DInMenu); W(SetColorCorrection); W(AppActivate); W(DrawFontString); W(FontStringWidth);
	W(FontLineHeight); W(ReadThumbnail);
#undef W
}
