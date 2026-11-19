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
Phase 1 (2026-11-19): the writing surface – `WritingSurface` (pages, paper, ink, DirectManipulation pan/zoom, pen
cursor, lazy pages), demo, unit tests and sandbox tests (see "Writing surface" below). Next: phase 2, the tools.

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
- Pure logic (unit-tested, `tests/GeometryTests.cpp`): `Paper.h` (paper kinds and their lines, ported from
  `Model/PaperLines.cs`), `PageLayout.h` (vertical page stack, page at/nearest to a point, page in view),
  `ViewMath.h` (DirectManipulation's transform <-> document DIPs, zoom clamp, fit width, zoom/scroll rectangles kept on
  the content, `LivePages` = which pages hold their ink).
- `WritingSurface.h` (public) / `src/WritingSurface.cpp`: the child window; wires `Graphics` (D3D11 + D2D + DComp
  device), `Viewport` (DirectManipulation), `PaperLayer` (one sheet: D2D into a DComp virtual surface) and `InkLayer`
  (the ink presenter on Windows' ink host thread, page ownership of strokes, lazy load/store).
- `demo/`: `main.cpp` (only the public API), `cursors/` + `tools/make_cursors.py` (dot cursors per color × width),
  `UiTests/SurfaceTest.ps1` (pan/pinch/wheel, ink under the pen, page 2, lazy pages, pen + finger, cursor),
  `UiTests/BoundaryTest.ps1` (a stroke across the gap). `tools/cursorprobe.cpp`: DXGI Desktop Duplication probe – the
  pointer as the screen draws it (GetCursorInfo and screenshots cannot show it); `build.ps1` puts it in
  `.local-build	ools` (pass it with `-Data`).

## Writing surface (phase 1, measured 2026-11-19; sandbox = Windows Sandbox 1514x900 at 150 %)

- **One ink presenter for the whole document, not one per page (decided by measurement).** Numbers (local, 150 %):
  one presenter over 3 or over 40 or 100 A4 pages: created in ~5 ms (first in a process ~40 ms incl. the host
  thread), working set unchanged by the document's size (50.1 MB either way – the presenter has no surface of its own
  size). One presenter per page: 40 in ~100 ms (2.5 ms each), +0.36 MB each (64.5 MB vs 50.1 MB). Decisive:
  **setting `CoreInkIndependentInputSource.PointerCursor` while Windows still activates a presenter's input context
  kills the process** – fail-fast 0xC0000409 in Windows.UI.Input.Inking (`CInputContext::_RunOnInkThreadSync` →
  E_UNEXPECTED; stack with cdb: the internal ink thread was in `CInputContext::Activate` at the same moment). It hit
  reliably when presenters were created while others already had cursors (the lazy per-page pattern), never when all
  cursors were set after creation. No event says the activation is done, so per-page presenters with lazy creation
  would gamble on timing. With one presenter: created once, its cursor set 300 ms later (`CursorDelayMs`), tool changes
  later are safe. No input switching between presenters either.
- Strokes still belong to pages (the file format keeps them per page): a stroke belongs to the page nearest to its
  first point. **A stroke across the gap is one stroke, owned by the page it began on, and drawn over the gap**
  (`BoundaryTest.ps1`) – the WinUI version cut ink off at the page's edge. Open for phase 2: clip or split.
- Lazy pages = lazy strokes: memory is in the `InkStroke`s (WinUI fact), so a page far away hands its strokes to the
  app (`store`, rebuilt in page coordinates with `InkStrokeBuilder.CreateStrokeFromInkPoints`) and they leave the
  presenter; near again, `load` gives them back (rebuilt at the page's place). Pages within one view height come in,
  beyond two view heights go, `Hold` keeps a page (`LivePages`). Sandbox: 5 strokes on page 1 released at page 11 and
  restored at Home.
- **Ink lands under the pen** at fit width (zoom 1.15), after a finger pan, after a pinch to 3.66x, after Ctrl+wheel /
  wheel, at zoom 4 and on page 2 after Page Down: the first/last ink point mapped back through DirectManipulation's
  transform was within 0.3 DIP of the injected pen position every time. Ink is drawn sharp at every zoom (the presenter
  renders it, no bitmap). The presenter's visual sits inside DirectManipulation's content and follows its transform for
  drawing and input by itself.
- Pen + finger at once: the pen keeps writing while a finger pans (the stroke follows the moving paper).
- Wheel: `SetContact(DIRECTMANIPULATION_MOUSEFOCUS)` + `ProcessInput` (Microsoft's sample): the wheel scrolls,
  **Ctrl+wheel zooms** (DirectManipulation does both itself; sandbox: 3.66 → 4.79 with two notches). Precision
  touchpad: `DM_POINTERHITTEST` → `SetContact` – not testable in the sandbox (no synthetic touchpad); check on the
  device.
- `ZoomToRect` does not keep the rectangle on the content (zooming out at the top left the document 144 px below the
  view's top): `KeepOnContent` does.
- Paper: D2D into a DComp virtual surface per sheet at DPI x zoom, only near the view (`Trim`), redrawn when a zoom
  settles (READY); while a pinch runs the last drawing scales. Lines are page units, so they get thicker when zoomed,
  like printed paper. A4 at 6x and 150 % is a 5357x7578 px virtual surface – only the part in view is allocated.
- Platform setup facts: the presenter needs visuals of a `DCompositionCreateDevice3` device (a `DCompositionCreateDevice`
  visual: E_NOINTERFACE), DirectManipulation's compositor needs `IDCompositionDevice` – request that interface from
  `DCompositionCreateDevice3`; create it from the D2D device so surfaces hand out `ID2D1DeviceContext`. The presenter
  works in the units of its visual (pixels unless a parent scales – the content visual scales DIPs to pixels). Nothing
  inks before the app commits its DComp device after the presenter exists, and an opaque sibling above the ink visual
  takes its pen input (`AddVisual(v, FALSE, nullptr)` = on top).
- Size/start (demo): one exe of ~1 MB, 59 modules (all Windows'), working set ~60–68 MB (D3D/D2D/DComp/DM + ink);
  window ~0.1 s after start when warm; the very first start of a new exe waited ~14–24 s before the process existed
  (Defender scanning the new file, as with the WinUI build).

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
