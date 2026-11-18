using Windows.UI.Input.Inking;

namespace InkPages;

/// <summary>A change of pages that can be undone and redone. Recorded after it was made.</summary>
public interface IPageEdit
{
    /// <summary>Whether the edit changes <paramref name="page"/> (its history goes with the page).</summary>
    bool Touches(InkPage page);

    bool IsEmpty { get; }

    void Undo();

    void Redo();
}

/// <summary>
/// Strokes and pictures removed and added (by the InkPresenter or a tool), so <see cref="Undo"/> reverses it and
/// <see cref="Redo"/> repeats it.
/// </summary>
public sealed class PageEdit(InkPage page) : IPageEdit
{
    public InkPage Page { get; } = page;
    public List<InkStroke> RemovedStrokes { get; private set; } = new();
    public List<InkStroke> AddedStrokes { get; private set; } = new();
    public List<object> RemovedPictures { get; } = new();
    public List<object> AddedPictures { get; } = new();
    private List<object> removals = new(); // the page's tokens of removed pictures, in order

    /// <summary>Removes pictures from the page as part of this edit (undo puts them back at their place).</summary>
    public void RemovePictures(IReadOnlyCollection<object> pictures)
    {
        if (pictures.Count == 0)
        {
            return;
        }
        removals.Add(Page.RemovePictures(pictures));
        RemovedPictures.AddRange(pictures);
    }

    public bool IsEmpty => RemovedStrokes.Count + AddedStrokes.Count + RemovedPictures.Count + AddedPictures.Count == 0;

    public bool Touches(InkPage page) => page == Page;

    public void Undo()
    {
        RemoveStrokes(Page, AddedStrokes);
        RemovedStrokes = RestoreStrokes(Page, RemovedStrokes);
        if (AddedPictures.Count > 0)
        {
            Page.RemovePictures(AddedPictures);
        }
        for (int i = removals.Count - 1; i >= 0; i--)
        {
            Page.RestorePictures(removals[i]);
        }
    }

    public void Redo()
    {
        RemoveStrokes(Page, RemovedStrokes);
        AddedStrokes = RestoreStrokes(Page, AddedStrokes);
        removals = RemovedPictures.Count > 0 ? [Page.RemovePictures(RemovedPictures)] : [];
        if (AddedPictures.Count > 0)
        {
            Page.AddPictures(AddedPictures);
        }
    }

    /// <summary>
    /// Removes a selection's content from its page as one edit (not recorded yet): what of it is still there – the
    /// eraser may have taken a selected stroke, the app a picture.
    /// </summary>
    public static PageEdit RemoveSelected(Selection selection)
    {
        var page = selection.Page;
        var edit = new PageEdit(page);
        edit.RemovedStrokes.AddRange(SelectOnly(page, selection.Strokes));
        page.Ink.StrokeContainer.DeleteSelected();
        var onPage = page.Pictures.ToHashSet(ReferenceEqualityComparer.Instance);
        edit.RemovePictures(selection.Pictures.Where(onPage.Contains).ToList());
        return edit;
    }

    /// <summary>Removes strokes (or the copies undo/redo put in their place) from the page's ink.</summary>
    public static void RemoveStrokes(InkPage page, IReadOnlyCollection<InkStroke> strokes)
    {
        if (strokes.Count == 0)
        {
            return;
        }
        SelectOnly(page, strokes);
        page.Ink.StrokeContainer.DeleteSelected();
    }

    /// <summary>Marks exactly these strokes (as they are on the page now) as selected; returns them.</summary>
    public static List<InkStroke> SelectOnly(InkPage page, IEnumerable<InkStroke> strokes)
    {
        var onPage = page.Ink.StrokeContainer.GetStrokes(); // read once: deselect and find
        foreach (var s in onPage)
        {
            s.Selected = false;
        }
        var current = page.CurrentStrokes(strokes, onPage).OfType<InkStroke>().ToList();
        current.ForEach(s => s.Selected = true);
        return current;
    }

    /// <summary>Puts removed strokes back: a removed stroke cannot be added again, its copy takes its place (also in the app's data: <see cref="InkPage.StrokeReplaced"/>).</summary>
    public static List<InkStroke> RestoreStrokes(InkPage page, List<InkStroke> strokes)
    {
        var copies = new List<InkStroke>(strokes.Count);
        foreach (var s in strokes)
        {
            var copy = s.Clone();
            page.Ink.StrokeContainer.AddStroke(copy);
            page.StrokeReplaced(s.Id, copy.Id);
            copies.Add(copy);
        }
        return copies;
    }
}

/// <summary>
/// Strokes and pictures moved/scaled (a selection dragged or resized): undo and redo put back the exact placement
/// before and after (no inverse transform: the app's data gets back exactly its numbers, <see cref="InkPage.RestorePlacement"/>).
/// </summary>
public sealed class TransformEdit(InkPage page, object before, object after) : IPageEdit
{
    public InkPage Page { get; } = page;


    public bool IsEmpty => false; // made only for a transform other than identity

    public bool Touches(InkPage page) => page == Page;

    public void Undo() => Page.RestorePlacement(before);

    public void Redo() => Page.RestorePlacement(after);
}

/// <summary>Edits of several pages as one step (content moved to another page: taken from one, added to the other).</summary>
public sealed class CombinedEdit(IReadOnlyList<IPageEdit> edits) : IPageEdit
{
    public bool IsEmpty => edits.All(e => e.IsEmpty);

    public bool Touches(InkPage page) => edits.Any(e => e.Touches(page));

    public void Undo()
    {
        for (int i = edits.Count - 1; i >= 0; i--)
        {
            edits[i].Undo();
        }
    }

    public void Redo()
    {
        foreach (var edit in edits)
        {
            edit.Redo();
        }
    }
}

/// <summary>
/// Undo/redo of page edits (the WinUI InkCanvas has no undo of its own). Every change of the pages' content goes through
/// it, so it also tells an app whether there is something to save: <see cref="ContentChanged"/> and the save point
/// (<see cref="MarkSaved"/>, <see cref="IsAtSavePoint"/>).
/// </summary>
public sealed class UndoStack
{
    private readonly Stack<IPageEdit> undo = new();
    private readonly Stack<IPageEdit> redo = new();
    private readonly SavePoint savePoint = new();

    /// <summary>The history changed (also when it was cleared or a page's history forgotten): enable undo/redo.</summary>
    public event EventHandler? Changed;

    /// <summary>The pages' content changed: an edit was recorded, undone or redone (not when the history is cleared).</summary>
    public event EventHandler? ContentChanged;

    /// <summary>
    /// The same as <see cref="ContentChanged"/>, with the edit (recorded, undone or redone): which pages it changed is
    /// <see cref="IPageEdit.Touches"/> (e.g. to draw only their thumbnails again).
    /// </summary>
    public event EventHandler<IPageEdit>? ContentEdited;

    /// <summary>
    /// Whether the content is as it was at <see cref="MarkSaved"/> (or at the last <see cref="Clear"/>): false after an
    /// edit, true again after undoing back to it.
    /// </summary>
    public bool IsAtSavePoint => savePoint.IsAt(Top);

    /// <summary>The content as it is now was saved (or loaded).</summary>
    public void MarkSaved() => savePoint.Mark(Top);

    private IPageEdit? Top => undo.TryPeek(out var top) ? top : null;

    public bool CanUndo => undo.Count > 0;
    public bool CanRedo => redo.Count > 0;

    /// <summary>Records an edit that has already been made.</summary>
    public void Push(IPageEdit edit)
    {
        if (edit.IsEmpty)
        {
            return;
        }
        undo.Push(edit);
        redo.Clear();
        InkLog.Write($"undo: recorded {edit.GetType().Name} ({undo.Count} steps)");
        Changed?.Invoke(this, EventArgs.Empty);
        ContentChanged?.Invoke(this, EventArgs.Empty);
        ContentEdited?.Invoke(this, edit);
    }

    /// <summary>No history (other pages are shown): the content as it is now counts as saved.</summary>
    public void Clear()
    {
        undo.Clear();
        redo.Clear();
        savePoint.Mark(null);
        Changed?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Whether an edit to undo or redo changes <paramref name="page"/> (it needs the page's ink).</summary>
    public bool Touches(InkPage page) => undo.Any(e => e.Touches(page)) || redo.Any(e => e.Touches(page));

    /// <summary>Forgets the history of a deleted page.</summary>
    public void Forget(InkPage page)
    {
        Filter(undo, page);
        Filter(redo, page);
        if (savePoint.Saved is IPageEdit saved && !undo.Contains(saved))
        {
            savePoint.Lose(); // that state cannot come back by undo any more
        }
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Undo()
    {
        if (undo.TryPop(out var edit))
        {
            InkLog.Write($"undo: {edit.GetType().Name}");
            edit.Undo();
            redo.Push(edit);
            Changed?.Invoke(this, EventArgs.Empty);
            ContentChanged?.Invoke(this, EventArgs.Empty);
            ContentEdited?.Invoke(this, edit);
        }
    }

    public void Redo()
    {
        if (redo.TryPop(out var edit))
        {
            InkLog.Write($"redo: {edit.GetType().Name}");
            edit.Redo();
            undo.Push(edit);
            Changed?.Invoke(this, EventArgs.Empty);
            ContentChanged?.Invoke(this, EventArgs.Empty);
            ContentEdited?.Invoke(this, edit);
        }
    }

    private static void Filter(Stack<IPageEdit> stack, InkPage page)
    {
        var kept = stack.Where(e => !e.Touches(page)).Reverse().ToList();
        stack.Clear();
        foreach (var e in kept)
        {
            stack.Push(e);
        }
    }
}
