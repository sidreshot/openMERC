// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc X1 - typed text for a hidden test session (xtype.ps1). Waits for the game window on the test desktop
// and, whenever the game's log gains a line "OMTYPE <n>", posts step n to it: for every character a key press
// (WM_KEYDOWN, the character as WM_CHAR, WM_KEYUP), i.e. the message sequence a keyboard layout produces. The
// key is the character's key on the US layout (ASCII) or the Russian layout (Cyrillic); Shift is pressed around
// shifted characters. Window messages only: the owner's keyboard, layout and focus are never touched.
// Step syntax: text, with {ENTER} {ESC} {BS} {CON} (console key) {PACKET:x} (VK_PACKET carrying character x, as an input method).
// Implements: black-box test harness (no game code)
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class XType
{
    delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lparam);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenDesktop(string name, int flags, bool inherit, uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll")] static extern bool EnumDesktopWindows(IntPtr desk, EnumWindowsProc fn, IntPtr lparam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern uint MapVirtualKeyW(uint code, uint type);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern short VkKeyScanExW(char c, IntPtr hkl);
    [DllImport("user32.dll")] static extern int GetKeyboardLayoutList(int n, IntPtr[] list);

    const uint WM_KEYDOWN = 0x100, WM_KEYUP = 0x101, WM_CHAR = 0x102;
    static IntPtr hwnd, us, ru;
    public static List<string> Log = new List<string>();

    static void Key(int vk, char ch)
    {
        uint sc = MapVirtualKeyW((uint)vk, 0);
        PostMessageW(hwnd, WM_KEYDOWN, (IntPtr)vk, (IntPtr)(1 | (sc << 16)));
        if (ch != '\0') PostMessageW(hwnd, WM_CHAR, (IntPtr)ch, (IntPtr)(1 | (sc << 16)));
        PostMessageW(hwnd, WM_KEYUP, (IntPtr)vk, (IntPtr)unchecked((int)(1 | (sc << 16) | (1u << 30) | (1u << 31))));
        Thread.Sleep(35);
    }

    static void Char(char c)
    {
        short r = VkKeyScanExW(c, c < 128 ? us : ru);
        if (r == -1) { Log.Add("no key for U+" + ((int)c).ToString("X4")); return; }
        bool shift = ((r >> 8) & 1) != 0;
        if (shift) { PostMessageW(hwnd, WM_KEYDOWN, (IntPtr)0x10, (IntPtr)(1 | (0x2A << 16))); Thread.Sleep(15); }
        Key(r & 0xFF, c);
        if (shift) { PostMessageW(hwnd, WM_KEYUP, (IntPtr)0x10, (IntPtr)unchecked((int)(1 | (0x2A << 16) | (3u << 30)))); Thread.Sleep(15); }
    }

    static void Step(string s)
    {
        for (int i = 0; i < s.Length; i++)
        {
            if (s[i] == '{')
            {
                int e = s.IndexOf('}', i);
                string t = s.Substring(i + 1, e - i - 1);
                i = e;
                if (t == "ENTER") Key(0x0D, '\r');
                else if (t == "CON") Key(0xC0, '`'); // the console key (grave accent on the US layout)
                else if (t == "ESC") Key(0x1B, (char)27);
                else if (t == "BS") Key(0x08, (char)8);
                else if (t.StartsWith("PACKET:"))
                {
                    char c = t[7];
                    PostMessageW(hwnd, WM_KEYDOWN, (IntPtr)0xE7, (IntPtr)1);
                    PostMessageW(hwnd, WM_CHAR, (IntPtr)c, (IntPtr)1);
                    PostMessageW(hwnd, WM_KEYUP, (IntPtr)0xE7, (IntPtr)unchecked((int)(1 | (3u << 30))));
                    Thread.Sleep(35);
                }
                continue;
            }
            Char(s[i]);
        }
    }

    // runs until the log shows "OMTEST done" or timeoutMs; returns the number of steps posted
    public static int Run(string desktop, string logPath, string[] steps, int timeoutMs)
    {
        var l = new IntPtr[16]; int n = GetKeyboardLayoutList(16, l);
        for (int i = 0; i < n; i++)
        {
            int lang = (int)((long)l[i] & 0xFFFF);
            if (lang == 0x0409) us = l[i];
            if (lang == 0x0419) ru = l[i];
        }
        if (us == IntPtr.Zero || ru == IntPtr.Zero) throw new Exception("US and Russian layouts must be installed (key lookup only)");
        IntPtr desk = OpenDesktop(desktop, 0, false, 0x0040 /* DESKTOP_ENUMERATE */ | 0x0001);
        var sw = System.Diagnostics.Stopwatch.StartNew();
        long pos = 0; int done = 0;
        try
        {
            while (sw.ElapsedMilliseconds < timeoutMs)
            {
                Thread.Sleep(50);
                if (desk == IntPtr.Zero) desk = OpenDesktop(desktop, 0, false, 0x0041);
                if (hwnd == IntPtr.Zero && desk != IntPtr.Zero)
                    EnumDesktopWindows(desk, (h, p) => { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "SDL_app") { hwnd = h; return false; } return true; }, IntPtr.Zero);
                if (!File.Exists(logPath)) continue;
                string text;
                try
                {
                    using (var fs = new FileStream(logPath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                    {
                        if (fs.Length <= pos) continue;
                        fs.Seek(pos, SeekOrigin.Begin);
                        var buf = new byte[fs.Length - pos];
                        int r = fs.Read(buf, 0, buf.Length);
                        int last = Array.LastIndexOf(buf, (byte)10, r - 1);
                        if (last < 0) continue;
                        text = Encoding.ASCII.GetString(buf, 0, last + 1);
                        pos += last + 1;
                    }
                }
                catch (IOException) { continue; }
                foreach (var line in text.Split('\n'))
                {
                    if (line.StartsWith("OMTEST done")) return done;
                    if (!line.StartsWith("OMTYPE ")) continue;
                    int k; if (!int.TryParse(line.Substring(7).Trim(), out k) || k < 0 || k >= steps.Length) continue;
                    if (hwnd == IntPtr.Zero) { Log.Add("step " + k + ": no window"); continue; }
                    Step(steps[k]); done++;
                    Log.Add("step " + k + " posted");
                }
            }
        }
        finally { if (desk != IntPtr.Zero) CloseDesktop(desk); }
        return done;
    }
}
