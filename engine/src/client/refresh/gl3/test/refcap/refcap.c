/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-002 (renderer module interface: GetRefAPI, import/export tables, EndFrame slot 0xbc),
 *             SPEC-412 test harness (frame capture)
 *
 * ref_omcap.dll - a capture shim between the engine and a renderer module, written from SPEC-002 only.
 * Set `vid_ref omcap` and `om_capref <x>`: GetRefAPI loads ref_<x>.dll (default `gl`, the original),
 * forwards the engine's import table to it unchanged and returns its export table unchanged except
 * for EndFrame (slot 0xbc). Before calling the real EndFrame (which swaps the buffers) the shim checks
 * for the request file "omshot.req" in the current directory; if it exists, its first line is an
 * output path, the back buffer is read and written there as an uncompressed 24-bit TGA (bottom row
 * first, the screenshot layout of SPEC-412), and the request file is deleted.
 * Tracing (reftrace.h): every other slot is wrapped too; when "omtrace.req" exists, all calls with
 * their arguments are logged to the path it names until the next EndFrame (one frame).
 *
 * View override (I4c): cvar `om_capview` = "x y z pitch yaw roll [noghoul]". While it is set, every
 * world frame (RenderFrame without SOF_RDF_NOWORLDMODEL) is drawn from that eye with all areas open; with
 * a non-zero 7th value Ghoul, weapon and viewer entities are left out (brush models stay). The engine's
 * frame record is restored after the call, so the engine never sees the change. Same input for every
 * renderer, so the original and a replacement can be compared at any spot of a map.
 *
 * Why: the renderer's own `screenshot` reads the back buffer after the buffer swap. With current
 * drivers (OpenGL presented through DXGI flip) that buffer is undefined, so both the original and any
 * replacement give black screenshots. Capturing at EndFrame gives the frame that is presented, for
 * the original and the OpenMerc renderer alike.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../../../../../common/header/sof/refapi.h"

typedef sofrefimport_t refimport_t;
typedef sofrefexport_t refexport_t;

typedef refexport_t (*GetRefAPI_t)(refimport_t imp);
typedef void *(*Cvar_Get_t)(const char *name, const char *value, int flags, void *callback);
typedef void (*Con_Printf_t)(int level, const char *fmt, ...);
typedef void (*EndFrame_t)(void);

#define SLOT(ofs) (((ofs) - 4) / 4)

typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;

#define GL_BACK 0x0405
#define GL_READ_BUFFER 0x0C02
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_PACK_ROW_LENGTH 0x0D02
#define GL_PACK_SKIP_ROWS 0x0D03
#define GL_PACK_SKIP_PIXELS 0x0D04
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#define GL_BGR 0x80E0
#define GL_UNSIGNED_BYTE 0x1401

typedef void (WINAPI *pfnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef void (WINAPI *pfnReadBuffer)(GLenum);
typedef void (WINAPI *pfnPixelStorei)(GLenum, GLint);
typedef void (WINAPI *pfnGetIntegerv)(GLenum, GLint *);
typedef void (WINAPI *pfnFinish)(void);
typedef void (WINAPI *pfnBindBuffer)(GLenum, unsigned int);
typedef void (WINAPI *pfnBindFramebuffer)(GLenum, unsigned int);
typedef PROC (WINAPI *pfnGetProcAddress)(LPCSTR);
typedef HDC (WINAPI *pfnGetCurrentDC)(void);

static HMODULE target;
static EndFrame_t realEndFrame;
static void (*realBeginFrame)(void);

#include "reftrace.h"

static void LightProbe(void);

static void
Capture(const char *path)
{
	HMODULE gl = GetModuleHandleA("opengl32.dll");
	pfnReadPixels readPixels;
	pfnReadBuffer readBuffer;
	pfnPixelStorei pixelStorei;
	pfnGetIntegerv getIntegerv;
	pfnGetProcAddress getProc;
	pfnGetCurrentDC getDC;
	pfnBindBuffer bindBuffer = NULL;
	pfnBindFramebuffer bindFramebuffer = NULL;
	GLint oldRead = GL_BACK, oldAlign = 4, oldRow = 0, oldSkipR = 0, oldSkipP = 0, oldPbo = 0, oldFbo = 0;
	unsigned char *buf, header[18];
	HWND hwnd;
	RECT rc;
	int w, h;
	FILE *f;

	if (!gl)
	{
		return;
	}

	readPixels = (pfnReadPixels)GetProcAddress(gl, "glReadPixels");
	readBuffer = (pfnReadBuffer)GetProcAddress(gl, "glReadBuffer");
	pixelStorei = (pfnPixelStorei)GetProcAddress(gl, "glPixelStorei");
	getIntegerv = (pfnGetIntegerv)GetProcAddress(gl, "glGetIntegerv");
	getProc = (pfnGetProcAddress)GetProcAddress(gl, "wglGetProcAddress");
	getDC = (pfnGetCurrentDC)GetProcAddress(gl, "wglGetCurrentDC");

	if (!readPixels || !readBuffer || !pixelStorei || !getIntegerv || !getDC)
	{
		return;
	}

	hwnd = WindowFromDC(getDC());

	if (!hwnd || !GetClientRect(hwnd, &rc))
	{
		return;
	}

	w = rc.right - rc.left;
	h = rc.bottom - rc.top;

	if ((w <= 0) || (h <= 0))
	{
		return;
	}

	if (getProc)
	{
		bindBuffer = (pfnBindBuffer)getProc("glBindBuffer");
		bindFramebuffer = (pfnBindFramebuffer)getProc("glBindFramebuffer");
	}

	buf = malloc((size_t)w * h * 3);

	if (!buf)
	{
		return;
	}

	getIntegerv(GL_READ_BUFFER, &oldRead);
	getIntegerv(GL_PACK_ALIGNMENT, &oldAlign);
	getIntegerv(GL_PACK_ROW_LENGTH, &oldRow);
	getIntegerv(GL_PACK_SKIP_ROWS, &oldSkipR);
	getIntegerv(GL_PACK_SKIP_PIXELS, &oldSkipP);

	if (bindBuffer)
	{
		getIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldPbo);
		bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}

	if (bindFramebuffer)
	{
		getIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldFbo);
		bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}

	readBuffer(GL_BACK);
	pixelStorei(GL_PACK_ALIGNMENT, 1);
	pixelStorei(GL_PACK_ROW_LENGTH, 0);
	pixelStorei(GL_PACK_SKIP_ROWS, 0);
	pixelStorei(GL_PACK_SKIP_PIXELS, 0);
	readPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, buf);

	pixelStorei(GL_PACK_ALIGNMENT, oldAlign);
	pixelStorei(GL_PACK_ROW_LENGTH, oldRow);
	pixelStorei(GL_PACK_SKIP_ROWS, oldSkipR);
	pixelStorei(GL_PACK_SKIP_PIXELS, oldSkipP);
	readBuffer(oldRead);

	if (bindFramebuffer)
	{
		bindFramebuffer(GL_READ_FRAMEBUFFER, oldFbo);
	}

	if (bindBuffer)
	{
		bindBuffer(GL_PIXEL_PACK_BUFFER, oldPbo);
	}

	memset(header, 0, sizeof(header));
	header[2] = 2;
	header[12] = w & 255;
	header[13] = (w >> 8) & 255;
	header[14] = h & 255;
	header[15] = (h >> 8) & 255;
	header[16] = 24;

	f = fopen(path, "wb");

	if (f)
	{
		fwrite(header, 1, sizeof(header), f);
		fwrite(buf, 1, (size_t)w * h * 3, f);
		fclose(f);
	}

	free(buf);
}

/* reads the first line of a request file and deletes the file; false if there is none */
static int
TakeRequest(const char *file, char *path, size_t size)
{
	FILE *f = fopen(file, "rb");
	size_t n;

	if (!f)
	{
		return 0;
	}

	n = fread(path, 1, size - 1, f);
	fclose(f);
	DeleteFileA(file);
	path[n] = 0;

	while ((n > 0) && ((path[n - 1] == 10) || (path[n - 1] == 13) || (path[n - 1] == ' ')))
	{
		path[--n] = 0;
	}

	return n > 0;
}

/* a trace request starts logging at once; it ends with the next EndFrame */
static void
CheckTraceRequest(void)
{
	char path[MAX_PATH];

	if (!tracefile && TakeRequest("omtrace.req", path, sizeof(path)))
	{
		tracefile = fopen(path, "w");
	}
}

static void
CapBeginFrame(void)
{
	CheckTraceRequest();
	T("BeginFrame\n");
	realBeginFrame();
}

static void
CapEndFrame(void)
{
	char path[MAX_PATH];

	if (TakeRequest("omshot.req", path, sizeof(path)))
	{
		/* a renderer that batches 2D drawing completes the frame when it reads pixels:
		   slot 0xd4 with a 1 x 1 thumbnail (harmless for the original) */
		unsigned char px[4];

		if (real.ReadThumbnail)
		{
			real.ReadThumbnail(px, 1, 1);
		}

		Capture(path);
	}

	if (tracefile)
	{
		T("EndFrame\n");
		fclose(tracefile);
		tracefile = NULL;
	}

	LightProbe();
	realEndFrame();
}

/* light probe (I4c): cvar om_caplight "x y z" makes the next EndFrame print slot 0xa4 for that point with every
   direction byte 0..249 and 255 ("OMLIGHT <dir> r g b") and clears the cvar's text */
static sofcvar_t *caplight;
static void (*capPrintf)(int level, const char *fmt, ...);

static void
LightProbe(void)
{
	float p[3], out[3];
	int dir;

	if (!caplight || !caplight->string || !caplight->string[0] || !real.LightPoint)
	{
		return;
	}

	if (sscanf(caplight->string, "%f %f %f", &p[0], &p[1], &p[2]) != 3)
	{
		return;
	}

	for (dir = 0; dir < 256; dir++)
	{
		float q[3] = {p[0], p[1], p[2]};

		if ((dir >= 250) && (dir < 255))
		{
			continue; /* no table entry (SPEC-AMEND-005 I4a.4) */
		}


		out[0] = out[1] = out[2] = -1;
		real.LightPoint(q, out, dir);
		capPrintf(0, "OMLIGHT %d %.4f %.4f %.4f\n", dir, out[0], out[1], out[2]);
	}

	caplight->string[0] = 0; /* once per request (the script sets the cvar again for the next point) */
}

/* view override (om_capview), see the file comment */
static sofcvar_t *capview;
static void (*viewNextRenderFrame)(sofrefdef_t *fd);

static void
ViewRenderFrame(sofrefdef_t *fd)
{
	static unsigned char allareas[64];
	static sofentity_t *kept[SOF_MAX_ENTITIES];
	float v[7] = {0, 0, 0, 0, 0, 0, 0};
	float org[3], ang[3];
	unsigned char *areabits;
	sofentity_t **ents;
	int numents, n, i;

	if (!fd || !capview || !capview->string || !capview->string[0] || (fd->rdflags & SOF_RDF_NOWORLDMODEL))
	{
		viewNextRenderFrame(fd);
		return;
	}

	n = sscanf(capview->string, "%f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);

	if (n < 6)
	{
		viewNextRenderFrame(fd);
		return;
	}

	memcpy(org, fd->vieworg, sizeof(org));
	memcpy(ang, fd->viewangles, sizeof(ang));
	areabits = fd->areabits;
	ents = fd->entities;
	numents = fd->num_entities;

	for (i = 0; i < 3; i++)
	{
		fd->vieworg[i] = v[i];
		fd->viewangles[i] = v[3 + i];
	}

	memset(allareas, 0xff, sizeof(allareas));
	fd->areabits = allareas;

	if ((n >= 7) && (v[6] != 0) && ents)
	{
		int k = 0;

		for (i = 0; (i < numents) && (k < SOF_MAX_ENTITIES); i++)
		{
			if (ents[i] && !(ents[i]->flags & (SOF_RF_GHOUL | SOF_RF_WEAPONMODEL | SOF_RF_VIEWERMODEL)))
			{
				kept[k++] = ents[i];
			}
		}

		fd->entities = kept;
		fd->num_entities = k;
	}

	viewNextRenderFrame(fd);

	memcpy(fd->vieworg, org, sizeof(org));
	memcpy(fd->viewangles, ang, sizeof(ang));
	fd->areabits = areabits;
	fd->entities = ents;
	fd->num_entities = numents;
}

__declspec(dllexport) refexport_t
GetRefAPI(refimport_t imp)
{
	refexport_t re;
	char name[MAX_PATH];
	const char *which = "gl";
	GetRefAPI_t get;
	void **cv;

	memset(&re, 0, sizeof(re));

	/* import 0x40 Cvar_Get; the cvar's string is at offset 4 (SPEC-002 cvar object) */
	cv = (void **)imp.Cvar_Get("om_capref", "gl", 0, NULL);

	if (cv && cv[1] && ((char *)cv[1])[0])
	{
		which = (const char *)cv[1];
	}

	_snprintf(name, sizeof(name), "ref_%s.dll", which);
	name[sizeof(name) - 1] = 0;

	if (!target)
	{
		target = LoadLibraryA(name);
	}

	if (!target)
	{
		imp.Con_Printf(0, "ref_omcap: could not load %s\n", name);
		return re; /* api_version 0: the engine reports an incompatible version */
	}

	get = (GetRefAPI_t)GetProcAddress(target, "GetRefAPI");

	if (!get)
	{
		return re;
	}

	re = get(imp);
	TraceWrap(&re);
	capview = (sofcvar_t *)imp.Cvar_Get("om_capview", "", 0, NULL);
	caplight = (sofcvar_t *)imp.Cvar_Get("om_caplight", "", 0, NULL);
	capPrintf = imp.Con_Printf;
	viewNextRenderFrame = re.RenderFrame;
	re.RenderFrame = ViewRenderFrame;
	realEndFrame = re.EndFrame;
	realBeginFrame = re.BeginFrame;
	re.EndFrame = CapEndFrame;
	re.BeginFrame = CapBeginFrame;
	imp.Con_Printf(0, "ref_omcap: capturing %s\n", name);

	return re;
}
