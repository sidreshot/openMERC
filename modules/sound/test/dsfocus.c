/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Implements: SPEC-004, SPEC-040 (black-box test harness for the original sound backends; no game code)
 *
 * dsfocus: a dsound.dll proxy for test runs only. The original backends load "dsound.dll" at run time and
 * create a normal secondary buffer, which DirectSound mutes while the game window is not the foreground
 * window. Test runs never give the game the focus (they run on a separate desktop), so this proxy, placed
 * next to SoF.exe in the test copy, forwards DirectSoundCreate to the system dsound.dll and adds
 * DSBCAPS_GLOBALFOCUS to every secondary buffer the game creates. Nothing else is changed; the sample
 * data, format and timing stay those of the backend. Every buffer request is logged to dsfocus.log next to
 * the executable (flags before/after, format) as evidence.
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define INITGUID
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <stdio.h>

typedef HRESULT(WINAPI *DSCREATE)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);

static HMODULE real_dll;
static DSCREATE real_create;
static FILE *logf;

static void logline(const char *fmt, ...)
{
    va_list ap;
    if (!logf) {
        char path[MAX_PATH], *slash;
        GetModuleFileNameA(NULL, path, sizeof(path));
        slash = strrchr(path, '\\');
        if (slash)
            strcpy(slash + 1, "dsfocus.log");
        logf = fopen(path, "a");
        if (!logf)
            return;
    }
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fflush(logf);
}

typedef struct {
    IDirectSoundVtbl *lpVtbl;
    LONG ref;
    IDirectSound *real;
} wrap_t;

static HRESULT STDMETHODCALLTYPE W_QueryInterface(IDirectSound *self, REFIID riid, void **pp)
{
    wrap_t *w = (wrap_t *)self;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectSound)) {
        InterlockedIncrement(&w->ref);
        *pp = self;
        return S_OK;
    }
    return w->real->lpVtbl->QueryInterface(w->real, riid, pp);
}
static ULONG STDMETHODCALLTYPE W_AddRef(IDirectSound *self)
{
    return (ULONG)InterlockedIncrement(&((wrap_t *)self)->ref);
}
static ULONG STDMETHODCALLTYPE W_Release(IDirectSound *self)
{
    wrap_t *w = (wrap_t *)self;
    LONG r = InterlockedDecrement(&w->ref);
    if (!r) {
        w->real->lpVtbl->Release(w->real);
        HeapFree(GetProcessHeap(), 0, w);
    }
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE W_CreateSoundBuffer(IDirectSound *self, LPCDSBUFFERDESC desc,
                                                     LPDIRECTSOUNDBUFFER *pp, LPUNKNOWN outer)
{
    wrap_t *w = (wrap_t *)self;
    DSBUFFERDESC d;
    HRESULT hr;
    if (!desc)
        return w->real->lpVtbl->CreateSoundBuffer(w->real, desc, pp, outer);
    d = *desc;
    if (!(d.dwFlags & DSBCAPS_PRIMARYBUFFER))
        d.dwFlags |= DSBCAPS_GLOBALFOCUS;
    hr = w->real->lpVtbl->CreateSoundBuffer(w->real, &d, pp, outer);
    if (d.lpwfxFormat)
        logline("CreateSoundBuffer flags 0x%08lx -> 0x%08lx bytes %lu fmt %u ch %u Hz %lu bits %u: hr 0x%08lx\n",
                desc->dwFlags, d.dwFlags, d.dwBufferBytes, d.lpwfxFormat->wFormatTag, d.lpwfxFormat->nChannels,
                d.lpwfxFormat->nSamplesPerSec, d.lpwfxFormat->wBitsPerSample, hr);
    else
        logline("CreateSoundBuffer flags 0x%08lx -> 0x%08lx (no format): hr 0x%08lx\n", desc->dwFlags, d.dwFlags, hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE W_GetCaps(IDirectSound *self, LPDSCAPS caps)
{
    wrap_t *w = (wrap_t *)self;
    return w->real->lpVtbl->GetCaps(w->real, caps);
}
static HRESULT STDMETHODCALLTYPE W_DuplicateSoundBuffer(IDirectSound *self, LPDIRECTSOUNDBUFFER src,
                                                        LPDIRECTSOUNDBUFFER *dst)
{
    wrap_t *w = (wrap_t *)self;
    return w->real->lpVtbl->DuplicateSoundBuffer(w->real, src, dst);
}
static HRESULT STDMETHODCALLTYPE W_SetCooperativeLevel(IDirectSound *self, HWND hwnd, DWORD level)
{
    wrap_t *w = (wrap_t *)self;
    HRESULT hr = w->real->lpVtbl->SetCooperativeLevel(w->real, hwnd, level);
    logline("SetCooperativeLevel %lu: hr 0x%08lx\n", level, hr);
    return hr;
}
static HRESULT STDMETHODCALLTYPE W_Compact(IDirectSound *self)
{
    wrap_t *w = (wrap_t *)self;
    return w->real->lpVtbl->Compact(w->real);
}
static HRESULT STDMETHODCALLTYPE W_GetSpeakerConfig(IDirectSound *self, LPDWORD cfg)
{
    wrap_t *w = (wrap_t *)self;
    return w->real->lpVtbl->GetSpeakerConfig(w->real, cfg);
}
static HRESULT STDMETHODCALLTYPE W_SetSpeakerConfig(IDirectSound *self, DWORD cfg)
{
    wrap_t *w = (wrap_t *)self;
    return w->real->lpVtbl->SetSpeakerConfig(w->real, cfg);
}
static HRESULT STDMETHODCALLTYPE W_Initialize(IDirectSound *self, LPCGUID guid)
{
    wrap_t *w = (wrap_t *)self;
    return w->real->lpVtbl->Initialize(w->real, guid);
}

static IDirectSoundVtbl wrap_vtbl = {
    W_QueryInterface, W_AddRef,           W_Release,  W_CreateSoundBuffer, W_GetCaps,         W_DuplicateSoundBuffer,
    W_SetCooperativeLevel, W_Compact, W_GetSpeakerConfig, W_SetSpeakerConfig, W_Initialize,
};

static BOOL load_real(void)
{
    char path[MAX_PATH];
    if (real_create)
        return TRUE;
    GetSystemDirectoryA(path, MAX_PATH);
    strcat(path, "\\dsound.dll");
    real_dll = LoadLibraryA(path);
    if (!real_dll)
        return FALSE;
    real_create = (DSCREATE)GetProcAddress(real_dll, "DirectSoundCreate");
    return real_create != NULL;
}

HRESULT WINAPI DirectSoundCreate(LPCGUID guid, LPDIRECTSOUND *pp, LPUNKNOWN outer)
{
    IDirectSound *real = NULL;
    wrap_t *w;
    HRESULT hr;
    if (!load_real())
        return DSERR_NODRIVER;
    hr = real_create(guid, &real, outer);
    logline("DirectSoundCreate: hr 0x%08lx\n", hr);
    if (FAILED(hr) || !real) {
        *pp = real;
        return hr;
    }
    w = (wrap_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    w->lpVtbl = &wrap_vtbl;
    w->ref = 1;
    w->real = real;
    *pp = (IDirectSound *)w;
    return hr;
}
