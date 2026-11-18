# The current page's mark (the accent outline around the page the toolbar drives), light and dark: at the start (page 1)
# and after scrolling the next page into the middle of the view (Run-SandboxTest.ps1 -Test this, the demo as -App).
. C:\sbx\TestKit.ps1

Invoke-Test {
    foreach ($theme in 'light', 'dark') {
        Start-TestApp 'InkPages.Demo.exe' "--$theme"
        Shot "$theme-01-first-page-current"
        [Inj]::TouchDrag(500, 480, 500, 40, 40) # touch scrolls (client DIPs): the next page comes into the middle
        [Inj]::TouchDrag(500, 480, 500, 40, 40)
        Start-Sleep -Milliseconds 800
        Shot "$theme-02-after-scrolling"
        Button 'StencilButton' # the ruler appears on the marked page
        Shot "$theme-03-ruler-on-marked-page"
        Stop-TestApp
    }
}

