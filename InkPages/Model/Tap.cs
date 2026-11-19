namespace InkPages;

/// <summary>
/// When a press on the selection is a tap (it clears the selection, or selects the picture under the pen) and when it is
/// a drag that moves or scales the selection. A drag is applied exactly however small it is – improving a sketch needs
/// moves of a fraction of a line width (user, 2026-11-19: a 3-unit threshold swallowed such moves). Only a short press
/// that stayed (nearly) where it began is a tap.
/// </summary>
internal static class Tap
{
    /// <summary>The farthest a tap may wander (page units, |dx| + |dy|): a pen's jitter while it touches briefly.</summary>
    public const double MaxDistance = 3;

    /// <summary>The longest a tap lasts: a slower press is a deliberate (maybe tiny) drag.</summary>
    public static readonly TimeSpan MaxDuration = TimeSpan.FromMilliseconds(300);

    /// <param name="farthest">The farthest the pen was from where it pressed (page units, |dx| + |dy|).</param>
    /// <param name="duration">From press to release.</param>
    public static bool IsTap(double farthest, TimeSpan duration) => farthest < MaxDistance && duration < MaxDuration;
}
