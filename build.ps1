# Builds InkPages and runs its unit tests. The configuration is CMakePresets.json (also what Visual Studio, VS Code and
# CLion use); this script only enters the x64 developer environment and calls CMake and CTest with the presets.
#   .\build.ps1                  Release build + unit tests
#   .\build.ps1 -NoTests         build only
#   .\build.ps1 -Config Debug
# Outputs in .local-build\cmake: demo\<Config>\InkPagesDemo.exe, tools\<Config>\{uiaprobe,cursorprobe}.exe
param([switch]$NoTests, [ValidateSet('Release', 'Debug')][string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
if (-not $env:VSCMD_VER) {
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    if (-not $vs) { throw 'Visual Studio or Build Tools not found (vswhere); see .vsconfig for the components' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    if (-not $env:VSCMD_VER) { throw "Enter-VsDevShell did not set up the developer environment ($vs)" }
}
$preset = $Config.ToLowerInvariant()
Push-Location $PSScriptRoot
try {
    cmake --preset default
    if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
    cmake --build --preset $preset
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
    if (-not $NoTests) {
        ctest --preset $preset
        if ($LASTEXITCODE) { throw "unit tests failed ($LASTEXITCODE)" }
    }
}
finally { Pop-Location }
