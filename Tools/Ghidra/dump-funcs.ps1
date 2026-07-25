# Compact batch decompiler over the ghidra-bridge export.
#
#   Tools/Ghidra/dump-funcs.ps1 -Out Docs/scratchpad/ghidra/out_foo.txt -Addrs 0x1234,0x5678
#
# Writes one decompile per address with blank lines collapsed, so a long cluster
# stays readable in a single file.
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string[]]$Addrs,
    [string]$Header = ""
)

$bridge = "Tools/re-agent/.venv/Scripts/ghidra-bridge.exe"
$lines = New-Object System.Collections.Generic.List[string]
if ($Header -ne "") { $lines.Add($Header) }

foreach ($a in $Addrs) {
    $lines.Add("")
    $lines.Add("################################ $a ################################")
    $text = (& $bridge decompile $a 2>&1) | Out-String
    # The bridge emits blank-line-separated output; drop empty lines entirely.
    foreach ($l in ($text -split "`r?`n")) {
        if ($l.Trim() -eq "") { continue }
        $lines.Add($l.TrimEnd())
    }
}

$dir = Split-Path $Out
if ($dir -ne "" -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
Set-Content -Path $Out -Value $lines -Encoding utf8
Write-Host ("Wrote {0} ({1} lines, {2} functions)" -f $Out, $lines.Count, $Addrs.Count)
