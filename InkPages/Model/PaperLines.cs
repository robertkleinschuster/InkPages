using Windows.Foundation;

namespace InkPages;

/// <summary>What a sheet is printed with below everything: nothing, a grid of squares, or lines.</summary>
public enum PaperKind { Plain, Graph, Lined }

/// <summary>
/// A sheet's paper (<c>InkPage.Paper</c>): its kind, the lines' spacing in page units (DIPs; 24 DIP = 18 pt), their color
/// and thickness. Lines start at the sheet's edge (0) and include both edges where the spacing meets them.
/// </summary>
public sealed record Paper(PaperKind Kind, double Spacing = 24, Windows.UI.Color? LineColor = null, double Thickness = 0.8)
{
    /// <summary>A light blue-grey that reads as paper ruling on white in either theme.</summary>
    public static readonly Windows.UI.Color DefaultLineColor = Windows.UI.Color.FromArgb(0xFF, 0xD6, 0xE2, 0xF0);

    public Windows.UI.Color Color => LineColor ?? DefaultLineColor;

    /// <summary>The paper's lines on a sheet of this size (start and end points, page units).</summary>
    public IReadOnlyList<(Point From, Point To)> Lines(double width, double height)
    {
        var lines = new List<(Point, Point)>();
        if (Kind == PaperKind.Plain || Spacing <= 0 || width <= 0 || height <= 0)
        {
            return lines;
        }
        if (Kind == PaperKind.Graph)
        {
            for (double x = 0; x <= width + 1e-6; x += Spacing)
            {
                lines.Add((new Point(x, 0), new Point(x, height)));
            }
        }
        for (double y = 0; y <= height + 1e-6; y += Spacing)
        {
            lines.Add((new Point(0, y), new Point(width, y)));
        }
        return lines;
    }
}
