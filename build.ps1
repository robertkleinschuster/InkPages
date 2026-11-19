# Builds InkPages (Release) with the CMake that ships with Visual Studio and runs the unit tests (GoogleTest, fetched once).
#   .\build.ps1            build + unit tests; the demo goes to .local-build\demo (for the sandbox UI tests)
#   .\build.ps1 -NoTests   build only
param([switch]$NoTests, [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
if (-not $vs) { throw 'Visual Studio not found (vswhere)' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$build = Join-Path $PSScriptRoot '.local-build\cmake'

& $cmake -S $PSScriptRoot -B $build -A x64
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config $Config
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

$demo = Join-Path $PSScriptRoot '.local-build\demo'
if (Test-Path $demo) { Remove-Item -Recurse -Force $demo }   # only the demo's own files go to the sandbox
New-Item -ItemType Directory -Force $demo | Out-Null
Copy-Item (Join-Path $build "$Config\InkPagesDemo.exe") $demo -Force
$tools = Join-Path $PSScriptRoot '.local-build\tools'
New-Item -ItemType Directory -Force $tools | Out-Null
Copy-Item (Join-Path $build "$Config\cursorprobe.exe"), (Join-Path $build "$Config\uiaprobe.exe") $tools -Force   # test tools (sandbox -Data)
"demo: $demo\InkPagesDemo.exe; tools: $tools"

if (-not $NoTests) {
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    & $ctest --test-dir $build -C $Config --output-on-failure
    if ($LASTEXITCODE) { throw "unit tests failed ($LASTEXITCODE)" }
}
