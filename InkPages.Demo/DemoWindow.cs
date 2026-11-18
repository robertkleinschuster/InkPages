using System.Numerics;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.System;
using Windows.UI;
using InkStroke = Windows.UI.Input.Inking.InkStroke;

namespace InkPages.Demo;

/// <summary>
/// The demo's window: the library's toolbar above three graph pages in a ScrollView, wired by
/// <see cref="InkWorkspace"/>; delete, duplicate, copy/paste (Windows ink's clipboard) and adding pages are the demo's
/// own small commands, as an app adds its own. Built in code.
/// </summary>
public sealed class DemoWindow : Window
{
    private readonly PenToolbar bar;
    private readonly ScrollView scroller;
    private readonly StackPanel panel = new() { Spacing = 16, Padding = new Thickness(16), HorizontalAlignment = HorizontalAlignment.Center };
    private readonly InkWorkspace ink;

    public DemoWindow(ElementTheme theme)
    {
        Title = "InkPages Demo";
        SystemBackdrop = new MicaBackdrop();
        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.Maximize();
        }

        var choices = new ToolChoices(); // the defaults (an app loads the user's saved choices)
        bar = new PenToolbar(new PenToolbarTexts(), choices);
        scroller = new ScrollView
        {
            // The pen never pans (it would capture the pen); touch and touchpad scroll and zoom.
            IgnoredInputKinds = ScrollingInputKinds.Pen,
            ContentOrientation = ScrollingContentOrientation.Vertical,
            ZoomMode = ScrollingZoomMode.Enabled,
            HorizontalScrollBarVisibility = ScrollingScrollBarVisibility.Hidden,
            VerticalScrollBarVisibility = ScrollingScrollBarVisibility.Hidden,
            Content = panel,
        };
        ink = new InkWorkspace(bar, scroller, choices);
        ink.Selection.TransferFailed += e => InkLog.Write("demo: the selection stays on its page" + (e == null ? "" : $" ({e.Message})"));

        AddCommands();
        for (int i = 0; i < 3; i++)
        {
            AddPage();
        }
        ((DemoPage)panel.Children[0]).AddPictures(new object[] { new DemoPicture(new Rect(60, 420, 160, 100), Colors.SteelBlue) });
        // The page in view is known only once the pages are laid out: before, every page lies at 0 and CurrentPage would
        // be the last one - the toolbar (and the ruler, which belongs to its target page) would go to a page out of view.
        // ViewChanged is not raised for the first layout; SizeChanged of the viewport is.
        scroller.ViewChanged += (_, _) => ink.SetCurrentPage(CurrentPage);
        scroller.SizeChanged += (_, _) => ink.SetCurrentPage(CurrentPage);

        var root = new Grid { RequestedTheme = theme };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition());
        bar.Margin = new Thickness(8, 4, 8, 4);
        root.Children.Add(bar);
        Grid.SetRow(scroller, 1);
        root.Children.Add(scroller);
        // Ctrl+Z/Y/X/C/V/D/A and Del, with the accelerators' tooltips hidden (a windowed popup over the pages breaks pen drags).
        ink.Commands.AddShortcuts(root);
        Content = root;
        // The page has the keyboard focus at start (not the first toolbar button, which would show a focus rectangle).
        root.Loaded += (_, _) => scroller.Focus(FocusState.Programmatic);
    }

    private IEnumerable<DemoPage> Pages => panel.Children.OfType<DemoPage>();

    // The page in the middle of the view (none before the first layout).
    private DemoPage? CurrentPage => scroller.ViewportHeight <= 0 ? null : Pages.LastOrDefault(p =>
        p.TransformToVisual(scroller).TransformPoint(default).Y <= scroller.ViewportHeight / 2) ?? Pages.FirstOrDefault();

    private void AddCommands()
    {
        // The library's commands on Windows ink; duplicate also copies the demo's pictures.
        ink.Commands.CopyPicture = (picture, m) =>
            new DemoPicture(Selection.TransformBounds(((DemoPicture)picture).Bounds, m), ((DemoPicture)picture).Color);
        ink.Commands.WireToolbar();
        var paste = PenToolbar.Command(new SymbolIcon(Symbol.Paste), "Paste ink", "PasteButton");
        paste.Click += (_, _) => ink.Commands.PasteInk();
        var addPage = PenToolbar.Command(new SymbolIcon(Symbol.Add), "Add page", "InsertPageButton");
        addPage.Click += (_, _) => AddPage();
        bar.Extensions.Add(paste);
        bar.Extensions.Add(new AppBarSeparator());
        bar.Extensions.Add(addPage);
    }

    private void AddPage()
    {
        var page = new DemoPage(595, 842); // A4 in points
        ink.Attach(page);
        panel.Children.Add(page);
        ink.SetPages(Pages);
    }}
