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
