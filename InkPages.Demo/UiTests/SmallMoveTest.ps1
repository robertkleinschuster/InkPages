# A selection moved by a tiny amount, slowly (improving a sketch), must move exactly – not count as a tap that clears
# it (user, 2026-11-19: a 3-unit threshold swallowed such moves). A quick tap on the selection still clears it.
# Run-SandboxTest.ps1 -App .local-build\demo -Test InkPages.Demo\UiTests\SmallMoveTest.ps1; the demo log shows undo steps.
. C:\sbx\TestKit.ps1

function HoverAt($x, $y, $ms = 120) { for ($t = 0; $t -lt $ms; $t += 16) { [Inj]::PenFrame($x, $y, 0x22002, 1, 0); Start-Sleep -Milliseconds 16 } }

# A barrel drag from (x, y) by dx, dy in steps over about ms milliseconds (slow, as a careful correction).
function SlowBarrelDrag([double]$x, [double]$y, [double]$dx, [double]$dy, [int]$ms) {
    HoverAt $x $y
    [Inj]::PenFrame($x, $y, 0x12036, 1, 512)                       # down (tip + barrel)
    $steps = 20
    for ($i = 1; $i -le $steps; $i++) {
        [Inj]::PenFrame($x + $dx * $i / $steps, $y + $dy * $i / $steps, 0x22036, 1, 512); Start-Sleep -Milliseconds ($ms / $steps)
    }
    [Inj]::PenFrame($x + $dx, $y + $dy, 0x42002, 1, 0)             # up
    [Inj]::PenFrame($x + $dx, $y + $dy, 0x22000, 0, 0)
    Start-Sleep -Milliseconds 300
}

Invoke-Test {
    Start-TestApp 'InkPages.Demo.exe' '--light'
    Shot '01-start'
    [Inj]::PenStroke((L 580 220 720 220), $false, 512)
    [Inj]::PenStrokeFlags((L 570 205 735 205 735 235 570 235 570 205), 1, 512)   # barrel lasso
    Shot '02-selected'
    SlowBarrelDrag 650 220 1.5 1 700
    Shot '03-moved-by-1.5'
    "small move: $(@(Get-Content $env:INKPAGES_DEMO_LOG | Select-String 'undo: recorded TransformEdit').Count) TransformEdit(s)"
    SlowBarrelDrag 650 221 (-0.5) 0 500
    "half-unit move: $(@(Get-Content $env:INKPAGES_DEMO_LOG | Select-String 'undo: recorded TransformEdit').Count) TransformEdit(s)"
    # A quick tap on the selection clears it.
    HoverAt 650 221
    [Inj]::PenStrokeFlags((L 650 221 650 221.5), 1, 512)
    Shot '04-tap-cleared'
    "after tap: $(@(Get-Content $env:INKPAGES_DEMO_LOG | Select-String 'undo: recorded TransformEdit').Count) TransformEdit(s)"

    # A tip tap beside a selection ends it and leaves no dot; writing beside it ends it and keeps the stroke.
    function Strokes { @(Get-Content $env:INKPAGES_DEMO_LOG | Select-String 'undo: recorded PageEdit').Count }
    [Inj]::PenStrokeFlags((L 570 205 735 205 735 235 570 235 570 205), 1, 512)   # barrel lasso again
    Shot '05-selected-again'
    $before = Strokes
    HoverAt 400 400 200                                                           # away from the selection: the ink has the pen
    [Inj]::PenStroke((L 400 400 400.5 400), $false, 512)                         # a tap
    Shot '06-tap-beside'
    "tip tap beside: $((Strokes) - $before) new stroke edit(s) (expected 0)"
    [Inj]::PenStrokeFlags((L 570 205 735 205 735 235 570 235 570 205), 1, 512)
    $before = Strokes
    HoverAt 400 400 200
    [Inj]::PenStroke((L 400 400 480 420), $false, 512)                           # writing
    Shot '07-writing-beside'
    "writing beside: $((Strokes) - $before) new stroke edit(s) (expected 1)"
}
