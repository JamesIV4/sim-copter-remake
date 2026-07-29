param([string[]]$Vas, [int]$Count = 12)
$path = "S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\SimCopter.exe"
$b = [System.IO.File]::ReadAllBytes($path)
$peOff = [BitConverter]::ToInt32($b, 0x3c)
$numSec = [BitConverter]::ToUInt16($b, $peOff + 6)
$optSize = [BitConverter]::ToUInt16($b, $peOff + 20)
$secTab = $peOff + 24 + $optSize
$imageBase = [BitConverter]::ToUInt32($b, $peOff + 24 + 28)
$secs = @()
for ($i = 0; $i -lt $numSec; $i++) {
    $o = $secTab + $i * 40
    $name = [System.Text.Encoding]::ASCII.GetString($b, $o, 8).Trim([char]0)
    $vsize = [BitConverter]::ToUInt32($b, $o + 8)
    $vaddr = [BitConverter]::ToUInt32($b, $o + 12)
    $rsize = [BitConverter]::ToUInt32($b, $o + 16)
    $raddr = [BitConverter]::ToUInt32($b, $o + 20)
    $secs += [pscustomobject]@{ Name = $name; VA = $vaddr; VSize = $vsize; RAW = $raddr; RSize = $rsize }
}
Write-Output ("ImageBase=0x{0:x}" -f $imageBase)
foreach ($s in $secs) { Write-Output ("  {0,-8} VA=0x{1:x8} VSz=0x{2:x} RAW=0x{3:x8}" -f $s.Name, ($imageBase + $s.VA), $s.VSize, $s.RAW) }
foreach ($vaStr in $Vas) {
    $va = [Convert]::ToUInt32($vaStr, 16)
    $rva = $va - $imageBase
    $sec = $secs | Where-Object { $rva -ge $_.VA -and $rva -lt ($_.VA + $_.VSize) } | Select-Object -First 1
    if (-not $sec) { Write-Output ("0x{0:x}: not mapped" -f $va); continue }
    $off = $sec.RAW + ($rva - $sec.VA)
    Write-Output ("`n0x{0:x} ({1}) file=0x{2:x}" -f $va, $sec.Name, $off)
    for ($i = 0; $i -lt $Count; $i++) {
        $v = [BitConverter]::ToUInt32($b, $off + $i * 4)
        Write-Output ("  [{0,2}] +0x{1:x2} = 0x{2:x8}" -f $i, ($i * 4), $v)
    }
}
