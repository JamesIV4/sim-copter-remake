# Editor MCP

*"The config is in the .uproject folder, so a client started at the repo root has no MCP tools at
all — and it looks exactly like the server being down."*

*Recorded 2026-08-04.*

Full how-to, toolset list and traps: [`Docs/EditorMcpWorkflow.md`](../EditorMcpWorkflow.md).
Working-instruction summary: AGENTS.md §6.

## The one that costs an hour

`SimCopterRemake/.mcp.json` sits beside the `.uproject`, **not** at the repo root. Claude Code and
friends read `.mcp.json` from the directory they were started in, so a session opened at
`s:\Repos\sim-copter-remake` registers **zero** MCP tools and a tool search for them returns
nothing. That is a config-discovery failure, not a dead server — check
`http://127.0.0.1:8000/mcp` before concluding anything, via `Tools/Unreal/McpCall.ps1`, which speaks
the protocol over raw HTTP and needs no client config.

The endpoint only exists while **the editor is running**; the plugin publishes it.

## The other one that costs an hour

**A headless `UnrealEditor-Cmd` run steals port 8000 from the open editor.** The automation-test
command (AGENTS.md §3) and the `-ExecutePythonScript` bakes read the same per-project
`bAutoStartServer=True` and bind 8000 on the way up. Whichever instance starts first wins; the loser
logs `HttpListener unable to bind to 127.0.0.1:8000` and **never retries**, because autostart is a
one-shot at module startup. Freeing the port does not bring it back. The editor looks perfectly
healthy with no server at all.

`ModelContextProtocol.StartServer` in the editor console fixes the running session — no restart.
And check `SimCopterRemake_2.log`, not `SimCopterRemake.log`: the second concurrent instance gets
the `_2` file, so the interactive editor's startup is often not in the log you first open.

## Shape of the API

`tools/list` returns just three tools — `list_toolsets`, `describe_toolset`, `call_tool` — and every
real operation is nested inside `call_tool` (`toolset_name` + `tool_name` + `arguments`), so the
payload has an `arguments` inside an `arguments`. ~50 toolsets are registered; `ObjectTools`,
`SceneTools`, `ActorTools`, `LogsToolset` and `AutomationTestToolset` cover most of what this
project wants.

## Traps

* **Strict schemas.** `SceneTools.find_actors` requires `name`, `tag` *and* `collision_channels`
  even when only the name matters. The error prints the whole schema, so a deliberate bad call is a
  cheap way to learn one — but `describe_toolset` first is cheaper.
* **Never guess an object path.** Refs are `{"refPath": "..."}` and the component path does not
  follow the property name: on the level's day sequence actor the property is
  `ExponentialHeightFogComponent` while the object is `...CelestialVaultDaySequenceActor_1.ExponentialHeightFog`.
  Read the property off the owner and reuse the ref that comes back.
* **`get_properties` double-encodes** — `returnValue` is a JSON string holding the JSON object.
* **`set_properties` on a name the object lacks fails silently**, the same way
  `SetScalarParameterValue` does (see [[simcopter-vehicle-material]]).
* **Tool calls run on the game thread**, so a slow one freezes the editor.
* **It cannot build C++.** The open editor holds a Live Coding session and the link fails even
  though `RebuildUnrealCpp.bat` pins `-NoLiveCoding` (see [[build-and-run]]). A new `UCLASS` cannot
  be compiled and placed in one pass: build with the editor closed, reopen, then place over MCP.
* **Editing the level dirties it.** Whoever is at the keyboard still has to save; say so rather than
  assuming the change persisted.

## What it does not replace

Running the actual game — that is still [[simcopter-ingame-verification]], and still a last resort.
MCP answers "what is in the level and what are its properties", not "does it feel right in flight".
