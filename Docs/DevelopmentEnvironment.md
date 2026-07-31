# Development Environment

## Unreal Engine

This project targets Unreal Engine 5.8.

Local engine install currently used for builds:

```text
C:\GameDev\UE_5.8
```

Useful paths:

```text
C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
C:\GameDev\UE_5.8\Engine\Build\BatchFiles\Build.bat
C:\GameDev\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe
```

Enabled engine plugins:

```text
ModelingToolsEditorMode
ModelContextProtocol
ProceduralMeshComponent
```

Typical editor target build command from the repository root:

```powershell
& "C:\GameDev\UE_5.8\Engine\Build\BatchFiles\Build.bat" SimCopterRemakeEditor Win64 Development -Project="S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject" -WaitMutex
```

If the editor has Live Coding active, `Build.bat` can stop with:

```text
Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11 if iterating on code in the editor or game
```

Close the running editor session or disable Live Coding before running a full command-line build.

Headless parser test command:

```powershell
& "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject" -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests SimCopter.Formats.SimCity2000; Quit"
```

The original game files live under `Reference/SimCopterOriginalGame` on this machine. That folder is ignored by git and should remain user-provided.

### Running the game: the front end and the city level

```powershell
Start-Process "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject"','-game','-windowed','-ResX=1600','-ResY=900','-log'
```

The game boots into `/Game/MainMenu`, an otherwise empty front-end map whose World Settings override
the game mode to `ASimCopterMainMenuGameMode`. No city is loaded until the player chooses one, which
is why `GameDefaultMap` points there and not at `CityRender`.

`SSimCopterMainMenu` carries the original's five items (see `help/English/37ref.htm` in the original
game folder):

| Item | State |
| --- | --- |
| New Career Game | choose one of three cities (the original's opening successor trio City0/1/2), then fly it |
| Open Career Game | choose a compatible named career save, then restore it |
| New User Game | pick any `.sc2` under the original game's `cities/`; the original opened a file dialog |
| Open User Game | choose a compatible named user-game save, then restore it |
| Quit | quits |

Below a divider the menu carries two development extras with no original equivalent: a mission type
to create as soon as the city is up, and a toggle for rolling the city's first scheduled job
immediately instead of after the original's 180 second opening countdown.

Choosing a city writes it into `USimCopterSessionSubsystem` (a game-instance subsystem, so it
survives the travel), then opens `/Game/CityRender`. There the city actor loads that `.sc2` -
`cities/career/city<N>.sc2` for a career - and `ASimCopterGameMode` opens the matching mission
session. Entering `CityRender` directly (PIE, or `-game /Game/CityRender`) still works: with no
pending session the mission actor opens its own default session on city 0.

To test the front end in the editor, open `/Game/MainMenu` before pressing Play - PIE always plays
the map that is open.

City-level console commands (the city level's game mode): `SimMainMenu` (the Settings panel's "Leave
City"), `SimFreeRoam <city>`, `SimCityJobs <city>`, `SimLoadMission <missionIndex> [city]`
(`SimLoadMission -1` lists the indices). The front end has `SimNewCareer <city>` and
`SimNewUserGame <index>`.

## Unreal MCP

Unreal MCP is enabled through the experimental engine plugin named `ModelContextProtocol` in `SimCopterRemake.uproject`.

Editor-side setup:

1. Launch the editor.
2. Enable auto-start at `Edit > Editor Preferences > General > Model Context Protocol`, or run this console command:

```text
ModelContextProtocol.StartServer 8000
```

3. Generate Codex config from the editor console:

```text
ModelContextProtocol.GenerateClientConfig Codex
```

The generated project config currently lives at:

```text
S:\Repos\sim-copter-remake\SimCopterRemake\.codex\config.toml
```

The repo root also has a matching `.codex/config.toml` so Codex sessions launched from `S:\Repos\sim-copter-remake` can discover the same local server.

The configured endpoint is:

```text
http://127.0.0.1:8000/mcp
```

If Codex was already running before the config was generated, restart Codex from the repo root so it reloads MCP server configuration.

## Reverse Engineering Tools Seen On This Machine

Available in `PATH`:

```text
C:\msys64\mingw64\bin\strings.exe
C:\msys64\mingw64\bin\objdump.exe
C:\Users\james\scoop\shims\rizin.exe
```

Ghidra has a shim at `C:\Users\james\scoop\shims\ghidrarun`, but `ghidraRun.bat` was not available from this PowerShell session.
