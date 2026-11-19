# Page operations keep the history, and the toolbar's buttons have names (native UI Automation finds them by name:
# uiaprobe; the managed client of Windows PowerShell sees comctl32 toolbars only as an empty pane).
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\PagesTest.ps1 -Data .local-build\tools\uiaprobe.exe
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
    "line $label"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 900; "key 0x{0:X}" -f $k }
# A toolbar button by (part of) its accessible name, through native UI Automation.
function Named($text) {
    $at = & C:\sbx\data\uiaprobe.exe InkPagesDemo $text
    if ($LASTEXITCODE -ne 0) { return $null }
    $xy = $at.Split(' ')
    return @([int]$xy[0], [int]$xy[1])
}
function Press($text) {
    $at = Named $text
    if ($at -eq $null) { throw "no button '$text'" }
    [Inj]::ClickPixel($at[0], $at[1]); Start-Sleep -Milliseconds 800
    [Inj]::MoveMouse(700, 340)
    "press $text"
}

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '3'
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)

    # 1. accessibility: the buttons are found by their names
    foreach ($name in @('(Strg+Z)', 'Stift', 'Textmarker', 'Radierer', 'Ganze Striche radieren', 'Formen', 'Lineal',
        '(Entf)', 'Seite einf', 'Seite l', 'Seite nach unten')) {
        $at = Named $name
        if ($at -eq $null) { "name NOT found: $name" } else { "name found: $name at $($at -join ',')" }
    }
    & C:\sbx\data\uiaprobe.exe InkPagesDemo | Out-File "$out\uia-tree.txt" -Encoding utf8

    # 2. a stroke on page 1, page 2 in view, a stroke on page 2
    Line 'page 1' 150 120 350 125
    Key 0x22
    Line 'page 2' 150 120 350 125
    # 3. insert a page after page 2, undo: page 2's stroke goes (the history followed), redo brings it back
    Press 'Seite einf'
    [Inj]::Ctrl(0x5A); Start-Sleep 1; "undo after insert"
    Shot '01-after-insert-undo'
    [Inj]::Ctrl(0x59); Start-Sleep 1; "redo"
    # 4. move page 1 down, then undo twice: both strokes go
    Key 0x24
    Press 'Seite nach unten'
    [Inj]::Ctrl(0x5A); Start-Sleep 1
    [Inj]::Ctrl(0x5A); Start-Sleep 1; "undo twice after move"
    Shot '02-after-move-undo'
}
