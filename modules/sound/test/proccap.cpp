// SPDX-License-Identifier: GPL-2.0-or-later
// Implements: SPEC-004, SPEC-040 (black-box verification harness; no game code)
//
// proccap: records the audio output of one process tree (Windows 10 2004+ process loopback,
// AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK) into a 16-bit stereo WAV, so that a test run of the
// game can be captured without anything else that plays on the PC (the owner may be listening to
// something). Only the target process tree is recorded.
//
// usage: proccap <pid> <out.wav> [rate] [max_seconds]
//   rate defaults to 48000. The tool stops when the process exits, after max_seconds, or when the file
//   <out.wav>.stop appears. Gaps in the packet stream (no packets while the process renders nothing) are
//   filled with silence from the packet time stamps, so sample n of the file is n/rate seconds after the
//   first packet (gaps under 250 ms are time-stamp jitter and are not filled). The WAV header is rewritten every second, so a killed recorder still leaves a valid file.
//   <out.wav>.txt receives the QPC frequency and the QPC time of sample 0 for alignment with other logs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "ole32.lib")

struct Handler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
    LONG ref = 1;
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_FAIL;
    IAudioClient *client = nullptr;

    STDMETHODIMP QueryInterface(REFIID riid, void **pp) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            *pp = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
        else if (riid == __uuidof(IAgileObject))
            *pp = static_cast<IAgileObject *>(this);
        else {
            *pp = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        LONG r = InterlockedDecrement(&ref);
        if (!r)
            delete this;
        return r;
    }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override
    {
        HRESULT hr = E_FAIL;
        IUnknown *unk = nullptr;
        if (SUCCEEDED(op->GetActivateResult(&hr, &unk)) && SUCCEEDED(hr) && unk) {
            hr = unk->QueryInterface(__uuidof(IAudioClient), (void **)&client);
            unk->Release();
        }
        result = hr;
        SetEvent(done);
        return S_OK;
    }
};

static void write_header(FILE *f, int rate, unsigned long long frames)
{
    unsigned data = (unsigned)(frames * 4 > 0xFFFFFF00ull ? 0xFFFFFF00ull : frames * 4);
    unsigned riff = 36 + data;
    unsigned short fmt[8] = {1, 2, 0, 0, 0, 0, 4, 16};
    unsigned r = (unsigned)rate, br = (unsigned)rate * 4;
    long pos = ftell(f);
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    unsigned sixteen = 16;
    fwrite(&sixteen, 4, 1, f);
    fwrite(&fmt[0], 2, 1, f);
    fwrite(&fmt[1], 2, 1, f);
    fwrite(&r, 4, 1, f);
    fwrite(&br, 4, 1, f);
    fwrite(&fmt[6], 2, 1, f);
    fwrite(&fmt[7], 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    if (pos > 44)
        fseek(f, pos, SEEK_SET);
    fflush(f);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3) {
        fwprintf(stderr, L"usage: proccap <pid> <out.wav> [rate] [max_seconds]\n");
        return 2;
    }
    DWORD pid = (DWORD)_wtoi(argv[1]);
    const wchar_t *out = argv[2];
    int rate = argc > 3 ? _wtoi(argv[3]) : 48000;
    double maxsec = argc > 4 ? _wtof(argv[4]) : 3600.0;
    wchar_t stopfile[MAX_PATH], txtfile[MAX_PATH];
    swprintf(stopfile, MAX_PATH, L"%s.stop", out);
    swprintf(txtfile, MAX_PATH, L"%s.txt", out);
    DeleteFileW(stopfile);

    HANDLE proc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        fwprintf(stderr, L"proccap: cannot open process %lu\n", pid);
        return 1;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    AUDIOCLIENT_ACTIVATION_PARAMS ap = {};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv = {};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = (BYTE *)&ap;

    Handler *h = new Handler();
    IActivateAudioInterfaceAsyncOperation *op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, h, &op);
    if (FAILED(hr)) {
        fwprintf(stderr, L"proccap: ActivateAudioInterfaceAsync failed 0x%08lx\n", hr);
        return 1;
    }
    WaitForSingleObject(h->done, 10000);
    if (FAILED(h->result) || !h->client) {
        fwprintf(stderr, L"proccap: activation failed 0x%08lx\n", h->result);
        return 1;
    }
    IAudioClient *client = h->client;

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = (DWORD)rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = (DWORD)rate * 4;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                            2000000, 0, &wf, nullptr);
    if (FAILED(hr)) {
        fwprintf(stderr, L"proccap: Initialize failed 0x%08lx\n", hr);
        return 1;
    }
    client->SetEventHandle(ev);
    IAudioCaptureClient *cap = nullptr;
    hr = client->GetService(__uuidof(IAudioCaptureClient), (void **)&cap);
    if (FAILED(hr)) {
        fwprintf(stderr, L"proccap: GetService failed 0x%08lx\n", hr);
        return 1;
    }

    FILE *f = _wfopen(out, L"wb+");
    if (!f) {
        fwprintf(stderr, L"proccap: cannot write %s\n", out);
        return 1;
    }
    write_header(f, rate, 0);
    fseek(f, 44, SEEK_SET);

    LARGE_INTEGER qf, qstart;
    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&qstart);
    hr = client->Start();
    if (FAILED(hr)) {
        fwprintf(stderr, L"proccap: Start failed 0x%08lx\n", hr);
        return 1;
    }
    fwprintf(stderr, L"proccap: recording pid %lu at %d Hz into %s\n", pid, rate, out);

    unsigned long long written = 0, packets = 0, silentpk = 0, gapfill = 0, discont = 0, devgap = 0;
    long long first_qpc = -1;      // QPC (100 ns units from the capture client) of sample 0
    long long first_devpos = 0;
    std::vector<short> zeros(4096 * 2, 0);
    DWORD last_hdr = GetTickCount();
    for (;;) {
        DWORD w = WaitForSingleObject(ev, 100);
        if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0)
            break;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if ((double)(now.QuadPart - qstart.QuadPart) / qf.QuadPart > maxsec)
            break;
        if (GetFileAttributesW(stopfile) != INVALID_FILE_ATTRIBUTES)
            break;
        if (w == WAIT_OBJECT_0) {
            UINT32 n = 0;
            while (SUCCEEDED(cap->GetNextPacketSize(&n)) && n) {
                BYTE *data;
                UINT32 frames;
                DWORD flags;
                UINT64 devpos, qpc;
                if (FAILED(cap->GetBuffer(&data, &frames, &flags, &devpos, &qpc)))
                    break;
                packets++;
                if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
                    discont++;
                if (first_qpc < 0) {
                    first_qpc = (long long)qpc;
                    first_devpos = (long long)devpos;
                } else if (devpos) {
                    // the device position counts the stream's frames: a packet that starts later than the frames
                    // written so far follows a gap (frames lost by the recorder or not delivered) - fill it so the
                    // file keeps the stream's time line
                    long long expect = (long long)devpos - first_devpos;
                    if (expect > (long long)written) {
                        unsigned long long fill = (unsigned long long)(expect - (long long)written);
                        devgap += fill;
                        while (fill) {
                            unsigned long long k = fill > 4096 ? 4096 : fill;
                            fwrite(zeros.data(), 4, (size_t)k, f);
                            written += k;
                            fill -= k;
                        }
                    }
                } else {
                    // expected time of this packet's first sample vs. its time stamp: fill gaps > 250 ms (the stream
                    // is continuous while the game has an output open; smaller differences are time-stamp jitter)
                    double expect = (double)written / rate;
                    double stamp = (double)((long long)qpc - first_qpc) * 1e-7;
                    if (stamp - expect > 0.250) {
                        unsigned long long fill = (unsigned long long)((stamp - expect) * rate);
                        gapfill += fill;
                        while (fill) {
                            unsigned long long k = fill > 4096 ? 4096 : fill;
                            fwrite(zeros.data(), 4, (size_t)k, f);
                            written += k;
                            fill -= k;
                        }
                    }
                }
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    silentpk++;
                    UINT32 left = frames;
                    while (left) {
                        UINT32 k = left > 4096 ? 4096 : left;
                        fwrite(zeros.data(), 4, k, f);
                        left -= k;
                    }
                } else
                    fwrite(data, 4, frames, f);
                written += frames;
                cap->ReleaseBuffer(frames);
            }
        }
        if (GetTickCount() - last_hdr > 1000) {
            write_header(f, rate, written);
            last_hdr = GetTickCount();
        }
    }
    client->Stop();
    write_header(f, rate, written);
    fclose(f);
    FILE *t = _wfopen(txtfile, L"w");
    if (t) {
        fprintf(t, "rate %d\nframes %llu\npackets %llu\nsilent_packets %llu\ngap_fill_frames %llu\n"
                   "first_packet_qpc_100ns %lld\nrecorder_start_qpc %lld\nqpc_freq %lld\ndiscontinuities %llu\n"
                   "devpos_gap_frames %llu\n",
                rate, written, packets, silentpk, gapfill, first_qpc, (long long)qstart.QuadPart,
                (long long)qf.QuadPart, discont, devgap);
        fclose(t);
    }
    fwprintf(stderr, L"proccap: %llu frames (%.1f s), %llu packets, %llu silent, %llu gap-filled, %llu discontinuities, "
                     L"%llu frames filled by device position\n",
             written, (double)written / rate, packets, silentpk, gapfill, discont, devgap);
    return 0;
}
