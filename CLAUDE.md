# CLAUDE.md

Guidance for Claude Code in this repository. Keep it current: add what is learned, remove what turns out wrong.

## What this is

**InkPages**: the reusable, pen-first writing area for WinUI 3 apps. It holds pages with Windows Ink, the tools
(selection, shapes, partial eraser, auto-scroll, undo) and the toolbar. It was moved out of
[xournal-studio-windows](https://github.com/robertkleinschuster/xournal-studio-windows) on 2026-11-18, history included,
so that other apps can use it too. Xournal Studio for Windows and the MatheTrainer embed it as a git submodule
(`external/InkPages`). The library has no file format: apps implement `InkPage` and add their commands to the toolbar.
Public repository. The README is the user-facing documentation (embedding, runtime).

The platform facts behind this code (InkCanvas, InkPresenter input, lifted compositor, XAML input) are in the user's
global CLAUDE.md. The app-specific history (how the tools were found and tested with the user) is in
xournal-studio-windows' CLAUDE.md.

## Rules (agreed with the user)

- **Microsoft's APIs do the work; the library only wires them up correctly.** `InkStrokeContainer` selects, moves,
  deletes and copies ink. Straight lines come from the Windows ruler, shape recognition from `InkAnalyzer`, and the
  eraser is the native one. Do not re-implement what a platform feature does, and do not work around framework
  behavior. When an API is broken, find the root cause (issue/PR) and report the options. The partial eraser is an
  agreed exception: Windows ink erases whole strokes only (proposal microsoft-ui-xaml #1530).
- **Never swallow exceptions.** Log type, HResult and message (`InkLog`; handlers on forwarded ink events log
  themselves through `InkTools.Logged`).
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

## Architecture

- `InkWorkspace`: the facade. It wires the toolbar, tools, input, auto-scroll and undo. Members: `Attach(page)`,
  `SetPages` (the pages that have a view now), `Holds(page)` (the page's view must stay: history, selection, running
  gesture or drag hand-over, shape recognition), `Reset`, `Removing/Removed`, `UndoStep`, `TransferToPage` (the app
  moves a dragged selection's content to another page).
- `InkWorkspace` also holds `Commands` (`InkCommands`), `CurrentPage`, `Choices`/`ChoicesChanged` (the user's
  `ToolChoices`) and `InkHidden` (collapse all pages' ink while a dialog lies over them).
- `InkPage`: the abstract page. It holds the app's `UnderInk` layers, the InkCanvas, the `Overlay` and the `Surface`.
  Stroke identity across undo (replaced ids), previews, placements and pictures have defaults for a page with ink only
  (`ClearInk` empties it without undo); an app with data per stroke or pictures overrides them (pictures are its own
  objects, removal tokens its own). `CreatePlaceholder` is a blank sheet for a page without a view.
- `InkCommands`: the edit commands on Windows ink shared by the apps - undo/redo, delete, duplicate (`CopyPicture` for
  pictures), cut/copy/paste through Windows ink's clipboard, select all, `ClearPage`, `SelectAdded`; refused while the
  selection is dragged (`Refused`). `WireToolbar` and `AddShortcuts` (hidden accelerator tooltips, not while typing).
- `PenToolbar` (+ `PenToolbarTexts` with `German`, `BallpointPen`/`HighlighterPen`, `Extensions` for app buttons,
  `PenToolbar.Command`). `new PenToolbar(texts, choices)` uses `XournalPalette` and the user's `ToolChoices`
  (`Model/`, defaults: black/blue/red pens, yellow/green highlighters). The row: undo | pens | highlighters | eraser
  (+ whole-stroke toggle) | select, shapes, ruler | selection commands | app extensions.
- `ToolController`: copies the InkToolbar's tool to all pages' presenters. For a custom tool and for hover takeovers
  it sets processing mode None, and then switches only the mode.
- `InkTools`: routes the presenters' unprocessed pen/mouse input to `SelectionTool`, `ShapeTool` and
  `PartialEraser`. A barrel press always goes to the selection. It also handles the hover takeovers (the eraser end,
  the tip over the selection).
- `Selection`, `SelectionDrag`, `DragAutoScroll` and `SelectionCommands` handle the selection. A move or scale runs
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
