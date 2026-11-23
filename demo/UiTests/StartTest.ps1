# The demo starts in Windows Sandbox: its window appears, it logs, and it ends cleanly when closed (phase 0 skeleton).
# UiTests\Run-SandboxTest.ps1 -App .local-build\cmake\demo\Release -Test demo\UiTests\StartTest.ps1
. C:\sbx\TestKit.ps1

Invoke-Test {
    $env:INKPAGES_DEMO_LOG = "$out\demo.log"
    Start-TestApp 'InkPagesDemo.exe'
    Start-Sleep 2
    Shot '01-window'
    $script:p.CloseMainWindow() | Out-Null
    if (-not $script:p.WaitForExit(5000)) { throw 'the demo did not end after its window was closed' }
    "exit code: $($script:p.ExitCode)"
    Get-Content "$out\demo.log" | ForEach-Object { "log: $_" }
}
