Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
}
"@ -ErrorAction SilentlyContinue

function Get-GameWindow {
  $p = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($null -eq $p) { throw "no game window" }
  return $p.MainWindowHandle
}

function Send-Key([byte]$vk, [bool]$shift = $false) {
  if ($shift) { [Win32]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
  [Win32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 20
  [Win32]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
  if ($shift) { [Win32]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
  Start-Sleep -Milliseconds 20
}

function Send-Text([string]$text) {
  foreach ($ch in $text.ToCharArray()) {
    $vks = [Win32]::VkKeyScan($ch)
    $vk = [byte]($vks -band 0xFF)
    $shift = (($vks -shr 8) -band 1) -eq 1
    Send-Key $vk $shift
  }
}

function Invoke-Console([string]$cmd) {
  $h = Get-GameWindow
  [Win32]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 400
  Send-Key 0xC0   # ~ opens console
  Start-Sleep -Milliseconds 600
  Send-Text $cmd
  Start-Sleep -Milliseconds 300
  Send-Key 0x0D   # Enter
  Start-Sleep -Milliseconds 800
}

function Save-Shot([string]$path) {
  Add-Type -AssemblyName System.Drawing
  $h = Get-GameWindow
  [Win32]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 400
  $r = New-Object Win32+RECT
  [Win32]::GetClientRect($h, [ref]$r) | Out-Null
  $tl = New-Object Win32+POINT
  $tl.X = 0; $tl.Y = 0
  [Win32]::ClientToScreen($h, [ref]$tl) | Out-Null
  $w = $r.Right - $r.Left
  $ht = $r.Bottom - $r.Top
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($tl.X, $tl.Y, 0, 0, $bmp.Size)
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
