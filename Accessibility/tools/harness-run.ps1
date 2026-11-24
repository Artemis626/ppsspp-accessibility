# Runs the Discovery harness: loads each case's save state, lets the layer
# look at the screen, and reports what it found against what was expected.
#
# Usage:
#   powershell -File Accessibility\tools\harness-run.ps1 -BuildDir ..\builds\access-dev
#   optional: -DataDir <folder>   where save states and results live (default: ..\harness-data beside the repository)
#             -Game ULES01500     only this game's cases
#             -Case name          only cases whose file name contains this
#
# Folders (see Accessibility\harness\README.md):
#   In the repository:   Accessibility\harness\cases\<GAMEID>\<case>.ini   cases that need no game
#   Outside it:          <DataDir>\cases\<GAMEID>\<case>.ini               cases for real games (they quote the game's text)
#                        <DataDir>\games.ini                               GAMEID = path to your ISO
#                        <DataDir>\<GAMEID>\<state>.ppst                   the save states
#                        <DataDir>\results\                                reports
# Save states and ISOs are game data and are never put in the repository.
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$DataDir,
    [string]$Game,
    [string]$Case
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $DataDir) { $DataDir = Join-Path (Split-Path $repo -Parent) 'harness-data' }
New-Item -ItemType Directory -Force $DataDir | Out-Null
$DataDir = (Resolve-Path $DataDir).Path
$exe = Join-Path $BuildDir 'PPSSPPWindows64.exe'
$casesRoot = Join-Path $repo 'Accessibility\harness\cases'
$stamp = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$runDir = Join-Path $DataDir "results\$stamp"
New-Item -ItemType Directory -Force $runDir | Out-Null

# A very small INI reader: section -> key -> value. Enough for case files and games.ini.
function Read-Ini($path) {
    $ini = @{}
    $section = ''
    foreach ($raw in Get-Content $path -Encoding utf8) {
        $line = ($raw -replace '(^|\s)[;#].*$', '').Trim()
        if (-not $line) { continue }
        if ($line.StartsWith('[')) { $section = $line.Trim('[', ']').Trim(); $ini[$section] = @{}; continue }
        $eq = $line.IndexOf('=')
        if ($eq -lt 0) { continue }
        if (-not $ini.ContainsKey($section)) { $ini[$section] = @{} }
        $ini[$section][$line.Substring(0, $eq).Trim().ToLower()] = $line.Substring($eq + 1).Trim()
    }
    return $ini
}

$games = @{}
$gamesFile = Join-Path $DataDir 'games.ini'
if (Test-Path $gamesFile) {
    $g = Read-Ini $gamesFile
    foreach ($s in $g.Keys) { foreach ($k in $g[$s].Keys) { $games[$k.ToUpper()] = $g[$s][$k] } }
}

# Runs one case once. Returns the parsed result, or an object with .error.
function Invoke-Case($caseFile, $target, $state, $cold) {
    $out = Join-Path $runDir ((Split-Path (Split-Path $caseFile -Parent) -Leaf) + '.' + [IO.Path]::GetFileNameWithoutExtension($caseFile) + $(if ($cold) { '.cold' } else { '.warm' }) + '.json')
    $argList = @("`"$target`"", "--access-harness=`"$caseFile`"", "--access-harness-out=`"$out`"", '--no-script')
    if ($state) { $argList += "--state=`"$state`"" }
    if ($cold) { $argList += '--access-no-sidecar' }
    $p = Start-Process $exe -ArgumentList $argList -WorkingDirectory $BuildDir -PassThru
    if (-not $p.WaitForExit(120000)) {
        $p.Kill()
        return [pscustomobject]@{ error = 'the emulator did not finish within two minutes' }
    }
    if (-not (Test-Path $out)) { return [pscustomobject]@{ error = 'no result file was written' } }
    return (Get-Content $out -Raw -Encoding utf8 | ConvertFrom-Json)
}

$rows = @()
$details = @()
# Cases that quote a game's own text live in the data folder, not in the repository.
$caseFiles = @(@($casesRoot, (Join-Path $DataDir 'cases')) | ForEach-Object { Get-ChildItem $_ -Recurse -Filter '*.ini' -ErrorAction SilentlyContinue } | Sort-Object FullName)
foreach ($file in $caseFiles) {
    $ini = Read-Ini $file.FullName
    $c = $ini['case']
    if (-not $c) { continue }
    $gameId = "$($c['game'])"
    $name = [IO.Path]::GetFileNameWithoutExtension($file.Name)
    if ($Game -and $gameId -ne $Game) { continue }
    if ($Case -and $name -notlike "*$Case*") { continue }

    # What to boot: a program inside the repository (test programs), or the game's ISO.
    $target = $null
    $problem = $null
    if ($c['program']) {
        $target = Join-Path $repo $c['program']
        if (-not (Test-Path $target)) { $problem = "program not found: $($c['program'])" }
    } elseif ($games.ContainsKey($gameId.ToUpper())) {
        $target = $games[$gameId.ToUpper()]
        if (-not (Test-Path $target)) { $problem = "the ISO listed for $gameId in games.ini was not found" }
    } else {
        $problem = "no ISO listed for $gameId in $gamesFile"
    }
    $state = $null
    if (-not $problem -and $c['state']) {
        $state = Join-Path (Join-Path $DataDir $gameId) $c['state']
        if (-not (Test-Path $state)) { $problem = "save state not found: $state" }
    }
    if ($problem) {
        $rows += [pscustomobject]@{ game = $gameId; case = $name; mode = '-'; expected = '-'; exact = '-'; contained = '-'; near = '-'; missing = '-'; recall = '-'; precision = '-'; found = '-'; note = "SKIPPED: $problem" }
        continue
    }

    $modes = @($true)                                              # cold: the state alone
    if ($state -and (Test-Path "$state.access")) { $modes += $false }  # warm: with the layer's own file
    foreach ($cold in $modes) {
        "Running $gameId / $name ($(if ($cold) { 'cold' } else { 'warm' }))..."
        $r = Invoke-Case $file.FullName $target $state $cold
        if ($r.error) {
            $rows += [pscustomobject]@{ game = $gameId; case = $name; mode = $(if ($cold) { 'cold' } else { 'warm' }); expected = '-'; exact = '-'; contained = '-'; near = '-'; missing = '-'; recall = '-'; precision = '-'; found = '-'; note = "ERROR: $($r.error)" }
            continue
        }
        $rows += [pscustomobject]@{
            game = $gameId; case = $name; mode = $r.mode; expected = $r.expected_texts; exact = $r.exact; contained = $r.contained
            near = $r.near; missing = $r.missing; recall = ('{0:P0}' -f $r.recall)
            precision = $(if ($r.complete) { '{0:P0}' -f $r.precision } else { 'n/a' }); found = $r.found_runs; note = ''
        }
        $d = @("### $gameId, $name, $($r.mode)", '', "$($r.description)", '')
        $d += "- Survey: $($r.survey_verdict)"
        $d += "- Expected texts: $($r.expected_texts). Exact: $($r.exact). Contained: $($r.contained). Near: $($r.near). Missing: $($r.missing)."
        if ($r.cursors_declared -or $r.values_declared) { $d += "- Declared but not scored until Phase D3: $($r.cursors_declared) cursors, $($r.values_declared) values." }
        $d += ''
        $d += '#### Expected'
        $d += ''
        foreach ($t in $r.texts) {
            $line = "- **$($t.verdict)**: `"$($t.expected)`""
            if ($t.verdict -ne 'exact' -and $t.best) { $line += ", closest found: `"$($t.best)`"" }
            if ($t.source) { $line += " ($($t.source))" }
            $d += $line
        }
        $d += ''
        $d += '#### Found'
        $d += ''
        if (-not $r.found) { $d += '- Nothing.' }
        foreach ($f in $r.found) {
            $where = if ($f.code -ne '0x00000000') { ", code $($f.code), glyph buffer $($f.buffer)" } else { '' }
            $d += "- $(if ($f.explained) { 'expected' } else { 'EXTRA' }): `"$($f.text)`" ($($f.source), confidence $($f.confidence)$where)"
        }
        if ($r.locations) {
            $d += ''
            $d += '#### Where the found text is stored in game memory'
            $d += ''
            foreach ($l in $r.locations) { $d += "- `"$($l.text)`": $($l.at -join ', ')" }
        }
        $d += ''
        $details += $d
    }
}

# ---- report ----
$report = @('# Discovery harness report', '', "Run: $stamp. Build: $BuildDir.", '')
$report += '## Summary'
$report += ''
if (-not $rows) {
    $report += 'No cases were run. Put case files in Accessibility\harness\cases\<GAMEID>\ and states in the data folder.'
} else {
    $report += '| Game | Case | Mode | Expected | Exact | Contained | Near | Missing | Recall | Precision | Runs found | Note |'
    $report += '|---|---|---|---|---|---|---|---|---|---|---|---|'
    foreach ($r in $rows) {
        $report += "| $($r.game) | $($r.case) | $($r.mode) | $($r.expected) | $($r.exact) | $($r.contained) | $($r.near) | $($r.missing) | $($r.recall) | $($r.precision) | $($r.found) | $($r.note) |"
    }
    $report += ''
    $report += '- **Exact:** a found line says exactly the expected text.'
    $report += '- **Contained:** the words were found, but split or joined differently.'
    $report += '- **Near:** close, with a few wrong characters. Counts as not found.'
    $report += '- **Recall:** share of expected texts found (exact or contained).'
    $report += '- **Precision:** share of found lines that were expected. Only given when the case lists everything on screen.'
    $report += '- **Cold / warm:** cold uses the save state alone; warm also uses the layer''s own file saved beside it.'
}
$report += ''
$report += '## Cases'
$report += ''
$report += $details
$reportFile = Join-Path $runDir 'report.md'
$report | Set-Content $reportFile -Encoding utf8
Copy-Item $reportFile (Join-Path $DataDir 'results\latest-report.md') -Force

# One line per case per run, so progress between runs can be compared.
$history = Join-Path $DataDir 'results\history.csv'
if (-not (Test-Path $history)) { 'run,game,case,mode,expected,exact,contained,near,missing,recall,precision,found,note' | Set-Content $history -Encoding utf8 }
foreach ($r in $rows) {
    "$stamp,$($r.game),$($r.case),$($r.mode),$($r.expected),$($r.exact),$($r.contained),$($r.near),$($r.missing),$($r.recall),$($r.precision),$($r.found),`"$($r.note)`"" | Add-Content $history -Encoding utf8
}
$report | Select-Object -First ($rows.Count + 8)
"Report: $reportFile"
