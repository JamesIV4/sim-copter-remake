param([string]$OutFile, [string[]]$Addrs)
$root = "s:\Repos\sim-copter-remake"
$exe = "$root\Tools\re-agent\.venv\Scripts\ghidra-bridge.exe"
Push-Location $root
$lines = New-Object System.Collections.Generic.List[string]
foreach ($a in $Addrs) {
    $t = & $exe decompile $a 2>&1
    foreach ($l in $t) {
        $s = [string]$l
        if ($s.Trim().Length -eq 0) { continue }
        $lines.Add($s)
    }
}
Pop-Location
$lines | Out-File -Encoding utf8 $OutFile
Write-Output "$($lines.Count) lines -> $OutFile"
