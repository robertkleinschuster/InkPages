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

**Status (2026-11-19): the move from WinUI 3 to C++/Win32 is under way – the writing surface (pages, paper, ink,
pan/zoom, pen cursor, lazy pages) and the first tools (toolbar, pen and highlighter presets, eraser whole/exact, the
pen's eraser end, undo/redo) are in; selection, shapes and the ruler follow.** The WinUI 3 version
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

## Embedding (phase 1 API)

```cpp
#include "inkpages/WritingSurface.h"

auto surface = inkpages::WritingSurface::Create(parentWindow);       // a child window; place it with MoveWindow
inkpages::PageSpec a4;                                                // 595 x 842 DIP
a4.paper = { inkpages::PaperKind::Graph, 5 * 96 / 25.4f };            // 5 mm squares, clipped to the sheet
surface->SetPages(std::vector<inkpages::PageSpec>(3, a4));            // stacked vertically
surface->SetPen({ 0xFF, 0, 0, 0 }, 2, IDC_MY_PEN_CURSOR);             // color, size (DIPs), cursor resource of the exe
surface->FitWidth();
surface->load = [](int page) { /* the page's strokes, page coordinates (ink thread) */ return strokes; };
surface->store = [](int page, std::vector<InkStroke> strokes) { /* keep them (ink thread) */ };
surface->viewChanged = [] { /* zoom / page in view changed */ };
```

- Finger and precision touchpad pan (inertia) and pinch-zoom; the mouse wheel scrolls, Ctrl+wheel zooms
  (DirectManipulation). Pen and mouse buttons never pan; the pen writes (the mouse too with `mouseInks`).
- `ZoomTo`, `FitWidth`, `ScrollToPage`, `PageInView`, `CurrentView`, `Layout` (page rectangles, page at a point).
- Lazy pages: only pages near the view hold their strokes in the ink presenter; the others are handed to `store` and
  come back through `load`. `Hold(page, true)` keeps a page's ink (e.g. while it has an edit).
- Toolbar: `inkpages::Toolbar::Create(parent, *surface, choices, texts, cursorFor)` – undo/redo, pen presets (color
  and width menus), highlighter presets, eraser and the "erase whole strokes" toggle (its arrow: the circle's size),
  the app's own buttons. `ToolbarTexts` (English defaults; the app passes its language), `ToolChoices` (the app keeps
  them: `choicesChanged`, `Serialize`/`Parse`), `Choose` for keyboard shortcuts.
- Undo/redo: `surface->Undo()`, `Redo()`, `historyChanged`, `MarkSaved()`/`AtSavePoint()` – one history across pages.
- The pen cursor is a cursor resource in the app's exe (the demo ships dot cursors per color × width,
  `demo/tools/make_cursors.py`).

## Layout

- `include/inkpages/` public headers, `src/` the library (static), `demo/` the demo app, `tests/` unit tests,
  `UiTests/` the Windows Sandbox runner with synthetic pen and touch input (`TestKit.ps1`, `Inj.cs`, `Finger.cs`,
  `CursorShot.cs`), `tools/cursorprobe.cpp` (the pointer as the screen draws it, for the cursor test).
