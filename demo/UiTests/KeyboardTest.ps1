# The touch keyboard for the Text tool: a pen tap starts a text box, UI Automation sees it as the focused edit control
# with the text pattern (uiaprobe --focus), and whether Windows' touch keyboard comes up (uiaprobe --keyboard, screenshot)
# - first with the sandbox's default setting, then with "show the touch keyboard: always" (TabletTip\1.7\
# TouchKeyboardTapInvoke = 2, Settings > Time & language > Typing; a sandbox has a keyboard attached).
#   UiTests\Run-SandboxTest.ps1 -App .local-build\demo -Test demo\UiTests\KeyboardTest.ps1 -Data .local-build\tools\uiaprobe.exe
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
function Probe($label) {
    Start-Sleep 2
    "-- $label"
    $focus = & C:\sbx\data\uiaprobe.exe --focus 2>&1
    "focus probe exit $LASTEXITCODE"
    $focus | ForEach-Object { "  $_" }
    $keyboard = & C:\sbx\data\uiaprobe.exe --keyboard 2>&1
    $keyboard | ForEach-Object { "  $_" }
}

Invoke-Test {
    $env:INKPAGES_DEMO_PAGES = '2'
    "touch keyboard setting before: " + (Get-ItemProperty 'HKCU:\Software\Microsoft\TabletTip\1.7' -ErrorAction SilentlyContinue).TouchKeyboardTapInvoke
    Start-TestApp 'InkPagesDemo.exe'
    [Inj]::MoveMouse(700, 340)
    Key 0x53  # S: the Text tool

    # 1. the sandbox's default setting
    PenTap 'new text' 200 140
    [Inj]::TypeText('Hallo')
    Probe 'editing (default setting)'
    Shot '01-default'
    PenTap 'beside' 520 300
    Probe 'ended'

    # 2. "show the touch keyboard: always"
    New-Item -Force 'HKCU:\Software\Microsoft\TabletTip\1.7' | Out-Null
    Set-ItemProperty 'HKCU:\Software\Microsoft\TabletTip\1.7' TouchKeyboardTapInvoke 2 -Type DWord
    Get-Process TextInputHost -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 2
    PenTap 'the text again' 215 140
    Probe 'editing (always)'
    PenTap 'into the edited box' 220 140
    Probe 'a tap into the box (always)'
    [Inj]::TouchTap(220, 140); 'finger tap into the edited box'
    Probe 'a finger tap into the box (always)'
    Shot '02b-finger'
    Shot '02-always'
    PenTap 'beside again' 520 300
    PenTap 'a new text (always)' 300 260
    Probe 'second new text (always)'
    Shot '03-always-new'
}
