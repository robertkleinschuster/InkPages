using InkPages;
using Xunit;

namespace InkPages.Tests;

public class SavePointTests
{
    [Fact]
    public void TheHistoryIsAtItsSavePointUntilAnEditAndAgainAfterUndoingBackToIt()
    {
        var point = new SavePoint();
        object a = new(), b = new();
        Assert.True(point.IsAt(null)); // a new history: nothing to save
        Assert.False(point.IsAt(a)); // an edit
        point.Mark(a); // saved
        Assert.True(point.IsAt(a));
        Assert.False(point.IsAt(b)); // another edit on top
        Assert.True(point.IsAt(a)); // undone back to the saved step
        Assert.False(point.IsAt(null)); // undone further than saved
    }

    [Fact]
    public void ALostSavePointIsReachableOnlyByMarkingAgain()
    {
        var point = new SavePoint();
        object a = new();
        point.Mark(a);
        point.Lose(); // its page was removed
        Assert.False(point.IsAt(a));
        Assert.False(point.IsAt(null));
        point.Mark(null);
        Assert.True(point.IsAt(null));
    }

    [Theory]
    [InlineData(0.0)]
    [InlineData(0.5)]
    [InlineData(1.0)]
    public void PressureForIsTheInverseOfWidthFactor(double pressure) =>
        Assert.Equal(pressure, InkStrokes.PressureFor(InkStrokes.WidthFactor(pressure)), 10);
}
