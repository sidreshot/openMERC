// SPDX-License-Identifier: GPL-2.0-or-later
// Starts a process on a separate (never shown) desktop of the interactive window station, so a test
// run of the game can neither take the focus nor receive the owner's input. Used by run_sof.ps1.
// Whenever the game's log file gains a line "OMSHOT <name>" (written by the test script with `echo`), it
// either writes <outdir>\<name>.tga into requestFile for the opengl32.dll capture proxy (glproxy/), or,
// without a request file, captures the window with PrintWindow as <outdir>\<name>.png.
// Implements: SPEC-002 / SPEC-412 test harness (no game code)
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class DeskRun
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
    static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")]
    static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool SetThreadDesktop(IntPtr desk);
    [DllImport("user32.dll")]
    static extern IntPtr GetThreadDesktop(int threadId);
    [DllImport("kernel32.dll")]
    static extern int GetCurrentThreadId();
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
    [DllImport("kernel32.dll")]
    static extern int GetProcessId(IntPtr h);

    static IntPtr FindGameWindow(IntPtr desk, string cls)
    {
        IntPtr found = IntPtr.Zero;
        EnumDesktopWindows(desk, (h, l) =>
        {
            var sb = new StringBuilder(64);
            GetClassName(h, sb, 64);
            if (sb.ToString() == cls) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
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

    static int Monitor(IntPtr desk, IntPtr hProcess, int timeoutMs, string logPath, string shotDir, string windowClass, string requestFile)
    {
        if (desk != IntPtr.Zero) SetThreadDesktop(desk);
        var sw = System.Diagnostics.Stopwatch.StartNew();
        long logPos = 0;
        while (true)
        {
            if (WaitForSingleObject(hProcess, 20) == 0)
            {
                uint code; GetExitCodeProcess(hProcess, out code); return (int)code;
            }
            if (sw.ElapsedMilliseconds > timeoutMs)
            {
                TerminateProcess(hProcess, 1);
                WaitForSingleObject(hProcess, 10000);
                return -1;
            }
            if (logPath == null || shotDir == null || !File.Exists(logPath)) continue;
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
                if (line.Contains("OMMODULES") && shotDir != null)
                {
                    // list the modules loaded by the game (black-box observation of the process)
                    try
                    {
                        var sb = new StringBuilder();
                        foreach (System.Diagnostics.ProcessModule m in
                                 System.Diagnostics.Process.GetProcessById(GetProcessId(hProcess)).Modules)
                            sb.AppendLine(m.ModuleName + "\t" + m.FileName);
                        File.WriteAllText(Path.Combine(shotDir, "modules.txt"), sb.ToString());
                    }
                    catch (Exception e) { File.WriteAllText(Path.Combine(shotDir, "modules.txt"), e.ToString()); }
                    continue;
                }
                int tk = line.IndexOf("OMTRACE ");
                if (tk >= 0 && requestFile != null)
                {
                    // the capture shim logs all renderer calls of the next frame
                    string tname = line.Substring(tk + 8).Trim();
                    if (tname.Length > 0)
                        File.WriteAllText(Path.Combine(Path.GetDirectoryName(requestFile), "omtrace.req"),
                            Path.Combine(shotDir, tname + ".txt"));
                    continue;
                }
                int k = line.IndexOf("OMSHOT ");
                if (k < 0) continue;
                string name = line.Substring(k + 7).Trim();
                if (name.Length == 0) continue;
                if (requestFile != null)
                {
                    // the opengl32.dll capture proxy reads the back buffer at the next buffer swap
                    File.WriteAllText(requestFile, Path.Combine(shotDir, name + ".tga"));
                }
                else
                {
                    IntPtr hwnd = FindGameWindow(desk, windowClass);
                    if (hwnd != IntPtr.Zero) Capture(hwnd, Path.Combine(shotDir, name + ".png"));
                }
            }
        }
    }

    // Returns the exit code, or -1 when the process had to be killed after timeoutMs.
    public static int Run(string desktop, string exe, string args, string dir, int timeoutMs,
        string logPath, string shotDir, string windowClass, string requestFile)
    {
        // desktop "" = the owner's desktop (the caller must then keep the window off-screen)
        IntPtr desk = IntPtr.Zero;
        if (desktop.Length > 0)
        {
            desk = CreateDesktop(desktop, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000u /* GENERIC_ALL */, IntPtr.Zero);
            if (desk == IntPtr.Zero) throw new Exception("CreateDesktop failed: " + Marshal.GetLastWin32Error());
        }
        try
        {
            var si = new STARTUPINFO();
            si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
            si.lpDesktop = desktop.Length > 0 ? "WinSta0\\" + desktop : null;
            PROCESS_INFORMATION pi;
            string cmd = "\"" + exe + "\" " + args;
            if (!CreateProcess(null, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, dir, ref si, out pi))
                throw new Exception("CreateProcess failed: " + Marshal.GetLastWin32Error());
            int result = -1;
            // The monitor runs on its own thread: SetThreadDesktop needs a thread without windows.
            var t = new Thread(() => { result = Monitor(desk, pi.hProcess, timeoutMs, logPath, shotDir, windowClass, requestFile); });
            t.Start();
            t.Join();
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            return result;
        }
        finally { if (desk != IntPtr.Zero) CloseDesktop(desk); }
    }
}
