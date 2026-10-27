// SPDX-License-Identifier: GPL-2.0-or-later
// Black-box test helper for the client effects package (I5), after gl3/test/DeskRun.cs (I4a):
// starts several processes (an original dedicated server, original clients, the OpenMerc client) on a
// separate, never shown desktop of the interactive window station, so no test window can take the
// focus or receive the owner's input. Non-blocking: Start returns a process id; Wait/Kill/KillAll.
// Watch() follows a game log and, for every line "OMSHOT <name>", writes <shotdir>\<name>.tga into a
// request file (read by the ref_omcap.dll capture shim of the original renderer, see gl3/test/refcap)
// or, without a request file, captures the window of the given class with PrintWindow. Lines
// "OMTRACE <name>" write <shotdir>\<name>_<n>.txt (n counts up from 0 per watcher) into the trace request file
// (the shim then logs every renderer call of one frame, entity lists included). "OMSHOT <name>#" numbers the
// captures (<name>_000, ...).
// Implements: SPEC-400 / SPEC-401 test harness (no game code)
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class FxRun
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    struct RECT { public int Left, Top, Right, Bottom; }
    delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lparam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktop(string name, IntPtr dev, IntPtr devmode, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool EnumDesktopWindows(IntPtr desk, EnumWindowsProc fn, IntPtr lparam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassName(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll")]
    static extern int GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    [DllImport("user32.dll")]
    static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")]
    static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool SetThreadDesktop(IntPtr desk);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
        IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool TerminateProcess(IntPtr h, uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr h);

    static IntPtr desk = IntPtr.Zero;
    static string deskName = "";
    const int MaxProcs = 16;
    static readonly int[] procIds = new int[MaxProcs];
    static readonly IntPtr[] procHandles = new IntPtr[MaxProcs];
    static readonly Thread[] watchers = new Thread[MaxProcs];
    static int numWatchers;
    static readonly object lk = new object();

    static int Slot(int pid)
    {
        for (int i = 0; i < MaxProcs; i++) if (procIds[i] == pid && procHandles[i] != IntPtr.Zero) return i;
        return -1;
    }
    static volatile bool stopWatch;

    public static void OpenDesktop(string name)
    {
        if (desk != IntPtr.Zero) return;
        deskName = name;
        desk = CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000u /* GENERIC_ALL */, IntPtr.Zero);
        if (desk == IntPtr.Zero) throw new Exception("CreateDesktop failed: " + Marshal.GetLastWin32Error());
    }

    public static int Start(string exe, string args, string dir)
    {
        var si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = "WinSta0\\" + deskName;
        PROCESS_INFORMATION pi;
        string cmd = "\"" + exe + "\" " + args;
        if (!CreateProcess(null, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, dir, ref si, out pi))
            throw new Exception("CreateProcess failed: " + Marshal.GetLastWin32Error());
        CloseHandle(pi.hThread);
        lock (lk)
        {
            for (int i = 0; i < MaxProcs; i++)
                if (procHandles[i] == IntPtr.Zero) { procIds[i] = pi.dwProcessId; procHandles[i] = pi.hProcess; break; }
        }
        return pi.dwProcessId;
    }

    // -1 still running after ms, else the exit code
    public static int Wait(int pid, int ms)
    {
        IntPtr h;
        lock (lk) { int i = Slot(pid); if (i < 0) return -2; h = procHandles[i]; }
        if (WaitForSingleObject(h, (uint)ms) != 0) return -1;
        uint code; GetExitCodeProcess(h, out code); return (int)code;
    }

    public static void Kill(int pid)
    {
        IntPtr h;
        lock (lk)
        {
            int i = Slot(pid);
            if (i < 0) return;
            h = procHandles[i];
            procHandles[i] = IntPtr.Zero;
            procIds[i] = 0;
        }
        if (WaitForSingleObject(h, 0) != 0) { TerminateProcess(h, 1); WaitForSingleObject(h, 10000); }
        CloseHandle(h);
    }

    public static void KillAll()
    {
        stopWatch = true;
        for (int i = 0; i < numWatchers; i++) { watchers[i].Join(2000); watchers[i] = null; }
        numWatchers = 0;
        int[] ids = new int[MaxProcs];
        lock (lk) Array.Copy(procIds, ids, MaxProcs);
        foreach (int id in ids) if (id != 0) Kill(id);
        if (desk != IntPtr.Zero) { CloseDesktop(desk); desk = IntPtr.Zero; }
        stopWatch = false;
    }

    static IntPtr FindWindow(int pid, string cls)
    {
        IntPtr found = IntPtr.Zero;
        EnumDesktopWindows(desk, (h, l) =>
        {
            int wpid; GetWindowThreadProcessId(h, out wpid);
            var sb = new StringBuilder(64);
            GetClassName(h, sb, 64);
            if (wpid == pid && (cls.Length == 0 || sb.ToString() == cls)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll")]
    static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")]
    static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc fn, IntPtr lparam);

    // diagnostics: class and title of every window of the process (message boxes included)
    public static string ListWindows(int pid)
    {
        var all = new StringBuilder();
        EnumDesktopWindows(desk, (h, l) =>
        {
            int wpid; GetWindowThreadProcessId(h, out wpid);
            if (wpid != pid) return true;
            var c = new StringBuilder(64); GetClassName(h, c, 64);
            var t = new StringBuilder(256); GetWindowText(h, t, 256);
            all.Append(c.ToString()).Append(" | ").Append(t.ToString());
            EnumChildWindows(h, (ch, l2) =>
            {
                var ct = new StringBuilder(512); GetWindowText(ch, ct, 512);
                if (ct.Length > 0) all.Append(" || ").Append(ct.ToString());
                return true;
            }, IntPtr.Zero);
            all.Append("\n");
            return true;
        }, IntPtr.Zero);
        return all.ToString();
    }

    static void Capture(IntPtr hwnd, string path)
    {
        RECT r; GetClientRect(hwnd, out r);
        int w = r.Right - r.Left, h = r.Bottom - r.Top;
        if (w <= 0 || h <= 0) return;
        using (var bmp = new Bitmap(w, h, PixelFormat.Format24bppRgb))
        {
            using (var g = Graphics.FromImage(bmp))
            {
                IntPtr hdc = g.GetHdc();
                PrintWindow(hwnd, hdc, 3); // PW_CLIENTONLY | PW_RENDERFULLCONTENT
                g.ReleaseHdc(hdc);
            }
            bmp.Save(path, ImageFormat.Png);
        }
    }

    // pid/windowClass: PrintWindow capture target when requestFile is null
    public static void Watch(string logPath, string shotDir, string requestFile, int pid, string windowClass)
    {
        Watch(logPath, shotDir, requestFile, pid, windowClass, null);
    }

    public static void Watch(string logPath, string shotDir, string requestFile, int pid, string windowClass,
        string traceRequestFile)
    {
        int traces = 0, shots = 0;
        var t = new Thread(() =>
        {
            SetThreadDesktop(desk);
            long logPos = 0;
            while (!stopWatch)
            {
                Thread.Sleep(20);
                if (!File.Exists(logPath)) continue;
                string text;
                try
                {
                    using (var fs = new FileStream(logPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                    {
                        if (fs.Length <= logPos) continue;
                        fs.Seek(logPos, SeekOrigin.Begin);
                        var buf = new byte[fs.Length - logPos];
                        int n = fs.Read(buf, 0, buf.Length);
                        int last = Array.LastIndexOf(buf, (byte)10, n - 1);
                        if (last < 0) continue;
                        text = Encoding.ASCII.GetString(buf, 0, last + 1);
                        logPos += last + 1;
                    }
                }
                catch (IOException) { continue; }
                foreach (var line in text.Split((char)10))
                {
                    int j = line.IndexOf("OMTRACE ");
                    if ((j >= 0) && (traceRequestFile != null))
                    {
                        string tname = line.Substring(j + 8).Trim();
                        if (tname.Length > 0)
                        {
                            // the shim takes one request per frame: wait until the previous one was taken
                            for (int w = 0; (w < 50) && File.Exists(traceRequestFile); w++) Thread.Sleep(5);
                            File.WriteAllText(traceRequestFile, Path.Combine(shotDir, string.Format("{0}_{1:D3}.txt", tname, traces++)));
                        }
                        continue;
                    }
                    int k = line.IndexOf("OMSHOT ");
                    if (k < 0) continue;
                    string name = line.Substring(k + 7).Trim();
                    if (name.Length == 0) continue;
                    // "OMSHOT <name>#": numbered captures <name>_000, <name>_001, ...
                    if (name.EndsWith("#")) name = string.Format("{0}_{1:D3}", name.Substring(0, name.Length - 1), shots++);
                    // one request per frame: wait until the shim has taken the previous one
                    for (int w = 0; (requestFile != null) && (w < 50) && File.Exists(requestFile); w++) Thread.Sleep(5);
                    if (requestFile != null)
                        File.WriteAllText(requestFile, Path.Combine(shotDir, name + ".tga"));
                    else
                    {
                        IntPtr hwnd = FindWindow(pid, windowClass);
                        if (hwnd != IntPtr.Zero) Capture(hwnd, Path.Combine(shotDir, name + ".png"));
                    }
                }
            }
        });
        t.IsBackground = true;
        t.Start();
        if (numWatchers < MaxProcs) watchers[numWatchers++] = t;
    }
}
