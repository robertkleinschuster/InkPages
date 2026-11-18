# InkPages

A pen-first writing surface for WinUI 3 apps on Windows 11: pages with Windows Ink and the tools that go with them,
plus their toolbar.

- Pens and highlighters as one-tap presets (the native `InkToolbar` with custom pen buttons). Each has the
  native color/size flyout.
- Eraser: the native whole-stroke eraser, or exact (partial) erasing with a circle cursor. The pen's eraser end and
  eraser button work with every tool.
- Selection: lasso or rectangle. Move and scale it (corner handles), duplicate, cut/copy, delete. The pen's barrel
  button always draws a lasso. A selection moves across pages, and the view scrolls near its edge.
- Shapes (line, arrow, rectangle, ellipse), shape recognition with Windows `InkAnalyzer`, and the Windows
  ruler/protractor.
- Undo/redo for everything as one history.

There is no file format inside. The app implements `InkPage` (its document's page: what lies under the ink, the app's
data per stroke, pictures) and adds its own commands to the toolbar. The toolbar is built in code with no XAML
resources, so an unpackaged, self-contained app needs no resource merging.

Used by [Xournal Studio for Windows](https://github.com/robertkleinschuster/xournal-studio-windows) (Xournal++
`.xopp` files) and by MatheTrainer (a pen-first math exercise trainer).

## Requirements

- Windows 11, .NET 10 SDK, x64.
- `Microsoft.WindowsAppSDK.WinUI` + `.DWrite` **2.6.6-experimental**. `InkCanvas`/`InkToolbar` exist for WinUI 3
  only in the experimental channel.
- **The WinUI fork runtime** (see below). The stock 2.6.6-experimental runtime does not work for the tools.

## WinUI runtime

The tools need fixes that are not in an official Windows App SDK release yet:

- [microsoft-ui-xaml#12292](https://github.com/microsoft/microsoft-ui-xaml/pull/12292): `InkPresenter.UnprocessedInput`
  args are readable.
- The `InkToolbar` uses custom pens' drawing attributes. Without this, highlighters draw opaque.
- Custom tools show their checked state.
- A pointer keeps its capture when the pointer is routed away and back.

The fixes are built in
[robertkleinschuster/microsoft-ui-xaml](https://github.com/robertkleinschuster/microsoft-ui-xaml) and published as a
release zip. `tools/Get-WinUIRuntime.ps1` downloads the pinned release (checked by SHA-256) into `.local-build/winui-fre`.
`build/WinUIForkRuntime.targets` then puts that runtime over the package's runtime in the app that imports it, before
the app's `.pri` is generated, so the fixed control templates reach the app too. The build fails if the runtime is
missing.

## Using it in an app (git submodule)

```powershell
git submodule add https://github.com/robertkleinschuster/InkPages.git external/InkPages
external\InkPages\tools\Get-WinUIRuntime.ps1
```

In the app's `.csproj` (unpackaged, self-contained, x64):

```xml
<ItemGroup>
  <ProjectReference Include="..\external\InkPages\InkPages\InkPages.csproj" />
  <PackageReference Include="Microsoft.WindowsAppSDK.WinUI" Version="2.6.6-experimental" />
  <PackageReference Include="Microsoft.WindowsAppSDK.DWrite" Version="2.6.6-experimental" />
</ItemGroup>
<Import Project="..\external\InkPages\build\WinUIForkRuntime.targets" />
```

In CI, check out with `submodules: true` and run `external\InkPages\tools\Get-WinUIRuntime.ps1` before building.

In code (see `InkPages.Demo/DemoWindow.cs` and `DemoPage.cs` for a complete example):

```csharp
var choices = LoadChoices() ?? new ToolChoices();          // the user's tool choices (plain data, e.g. JSON)
var bar = new PenToolbar(PenToolbarTexts.German, choices); // Xournal++'s palette; English: new PenToolbarTexts()
var scroller = new ScrollView { IgnoredInputKinds = ScrollingInputKinds.Pen, /* pages in a StackPanel */ };
var ink = new InkWorkspace(bar, scroller, choices);
ink.ChoicesChanged += (_, _) => SaveChoices(ink.Choices);
foreach (var page in pages) ink.Attach(page);     // your InkPage subclasses
ink.SetPages(pages);                              // the pages that have a view now
ink.Commands.WireToolbar();                       // undo/redo/delete, cut/copy/duplicate on Windows ink (false: not these three)
ink.Commands.AddShortcuts(root);                  // Ctrl+Z/Y/X/C/V/D/A, Del (not while typing)
ink.Commands.Refused += (_, command) => Say(texts.Refused);  // a command while the selection is dragged
bar.Extensions.Add(PenToolbar.Command(icon, "Paste"));  // app commands after the library's buttons
ink.InkHidden = true;                             // while a dialog is open (the ink would draw over it)
ink.Undo.ContentChanged += (_, _) => ShowUnsaved(!ink.Undo.IsAtSavePoint);  // ink edited, undone or redone
ink.Undo.MarkSaved();                             // after saving (or loading)
ink.ResetPage(page, loadedStrokes);               // a page's ink replaced without undo (load, new task)
InkLog.Writer = Log; InkLog.ErrorWriter = LogError;  // the library's log; errors (exceptions) apart
```

A page that keeps nothing beside its ink derives from `InkPage` and only fills `UnderInk` (e.g. ruled paper): stroke
identity across undo, placements and "no pictures" are defaults. An app with its own data per stroke or with pictures
overrides those members: `OnStrokeReplaced` (its data follows a stroke's copy), `CopyTo` (a dragged selection goes on to
another page: the library removes the originals and records one undo step), the placement and picture members. An app
with its own clipboard format (pictures, file elements) calls `WireToolbar(clipboard: false)`, wires cut/copy/duplicate
itself and uses the rest of `InkCommands` (`Delete`, `SelectAll`, `PasteInk`, `SelectAdded`, `Refuses`).
Rules the library depends on:

- The `ScrollView` must ignore the pen (`IgnoredInputKinds = Pen`). Otherwise it captures the pen for panning.
- Never lay XAML over a page. The experimental `InkCanvas` draws its ink above it and takes its input. Collapse the
  ink while a `ContentDialog` is open.
- Root-level keyboard accelerators need `KeyboardAcceleratorPlacementMode.Hidden`. Their tooltip is a windowed popup
  that breaks pen drags.
- Call `SetCurrentPage` with the page in view only after the first layout. The ruler belongs to that page. With one
  page it is not needed (`SetPages` makes it the toolbar's target).
- `InkHidden` covers the workspace's pages only: hide other `InkCanvas`es of the app (e.g. answer fields) yourself.
- `ChoicesChanged` comes once the changes settle (0.4 s after the last): save `Choices` on closing too.

## Repository

| Path | What |
| --- | --- |
| `InkPages/` | the library (`Model/`: geometry without UI, unit-tested) |
| `InkPages.Demo/` | demo and development app: graph pages with in-memory content, the whole toolbar |
| `InkPages.Tests/` | unit tests (`dotnet test InkPages.Tests`) |
| `UiTests/` | Windows Sandbox UI tests with synthetic pen input (`Run-SandboxTest.ps1`, `TestKit.ps1`, `Inj.cs`), also usable by apps |
| `build/`, `tools/` | the WinUI fork runtime (targets, download, local WinUI build) |

```powershell
tools\Get-WinUIRuntime.ps1
dotnet test InkPages.Tests
dotnet publish InkPages.Demo -c Release -o .local-build\demo
UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test InkPages.Demo\UiTests\ToolsTest.ps1
```

Development notes: [CLAUDE.md](CLAUDE.md).
