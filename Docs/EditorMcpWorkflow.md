# Editor MCP workflow

The project runs Unreal's **ModelContextProtocol** plugin, which exposes the live editor — the
loaded level, every actor and asset, the output log, automation tests, the Slate UI — to an MCP
client over HTTP. It is the cheapest way to answer "what is actually in the level right now", and it
beats guessing at a `.umap` you cannot read.

It is **not** a way to run the game. That is still `Docs/memory/simcopter-ingame-verification.md`,
and still a last resort (AGENTS.md §7).

---

## 1. Getting connected

The server is published by the editor itself, so **the editor has to be running**. It listens on:

```
http://127.0.0.1:8000/mcp
```

This project already has it starting automatically — `bAutoStartServer=True` and
`ServerPortNumber=8000` are set under
`[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]` in
`Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini` (per-user, so a fresh clone may need
it ticked in **Editor Preferences → Model Context Protocol**). The engine default is `false`.

### When nothing is listening on 8000

**The usual cause is a second Unreal instance holding the port**, and it is easy to cause by
accident: a headless `UnrealEditor-Cmd` run — the automation-test command in AGENTS.md §3, or a
`-ExecutePythonScript` bake — reads the same per-project setting and grabs 8000 on the way up. If it
wins the race, the interactive editor logs

```
LogModelContextProtocol: Starting MCP server on port 8000
LogHttpListener: Error: HttpListener unable to bind to 127.0.0.1:8000
```

and **gives up permanently**: autostart is a one-shot at module startup, so freeing the port later
does not make it retry. The editor then looks completely healthy while having no server at all.

Recover it without restarting — from the editor console (`` ` ``, or the Output Log's command box):

```
ModelContextProtocol.StartServer          # optionally: ModelContextProtocol.StartServer <port>
```

`ModelContextProtocol.StopServer` is the counterpart, and `-ModelContextProtocolStartServer` on the
command line forces it on for one launch. To avoid the collision in the first place, pass a
different port to the headless run, or just don't run one while you need the editor's MCP.

**Diagnose before assuming.** "No MCP tools" has two unrelated causes — a dead server and an
unloaded client config (§1 above) — and they look identical from the client. Two commands separate
them:

```powershell
Get-NetTCPConnection -LocalPort 8000 -State Listen    # nothing back -> server side
Select-String Saved\Logs\SimCopterRemake*.log -Pattern 'LogModelContextProtocol|HttpListener'
```

Mind which log: a second concurrent instance writes `SimCopterRemake_2.log`, so the interactive
editor's startup may not be in `SimCopterRemake.log` at all.

The client config is `SimCopterRemake/.mcp.json` — note the path. **It sits in the `.uproject`
folder, not at the repo root**, so a client launched from the repo root never loads it and comes up
with no MCP tools at all. Either start the client from `SimCopterRemake/`, or copy that file to the
repo root, or fall back to §2.

Check the endpoint is alive before assuming anything is broken:

```powershell
Tools\Unreal\McpCall.ps1 tools/list
```

## 2. The raw HTTP fallback

`Tools/Unreal/McpCall.ps1` speaks the protocol directly, for when the client did not load the
config. It handles the parts that are easy to get wrong:

* `Accept: application/json, text/event-stream` is **required** — the streamable-HTTP transport
  rejects a plain JSON Accept header.
* `initialize` returns an `Mcp-Session-Id` **response header** that every later request must echo.
* A `notifications/initialized` notification has to follow `initialize` before tool calls.
* The reply may come back as SSE (`data:` lines) rather than bare JSON.

```powershell
Tools\Unreal\McpCall.ps1 tools/call '{"name":"list_toolsets","arguments":{}}'
```

Each invocation opens a fresh session, so it is one round trip per call — fine for a handful of
queries, not for a loop.

## 3. Everything lives behind three tools

`tools/list` returns only three entries, and none of them do any editor work:

| Tool | Purpose |
| --- | --- |
| `list_toolsets` | Names + descriptions of every registered toolset |
| `describe_toolset` | Full tool list and **input schemas** for one toolset |
| `call_tool` | Actually calls something: `toolset_name` + `tool_name` + `arguments` |

So a real call is nested two deep — a `call_tool` whose `arguments` contains another `arguments`:

```powershell
Tools\Unreal\McpCall.ps1 tools/call '{
  "name": "call_tool",
  "arguments": {
    "toolset_name": "editor_toolset.toolsets.object.ObjectTools",
    "tool_name": "get_properties",
    "arguments": { "instance": { "refPath": "..." }, "properties": ["FogDensity"] }
  }
}'
```

## 4. The toolsets worth knowing

There are ~50. The ones this project actually reaches for:

* **`editor_toolset.toolsets.scene.SceneTools`** — find/place/remove actors in the loaded level,
  load levels, drive the level camera.
* **`editor_toolset.toolsets.object.ObjectTools`** — `list_properties` / `get_properties` /
  `set_properties` / `get_class` on any object. The workhorse.
* **`editor_toolset.toolsets.actor.ActorTools`** — transforms, labels, components, attachment.
* **`editor_toolset.toolsets.asset.AssetTools`** — assets in the project and files on disk.
* **`editor_toolset.toolsets.material.MaterialTools`** / **`material_instance.MaterialInstanceTools`**
  — an alternative to the `Tools/Unreal/*.py` editor scripts for one-off material work.
* **`EditorToolset.LogsToolset`** — read the output log, set category verbosity. Much cheaper than
  launching the game to read a log.
* **`AutomationTestToolset.AutomationTestToolset`** — discover and run the `SimCopter.*` tests
  in the already-open editor instead of paying a fresh `UnrealEditor-Cmd` boot.
* **`SlateInspectorToolset.SlateInspectorToolset`** — Playwright-style snapshot/click/screenshot of
  the editor UI.
* **`editor_toolset.toolsets.programmatic.ProgrammaticToolset`** — batches several of the above
  through one sandboxed Python script. Use it when a task is many small dependent calls.

## 5. Traps

**Schemas are strict, and optional-looking arguments are not optional.** `SceneTools.find_actors`
requires `name`, `tag` *and* `collision_channels` even when you only care about the name; omitting
them fails. The failure is friendly though — the error prints the entire expected schema, so a
deliberate bad call is a fast way to learn one.

**Never guess an object path — read it.** Objects are addressed as `{"refPath": "..."}`, and the
component path does not follow the property name. On the level's day sequence actor the property is
`ExponentialHeightFogComponent` but the object is:

```
/Game/CityRender.CityRender:PersistentLevel.CelestialVaultDaySequenceActor_1.ExponentialHeightFog
```

Get the ref by reading the property off the owner and reusing the `refPath` that comes back.

**`get_properties` double-encodes.** The result's `returnValue` is a JSON *string* containing the
JSON object, so it needs unwrapping twice.

**`list_properties` before `set_properties`.** Property names vary per class and cannot be guessed;
setting a name the object does not have fails silently, exactly like `SetScalarParameterValue` on a
missing material parameter (see `Docs/memory/simcopter-vehicle-material.md`).

**Tool calls run on the game thread.** A long call blocks the editor.

**Editing the level dirties it.** Anything placed or changed through MCP has to be saved by whoever
is at the keyboard — say so rather than assuming it persisted.

**MCP cannot build C++.** The editor holds an active Live Coding session, so `RebuildUnrealCpp.bat`
fails to link while it is open (AGENTS.md §2). New `UCLASS`es therefore cannot be created and placed
in one pass: build with the editor **closed**, then reopen and place through MCP.
