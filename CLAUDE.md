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
cursor, lazy pages), demo, unit tests and sandbox tests (see "Writing surface" below).
Phase 2a (2026-11-19): toolbar, pens, highlighters, eraser (whole strokes and exact), the pen's eraser end, undo/redo
(see "Tools" below). Phase 2b/2c (2026-11-19): selection with the app's pictures, shapes, recognition, ruler/protractor,
strokes cut at page edges, stroke keys, page underlay (see "Selection, shapes, ruler"); the library is split into
modules (see "Structure").

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
  pan and zoom the sheet; the pen writes; the mouse never writes (user, 2026-11-19): it selects and taps links.
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
- UI tests in Windows Sandbox, on a desktop of their own (user, 2026-11-19: the sandbox window must not appear while
  they work): `UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\<Test>.ps1` (`-Show`: the window
  on the user's desktop to watch or debug – tell the user first). How: [doc] `wsb exec -r ExistingLogin` needs a user
  session, which only `wsb connect` (a remote desktop window) creates
  (https://learn.microsoft.com/windows/security/application-security/application-isolation/windows-sandbox/windows-sandbox-cli);
  [measured] without it exec fails with 0x80070520 (no logon session); `wsb start` alone shows no window. So the runner
  creates a Win32 desktop (`UiTests\HiddenDesktop.cs`: `CreateDesktop` + `STARTUPINFO.lpDesktop`, [doc]
  https://learn.microsoft.com/windows/win32/winstation/desktops) and starts `wsb connect` on it; the client
  (`WindowsSandboxRemoteSession.exe`, wsb's child) opens its window there. [measured 2026-11-19] Inside the sandbox
  nothing changes: 1514x900 at 144 DPI, screenshots, synthetic pen/mouse/keys, UI Automation (PagesTest: same log, uia
  tree and screenshots as with the window shown; only the app window's default position differs). The runner checks
  every 2 s with `EnumWindows` that no sandbox window is visible on the user's desktop and prints the result. Not
  virtual desktops: no public API creates one, and [measured] `IVirtualDesktopManager::MoveWindowToDesktop` on another
  process' window returns E_ACCESSDENIED (own window: S_OK). In-sandbox scripts dot-source
  `C:\sbx\TestKit.ps1` (start the app, UI Automation, synthetic pen/touch with `Inj.cs`: hover ~100 ms before a press,
  a sample every ~4 DIPs). The demo logs to `INKPAGES_DEMO_LOG` (TestKit sets `out\demo.log`).
- Synthetic input fails with ERROR_ACCESS_DENIED while the host session is locked (Win32 demo, 2026-11-19).

## Structure (modules)

Folders are modules; each has a narrow interface and **dependencies point downwards only**:

```
toolbar  ->  surface  ->  text, tools  ->  ink  ->  render  ->  model        (base: Log, used by all)
```

- `include/inkpages/` – the public API: `WritingSurface.h` (the surface, its tools and callbacks), `Toolbar.h`,
  `Pictures.h` (the app's pictures in the selection), `Texts.h` (the app's texts for the Text tool), `Log.h`; `include/inkpages/model/` – pure data and logic the
  API uses (`Paper`, `PageLayout`, `ViewMath`, `Pens`, `Geometry`, `StrokeEraser`, `UndoHistory`, `StrokeKeys`, `PageMap`, `PathData`, `TapGesture`, `Text`).
- `src/base` – `Log`.
- `src/model` – pure logic, no Win32/WinRT (unit-tested: `tests/GeometryTests`, `ToolTests`, `ToolGeometryTests`,
  `StrokeKeysTests`): page layout, view math (zoom, fit width, zoom rectangles kept on the content, lazy pages), paper
  lines, pens and tool choices (text round trip), the exact eraser's geometry, affine transforms, the selection frame
  and its handles, taps, clipping at a page's edge, shape outlines, the undo history (+ CompositeEdit, save point),
  stroke keys.
- `src/render` – drawing with D3D11/D2D/DComp: `Graphics` (devices), `PaperLayer` (a sheet: color, paper, the app's
  underlay into a DComp virtual surface), `Overlay` (tools' feedback below the ink: circle, lasso, frame, shape), `PageMark` (the outline of the page in view).
- `src/ink` – the ink presenter and its state (ink thread): `InkHost` (IInkDesktopHost, work items), `UiThread` (work
  handed to the UI thread), `InkLayer` (presenter, pens, mode, cursor, collected/erased strokes into the history,
  strokes cut at the pages' edges), `InkState` (container, stroke -> page, stroke keys), `Strokes` (make/transform
  strokes), `StrokeEdit` (an undo step of strokes), `History`, `PageStore` (lazy pages, snapshots with keys).
- `src/tools` – each tool in its own unit (ink thread): `ToolInput` (routes unprocessed input and hover, presenter
  mode, eraser-end and selection takeovers, taps beside the selection, recognition), `ExactEraser`, `InkSelection`
  (select/preview/commit/delete/duplicate/clipboard of strokes), `ShapeTool`, `ShapeRecognizer` (InkAnalyzer),
  `Stencils` (ruler, protractor), `PictureEdit` (the app's pictures in an undo step), `TextEdit` (a text in an undo
  step).
- `src/text` – editing one text (UI thread): `RichEditBox` (windowless RichEdit: ITextServices2 + our ITextHost2,
  TxDrawD2D; headless unit tests `tests/RichEditBoxTests`), `TextBoxVisual` (its DComp visual in the text layer,
  caret, frame).
- `src/surface` – the window (UI thread): `Surface` (implements WritingSurface, wires the modules, hands work between
  threads), `SurfaceWindow` (window procedure: input to the module it belongs to), `Composition` (visual tree, sheets,
  page layers, overlay), `Viewport` (DirectManipulation), `SelectionController` (selection with the app's pictures,
  window pointer drags, auto-scroll, commands), `MouseSelect`, `TouchTaps`, `TextEditor` (the Text tool: begin/end,
  keyboard and the pen's mouse messages to the RichEdit, the result to the app's TextHost and the history).
- `src/toolbar` – `Toolbar` (comctl32 toolbar in its own container window), `ToolbarModel` (buttons, states, tips –
  no Win32, unit-tested), `ToolbarMenus` (popup menus), `Icons` (Segoe Fluent Icons glyphs, swatches).
- `demo/` – `main.cpp` (only the public API; German toolbar texts), `DemoPictures` (cards and texts in page layers: the picture
  and text extension points), `cursors/` + `tools/make_cursors.py` (a dot cursor per palette color × width step, ids 1000/2000 +
  color × 10 + step), `UiTests/` (`SurfaceTest`, `BoundaryTest`, `ToolsTest`, `SelectionTest`, `RulerTouchTest`, `PagesTest`,
  `MouseTest`, `TextTest`). `tools/cursorprobe.cpp`:
  DXGI Desktop Duplication probe – the pointer as the screen draws it; `build.ps1` puts it in `.local-build\tools`.

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
  would gamble on timing. With one presenter there is one activation, and no input switching between presenters.
- **When the pen cursor may be set (root cause, 2026-11-19):** the crash is `put_PointerCursor` -> `_RunOnInkThreadSync`
  failing while `CInputContext::Activate` runs on Windows' internal ink thread (queued when the presenter and its
  `CoreInkIndependentInputSource` are created; no API reports its end, Microsoft's docs say nothing about it). The
  observable signal that it is done: the input source's first pointer event (Entering/Hovering/Moving) - input only
  arrives through an active context. The library sets the cursor there (from that event, on Windows' input thread)
  and afterwards whenever the tool changes; no timer. Sandbox: the dot showed at the pen on the very first hover
  (`ToolsTest.ps1`, cursorprobe); 30 starts of a single presenter with the cursor set right after creation (the
  win32-ink-demo) never crashed either - only several activations at once did.
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

## Tools (phase 2a, measured 2026-11-19 in the sandbox, `ToolsTest.ps1`)

- Toolbar: comctl32 toolbar (flat, tooltips via TBN_GETINFOTIP, BTNS_CHECK + BTNS_DROPDOWN, images from Segoe Fluent
  Icons at 16 px × DPI); preset menus are user32 popup menus (swatch bitmaps; a bitmap replaces the check mark, so the
  current color is bold) - windowed popups, pen taps reach them. A second tap on the active preset opens its menu too
  (WinUI behavior). The toolbar lives in its own container window (its notifications stay in the library).
- Pens: size = Xournal width / 1.75 (`InkWidth::FullPressureFactor`), pressure on, `FitToCurve` off, round tip.
  Highlighters: `DrawAsHighlighter`, no pressure, size = width. Windows ink draws highlighter strokes below the pens'
  ink (a black stroke stays visible over a later yellow highlighter).
- Eraser: whole strokes = presenter mode Erasing (`StrokesErased` -> undo edit); exact = mode None, the presenter's
  `UnprocessedInput` (Pressed/Moved/Released/Lost; `GetIntermediatePoints` sorted by time) drives `PartialEraser`, one
  undo step per drag. The pen's eraser end: with whole-stroke erasing Windows ink erases in mode Inking by itself;
  with exact erasing it is taken on its inverted hover (input source Hovering with `IsInverted`, mode switched to None
  - only the mode, a hover survives that) and given back when the tip hovers. The circle is a D2D ring below the ink
  (on the sheets) - above the ink a visual sits in the pen's way.
- Undo: one `UndoHistory` on the ink thread; edits are removed/added strokes (collected, erased, cut); undo re-adds
  `Clone()`s (a stroke cannot be added twice) and from then on the edit holds the copies. Pages with history are held
  (lazy pages never release them). Sandbox: 4 undos restored strokes erased whole, by the eraser end and cut exactly;
  a stroke on page 1 and one on page 2 undone in time order and redone.
- Presenter state lives on the host thread: input events (Windows' input thread) and UI calls queue work items
  (`OnInkThread`, run inline when already on the host thread).

## Selection, shapes, ruler (phase 2b/2c, 2026-11-19; sandbox `SelectionTest.ps1`)

- **Strokes never cross a page:** one presenter over the document, so a stroke is cut at the page's edge when it is
  collected, moved, pasted or recognized (`InkState::Split`, `ClipToRect`); pieces in the gap are dropped – the
  simplest way that matches Xournal++ keeping strokes per page. Sandbox: a stroke drawn across the gap came out as a
  piece on page 1 and one on page 2, nothing over the gap.
- **Selection:** the lasso/rectangle runs on unprocessed input (`RightDragAction = LeaveUnprocessed`: the barrel
  always lassos, with any tool); strokes are selected with the container's `SelectWithPolyLine` (its `Selected` flags
  are reset at once – the library keeps the selection itself and uses `Selected` only as the removal mark), the app's
  pictures by `PictureHost::PicturesIn`. Moving/scaling runs on window pointer input: the pen hovering over the frame
  takes the pen from the presenter (`InputDeviceTypes = None`), the window gets WM_POINTER*; hovering away gives it
  back. Preview: `InkStroke.PointTransform` (the ink redraws live) and `PictureHost::Preview`; commit: transformed
  strokes replace the originals (width unchanged), cut at page edges, one undo step with the pictures
  (`CompositeEdit`). A short still press on the selection (`Tap`) clears it; a tap beside it clears it and its dot is
  dropped; writing beside it clears it. Near the view's edge the drag scrolls the pages (16 ms timer = one frame;
  `ZoomToRect` kept on the content). Undo/redo clear the selection (its frame would not fit).
  Sandbox: barrel lasso selected 2 strokes + 1 card, the pen moved them (card moved too), undo/redo, tap beside,
  duplicate, copy, delete, undo, paste (Windows ink's clipboard), rectangle select + tap clears.
- **Shapes** on unprocessed input (preview in the overlay), one stroke of even width (pen width at full pressure,
  no pressure). **Recognition:** `InkAnalyzer.AnalyzeAsync` (Completed handler, result posted to the ink thread); the
  freehand stroke is replaced by the clean outline as its own undo step.
- **Ruler/protractor:** `InkPresenterRuler(presenter)` / `InkPresenterProtractor(presenter)` work on the Win32
  presenter (documented constructors). Without a transform the ruler appeared at the document-sized presenter's top
  right, out of view: the library places it in the view when it is shown (`Transform`). [doc] The stencil has no
  property to stay in view (`InkPresenterRuler`: Length, Width, AreTickMarksVisible, IsCompassVisible, colors, Kind,
  IsVisible, Transform): it lies on the document and scrolls with the pages, like a ruler on paper; shown again, it
  comes back into view. [measured, sandbox `RulerTouchTest.ps1`] A finger drag on the ruler moves the ruler and does
  not pan the pages (no DirectManipulation contact): the stencil takes the touch itself although the presenter's
  `InputDeviceTypes` has no touch; a finger beside it pans as before.
- A selection tap (selection tool or barrel) on no stroke selects the app's picture under the pen
  (`PictureHost::PictureAt`, optional); the frame and its handles keep their screen size when the zoom changes
  (`SelectionController::Zoomed`). Sandbox: the card was selected by a tap; at 100 % the handles kept their size.
- Toolbar numbers use `ToolbarTexts::decimalSeparator` ("0,85 pt"); the shapes tool's icon is drawn (Segoe Fluent
  Icons has no shapes glyph: square and circle in the font's stroke weight, `ShapesIcon`).
- **Stroke keys** (`StrokeKeys`, `KeyedStroke`): load/store/StrokesOf carry the app's key and the transform since
  loading; followed through `Clone()` in undo, moves (`Moved`), lazy copies; new ink, erased pieces, cuts, duplicates,
  pastes have key 0.
- **Page operations** (`InsertPages`, `RemovePage`, `MovePage`, 2026-11-19): one `PageMap` (new index by old, -1 =
  removed) drives everything: `UndoHistory::RemapPages` drops the edits of a removed page and remaps the others, the ink
  thread rebuilds the strokes of moved pages at their new place (`InkState::BeginRelocation`/`Relocated`, stroke keys
  follow with `Moved`), lazy pages (`PageStore::Remap`) and page layers (`Composition::Rearrange`, visuals reused) follow;
  the selection is cleared. Sandbox `PagesTest.ps1`: insert after page 2 then undo/redo, move page 1 down then undo
  twice - the strokes come and go on the right pages.
- **Toolbar accessibility** [measured 2026-11-19]: comctl32 exposes a button's text as its MSAA name; with
  `TBSTYLE_EX_MIXEDBUTTONS` the text is not drawn without `BTNS_SHOWTEXT`, so every button gets its name as text and
  the tooltip (`TBN_GETINFOTIP`) stays the long tip. Native UI Automation (`IUIAutomation`, as Narrator uses it) shows a
  ToolBar with Button/CheckBox children and names; the **managed** `System.Windows.Automation` client of Windows
  PowerShell (TestKit's `Find`) sees comctl32 toolbars and status bars as an empty pane. There are no automation ids
  (MSAA has none; they would need an own UIA provider). UI tests find buttons with `tools/uiaprobe.exe` (native UIA,
  per-monitor DPI aware - unaware it returns coordinates scaled to 96 DPI): pass it with `-Data .local-build\tools\uiaprobe.exe`.
- **Path icons:** app buttons may bring XAML/SVG path data (`F0`/`F1`, M L H V C A Z, absolute/relative; `model/PathData`,
  unit-tested), drawn with D2D into the button's bitmap in the font glyphs' color and size.
- **Mouse and taps** (2026-11-19, sandbox `MouseTest.ps1`): the presenter takes only the pen (`InputDeviceTypes` =
  Pen, set once at creation - no rewrite during a hover; `SurfaceOptions::mouseInks` is gone). The mouse comes to the
  window as WM_LBUTTON*/WM_MOUSEMOVE (mouse messages Windows makes from pen/touch carry the 0xFF515700 signature in
  `GetMessageExtraInfo` [doc] and are ignored). `SelectionController` gets the press first (move/scale on the frame,
  a press beside clears); otherwise `MouseSelect` runs: a drag that leaves the tap distance becomes ToolInput's lasso or
  rectangle (`MouseSelect*`, the select mode, any tool; the pen's press takes over), a click selects the picture under
  it, else it is the app's `tapped`. `TapGesture` (model, unit-tested) tells taps: Tap's rules, one pointer, not
  cancelled. Finger: [measured] after `SetContact` the window still gets WM_POINTERUPDATE/UP of the contact, and a tap
  leaves DirectManipulation READY; a pan goes RUNNING within ~40 ms of the down, which cancels the tap (`TouchTaps`;
  WM_POINTERCAPTURECHANGED cancels too). Measured: mouse drag with the pen tool = no ink, lasso; click on the card
  selects it; click beside clears (no tap); click on nothing, finger tap = `tapped`; a finger pan = no tap, still pans.
- **Text tool** (2026-11-19, sandbox `TextTest.ps1`; technique from the probe `D:\src\feature-probes\textedit`):
  a windowless RichEdit (msftedit.dll `CreateTextServices`, ITextServices2 + our ITextHost2, plain text, multi-line,
  no wrapping, `TxDrawD2D`) drawn into a DComp surface in the composition's text layer (above the page layers, below
  overlay and ink): it pans and zooms with the pages. [measured, probe] The render target's DPI must be the window's,
  the bounds given to TxDrawD2D are surface pixels, while the client rectangle, mouse and caret are client pixels. So
  the box is laid out at the settled zoom: RichEdit's font is `RichEditText::Twips(size, zoom)` = size x zoom x 72/96
  points (a page unit shows as DPI scale x zoom pixels, RichEdit draws a point as DPI/72 pixels). [measured, unit test]
  `OnTxPropertyBitsChange` must announce the host's bits after `OnTxInPlaceActivate` (before, Enter did not make a new
  line), the char format goes to the text with `EM_SETCHARFORMAT SCF_ALL` too, and `TxGetNaturalSize` needs the extent
  in HIMETRIC of the offered size (with {-1,-1} it returned sizes ~130x too large). Enter arrives as WM_KEYDOWN +
  WM_CHAR, as typing sends it. The caret is drawn by the box (blinking with `GetCaretBlinkTime`); the system caret
  (`CreateCaret`, hidden under the composition) is set too for Magnifier/Narrator. The pen: the Text tool puts the
  presenter in mode None; a pen tap (unprocessed input, `Tap` rules) goes to `TextEditor::Tap`; while a box is edited
  the pen hovering over it is taken from the presenter (`ToolInput::SetTextBox`, as over the selection frame) and its
  presses reach RichEdit as the mouse messages Windows makes from them (`GetMessageExtraInfo` signature; elsewhere
  they are ignored). The surface's window takes the focus while editing and gives it back after. End: a tap beside,
  Escape, another tool, undo/redo, a selection, a page operation, `SetTextHost`, the surface going. The result goes to
  the app's `TextHost` (`Add`/`Change`/`Remove`; an empty new text is nothing, a text emptied is removed) and the
  history (`TextEdit`, undo/redo call the host on the UI thread). Sandbox: a new text typed with umlauts and a second
  line (`KEYEVENTF_UNICODE` injection: WM_CHAR whatever the layout), ended by a tap beside, edited again (End, " neu"),
  change undone/redone, an empty text left nothing, a mouse lasso selected the text (through `PictureHost`), moved it,
  undo; at zoom 2 the box lay exactly on the text. UI Automation and the touch keyboard: see the next point.
- **Text box in UI Automation** (2026-11-19, sandbox `KeyboardTest.ps1`): Microsoft's route for a windowless RichEdit
  ([doc] https://learn.microsoft.com/windows/win32/api/textserv/nn-textserv-iricheditwindowlessaccessibility):
  `SurfaceAutomation` answers WM_GETOBJECT (UiaRootObjectId) with a fragment root hosted on the window
  (`UiaHostProviderFromHwnd`); while a text is edited its only child is RichEdit's own provider from
  `IRicheditWindowlessAccessibility::CreateProvider`, given a site of ours (`IRawElementProviderWindowlessSite`: parent =
  the root, runtime id prefix {UiaAppendRuntimeId, 1}; `IRichEditUiaInformation`: the box on the screen, visible). The
  root's `GetFocus` is the box while the window has the focus; a focus-changed event is raised when editing begins, the
  provider is disconnected when it ends. [measured, sandbox, native UIA client `uiaprobe --focus`] The focused element
  while editing is RichEdit's: control type Document (50030), name "RichEdit Control", keyboard focus, the text pattern
  with the typed text, the box's screen bounds; after editing the window again. Include order: `ole2.h` before
  `uiautomation.h` (WIN32_LEAN_AND_MEAN), `uiautomation.h` and `richedit.h` before `textserv.h`. A finger tap into the
  edited box puts the caret there (the finger never reaches RichEdit; the tap is not the app's).
  **Touch keyboard:** [doc] it comes up when the user taps into the focused element that has an editable text pattern
  (not on programmatic focus); a hardware keyboard keeps it away unless "Show the touch keyboard" is "Always". [measured,
  sandbox] It did not show - neither by default nor with `TabletTip\1.7\TouchKeyboardTapInvoke = 2` ("Always"; set in
  the test, TextInputHost restarted; a sign-out may be needed) after pen taps and a finger tap into the box: the
  TextInputHost window "Windows-Eingabeerfahrung" stayed cloaked. The sandbox has a keyboard attached and only virtual
  pen/touch devices, so this is a device check (see README "Device checks"). The documented explicit route, if
  auto-invoke does not come: `IInputPaneInterop::GetForWindow(surface window)` -> `InputPane.TryShow()` (the text probe
  saw it return true with Visible = false while a hardware keyboard was attached) - not built; Windows' auto-invoke is
  the intended route.
- **Undo after undo** (review 2026-11-19): undo/redo bring strokes back as `Clone()`s; edits recorded earlier name the
  original, so `InkState::Current` follows `Replaced` copies (undo/redo, relocation) - draw, erase, undo, undo, redo
  and draw, move, undo, undo, redo leave exactly one stroke (`tests/StrokeEditTests`, real Windows ink objects).
- **InkHost::Run** carries the work's exception to the caller and throws on its timeout; shutdown waits without a
  timeout (the work refers to the surface). `StrokesOf` returns none for a page whose ink is not in the presenter and
  throws when the ink thread fails or does not answer.
- **Page operations** also end running gestures and the ink-side selection inside the rearrange work item; a tool's
  result made before the ink thread applied every issued page operation is dropped (generation counted on both
  threads). Sheets of pages that stay keep their drawing (`PaperLayer::MoveTo`). Taps are measured on the screen
  (`Tap::MaxDistanceDip` / zoom); a finger that stops the pages' inertia is no tap (device check).
- **Page in view outlined** (`MarkCurrentPage(bool)`, 2026-11-19, sandbox `MarkTest.ps1`): as the WinUI version (tag
  `winui-final`, InkPage: MarkGap 3, MarkThickness 2, CornerRadius 4, AccentFillColorDefaultBrush) - a 2-unit outline
  3 units around the page, rounded, in the accent's first darker shade (`UISettings.GetColorValue(AccentDark1)`: what
  AccentFillColorDefaultBrush is in the light theme), read again on `ColorValuesChanged` (a worker thread: posted to
  the UI thread). `render/PageMark` draws four strips (top/bottom with the corners, left/right) into the composition's
  mark layer below the sheets - never over a page or the ink, not in any page's content - at the settled zoom's
  resolution; it follows `PageInView` as the view moves (the 120 ms motion tick). Sandbox: outline at fit width (115 %)
  and 50 %, crisp at 150 % (corner enlarged), and on page 2 after a finger scroll.
- **Underlay:** the app draws under the ink with D2D (`drawUnderlay(page, dc, area)`, page coordinates) into the
  sheet's virtual surface, over the Paper; `InvalidatePage` redraws it; page layers (`PageLayer`) are DComp visuals for
  live content (pictures dragged by the selection).

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
