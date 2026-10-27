/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: the "avi" console command of Soldier of Fortune,
 * recording the window into an AVI file at 10 frames per second through the
 * Windows video-for-Windows API (the standard video-compression dialog picks
 * the codec). The library (avifil32.dll) is loaded when a recording starts.
 * Other platforms print the messages of the command but cannot record.
 *
 * Test extension (not in the original): cvar r_om_avinodialog 1 skips the
 * dialog and records full frames uncompressed, so the recorder can be tested
 * without input.
 *
 * Implements: SPEC-412 §13 (AVI recording), SPEC-413 §1 step 10 (movie
 *             recording start-up)
 */

#ifdef _WIN32
#include <windows.h>
#include <vfw.h>
#endif

#include "header/local.h"

static qboolean recording;
static qboolean paused;
static int framecount;
static int capturemark;     /* system milliseconds */
static int avi_w, avi_h;

#ifdef _WIN32

typedef void (WINAPI *AVIFileInit_t)(void);
typedef void (WINAPI *AVIFileExit_t)(void);
typedef HRESULT (WINAPI *AVIFileOpenA_t)(PAVIFILE *, LPCSTR, UINT, LPCLSID);
typedef HRESULT (WINAPI *AVIFileCreateStreamA_t)(PAVIFILE, PAVISTREAM *, AVISTREAMINFOA *);
typedef BOOL (WINAPI *AVISaveOptions_t)(HWND, UINT, int, PAVISTREAM *, LPAVICOMPRESSOPTIONS *);
typedef LONG (WINAPI *AVISaveOptionsFree_t)(int, LPAVICOMPRESSOPTIONS *);
typedef HRESULT (WINAPI *AVIMakeCompressedStream_t)(PAVISTREAM *, PAVISTREAM, AVICOMPRESSOPTIONS *, CLSID *);
typedef HRESULT (WINAPI *AVIStreamSetFormat_t)(PAVISTREAM, LONG, LPVOID, LONG);
typedef HRESULT (WINAPI *AVIStreamWrite_t)(PAVISTREAM, LONG, LONG, LPVOID, LONG, DWORD, LONG *, LONG *);
typedef ULONG (WINAPI *AVIStreamRelease_t)(PAVISTREAM);
typedef ULONG (WINAPI *AVIFileRelease_t)(PAVIFILE);

static HMODULE avilib;
static AVIFileInit_t pAVIFileInit;
static AVIFileExit_t pAVIFileExit;
static AVIFileOpenA_t pAVIFileOpen;
static AVIFileCreateStreamA_t pAVIFileCreateStream;
static AVISaveOptions_t pAVISaveOptions;
static AVISaveOptionsFree_t pAVISaveOptionsFree;
static AVIMakeCompressedStream_t pAVIMakeCompressedStream;
static AVIStreamSetFormat_t pAVIStreamSetFormat;
static AVIStreamWrite_t pAVIStreamWrite;
static AVIStreamRelease_t pAVIStreamRelease;
static AVIFileRelease_t pAVIFileRelease;

/* the system millisecond clock (real time) */
static int
AviMilliseconds(void)
{
	static LARGE_INTEGER freq;
	LARGE_INTEGER now;

	if (!freq.QuadPart)
	{
		QueryPerformanceFrequency(&freq);
	}

	QueryPerformanceCounter(&now);
	return (int)(now.QuadPart * 1000 / freq.QuadPart);
}

static PAVIFILE avifile;
static PAVISTREAM avistream, avicompressed;
static byte *avibuffer;
static int avirowbytes;

static qboolean
LoadAviLib(void)
{
	if (avilib)
	{
		return true;
	}

	avilib = LoadLibraryA("avifil32.dll");

	if (!avilib)
	{
		return false;
	}

	pAVIFileInit = (AVIFileInit_t)(void *)GetProcAddress(avilib, "AVIFileInit");
	pAVIFileExit = (AVIFileExit_t)(void *)GetProcAddress(avilib, "AVIFileExit");
	pAVIFileOpen = (AVIFileOpenA_t)(void *)GetProcAddress(avilib, "AVIFileOpenA");
	pAVIFileCreateStream = (AVIFileCreateStreamA_t)(void *)GetProcAddress(avilib, "AVIFileCreateStreamA");
	pAVISaveOptions = (AVISaveOptions_t)(void *)GetProcAddress(avilib, "AVISaveOptions");
	pAVISaveOptionsFree = (AVISaveOptionsFree_t)(void *)GetProcAddress(avilib, "AVISaveOptionsFree");
	pAVIMakeCompressedStream = (AVIMakeCompressedStream_t)(void *)GetProcAddress(avilib, "AVIMakeCompressedStream");
	pAVIStreamSetFormat = (AVIStreamSetFormat_t)(void *)GetProcAddress(avilib, "AVIStreamSetFormat");
	pAVIStreamWrite = (AVIStreamWrite_t)(void *)GetProcAddress(avilib, "AVIStreamWrite");
	pAVIStreamRelease = (AVIStreamRelease_t)(void *)GetProcAddress(avilib, "AVIStreamRelease");
	pAVIFileRelease = (AVIFileRelease_t)(void *)GetProcAddress(avilib, "AVIFileRelease");

	if (!pAVIFileInit || !pAVIFileExit || !pAVIFileOpen || !pAVIFileCreateStream || !pAVISaveOptions ||
		!pAVISaveOptionsFree || !pAVIMakeCompressedStream || !pAVIStreamSetFormat || !pAVIStreamWrite ||
		!pAVIStreamRelease || !pAVIFileRelease)
	{
		FreeLibrary(avilib);
		avilib = NULL;
		return false;
	}

	pAVIFileInit();
	return true;
}

/* releases everything of a recording (also after a failed start) */
static void
CloseAvi(void)
{
	if (avicompressed)
	{
		pAVIStreamRelease(avicompressed);
		avicompressed = NULL;
	}

	if (avistream)
	{
		pAVIStreamRelease(avistream);
		avistream = NULL;
	}

	if (avifile)
	{
		pAVIFileRelease(avifile);
		avifile = NULL;
	}

	free(avibuffer);
	avibuffer = NULL;
	recording = false;
	paused = false;
}

static void
StopRecording(void)
{
	if (!recording)
	{
		return;
	}

	CloseAvi();
	Com_Printf("Record %d frames\n", framecount);
}

static void
StartRecording(const char *name)
{
	char path[MAX_OSPATH];
	AVISTREAMINFOA si;
	AVICOMPRESSOPTIONS opts, *popts = &opts;
	BITMAPINFOHEADER bih;
	cvar_t *nodialog;

	Com_sprintf(path, sizeof(path), "%s/%s", GL3_UserDir(), name);

	if (!LoadAviLib() || (pAVIFileOpen(&avifile, path, OF_WRITE | OF_CREATE, NULL) != 0))
	{
		avifile = NULL;
		Com_Printf("ERROR: Could not create '%s' AVI file\n", path);
		return;
	}

	avi_w = vid.width;
	avi_h = vid.height;
	avirowbytes = (avi_w * 3 + 3) & ~3;

	memset(&si, 0, sizeof(si));
	si.fccType = streamtypeVIDEO;
	si.dwScale = 1;
	si.dwRate = 10;
	si.dwSuggestedBufferSize = avirowbytes * avi_h;
	SetRect(&si.rcFrame, 0, 0, avi_w, avi_h);

	if (pAVIFileCreateStream(avifile, &avistream, &si) != 0)
	{
		avistream = NULL;
		Com_Printf("ERROR: Could not create AVI stream\n");
		CloseAvi();
		return;
	}

	memset(&opts, 0, sizeof(opts));
	nodialog = ri.Cvar_Get("r_om_avinodialog", "0", 0);

	if (nodialog->value)
	{
		/* test extension: "Full frames (uncompressed)" without the dialog */
		opts.fccType = streamtypeVIDEO;
		opts.fccHandler = mmioFOURCC('D', 'I', 'B', ' ');
		opts.dwFlags = AVICOMPRESSF_VALID;
	}
	else if (!pAVISaveOptions(NULL, 0, 1, &avistream, &popts))
	{
		Com_Printf("ERROR: Could not get AVI options\n");
		pAVISaveOptionsFree(1, &popts);
		CloseAvi();
		return;
	}

	if (pAVIMakeCompressedStream(&avicompressed, avistream, &opts, NULL) != 0)
	{
		avicompressed = NULL;
		Com_Printf("Could not create AVI compressed stream\n");

		if (!nodialog->value)
		{
			pAVISaveOptionsFree(1, &popts);
		}

		CloseAvi();
		return;
	}

	if (!nodialog->value)
	{
		pAVISaveOptionsFree(1, &popts);
	}

	memset(&bih, 0, sizeof(bih));
	bih.biSize = sizeof(bih);
	bih.biWidth = avi_w;
	bih.biHeight = avi_h; /* bottom-up rows */
	bih.biPlanes = 1;
	bih.biBitCount = 24;
	bih.biCompression = BI_RGB;
	bih.biSizeImage = avirowbytes * avi_h;

	if (pAVIStreamSetFormat(avicompressed, 0, &bih, sizeof(bih)) != 0)
	{
		Com_Printf("Could not set AVI format\n");
		CloseAvi();
		return;
	}

	avibuffer = malloc((size_t)avirowbytes * avi_h + (size_t)avi_w * 4 * avi_h);

	if (!avibuffer)
	{
		Com_Printf("Could not create AVI recording buffer\n");
		CloseAvi();
		return;
	}

	recording = true;
	paused = false;
	framecount = 0;
	capturemark = AviMilliseconds();
}

/* one frame, bottom-up BGR, key frame */
static void
WriteFrame(void)
{
	byte *rgba = avibuffer + (size_t)avirowbytes * avi_h;
	int x, y;

	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, avi_w, avi_h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	for (y = 0; y < avi_h; y++)
	{
		const byte *src = rgba + (size_t)y * avi_w * 4;
		byte *dst = avibuffer + (size_t)y * avirowbytes;

		for (x = 0; x < avi_w; x++)
		{
			dst[x * 3 + 0] = src[x * 4 + 2];
			dst[x * 3 + 1] = src[x * 4 + 1];
			dst[x * 3 + 2] = src[x * 4 + 0];
		}
	}

	if (pAVIStreamWrite(avicompressed, framecount, 1, avibuffer, avirowbytes * avi_h, AVIIF_KEYFRAME,
			NULL, NULL) != 0)
	{
		Com_Printf("Could not write frame %d of AVI\n", framecount);
		return;
	}

	framecount++;
}

#else /* !_WIN32: the command without a recorder */

static int
AviMilliseconds(void)
{
	return 0;
}

static void
StopRecording(void)
{
}

static void
StartRecording(const char *name)
{
	Com_Printf("ERROR: Could not create '%s/%s' AVI file\n", GL3_UserDir(), name);
}

static void
WriteFrame(void)
{
}

#endif

/* "avi [ record < name > ] [ pause ] [ stop ]" */
void
GL3_Avi_f(void)
{
	const char *cmd = (ri.Cmd_Argc() > 1) ? ri.Cmd_Argv(1) : "";

	if (!Q_stricmp(cmd, "record"))
	{
		StopRecording();

		if (ri.Cmd_Argc() > 2)
		{
			StartRecording(ri.Cmd_Argv(2));
			return;
		}

		/* black box (I4c): without a name the original also prints the usage line */
		Com_Printf("avi [ record < name > ] [ pause ] [ stop ]\n");
		return;
	}

	if (!Q_stricmp(cmd, "pause"))
	{
		if (!recording)
		{
			Com_Printf("AVI is not recording\n");
			return;
		}

		paused = !paused;

		if (paused)
		{
			Com_Printf("AVI Recording Paused\n");
		}

		return;
	}

	if (!Q_stricmp(cmd, "stop"))
	{
		StopRecording();
		return;
	}

	Com_Printf("avi [ record < name > ] [ pause ] [ stop ]\n");
}

/* end of every frame, before the buffer swap: one frame per 100 ms of real time at most */
void
GL3_Avi_Frame(void)
{
	int now;

	if (!recording || paused)
	{
		return;
	}

	now = AviMilliseconds();

	if (now - capturemark < 100)
	{
		return;
	}

	capturemark += 100;
	WriteFrame();
}

/* renderer shutdown also stops a recording */
void
GL3_Avi_Shutdown(void)
{
	StopRecording();
}
