using System.Text.Json;
using Xunit;

namespace InkPages.Tests;

public class ToolChoicesTests
{
    [Fact]
    public void EmptyPresetsMeanTheDefaults()
    {
        var choices = new ToolChoices();
        Assert.Equal(ToolChoices.DefaultPens, choices.PensOrDefault);
        Assert.Equal(ToolChoices.DefaultHighlighters, choices.HighlightersOrDefault);
        Assert.Equal([0, 1, 2], choices.PensOrDefault.Select(p => p.ColorIndex));
    }

    [Fact]
    public void SavedPresetsWin()
    {
        var choices = new ToolChoices { Pens = [new(4, 0.85)] };
        Assert.Equal([new PenPreset(4, 0.85)], choices.PensOrDefault);
        Assert.Equal(ToolChoices.DefaultHighlighters, choices.HighlightersOrDefault);
    }

    [Fact]
    public void RoundTripsThroughJson()
    {
        var choices = new ToolChoices
        {
            Pens = [new(1, 2.83), new(9, 0.42)],
            Highlighters = [new(2, 12)],
            Shape = ShapeKind.Ellipse,
            SelectMode = SelectMode.Rectangle,
            EraserRadius = 7.5,
            WholeStrokeErasing = true,
            RecognizeShapes = true,
        };
        var back = JsonSerializer.Deserialize<ToolChoices>(JsonSerializer.Serialize(choices))!;
        Assert.Equal(choices.Pens, back.Pens);
        Assert.Equal(choices.Highlighters, back.Highlighters);
        Assert.Equal(ShapeKind.Ellipse, back.Shape);
        Assert.Equal(SelectMode.Rectangle, back.SelectMode);
        Assert.Equal(7.5, back.EraserRadius);
        Assert.True(back.WholeStrokeErasing);
        Assert.True(back.RecognizeShapes);
    }

    [Fact]
    public void MissingFieldsKeepTheirDefaults()
    {
        var back = JsonSerializer.Deserialize<ToolChoices>("{}")!;
        Assert.Empty(back.Pens);
        Assert.Equal(4, back.EraserRadius);
        Assert.False(back.WholeStrokeErasing);
    }
}
