# Runs the script sandbox self-test (Accessibility\tests\sandbox_selftest.lua)
# and the Phase 2 API self-test in the packaged build, on the small PSP test
# program displayWait.prx. No game is needed.
#
# Usage: powershell -ExecutionPolicy Bypass -File Accessibility\tools\sandbox-test.ps1 -BuildDir ..\builds\access-dev
#
# For the link check it puts a folder link (junction) named "escape" in the
# self-test's data folder, pointing to a scratch folder in %TEMP%, and removes
# the link afterwards (the link only; never what it points to).
param([Parameter(Mandatory = $true)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path
$log = Join-Path $BuildDir 'accessibility\logs\access.log'
$prx = Join-Path $repo 'pspautotests\demos\displayWait.prx'

$dataDir = Join-Path $BuildDir 'scripts\data\sandbox_selftest'
New-Item -ItemType Directory -Force $dataDir | Out-Null
$target = Join-Path $env:TEMP 'sandbox-escape-target'
New-Item -ItemType Directory -Force $target | Out-Null
Set-Content -Path (Join-Path $target 'target.txt') -Value 'outside' -Encoding ascii
$link = Join-Path $dataDir 'escape'
if (Test-Path $link) { [System.IO.Directory]::Delete($link) }
New-Item -ItemType Junction -Path $link -Target $target | Out-Null
Set-Content -Path (Join-Path $dataDir 'LINK_PRESENT') -Value 'yes' -Encoding ascii

$failed = $false
try {
    foreach ($t in @('sandbox_selftest.lua', 'phase2_selftest.lua')) {
        $test = Join-Path $repo "Accessibility\tests\$t"
        if (Test-Path $log) { Remove-Item $log }
        $p = Start-Process (Join-Path $BuildDir 'PPSSPPWindows64.exe') -ArgumentList "`"$prx`"", "--script=`"$test`"" -WorkingDirectory $BuildDir -PassThru
        $deadline = (Get-Date).AddSeconds(40)
        $done = $false
        while ((Get-Date) -lt $deadline -and -not $done) {
            Start-Sleep -Milliseconds 500
            if (Test-Path $log) { $done = (Select-String -Path $log -Pattern 'SANDBOX RESULT|SELFTEST RESULT' -Quiet) }
        }
        Start-Sleep -Seconds 1
        # Close this emulator by its own process object, never by number.
        $p.CloseMainWindow() | Out-Null
        if (-not $p.WaitForExit(5000)) { $p.Kill() }
        Write-Output "=== $t"
        if (-not (Test-Path $log)) { Write-Output 'no log written'; $failed = $true; continue }
        Select-String -Path $log -Pattern 'FAIL |SKIP |RESULT' | ForEach-Object { $_.Line }
        if (-not (Select-String -Path $log -Pattern 'RESULT: ALL PASSED' -Quiet)) { $failed = $true }
    }
} finally {
    [System.IO.Directory]::Delete($link)
    Remove-Item (Join-Path $dataDir 'LINK_PRESENT') -ErrorAction SilentlyContinue
    if (Test-Path (Join-Path $target 'sandbox_link_test.txt')) {
        Write-Output 'FAIL a file was written through the link'
        $failed = $true
    }
}
if ($failed) { Write-Output 'SANDBOX TEST: FAILED'; exit 1 }
Write-Output 'SANDBOX TEST: ALL PASSED'
