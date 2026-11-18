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
`.xopp` files).

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
var bar = new PenToolbar(new PenToolbarTexts(), palette, pens, highlighterPalette, highlighters);
var scroller = new ScrollView { IgnoredInputKinds = ScrollingInputKinds.Pen, /* pages in a StackPanel */ };
var ink = new InkWorkspace(bar, scroller);
foreach (var page in pages) ink.Attach(page);     // your InkPage subclasses
ink.SetPages(pages);                              // the pages that have a view now
bar.Undo.Click += (_, _) => ink.UndoStep();
bar.Redo.Click += (_, _) => ink.RedoStep();
bar.Extensions.Add(PenToolbar.Command(icon, "Paste"));  // app commands after the library's buttons
```

Rules the library depends on:

- The `ScrollView` must ignore the pen (`IgnoredInputKinds = Pen`). Otherwise it captures the pen for panning.
- Never lay XAML over a page. The experimental `InkCanvas` draws its ink above it and takes its input. Collapse the
  ink while a `ContentDialog` is open.
- Root-level keyboard accelerators need `KeyboardAcceleratorPlacementMode.Hidden`. Their tooltip is a windowed popup
  that breaks pen drags.
- Call `SetCurrentPage` with the page in view only after the first layout. The ruler belongs to that page.

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
