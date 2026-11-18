using Windows.UI;

namespace InkPages;

/// <summary>
/// Xournal++'s default palette for pens and its usual highlighter colors (yellow first), named in the toolbar's
/// language (<see cref="PenToolbarTexts.PenColorNames"/>, <see cref="PenToolbarTexts.HighlighterColorNames"/>). The
/// defaults of <see cref="ToolChoices"/> index into these.
/// </summary>
public static class XournalPalette
{
    public static IReadOnlyList<Color> PenColors { get; } = new Color[]
    {
        Rgb(0x000000), // black
        Rgb(0x3333cc), // blue
        Rgb(0xff0000), // red
        Rgb(0x008000), // green
        Rgb(0x808080), // gray
        Rgb(0x00c0ff), // light blue
        Rgb(0x00ff00), // light green
        Rgb(0xff00ff), // magenta
        Rgb(0xff8000), // orange
        Rgb(0xffff00), // yellow
    };

    public static IReadOnlyList<Color> HighlighterColors { get; } = new Color[]
    {
        Rgb(0xffff00), // yellow
        Rgb(0x00ff00), // green
        Rgb(0x00c0ff), // light blue
        Rgb(0xff00ff), // magenta
        Rgb(0xff8000), // orange
    };

    public static IReadOnlyList<NamedColor> Pens(PenToolbarTexts texts) => Named(PenColors, texts.PenColorNames);

    public static IReadOnlyList<NamedColor> Highlighters(PenToolbarTexts texts) => Named(HighlighterColors, texts.HighlighterColorNames);

    private static List<NamedColor> Named(IReadOnlyList<Color> colors, IReadOnlyList<string> names)
    {
        if (names.Count != colors.Count)
        {
            throw new ArgumentException($"The texts name {names.Count} colors; the palette has {colors.Count}.", nameof(names));
        }
        return colors.Select((c, i) => new NamedColor(c, names[i])).ToList();
    }

    private static Color Rgb(uint rgb) => Color.FromArgb(255, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
}
