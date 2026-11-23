# Phase 2b/2c: selection (barrel lasso with the pen tool, rectangle), the pen over the selection moves it (window input,
# the demo's card with the strokes), undo/redo of the move, a tap inside keeps it and a tap outside clears it (no dot), duplicate,
# delete, copy/paste (Windows ink's clipboard), a drag to the view's edge auto-scrolls onto the next page, strokes cut
# at the page edge, shapes, shape recognition, Windows' ruler.
#   UiTests\Run-SandboxTest.ps1 -App .local-build\cmake\demo\Release -Test demo\UiTests\SelectionTest.ps1
# The log (out\demo.log) says what happened: selections, drags, moves (with their pages), edits, recognition.
. C:\sbx\TestKit.ps1
Add-Type -Path C:\sbx\CursorShot.cs -ReferencedAssemblies System.Drawing

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $SECOND = 0x20; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
$BARREL = 0x1

function ZShot($name, $x, $y) { Start-Sleep -Milliseconds 400; [CursorShot]::ShotClient("$out\$name.png", $script:h, $x, $y); "shot $name" }
function Hover($x, $y, $pf) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, $pf, 0) } }
# A pen drag through client DIP points; $pf: pen flags (barrel); $hold: frames to stay at the end before lifting.
function Drag($label, [double[]]$pts, $pf = 0, $hold = 0) {
    $buttons = $FIRST -bor $(if ($pf -band $BARREL) { $SECOND } else { 0 })
    Hover $pts[0] $pts[1] $pf
    [Inj]::PenFrame($pts[0], $pts[1], $INRANGE -bor $INCONTACT -bor $DOWN -bor $buttons -bor $PRIMARY, $pf, 600)
    for ($i = 2; $i + 1 -lt $pts.Length; $i += 2) {
        $x0 = $pts[$i - 2]; $y0 = $pts[$i - 1]; $x1 = $pts[$i]; $y1 = $pts[$i + 1]
        $n = [Math]::Max(1, [int]([Math]::Sqrt(($x1 - $x0) * ($x1 - $x0) + ($y1 - $y0) * ($y1 - $y0)) / 4))
        for ($s = 1; $s -le $n; $s++) { [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $buttons -bor $PRIMARY, $pf, 600) }
    }
    $xe = $pts[$pts.Length - 2]; $ye = $pts[$pts.Length - 1]
    for ($i = 0; $i -lt $hold; $i++) { [Inj]::PenFrame($xe, $ye, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $buttons -bor $PRIMARY, $pf, 600) }
    [Inj]::PenFrame($xe, $ye, $INRANGE -bor $UP -bor $PRIMARY, $pf, 0)
    [Inj]::PenFrame($xe, $ye, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 600
    "drag ${label}"
}
function Tap($label, $x, $y) {
    Hover $x $y 0
    [Inj]::PenFrame($x, $y, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    [Inj]::PenFrame($x, $y, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame($x, $y, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 600
    "tap ${label}"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 700; "key 0x{0:X}" -f $k }
function Ctrl($k) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 700; "ctrl+0x{0:X}" -f $k }
$lasso = @(230, 190, 350, 190, 350, 300, 230, 300, 230, 190)

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '3'
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)
    Key 0x51  # zoom 0.5: the demo's cards are in view
    Drag 'stroke 1' @(240, 200, 340, 205)
    Drag 'stroke 2' @(240, 215, 340, 220)
    Shot '00-content'

    # 1. barrel lasso with the pen tool: the strokes and the card
    Drag 'barrel lasso' $lasso $BARREL
    ZShot '01-selected' 290 240

    # 2. a barrel drag over the selection moves it (window input) together with the card
    Hover 290 240 0; Hover 291 241 0
    Drag 'barrel move' @(291, 241, 391, 241) $BARREL
    if ((Get-Content "$out\demo.log" -Raw) -notmatch 'selection: 2 strokes moved') { throw 'Barrel drag did not move the selection' }
    ZShot '02-moved' 390 240
    Ctrl 0x5A; ZShot '03-move-undone' 290 240
    Ctrl 0x59; ZShot '04-move-redone' 390 240

    # 3. a tap beside the selection clears it, without a dot
    Drag 'barrel lasso again' @(330, 190, 450, 190, 450, 300, 330, 300, 330, 190) $BARREL
    Tap 'beside' 500 120
    ZShot '05-tap-beside' 500 120

    # 4. duplicate with the pen button, copy, delete, undo; paste
    Drag 'barrel lasso 3' @(330, 190, 450, 190, 450, 300, 330, 300, 330, 190) $BARREL
    Drag 'barrel duplicate' @(391, 241) $BARREL
    ZShot '06-duplicated' 400 260
    Ctrl 0x43
    Key 0x2E
    ZShot '07-deleted' 400 260
    Ctrl 0x5A
    Ctrl 0x56
    ZShot '08-pasted' 370 200

    # 5. an inside tap keeps the selection, an outside tap clears it
    Key 0x31
    Drag 'stroke 3' @(250, 120, 320, 125)
    Key 0x39
    Drag 'rectangle' @(240, 110, 330, 135)
    Hover 285 122 0; Hover 286 123 0
    Tap 'on the selection' 286 123
    ZShot '08b-inside-keeps-selection' 286 123
    Tap 'outside the selection' 350 140
    Drag 'reselect for frame actions' @(240, 110, 330, 135)
    Drag 'rotate with the right handle' @(344, 123, 285, 182)
    ZShot '08c-rotated' 285 123
    Ctrl 0x5A
    Drag 'reselect after undo' @(240, 110, 330, 135)
    Tap 'duplicate with the frame button' 226 141
    Tap 'delete the duplicate with the frame button' 234 124
    $selectionLog = Get-Content "$out\demo.log" -Raw
    foreach ($handle in 6, 7, 8) {
        if ($selectionLog -notmatch "selection: drag begins \($handle\)") { throw "Selection handle $handle was not reached" }
    }

    # 6. shapes: a line; recognition: a drawn circle becomes an ellipse
    Key 0x37
    Drag 'line shape' @(380, 100, 480, 130)
    Key 0x52; Key 0x31
    $circle = @(); for ($a = 0; $a -le 360; $a += 15) { $r = [Math]::PI * $a / 180; $circle += 300 + 25 * [Math]::Cos($r); $circle += 170 + 25 * [Math]::Sin($r) }
    Drag 'circle to recognize' $circle
    Start-Sleep 2
    ZShot '09-shapes' 340 160
    Key 0x52

    # 7. a stroke across the gap between pages 1 and 2 is cut into two strokes, one per page
    [Inj]::TouchDrag(650, 330, 650, 130, 15); Start-Sleep 2   # the gap comes into view
    Drag 'across the gap' @(300, 60, 310, 340)
    Shot '10-gap'

    # 8. drag a selection to the view's bottom edge: the pages scroll, the strokes land further down
    Start-Sleep 2   # the pan's inertia ends
    Drag 'stroke 4' @(380, 200, 460, 205)
    Drag 'barrel lasso 4' @(370, 185, 470, 185, 470, 220, 370, 220, 370, 185) $BARREL
    Hover 420 202 0; Hover 421 203 0
    Drag 'to the edge' @(421, 203, 421, 360) 0 60
    ZShot '11-auto-scrolled' 421 300

    # 9. Windows' ruler
    Key 0x38
    Shot '12-ruler'
    Key 0x38
}
