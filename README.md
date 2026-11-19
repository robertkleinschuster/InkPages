# InkPages

A pen-first writing surface for Win32 apps on Windows 11, in C++: pages with Windows Ink and the tools that go with
them, plus their toolbar – native, lean, modern Win32.

- Windows Ink through the Win32 ink presenter (`IInkDesktopHost` / `IInkPresenterDesktop`) in the app's
  DirectComposition tree: low latency, pressure, the pen's eraser end, a pen cursor in the tool's color
  (`CoreInkIndependentInputSource.PointerCursor`).
- The sheet moves like paper: finger and precision touchpad pan (with inertia) and zoom with DirectManipulation; the
  pen writes, also while a finger holds the sheet.
- Tools (being ported): pen and highlighter presets, eraser (whole strokes or exact), lasso/rectangle selection with
  exact moves and scaling, shapes and shape recognition (`InkAnalyzer`), the Windows ruler, undo/redo.
- Controls: comctl32 v6 with visual styles, PerMonitorV2. No UI framework, few dependencies, static CRT – an app is a
  single small exe.

There is no file format inside: the app supplies its document's pages and adds its own commands.

**Status (2026-11-19): the C++/Win32 library has the writing surface (pages, paper, the app's underlay, ink,
pan/zoom, pen cursor, lazy pages with stroke keys) and the tools (toolbar, pen and highlighter presets, eraser
whole/exact, the pen's eraser end, selection with the app's pictures, shapes and recognition, Windows' ruler and
protractor, undo/redo).** The WinUI 3 version
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

- **Pages and view:** `SetPages`, `ZoomTo`, `FitWidth`, `ScrollToPage`, `PageInView`, `Layout`, `viewChanged`.
  Finger/touchpad pan and pinch, wheel scrolls, Ctrl+wheel zooms; pen and mouse buttons never pan.
- **Strokes:** `KeyedStroke` = `InkStroke` (points with pressure, tilt, time; drawing attributes) in page
  coordinates + the app's key + the transform since loading. Lazy pages: `load`/`store` (ink thread); `StrokesOf(page)`
  to save live pages; `contentChanged(pages)` after every edit; `Hold(page)`.
- **The app's content:** `drawUnderlay` + `InvalidatePage` (D2D, page coordinates, under the ink), `PageLayer(page)`
  (DComp visual for live content), `PictureHost` (`Pictures.h`: your pictures selected, moved, scaled, deleted,
  duplicated with the strokes; undo calls back).
- **Tools:** `SetTool(ToolState)` (the toolbar does it): pen, highlighter, eraser (whole strokes or exact circle),
  select (lasso/rectangle; the barrel lassos with any tool), shapes (+ recognition), `SetRuler`/`SetProtractor`. The
  pen's eraser end erases with any tool. Pen cursor: a cursor resource of the app's exe (`cursorFor`).
- **Selection:** `Cut`, `Copy`, `Paste`, `Duplicate`, `DeleteSelection`, `SelectAll`, `ClearSelection`,
  `selectionChanged`; Windows ink's clipboard, or your own format with `copyToClipboard`/`pasteFromClipboard` (`Clip`).
- **History:** `Undo`, `Redo`, `historyChanged`, `MarkSaved`/`AtSavePoint` – one history across pages.
- **Toolbar:** comctl32, Segoe Fluent icons, menus at the presets (color, width), at the eraser toggle (size), at
  select (lasso/rectangle), shapes (kind, recognition) and the ruler; `ToolbarTexts` in your language; app buttons.

## Layout

- `include/inkpages/` public headers, `src/` the library (static), `demo/` the demo app, `tests/` unit tests,
  `UiTests/` the Windows Sandbox runner with synthetic pen and touch input (`TestKit.ps1`, `Inj.cs`, `Finger.cs`,
  `CursorShot.cs`), `tools/cursorprobe.cpp` (the pointer as the screen draws it, for the cursor test).
