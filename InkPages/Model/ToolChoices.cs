namespace InkPages;

/// <summary>A pen preset: one tap selects it; it keeps its own color and width (an InkToolbar pen button of its own).</summary>
/// <param name="ColorIndex">Index into the palette.</param>
/// <param name="Width">Pens: Xournal's width at full pressure (<see cref="PenWidths"/>); highlighters: the drawn width.</param>
public sealed record PenPreset(int ColorIndex, double Width);

/// <summary>
/// What the user chose in the toolbar and keeps across sessions: the presets' colors and widths, the shape, the selection
/// mode and the eraser. Plain data (serializable, e.g. with System.Text.Json): the app stores it where it keeps its
/// settings, creates the toolbar and workspace from it and saves it on <c>InkWorkspace.ChoicesChanged</c>.
/// </summary>
public sealed class ToolChoices
{
    /// <summary>Xournal's medium pen width (pt at full pressure).</summary>
    public const double DefaultPenWidth = 1.41;

    /// <summary>Xournal's highlighter width (pt).</summary>
    public const double DefaultHighlighterWidth = 8.5;

    /// <summary>The pen presets; empty: <see cref="DefaultPens"/>.</summary>
    public List<PenPreset> Pens { get; set; } = [];

    /// <summary>The highlighter presets; empty: <see cref="DefaultHighlighters"/>.</summary>
    public List<PenPreset> Highlighters { get; set; } = [];

    public ShapeKind Shape { get; set; }

    public SelectMode SelectMode { get; set; }

    /// <summary>The exact eraser's circle radius (page points).</summary>
    public double EraserRadius { get; set; } = 4;

    /// <summary>The eraser erases whole strokes (Windows ink's eraser) instead of exactly the circle at the pen.</summary>
    public bool WholeStrokeErasing { get; set; }

    /// <summary>Shapes drawn freehand become clean ones.</summary>
    public bool RecognizeShapes { get; set; }

    /// <summary>Black, blue and red of Xournal's palette (<see cref="XournalPalette"/>) at the medium width.</summary>
    public static IReadOnlyList<PenPreset> DefaultPens { get; } =
        new PenPreset[] { new(0, DefaultPenWidth), new(1, DefaultPenWidth), new(2, DefaultPenWidth) };

    /// <summary>Yellow and green of the highlighter palette.</summary>
    public static IReadOnlyList<PenPreset> DefaultHighlighters { get; } =
        new PenPreset[] { new(0, DefaultHighlighterWidth), new(1, DefaultHighlighterWidth) };

    /// <summary>The pen presets to show: the saved ones, else the defaults.</summary>
    public IReadOnlyList<PenPreset> PensOrDefault => Pens.Count > 0 ? Pens : DefaultPens;

    /// <summary>The highlighter presets to show: the saved ones, else the defaults.</summary>
    public IReadOnlyList<PenPreset> HighlightersOrDefault => Highlighters.Count > 0 ? Highlighters : DefaultHighlighters;
}
