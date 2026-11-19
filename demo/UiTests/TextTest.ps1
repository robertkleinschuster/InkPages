# The Text tool: a pen tap writes a text (typed with the keyboard, umlauts included), a tap beside ends it, a tap on
# the text edits it, undo/redo; the selection (mouse) selects and moves a text, undo; an empty new text is nothing; at
# another zoom the box lies on the text.
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\TextTest.ps1
# The log (out\demo.log) says what happened: "text: ...", "demo: text ...", selections, undo.
# (Umlauts as [char] codes: Windows PowerShell 5.1 reads a script without BOM in the ANSI code page.)
. C:\sbx\TestKit.ps1

$INRANGE = 0x2; $INCONTACT = 0x4; $FIRST = 0x10; $PRIMARY = 0x2000; $DOWN = 0x10000; $UPDATE = 0x20000; $UP = 0x40000
function Hover($x, $y) { for ($i = 0; $i -lt 15; $i++) { [Inj]::PenFrame($x, $y, $INRANGE -bor $UPDATE -bor $PRIMARY, 0, 0) } }
function PenTap($label, $x, $y) {
    Hover $x $y
    [Inj]::PenFrame($x, $y, $INRANGE -bor $INCONTACT -bor $DOWN -bor $FIRST -bor $PRIMARY, 0, 600)
    [Inj]::PenFrame($x, $y, $INRANGE -bor $UP -bor $PRIMARY, 0, 0)
    Hover $x $y
    [Inj]::PenFrame($x, $y, $UPDATE -bor $PRIMARY, 0, 0)
    Start-Sleep -Milliseconds 700
    "pen tap $label"
}
function Key($k) { [Inj]::Key($k); Start-Sleep -Milliseconds 600; "key 0x{0:X}" -f $k }
function Ctrl($k) { [Inj]::Ctrl($k); Start-Sleep -Milliseconds 700; "ctrl+0x{0:X}" -f $k }
function Write-Typed($text) { [Inj]::TypeText($text); Start-Sleep -Milliseconds 500; "typed $($text.Length) characters" }
function MDrag($label, [double[]]$pts) { [Inj]::MouseDrag($pts); Start-Sleep -Milliseconds 700; "mouse drag $label" }

$ae = [char]0xE4; $oe = [char]0xF6; $ue = [char]0xFC; $sz = [char]0xDF; $bigAe = [char]0xC4; $euro = [char]0x20AC

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '3'
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)
    Key 0x53  # S: the Text tool

    # 1. a new text: tap, type (umlauts, a second line), tap beside
    PenTap 'new text' 200 140
    Write-Typed "Gr$($ue)$($sz)e $($ae)$($oe)$($ue) $($bigAe) $euro`nZeile 2"
    Shot '01-typing'
    PenTap 'beside' 520 300
    Shot '02-ended'

    # 2. edit it: tap on it, End, type, tap beside; undo and redo the change
    PenTap 'on the text' 215 140
    Key 0x23  # End
    Write-Typed ' neu'
    Shot '03-editing-again'
    PenTap 'beside again' 520 300
    Ctrl 0x5A
    Shot '04-change-undone'
    Ctrl 0x59

    # 3. an empty new text is nothing
    PenTap 'empty text' 300 280
    PenTap 'beside the empty one' 520 330

    # 4. the selection: a mouse lasso around the text selects it, a drag moves it, undo puts it back
    Key 0x36  # 6: select
    MDrag 'lasso around the text' @(150, 110, 450, 110, 450, 200, 150, 200, 150, 110)
    Shot '05-selected'
    MDrag 'move' @(250, 150, 330, 230)
    Shot '06-moved'
    Ctrl 0x5A
    Shot '07-move-undone'

    # 5. at zoom 2 the edited box lies on the text
    Key 0x53
    Key 0x45  # E: zoom 2
    Start-Sleep 1
    Shot '08-zoom2'
    PenTap 'on the text at zoom 2' 260 120
    Shot '09-zoom2-editing'
    [Inj]::Key(0x1B); Start-Sleep -Milliseconds 600; 'escape'
    Shot '10-escaped'
}
