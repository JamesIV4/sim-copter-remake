# One-off migration (2026-07-29): pull durable agent scratch work out of Claude Code's
# machine-local temp scratchpads into Docs/scratchpad/agent-sessions/.
#
# Keeps the work product: analysis scripts (.py/.ps1) and the small text artifacts they produced
# (decompile dumps, disassembly, string/sound tables).
#
# Deliberately DROPS, as regenerable byproducts that would bloat git:
#   .png     165 files / 139 MB  - in-game verification screenshots
#   .log      29 files / 8.5 MB  - build, test and game logs
#   .output   44 files / 1.2 MB  - background-task stdout captures (harness artifacts)
#   .pyc                          - bytecode
#   privanim.json      11.5 MB    - regenerable via Tools/privanim_extract.py
#
# Session ids are opaque, so each one is renamed to <date>-<topic>, where the topic was read off
# the scripts and dumps inside it and cross-checked against the matching note in Docs/memory/.

param(
    [string]$Source = "C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake",
    [string]$Dest   = "S:\Repos\sim-copter-remake\Docs\scratchpad\agent-sessions",
    [switch]$WhatIfOnly
)

$KeepExtensions = @('.py', '.ps1', '.txt', '.asm')

# session-id prefix -> folder name
$SessionMap = [ordered]@{
    '0a968cfd' = '2026-07-24-people-behaviors'
    '67e07e37' = '2026-07-24-heli-tools'
    '86d83fe5' = '2026-07-24-cockpit-and-menu'
    'b37d4572' = '2026-07-25-airport-and-dispatch'
    '3a85a4ba' = '2026-07-26-hangar-shell-and-flaps'
    '1e7b7a5f' = '2026-07-27-ambient-vehicles'
    'f4c07a37' = '2026-07-27-privanim-figures'
    'f4e50bd8' = '2026-07-27-population-and-cards'
    '3cd85273' = '2026-07-28-ingame-driving'
}

$total = 0
foreach ($prefix in $SessionMap.Keys) {
    $src = Get-ChildItem $Source -Directory | Where-Object { $_.Name.StartsWith($prefix) }
    if (-not $src) { "MISSING session $prefix"; continue }

    $files = Get-ChildItem $src.FullName -Recurse -File |
             Where-Object { $KeepExtensions -contains $_.Extension }
    if (-not $files) { "  (nothing durable in $prefix)"; continue }

    $targetDir = Join-Path $Dest $SessionMap[$prefix]
    if (-not $WhatIfOnly) { New-Item -ItemType Directory -Force -Path $targetDir | Out-Null }

    foreach ($f in $files) {
        # Flatten, but keep the tasks/ subfolder distinction out of the way by prefixing.
        $leaf = $f.Name
        if ($f.Directory.Name -ne 'scratchpad') { $leaf = "$($f.Directory.Name)-$($f.Name)" }
        if (-not $WhatIfOnly) { Copy-Item $f.FullName (Join-Path $targetDir $leaf) -Force }
        $total++
    }
    "{0,-34} {1,3} files  {2,8:N0} KB" -f $SessionMap[$prefix], $files.Count, (($files | Measure-Object Length -Sum).Sum/1KB)
}
"TOTAL kept: $total files"
