namespace InkPages;

/// <summary>
/// Where a history was saved: the step on top of it then (null: no step). The history is at its save point while the
/// same step is on top again – after undoing back to it too; once that step is gone for good (forgotten with its page),
/// the save point cannot be reached any more.
/// </summary>
public sealed class SavePoint
{
    private object? saved;
    private bool reachable = true;

    /// <summary>The history was saved (or loaded, or cleared) with <paramref name="top"/> on top.</summary>
    public void Mark(object? top)
    {
        saved = top;
        reachable = true;
    }

    /// <summary>Whether the history with <paramref name="top"/> on top is as it was saved.</summary>
    public bool IsAt(object? top) => reachable && ReferenceEquals(saved, top);

    /// <summary>The saved step left the history (its page was removed): only a new save point is reachable again.</summary>
    public void Lose() => reachable = false;

    /// <summary>The step saved last (to check whether it is still in the history).</summary>
    public object? Saved => saved;
}
