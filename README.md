# InkPages

A pen-first writing surface for Win32 apps on Windows 11, in C++: pages with Windows Ink and the tools that go with
them, plus their toolbar – native, lean, modern Win32.

- Windows Ink through the Win32 ink presenter (`IInkDesktopHost` / `IInkPresenterDesktop`) in the app's
  DirectComposition tree: low latency, pressure, the pen's eraser end, a pen cursor in the tool's color
  (`CoreInkIndependentInputSource.PointerCursor`).
- The sheet moves like paper: finger and precision touchpad pan (with inertia) and zoom with DirectManipulation; the
  pen writes, also while a finger holds the sheet.
- Tools: pen and highlighter presets, eraser (whole strokes or exact), lasso/rectangle selection with exact moves and
  scaling, shapes and shape recognition (`InkAnalyzer`), the Windows ruler and protractor, a text tool, undo/redo.
- Controls: comctl32 v6 with visual styles, PerMonitorV2. No UI framework, few dependencies, static CRT – an app is a
  single small exe.

There is no file format inside: the app supplies its document's pages and adds its own commands.

**Status (2026-11-20): the C++/Win32 library has the writing surface (pages, paper, the app's underlay, ink,
pan/zoom, pen cursor, lazy pages with stroke keys, page operations) and the tools (toolbar, pen and highlighter
presets, eraser whole/exact, the pen's eraser end, selection with the app's pictures, shapes and recognition, Windows'
ruler and protractor, the text tool, undo/redo).** The WinUI 3 version
(C#, `InkCanvas`/`InkToolbar`, with its WinUI fork runtime) is tagged
[`winui-final`](https://github.com/robertkleinschuster/InkPages/tree/winui-final).

Used by [Xournal Studio for Windows](https://github.com/robertkleinschuster/xournal-studio-windows) (Xournal++
`.xopp` files); MatheTrainer stays on `winui-final` for now.

## Build

Requirements: Windows 11 (x64), Visual Studio 2026 with the C++ desktop workload (its CMake and Windows SDK).

```powershell
.\build.ps1            # Release build + unit tests (GoogleTest via CTest); the demo goes to .local-build\demo
.\build.ps1 -NoTests
```

## Embedding

```cpp
#include "inkpages/WritingSurface.h"
#include "inkpages/Toolbar.h"

auto surface = inkpages::WritingSurface::Create(parentWindow);  // a child window; place it with MoveWindow
inkpages::PageSpec page;                                         // size in page units (default A4 in points)
page.paper = { inkpages::PaperKind::Graph, 5 * 72 / 25.4f };     // or Plain, and draw your own background:
surface->drawUnderlay = [](int page, ID2D1DeviceContext* dc, inkpages::RectF area) { /* background, PDF, pictures */ };
surface->SetPages({ page, page, page });
surface->load = [](int page) { return std::vector<inkpages::KeyedStroke>{ /* stroke, your key, transform */ }; };
surface->store = [](int page, std::vector<inkpages::KeyedStroke> strokes) { /* keep them (ink thread) */ };
surface->SetPictureHost(&myPictures);                            // your pictures in the selection
auto toolbar = inkpages::Toolbar::Create(parentWindow, *surface, choices, texts, cursorFor);
toolbar->choicesChanged = [](inkpages::ToolChoices const& c) { save(c.Serialize()); };
```

- **Pages and view:** `MarkCurrentPage(bool)` (the page in view outlined in the accent color; turn it on for more than
  one page), `SetPages`, `InsertPages(index, specs)`, `RemovePage(index)`, `MovePage(from, to)` (the history,
  strokes, stroke keys and page layers follow; edits of a removed page are dropped; remap your own per-page data with
  `model/PageMap.h`; the strokes you keep for `store`d pages in `pagesRemapped`, ink thread, in order with load/store), `ZoomTo`, `FitWidth`, `ScrollToPage`, `ScrollTo(page, point)` (the point to the view's top left, 16 DIPs in: outline
  entries, links), `RevealRect(page, area)` (scrolls only as far as needed: search hits), `PageInView`, `Layout`, `viewChanged`.
  Finger/touchpad pan and pinch, wheel scrolls, Ctrl+wheel zooms; pen and mouse buttons never pan.
- **Input:** only the pen writes. The mouse (and touchpad clicks) selects with any tool: a drag lassos (or draws the
  rectangle, by the select mode), on the selection moves it, on a corner scales it; a click beside it clears it, on a
  picture selects it; with the Text tool a click writes or edits a text there (a click beside the box ends it).
  `tapped(page, point, PointerKind)`: a mouse click or finger tap no selection gesture took (for links); a finger pan
  is no tap. The app's own mouse gestures (e.g. selecting a PDF's text): `mouseClaims(page, point)` is asked when a
  press becomes a drag or a double/triple click where no ink, picture, selection frame or edited text lies; claimed,
  the gesture goes to `mouseGesture` (Begin/Move/End/Cancel, page coordinates of the press page, click count) instead
  of the lasso. A single click stays a click.
- **Strokes:** `KeyedStroke` = `InkStroke` (points with pressure, tilt, time; drawing attributes) in page
  coordinates + the app's key + the transform since loading. Lazy pages: `load`/`store` (ink thread); `StrokesOf(page)`
  to save live pages; `contentChanged(pages)` after every edit; `Hold(page)`.
- **The app's content:** `drawUnderlay` + `InvalidatePage` (D2D, page coordinates, under the ink), `PageLayer(page)`
  (DComp visual for live content), `PictureHost` (`Pictures.h`: your pictures selected, moved, scaled, deleted,
  duplicated with the strokes; undo calls back).
- **Texts:** `SetTextHost` (`Texts.h`). The Text tool: a pen tap writes a text there or edits the text there (the
  keyboard types, a windowless RichEdit on the page), a tap beside it, Escape or another tool ends it; the result comes
  back as `Add`/`Change`/`Remove` and is one undo step. Your texts are drawn by you (left out while `SetEditing` names
  one) and selected, moved, scaled, deleted through your `PictureHost` (same id space). `EditingText`,
  `textEditingChanged` (leave your key shortcuts alone meanwhile), `EndTextEditing` (e.g. before saving). Plain text,
  one font, size and color per text, as Xournal++'s `<text>`.
- **Tools:** `SetTool(ToolState)` (the toolbar does it): pen, highlighter, eraser (whole strokes or exact circle),
  select (lasso/rectangle; the barrel lassos with any tool), shapes (+ recognition), text, `SetRuler`/`SetProtractor`. The
  pen's eraser end erases with any tool. Pen cursor: a cursor resource of the app's exe (`cursorFor`).
- **Selection:** `Cut`, `Copy`, `Paste`, `Duplicate`, `DeleteSelection`, `SelectAll`, `ClearSelection`,
  `selectionChanged`; Windows ink's clipboard, or your own format with `copyToClipboard`/`pasteFromClipboard` (`Clip`).
- **History:** `Undo`, `Redo`, `historyChanged`, `MarkSaved`/`AtSavePoint` – one history across pages.
- **Toolbar:** comctl32, Segoe Fluent icons, menus at the presets (color, width), at the eraser toggle (size), at
  select (lasso/rectangle), shapes (kind, recognition), text (font, size, color) and the ruler; `ToolbarTexts` in your language; app buttons
  (`ToolbarButton`: a Segoe Fluent glyph or a vector `path` in XAML path syntax, `pathSize` = its box; `name` = the
  accessible name; `groupStart` = a separator before it; `atStart` = at the toolbar's start, before undo/redo). Every button has an accessible name (MSAA, so native UI Automation and Narrator find it).

## Layout

```
include/inkpages/        public API (WritingSurface, Toolbar, Pictures, Texts, Log) and model/ (pure data and logic)
src/<module>/            the library (static): base, model, render, ink, tools, text, surface, toolbar
demo/                    the demo app (main.cpp, DemoPictures), its cursors (cursors/, tools/make_cursors.py) and its
                         sandbox UI tests (UiTests/*.ps1)
tests/                   unit tests (GoogleTest, CTest)
UiTests/                 the Windows Sandbox kit for embedding apps too: Run-SandboxTest.ps1 (on a hidden desktop),
                         TestKit.ps1, Inj.cs (synthetic pen, touch, mouse, keys), Finger.cs, CursorShot.cs, HiddenDesktop.cs
tools/                   test tools: uiaprobe (native UI Automation: find by name, focus, touch keyboard),
                         cursorprobe (the pointer as the screen draws it)
build.ps1                CMake (Visual Studio's) Release build + unit tests; .github/workflows/build.yml runs it
```

The WinUI 3 (C#) version is at the tag `winui-final`.

## Device checks

What the sandbox cannot show (no tablet posture, a keyboard attached, virtual pen/touch only) - check on the device:

- **Touch keyboard in tablet posture:** fold the convertible into tablet posture (no keyboard), choose the Text tool,
  tap the page with the pen: the touch keyboard should come up; typing on it writes into the box. Then tap into an
  edited box with a finger: the caret goes there and the keyboard stays/comes up. If it does not come up: Settings >
  Time & language > Typing > Touch keyboard > "Show the touch keyboard" - try "When no keyboard attached" and
  "Always", and note which taps (pen on the page, pen into the box, finger into the box) bring it up.
- **Narrator** (Ctrl+Win+Enter) while a text is edited: it should read the box as an editable document with its text;
  typing should be echoed.
- **Magnifier** follows the caret while typing.
- **A finger on the pages while they coast** (inertia) stops them and is not a tap (no link opens).

