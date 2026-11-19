using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

// Screenshots for the cursor test (C# 5 for Windows PowerShell's Add-Type). CopyFromScreen leaves the mouse cursor
// out (the app itself writes cursor-<name>.bmp for every cursor it sees); what it does capture is the composed screen,
// e.g. Windows' pen hover feedback.
public static class CursorShot
{
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)]
    struct CURSORINFO { public int cbSize; public int flags; public IntPtr hCursor; public POINT pt; }

    [DllImport("user32")] static extern bool GetCursorInfo(ref CURSORINFO ci);
    [DllImport("user32")] static extern int GetSystemMetrics(int i);

    /// <summary>Cursor state as this (other) process sees it: flags (1 showing, 2 suppressed, 0 hidden), handle, position.</summary>
    public static string State()
    {
        var ci = new CURSORINFO(); ci.cbSize = Marshal.SizeOf(typeof(CURSORINFO));
        if (!GetCursorInfo(ref ci)) return "GetCursorInfo failed " + Marshal.GetLastWin32Error();
        return "flags=" + ci.flags + " h=0x" + ci.hCursor.ToString("X") + " at " + ci.pt.X + "," + ci.pt.Y;
    }

    [DllImport("user32")] static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32")] static extern uint GetDpiForWindow(IntPtr h);

    /// <summary>Shot with the zoom around client DIPs (x, y) of the window.</summary>
    public static void ShotClient(string file, IntPtr hwnd, double x, double y)
    {
        var p = new POINT();
        ClientToScreen(hwnd, ref p);
        double s = GetDpiForWindow(hwnd) / 96.0;
        Shot(file, p.X + (int)(x * s), p.Y + (int)(y * s));
    }

    /// <summary>Full screen plus a 4x crop around screen pixel (cx, cy) (file + "-zoom.png").</summary>
    public static void Shot(string file, int cx, int cy)
    {
        int w = GetSystemMetrics(0), h = GetSystemMetrics(1);
        using (var bmp = new Bitmap(w, h))
        {
            using (var g = Graphics.FromImage(bmp)) g.CopyFromScreen(0, 0, 0, 0, new Size(w, h));
            bmp.Save(file, ImageFormat.Png);
            int r = 50;
            var src = new Rectangle(Math.Max(0, cx - r), Math.Max(0, cy - r), 2 * r, 2 * r);
            using (var zoom = new Bitmap(8 * r, 8 * r))
            {
                using (var g = Graphics.FromImage(zoom))
                {
                    g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
                    g.PixelOffsetMode = System.Drawing.Drawing2D.PixelOffsetMode.Half;
                    g.DrawImage(bmp, new Rectangle(0, 0, 8 * r, 8 * r), src, GraphicsUnit.Pixel);
                }
                zoom.Save(file.Replace(".png", "-zoom.png"), ImageFormat.Png);
            }
        }
    }
}
