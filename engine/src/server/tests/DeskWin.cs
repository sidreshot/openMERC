// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc I1b test aid: lists the windows (class, title, child texts) of one process on a named desktop, so a
// stalled test run shows its message boxes. Used by sprun.ps1. No game code.
// Implements: SPEC-001 test harness
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class DeskWin
{
    delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lparam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr OpenDesktop(string name, int flags, bool inherit, uint access);
    [DllImport("user32.dll")]
    static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll")]
    static extern bool EnumDesktopWindows(IntPtr desk, EnumWindowsProc fn, IntPtr lparam);
    [DllImport("user32.dll")]
    static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc fn, IntPtr lparam);
    [DllImport("user32.dll")]
    static extern int GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetClassName(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr hwnd, StringBuilder sb, int max);

    public static string List(string desktop, int pid)
    {
        IntPtr desk = OpenDesktop(desktop, 0, false, 0x0041 /* DESKTOP_READOBJECTS | DESKTOP_ENUMERATE */);
        if (desk == IntPtr.Zero) return "OpenDesktop failed: " + Marshal.GetLastWin32Error();
        var all = new StringBuilder();
        try
        {
            EnumDesktopWindows(desk, (h, l) =>
            {
                int wpid; GetWindowThreadProcessId(h, out wpid);
                if (pid != 0 && wpid != pid) return true;
                var c = new StringBuilder(64); GetClassName(h, c, 64);
                var t = new StringBuilder(256); GetWindowText(h, t, 256);
                all.Append(wpid).Append(" ").Append(c.ToString()).Append(" | ").Append(t.ToString());
                EnumChildWindows(h, (ch, l2) =>
                {
                    var ct = new StringBuilder(512); GetWindowText(ch, ct, 512);
                    if (ct.Length > 0) all.Append(" || ").Append(ct.ToString());
                    return true;
                }, IntPtr.Zero);
                all.Append("\n");
                return true;
            }, IntPtr.Zero);
        }
        finally { CloseDesktop(desk); }
        return all.ToString();
    }
}
