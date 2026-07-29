param([string[]]$Keys, [int]$DelayMs = 900)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Keys {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@

$proc = Get-Process UnrealEditor -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
[Win32Keys]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 600

$vk = @{ 'F2' = 0x71; 'F3' = 0x72; 'F4' = 0x73; 'F5' = 0x74; 'SHIFT' = 0x10 }
$KEYUP = 0x0002

foreach ($k in $Keys) {
    if ($k -like 'SHIFT+*') {
        $inner = $k.Substring(6)
        [Win32Keys]::keybd_event([byte]$vk['SHIFT'], 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [Win32Keys]::keybd_event([byte]$vk[$inner], 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [Win32Keys]::keybd_event([byte]$vk[$inner], 0, $KEYUP, [UIntPtr]::Zero)
        [Win32Keys]::keybd_event([byte]$vk['SHIFT'], 0, $KEYUP, [UIntPtr]::Zero)
    }
    else {
        [Win32Keys]::keybd_event([byte]$vk[$k], 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [Win32Keys]::keybd_event([byte]$vk[$k], 0, $KEYUP, [UIntPtr]::Zero)
    }
    Write-Host "sent $k"
    Start-Sleep -Milliseconds $DelayMs
}
