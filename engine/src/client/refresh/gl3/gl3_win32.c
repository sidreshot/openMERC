/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer, SoF module build: the window and OpenGL context on
 * Windows. The SoF engine hands its instance and window procedure to the
 * renderer (SPEC-002 Init); the renderer creates the window (class and title
 * "SoF", the engine's icon resource 101), sets the video mode, chooses the
 * pixel format and makes the context. It replaces gl3_sdl.c in that build.
 *
 * Implements: SPEC-002 (Init, AppActivate, EndFrame), SPEC-413 §2, §3, §6, §10
 */

#ifdef OM_SOFREF

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "header/local.h"

qboolean IsHighDPIaware = false;

static HINSTANCE engine_instance;
static WNDPROC engine_wndproc;
static HWND window;
static HDC dc;
static HGLRC context;
static HMODULE opengl32;
static qboolean fullscreen_set;
static qboolean vsyncActive;
static int window_width, window_height;

typedef BOOL (WINAPI *pfnSwapInterval)(int);

extern void (*gl3_NewWindow)(int width, int height);

void
GL3_Win32_SetEngine(void *hinstance, void *wndproc)
{
	engine_instance = (HINSTANCE)hinstance;
	engine_wndproc = (WNDPROC)wndproc;
}

static void *
GetGLProc(const char *name)
{
	void *p = (void *)wglGetProcAddress(name);

	if ((p == NULL) || (p == (void *)1) || (p == (void *)2) || (p == (void *)3) || (p == (void *)-1))
	{
		p = opengl32 ? (void *)GetProcAddress(opengl32, name) : NULL;
	}

	return p;
}

static void
DestroyGLWindow(void)
{
	if (context)
	{
		if (!wglMakeCurrent(NULL, NULL))
		{
			Com_Printf("ref_gl::R_Shutdown() - wglMakeCurrent failed\n");
		}

		if (!wglDeleteContext(context))
		{
			Com_Printf("ref_gl::R_Shutdown() - wglDeleteContext failed\n");
		}

		context = NULL;
	}

	if (dc)
	{
		if (!ReleaseDC(window, dc))
		{
			Com_Printf("ref_gl::R_Shutdown() - ReleaseDC failed\n");
		}

		dc = NULL;
	}

	if (window)
	{
		DestroyWindow(window);
		window = NULL;
	}

	UnregisterClassA("SoF", engine_instance);

	if (fullscreen_set)
	{
		ChangeDisplaySettings(NULL, 0);
		fullscreen_set = false;
	}
}

static qboolean
CreateGLWindow(int width, int height, qboolean fullscreen)
{
	WNDCLASSA wc;
	RECT r;
	DWORD style, exstyle;
	int x = 0, y = 0, w, h, pf;
	PIXELFORMATDESCRIPTOR pfd;
	cvar_t *vid_xpos, *vid_ypos;
	const char *renderer;

	memset(&wc, 0, sizeof(wc));
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = engine_wndproc ? engine_wndproc : DefWindowProcA;
	wc.hInstance = engine_instance;
	wc.hIcon = LoadIconA(engine_instance, MAKEINTRESOURCEA(101));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)COLOR_GRAYTEXT;
	wc.lpszClassName = "SoF";

	if (!RegisterClassA(&wc))
	{
		Com_Error(ERR_FATAL, "Couldn't register window class");
		return false;
	}

	if (fullscreen)
	{
		exstyle = WS_EX_TOPMOST;
		style = WS_POPUP | WS_VISIBLE;
	}
	else
	{
		exstyle = 0;
		style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
	}

	r.left = 0;
	r.top = 0;
	r.right = width;
	r.bottom = height;
	AdjustWindowRect(&r, style, FALSE);
	w = r.right - r.left;
	h = r.bottom - r.top;

	if (!fullscreen)
	{
		vid_xpos = ri.Cvar_Get("vid_xpos", "3", CVAR_ARCHIVE);
		vid_ypos = ri.Cvar_Get("vid_ypos", "22", CVAR_ARCHIVE);
		x = (int)vid_xpos->value;
		y = (int)vid_ypos->value;
	}

	window = CreateWindowExA(exstyle, "SoF", "SoF", style, x, y, w, h, NULL, NULL, engine_instance, NULL);

	if (!window)
	{
		Com_Error(ERR_FATAL, "Couldn't create window");
		return false;
	}

	ShowWindow(window, SW_SHOW);
	UpdateWindow(window);

	dc = GetDC(window);

	if (!dc)
	{
		Com_Printf("GLimp_Init() - GetDC failed\n");
		DestroyGLWindow();
		return false;
	}

	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 24;
	pfd.cAlphaBits = 8;
	pfd.cDepthBits = 32;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	pf = ChoosePixelFormat(dc, &pfd);

	if (!pf)
	{
		Com_Printf("GLimp_Init() - ChoosePixelFormat failed\n");
		DestroyGLWindow();
		return false;
	}

	if (!SetPixelFormat(dc, pf, &pfd))
	{
		Com_Printf("GLimp_Init() - SetPixelFormat failed\n");
		DestroyGLWindow();
		return false;
	}

	DescribePixelFormat(dc, pf, sizeof(pfd), &pfd);
	Com_Printf("GL_PFD: color(%d-bits) Z(%d-bit)\n", (int)pfd.cColorBits, (int)pfd.cDepthBits);
	gl3config.stencil = pfd.cStencilBits >= 8;

	/* a legacy context: on current drivers the compatibility profile of the
	   newest version, so the GL 3 core functions and (for the Ghoul system,
	   package I4b) the GL 1.1 functions are both available */
	context = wglCreateContext(dc);

	if (!context)
	{
		Com_Printf("GLimp_Init() - wglCreateContext failed\n");
		DestroyGLWindow();
		return false;
	}

	if (!wglMakeCurrent(dc, context))
	{
		Com_Printf("GLimp_Init() - wglMakeCurrent failed\n");
		DestroyGLWindow();
		return false;
	}

	/* I4b (testing): r_om_coreprofile 1 replaces the context by a GL 3.2 core-profile one, so the
	   Ghoul system draws through the GL 1.1 emulation (gl3_ghoulemu.c) as in the OpenMerc engine */
	if (ri.Cvar_Get("r_om_coreprofile", "0", 0)->value)
	{
		typedef HGLRC (WINAPI *pfnCreateContextAttribs)(HDC, HGLRC, const int *);
		static const int attribs[] = {0x2091, 3, 0x2092, 2, 0x9126, 0x1, 0}; /* 3.2, core profile */
		pfnCreateContextAttribs createAttribs =
			(pfnCreateContextAttribs)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
		HGLRC core = createAttribs ? createAttribs(dc, NULL, attribs) : NULL;

		if (core && wglMakeCurrent(dc, core))
		{
			wglDeleteContext(context);
			context = core;
			Com_Printf("...using a GL 3.2 core-profile context (r_om_coreprofile)\n");
		}
		else
		{
			if (core)
			{
				wglDeleteContext(core);
			}

			wglMakeCurrent(dc, context);
			Com_Printf("...no core-profile context available\n");
		}
	}

	if (!opengl32)
	{
		opengl32 = LoadLibraryA("opengl32.dll");
	}

	if (!gladLoadGLLoader((GLADloadproc)GetGLProc))
	{
		Com_Printf("GL3_InitContext(): ERROR: loading OpenGL function pointers failed!\n");
		DestroyGLWindow();
		return false;
	}

	if ((GLVersion.major < 3) || ((GLVersion.major == 3) && (GLVersion.minor < 2)))
	{
		Com_Printf("GL3_InitContext(): ERROR: OpenGL 3.2 needed, got %d.%d\n", GLVersion.major, GLVersion.minor);
		DestroyGLWindow();
		return false;
	}

	renderer = (const char *)glGetString(GL_RENDERER);

	if (renderer && strstr(renderer, "GDI Generic") && !(gl_allow_software && gl_allow_software->value))
	{
		Com_Printf("GLimp_Init() - no hardware acceleration detected\n");
		DestroyGLWindow();
		return false;
	}

	gl3config.anisotropic = GLAD_GL_EXT_texture_filter_anisotropic != 0;
	gl3config.debug_output = GLAD_GL_ARB_debug_output != 0;
	gl3config.major_version = GLVersion.major;
	gl3config.minor_version = GLVersion.minor;

	/* the window gets the input focus like the original's (the test harness runs
	   the game on its own desktop, so this never reaches the owner's desktop) */
	SetForegroundWindow(window);
	SetFocus(window);

	window_width = width;
	window_height = height;

	if (gl3_NewWindow)
	{
		gl3_NewWindow(width, height);
	}

	GL3_SetVsync();
	return true;
}

/* SPEC-413 §3: one mode-setting attempt; returns false if no window could be made */
qboolean
GL3_Win32_InitGraphics(int fullscreen, int *pwidth, int *pheight)
{
	int width = *pwidth, height = *pheight;

	DestroyGLWindow();

	if (fullscreen)
	{
		DEVMODE dm;

		Com_Printf("...attempting fullscreen\n");
		memset(&dm, 0, sizeof(dm));
		dm.dmSize = sizeof(dm);
		dm.dmPelsWidth = width;
		dm.dmPelsHeight = height;
		dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

		if (gl_bitdepth && gl_bitdepth->value)
		{
			dm.dmBitsPerPel = (int)gl_bitdepth->value;
			dm.dmFields |= DM_BITSPERPEL;
			Com_Printf("...using gl_bitdepth of %d\n", (int)gl_bitdepth->value);
		}
		else
		{
			HDC hdc = GetDC(GetDesktopWindow());

			Com_Printf("...using desktop display depth of %d\n", GetDeviceCaps(hdc, BITSPIXEL));
			ReleaseDC(GetDesktopWindow(), hdc);
		}

		if (gl_displayrefresh && gl_displayrefresh->value)
		{
			dm.dmDisplayFrequency = (int)gl_displayrefresh->value;
			dm.dmFields |= DM_DISPLAYFREQUENCY;
		}

		Com_Printf("...calling CDS: ");

		if (ChangeDisplaySettings(&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
		{
			Com_Printf("ok\n");
			fullscreen_set = true;

			if (CreateGLWindow(width, height, true))
			{
				return true;
			}

			return false;
		}

		Com_Printf("failed\n");
		Com_Printf("...setting windowed mode\n");
		ChangeDisplaySettings(NULL, 0);
		ri.Cvar_SetValue("vid_fullscreen", 0);
	}
	else
	{
		ri.Cvar_SetValue("gl_bitdepth", 0);
		Com_Printf("...setting windowed mode\n");
	}

	return CreateGLWindow(width, height, false);
}

void
GL3_Win32_Shutdown(void)
{
	DestroyGLWindow();

	if (opengl32)
	{
		FreeLibrary(opengl32);
		opengl32 = NULL;
	}
}

/* SPEC-002 slot 0xc4 */
void
GL3_Win32_AppActivate(int active)
{
	if (!window)
	{
		return;
	}

	if (active)
	{
		SetForegroundWindow(window);
		ShowWindow(window, SW_RESTORE);
	}
	else if (fullscreen_set)
	{
		ShowWindow(window, SW_MINIMIZE);
	}
}

/* the gl3_sdl.c functions the core calls */

int
GL3_InitContext(void *win)
{
	(void)win;
	return context != NULL;
}

void
GL3_GetDrawableSize(int *width, int *height)
{
	*width = window_width;
	*height = window_height;
}

int
GL3_PrepareForWindow(void)
{
	return 0;
}

qboolean
GL3_IsVsyncActive(void)
{
	return vsyncActive;
}

void
GL3_SwapWindow(void)
{
	/* SPEC-413 §6: buffers are swapped only while gl_drawbuffer is GL_BACK */
	if (gl_drawbuffer && Q_stricmp(gl_drawbuffer->string, "GL_BACK"))
	{
		return;
	}

	if (dc && !SwapBuffers(dc))
	{
		Com_Error(ERR_FATAL, "GLimp_EndFrame() - SwapBuffers() failed!");
	}
}

void
GL3_SetVsync(void)
{
	pfnSwapInterval swapInterval = (pfnSwapInterval)GetGLProc("wglSwapIntervalEXT");

	if (swapInterval && gl_swapinterval)
	{
		swapInterval((int)gl_swapinterval->value);
		vsyncActive = gl_swapinterval->value != 0;
	}
}

void
GL3_ShutdownContext(void)
{
}

int
GL3_GetSDLVersion(void)
{
	return 0;
}

#endif /* OM_SOFREF */
