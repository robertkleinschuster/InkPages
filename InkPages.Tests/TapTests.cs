using InkPages;
using Xunit;

namespace InkPages.Tests;

/// <summary>A press on the selection: a tap only when short and still; any other drag moves it exactly.</summary>
public class TapTests
{
    [Fact]
    public void AShortStillPressIsATap()
    {
        Assert.True(Tap.IsTap(0, TimeSpan.FromMilliseconds(120)));
        Assert.True(Tap.IsTap(2.5, TimeSpan.FromMilliseconds(250)));
    }

    [Fact]
    public void ASlowSmallDragIsAMoveNotATap()
    {
        // Improving a sketch: half a unit, slowly – must move, not clear the selection.
        Assert.False(Tap.IsTap(0.5, TimeSpan.FromMilliseconds(600)));
        Assert.False(Tap.IsTap(2.9, TimeSpan.FromMilliseconds(300)));
    }

    [Fact]
    public void AFarDragIsNeverATap()
    {
        Assert.False(Tap.IsTap(3, TimeSpan.FromMilliseconds(50)));
        Assert.False(Tap.IsTap(40, TimeSpan.FromMilliseconds(100)));
    }
}
