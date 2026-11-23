# UI test in Windows Sandbox with real (synthetic) pen and touch input. The sandbox's window (the client of
# `wsb connect`, which the test's interactive session needs) opens on a separate Win32 desktop (HiddenDesktop.cs), so
# it never shows on the user's screen; -Show opens it on the user's desktop to watch or debug a test.
# Copies an app build (-App, e.g. .local-build\cmake\demo\Release), optional test data and the test script into a shared folder,
# starts a sandbox, runs the script in its user session and collects screenshots and logs in <Work>\out.
# In-sandbox scripts dot-source C:\sbx\TestKit.ps1 (shared helpers: start the app, UI Automation, pen injection).
# The library's own tests: .\build.ps1 (the demo goes to .local-build\cmake\demo\Release), then
#   UiTests\Run-SandboxTest.ps1 -App .local-build\cmake\demo\Release -Test demo\UiTests\StartTest.ps1
# Apps that embed InkPages can use this runner and TestKit.ps1 for their own UI tests.
param(
    [Parameter(Mandatory)][string]$App,            # folder with the app (copied to C:\sbx\app)
    [Parameter(Mandatory)][string]$Test,           # in-sandbox script (copied to C:\sbx\test.ps1)
    [string[]]$Data = @(),                         # files copied to C:\sbx\data
    [string]$Work = (Join-Path $env:TEMP 'inkpages-sandbox-test'),
    [int]$TimeoutSeconds = 240,
    [switch]$Show                                  # the sandbox window on the user's desktop (default: its own desktop)
)
$ErrorActionPreference = 'Stop'

if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Force "$Work\data", "$Work\out" | Out-Null
robocopy $App "$Work\app" /MIR /NFL /NDL /NJH /NJS /MT:16 | Out-Null
foreach ($file in $Data) { Copy-Item $file "$Work\data" }
Copy-Item "$PSScriptRoot\*.cs", "$PSScriptRoot\TestKit.ps1" $Work -Exclude HiddenDesktop.cs   # Inj.cs, Finger.cs, CursorShot.cs
Copy-Item $Test "$Work\test.ps1"

if (-not $Show -and -not ('HiddenDesktop' -as [type])) { Add-Type -Path "$PSScriptRoot\HiddenDesktop.cs" }
$desktop = $null
$shown = @()   # sandbox client windows seen on the user's desktop during the run
$id = (wsb start --raw | ConvertFrom-Json).Id
try {
    wsb share --id $id -f $Work -s C:\sbx -w | Out-Null
    # The test needs an interactive session in the sandbox: `wsb exec -r ExistingLogin` fails with 0x80070520 (no logon
    # session) until a client is connected. The client window goes to a desktop of its own unless -Show.
    if ($Show) { Start-Process wsb -ArgumentList "connect --id $id" }
    else {
        $desktop = New-Object HiddenDesktop "InkPagesSandbox-$PID"
        $null = $desktop.Start("`"$((Get-Command wsb).Source)`" connect --id $id")
    }
    Start-Sleep 20
    wsb exec --id $id -r ExistingLogin -c 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\sbx\test.ps1' | Out-Null
    for ($i = 0; $i -lt $TimeoutSeconds / 2 -and -not (Test-Path "$Work\out\done.txt"); $i++) {
        if ($desktop) { $shown += [HiddenDesktop]::WindowsOnCurrentDesktop('WindowsSandboxRemoteSession') }
        Start-Sleep 2
    }
    if ($desktop) {
        "sandbox window on desktop $($desktop.Name): $($desktop.Windows('WindowsSandboxRemoteSession') -join '; ')"
        if ($shown) { Write-Warning "sandbox window on the user's desktop: $($shown | Select-Object -Unique)" }
        else { "sandbox window on the user's desktop: none (checked every 2 s)" }
    }
}
finally {
    wsb stop --id $id | Out-Null
    if ($desktop) { $desktop.Dispose() }
}
Get-Content "$Work\out\transcript.txt" | Select-String -Pattern '^(init|shot|exited|ERROR)'
"Results: $Work\out"
