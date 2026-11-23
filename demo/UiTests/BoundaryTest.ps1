# A stroke across the gap between two pages: one presenter over the document collects it as one stroke, owned by the
# page where it began (the log says which, with its first and last point); the screenshot shows how it is drawn.
#   UiTests\Run-SandboxTest.ps1 -App .local-build\cmake\demo\Release -Test demo\UiTests\BoundaryTest.ps1
. C:\sbx\TestKit.ps1

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function PenLine($label, $x0, $y0, $x1, $y1) {
    Hover $x0 $y0
    [Inj]::PenFrame($x0, $y0, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 512)
    $n = [Math]::Max(1, [int]([Math]::Sqrt(($x1 - $x0) * ($x1 - $x0) + ($y1 - $y0) * ($y1 - $y0)) / 4))
    for ($s = 1; $s -le $n; $s++) { [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, 0, 512) }
    [Inj]::PenFrame($x1, $y1, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x1, $y1, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 500
    "pen ${label}: $x0,$y0 -> $x1,$y1 (client DIPs)"
}

Invoke-Test {
    $env:INKPAGES_DEMO_LOG = "$out\demo.log"
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(400, 5)
    [Inj]::Key(0x51); Start-Sleep 1   # zoom 0.5
    [Inj]::TouchDrag(600, 300, 600, 150, 15); Start-Sleep 2   # the gap between pages 1 and 2 comes into view
    Shot '01-gap-in-view'
    PenLine 'across' 300 40 340 340
    PenLine 'beside' 420 40 460 340
    Shot '02-across-the-gap'
}
