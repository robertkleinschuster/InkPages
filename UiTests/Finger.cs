using System;
using System.Runtime.InteropServices;
using System.Threading;

// One synthetic touch contact driven frame by frame (client DIPs), so a test can interleave it with pen frames
// (Inj.PenFrame) - finger panning while the pen writes. C# 5 for Windows PowerShell's Add-Type.
public static class Finger
{
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Explicit, Size = 96)]
    struct POINTER_INFO
    {
        [FieldOffset(0)] public int pointerType;
        [FieldOffset(4)] public uint pointerId;
        [FieldOffset(12)] public uint pointerFlags;
        [FieldOffset(32)] public POINT ptPixelLocation;
    }

    [StructLayout(LayoutKind.Explicit, Size = 152)]
    struct TOUCH_TYPE_INFO
    {
        [FieldOffset(0)] public int type;
        [FieldOffset(8)] public POINTER_INFO info;
        [FieldOffset(104)] public uint touchFlags;
        [FieldOffset(108)] public uint touchMask;
        [FieldOffset(112)] public RECT rcContact;
        [FieldOffset(144)] public uint orientation;
        [FieldOffset(148)] public uint pressure;
    }

    [DllImport("user32", SetLastError = true)] static extern IntPtr CreateSyntheticPointerDevice(int type, uint maxCount, int mode);
    [DllImport("user32", SetLastError = true)] static extern bool InjectSyntheticPointerInput(IntPtr dev, TOUCH_TYPE_INFO[] info, uint count);
    [DllImport("user32")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32")] static extern uint GetDpiForWindow(IntPtr h);

    const uint INRANGE = 0x2, INCONTACT = 0x4, PRIMARY = 0x2000, CONFIDENCE = 0x4000, DOWN = 0x10000, UPDATE = 0x20000, UP = 0x40000;
    static IntPtr dev;
    static IntPtr window;

    public static void Init(IntPtr hwnd)
    {
        window = hwnd;
        if (dev == IntPtr.Zero) dev = CreateSyntheticPointerDevice(2, 1, 1);
    }

    static void Frame(double x, double y, uint flags)
    {
        var p = new POINT();
        ClientToScreen(window, ref p);
        double s = GetDpiForWindow(window) / 96.0;
        p.X += (int)Math.Round(x * s); p.Y += (int)Math.Round(y * s);
        var t = new TOUCH_TYPE_INFO();
        t.type = 2; t.info.pointerType = 2; t.info.pointerId = 0; t.info.pointerFlags = flags | PRIMARY | CONFIDENCE;
        t.info.ptPixelLocation = p;
        t.touchMask = 0x7;
        t.rcContact.Left = p.X - 4; t.rcContact.Right = p.X + 4; t.rcContact.Top = p.Y - 4; t.rcContact.Bottom = p.Y + 4;
        t.orientation = 90; t.pressure = 32000;
        if (!InjectSyntheticPointerInput(dev, new[] { t }, 1))
            throw new Exception("touch injection failed: " + Marshal.GetLastWin32Error());
    }

    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; public uint pad1, pad2; }
    [StructLayout(LayoutKind.Explicit)] struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public int type; public INPUTUNION u; }
    [DllImport("user32")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
    [DllImport("user32")] static extern bool SetCursorPos(int x, int y);

    static void Send(INPUT i) { SendInput(1, new[] { i }, Marshal.SizeOf(typeof(INPUT))); Thread.Sleep(20); }
    static void CtrlKey(bool up) { var i = new INPUT(); i.type = 1; i.u.ki.wVk = 0x11; i.u.ki.dwFlags = up ? 2u : 0u; Send(i); }

    /// <summary>Mouse wheel notches (120 each, positive = away from the user) at client DIPs, with Ctrl held if asked.</summary>
    public static void Wheel(double x, double y, int notches, bool ctrl)
    {
        var p = new POINT();
        ClientToScreen(window, ref p);
        double s = GetDpiForWindow(window) / 96.0;
        SetCursorPos(p.X + (int)Math.Round(x * s), p.Y + (int)Math.Round(y * s));
        Thread.Sleep(50);
        if (ctrl) CtrlKey(false);
        for (int k = 0; k < Math.Abs(notches); k++)
        {
            var i = new INPUT(); i.type = 0; i.u.mi.mouseData = (uint)(notches > 0 ? 120 : -120); i.u.mi.dwFlags = 0x0800; // WHEEL
            Send(i);
            Thread.Sleep(60);
        }
        if (ctrl) CtrlKey(true);
    }

    public static void Down(double x, double y) { Frame(x, y, INRANGE | INCONTACT | DOWN); }
    public static void Move(double x, double y) { Frame(x, y, INRANGE | INCONTACT | UPDATE); }
    public static void Up(double x, double y) { Frame(x, y, UP); }
}
