# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.
General Windows facts (how to work on Microsoft tech, Windows.Data.Pdf, Windows Sandbox / `wsb`, synthetic input,
GitHub runners, cdb, Segoe Fluent Icons) are in the user's global CLAUDE.md. **This file is the home of the facts about
Windows ink, the Win32 ink presenter, DirectComposition, DirectManipulation, Direct2D, comctl32 toolbars, windowless
RichEdit and UI Automation that every app embedding InkPages needs**; app-only facts stay in the app's CLAUDE.md.

## What this is

**InkPages**: the reusable, pen-first writing area for Win32 apps – Windows ink through the Win32 ink presenter
(`IInkDesktopHost`/`IInkPresenterDesktop`) in DirectComposition, finger/touchpad pan and zoom with DirectManipulation,
the tools (pens, highlighters, exact and whole-stroke eraser, selection, shapes and recognition, ruler/protractor,
text, auto-scroll, undo) and the toolbar (comctl32). C++20, plain Win32 + C++/WinRT, CMake. Public repository; the
README is the user-facing documentation (API, layout, device checks).

Embedded by Xournal Studio for Windows as the submodule `external/InkPages` (it builds only the `InkPages` target and
uses `UiTests/` and `tools/uiaprobe` for its UI tests). Since 2026-11-19 InkPages is C++/Win32; the C# WinUI 3 version
(`InkPages/`, `InkPages.Demo/`, `InkPages.Tests/`, the WinUI fork runtime scripts) lives only at the tag
**`winui-final`** – MatheTrainer pins it until it moves to Win32; never move or delete the tag. Its WinUI platform facts
are in the global CLAUDE.md ("WinUI 3"); what it taught that still holds is folded in below.

## Rules (agreed with the user)

- **Nativ, schlank, modern Win32** (user, 2026-11-19). C++20, plain Win32, C++/WinRT for WinRT APIs
  (Windows.UI.Input.Inking, InkAnalyzer). comctl32 v6 with visual styles (Windows 11 look, normal mouse sizes – the pen
  operates them), user32 menus, DirectComposition/Direct2D/DirectWrite/WIC, PerMonitorV2, static CRT. Few dependencies,
  only where Windows has nothing, statically linked. No UI framework on top, no XAML islands – no Frankenstein.
- **Microsoft's APIs do the work; the library only wires them up.** The ink presenter collects, renders, selects and
  erases ink; DirectManipulation pans and zooms; the Windows ruler draws straight lines; `InkAnalyzer` recognizes shapes.
  Follow Microsoft's docs and samples (Windows-classic-samples: TouchInputDirectManipulation, ink). When an API is
  broken, find the root cause and report the options. Agreed exception: the exact eraser (Windows ink erases whole
  strokes only).
- **Never swallow errors.** Log kind, HRESULT and message (`inkpages::LogHr`, `inkpages::LogException` inside a catch);
  every boundary (window procedure, callbacks from Windows, threads) catches and logs.
- **No app dependencies.** No file format, no app texts (apps pass their language). Small classes with one job; logic
  without UI is unit-tested.
- Windows 11 (26200) only, x64. Right-handed: nothing interactive at the right edge. Touch and touchpad never draw: they
  pan and zoom; the pen writes. A mouse left drag selects with the selection tool, or pans and selects PDF text in
  viewer mode. A right drag lassos with any tool, and a right click opens the app's context menu. A click on a picture
  selects it. A click with the Text tool writes or edits a text, otherwise a click is the app's tap (links).
  No drawn-out text boxes (user, 2026-11-19): upstream `<text>` has no width, a drawn frame could not be kept.
- Code, comments and commits in English. Breaking API changes: update Xournal Studio in the same session (commit and
  push here, then bump the submodule there). A WinUI fix for MatheTrainer would go on a branch from `winui-final`,
  announced first.
- One Windows Sandbox at a time on the machine: Xournal's `UiTests\Run-AppTest.ps1` holds the named mutex
  `Global\XournalStudioSandbox`; this repository's runner takes no lock, so check `wsb list` is empty before starting it.

## Build, test

```powershell
.\build.ps1            # Visual Studio's CMake, Release build + unit tests (CTest); demo -> .local-build\demo,
.\build.ps1 -NoTests   # uiaprobe/cursorprobe -> .local-build\tools
UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\<Test>.ps1 -Data .local-build\tools\uiaprobe.exe
```
- CI (`.github/workflows/build.yml`, `windows-2025`): every push and PR runs `build.ps1`.
- Unit tests: GoogleTest (fetched by CMake at configure time, pinned; only for tests). Microsoft's CppUnitTest was tried
  first: vstest.console found no tests in the CMake-built DLL (2026-11-19, not pursued).
- UI tests in Windows Sandbox on a desktop of their own (the mechanism and why: global CLAUDE.md "Diagnostics and UI
  testing"; `-Show` puts the window on the user's desktop – tell the user first). The runner checks every 2 s with
  `EnumWindows` that no sandbox window is visible on the user's desktop. In-sandbox scripts dot-source
  `C:\sbx\TestKit.ps1` (start the app, UI Automation, synthetic pen/touch/mouse/keys with `Inj.cs`). The demo logs to
  `INKPAGES_DEMO_LOG` (TestKit sets `out\demo.log`). Embedding apps use the same runner and TestKit.

## Structure (modules)

Folders are modules; each has a narrow interface and **dependencies point downwards only**:

```
toolbar  ->  surface  ->  text, tools  ->  ink  ->  render  ->  model        (base: Log, used by all)
```

- `include/inkpages/` – the public API: `WritingSurface.h` (surface, tools, callbacks), `Toolbar.h`, `Pictures.h` (the
  app's pictures in the selection), `Texts.h` (the app's texts for the Text tool), `Log.h`; `include/inkpages/model/` –
  pure data and logic the API uses (`Paper`, `PageLayout`, `ViewMath`, `Pens`, `Geometry`, `StrokeEraser`,
  `UndoHistory`, `StrokeKeys`, `PageMap`, `PathData`, `TapGesture`, `ClickCount`, `Text`).
- `src/base` – `Log`.
- `src/model` – pure logic, no Win32/WinRT (unit-tested: `tests/GeometryTests`, `ToolTests`, `ToolGeometryTests`,
  `StrokeKeysTests`): page layout, view math (zoom, fit width, zoom rectangles kept on the content, lazy pages), paper
  lines, pens and tool choices (text round trip), the exact eraser's geometry, affine transforms, the selection frame
  and handles, taps, clipping at a page's edge, shape outlines, the undo history (+ CompositeEdit, save point), stroke
  keys.
- `src/render` – D3D11/D2D/DComp: `Graphics` (devices), `PaperLayer` (a sheet: color, paper, the app's underlay into a
  DComp virtual surface), `Overlay` (tools' feedback below the ink: circle, lasso, frame, shape), `PageMark` (outline of
  the page in view).
- `src/ink` – the ink presenter and its state (ink thread): `InkHost` (IInkDesktopHost, work items), `UiThread` (work
  handed to the UI thread), `InkLayer` (presenter, pens, mode, cursor, collected/erased strokes into the history,
  strokes cut at the pages' edges), `InkState` (container, stroke -> page, stroke keys), `Strokes` (make/transform
  strokes), `StrokeEdit` (an undo step of strokes), `History`, `PageStore` (lazy pages, snapshots with keys).
- `src/tools` – one unit per tool (ink thread): `ToolInput` (routes unprocessed input and hover, presenter mode,
  eraser-end, selection and text-box takeovers, taps beside the selection, recognition), `ExactEraser`, `InkSelection`
  (select/preview/commit/delete/duplicate/clipboard of strokes), `ShapeTool`, `ShapeRecognizer` (InkAnalyzer),
  `Stencils` (ruler, protractor), `PictureEdit` / `TextEdit` (the app's pictures / a text in an undo step).
- `src/text` – editing one text (UI thread): `RichEditBox` (windowless RichEdit: ITextServices2 + our ITextHost2,
  TxDrawD2D; headless unit tests `tests/RichEditBoxTests`), `TextBoxVisual` (its DComp visual, caret, frame).
- `src/surface` – the window (UI thread): `Surface` (implements WritingSurface, wires the modules, hands work between
  threads), `SurfaceWindow` (window procedure), `SurfaceAutomation` (UI Automation root), `Composition` (visual tree,
  sheets, page layers, overlay), `Viewport` (DirectManipulation), `SelectionController` (selection with the app's
  pictures, window pointer drags, auto-scroll, commands), `MouseSelect`, `TouchTaps`, `TextEditor` (the Text tool).
- `src/toolbar` – `Toolbar` (comctl32 toolbar in its own container window), `ToolbarModel` (buttons, states, tips –
  no Win32, unit-tested), `ToolbarMenus` (popup menus), `Icons` (Segoe Fluent Icons glyphs, swatches, path icons).
- `demo/` – `main.cpp` (only the public API; German texts), `DemoPictures` (cards and texts in page layers: the picture
  and text extension points), `cursors/` + `tools/make_cursors.py` (a dot cursor per palette color × width step, ids
  1000/2000 + color × 10 + step), `UiTests/` (`StartTest`, `SurfaceTest`, `BoundaryTest`, `ToolsTest`, `SelectionTest`,
  `RulerTouchTest`, `PagesTest`, `MouseTest`, `TextTest`, `KeyboardTest`, `MarkTest`). `tools/cursorprobe.cpp`: DXGI
  Desktop Duplication probe – the pointer as the screen draws it.

## Windows ink (Windows.UI.Input.Inking)

- [doc] A stroke cannot be in more than one container; a stroke passed to `AddStroke` must come from `Clone()`,
  `InkStrokeBuilder` or the ink manager – undo of a removal re-adds a `Clone()`:
  https://learn.microsoft.com/uwp/api/windows.ui.input.inking.inkstrokecontainer.addstroke
- [doc] `InkStroke.PointTransform` is an affine matrix applied to the stroke:
  https://learn.microsoft.com/uwp/api/windows.ui.input.inking.inkstroke.pointtransform. [measured] On a stroke in the
  presenter's container it redraws live (move/scale previews); the pen width is not scaled; Windows ink's clipboard
  copy carries the transform.
- [doc] `InkPoint.Position` is in DIPs relative to the inking area:
  https://learn.microsoft.com/uwp/api/windows.ui.input.inking.inkpoint.position (the Win32 presenter: the units of its
  visual, see below).
- [measured] Width at pressure p (0..1) for size s is about `s * (0.25 + 1.5 p)`: s at half, 1.75 s at full pressure.
  The library's widths are the width at full pressure (Xournal++'s meaning): size = width /
  `InkWidth::FullPressureFactor` (1.75); shapes have no pressure and use the full width. The user's Wacom AES pen
  delivers the full range (handwriting median ~0.55).
- [measured] `FitToCurve = true` (the docs' sample default) turns few-point polylines (rectangles, zigzags) into round
  curves: strokes are polylines (`false`).
- [measured] Memory is in the `InkStroke` objects (40-page document, 26.5k strokes: ~230 MB) – hence lazy pages.
- [doc] Windows ink's eraser erases whole strokes; there is no point eraser (proposal: microsoft-ui-xaml #1530).
- [doc] Pass-through input: `InputProcessingConfiguration.Mode` (Inking/Erasing/None) and `RightDragAction =
  LeaveUnprocessed` (barrel / right button) send input to `InkPresenter.UnprocessedInput` (Pressed/Moved/Released/
  Entered/Hovered/Exited/Lost) for app processing such as lasso selection (`SelectWithPolyLine`):
  https://learn.microsoft.com/windows/uwp/ui-input/pen-and-stylus-interactions,
  https://learn.microsoft.com/uwp/api/windows.ui.input.inking.inkpresenter.unprocessedinput
- [doc] `InkInputConfiguration.IsPrimaryBarrelButtonInputEnabled` / `IsEraserInputEnabled` control whether the presenter
  processes barrel / eraser-tip input: https://learn.microsoft.com/uwp/api/windows.ui.input.inking.inkinputconfiguration
- [doc] `Windows.UI.Core.PointerEventArgs.GetIntermediatePoints` documents no order -> sorted by `Timestamp`
  (`ToolInput`).
- [measured] The pen's eraser button makes the pen re-enter as a new pointer, inverted (`IsInverted`; touching:
  `IsEraser`); the barrel reads as barrel and right button. Switching only the presenter's mode during a hover is safe.
- [measured] Windows ink draws highlighter strokes (`DrawAsHighlighter`) below the pens' ink (a black stroke stays
  visible over a later yellow highlighter).
- [measured] `InkAnalyzer` works unpackaged; for ellipses `InkAnalysisInkDrawing.Points` are the ends of both axes
  (0-2, 1-3).

## Win32 ink presenter, DirectComposition, DirectManipulation

- [doc] Win32 apps host Windows ink with `IInkDesktopHost`/`IInkPresenterDesktop` (inkpresenterdesktop.h) in their own
  DirectComposition tree: https://learn.microsoft.com/windows/win32/api/inkpresenterdesktop/. [doc] Its
  `CoreInkIndependentInputSource.PointerCursor` takes system or resource cursors only (no runtime colors) – the library
  takes the pen cursor from the app's exe resources (`cursorFor`):
  https://learn.microsoft.com/uwp/api/windows.ui.input.inking.core.coreinkindependentinputsource.pointercursor
- [measured] Setup: the presenter needs visuals of a `DCompositionCreateDevice3` device (a `DCompositionCreateDevice`
  visual: E_NOINTERFACE); DirectManipulation's compositor needs `IDCompositionDevice` – request it from
  `DCompositionCreateDevice3`; create the device from the D2D device so surfaces hand out `ID2D1DeviceContext`. The
  presenter works in the units of its visual (pixels unless a parent scales – the content visual scales DIPs to pixels).
  Nothing inks before the app commits its DComp device after the presenter exists; an opaque sibling above the ink
  visual takes its pen input (`AddVisual(v, FALSE, nullptr)` = on top) – so feedback (eraser circle, lasso, frame) is
  drawn below the ink.
- [measured + cdb, 2026-11-19] **Setting `PointerCursor` while Windows still activates a presenter's input context kills
  the process** (fail-fast 0xC0000409 in Windows.UI.Input.Inking: `put_PointerCursor` -> `_RunOnInkThreadSync` ->
  E_UNEXPECTED while the internal ink thread runs `CInputContext::Activate`, queued when the presenter and its input
  source are created). No API reports the end of the activation (the docs say nothing); the observable signal is the
  input source's first pointer event (Entering/Hovering/Moving) – input only arrives through an active context. The
  library sets the cursor there (on Windows' input thread) and afterwards when the tool changes; no timer. It hit
  reliably when presenters were created while others had cursors; 30 starts of one presenter with the cursor set right
  after creation never crashed.
- **One ink presenter for the whole document** (decided by measurement, 150 %): one presenter over 3, 40 or 100 A4 pages
  is created in ~5 ms (first in a process ~40 ms with the host thread), working set unchanged by document size (50.1 MB;
  the presenter has no surface of its own size). One per page: 2.5 ms and +0.36 MB each – and lazy per-page creation
  would gamble on the activation timing above. One presenter = one activation, no input switching.
- Presenter state lives on the host thread: input events (Windows' input thread) and UI calls queue work items
  (`OnInkThread`, inline when already there). `InkHost::Run` carries the work's exception to the caller and throws on
  its timeout; shutdown waits without a timeout (the work refers to the surface).
  `Surface` stores its `InkLayer` before calling `Start`, because the ready callback runs on the ink thread and
  uses that member.
- [measured, 2026-11-20] Dropping the last ink host reference shuts its thread down asynchronously. Ending the
  creating apartment immediately afterward can unload Windows.UI.Input.Inking.dll while the thread is still running
  (0xC0000005, reproduced by the InkHost regression test). The host keeps a synchronizable thread handle and waits
  after releasing the COM object. The thread's shutdown timing is observed behavior.
- [measured] The presenter's visual sits inside DirectManipulation's content and follows its transform for drawing and
  input by itself: **ink lands under the pen** within 0.3 DIP at fit width, after finger pan, pinch to 3.66x,
  Ctrl+wheel, zoom 4 and on page 2; ink is drawn sharp at every zoom (no bitmap). The pen keeps writing while a finger
  pans (the stroke follows the paper).
- [measured] The presenter takes only the pen (`InputDeviceTypes` = Pen, set at creation). Rewriting `InputDeviceTypes`
  during a hover works on the Win32 presenter (the selection and text-box takeovers set None while the pen hovers the
  frame/box, so the window gets WM_POINTER*; sandbox `SelectionTest`, `TextTest`) – in WinUI it dropped the pointer.
- [doc] Mouse messages Windows makes from pen/touch carry the signature 0xFF515700 in `GetMessageExtraInfo` (masked with
  0xFFFFFF00); the window ignores them except while a text box is edited (they reach RichEdit).
- DirectManipulation: wheel = `SetContact(DIRECTMANIPULATION_MOUSEFOCUS)` + `ProcessInput` (Microsoft's sample) – the
  wheel scrolls, **Ctrl+wheel zooms** (DM does both; sandbox 3.66 -> 4.79 with two notches). Precision touchpad:
  `DM_POINTERHITTEST` -> `SetContact` – not testable in the sandbox (no synthetic touchpad). [measured] `ZoomToRect` does
  not keep the rectangle on the content (zooming out at the top left the document 144 px low): `KeepOnContent` does.
  [measured] After `SetContact` the window still gets WM_POINTERUPDATE/UP of the contact; a tap leaves DM READY, a pan
  goes RUNNING within ~40 ms of the down, which cancels the tap (`TouchTaps`; WM_POINTERCAPTURECHANGED cancels too).
- Paper: D2D into a DComp virtual surface per sheet at DPI × zoom, only near the view (`Trim`), redrawn when a zoom
  settles (READY); during a pinch the last drawing scales. Lines are page units (thicker when zoomed, like paper). A4 at
  6x and 150 % is a 5357x7578 px virtual surface – only the part in view is allocated.
- [measured] `InkPresenterRuler(presenter)` / `InkPresenterProtractor(presenter)` work on the Win32 presenter
  (documented constructors). Without a transform the ruler appeared at the document-sized presenter's top right, out of
  view: the library places it in the view when shown (`Transform`). [doc] The stencil has no property to stay in view
  (Length, Width, AreTickMarksVisible, IsCompassVisible, colors, Kind, IsVisible, Transform): it lies on the document
  and scrolls with the pages. [measured, `RulerTouchTest`] A finger drag on the ruler moves the ruler and does not pan
  (the stencil takes touch although `InputDeviceTypes` has none); beside it the finger pans.
- Size/start (demo): one exe of ~1 MB, 59 modules (all Windows'), working set ~60–68 MB; window ~0.1 s after a warm
  start.

## comctl32 toolbar, RichEdit, UI Automation

- Toolbar: comctl32 (flat, tooltips via TBN_GETINFOTIP, BTNS_CHECK + BTNS_DROPDOWN, 16 DIP images)
  in its own container window (its notifications stay in the library); preset menus are user32 popup menus
  (a swatch bitmap replaces the check mark, so the current color is bold) – windowed popups, pen taps reach them. A
  second tap on the active preset opens its menu (WinUI behavior). Numbers use `ToolbarTexts::decimalSeparator`
  ("0,85 pt"). InkPages owns the shared icon set. The embedded `assets/toolbar-icons.png` atlas has five columns
  and four rows of 64 px tiles in `ToolbarSymbol` order. WIC scales the selected tile to the toolbar's current DPI.
- [measured 2026-11-20] After setting a hidden toolbar's bitmap size to 16 or 24 pixels, `TB_LOADIMAGES` with
  the matching standard set returned zero on its first call. `TB_GETIMAGELIST` returned the requested dimensions and
  all 15 images. Verify the list and its count to check that the load succeeded.
  Destroy the image list after destroying the temporary toolbar. In a 400-toolbar probe, destroying only the toolbar
  leaked five GDI objects per instance; destroying the list as well kept GDI and USER object counts constant.
- [measured 2026-11-19] comctl32 exposes a button's text as its MSAA name; with `TBSTYLE_EX_MIXEDBUTTONS` the text is not
  drawn without `BTNS_SHOWTEXT`, so every button gets its name as text and the tooltip stays the long tip. Native UIA
  (as Narrator) shows a ToolBar with Button/CheckBox children and names; no automation ids (MSAA has none). UI tests
  find buttons with `tools/uiaprobe.exe` (native UIA; see global CLAUDE.md on the managed client).
- **Windowless RichEdit for the Text tool** (technique from the probe `D:\src\feature-probes\textedit`): msftedit.dll
  `CreateTextServices`, ITextServices2 + our ITextHost2, plain text, multi-line, no wrapping, `TxDrawD2D` into a DComp
  surface in the text layer (above page layers, below overlay and ink; pans and zooms with the pages). [measured,
  probe] The render target's DPI must be the window's; TxDrawD2D's bounds are surface pixels, while client rectangle,
  mouse and caret are client pixels – so the box is laid out at the settled zoom: font = `RichEditText::Twips(size,
  zoom)` = size × zoom × 72/96 points. [measured, unit test] `OnTxPropertyBitsChange` must announce the host's bits after
  `OnTxInPlaceActivate` (before, Enter made no new line); the char format goes to the text with `EM_SETCHARFORMAT
  SCF_ALL` too; `TxGetNaturalSize` needs the extent in HIMETRIC of the offered size ({-1,-1} gave sizes ~130x too large).
  Enter arrives as WM_KEYDOWN + WM_CHAR. The box draws its caret (`GetCaretBlinkTime`); the system caret (`CreateCaret`,
  hidden under the composition) is set too for Magnifier/Narrator.
- **UI Automation of the text box** (sandbox `KeyboardTest`), Microsoft's route for a windowless RichEdit ([doc]
  https://learn.microsoft.com/windows/win32/api/textserv/nn-textserv-iricheditwindowlessaccessibility):
  `SurfaceAutomation` answers WM_GETOBJECT (UiaRootObjectId) with a fragment root on the window
  (`UiaHostProviderFromHwnd`); while a text is edited its only child is RichEdit's provider from
  `IRicheditWindowlessAccessibility::CreateProvider` with our site (`IRawElementProviderWindowlessSite`: parent = root,
  runtime id {UiaAppendRuntimeId, 1}; `IRichEditUiaInformation`: screen box, visible). Root `GetFocus` = the box while
  the window has focus; focus-changed raised when editing begins, the provider disconnected when it ends; no
  `UiaDisconnectProvider` on the root (E_INVALIDARG at exit). [measured, `uiaprobe --focus`] Focused element while
  editing: control type Document (50030), "RichEdit Control", keyboard focus, text pattern with the typed text, the box's
  bounds. Include order: `ole2.h` before `uiautomation.h` (WIN32_LEAN_AND_MEAN), `uiautomation.h` and `richedit.h`
  before `textserv.h`.
- **Touch keyboard:** [doc] it comes up when the user taps into the focused element with an editable text pattern (not
  on programmatic focus); a hardware keyboard keeps it away unless "Show the touch keyboard" is "Always". [measured,
  sandbox] It did not show, also with `TabletTip\1.7\TouchKeyboardTapInvoke = 2` (TextInputHost stayed cloaked): the
  sandbox has a keyboard and only virtual pen/touch – a device check (README "Device checks"). During editing
  `IFrameworkInputPane::AdviseWithHWND` watches the desktop pane's occupied screen rectangle. [measured] Unregister
  on edit end while the root window is still alive. Unregistering at window teardown returned `E_INVALIDARG`.
  The surface shortens
  its DirectManipulation viewport and reveals the edited box. Documented explicit route
  if auto-invoke fails: `IInputPaneInterop::GetForWindow` -> `InputPane.TryShow()` (the probe saw true with Visible =
  false while a keyboard was attached) – not built.

## The library's behavior (measured in the sandbox, 2026-11-19)

- **Strokes never cross a page:** a stroke belongs to the page of its first point and is cut at the page's edge when
  collected, moved, pasted or recognized (`InkState::Split`, `ClipToRect`); pieces in the gap are dropped (`BoundaryTest`).
- **Lazy pages = lazy strokes:** a page far away hands its strokes to the app (`store`, rebuilt in page coordinates with
  `InkStrokeBuilder.CreateStrokeFromInkPoints`) and they leave the presenter; near again, `load` gives them back. Pages
  within one view height come in, beyond two go; `Hold` keeps a page; pages with history stay (an edit refers to their
  strokes). **Stroke keys** (`StrokeKeys`, `KeyedStroke`): load/store/StrokesOf carry the app's key and the transform
  since loading, followed through `Clone()`, moves, lazy copies; new ink, erased pieces, cuts, duplicates, pastes have
  key 0. `StrokesOf` returns none for a page not in the presenter and throws when the ink thread fails or does not answer.
- **Pens:** size = width / 1.75, pressure on, `FitToCurve` off, round tip. Highlighters: `DrawAsHighlighter`, no
  pressure, size = width.
- **Eraser:** whole strokes = presenter mode Erasing (`StrokesErased` -> undo edit); exact = mode None, `UnprocessedInput`
  drives `ExactEraser`, one undo step per drag. Eraser end: with whole-stroke erasing Windows erases in mode Inking by
  itself; with exact erasing it is taken on its inverted hover (input source Hovering with `IsInverted`, only the mode
  switched to None) and given back when the tip hovers – exact erasing works from the first touch. The circle is a D2D
  ring below the ink.
- **Undo:** one `UndoHistory` on the ink thread; edits are removed/added strokes; undo re-adds `Clone()`s and the edit
  holds the copies from then on; earlier edits name the original, so `InkState::Current` follows `Replaced` copies
  (`tests/StrokeEditTests`, real Windows ink objects). Undo/redo clear the selection.
- **Selection:** lasso/rectangle on unprocessed input (the barrel lassos with any tool outside an existing selection); strokes via the container's
  `SelectWithPolyLine` (its `Selected` flags are reset at once – the library keeps the selection and uses `Selected`
  only as the removal mark), the app's pictures by `PictureHost::PicturesIn`; a tap on no stroke selects the picture
  under the pen (`PictureAt`). Moving, scaling and rotating use window pointer input (the pen hovering over the frame is taken from the
  presenter). Preview: `PointTransform` + `PictureHost::Preview`, frame and ink changed in one ink-thread work item
  (`ToolInput::PreviewSelection`; hit-testing uses the committed frame until the drag ends); commit: transformed
  strokes replace the originals (width unchanged), cut at page edges, one undo step with the pictures
  (`CompositeEdit`). An inside tap keeps the selection, an outside tap clears it (the pen dot is dropped). Near the
  view's edge the drag scrolls (16 ms timer = one frame). Handles keep their screen size when the zoom changes. The
  frame has inline rotation, delete and duplicate handles. Inside it, a barrel tap duplicates and a barrel drag moves.
- **Shapes** on unprocessed input (preview in the overlay), one stroke of even width. **Recognition:**
  `InkAnalyzer.AnalyzeAsync` (Completed handler, result posted to the ink thread); the freehand stroke is replaced by
  the outline as its own undo step.
- **Mouse and taps** (`MouseTest`): `SelectionController` gets a left press first (move/scale on the frame, a press
  beside clears); else `MouseSelect` starts a drag at the DPI-aware system drag threshold. With the selection tool
  left drags select using its mode. In viewer mode they pan or select app content. Right drags always lasso.
  A left click selects the picture under it or, with the Text tool, writes/edits a text, else it is the
  app's `tapped`. `TapGesture` (unit-tested) decides taps; taps are measured on the screen (`Tap::MaxDistanceDip` /
  zoom); a finger that stops the inertia is no tap (device check).
- **The app's mouse gestures** (2026-11-20, for Xournal Studio's PDF text selection): `mouseClaims(page, point)` +
  `mouseGesture`. `MouseSelect` asks the app (through `Surface::AppClaims`: not on the selection's frame, a
  `PictureHost::PictureAt` picture or the edited text box) at a left press in viewer mode; ink is transparent to it.
  A claimed drag or double/triple click goes to the app. `ClickCount` uses Windows' double-click time and DPI-aware
  rectangle. Single clicks stay the library's (pictures, Text tool, `tapped`). The hover cursor is a separate hook.
- **Touch in Auswählen:** `InkStrokeContainer::SelectWithLine` picks ink at the tap; the picture host handles
  pictures and texts. The selection's frame can take the primary touch without assigning it to DirectManipulation.
  A second touch cancels the selection preview and assigns both contacts. `TouchTaps` watches the native
  Interaction Context secondary tap while DirectManipulation has not claimed the contact, and reports it to the app.
- **Text tool** (`TextTest`): mode None; a pen press without a drag (farthest movement under the tap distance, however
  long it rests – real pens hold deliberate taps longer than 300 ms) goes to `TextEditor::Tap`; while a box is edited the
  pen over it is taken from the presenter (`ToolInput::SetTextBox`) and its presses reach RichEdit as mouse messages; a
  finger tap into the box places the caret. The window takes focus while editing and gives it back. End: tap beside,
  Escape, another tool, undo/redo, a selection, a page operation, `SetTextHost`, the surface going. Result to the app's
  `TextHost` (`Add`/`Change`/`Remove`; an empty new text is nothing, an emptied text is removed) and the history
  (`TextEdit`); `EndTextEditing` returns once the edit is in the history. Logged along the path (`tools: text
  press/release`, `text: tap at ...`, why nothing opens).
- **Page operations** (`InsertPages`, `RemovePage`, `MovePage`; `PagesTest`): one `PageMap` (new index by old, -1 =
  removed) drives `UndoHistory::RemapPages` (edits of a removed page dropped), the strokes of moved pages rebuilt at
  their place (`InkState::BeginRelocation`/`Relocated`, keys follow), `PageStore::Remap`, `Composition::Rearrange`
  (visuals reused, sheets keep their drawing via `PaperLayer::MoveTo`); running gestures and the selection end in the
  same work item; a tool result made before the ink thread applied every issued page operation is dropped (generation
  counted on both threads).
- **Page in view outlined** (`MarkCurrentPage`, `MarkTest`): as the WinUI version (MarkGap 3, MarkThickness 2,
  CornerRadius 4) – a 2-unit outline 3 units around the page, rounded, in `UISettings.GetColorValue(AccentDark1)` (what
  AccentFillColorDefaultBrush is in the light theme), re-read on `ColorValuesChanged` (worker thread, posted). Four
  strips in the mark layer below the sheets at the settled zoom's resolution, following `PageInView` (120 ms motion
  tick).
- **Underlay:** the app draws under the ink with D2D (`drawUnderlay(page, dc, area)`, page coordinates) into the sheet's
  virtual surface; `InvalidatePage` redraws; page layers (`PageLayer`) are DComp visuals for live content.
- **App clipboard:** `copyToClipboard` gets the clip before Copy returns (Cut deletes after the app took it; false =
  nothing copied, Cut keeps the selection); `pasteFromClipboard` returning nothing pastes Windows ink.
