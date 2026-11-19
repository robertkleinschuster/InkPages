# UI test in Windows Sandbox with real (synthetic) pen and touch input (the sandbox window opens on the desktop).
# Copies an app build (-App, e.g. .local-build\demo), optional test data and the test script into a shared folder,
# starts a sandbox, runs the script in its user session and collects screenshots and logs in <Work>\out.
# In-sandbox scripts dot-source C:\sbx\TestKit.ps1 (shared helpers: start the app, UI Automation, pen injection).
# The library's own tests: .\build.ps1 (the demo goes to .local-build\demo), then
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\StartTest.ps1
# Apps that embed InkPages can use this runner and TestKit.ps1 for their own UI tests.
param(
    [Parameter(Mandatory)][string]$App,            # folder with the app (copied to C:\sbx\app)
    [Parameter(Mandatory)][string]$Test,           # in-sandbox script (copied to C:\sbx\test.ps1)
    [string[]]$Data = @(),                         # files copied to C:\sbx\data
    [string]$Work = (Join-Path $env:TEMP 'inkpages-sandbox-test'),
    [int]$TimeoutSeconds = 240
)
$ErrorActionPreference = 'Stop'

if (Test-Path $Work) { Remove-Item -Recurse -Force $Work }
New-Item -ItemType Directory -Force "$Work\data", "$Work\out" | Out-Null
robocopy $App "$Work\app" /MIR /NFL /NDL /NJH /NJS /MT:16 | Out-Null
foreach ($file in $Data) { Copy-Item $file "$Work\data" }
Copy-Item "$PSScriptRoot\*.cs", "$PSScriptRoot\TestKit.ps1" $Work   # Inj.cs, Finger.cs, CursorShot.cs
Copy-Item $Test "$Work\test.ps1"

$id = (wsb start --raw | ConvertFrom-Json).Id
try {
    wsb share --id $id -f $Work -s C:\sbx -w | Out-Null
    Start-Process wsb -ArgumentList "connect --id $id"   # the test needs an interactive session
    Start-Sleep 20
    wsb exec --id $id -r ExistingLogin -c 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\sbx\test.ps1' | Out-Null
    for ($i = 0; $i -lt $TimeoutSeconds / 2 -and -not (Test-Path "$Work\out\done.txt"); $i++) { Start-Sleep 2 }
}
finally {
    wsb stop --id $id | Out-Null
}
Get-Content "$Work\out\transcript.txt" | Select-String -Pattern '^(init|shot|exited|ERROR)'
"Results: $Work\out"
