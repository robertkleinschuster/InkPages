# The page in view is outlined in the accent color (MarkCurrentPage): at fit width, at zoom 0.5 and 2, while a finger
# drag scrolls towards the next page and after it. Screenshots (and crops of the outline) show it.
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\MarkTest.ps1
. C:\sbx\TestKit.ps1

function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 1200; "key 0x{0:X}" -f $k }

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '3'
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)
    Start-Sleep 1
    Shot '01-fit-width'
    Key 0x51  # Q: zoom 0.5
    Shot '02-zoom-half'
    Key 0x45  # E: zoom 2
    Shot '03-zoom-2'
    Key 0x46  # F: fit width again
    # a finger drag up: page 2 comes into view; a shot while the pages still coast, one after
    [Inj]::TouchDrag(650, 330, 650, 60, 12)
    Shot '04-scrolling'
    Start-Sleep 2
    Shot '05-next-page'
}
