using System.Numerics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.System;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace InkPages;

/// <summary>The commands of <see cref="InkCommands"/> (for <see cref="InkCommands.Refused"/>).</summary>
public enum InkCommand { Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, SelectAll, ClearPage }

/// <summary>
/// The edit commands every app on the library needs, on Windows ink alone: undo/redo, delete, duplicate, cut/copy/paste
/// through Windows ink's clipboard, select all, clear a page; each one undo step, and what is added ends up selected with
/// the select tool active. An app with its own clipboard format (pictures, file elements) keeps its own copy, paste and
/// duplicate and uses the rest – <see cref="Refuses"/>, <see cref="Delete"/>, <see cref="SelectAll"/>,
/// <see cref="PasteInk(InkPage, Point, double)"/>, <see cref="SelectAdded"/>.
/// <para>
/// The one rule while the selection is being moved or scaled: a command that reads or changes content is not run, and
/// <see cref="Refused"/> tells the app (it says "let go first"). The move is never lost or measured mid-way.
/// </para>
/// </summary>
public sealed class InkCommands
{
    private readonly InkWorkspace ink;
    private readonly PenToolbar bar;
    private readonly ScrollView scroller;

    internal InkCommands(InkWorkspace ink, PenToolbar bar, ScrollView scroller)
    {
        this.ink = ink;
        this.bar = bar;
        this.scroller = scroller;
    }

    /// <summary>A command was not run because the selection is being moved or scaled.</summary>
    public event EventHandler<InkCommand>? Refused;

    /// <summary>Where duplicates go (page points; default one 5 mm grid step right and down).</summary>
    public Vector2 DuplicateOffset { get; set; } = new(14.17f, 14.17f);

    /// <summary>The distance from the page's edges of pasted ink.</summary>
    public double PasteMargin { get; set; } = 14.17;

    private SelectionTool Selection => ink.Selection;

    /// <summary>
    /// True while the selection is being moved or scaled (<see cref="Refused"/> raised), and while the ink is hidden
    /// (<see cref="InkWorkspace.InkHidden"/>: nothing changes ink the user cannot see; nothing to say then).
    /// </summary>
    public bool Refuses(InkCommand command)
    {
        if (ink.InkHidden)
        {
            return true;
        }
        if (!Selection.IsDragging)
        {
            return false;
        }
        Refused?.Invoke(this, command);
        return true;
    }

    public bool Undo() => !Refuses(InkCommand.Undo) && ink.UndoStep();

    public bool Redo() => !Refuses(InkCommand.Redo) && ink.RedoStep();

    /// <summary>Deletes the selected strokes and pictures (undoable). False: nothing selected or nothing left of it.</summary>
    public bool Delete()
    {
        if (Selection.Current is not { HasContent: true } current || Refuses(InkCommand.Delete))
        {
            return false;
        }
        var edit = PageEdit.RemoveSelected(current); // what of it is still on the page
        Selection.Clear();
        ink.Undo.Push(edit); // an empty edit (all of it erased meanwhile) is not recorded
        return !edit.IsEmpty;
    }

    /// <summary>
    /// Copies an app's picture moved by a transform (for <see cref="Duplicate"/>); null: a selection with pictures
    /// duplicates its strokes only.
    /// </summary>
    public Func<object, Matrix3x2, object>? CopyPicture { get; set; }

    /// <summary>Copies of the selection, moved by <see cref="DuplicateOffset"/>, selected (pictures by <see cref="CopyPicture"/>).</summary>
    public bool Duplicate()
    {
        if (Selection.Current is not { HasContent: true } current || Refuses(InkCommand.Duplicate))
        {
            return false;
        }
        var page = current.Page;
        var m = Matrix3x2.CreateTranslation(DuplicateOffset);
        var edit = new PageEdit(page);
        try
        {
            foreach (var stroke in page.CurrentStrokes(current.Strokes).OfType<InkStroke>())
            {
                var copy = stroke.Clone();
                copy.PointTransform = stroke.PointTransform * m;
                page.Ink.StrokeContainer.AddStroke(copy);
                edit.AddedStrokes.Add(copy);
            }
            if (CopyPicture is { } copyPicture && current.Pictures.Count > 0)
            {
                var onPage = page.Pictures.ToHashSet(ReferenceEqualityComparer.Instance);
                edit.AddedPictures.AddRange(current.Pictures.Where(onPage.Contains).Select(p => copyPicture(p, m)));
                page.AddPictures(edit.AddedPictures);
            }
        }
        catch (Exception e)
        {
            InkLog.Write("duplicate (what was added is taken back)", e);
            TakeBack(edit);
            throw;
        }
        if (edit.IsEmpty)
        {
            return false;
        }
        ink.Undo.Push(edit);
        SelectAddedLogged(page, edit.AddedStrokes, edit.AddedPictures, "duplicate");
        return true;
    }

    /// <summary>
    /// Copies the selected strokes to the clipboard as Windows ink (other ink apps paste it too). False: nothing to copy
    /// (no selection, or pictures only – those are the app's).
    /// </summary>
    public bool CopyInk()
    {
        if (Selection.Current is not { } current || Refuses(InkCommand.Copy))
        {
            return false;
        }
        var strokes = PageEdit.SelectOnly(current.Page, current.Strokes);
        try
        {
            if (strokes.Count == 0)
            {
                return false;
            }
            current.Page.Ink.StrokeContainer.CopySelectedToClipboard();
            return true;
        }
        finally
        {
            strokes.ForEach(s => s.Selected = false);
        }
    }

    /// <summary>Copies (Windows ink) and deletes the selection. A selection with pictures is not cut: they would be lost.</summary>
    public bool CutInk()
    {
        if (Selection.Current is not { } current || Refuses(InkCommand.Cut) || current.Pictures.Count > 0)
        {
            return false;
        }
        return CopyInk() && Selection.Current == current && Delete();
    }

    /// <summary>Whether the clipboard holds Windows ink.</summary>
    public bool CanPasteInk(InkPage page) => page.Ink.StrokeContainer.CanPasteFromClipboard();

    /// <summary>Pastes Windows ink onto the page in view, at the top left of what is seen of it.</summary>
    public List<InkStroke>? PasteInk() =>
        ink.CurrentPage is { } page ? PasteInk(page, VisibleTopLeft(page), PasteMargin) : null;

    /// <summary>
    /// Pastes Windows ink from the clipboard at <paramref name="at"/> (page points), as one undo step, selected. Ink
    /// that would reach below the page goes to its top (<paramref name="margin"/>) instead. Null: refused (while the
    /// selection is dragged or the ink is hidden: <see cref="Refused"/> was raised); empty: nothing pasted (no ink in the
    /// clipboard, or it held no strokes). If pasting fails, what was added is taken back and the error thrown.
    /// </summary>
    public List<InkStroke>? PasteInk(InkPage page, Point at, double margin)
    {
        if (Refuses(InkCommand.Paste))
        {
            return null;
        }
        if (!CanPasteInk(page))
        {
            return [];
        }
        var container = page.Ink.StrokeContainer;
        var before = container.GetStrokes().Select(s => s.Id).ToHashSet();
        var edit = new PageEdit(page);
        try
        {
            container.PasteFromClipboard(at);
            edit.AddedStrokes.AddRange(container.GetStrokes().Where(s => !before.Contains(s.Id)));
            if (edit.AddedStrokes.Count == 0)
            {
                InkLog.Error("paste ink: the clipboard's ink added no strokes");
                return [];
            }
            var bounds = Rect.Empty;
            edit.AddedStrokes.ForEach(s => bounds.Union(s.BoundingRect));
            // Its size is only known now: ink reaching beyond the page goes back onto it (to the top, or left so that its
            // right edge is inside; wider than the page: to the left margin).
            double dx = bounds.Right > page.Width - margin ? Math.Max(margin, page.Width - margin - bounds.Width) - bounds.Left : 0;
            double dy = bounds.Bottom > page.Height ? margin - bounds.Top : 0;
            if (dx != 0 || dy != 0)
            {
                var back = Matrix3x2.CreateTranslation((float)dx, (float)dy);
                edit.AddedStrokes.ForEach(s => s.PointTransform *= back);
            }
            edit.AddedStrokes.ForEach(s => s.Selected = false);
        }
        catch (Exception e)
        {
            InkLog.Write("paste ink (what was added is taken back)", e);
            try
            {
                // Strokes Windows ink added before it failed (found by id).
                var found = container.GetStrokes().Where(s => !before.Contains(s.Id)).ToList();
                edit.AddedStrokes.Clear();
                edit.AddedStrokes.AddRange(found);
            }
            catch (Exception inner)
            {
                InkLog.Write("paste ink: finding the strokes to take back", inner);
            }
            TakeBack(edit);
            throw;
        }
        ink.Undo.Push(edit);
        SelectAddedLogged(page, edit.AddedStrokes, [], "paste ink");
        return edit.AddedStrokes.ToList();
    }

    /// <summary>Selects everything on the page in view (the select tool becomes active).</summary>
    public bool SelectAll() => ink.CurrentPage is { } page && SelectAll(page);

    public bool SelectAll(InkPage page)
    {
        if (Refuses(InkCommand.SelectAll))
        {
            return false;
        }
        var strokes = page.Ink.StrokeContainer.GetStrokes().ToList();
        var pictures = page.Pictures.ToList();
        if (strokes.Count + pictures.Count == 0)
        {
            return false;
        }
        SelectAdded(page, strokes, pictures);
        return true;
    }

    /// <summary>Selects content just added (pasted, duplicated) with the select tool active, ready to be moved.</summary>
    public void SelectAdded(InkPage page, IEnumerable<InkStroke> strokes, IEnumerable<object> pictures)
    {
        bar.Ink.ActiveTool = bar.Select;
        Selection.Select(page, strokes.ToList(), pictures.ToList());
    }

    // Selecting what was added (and recorded) failed: that is not a failed paste or duplicate - logged only, the content stays.
    private void SelectAddedLogged(InkPage page, List<InkStroke> strokes, List<object> pictures, string operation)
    {
        try
        {
            SelectAdded(page, strokes, pictures);
        }
        catch (Exception e)
        {
            InkLog.Write($"{operation}: select the added content", e);
        }
    }
    /// <summary>Removes all strokes and pictures of the page as one undo step. False: the page was empty.</summary>
    public bool ClearPage(InkPage page)
    {
        var strokes = page.Ink.StrokeContainer.GetStrokes().ToList();
        if ((strokes.Count == 0 && !page.Pictures.Any()) || Refuses(InkCommand.ClearPage))
        {
            return false; // an empty page stays as it is (also its selection state)
        }
        Selection.Clear();
        var edit = new PageEdit(page);
        PageEdit.RemoveStrokes(page, strokes);
        edit.RemovedStrokes.AddRange(strokes);
        edit.RemovePictures(page.Pictures.ToList());
        ink.Undo.Push(edit);
        return !edit.IsEmpty;
    }

    /// <summary>
    /// The toolbar's undo, redo and delete buttons run these commands, and with <paramref name="clipboard"/> also cut, copy
    /// and duplicate (Windows ink's clipboard). An app with its own clipboard format passes false and wires those three
    /// itself (checking <see cref="Refuses"/> first).
    /// </summary>
    public void WireToolbar(bool clipboard = true)
    {
        bar.Undo.Click += (_, _) => Undo();
        bar.Redo.Click += (_, _) => Redo();
        bar.DeleteSelection.Click += (_, _) => Delete();
        if (clipboard)
        {
            bar.Cut.Click += (_, _) => { CutInk(); RefreshPaste(); };
            bar.Copy.Click += (_, _) => { CopyInk(); RefreshPaste(); };
            bar.Duplicate.Click += (_, _) => Duplicate();
            // Paste: enabled while the clipboard holds Windows ink (checked when the clipboard changes and after copying)
            bar.Paste.Click += (_, _) => PasteInk();
            bar.ShowPaste();
            // Clipboard.ContentChanged is static: attached while the toolbar is in the tree, so a toolbar that is gone
            // neither stays alive through it nor reads the clipboard on every copy
            EventHandler<object> changed = (_, _) => bar.DispatcherQueue.TryEnqueue(() => RefreshPaste());
            bar.Loaded += (_, _) =>
            {
                Windows.ApplicationModel.DataTransfer.Clipboard.ContentChanged -= changed;
                Windows.ApplicationModel.DataTransfer.Clipboard.ContentChanged += changed;
                RefreshPaste();
            };
            bar.Unloaded += (_, _) => Windows.ApplicationModel.DataTransfer.Clipboard.ContentChanged -= changed;
            if (bar.IsLoaded)
            {
                Windows.ApplicationModel.DataTransfer.Clipboard.ContentChanged += changed;
            }
            RefreshPaste();
        }
    }

    private int pasteRetries;
    private Microsoft.UI.Dispatching.DispatcherQueueTimer? pasteRetry;

    /// <summary>Enables the toolbar's paste button while the clipboard holds Windows ink.</summary>
    public void RefreshPaste()
    {
        try
        {
            bar.Paste.IsEnabled = ink.CurrentPage is { } page && CanPasteInk(page);
            pasteRetries = 0;
        }
        catch (Exception e)
        {
            // The clipboard is busy (clipboard history opens it right after a copy, another app may hold it): the button
            // keeps its state and the check runs again shortly (a few times), then at the next clipboard change, copy
            // or the app's call (e.g. when its window is activated).
            InkLog.Write("paste ink: reading the clipboard", e);
            if (pasteRetries++ < 4)
            {
                pasteRetry ??= bar.DispatcherQueue.CreateTimer();
                pasteRetry.Interval = TimeSpan.FromMilliseconds(150);
                pasteRetry.IsRepeating = false;
                pasteRetry.Tick -= RetryPaste;
                pasteRetry.Tick += RetryPaste;
                pasteRetry.Start();
            }
            else
            {
                pasteRetries = 0;
            }
        }
    }

    private void RetryPaste(Microsoft.UI.Dispatching.DispatcherQueueTimer timer, object args) => RefreshPaste();

    /// <summary>
    /// The usual shortcuts on <paramref name="root"/>: Ctrl+Z/Y undo/redo, Ctrl+X/C/V cut/copy/paste (Windows ink),
    /// Ctrl+D duplicate, Ctrl+A select all, Del delete. Not while a text input has the focus (its own shortcuts apply)
    /// or while <paramref name="active"/> says no (another view is shown). The root's accelerator tooltips are hidden:
    /// such a tooltip is a windowed popup that opens where the pen hovers and breaks its drags.
    /// </summary>
    public void AddShortcuts(UIElement root, Func<bool>? active = null)
    {
        root.KeyboardAcceleratorPlacementMode = KeyboardAcceleratorPlacementMode.Hidden;
        Add(VirtualKey.Z, VirtualKeyModifiers.Control, () => Undo());
        Add(VirtualKey.Y, VirtualKeyModifiers.Control, () => Redo());
        Add(VirtualKey.X, VirtualKeyModifiers.Control, () => CutInk());
        Add(VirtualKey.C, VirtualKeyModifiers.Control, () => CopyInk());
        Add(VirtualKey.V, VirtualKeyModifiers.Control, () => PasteInk());
        Add(VirtualKey.D, VirtualKeyModifiers.Control, () => Duplicate());
        Add(VirtualKey.A, VirtualKeyModifiers.Control, () => SelectAll());
        Add(VirtualKey.Delete, VirtualKeyModifiers.None, () => Delete());

        void Add(VirtualKey key, VirtualKeyModifiers modifiers, Action command)
        {
            var accelerator = new KeyboardAccelerator { Key = key, Modifiers = modifiers };
            accelerator.Invoked += (_, e) =>
            {
                if (active?.Invoke() == false || IsTextInput(FocusManager.GetFocusedElement(root.XamlRoot)))
                {
                    return; // not handled: the key goes on to the focused element
                }
                e.Handled = true;
                try
                {
                    command();
                }
                catch (Exception ex)
                {
                    InkLog.Write($"shortcut {modifiers}+{key}", ex);
                }
            };
            root.KeyboardAccelerators.Add(accelerator);
        }
    }

    private static bool IsTextInput(object? focused) =>
        focused is TextBox or RichEditBox or PasswordBox or AutoSuggestBox or NumberBox;

    // The top left of the part of the page that is seen (page points), inside the margin.
    private Point VisibleTopLeft(InkPage page)
    {
        try
        {
            var p = page.InkCanvas.TransformToVisual(scroller).Inverse.TransformPoint(default);
            double x = Math.Clamp(p.X + PasteMargin, PasteMargin, Math.Max(PasteMargin, page.Width - PasteMargin));
            double y = Math.Clamp(p.Y + PasteMargin, PasteMargin, Math.Max(PasteMargin, page.Height - PasteMargin));
            return new Point(x, y);
        }
        catch (Exception e)
        {
            InkLog.Write("paste ink: the visible part of the page (pasted at its top left)", e);
            return new Point(PasteMargin, PasteMargin);
        }
    }

    // Takes back what a failed add had added (strokes, and pictures already on the page); errors are logged, the
    // caller throws the original one.
    private static void TakeBack(PageEdit edit)
    {
        try
        {
            PageEdit.RemoveStrokes(edit.Page, edit.AddedStrokes);
        }
        catch (Exception e)
        {
            InkLog.Write("taking back the strokes of a failed add", e);
        }
        var onPage = edit.Page.Pictures.ToHashSet(ReferenceEqualityComparer.Instance);
        var attached = edit.AddedPictures.Where(onPage.Contains).ToList();
        if (attached.Count > 0)
        {
            try
            {
                edit.Page.RemovePictures(attached);
            }
            catch (Exception e)
            {
                InkLog.Write("taking back the pictures of a failed add", e);
            }
        }
    }
}
