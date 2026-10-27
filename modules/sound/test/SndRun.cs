// SPDX-License-Identifier: GPL-2.0-or-later
// Implements: SPEC-004, SPEC-040 (black-box test harness for OMsnd; no game code)
// Starts processes on a separate, never shown desktop of the interactive window station, so test runs of the
// game can neither take the focus nor receive the owner's input (and no window appears on the owner's screen).
// Used by run_session.ps1 / run_mp.ps1. Only processes started here are ever terminated.
using System;
using System.Runtime.InteropServices;

public class SndProc
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

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktop(string name, IntPtr dev, IntPtr devmode, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool CloseDesktop(IntPtr h);
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
    static int deskUsers = 0;

    IntPtr proc;
    bool ownsDesk;
    public int Pid;

    // desktopName "" = the owner's desktop, started minimised and not activated
    public static SndProc Start(string desktopName, string exe, string args, string dir)
    {
        SndProc p = new SndProc();
        STARTUPINFO si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        if (!string.IsNullOrEmpty(desktopName))
        {
            if (desk == IntPtr.Zero)
            {
                desk = CreateDesktop(desktopName, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);
                if (desk == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            }
            deskUsers++;
            p.ownsDesk = true;
            si.lpDesktop = desktopName;
        }
        else
        {
            si.dwFlags = 1; si.wShowWindow = 7; // STARTF_USESHOWWINDOW, SW_SHOWMINNOACTIVE
        }
        PROCESS_INFORMATION pi;
        if (!CreateProcess(exe, "\"" + exe + "\" " + args, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, dir,
                           ref si, out pi))
            throw new System.ComponentModel.Win32Exception();
        CloseHandle(pi.hThread);
        p.proc = pi.hProcess;
        p.Pid = pi.dwProcessId;
        return p;
    }

    // waits for the process; kills it after timeoutMs. Returns the exit code, or -1 when it was killed.
    public int Wait(int timeoutMs)
    {
        if (proc == IntPtr.Zero) return -2;
        int code;
        if (WaitForSingleObject(proc, (uint)Math.Max(0, timeoutMs)) != 0)
        {
            TerminateProcess(proc, 1);
            WaitForSingleObject(proc, 10000);
            code = -1;
        }
        else
        {
            uint c; GetExitCodeProcess(proc, out c); code = (int)c;
        }
        CloseHandle(proc);
        proc = IntPtr.Zero;
        if (ownsDesk && --deskUsers == 0 && desk != IntPtr.Zero) { CloseDesktop(desk); desk = IntPtr.Zero; }
        ownsDesk = false;
        return code;
    }

    public bool HasExited()
    {
        return proc == IntPtr.Zero || WaitForSingleObject(proc, 0) == 0;
    }
}
