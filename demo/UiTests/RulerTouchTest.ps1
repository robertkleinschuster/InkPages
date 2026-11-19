# Open points of phase 2: a selection tap on a picture selects it; the frame keeps its screen size when zooming;
# Windows' ruler and touch: does a finger move the ruler, or does DirectManipulation pan the pages?
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\RulerTouchTest.ps1
. C:\sbx\TestKit.ps1

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function Tap($x, $y) {
    Hover $x $y
    [Inj]::PenFrame($x, $y, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    [Inj]::PenFrame($x, $y, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x, $y, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 600
    "tap $x,$y"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 900; "key 0x{0:X}" -f $k }

Invoke-Test {
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)
    Key 0x51                 # zoom 0.5: the demo's cards are in view
    Key 0x36                 # the selection tool
    Tap 290 265              # on the first card
    Shot '01-picture-selected'
    Key 0x57                 # zoom 1: the frame keeps its screen size
    Shot '02-zoomed'
    Key 0x51
    Key 0x31
    Key 0x38                 # Windows' ruler
    Shot '03-ruler'
    [Inj]::TouchDrag(330, 230, 330, 130, 20); "touch drag on the ruler"
    Start-Sleep 2
    Shot '04-after-touch-drag'
}
