# SimCopter ingame verification

*How to launch the remake standalone and drive/screenshot its Slate UI from PowerShell to verify a change in the real game*

*Recorded 2026-07-25; ported into the repo 2026-07-29.*

> **Read `AGENTS.md` §6 first: don't do this routinely.** In-game validation is reserved for a
> genuinely complex problem that a build plus an automation test cannot settle, and it is worth
> asking before starting — it seizes the foreground window and keyboard. The rest of this note is
> *how* to do it on the occasions when it is warranted, not a sign that you should.

Verified 2026-07-24. Build, then run the game in a window and drive it with Win32 calls — no
Playwright/driver needed:

```powershell
& "C:\GameDev\UE_5.8\Engine\Build\BatchFiles\Build.bat" SimCopterRemakeEditor Win64 Development -Project="S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject" -WaitMutex
Start-Process "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -ArgumentList '"...SimCopterRemake.uproject"','-game','-windowed','-ResX=1600','-ResY=900','-log'
```

- **Wait for readiness by polling** `SimCopterRemake/Saved/Logs/SimCopterRemake.log` for
  `Bringing World ... up for play`, then ~10s more for the city build. Clicking before the widget
  exists silently does nothing and looks like a broken handler.
- **Screenshot**: `GetClientRect` + `ClientToScreen` + `Graphics.CopyFromScreen` after
  `SetForegroundWindow` (scripts kept in the session scratchpad; ~20 lines of Add-Type P/Invoke).
- **Click**: `SetCursorPos` + `mouse_event` LEFTDOWN/LEFTUP on client coords converted with
  `ClientToScreen`.
- **Trap**: a vertically centred Slate panel moves when its text changes height (e.g. an extra
  detail line appears), so cached button coordinates go stale mid-sequence — re-screenshot and
  re-locate between clicks instead of assuming a fixed layout.
- `UE_LOG(LogTemp, Display, ...)` lines in the log are the cheapest proof a handler ran.
- **Synthesized `keybd_event` keys reach Slate and the `~` console, but NOT gameplay input** — the
  possessed pawn's `BindKey`/`BindAction` handlers never fire from them (verified 2026-07-25).
  Drive gameplay through `UFUNCTION(Exec)` console commands instead; the pawn Exec chain only
  covers the **currently possessed** pawn.
- A session entered from the menu possesses `ASimCopterOnFootPawn`, not the helicopter, and
  entering `/Game/CityRender` directly possesses nothing useful — so helicopter keys and its Exec
  commands are both dead until you board. `SimBoardHelicopter` (on-foot pawn Exec) does it.
- The harness's Bash tool was broken in this session (`Top-level not found: C:\Program Files\Git\bin`);
  everything above ran through the PowerShell tool. Long `Start-Sleep` is blocked in the foreground —
  use `run_in_background: true` with a polling loop.

Automation tests (no window): `UnrealEditor-Cmd.exe <uproject> -unattended -nop4 -nosplash -NullRHI
-stdout -FullStdOutLogOutput -ExecCmds="Automation RunTests SimCopter; Quit"`. See
[[simcopter-ghidra-workflow]] for the decompile side.
