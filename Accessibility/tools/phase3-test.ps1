# Runs tests/phase3_selftest.lua on a PSP test program, then presses hotkeys
# as real key events and prints the relevant log lines.
#
# Usage: powershell -File phase3-test.ps1 -BuildDir <packaged build folder>
param([Parameter(Mandatory = $true)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public static class AccessKeys {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
  static void Down(byte vk, bool ext) { keybd_event(vk, (byte)MapVirtualKey(vk, 0), ext ? 1u : 0u, UIntPtr.Zero); }
  static void Up(byte vk, bool ext) { keybd_event(vk, (byte)MapVirtualKey(vk, 0), (ext ? 1u : 0u) | 2u, UIntPtr.Zero); }
  public static void Combo(bool ctrl, bool shift, byte vk, bool ext) {
    if (ctrl) Down(0x11, false); if (shift) Down(0x10, false);
    System.Threading.Thread.Sleep(40); Down(vk, ext); System.Threading.Thread.Sleep(60); Up(vk, ext);
    System.Threading.Thread.Sleep(40);
    if (shift) Up(0x10, false); if (ctrl) Up(0x11, false);
  }
}
'@
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$BuildDir = (Resolve-Path $BuildDir).Path
$log = Join-Path $BuildDir 'accessibility\logs\access.log'
$prx = Join-Path $repo 'pspautotests\demos\displayWait.prx'
$test = Join-Path $repo 'Accessibility\tests\phase3_selftest.lua'
$p = Start-Process (Join-Path $BuildDir 'PPSSPPWindows64.exe') -ArgumentList "`"$prx`"", "--script=`"$test`"" -WorkingDirectory $BuildDir -PassThru
# Wait for the self-test to finish its automatic part.
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'SELFTEST RESULT' -Quiet)) { break }
}
$p.Refresh()
[void][AccessKeys]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 500
Set-Clipboard -Value 'clipboard before test'
function Press($label, $ctrl, $shift, $vk, $ext) {
    Add-Content -Path $log -Value "---- KEY $label" -Encoding utf8
    [AccessKeys]::Combo($ctrl, $shift, [byte]$vk, $ext)
    Start-Sleep -Milliseconds 700
}
Press 'Ctrl+Shift+F9 (script hotkey)' $true $true 0x78 $false
Press 'Ctrl+Shift+V by scancode 0x2F' $true $true 0x56 $false
Press 'Numpad5 in game (plain key)' $false $false 0x65 $false
Press 'Ctrl+Shift+R (repeat last)' $true $true 0x52 $false
Press 'Ctrl+Shift+PageUp (older)' $true $true 0x21 $true
Press 'Ctrl+Shift+PageUp (older)' $true $true 0x21 $true
Press 'Ctrl+Shift+C (copy)' $true $true 0x43 $false
$clip = Get-Clipboard
Press 'Ctrl+Shift+PageDown (newer)' $true $true 0x22 $true
Press 'Ctrl+Shift+H (list hotkeys)' $true $true 0x48 $false
Press 'Escape (open pause menu)' $false $false 0x1B $false
Press 'Numpad5 in pause menu (must NOT run)' $false $false 0x65 $false
Press 'Escape (close pause menu)' $false $false 0x1B $false
Press 'Ctrl+Shift+F12 (reload script)' $true $true 0x7B $false
Start-Sleep -Seconds 4
[void]$p.CloseMainWindow()
if (-not $p.WaitForExit(5000)) { $p.Kill() }
Get-Content $log -Encoding utf8 | Select-String -Pattern 'FAIL|RESULT|KEYTEST|---- KEY|SAY|ERROR|reload|unloaded|loaded'
"Clipboard after copy: $clip"
