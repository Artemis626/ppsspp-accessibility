# Automated check of spoken menus: starts a packaged build, presses keys the
# way a user would, then prints every line the accessibility layer sent to the
# screen reader (taken from the PPSSPP log, so it works without listening).
#
# Keys are sent as real keyboard events (keybd_event), because PPSSPP reads
# the keyboard through Raw Input, which ignores some synthetic methods
# such as WScript SendKeys.
#
# Usage: powershell -File menu-speech-test.ps1 -BuildDir <folder> -Keys "Down,Down,Right,Enter"
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$Keys = "Down,Down,Down,Right,Right,Right,Up,Up,Left,Left",
    [int]$StartupSeconds = 6,
    [int]$DelayMs = 600
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class AccessKb {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
  public static void Tap(byte vk, bool extended) {
    byte sc = (byte)MapVirtualKey(vk, 0); uint e = extended ? 1u : 0u;
    keybd_event(vk, sc, e, UIntPtr.Zero); System.Threading.Thread.Sleep(60);
    keybd_event(vk, sc, e | 2u, UIntPtr.Zero);
  }
}
'@
# Virtual-key codes, and whether the key is an "extended" key.
$map = @{
    Up = @(0x26, $true); Down = @(0x28, $true); Left = @(0x25, $true); Right = @(0x27, $true)
    Enter = @(0x0D, $false); Space = @(0x20, $false); Escape = @(0x1B, $false); Tab = @(0x09, $false)
    Back = @(0x08, $false); PageUp = @(0x21, $true); PageDown = @(0x22, $true)
}
$BuildDir = (Resolve-Path $BuildDir).Path
$log = Join-Path $BuildDir 'menu-speech-test.log'
Remove-Item $log -ErrorAction SilentlyContinue
$p = Start-Process (Join-Path $BuildDir 'PPSSPPWindows64.exe') -ArgumentList "--log=`"$log`"", "--loglevel=4" -PassThru
Start-Sleep -Seconds $StartupSeconds
$p.Refresh()
[void][AccessKb]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 300
foreach ($k in $Keys.Split(',')) {
    $k = $k.Trim()
    if (-not $map.ContainsKey($k)) { throw "Unknown key '$k'" }
    [AccessKb]::Tap([byte]$map[$k][0], [bool]$map[$k][1])
    Start-Sleep -Milliseconds $DelayMs
}
Start-Sleep -Seconds 1
[void]$p.CloseMainWindow()
if (-not $p.WaitForExit(5000)) { $p.Kill() }
Select-String -Path $log -Pattern '\[Access\]' | ForEach-Object { $_.Line -replace '^.*?\[Access\]', '[Access]' }
