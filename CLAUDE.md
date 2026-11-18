# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.

## What this is

**InkPages**: the reusable, pen-first writing area for WinUI 3 apps. It holds pages with Windows Ink, the tools
(selection, shapes, partial eraser, auto-scroll, undo) and the toolbar. It was moved out of
[xournal-studio-windows](https://github.com/robertkleinschuster/xournal-studio-windows) on 2026-11-18, history included,
so that other apps can use it too. Xournal Studio for Windows and the MatheTrainer embed it as a git submodule
(`external/InkPages`). The library has no file format: apps implement `InkPage` and add their commands to the toolbar.
Public repository. The README is the user-facing documentation (embedding, runtime).

Where the knowledge is: platform facts for any WinUI ink app (InkCanvas, InkPresenter input, lifted compositor, XAML
input) are in the user's global CLAUDE.md; why the tools are built the way they are – and the WinUI fork's fixes – is
in "Facts learned" below (every embedding app's sessions should read it); the apps' own CLAUDE.md files hold only what
is specific to them (xournal-studio-windows: .xopp, PDF, explorer, sync; MatheTrainer: exercises, recognition).

## Rules (agreed with the user)

- **Microsoft's APIs do the work; the library only wires them up correctly.** `InkStrokeContainer` selects, moves,
  deletes and copies ink. Straight lines come from the Windows ruler, shape recognition from `InkAnalyzer`, and the
  eraser is the native one. Do not re-implement what a platform feature does, and do not work around framework
  behavior. When an API is broken, find the root cause (issue/PR) and report the options. The partial eraser is an
  agreed exception: Windows ink erases whole strokes only (proposal microsoft-ui-xaml #1530).
- **Never swallow exceptions.** Log type, HResult and message (`InkLog`; handlers on forwarded ink events log
  themselves through `InkLog.Guard`).
- **No app dependencies.** No file format and no app texts: `PenToolbarTexts` holds English defaults, and apps pass
  their own language. Built in code with no XAML resources, so nothing has to be merged into an app's .pri.
- Small classes with one job. Logic without UI goes into `Model/` (unit-tested).
- Windows 11 (26200) only, x64, unpackaged self-contained apps. Right-handed: nothing interactive at the right edge.
  Touch never draws.
- Code, comments and commits in English.
- Breaking API changes affect every embedding app. Update them in the same session, or say what they have to change.

## WinUI runtime (2026-11-18)

- The library compiles against the official `Microsoft.WindowsAppSDK.WinUI` + `.DWrite` **2.6.6-experimental**.
  `build/WinUIForkRuntime.targets` (imported by every app, including the demo) swaps the package's runtime payload
  for the **Release (fre)** runtime of the WinUI fork. It does this right after
  `AddMicrosoftWindowsAppSDKPayloadFilesFromComponents`, so the fork's `Microsoft.UI.Xaml.Controls.pri` templates reach
  the app's .pri. The fork is robertkleinschuster/microsoft-ui-xaml, branch `fix/ink-runtime-highlighter`: upstream main
  of 2026-11-16 + PR #12292 + InkToolbar custom tool states + pointer capture across routing + custom pens' drawing
  attributes.
- `tools/Get-WinUIRuntime.ps1` fetches it from the fork's release `winui-ink-fix-d3b1bf22` (pinned URL + SHA-256)
  into `.local-build/winui-fre` of **this** repository, so one pin serves all embedding apps. When an official
  package ships PR #12292 and the other fixes, reference it and drop the overlay and the script.
- Never use the chk (Debug) WinUI payload: it stops at internal asserts (0xC0000420). Never compile against the fork's
  own package (3.0.0-dev): its XAML compiler breaks ScrollView's template.
- `tools/Build-WinUILocal.ps1 -Source D:\wb` builds the fork's x64fre locally for iterating on WinUI fixes. The first
  build takes ~57 min.
- Issues/PRs for the fork's fixes are not opened upstream (public, needs the user's approval).

## Facts learned (the tools; measured with the user's device and in Windows Sandbox, 2026-11-15..18)

Platform facts that hold for any WinUI ink app are in the user's global CLAUDE.md (InkCanvas/InkPresenter, lifted
compositor, XAML input, PDF, sandbox); the facts below are why the library's code is the way it is. Moved here from
xournal-studio-windows' CLAUDE.md on 2026-11-18 so that every embedding app (MatheTrainer too) gets them.

### Input: presenters, unprocessed input, hover takeovers
- Custom tools (selection, shapes, partial eraser) run on `InkPresenter.UnprocessedInput`: custom tool → presenters'
  `InputProcessingMode.None`; barrel → `RightDragAction.LeaveUnprocessed`. Readable on the UI thread only with the
  fork's runtime (PR #12292; the stock 2.6.6 runtime throws 0x80004021 CO_E_NOT_SUPPORTED on `CurrentPoint`).
  **Positions are the InkCanvas' coordinates** = stroke and overlay coordinates – no transform (an extra
  XamlRoot→page transform put every lasso beside the page). Handlers log their own exceptions (`InkLog.Guard`):
  WinUI's forwarding would not hand them to `App.UnhandledException`.
- Unprocessed input also carries **Entered/Hovered/Exited while the presenters ink**, with `IsInverted` (eraser end;
  the Lenovo pen's eraser button makes the pen re-enter as a new pointer with `inv=1`, the touch then has `IsEraser`);
  barrel = `barrel=1 right=1 kind=RightButtonPressed`. The eraser end is taken on its inverted hover
  (`InkTools.TakeEraserEnd`, only in range and while erasing is partial), a tip hover in range gives it back – so
  partial erasing works from the first touch (a native erase before the hover was seen is put back as a fallback,
  `InkWorkspace.OnStrokesErased`).
- Switching only `InputProcessingConfiguration.Mode` during a hover is safe (the press then arrives unprocessed).
  Rewriting `InputDeviceTypes`/`UpdateDefaultDrawingAttributes`, or `IsEraserInputEnabled = false`, makes the presenter
  drop the hovering pointer (Exited in range, no press). `IsEraserInputEnabled = false` is ignored by 2.6.6 anyway.
- **Moving/scaling a selection is XAML pointer input** (`SelectionDrag`): the presenter that got the press keeps the
  pen, and its coordinates drift once its page scrolls out of view (lifted composition, not WinUI code – the dragged
  content slid away from the pen; reproduced with a barrel drag held at the view's edge, `ToolsTest.ps1`). Microsoft's
  docs build only the lasso from UnprocessedInput; their samples take the pen from the ink for a non-ink mode
  (SimpleInk Scenario9: `IsInputEnabled = false`). While the pen (tip or barrel) hovers over the selection, the
  presenters get `InputDeviceTypes = None` (`ToolController.SelectionTakenOver`); press, capture and moves then arrive
  on the page's `Surface` and `GetCurrentPoint(page.InkCanvas)` is right on any page, also while scrolling. Away from
  the selection (XAML hover) the presenters get the pen back before it touches. The takeover needs the pen's hover
  before the touch (a real pen always hovers; a test must hover ~100 ms).
- **Stale presenter events:** switching the presenters' devices to None makes them report Exited ~50 ms later; that
  stale event once gave the pen back to the ink, so the press went to the presenters and the drag ran on the ink path.
  Only XAML gives the pen back (`InkTools.Hand`); presenter events never release a takeover, none change it while a
  XAML drag runs.
- **Pointer capture lost mid-drag – fixed in the fork (3d994d89):** ~60 ms into a XAML drag the island's
  `InputPointerSource` raised `PointerRoutedAway` and ~3 ms later `PointerRoutedTo` (trigger: a barrel drag after an
  earlier tip drag); WinUI mapped RoutedAway to "suspended" and released the capture. The fork keeps the capture.
  Diagnose with `InputPointerSource.GetForIsland(XamlRoot.ContentIsland)` events.
- **Windowed popups and pen drags:** a toolbar button's tooltip (mouse resting on it), a `CommandBarFlyout`, the
  keyboard-accelerator tooltip of an element with `KeyboardAccelerators` (shown wherever the pen hovers) are windowed
  popups; while one is open a barrel gesture's release is lost and a XAML drag can lose its capture. Accelerators on a
  root element need `KeyboardAcceleratorPlacementMode = Hidden` (`InkCommands.AddShortcuts` does that); never show a
  windowed popup where the barrel drags (why the selection's commands are toolbar buttons, not a flyout at the
  selection). Tests move the mouse off the toolbar after each click.

### Pages, views, drawing
- **The experimental InkCanvas composes its ink above all XAML laid over it and takes pen input there** (issue #10461;
  fix #11904 merged and reverted #11928). Never lay XAML over a page. ContentDialog: collapse the pages' ink while it
  is open (`InkWorkspace.InkHidden`; `IsInputEnabled = false` stops input but leaves ink drawn over the dialog).
  MenuBar menus and `ConfigurationContent` flyouts are windowed popups: drawn above the ink, pen taps reach them.
- Each page's ink is drawn only within its page (WinUI sizes the InkCanvas' `ContentExternalOutputLink` placement
  visual to the control; ink moved beyond it by `PointTransform` is cut off, z-order does not help). So a selection
  dragged over another page is handed to that page (`SelectionTool` copies it there with `InkPage.CopyTo` – the app
  copies its data – and removes the originals; `TransferFailed` when it cannot) and the drag
  goes on there; the whole drag is one undo step (`CombinedEdit`).
- **Lazy page views:** memory is in the `InkStroke` objects, not the canvases (40-page document, 26.5k strokes:
  ~230 MB for all strokes, all views only ~85 MB more), so only pages near the viewport should have a view;
  `InkWorkspace.Holds` says which views must stay (history, selection, running gesture or hand-over, recognition) –
  `InkStrokeContainer` has no public constructor and a stroke belongs to one container, so edits cannot outlive their
  canvas. Released views' event handlers must come off (`SetPages`): presenter events would keep the view alive.
  `InkPage.CreatePlaceholder` keeps a released page's place in the layout.
- `InkStroke.PointTransform` on a stroke in the canvas' container redraws live (move/scale previews); the pen width is
  not scaled; Windows ink's clipboard carries the transform.
- Strokes are polylines (`FitToCurve = false`): with Bezier fitting few-point rectangles came out round.
- **Pressure and width:** Windows ink draws size s at pressure p about `s * (0.25 + 1.5 p)` wide (measured; docs only
  say DIPs): s at half, 1.75 s at full pressure. The library's widths mean the width at full pressure (Xournal++'s
  meaning): size = width / `InkStrokes.FullPressureFactor`; shapes have no pressure and use the full width
  (`InkStrokes.ToolWidth`). The user's pen (Lenovo/Wacom AES) delivers the full range: handwriting median 0.5–0.6.
  `InkToolbarStrokeWidthSlider` moves in whole steps and shows whole numbers, so the slider value is a step of
  `PenWidths.Steps` (0.42 / 0.85 / 1.41 / 2.26 / 3.4 / 5.67), mapped in `BallpointPen.CreateInkDrawingAttributesCore`
  (`PenWidthTest.ps1`).
- **Highlighters drew opaque – fixed in the fork (d3b1bf22):** WinUI's InkToolbar built drawing attributes from the
  tool kind only and dropped custom pens' attributes (`DrawAsHighlighter`, `IgnorePressure`, tip). Windows itself
  draws highlighter ink correctly. Before blaming rendering, read back `toolbar.InkDrawingAttributes`
  (`HighlighterTest.ps1`).

### Toolbar
- InkToolbar keeps only one built-in ballpoint/highlighter/eraser button and orders buttons itself (built-in pens,
  custom pens, eraser, custom tools, stencil, toggles; no separators inside): several pens = custom pen buttons, the
  highlighter is a custom pen too; the eraser-mode toggle cannot stand right beside the eraser.
- `InkToolbarCustomToolButton`'s WinUI template had no Checked states and no `GlyphVisualStates` – fixed in the fork
  (custom tools show their state and the options chevron). InkToolbar draws glyphs at 16 (`InkToolbarButtonFontSize`),
  FontIcon defaults to 20: custom buttons' icons are 16.
- UI Automation ids: the InkToolbar's own buttons are `InkToolbarBallpointPenButton`, `InkToolbarHighlighterButton`,
  `InkToolbarEraserButton` (their `x:Name` is ignored); `PenToolbar` sets `PenButton1..n`, `HighlighterButton1..n`,
  `WholeStrokeEraseButton`, `SelectButton`, `ShapeButton`, `StencilButton`, `UndoButton`, ...
- **The ruler/protractor belongs to the InkToolbar's `TargetInkCanvas`:** changing the target hides the stencil and
  unchecks its button (InkToolbar.cpp ~1567). The target is the page in view; set it only after the first layout
  (before it every page lies at y 0 – the demo's ruler "not shown" was that). Touch moves the ruler.
- `InkToolbarCustomToolButton.ConfigurationContent` gives a tool a native flyout (opens on the second tap).
- `ScrollView` needs `IgnoredInputKinds="Pen"` (it captures the pen for panning otherwise) and
  `ContentOrientation="Vertical"` (zoomed content was clipped). `ScrollView.ZoomTo` is asynchronous: act on
  `ZoomCompleted` of the request.
- `InkAnalyzer` works unpackaged; for ellipses `InkAnalysisInkDrawing.Points` are the ends of both axes (0–2, 1–3).

## Architecture

- `InkWorkspace`: the facade. It wires the toolbar, tools, input, auto-scroll and undo. Members: `Attach(page)`,
  `SetPages` (the pages that have a view now), `Holds(page)` (the page's view must stay: history, selection, running
  gesture or drag hand-over, shape recognition), `Reset`, `Removing/Removed`, `UndoStep`.
- `InkWorkspace` also holds `Commands` (`InkCommands`), `CurrentPage`, `Choices`/`ChoicesChanged` (the user's
  `ToolChoices`, raised once changes settle), `InkHidden` (collapse all pages' ink while a dialog lies over them, also
  on pages attached later) and `ResetPage` (a page's ink replaced without undo: load, new task).
- The page the toolbar drives (`CurrentPage`: ruler, where the user writes) is outlined in the accent color while more
  than one page is shown (`MarkCurrentPage`: null = by the shown pages; an app with lazy views decides by its
  document). The outline lies 3 px outside the page (never over the ink; the page's coordinates stay), so the pages'
  spacing must leave room for it (16 in the apps). Screenshots: `InkPages.Demo/UiTests/CurrentPageMark.ps1`.
- `UndoStack.ContentChanged` (edit recorded, undone, redone – not on clearing) and the save point (`MarkSaved`,
  `IsAtSavePoint`, `Model/SavePoint`) tell the apps whether there is something to save. `InkLog.ErrorWriter` takes the
  errors (exceptions, `InkLog.Error`) apart from the rest.
- `InkPage`: the abstract page. It holds the app's `UnderInk` layers, the InkCanvas, the `Overlay` and the `Surface`.
  Stroke identity across undo is the library's (`StrokeReplaced`/`CurrentStrokes`, not virtual; an app with data per
  stroke follows with `OnStrokeReplaced`). Copying to another page (`CopyTo`), previews, placements and pictures have
  defaults for a page with ink only (`ClearInk` empties it without undo); an app with data per stroke or pictures
  overrides them (pictures are its own objects, removal tokens its own). `CreatePlaceholder` is a blank sheet for a page without a view.
- `InkCommands`: the edit commands on Windows ink shared by the apps - undo/redo, delete, duplicate (`CopyPicture` for
  pictures), cut/copy/paste through Windows ink's clipboard, select all, `ClearPage`, `SelectAdded`; refused while the
  selection is dragged or the ink hidden (`Refused`; message `PenToolbarTexts.RefusedTitle(command)` + `Refused`, command names in
  `CommandNames`). `PasteInk`: null = refused, empty =
  nothing pasted. `WireToolbar(clipboard)` (false: an app with its own clipboard format wires cut/copy/duplicate) and
  `AddShortcuts` (hidden accelerator tooltips, not while typing).
- `InkPage.Paper` (`Model/PaperLines.cs`: `Paper`, `PaperKind` Plain/Graph/Lined, spacing in page units/DIPs, line
  color and thickness; default color #D6E2F0): ruling drawn below the app's layers and the ink. Null: none (an app with
  its own background, e.g. Xournal Studio's file ruling or PDF pages). The demo uses it.
- `PenToolbar.Paste` ("PasteInkButton"): in the row once `InkCommands.WireToolbar` runs. With `clipboard: true` it
  pastes Windows ink and is enabled while the clipboard holds ink (`RefreshPaste`: on `Clipboard.ContentChanged`, after
  copy/cut; an app may call it when its window is activated); with `false` (own clipboard format, Xournal Studio) the
  app wires its Click and IsEnabled.
  `PenToolbar.Contextual` (opt-in, MatheTrainer): the row swaps with the state – writing: the tools; something selected:
  select, undo/redo and the selection's commands, the other tools collapsed (the active tool stays active); in both,
  paste while the clipboard holds ink. Commands that cannot run are collapsed, not disabled (in a CommandBar the app's
  commands get the room). Default: every button in place, disabled when it cannot run.
- `PenToolbar.Compact`: 40-wide command buttons (also the app's extensions), no separators, no spacing – for narrow
  windows (MatheTrainer at ~1280 DIP); names/tooltips/UIA ids stay; the InkToolbar's own buttons keep WinUI's size.
- `PenToolbar` (+ `PenToolbarTexts` with `German`, `BallpointPen`/`HighlighterPen`, `Extensions` for app buttons,
  `PenToolbar.Command`). `new PenToolbar(texts, choices)` uses `XournalPalette` and the user's `ToolChoices`
  (`Model/`, defaults: black/blue/red pens, yellow/green highlighters). The row: undo | pens | highlighters | eraser
  (+ whole-stroke toggle) | select, shapes, ruler | selection commands | app extensions.
- `ToolController`: copies the InkToolbar's tool to all pages' presenters. For a custom tool and for hover takeovers
  it sets processing mode None, and then switches only the mode.
- `InkTools`: routes the presenters' unprocessed pen/mouse input to `SelectionTool`, `ShapeTool` and
  `PartialEraser`. A barrel press always goes to the selection. It also handles the hover takeovers (the eraser end,
  the tip over the selection).
- `Selection`, `SelectionDrag` and `DragAutoScroll` handle the selection (its commands' state: `PenToolbar.ShowSelection`). A move or scale runs
  on XAML pointer input on the page's `Surface`, because the presenter that got the press keeps the pen and its
  coordinates drift once its page scrolls. Auto-scroll runs on `CompositionTarget.Rendering`.
- `PartialEraser`, `ShapeTool`, `ShapeRecognition`, `UndoStack` (`IPageEdit`: `PageEdit`, `TransformEdit`,
  `CombinedEdit`), `InkLog`.
- `Model/` has no UI: `ToolChoices`/`PenPreset`, `StrokeEraser`, `ShapeGeometry`/`ShapeKind`, `InkStrokes` (drawn width;
  `FullPressureFactor` = 1.75), `PenWidths` (Xournal's width steps), `SampledStroke`, `SelectMode`.

## Testing

- `dotnet test InkPages.Tests`: unit tests of `Model/`.
- UI tests run in Windows Sandbox (the sandbox window opens on the desktop, so tell the user first):
  `dotnet publish InkPages.Demo -c Release -o .local-build\demo`, then
  `UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test InkPages.Demo\UiTests\ToolsTest.ps1`.
  `ToolsTest` covers the stroke eraser, eraser end, barrel lasso/move, selection (duplicate, scale, undo), exact
  erasing, shapes and recognition, and a barrel drag across pages with auto-scroll. Also available: `CrossPageDrag`,
  `DuplicateScaleUndo`, `HighlighterTest`, `PenWidthTest`, and `ToolbarStates` (screenshots of every toolbar state,
  light and dark, for design reviews).
- The demo logs to `INKPAGES_DEMO_LOG` (TestKit sets `out\demo.log`): gesture begin/end and undo steps.
- Synthetic pen input must hover ~100 ms before a press on the selection, as a real pen does, and needs a sample
  every ~4 DIPs. Check that the target window is in the foreground before injecting. In-sandbox scripts run in
  PowerShell 5.1 (Add-Type = C# 5). Never name a function `Erase` (an alias of Remove-Item).
- After a change, build an embedding app too (xournal-studio-windows: update its submodule and run its tests).
