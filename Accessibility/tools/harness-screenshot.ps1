# Harness authoring helper. NOT part of the product.
#
# Loads a save state in the build, waits, and saves a picture of the window,
# so that the person (or assistant) writing a case file can see what the
# screen shows and write down the expected text. The Discovery layer itself
# never looks at pixels; this script exists only to produce the answer key.
#
# Usage:
#   powershell -File Accessibility\tools\harness-screenshot.ps1 -BuildDir ..\builds\access-dev -Game ULES01500 -State shop.ppst
#   optional: -DataDir <folder>  -Seconds 6  -Target <iso or program, instead of games.ini>
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$Game,
    [string]$State,
    [string]$DataDir,
    [string]$Target,
    [int]$Seconds = 6
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class ShotWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][ShotWin]::SetProcessDPIAware()
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not $DataDir) { $DataDir = Join-Path (Split-Path $repo -Parent) 'harness-data' }
New-Item -ItemType Directory -Force (Join-Path $DataDir 'screens') | Out-Null
$DataDir = (Resolve-Path $DataDir).Path

if (-not $Target) {
    $gamesFile = Join-Path $DataDir 'games.ini'
    $line = Get-Content $gamesFile -Encoding utf8 | Where-Object { $_ -match "^\s*$Game\s*=" } | Select-Object -First 1
    if (-not $line) { throw "No ISO listed for $Game in $gamesFile" }
    $Target = ($line -split '=', 2)[1].Trim()
}
$argList = @("`"$Target`"", '--no-script')
$label = if ($Game) { $Game } else { [IO.Path]::GetFileNameWithoutExtension($Target) }
if ($State) {
    $statePath = Join-Path (Join-Path $DataDir $Game) $State
    if (-not (Test-Path $statePath)) { throw "Save state not found: $statePath" }
    $argList += "--state=`"$statePath`""
    $label += '.' + [IO.Path]::GetFileNameWithoutExtension($State)
}
$p = Start-Process (Join-Path $BuildDir 'PPSSPPWindows64.exe') -ArgumentList $argList -WorkingDirectory $BuildDir -PassThru
Start-Sleep -Seconds $Seconds
$p.Refresh()
[void][ShotWin]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 700
$rect = New-Object ShotWin+RECT
[void][ShotWin]::GetClientRect($p.MainWindowHandle, [ref]$rect)
$origin = New-Object ShotWin+POINT
[void][ShotWin]::ClientToScreen($p.MainWindowHandle, [ref]$origin)
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size $w, $h))
$out = Join-Path $DataDir "screens\$label.png"
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
[void]$p.CloseMainWindow()
if (-not $p.WaitForExit(8000)) { $p.Kill() }
"Saved $out"
