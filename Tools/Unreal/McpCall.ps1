# Talks to the running editor's ModelContextProtocol plugin over raw HTTP JSON-RPC.
#
# ONLY needed as a fallback: if your client loaded SimCopterRemake/.mcp.json it already has the
# tools natively and you should use those instead. This exists because that config lives in the
# .uproject folder, so a client started at the REPO ROOT never sees it. See Docs/EditorMcpWorkflow.md.
#
# The editor must be running. Each invocation opens its own MCP session.
#
#   .\McpCall.ps1 tools/list
#   .\McpCall.ps1 tools/call '{"name":"list_toolsets","arguments":{}}'
#   .\McpCall.ps1 tools/call '{"name":"call_tool","arguments":{"toolset_name":"...","tool_name":"...","arguments":{}}}'
param(
	[Parameter(Mandatory = $true)][string]$Method,
	[string]$ParamsJson = '{}'
)

$Url = 'http://127.0.0.1:8000/mcp'
$Accept = 'application/json, text/event-stream'

function Read-McpBody($Response) {
	# Streamable HTTP may answer as SSE; unwrap the data: lines when it does.
	$content = $Response.Content
	if ($content -match '(?m)^data:') {
		return (($content -split "`n" | Where-Object { $_ -match '^data:' } | ForEach-Object { $_.Substring(5).Trim() }) -join '')
	}
	return $content
}

$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"claude-code-raw","version":"1.0"}}}'
$initResponse = Invoke-WebRequest -Uri $Url -Method Post -Body $init -ContentType 'application/json' -Headers @{ 'Accept' = $Accept } -UseBasicParsing -TimeoutSec 30
$session = $initResponse.Headers['Mcp-Session-Id']
if ($session -is [array]) { $session = $session[0] }
$headers = @{ 'Accept' = $Accept; 'Mcp-Session-Id' = $session }

$ready = '{"jsonrpc":"2.0","method":"notifications/initialized"}'
try { Invoke-WebRequest -Uri $Url -Method Post -Body $ready -ContentType 'application/json' -Headers $headers -UseBasicParsing -TimeoutSec 30 | Out-Null } catch {}

$payload = "{`"jsonrpc`":`"2.0`",`"id`":2,`"method`":`"$Method`",`"params`":$ParamsJson}"
$response = Invoke-WebRequest -Uri $Url -Method Post -Body $payload -ContentType 'application/json' -Headers $headers -UseBasicParsing -TimeoutSec 120
Read-McpBody $response
