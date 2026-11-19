# The mouse never writes, it selects (whatever tool is active): a drag lassos, on the selection it moves it, a click
# beside it clears it, a click on a picture selects it; a click on nothing and a finger tap are the app's taps, a finger
# pan is no tap.
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\MouseTest.ps1
# The log (out\demo.log) says what happened: "mouse: ...", "touch: ...", "demo: tap ...", selections, edits.
. C:\sbx\TestKit.ps1

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function Line($label, $x0, $y0, $x1, $y1) {
    Hover $x0 $y0
    [Inj]::PenFrame($x0, $y0, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    $n = [Math]::Max(1, [int]([Math]::Abs($x1 - $x0) / 4))
    for ($s = 1; $s -le $n; $s++) { [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, 0, 600) }
    [Inj]::PenFrame($x1, $y1, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x1, $y1, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 600
    "pen line $label"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 700; "key 0x{0:X}" -f $k }
function MDrag($label, [double[]]$pts) { [Inj]::MouseDrag($pts); Start-Sleep -Milliseconds 700; "mouse drag $label" }
function MClick($label, $x, $y) { [Inj]::Click($x, $y); Start-Sleep -Milliseconds 700; "mouse click $label" }

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '3'
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)
    Key 0x51  # zoom 0.5: the demo's cards are in view
    Line 'stroke 1' 240 200 340 205
    Line 'stroke 2' 240 215 340 220
    Shot '00-content'

    # 1. the pen tool is active: a mouse drag does not write (it lassos nothing here)
    MDrag 'with the pen tool' @(150, 120, 350, 125)
    Shot '01-mouse-drag-no-ink'
    # 2. a mouse click on a card selects it (as the barrel tap does); a click beside clears it (no tap)
    MClick 'on the card' 290 275
    Shot '02-card-clicked'
    MClick 'beside' 500 120
    # 3. a click on nothing (no selection) is the app's tap
    MClick 'nothing' 500 120
    # 4. a mouse lasso around the strokes and the card selects them, a drag on the selection moves them
    MDrag 'lasso' @(230, 190, 350, 190, 350, 300, 230, 300, 230, 190)
    Shot '03-lasso'
    MDrag 'move' @(290, 240, 390, 240)
    Shot '04-moved'
    # 5. rectangle mode (9): a mouse drag selects the rectangle; a click on the selection clears it
    Key 0x39
    MDrag 'rectangle' @(330, 185, 460, 310)
    Shot '05-rectangle'
    MClick 'on the selection' 390 240
    Key 0x31
    # 6. a finger tap is the app's tap; a finger pan is not (and still pans)
    [Inj]::TouchTap(500, 120); Start-Sleep -Milliseconds 700; "finger tap"
    [Inj]::TouchDrag(650, 300, 650, 200, 10); Start-Sleep 2; "finger pan"
    [Inj]::TouchTap(500, 120); Start-Sleep -Milliseconds 700; "finger tap after the pan"
    Shot '06-after-touch'
}
