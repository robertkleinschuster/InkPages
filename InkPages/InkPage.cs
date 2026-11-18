using System.Numerics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using InkStroke = Windows.UI.Input.Inking.InkStroke;
using XamlCanvas = Microsoft.UI.Xaml.Controls.Canvas;

namespace InkPages;

/// <summary>
/// A page to write on: the app's layers (background, pictures, anything drawn below the ink) in
/// <see cref="UnderInk"/>, the <see cref="InkCanvas"/>, and an overlay for the tools' feedback – all in page units
/// (the page's size is its <see cref="FrameworkElement.Width"/> and <see cref="FrameworkElement.Height"/>).
/// <para>
/// The ink is the canvas' own <c>InkStrokeContainer</c>. What the app keeps beside it – its own data per stroke
/// (e.g. the file's element of a stroke) and the pictures – the tools reach through the members an app implements here:
/// pictures are the app's objects (compared by reference), placements and removed pictures are tokens the app gives
/// out and takes back.
/// </para>
/// </summary>
public abstract partial class InkPage : UserControl
{
    private readonly Grid sheet;

    protected InkPage(double width, double height)
    {
        Width = width;
        Height = height;
        InkCanvas = new InkCanvas();
        Overlay = new XamlCanvas { IsHitTestVisible = false };
        UnderInk = new Grid { IsHitTestVisible = false };
        sheet = Themed.Load<Grid>("Grid", "BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}' BorderThickness='0.5'");
        sheet.Children.Add(UnderInk);
        sheet.Children.Add(InkCanvas);
        sheet.Children.Add(Overlay);
        Content = sheet;
    }

    /// <summary>
    /// A blank sheet of a page's size and look, without ink or content: stands in for a page whose view is not created
    /// (an app that shows only the pages near the viewport), so the layout and the scroll extent stay the same.
    /// </summary>
    public static FrameworkElement CreatePlaceholder(double width, double height, Brush? background)
    {
        var sheet = Themed.Load<Border>("Border", "BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}' BorderThickness='0.5'");
        sheet.Width = width;
        sheet.Height = height;
        sheet.Background = background; // hit-test visible like a page: touch on it pans the view
        return sheet;
    }

    public InkCanvas InkCanvas { get; }

    public Microsoft.UI.Xaml.Controls.InkPresenter Ink => InkCanvas.InkPresenter;

    /// <summary>Layers below the ink, filled by the app (not hit-test visible).</summary>
    public Grid UnderInk { get; }

    /// <summary>Layer above the ink for tool feedback (lasso, selection, previews), in page units.</summary>
    public XamlCanvas Overlay { get; }

    /// <summary>Receives the pen as XAML pointer input while the presenters leave it (moving the selection).</summary>
    public UIElement Surface => sheet;

    /// <summary>The sheet's color.</summary>
    public Brush? SheetBackground
    {
        get => sheet.Background;
        set => sheet.Background = value;
    }

    /// <summary>A rectangle selection on this page also keeps its area (e.g. to copy what is seen there).</summary>
    public virtual bool KeepsSelectedAreas => false;

    // ---------------------------------------------------------------- strokes
    // The defaults serve a page that keeps nothing beside its ink (no data per stroke, no pictures). An app with its own
    // data per stroke or with pictures overrides the members of both sections.

    // Strokes that undo/redo replaced by copies (old id -> new id): CurrentStrokes follows the chain.
    private readonly Dictionary<uint, uint> replacedIds = new();

    /// <summary>A copy of a stroke took its place (undo/redo put back a removed stroke): the app's data goes with it.</summary>
    public virtual void StrokeReplaced(uint oldId, uint newId)
    {
        while (replacedIds.TryGetValue(oldId, out uint next))
        {
            oldId = next;
        }
        replacedIds[oldId] = newId;
    }

    /// <summary>The strokes on the page now for strokes an edit remembers: each itself or its copy; null where gone.</summary>
    public virtual List<InkStroke?> CurrentStrokes(IEnumerable<InkStroke> strokes)
    {
        var byId = Ink.StrokeContainer.GetStrokes().ToDictionary(s => s.Id);
        return strokes.Select(stroke =>
        {
            uint id = stroke.Id;
            while (replacedIds.TryGetValue(id, out uint next))
            {
                id = next;
            }
            return byId.GetValueOrDefault(id);
        }).ToList();
    }

    /// <summary>
    /// Removes all ink without an undo step (another document or exercise is shown; the app also calls
    /// <c>InkWorkspace.Reset</c>, which clears the history). For an undoable clear see <c>InkCommands.ClearPage</c>.
    /// </summary>
    public void ClearInk()
    {
        Ink.StrokeContainer.Clear();
        replacedIds.Clear();
    }

    /// <summary>
    /// A move or scale is previewed on the stroke (its <c>PointTransform</c> shows it): until it ends the app treats
    /// the stroke as having <paramref name="committed"/> (a snapshot never holds a half-done move).
    /// </summary>
    public virtual void BeginStrokePreview(InkStroke stroke, Matrix3x2 committed) { }

    /// <summary>The preview of these strokes ended (committed or reverted).</summary>
    public virtual void EndStrokePreview(IEnumerable<InkStroke> strokes) { }

    /// <summary>The ink shows the stroke transformed by <paramref name="m"/> (committed): the app's data follows.</summary>
    public virtual void StrokeTransformed(InkStroke stroke, Matrix3x2 m) { }

    private sealed record StrokePlacement(List<(InkStroke Stroke, Matrix3x2 Transform)> Strokes);

    /// <summary>
    /// Where strokes and pictures are now (a token for <see cref="RestorePlacement"/>): undo/redo of a move put back
    /// exactly this. <paramref name="transformOf"/> gives a stroke's transform instead of its current one.
    /// The default keeps the strokes' transforms (a page with pictures overrides both).
    /// </summary>
    public virtual object CapturePlacement(IEnumerable<InkStroke> strokes, IEnumerable<object> pictures,
        Func<InkStroke, Matrix3x2>? transformOf = null) =>
        new StrokePlacement(strokes.Select(s => (s, transformOf?.Invoke(s) ?? s.PointTransform)).ToList());

    public virtual void RestorePlacement(object placement)
    {
        var strokes = ((StrokePlacement)placement).Strokes;
        var current = CurrentStrokes(strokes.Select(s => s.Stroke));
        for (int i = 0; i < current.Count; i++)
        {
            if (current[i] is { } stroke)
            {
                stroke.PointTransform = strokes[i].Transform;
            }
        }
    }

    // ---------------------------------------------------------------- pictures (default: none)

    /// <summary>The page's pictures, bottom to top.</summary>
    public virtual IEnumerable<object> Pictures => [];

    public virtual Rect PictureBounds(object picture) => throw NoPictures();

    /// <summary>Starts a live preview of pictures: the action shows them moved by a transform (identity: as they are).</summary>
    public virtual Action<Matrix3x2> PreviewPictures(IReadOnlyCollection<object> pictures) => _ => { };

    /// <summary>Moves/scales pictures for good (also ends their preview).</summary>
    public virtual void TransformPictures(IReadOnlyCollection<object> pictures, Matrix3x2 m) { }

    public virtual void AddPictures(IReadOnlyCollection<object> pictures)
    {
        if (pictures.Count > 0)
        {
            throw NoPictures();
        }
    }

    /// <summary>Removes pictures; returns a token for <see cref="RestorePictures"/> (puts them back at their place).</summary>
    public virtual object RemovePictures(IReadOnlyCollection<object> pictures) => Array.Empty<object>();

    public virtual void RestorePictures(object removed) { }

    private NotSupportedException NoPictures() => new($"{GetType().Name} has no pictures (override the picture members of InkPage).");
}
