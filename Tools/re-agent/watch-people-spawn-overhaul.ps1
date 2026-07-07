param(
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"

$toolsDir = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $toolsDir
$bridge = Join-Path $repo "Tools\re-agent\.venv\Scripts\ghidra-bridge.exe"
$agent = Join-Path $repo "Tools\re-agent\.venv\Scripts\re-agent.exe"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$log = Join-Path $repo "reports\re-agent\logs\people-spawn-overhaul-$stamp.log"
$targetFile = Join-Path $repo "Docs\scratchpad\re-agent\people-spawn-overhaul-targets.txt"

$targets = @(
    @{ Address = "0x004c2ba0"; Name = "FUN_004c2ba0"; Note = "ambient spawn driver / camera-edge population pressure" },
    @{ Address = "0x004c2550"; Name = "FUN_004c2550"; Note = "per-tile ambient spawn attempt wrapper" },
    @{ Address = "0x004c25b0"; Name = "FUN_004c25b0"; Note = "scripted building ambient spawns" },
    @{ Address = "0x004c2450"; Name = "FUN_004c2450"; Note = "ambient behavior-class selector retry" },
    @{ Address = "0x004c3eb0"; Name = "FUN_004c3eb0"; Note = "spawn wrapper into person configurator" },
    @{ Address = "0x004c4190"; Name = "FUN_004c4190"; Note = "main person spawn configurator" },
    @{ Address = "0x004c02a0"; Name = "FUN_004c02a0"; Note = "local spawn position sampler" },
    @{ Address = "0x004c92a0"; Name = "FUN_004c92a0"; Note = "terrain/density gate helper used by FUN_004c9cc0" },
    @{ Address = "0x004abce0"; Name = "FUN_004abce0"; Note = "terrain/density grid builder for DAT_005bde80" },
    @{ Address = "0x004c3010"; Name = "FUN_004c3010"; Note = "people runtime table initialization, including DAT_0058d6d4 caps" },
    @{ Address = "0x004c9470"; Name = "FUN_004c9470"; Note = "per-step move gate and per-tile occupancy cap use" }
)

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetFile) | Out-Null

$lines = @(
    "# People Spawn Overhaul re-agent Queue",
    "",
    "Generated: $(Get-Date -Format o)",
    "",
    "Already accepted before this queue:",
    "- 0x004c9cc0 FUN_004c9cc0 - ambient tile/density gate PASS",
    "",
    "Queued fresh Codex/checker targets:"
)
foreach ($target in $targets) {
    $lines += "- $($target.Address) $($target.Name) - $($target.Note)"
}
$lines += ""
$lines += "Data/xref follow-up:"
$lines += "- DAT_005d9200 scene-cell flag 0x20 producer: identify exact writer before spending a reverse run."
$lines | Set-Content -Path $targetFile -Encoding UTF8

if ($NoLaunch) {
    Write-Host "Wrote target queue: $targetFile"
    Write-Host "Planned log: $log"
    return
}

$targetRows = ($targets | ForEach-Object { "$($_.Address)|$($_.Name)|$($_.Note)" }) -join "`n"
$script = @"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
Set-Location '$repo'
`$ErrorActionPreference = 'Continue'
`$env:RE_AGENT_BACKEND_CLI_PATH = '$bridge'
`$env:PYTHONUTF8 = '1'
`$env:PYTHONIOENCODING = 'utf-8'
`$gb = '$bridge'
`$ra = '$agent'
`$log = '$log'
`$targetRows = @'
$targetRows
'@
`$targets = @()
foreach (`$line in (`$targetRows -split '\r?\n')) {
    if ([string]::IsNullOrWhiteSpace(`$line)) { continue }
    `$parts = `$line -split '\|', 3
    if (`$parts.Count -eq 3) {
        `$targets += [pscustomobject]@{ Address = `$parts[0]; Name = `$parts[1]; Note = `$parts[2] }
    }
}
if (Test-Path 'Docs\scratchpad\re-agent\re-agent-progress.tmp') { Remove-Item -LiteralPath 'Docs\scratchpad\re-agent\re-agent-progress.tmp' -Force }

'=== SimCopter people spawning overhaul queue ===' | Tee-Object -FilePath `$log -Append
('Started: ' + (Get-Date -Format o)) | Tee-Object -FilePath `$log -Append
('Targets: ' + `$targets.Count) | Tee-Object -FilePath `$log -Append
('Backend: ' + `$env:RE_AGENT_BACKEND_CLI_PATH) | Tee-Object -FilePath `$log -Append
'' | Tee-Object -FilePath `$log -Append
'--- Codex login ---' | Tee-Object -FilePath `$log -Append
codex --version 2>&1 | Tee-Object -FilePath `$log -Append
codex login status 2>&1 | Tee-Object -FilePath `$log -Append

`$index = 0
foreach (`$target in `$targets) {
    `$index += 1
    `$address = [string]`$target.Address
    `$name = [string]`$target.Name
    `$note = [string]`$target.Note
    if (Test-Path 'Docs\scratchpad\re-agent\re-agent-progress.tmp') { Remove-Item -LiteralPath 'Docs\scratchpad\re-agent\re-agent-progress.tmp' -Force }

    '' | Tee-Object -FilePath `$log -Append
    ('=== [' + `$index + '/' + `$targets.Count + '] ' + `$address + ' ' + `$name + ' ===') | Tee-Object -FilePath `$log -Append
    `$note | Tee-Object -FilePath `$log -Append
    ('Target started: ' + (Get-Date -Format o)) | Tee-Object -FilePath `$log -Append

    '--- Bridge preflight ---' | Tee-Object -FilePath `$log -Append
    & `$gb decompile `$address 2>&1 | Tee-Object -FilePath `$log -Append
    & `$gb xrefs-to `$address 2>&1 | Tee-Object -FilePath `$log -Append
    & `$gb xrefs-from `$address 2>&1 | Tee-Object -FilePath `$log -Append

    '--- re-agent dry run ---' | Tee-Object -FilePath `$log -Append
    & `$ra reverse --address `$address --dry-run 2>&1 | Tee-Object -FilePath `$log -Append

    '--- re-agent paid reverse run ---' | Tee-Object -FilePath `$log -Append
    & `$ra reverse --address `$address 2>&1 | Tee-Object -FilePath `$log -Append
    `$exit = `$LASTEXITCODE
    ('Target finished: ' + (Get-Date -Format o)) | Tee-Object -FilePath `$log -Append
    ('Exit code: ' + `$exit) | Tee-Object -FilePath `$log -Append

    '--- re-agent status ---' | Tee-Object -FilePath `$log -Append
    & `$ra status 2>&1 | Tee-Object -FilePath `$log -Append
}

'' | Tee-Object -FilePath `$log -Append
'--- Data/xref follow-up: DAT_005d9200 scene-cell flag 0x20 ---' | Tee-Object -FilePath `$log -Append
& `$gb xrefs-to 0x005d9200 2>&1 | Tee-Object -FilePath `$log -Append

'' | Tee-Object -FilePath `$log -Append
('Queue finished: ' + (Get-Date -Format o)) | Tee-Object -FilePath `$log -Append
('Log saved to: ' + `$log) | Tee-Object -FilePath `$log -Append
Write-Host ''
Write-Host 'People spawning overhaul queue finished.'
Write-Host 'Log saved to: $log'
Write-Host 'Window will stay open.'
"@

Start-Process powershell.exe `
    -ArgumentList @("-NoExit", "-ExecutionPolicy", "Bypass", "-Command", $script) `
    -WorkingDirectory $repo

Write-Host "Launched visible people-spawn overhaul queue."
Write-Host "Targets: $targetFile"
Write-Host "Log: $log"
