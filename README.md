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

**Status (2026-11-19): the move from WinUI 3 to C++/Win32 has begun (phase 0: project skeleton).** The WinUI 3 version
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

## Layout

- `include/inkpages/` public headers, `src/` the library (static), `demo/` the demo app, `tests/` unit tests,
  `UiTests/` the Windows Sandbox runner with synthetic pen and touch input (`TestKit.ps1`, `Inj.cs`).
