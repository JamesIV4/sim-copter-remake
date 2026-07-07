param(
    [Parameter(Mandatory = $true)]
    [string]$Address
)

$ErrorActionPreference = "Stop"

$toolsDir = Split-Path -Parent $PSScriptRoot
$repo = Split-Path -Parent $toolsDir
$bridge = Join-Path $repo "Tools\re-agent\.venv\Scripts\ghidra-bridge.exe"
$agent = Join-Path $repo "Tools\re-agent\.venv\Scripts\re-agent.exe"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$safeAddress = $Address -replace "[^0-9A-Za-z_-]", ""
$log = Join-Path $repo "reports\re-agent\logs\live-$safeAddress-$stamp.log"

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $log) | Out-Null

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
if (Test-Path 'Docs\scratchpad\re-agent\re-agent-progress.tmp') { Remove-Item -LiteralPath 'Docs\scratchpad\re-agent\re-agent-progress.tmp' -Force }
'=== SimCopter re-agent live run: $Address ===' | Tee-Object -FilePath `$log -Append
('Started: ' + (Get-Date -Format o)) | Tee-Object -FilePath `$log -Append
('Backend: ' + `$env:RE_AGENT_BACKEND_CLI_PATH) | Tee-Object -FilePath `$log -Append
('Python UTF8: ' + `$env:PYTHONUTF8) | Tee-Object -FilePath `$log -Append
'' | Tee-Object -FilePath `$log -Append
'--- Codex login ---' | Tee-Object -FilePath `$log -Append
codex --version 2>&1 | Tee-Object -FilePath `$log -Append
codex login status 2>&1 | Tee-Object -FilePath `$log -Append
'' | Tee-Object -FilePath `$log -Append
'--- Bridge preflight ---' | Tee-Object -FilePath `$log -Append
& `$gb decompile $Address 2>&1 | Tee-Object -FilePath `$log -Append
& `$gb xrefs-to $Address 2>&1 | Tee-Object -FilePath `$log -Append
& `$gb xrefs-from $Address 2>&1 | Tee-Object -FilePath `$log -Append
'' | Tee-Object -FilePath `$log -Append
'--- re-agent dry run ---' | Tee-Object -FilePath `$log -Append
& `$ra reverse --address $Address --dry-run 2>&1 | Tee-Object -FilePath `$log -Append
'' | Tee-Object -FilePath `$log -Append
'--- re-agent paid reverse run ---' | Tee-Object -FilePath `$log -Append
& `$ra reverse --address $Address 2>&1 | Tee-Object -FilePath `$log -Append
`$exit = `$LASTEXITCODE
'' | Tee-Object -FilePath `$log -Append
('Finished: ' + (Get-Date -Format o)) | Tee-Object -FilePath `$log -Append
('Exit code: ' + `$exit) | Tee-Object -FilePath `$log -Append
'' | Tee-Object -FilePath `$log -Append
'--- re-agent status ---' | Tee-Object -FilePath `$log -Append
& `$ra status 2>&1 | Tee-Object -FilePath `$log -Append
Write-Host ''
Write-Host 'Log saved to: $log'
Write-Host 'Window will stay open. Exit code:' `$exit
"@

Start-Process powershell.exe `
    -ArgumentList @("-NoExit", "-ExecutionPolicy", "Bypass", "-Command", $script) `
    -WorkingDirectory $repo

Write-Host "Launched visible re-agent window for $Address"
Write-Host "Log: $log"
