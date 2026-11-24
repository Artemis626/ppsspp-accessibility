# Automated tests for the Discovery layer, Phase D1.
#
#   1. The core library on its own (no PPSSPP): discovery-core-test.ps1.
#   2. System dialogs: a PSP test program that shows message dialogs. Checks
#      that their text is captured, located in game memory, and spoken.
#   3. The system font library: PSP test programs that draw glyphs. Checks
#      that the system-call tap sees them and the ledger records them.
#
# The PSP programs come from pspautotests and have source code, so what they
# display is known. Usage:
#   powershell -File Accessibility\tools\discovery-test.ps1 -BuildDir ..\builds\access-dev
param([Parameter(Mandatory = $true)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path
$exe = Join-Path $BuildDir 'PPSSPPWindows64.exe'
$log = Join-Path $BuildDir 'accessibility\logs\access.log'
$script:failed = 0

function Report($ok, $what) {
    if ($ok) { "PASS $what" } else { $script:failed++; "FAIL $what" }
}

# Runs the build on a PSP program until $Until appears in the log (or the
# time runs out), closes it, and returns the log lines.
function Run-Program($program, $extraArgs, $Until, $seconds) {
    if (Test-Path $log) { Remove-Item $log -Force }
    $argList = @("`"$program`"") + $extraArgs
    $p = Start-Process $exe -ArgumentList $argList -WorkingDirectory $BuildDir -PassThru
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        Start-Sleep -Milliseconds 500
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern $Until -Quiet)) { break }
    }
    if (-not $p.HasExited) {
        [void]$p.CloseMainWindow()
        if (-not $p.WaitForExit(8000)) { $p.Kill() }
    }
    Start-Sleep -Milliseconds 300
    if (Test-Path $log) { return Get-Content $log -Encoding utf8 } else { return @() }
}

'==== 1. Core library on its own ===='
$core = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'discovery-core-test.ps1') 2>&1
$coreLine = ($core | Select-String -Pattern 'CORE TESTS' | Select-Object -Last 1)
"$coreLine"
Report ($LASTEXITCODE -eq 0) 'core tests pass without PPSSPP'

'==== 2. System dialogs ===='
$selftest = Join-Path $repo 'Accessibility\tests\discovery_selftest.lua'
$dialog = Join-Path $repo 'pspautotests\tests\utility\msgdialog\dialog.prx'
$lines = Run-Program $dialog @("--script=`"$selftest`"") 'DISCOVERY SELFTEST RESULT' 45
$lines | Select-String -Pattern 'FAIL|DISCOVERY|SAY' | Select-Object -First 40 | ForEach-Object { "  $($_.Line)" }
$message = '11111111111111111111111111111111111112111111111111111111111'
Report ([bool]($lines | Select-String -SimpleMatch "DISCOVERY TEXT [system_dialog, confidence 1.00] $message")) 'the dialog message was captured as text'
Report ([bool]($lines | Select-String -SimpleMatch 'DISCOVERY LOCATED')) 'the message was found in game memory'
Report ([bool]($lines | Select-String -Pattern "SAY.*$message")) 'the dialog was spoken without being asked'
Report ([bool]($lines | Select-String -SimpleMatch 'DISCOVERY SELFTEST RESULT: all passed')) 'Lua self-test passed'
Report (-not ($lines | Select-String -Pattern '^.*\bFAIL ')) 'no FAIL lines'

'==== 3. System font library ===='
foreach ($name in 'fonttest', 'charglyphimage') {
    $prx = Join-Path $repo "pspautotests\tests\font\$name.prx"
    $lines = Run-Program $prx @('--no-script') 'Discovery survey at game end' 30
    $start = ($lines | Select-String -SimpleMatch 'Discovery survey at game end' | Select-Object -First 1)
    if ($start) {
        $lines[($start.LineNumber - 1)..([Math]::Min($start.LineNumber + 14, $lines.Count - 1))] | ForEach-Object { "  $_" }
    }
    $rendered = ($lines | Select-String -Pattern 'font\.glyphs_rendered = (\d+)' | Select-Object -First 1)
    $count = if ($rendered) { [int]$rendered.Matches[0].Groups[1].Value } else { 0 }
    Report ($count -gt 0) "$name.prx: the tap saw glyphs being drawn ($count)"
}

if ($script:failed -eq 0) { 'DISCOVERY TESTS: all passed' } else { "DISCOVERY TESTS: $($script:failed) FAILED" }
exit $script:failed
