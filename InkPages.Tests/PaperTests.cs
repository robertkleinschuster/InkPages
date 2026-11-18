using InkPages;
using Xunit;

namespace InkPages.Tests;

public class PaperTests
{
    [Fact]
    public void GraphPaperHasLinesBothWaysIncludingTheEdges()
    {
        var lines = new Paper(PaperKind.Graph, Spacing: 24).Lines(48, 72);
        var vertical = lines.Where(l => l.From.X == l.To.X).Select(l => l.From.X).ToList();
        var horizontal = lines.Where(l => l.From.Y == l.To.Y).Select(l => l.From.Y).ToList();
        Assert.Equal([0, 24, 48], vertical);
        Assert.Equal([0, 24, 48, 72], horizontal);
    }

    [Fact]
    public void LinedPaperHasHorizontalLinesOnly()
    {
        var lines = new Paper(PaperKind.Lined, Spacing: 30).Lines(100, 100);
        Assert.All(lines, l => Assert.Equal(l.From.Y, l.To.Y));
        Assert.Equal(4, lines.Count); // 0, 30, 60, 90
    }

    [Fact]
    public void PlainPaperAndOddSizesHaveNoLines()
    {
        Assert.Empty(new Paper(PaperKind.Plain).Lines(100, 100));
        Assert.Empty(new Paper(PaperKind.Graph, Spacing: 0).Lines(100, 100));
        Assert.Empty(new Paper(PaperKind.Graph).Lines(0, 100));
        Assert.Equal(Paper.DefaultLineColor, new Paper(PaperKind.Graph).Color);
    }
}
