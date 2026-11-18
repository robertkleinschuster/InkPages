using System.Numerics;
using Windows.UI.Input.Inking;

namespace InkPages;

/// <summary>How Windows ink draws a stroke, and strokes made from samples.</summary>
public static class InkStrokes
{
    // InkStrokeBuilder is not agile (MarshalingType.None): one per thread, so strokes can be built off the UI thread
    // (an app reading a document's pages on the thread pool).
    [ThreadStatic]
    private static InkStrokeBuilder? builder;

    private static InkStrokeBuilder Builder => builder ??= new InkStrokeBuilder();

    /// <summary>
    /// The width Windows ink draws a stroke with at a pressure: a stroke of size s at pressure p is about
    /// s * (0.25 + 1.5 p) wide (measured), i.e. exactly s at p = 0.5; without pressure its size.
    /// </summary>
    public static double DrawnWidth(InkDrawingAttributes attributes, float pressure) =>
        DrawnWidth(attributes.Size.Width, attributes.IgnorePressure, pressure);

    /// <summary><see cref="DrawnWidth(InkDrawingAttributes, float)"/> with the attributes' size and pressure mode read once.</summary>
    public static double DrawnWidth(double size, bool ignorePressure, float pressure) =>
        ignorePressure ? size : size * WidthFactor(pressure);

    /// <summary>The factor of <see cref="DrawnWidth"/> at a pressure.</summary>
    public static double WidthFactor(double pressure) => 0.25 + 1.5 * pressure;

    /// <summary>The pressure at which a stroke is drawn <paramref name="widthFactor"/> times its size (the inverse of <see cref="WidthFactor"/>).</summary>
    public static double PressureFor(double widthFactor) => (widthFactor - 0.25) / 1.5;

    /// <summary>
    /// The factor at full pressure (1.75). Xournal++'s pen width is the width at full pressure (a segment is width ×
    /// pressure), Windows ink's size the width at half pressure: a pen of Xournal width w draws with size w / this, so it
    /// is never wider than w.
    /// </summary>
    public static double FullPressureFactor => WidthFactor(1);

    /// <summary>The pen's width as Xournal means it: the width at full pressure (a pen without pressure: its size).</summary>
    public static double ToolWidth(InkDrawingAttributes attributes) =>
        attributes.IgnorePressure ? attributes.Size.Width : attributes.Size.Width * FullPressureFactor;

    /// <summary>A stroke through these samples with these attributes (what the partial eraser leaves of a stroke).</summary>
    public static InkStroke FromSamples(IEnumerable<InkSample> samples, InkDrawingAttributes attributes) =>
        FromPoints(samples.Select(s => new InkPoint(s.Position, s.Pressure)), attributes);

    /// <summary>A stroke through these points with these attributes; on any thread.</summary>
    public static InkStroke FromPoints(IEnumerable<InkPoint> points, InkDrawingAttributes attributes)
    {
        Builder.SetDefaultDrawingAttributes(attributes);
        var stroke = Builder.CreateStrokeFromInkPoints(points, Matrix3x2.Identity);
        stroke.DrawingAttributes = attributes;
        return stroke;
    }
}
