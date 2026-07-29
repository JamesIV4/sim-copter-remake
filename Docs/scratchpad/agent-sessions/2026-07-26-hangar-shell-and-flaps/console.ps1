param([string]$Command)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32C {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
"@ -ErrorAction SilentlyContinue

$proc = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "no game process"; exit 1 }
$script:found = [IntPtr]::Zero
$cb = [W32C+EnumProc]{
  param($h, $p)
  $pid2 = 0
  [void][W32C]::GetWindowThreadProcessId($h, [ref]$pid2)
  if ($pid2 -eq $proc.Id) {
    $sb = New-Object System.Text.StringBuilder 512
    [void][W32C]::GetWindowText($h, $sb, 512)
    if ($sb.ToString() -like "*SimCopterRemake*") { $script:found = $h; return $false }
  }
  return $true
}
[void][W32C]::EnumWindows($cb, [IntPtr]::Zero)
if ($script:found -eq [IntPtr]::Zero) { Write-Error "no game window"; exit 1 }

[void][W32C]::SetForegroundWindow($script:found)
Start-Sleep -Milliseconds 500

# VK_OEM_3 is the ` / ~ key that opens the UE console.
[W32C]::keybd_event(0xC0, 0, 0, [IntPtr]::Zero)
[W32C]::keybd_event(0xC0, 0, 2, [IntPtr]::Zero)
Start-Sleep -Milliseconds 600

Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.SendKeys]::SendWait($Command)
Start-Sleep -Milliseconds 300
[System.Windows.Forms.SendKeys]::SendWait("{ENTER}")
Start-Sleep -Milliseconds 700
Write-Output "sent: $Command"
