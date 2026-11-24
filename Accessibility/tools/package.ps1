# Copies a built PPSSPPWindows64.exe plus everything it needs into a
# self-contained, portable folder (no installed.txt, so PPSSPP keeps its
# settings and memstick inside that folder and never touches Documents\PPSSPP).
#
# Usage: powershell -File Accessibility\tools\package.ps1 -Dest "..\builds\name"
param(
    [Parameter(Mandatory = $true)][string]$Dest,
    [switch]$NoNvda
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
New-Item -ItemType Directory -Force $Dest | Out-Null
$Dest = Resolve-Path $Dest

Copy-Item (Join-Path $repo 'PPSSPPWindows64.exe') $Dest -Force
# Pick up the debug symbols too, so crash reports have readable stack traces.
$pdb = Join-Path $repo 'PPSSPPWindows64.pdb'
if (Test-Path $pdb) { Copy-Item $pdb $Dest -Force }
# The assets folder (fonts, UI atlas, language files, shaders) must sit beside the exe.
robocopy (Join-Path $repo 'assets') (Join-Path $Dest 'assets') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if (-not $NoNvda) {
    Copy-Item (Join-Path $repo 'Accessibility\ext\nvda\x64\nvdaControllerClient.dll') $Dest -Force
}
Write-Output "Packaged to $Dest"
