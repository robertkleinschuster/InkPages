# Phase 2a tools: pen presets ink in their color, the highlighter, the eraser (exact and whole strokes), the pen's eraser
# end (taken on its inverted hover), undo/redo across two pages, the pen cursor on the first
# hover (cursorprobe) and the exact eraser's circle.
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\ToolsTest.ps1 -Data .local-build\tools\cursorprobe.exe
# Tools are chosen with the demo's keys (1/2/3 pens, 4 highlighter, 0 eraser, X whole strokes, Ctrl+Z/Y); the log
# (out\demo.log) says what happened: strokes with their color, erasing, undo/redo.
. C:\sbx\TestKit.ps1
Add-Type -Path C:\sbx\CursorShot.cs -ReferencedAssemblies System.Drawing

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
$PEN_INVERTED = 0x2; $PEN_ERASER = 0x4

function ZShot($name, $x, $y) { Start-Sleep -Milliseconds 400; [CursorShot]::ShotClient("$out\$name.png", $script:h, $x, $y); "shot $name" }
function Hover($x, $y, $pf) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, $pf, 0) } }
# A pen stroke (tip, or eraser end with $eraser) between client DIPs; hovers ~120 ms before it touches, as a real pen.
function Stroke($label, $x0, $y0, $x1, $y1, [switch]$eraser) {
    $hoverFlags = $(if ($eraser) { $PEN_INVERTED } else { 0 })
    $touchFlags = $(if ($eraser) { $PEN_INVERTED -bor $PEN_ERASER } else { 0 })
    Hover $x0 $y0 $hoverFlags
    [Inj]::PenFrame($x0, $y0, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, $touchFlags, 600)
    $n = [Math]::Max(1, [int]([Math]::Sqrt(($x1 - $x0) * ($x1 - $x0) + ($y1 - $y0) * ($y1 - $y0)) / 4))
    for ($s = 1; $s -le $n; $s++) { [Inj]::PenFrame($x0 + ($x1 - $x0) * $s / $n, $y0 + ($y1 - $y0) * $s / $n, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, $touchFlags, 600) }
    [Inj]::PenFrame($x1, $y1, $INRANGE -bor $UP -bor $PRIMARY, $hoverFlags, 0)
    [Inj]::PenFrame($x1, $y1, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 500
    "stroke ${label}: $x0,$y0 -> $x1,$y1$(if ($eraser) { ' (eraser end)' })"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 500; "key 0x{0:X}" -f $k }
function Ctrl($k) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 600; "ctrl+0x{0:X}" -f $k }
function Snap($name) {
    Set-Content "$out\snap-$name.req" 'x'
    for ($i = 0; $i -lt 40 -and (Test-Path "$out\snap-$name.req"); $i++) { Start-Sleep -Milliseconds 50 }
    "snap $name"
}

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '3'
    Start-Process C:\sbx\data\cursorprobe.exe -ArgumentList "$out 200" -WindowStyle Hidden
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(400, 400)
    Shot '00-start'

    # 1. the pen cursor appears on the first hover (the presenter's first pointer event activates it)
    Hover 200 100 0; Hover 205 102 0
    Snap 'first-hover'

    # 2. presets: black, blue, red; the highlighter over black
    Stroke 'black' 100 100 400 110
    Key 0x32; Stroke 'blue' 100 150 400 160
    Key 0x33; Stroke 'red' 100 200 400 210
    Key 0x34; Stroke 'highlighter' 100 105 400 115
    ZShot '01-presets' 250 150

    # 3. exact eraser (default): a cut through blue leaves two pieces; its circle shows while hovering
    Key 0x30
    Hover 250 130 0; Hover 252 132 0
    ZShot '02-eraser-circle' 252 132
    Stroke 'exact erase' 250 130 250 180
    ZShot '03-exact-erased' 250 155

    # 4. whole strokes: the eraser takes red entirely
    Key 0x58
    Stroke 'whole erase' 300 190 300 225
    ZShot '04-whole-erased' 300 205

    # 5. the pen's eraser end with the pen tool: whole strokes - black goes
    Key 0x31
    Stroke 'eraser end (whole)' 350 90 350 125 -eraser
    # exact again: the eraser end is taken on its inverted hover and cuts blue's left piece
    Key 0x58
    Stroke 'eraser end (exact)' 150 140 150 170 -eraser
    ZShot '05-eraser-end' 200 110

    # 6. a preset's menu (its dropdown arrow): green for the first pen, then it inks green
    [Inj]::Click(75, 11); Start-Sleep -Milliseconds 800
    Shot '05b-preset-menu'
    for ($i = 0; $i -lt 4; $i++) { [Inj]::Key(0x28); Start-Sleep -Milliseconds 150 }
    [Inj]::Key(0x0D); Start-Sleep -Milliseconds 600
    [Inj]::MoveMouse(400, 400)
    Stroke 'green from the menu' 450 100 650 110

    # 7. undo/redo: everything back step by step, then forward again
    Ctrl 0x5A; Ctrl 0x5A; Ctrl 0x5A; Ctrl 0x5A
    ZShot '06-undone-4' 250 150
    Ctrl 0x59; Ctrl 0x59; Ctrl 0x59; Ctrl 0x59
    ZShot '07-redone-3' 250 150

    # 8. undo across two pages: a stroke on page 1, one on page 2, undone in time order
    Key 0x32; Stroke 'page 1' 100 300 300 300
    Key 0x22; Start-Sleep 1
    Stroke 'page 2' 100 120 300 120
    Ctrl 0x5A
    ZShot '08-page2-undone' 200 120
    Ctrl 0x5A
    Key 0x24; Start-Sleep 1
    ZShot '09-page1-undone' 200 300
    Ctrl 0x59; Ctrl 0x59
    ZShot '10-both-redone' 200 300
}
