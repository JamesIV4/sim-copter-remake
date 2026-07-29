param([string[]]$Commands, [int]$DelayMs = 1200)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W32Con {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
}
"@

$proc = Get-Process UnrealEditor -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
[W32Con]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 700

$KEYUP = 0x0002
$VK_TILDE = 0xC0
$VK_RETURN = 0x0D
$VK_SHIFT = 0x10

function Send-Vk([int]$vk, [bool]$shift = $false) {
    if ($shift) { [W32Con]::keybd_event([byte]$VK_SHIFT, 0, 0, [UIntPtr]::Zero) }
    [W32Con]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 25
    [W32Con]::keybd_event([byte]$vk, 0, $KEYUP, [UIntPtr]::Zero)
    if ($shift) { [W32Con]::keybd_event([byte]$VK_SHIFT, 0, $KEYUP, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 25
}

foreach ($cmd in $Commands) {
    # Open the console, type, submit, and let the console close itself on Enter.
    Send-Vk $VK_TILDE
    Start-Sleep -Milliseconds 400
    foreach ($ch in $cmd.ToCharArray()) {
        $scan = [W32Con]::VkKeyScan($ch)
        $vk = $scan -band 0xFF
        $needShift = (($scan -shr 8) -band 1) -eq 1
        Send-Vk $vk $needShift
    }
    Start-Sleep -Milliseconds 200
    Send-Vk $VK_RETURN
    Write-Host "ran: $cmd"
    Start-Sleep -Milliseconds $DelayMs
}
