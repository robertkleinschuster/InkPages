namespace InkPages;

/// <summary>The toolbar's texts (names, tooltips); an app sets them in its language. English by default.</summary>
public sealed record PenToolbarTexts
{
    public string Pen { get; init; } = "Pen";
    public string Highlighter { get; init; } = "Highlighter";
    /// <summary>A pen or highlighter preset's tooltip after its name.</summary>
    public string PresetTip { get; init; } = "tap again for color and size";
    public string Eraser { get; init; } = "Eraser";
    public string EraserTip { get; init; } = "Eraser – the pen's eraser button erases too, with any tool";
    /// <summary>The eraser's mode toggle (whole strokes instead of exactly the circle at the pen).</summary>
    public string WholeStrokeErase { get; init; } = "Erase whole strokes";
    public string WholeStrokeEraseTip { get; init; } =
        "Erase whole strokes: the eraser and the pen's eraser button erase every stroke they touch. Off: only what is " +
        "in the circle. Right-click (pen: tap with the barrel button held): the circle's size";
    /// <summary>The exact eraser's circle size (the mode toggle's context flyout).</summary>
    public string EraserRadius { get; init; } = "Size";
    public string Select { get; init; } = "Select";
    public string SelectTip { get; init; } =
        "Select: lasso or rectangle (tap again to choose); drag to move, corners to scale. " +
        "With the pen, a lasso works with any tool: drag with the barrel button held";
    public string SelectWith { get; init; } = "Select with";
    public string Lasso { get; init; } = "Lasso";
    public string Rectangle { get; init; } = "Rectangle";
    public string Shapes { get; init; } = "Shapes";
    public string ShapesTip { get; init; } = "Shapes: line, arrow, rectangle, ellipse (tap again to choose)";
    public string Shape { get; init; } = "Shape";
    /// <summary>The header above shape recognition (in the shape tool's options).</summary>
    public string Freehand { get; init; } = "Drawn freehand";
    public IReadOnlyList<string> ShapeKinds { get; init; } = new[] { "Line", "Arrow", "Rectangle", "Ellipse" };
    public string Ruler { get; init; } = "Ruler / protractor";
    public string RecognizeShapes { get; init; } = "Recognize drawn shapes";
    public string RecognizeShapesTip { get; init; } = "Shapes drawn with the pen (circle, rectangle, ...) become clean shapes";
    // (Keyboard shortcuts are the app's: it names them in its texts.)
    public string Undo { get; init; } = "Undo";
    public string Redo { get; init; } = "Redo";
    public string Cut { get; init; } = "Cut selection";
    public string Copy { get; init; } = "Copy selection";
    public string Duplicate { get; init; } = "Duplicate selection";
    public string DeleteSelection { get; init; } = "Delete selection";
    /// <summary>The names of <see cref="XournalPalette.PenColors"/>, in its order.</summary>
    public IReadOnlyList<string> PenColorNames { get; init; } = new[]
        { "Black", "Blue", "Red", "Green", "Gray", "Light blue", "Light green", "Magenta", "Orange", "Yellow" };
    /// <summary>The names of <see cref="XournalPalette.HighlighterColors"/>, in its order.</summary>
    public IReadOnlyList<string> HighlighterColorNames { get; init; } = new[] { "Yellow", "Green", "Light blue", "Magenta", "Orange" };

    /// <summary>
    /// German texts (shared by the German apps that embed the library). The command tips name the shortcuts of
    /// <see cref="InkCommands.AddShortcuts"/>; an app changes single texts with <c>with { ... }</c>.
    /// </summary>
    public static PenToolbarTexts German { get; } = new()
    {
        Pen = "Stift",
        Highlighter = "Textmarker",
        PresetTip = "nochmal tippen für Farbe und Stärke",
        Eraser = "Radierer",
        EraserTip = "Radierer – die Radierer-Taste des Stifts radiert auch, mit jedem Werkzeug",
        WholeStrokeErase = "Ganze Striche radieren",
        WholeStrokeEraseTip = "Ganze Striche radieren: Radierer und Radierer-Taste des Stifts löschen jeden Strich, den sie " +
            "berühren. Aus: nur, was im Kreis liegt. Rechtsklick (mit dem Stift: Stifttaste halten und tippen): Größe des Kreises",
        EraserRadius = "Größe",
        Select = "Auswählen",
        SelectTip = "Auswählen: Lasso oder Rechteck (nochmal tippen zum Wählen); ziehen verschiebt, Ecken skalieren. " +
            "Mit dem Stift geht das Lasso mit jedem Werkzeug: mit gehaltener Stifttaste ziehen",
        SelectWith = "Auswählen mit",
        Lasso = "Lasso",
        Rectangle = "Rechteck",
        Shapes = "Formen",
        ShapesTip = "Formen: Linie, Pfeil, Rechteck, Ellipse (nochmal tippen zum Wählen)",
        Shape = "Form",
        Freehand = "Freihand gezeichnet",
        ShapeKinds = new[] { "Linie", "Pfeil", "Rechteck", "Ellipse" },
        Ruler = "Lineal / Geodreieck",
        RecognizeShapes = "Gezeichnete Formen erkennen",
        RecognizeShapesTip = "Mit dem Stift gezeichnete Kreise, Rechtecke, ... werden zu sauberen Formen",
        Undo = "Rückgängig (Strg+Z)",
        Redo = "Wiederholen (Strg+Y)",
        Cut = "Auswahl ausschneiden (Strg+X)",
        Copy = "Auswahl kopieren (Strg+C)",
        Duplicate = "Auswahl duplizieren (Strg+D)",
        DeleteSelection = "Auswahl löschen (Entf)",
        PenColorNames = new[] { "Schwarz", "Blau", "Rot", "Grün", "Grau", "Hellblau", "Hellgrün", "Magenta", "Orange", "Gelb" },
        HighlighterColorNames = new[] { "Gelb", "Grün", "Hellblau", "Magenta", "Orange" },
    };
}
