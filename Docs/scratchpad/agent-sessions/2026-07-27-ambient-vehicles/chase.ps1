param([string]$Tag = "chase", [int]$Shots = 4, [string]$Pattern = "TRAIN tile", [int]$OffsetX = 1100, [int]$OffsetY = -1100, [int]$Height = 800)
$sp = "C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\1e7b7a5f-851b-45a0-b6e6-fec686c5c5aa\scratchpad"
. "$sp\drive.ps1"
$log = "S:\Repos\sim-copter-remake\SimCopterRemake\Saved\Logs\SimCopterRemake.log"

for ($i = 1; $i -le $Shots; $i++) {
    Send-Console "SimDumpAmbientVehicles"
    Start-Sleep -Milliseconds 900
    $line = (Select-String -Path $log -Pattern $Pattern | Select-Object -Last 1).Line
    if (-not $line) { Write-Output "no $Pattern yet"; Start-Sleep -Seconds 3; continue }
    if ($line -notmatch "$Pattern[^\]]*world (-?\d+) (-?\d+) (-?\d+)") { Write-Output "unparsed: $line"; continue }
    $x = [int]$Matches[1]; $y = [int]$Matches[2]; $z = [int]$Matches[3]
    $camX = $x + $OffsetX; $camY = $y + $OffsetY
    # Face the reported point from the camera offset.
    $yaw = [math]::Round([math]::Atan2($y - $camY, $x - $camX) * 180.0 / [math]::PI)
    Send-Console "BugItGo $camX $camY $($z + $Height) -20 $yaw 0"
    Start-Sleep -Milliseconds 1200
    Save-Shot "$sp\$Tag$i.png"
    Write-Output "shot $i at target $x $y $z (cam $camX $camY yaw $yaw)"
    Start-Sleep -Seconds 2
}
