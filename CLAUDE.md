# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.

## What this is

**InkPages**: the reusable, pen-first writing area for Win32 apps – Windows ink through the Win32 ink presenter
(`IInkDesktopHost`/`IInkPresenterDesktop`) in DirectComposition, finger/touchpad pan and zoom with DirectManipulation,
the tools (selection, shapes, partial eraser, auto-scroll, undo) and the toolbar (comctl32). Written in C++20, plain
Win32 + C++/WinRT, built with CMake. Embedded by Xournal Studio for Windows as the git submodule `external/InkPages`
(its C++ app in `native/`). Public repository; the README is the user-facing documentation.

**Since 2026-11-19 InkPages moves from WinUI 3 (C#) to C++/Win32** (user: "nativ, schlank, Win32 – aber die moderne
Variante davon"). The WinUI version is tagged **`winui-final`**: MatheTrainer stays pinned on it until it moves to C++
later (a WinUI fix would go on a branch from the tag, announced first); Xournal Studio's C# app stays on its pinned
submodule commit until its C++ app replaces it. The plan: xournal-studio-windows `docs/CPP-UMZUG.md` (phases 0–5).
Phase 0 (2026-11-19): project skeleton – library (`Log`), demo window, unit tests, build script, CI.

## Rules (agreed with the user)

- **Nativ, schlank, modern Win32.** C++20, plain Win32, C++/WinRT for WinRT APIs (Windows.UI.Input.Inking, InkAnalyzer,
  Windows.Data.Pdf). comctl32 v6 with visual styles (Windows 11 look, normal mouse sizes – the pen operates them),
  user32 menus, DirectComposition/Direct2D/DirectWrite/WIC, PerMonitorV2, static CRT. Few dependencies: only where
  Windows has nothing, statically linked. No UI framework on top, no mix of UI frameworks (no XAML islands) – native
  elements of the technology in use, no Frankenstein.
- **Microsoft's APIs do the work; the library only wires them up correctly.** The ink presenter collects, renders,
  selects, erases ink; DirectManipulation pans and zooms; the Windows ruler draws straight lines; `InkAnalyzer`
  recognizes shapes. Follow Microsoft's docs and samples (Windows-classic-samples: TouchInputDirectManipulation, ink).
  When an API is broken, find the root cause and report the options. The partial eraser stays an agreed exception
  (Windows ink erases whole strokes only).
- **Never swallow errors.** Log the kind, HRESULT and message (`inkpages::LogHr`, `inkpages::LogException` inside a
  catch); every boundary (window procedure, callbacks from Windows, threads) catches and logs.
- **No app dependencies.** No file format, no app texts (apps pass their language). Small classes with one job; logic
  without UI is unit-tested.
- Windows 11 (26200) only, x64. Right-handed: nothing interactive at the right edge. Touch and touchpad never draw: they
  pan and zoom the sheet; the pen writes; the mouse is a pointer.
- Code, comments and commits in English. Breaking API changes: update Xournal Studio in the same session.
- Coordinate with the MatheTrainer session: announce before editing this repository; one Windows Sandbox at a time
  (`wsb list` empty, lock file xournal-studio-windows `.local-build\sandbox.lock`).

## Build, test

```powershell
.\build.ps1            # CMake (Visual Studio's) Release build + unit tests (CTest); demo -> .local-build\demo
.\build.ps1 -NoTests
```
- Unit tests: GoogleTest (fetched once by CMake at configure time, pinned release; only for tests). Microsoft's
  CppUnitTest was tried first: vstest.console found no tests in the CMake-built DLL (2026-11-19, not pursued).
- UI tests in Windows Sandbox (the sandbox window opens on the desktop – tell the user first):
  `UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\<Test>.ps1`; in-sandbox scripts dot-source
  `C:\sbx\TestKit.ps1` (start the app, UI Automation, synthetic pen/touch with `Inj.cs`: hover ~100 ms before a press,
  a sample every ~4 DIPs). The demo logs to `INKPAGES_DEMO_LOG` (TestKit sets `out\demo.log`).
- Synthetic input fails with ERROR_ACCESS_DENIED while the host session is locked (Win32 demo, 2026-11-19).

## Structure

- `include/inkpages/` – the public headers; `src/` – the library (static); `demo/` – the demo app (manifest: comctl32
  v6, PerMonitorV2); `tests/` – unit tests; `UiTests/` – sandbox runner and TestKit.
- `Log.h`: `SetLogFile`, `SetErrorSink` (errors also to the app, e.g. its errors.log), `Log`, `LogError`,
  `DescribeHr`, `LogHr`, `LogException`.

## Facts learned in the WinUI version (tag `winui-final`; measured on the user's device and in Windows Sandbox, 2026-11-15..19)

These describe how the tools must behave (the C++ version keeps the behavior) and what the WinUI 3 platform did. Class
names refer to the C# sources at `winui-final`. Platform facts of the Win32 route: the user's global CLAUDE.md and
D:\src\win32-ink-demo.


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
