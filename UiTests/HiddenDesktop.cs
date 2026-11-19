// Runs a process on a separate Win32 desktop of the interactive window station, so its windows never show on the
// user's desktop (CreateDesktop + STARTUPINFO.lpDesktop, documented:
// https://learn.microsoft.com/windows/win32/winstation/desktops
// https://learn.microsoft.com/windows/win32/procthread/process-creation-flags (lpDesktop: STARTUPINFO)).
// Used by Run-SandboxTest.ps1 for `wsb connect`: the Windows Sandbox client window opens there instead.
// C# 5 (Windows PowerShell 5.1 Add-Type).
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

public sealed class HiddenDesktop : IDisposable
{
    const uint DESKTOP_ALL = 0x01FF | 0x000F0000; // DESKTOP_* rights + STANDARD_RIGHTS_REQUIRED

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

    delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr devmode, uint flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool EnumDesktopWindows(IntPtr desktop, EnumWindowsProc proc, IntPtr lParam);
    [DllImport("user32.dll", SetLastError = true)] static extern bool EnumWindows(EnumWindowsProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr hwnd, out int pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, StringBuilder s, int n);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
        IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr h);

    readonly IntPtr handle;
    public readonly string Name;

    public HiddenDesktop(string name)
    {
        Name = name;
        handle = CreateDesktopW(name, IntPtr.Zero, IntPtr.Zero, 0, DESKTOP_ALL, IntPtr.Zero);
        if (handle == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateDesktop " + name);
    }

    // Starts commandLine on this desktop; returns the process id.
    public int Start(string commandLine)
    {
        var si = new STARTUPINFO();
        si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
        si.lpDesktop = @"WinSta0\" + Name;
        PROCESS_INFORMATION pi;
        if (!CreateProcessW(null, new StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, null,
                ref si, out pi))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateProcess on " + Name + ": " + commandLine);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return pi.dwProcessId;
    }

    // Visible top-level windows of processes named processName (without .exe), on this desktop.
    public string[] Windows(string processName) { return Collect(processName, true); }

    // The same on the calling thread's desktop (the user's desktop when called from the runner).
    public static string[] WindowsOnCurrentDesktop(string processName) { return Collect(processName, false, IntPtr.Zero); }

    string[] Collect(string processName, bool own) { return Collect(processName, own, handle); }

    static string[] Collect(string processName, bool own, IntPtr desktop)
    {
        var found = new List<string>();
        EnumWindowsProc proc = (hwnd, l) =>
        {
            if (!IsWindowVisible(hwnd)) return true;
            int pid;
            GetWindowThreadProcessId(hwnd, out pid);
            string name;
            try { name = Process.GetProcessById(pid).ProcessName; }
            catch (ArgumentException) { return true; } // the process ended meanwhile
            if (!string.Equals(name, processName, StringComparison.OrdinalIgnoreCase)) return true;
            var title = new StringBuilder(256);
            GetWindowTextW(hwnd, title, title.Capacity);
            found.Add(string.Format("0x{0:X} pid {1} '{2}'", hwnd.ToInt64(), pid, title));
            return true;
        };
        bool ok = own ? EnumDesktopWindows(desktop, proc, IntPtr.Zero) : EnumWindows(proc, IntPtr.Zero);
        int error = Marshal.GetLastWin32Error();
        GC.KeepAlive(proc);
        // EnumDesktopWindows returns false with error 0 on a desktop without windows.
        if (!ok && error != 0) throw new Win32Exception(error, "EnumWindows");
        return found.ToArray();
    }

    public void Dispose()
    {
        if (!CloseDesktop(handle)) throw new Win32Exception(Marshal.GetLastWin32Error(), "CloseDesktop " + Name);
    }
}
