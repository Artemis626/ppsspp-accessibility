# Builds and runs the Discovery core's own tests, WITHOUT PPSSPP.
#
# The compiler is given only the core's folder. If any core file included an
# emulator header, this build would fail: that is how the core is kept
# platform-independent.
#
# Usage: powershell -File Accessibility\tools\discovery-core-test.ps1
#
# The same program replays a recording (see Discovery\Core\Recording.h), again
# without the emulator:
#   powershell -File Accessibility\tools\discovery-core-test.ps1 -Replay <recording folder> [-Extra 1200] [-Find "text","text"]
# -Extra repeats the last frame that many more times, to give the memory
# searches time. -Find looks for strings in the recorded memory.
# -NoBuild uses the program built last time.
param(
    [string]$Replay,
    [int]$Extra = 1200,
    [string[]]$Find,
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$core = Join-Path $repo 'Accessibility\Discovery\Core'
$out = Join-Path $env:TEMP 'discovery-core-test'
New-Item -ItemType Directory -Force $out | Out-Null

$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "Visual Studio 2022 C++ tools not found at $vcvars" }

$sources = @(Get-ChildItem (Join-Path $core '*.cpp') | ForEach-Object { '"' + $_.FullName + '"' })
$sources += '"' + (Join-Path $core 'tests\CoreTests.cpp') + '"'
$exe = Join-Path $out 'CoreTests.exe'
# /I is deliberately absent: the core may only include its own files (by relative path) and the standard library.
$cl = "cl /nologo /std:c++17 /EHsc /W3 /O1 /utf-8 /Fe`"$exe`" /Fo`"$out\\`" $($sources -join ' ')"
if (-not ($NoBuild -and (Test-Path $exe))) {
    cmd /c "`"$vcvars`" >nul 2>&1 && $cl"
    if ($LASTEXITCODE -ne 0) { throw 'The core did not compile on its own.' }
}
if ($Replay) {
    $replayArgs = @('--replay', (Resolve-Path $Replay).Path, '--extra', "$Extra")
    foreach ($f in $Find) { $replayArgs += @('--find', $f) }
    & $exe @replayArgs
} else {
    & $exe
}
exit $LASTEXITCODE
