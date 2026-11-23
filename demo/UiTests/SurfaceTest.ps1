# The writing surface (phase 1): finger pan with inertia and pinch zoom, mouse wheel and Ctrl+wheel, pen ink under the
# pen after zoom and pan, a stroke on page 2 after scrolling there, lazy pages (a page far away gives its strokes to
# the app and gets them back), pen + finger at once, and the pen cursor as the screen draws it (cursorprobe).
#   UiTests\Run-SandboxTest.ps1 -App .local-build\cmake\demo\Release -Test demo\UiTests\SurfaceTest.ps1 -Data .local-build\cmake\tools\Release\cursorprobe.exe
# Every pen stroke is logged here with the client DIPs it was injected at ("pen <name>: x0,y0 -> x1,y1"); the library
# logs each stroke's first/last ink point mapped back to client DIPs ("ink: stroke on page N ...", out\demo.log).
. C:\sbx\TestKit.ps1
Add-Type -Path C:\sbx\CursorShot.cs -ReferencedAssemblies System.Drawing
Add-Type -Path C:\sbx\Finger.cs

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000

function ZShot($name, $x, $y) { Start-Sleep -Milliseconds 400; [CursorShot]::ShotClient("$out\$name.png", $script:h, $x, $y); "shot $name" }
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
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 900; "key 0x{0:X}" -f $k }
function Snap($name) {
    Set-Content "$out\snap-$name.req" 'x'
    for ($i = 0; $i -lt 40 -and (Test-Path "$out\snap-$name.req"); $i++) { Start-Sleep -Milliseconds 50 }
    "snap $name"
}

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '12'
    Start-Process C:\sbx\data\cursorprobe.exe -ArgumentList "$out 240" -WindowStyle Hidden
    Start-TestApp 'InkPagesDemo.exe'
    [Finger]::Init($script:h)
    [Inj]::MoveMouse(400, 5)
    Shot '00-start'

    # 1. fit width: ink lands under the pen
    PenLine 'a (fit width)' 150 120 400 140
    ZShot '01-ink' 400 140

    # 2. finger drag up: pan with inertia, then ink
    [Inj]::TouchDrag(500, 450, 480, 250, 20); "touch drag 500,450 -> 480,250"
    Start-Sleep 2
    PenLine 'b (after pan)' 150 250 400 270
    ZShot '02-after-pan' 400 270

    # 3. pinch zoom in, then ink; zoom crops show the paper and the ink sharp
    [Inj]::Pinch(400, 300, 80, 280, 25); "pinch 400,300 80 -> 280"
    Start-Sleep 2
    PenLine 'c (after pinch)' 200 200 450 230
    ZShot '03-after-pinch' 450 230

    # 4. Ctrl+wheel zooms, wheel scrolls (DirectManipulation ProcessInput)
    [Finger]::Wheel(500, 300, 2, $true); "ctrl+wheel up 2"
    Start-Sleep 1.5
    ZShot '04-ctrl-wheel' 500 300
    [Finger]::Wheel(500, 300, -3, $false); "wheel down 3"
    Start-Sleep 1.5
    ZShot '05-wheel' 500 300
    PenLine 'd (after wheel)' 250 200 450 260

    # 5. zoom 4 (key T): the paper is drawn anew, sharp
    Key 0x54
    PenLine 'e (zoom 4)' 300 250 450 280
    ZShot '06-zoom4' 450 280

    # 6. fit width, page 2 (Page Down): a stroke on page 2
    Key 0x46; Key 0x24; Key 0x22
    PenLine 'f (page 2)' 150 150 400 170
    ZShot '07-page2' 400 170

    # 7. lazy pages: far away (page 11) pages 1 and 2 give their strokes away; back home they come back
    for ($i = 0; $i -lt 9; $i++) { [Inj]::Key(0x22); Start-Sleep -Milliseconds 400 }
    Start-Sleep 1.5
    Shot '08-page11'
    Key 0x24
    Start-Sleep 1
    Shot '09-home-again'

    # 8. pen and finger at once: the finger pans while the pen writes
    Hover 150 330
    [Inj]::PenFrame(150, 330, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 512)
    [Finger]::Down(700, 450)
    for ($i = 1; $i -le 50; $i++) {
        [Finger]::Move(700, 450 - 4 * $i)
        [Inj]::PenFrame(150 + 4 * $i, 330 + 0.4 * $i, $INRANGE -bor $INCONTACT -bor $UPDATE -bor $FIRST -bor $PRIMARY, 0, 512)
    }
    [Finger]::Up(700, 250)
    [Inj]::PenFrame(350, 350, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    [Inj]::PenFrame(350, 350, $UPDATE -bor $PRIMARY, 0, 0)
    "pen g (finger pans meanwhile): 150,330 -> 350,350; finger 700,450 -> 700,250"
    Start-Sleep 2
    Shot '10-pen-and-finger'

    # 9. the pen cursor: the blue preset's dot - the screen draws it at the pen
    Key 0x32
    Hover 300 300; Hover 320 310
    Snap 'cursor-blue'
    [Inj]::PenFrame(320, 310, $UPDATE -bor $PRIMARY, 0, 0)

    # 10. pinch out to the minimum: the whole document
    [Inj]::Pinch(500, 300, 300, 30, 25); "pinch out"
    Start-Sleep 2
    Shot '11-zoomed-out'
}
