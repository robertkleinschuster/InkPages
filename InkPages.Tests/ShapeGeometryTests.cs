using Windows.Foundation;
using Xunit;

namespace InkPages.Tests;

public class ShapeGeometryTests
{
    [Fact]
    public void RectangleIsClosedThroughAllCorners()
    {
        var outline = ShapeGeometry.Outline(ShapeKind.Rectangle, new Point(10, 20), new Point(50, 40), 1.41);
        Assert.Equal([new(10, 20), new(50, 20), new(50, 40), new(10, 40), new(10, 20)], outline);
    }

    [Fact]
    public void ArrowHeadSitsAtTheEnd()
    {
        var outline = ShapeGeometry.Outline(ShapeKind.Arrow, new Point(0, 0), new Point(100, 0), 1.41);
        Assert.Equal(5, outline.Count);
        Assert.Equal(new Point(100, 0), outline[1]);
        Assert.Equal(new Point(100, 0), outline[3]);
        Assert.True(outline[2].X < 100 && outline[4].X < 100);
        Assert.Equal(-outline[2].Y, outline[4].Y, 6); // one side above, one below the shaft
        Assert.NotEqual(0, outline[2].Y);
    }

    [Fact]
    public void EllipseFitsTheDraggedRectangle()
    {
        var outline = ShapeGeometry.Outline(ShapeKind.Ellipse, new Point(0, 0), new Point(40, 20), 1.41);
        Assert.Equal(outline[0], outline[^1]);
        Assert.Equal(40, outline.Max(p => p.X), 6);
        Assert.Equal(0, outline.Min(p => p.X), 6);
        Assert.Equal(20, outline.Max(p => p.Y), 1);
    }

    [Fact]
    public void ShapeStrokeHasEvenWidthWithoutSmoothing()
    {
        var pen = new Windows.UI.Input.Inking.InkDrawingAttributes { Size = new Size(2.26, 2.26) };
        var stroke = ShapeGeometry.CreateStroke(ShapeGeometry.Outline(ShapeKind.Line, new(0, 0), new(30, 0), 2.26), pen);
        Assert.True(stroke.DrawingAttributes.IgnorePressure);
        Assert.False(stroke.DrawingAttributes.FitToCurve);
        Assert.Equal(2, stroke.GetInkPoints().Count);
    }
}
