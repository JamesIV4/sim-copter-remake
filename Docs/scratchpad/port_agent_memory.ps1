# One-off migration (2026-07-29): move agent memories out of Claude Code's machine-local store
# into Docs/memory/, converting YAML frontmatter to this repo's plain-heading style.
# See Docs/memory/agent-workspace-conventions.md for why the repo is the canonical home.

param(
    [string]$Source = "C:\Users\james\.claude\projects\s--Repos-sim-copter-remake\memory",
    [string]$Dest   = "S:\Repos\sim-copter-remake\Docs\memory",
    [switch]$WhatIfOnly
)

# Files that must NOT be overwritten: the repo copy is the canonical, longer version and the
# external one is only a pointer stub. Verified by size before this list was written.
$RepoIsCanonical = @('simcopter-heli-flight-model.md', 'simcopter-people-logic-next.md')

# Pointer stub that exists only to survive a cold start outside the repo; superseded here by
# agent-workspace-conventions.md.
$SkipEntirely = @('MEMORY.md', 'repo-is-the-workspace.md')

function ConvertTo-RepoNote {
    param([string]$Path)

    $raw = Get-Content $Path -Raw -Encoding UTF8
    $name = [IO.Path]::GetFileNameWithoutExtension($Path)
    $description = $null
    $modified = $null
    $body = $raw

    if ($raw -match '(?s)^---\r?\n(.*?)\r?\n---\r?\n(.*)$') {
        $front = $Matches[1]
        $body = $Matches[2]
        if ($front -match '(?m)^description:\s*(.+?)\s*$') { $description = $Matches[1] }
        if ($front -match '(?m)^\s*modified:\s*(\S+)') { $modified = $Matches[1] }
    }

    # "simcopter-airport-spawn" -> "SimCopter airport spawn"
    $title = ($name -replace '^simcopter-', 'SimCopter ') -replace '-', ' '
    if ($title -notmatch '^SimCopter') { $title = $title.Substring(0,1).ToUpper() + $title.Substring(1) }

    $header = "# $title`n"
    if ($description) { $header += "`n*$description*`n" }
    if ($modified)    { $header += "`n*Recorded $($modified.Substring(0,10)); ported into the repo 2026-07-29.*`n" }
    else              { $header += "`n*Ported into the repo 2026-07-29.*`n" }

    return ($header + "`n" + $body.TrimStart())
}

$copied = @(); $skippedSame = @(); $skippedCanonical = @(); $replaced = @()

Get-ChildItem $Source -Filter *.md | Sort-Object Name | ForEach-Object {
    if ($SkipEntirely -contains $_.Name) { return }

    $target = Join-Path $Dest $_.Name

    if ($RepoIsCanonical -contains $_.Name) { $skippedCanonical += $_.Name; return }

    if (Test-Path $target) {
        $srcLen = $_.Length; $dstLen = (Get-Item $target).Length
        if ($srcLen -eq $dstLen) { $skippedSame += $_.Name; return }
        if ($srcLen -le $dstLen) { $skippedCanonical += "$($_.Name) (repo larger)"; return }
        $replaced += "$($_.Name) ($dstLen -> $srcLen)"
    } else {
        $copied += $_.Name
    }

    if (-not $WhatIfOnly) {
        ConvertTo-RepoNote -Path $_.FullName | Set-Content $target -Encoding UTF8 -NoNewline
    }
}

"COPIED ($($copied.Count)):";            $copied            | ForEach-Object { "  + $_" }
"REPLACED, external newer ($($replaced.Count)):"; $replaced | ForEach-Object { "  ~ $_" }
"IDENTICAL, left alone ($($skippedSame.Count)):"; $skippedSame | ForEach-Object { "  = $_" }
"REPO CANONICAL, left alone ($($skippedCanonical.Count)):"; $skippedCanonical | ForEach-Object { "  ! $_" }
