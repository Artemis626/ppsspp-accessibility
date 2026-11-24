# Records every save state in the harness data folder in one pass: for each
# state it starts the emulator, loads the state, records everything the
# Discovery layer sees for a few seconds, and closes the emulator again.
# The recordings can then be studied, and replayed, without the emulator
# (see Accessibility\Discovery\Core\Recording.h and discovery-core-test.ps1 -Replay).
#
# Usage:
#   powershell -File Accessibility\tools\harness-record.ps1 -BuildDir ..\builds\access-dev
#   optional: -Import            first copy the save states made in the build's own
#                                memstick folder into the data folder (copies only;
#                                nothing in the memstick folder is changed)
#             -DataDir <folder>  default: ..\harness-data beside the repository
#             -Game ULES01500    only this game's states
#             -State name        only states whose file name contains this
#             -Frames 300        how long each recording lasts (60 frames = 1 second)
#
# Folders:
#   <DataDir>\games.ini                           GAMEID = path to your ISO
#   <DataDir>\<GAMEID>\<state>.ppst               the save states
#   <DataDir>\recordings\<GAMEID>\<state>\        one recording per state (replaced on each run)
#   <DataDir>\recordings\index.md                 what the last pass produced
# Recordings contain game memory: like save states, they stay outside the repository.
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$DataDir,
    [string]$Game,
    [string]$State,
    [int]$Frames = 300,
    [switch]$Import
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $DataDir) { $DataDir = Join-Path (Split-Path $repo -Parent) 'harness-data' }
New-Item -ItemType Directory -Force $DataDir | Out-Null
$DataDir = (Resolve-Path $DataDir).Path
$exe = Join-Path $BuildDir 'PPSSPPWindows64.exe'

if ($Import) {
    # PPSSPP names its states GAMEID_version_slot.ppst. The copy is named after
    # the slot and the time the state was made, so a slot saved again later
    # becomes a new file here and nothing is ever overwritten.
    $stateDir = Join-Path $BuildDir 'memstick\PSP\PPSSPP_STATE'
    foreach ($f in @(Get-ChildItem $stateDir -Filter '*.ppst' -ErrorAction SilentlyContinue)) {
        if ($f.Name -notmatch '^([A-Z]{4}\d{5})_[^_]+_(\d+)\.ppst$') { continue }
        $gameDir = Join-Path $DataDir $Matches[1]
        New-Item -ItemType Directory -Force $gameDir | Out-Null
        $dest = Join-Path $gameDir ("slot$($Matches[2])-$($f.LastWriteTime.ToString('yyyyMMdd-HHmm')).ppst")
        if (-not (Test-Path $dest)) {
            Copy-Item $f.FullName $dest
            "Imported $($f.Name) as $($Matches[1])\$(Split-Path $dest -Leaf)"
        }
    }
}

$games = @{}
$gamesFile = Join-Path $DataDir 'games.ini'
if (Test-Path $gamesFile) {
    foreach ($raw in Get-Content $gamesFile -Encoding utf8) {
        $line = ($raw -replace '(^|\s)[;#].*$', '').Trim()
        $eq = $line.IndexOf('=')
        if ($eq -gt 0 -and -not $line.StartsWith('[')) { $games[$line.Substring(0, $eq).Trim().ToUpper()] = $line.Substring($eq + 1).Trim() }
    }
}

$ours = @('recording.txt', 'sprites.txt', 'timeline.txt', 'summary.txt')
$rows = @()
foreach ($gameDir in @(Get-ChildItem $DataDir -Directory | Where-Object { $_.Name -match '^[A-Z]{4}\d{5}$' } | Sort-Object Name)) {
    $gameId = $gameDir.Name
    if ($Game -and $gameId -ne $Game) { continue }
    foreach ($stateFile in @(Get-ChildItem $gameDir.FullName -Filter '*.ppst' | Sort-Object Name)) {
        $name = [IO.Path]::GetFileNameWithoutExtension($stateFile.Name)
        if ($State -and $name -notlike "*$State*") { continue }
        if (-not $games.ContainsKey($gameId) -or -not (Test-Path $games[$gameId])) {
            $rows += [pscustomobject]@{ game = $gameId; state = $name; result = "SKIPPED: no ISO listed for $gameId in $gamesFile (or the file is missing)" }
            continue
        }
        $out = Join-Path $DataDir "recordings\$gameId\$name"
        New-Item -ItemType Directory -Force $out | Out-Null
        # The recorder adds to files, so the last pass's files must go first.
        foreach ($old in @(Get-ChildItem $out -File)) {
            if ($ours -contains $old.Name -or $old.Name -match '^memory-\d+\.bin$') { Remove-Item $old.FullName -Force -Confirm:$false }
        }
        "Recording $gameId / $name..."
        $argList = @("`"$($games[$gameId])`"", "--state=`"$($stateFile.FullName)`"", "--access-record=`"$out`"", "--access-record-frames=$Frames", '--no-script')
        $p = Start-Process $exe -ArgumentList $argList -WorkingDirectory $BuildDir -PassThru
        # The emulator is closed through its own process object, never by number.
        if (-not $p.WaitForExit(120000)) {
            $p.Kill()
            $p.WaitForExit(10000) | Out-Null
            $rows += [pscustomobject]@{ game = $gameId; state = $name; result = 'ERROR: the emulator did not finish within two minutes' }
            continue
        }
        if (-not (Test-Path (Join-Path $out 'recording.txt'))) {
            $rows += [pscustomobject]@{ game = $gameId; state = $name; result = 'ERROR: no recording was written (see accessibility\logs\access.log in the build folder)' }
            continue
        }
        $summary = Get-Content (Join-Path $out 'summary.txt') -Encoding utf8
        $section = ''
        $texts = 0; $unread = 0
        foreach ($l in $summary) {
            if ($l.StartsWith('==== ')) { $section = $l; continue }
            if ($l -match '^  frames \d+ to \d+: ') {
                if ($section -like '*EVERY TEXT SEEN*') { $texts++ }
                elseif ($section -like '*EVERY UNREAD ROW*') { $unread++ }
            }
        }
        $rows += [pscustomobject]@{ game = $gameId; state = $name; result = "$texts different texts read, $unread different rows not read" }
    }
}

$index = @('# Discovery recordings', '', "Pass of $(Get-Date -Format 'yyyy-MM-dd HH:mm'). Build: $BuildDir. $Frames frames per state.", '')
if (-not $rows) { $index += 'No save states were found. Put them in <DataDir>\<GAMEID>\, or run with -Import.' }
foreach ($r in $rows) { $index += "- $($r.game), $($r.state): $($r.result)" }
$indexFile = Join-Path $DataDir 'recordings\index.md'
New-Item -ItemType Directory -Force (Split-Path $indexFile -Parent) | Out-Null
$index | Set-Content $indexFile -Encoding utf8
$index
