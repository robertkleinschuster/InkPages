# The paper's ruling ends at the sheet: no line sticks out on the right or at the bottom (user, 2026-11-19).
# Run-SandboxTest.ps1 -App .local-build\demo -Test InkPages.Demo\UiTests\PaperEdgeTest.ps1; look at the screenshots.
. C:\sbx\TestKit.ps1

Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    Shot '01-top'
    # Scroll to the end of the first page (touch pan), so its bottom edge is in view.
    [Inj]::TouchDrag(700, 450, 700, 150, 30)
    Start-Sleep 1
    Shot '02-bottom-of-page'
}
