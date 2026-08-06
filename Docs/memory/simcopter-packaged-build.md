# Packaging SimCopter Remake (the Shipping build)

What a `Package Project` build gets wrong that a standalone-from-editor run never shows, decoded
2026-08-06 from `S:\SimCopter Stuff\Builds\pre-release 0.8\Windows`. All three faults had the same
shape: **a thing the editor finds by walking the source tree, which the cook cannot see as a
reference and therefore never stages.** The staged `Manifest_UFSFiles_Win64.txt` /
`Manifest_NonUFSFiles_Win64.txt` next to the exe is the ground truth for "did this ship" - read
them before theorising.

## 1. Only the default map was cooked, so no city would ever load

Both levels are opened by *name* from C++ (`USimCopterSessionSubsystem::GetMainMenuLevelName()` /
`GetCityLevelName()` -> `/Game/MainMenu`, `/Game/CityRender`). A string is not a reference, so the
cooker followed `GameDefaultMap` and stopped. `MainMenu.umap` shipped; **`CityRender.umap` did
not**, and `UGameplayStatics::OpenLevel` on a package that is not in the pak fails silently - the
front end just stays where it is, which reads exactly like "the city select bounced me back".

Fixed with `+MapsToCook` for both maps in `DefaultGame.ini`. The same class of bug covers the baked
city atlas: `/Game/Generated/CityAtlas/MI_CityImage_<page>` is built by `LoadObject` on a generated
name, so it needs `+DirectoriesToAlwaysCook`. (That one degrades rather than breaks - the city
falls back to decoding BMPs at runtime - which is why it hid behind the map bug.)

## 2. The menu backdrop was SKYCOOL.BMP stretched over the whole screen

`Content/Generated/Movies/MENUSKY.mp4` was not staged at all: **`.mp4` is not one of the extensions
UAT stages by default from `Content/`** (`.png` is, which is why `Content/Slate` came through and
made the front end look otherwise correct). With no movie, `SSimCopterMenuCloudBackdrop` took its
`FallbackCloudBrush` branch and stretched the **400x66** `SKYCOOL.BMP` across a 2560x1440 window.
The result is a field of dithered noise - render it with `Docs/scratchpad/render_skycool.py` and
compare against any "broken clouds" screenshot before chasing a media decode bug.

Fixed with `+DirectoriesToAlwaysStageAsNonUFS=(Path="Generated/Movies")`. **NonUFS, not UFS, and
that matters:** Media Foundation opens the file through the OS and cannot see inside a `.pak`, so a
UFS-staged movie is worse than a missing one - `FPaths::FileExists` (pak-aware) says yes, the brush
is built, nothing ever decodes, and the media texture paints uninitialised memory. `ResolveMenuSkyMoviePath`
now probes `IPlatformFile::GetPlatformPhysical()` on purpose so that case degrades to the fallback.

## 3. Where the player's original game files go: `<package root>/SimCopter`

Every reader used to carry its own copy-pasted candidate list (one of them hard-coded a developer's
`S:/Repos/...`), so a packaged build found some subsystems' data and not others. That is now one
list in `Formats/SimCopterOriginalGamePaths.h`.

The candidates are relative to `FPaths::ProjectDir()`, which is `SimCopterRemake/` in a checkout
and `<Package>/SimCopterRemake/` when staged - so **`../SimCopter` means the repo root to a
developer and the folder beside `SimCopterRemake.exe` to a player**, one path for both. The
game creates it plus `PLACE ORIGINAL in SimCopter FOLDER.txt` on first launch when nothing
resolves; UAT cannot stage anything above the project folder, so runtime creation is the only way
that folder reaches the package root.

**A candidate is only accepted when it looks like an install** (`IsOriginalGameRoot`: it has a
`cities`/`bmp`/`geo`/`tweak` subfolder). Without that check the empty placeholder folder resolves
first and every reader comes up empty - the folder exists from launch one, by design. The same
check now gates the *authored* `OriginalGameRoot` on the city/traffic/helicopter/hangar actors,
whose level-saved `../Reference/SimCopterOriginalGame` is a developer path that means nothing to a
player.

## Watch out

- `DirectoriesToAlwaysStageAs*` paths are relative to **`Content/`**, not the project dir, and land
  at `<Staged>/SimCopterRemake/Content/<path>`. `MapsToCook`/`DirectoriesToAlwaysCook` take
  `/Game/...` package paths. Mixing the two conventions up silently stages nothing but a warning.
- The editor rewrites `Config/DefaultGame.ini` when packaging settings are touched in the UI and
  **has dropped the whole `[/Script/UnrealEd.ProjectPackagingSettings]` section on the floor**
  doing it (it did, uncommitted, before this work). If a build regresses to "only the main menu
  ships", diff that file first.
- `AdditionalCookerOptions=-ModelContextProtocolPort=8123` is load-bearing: with the editor open,
  the cook commandlet's MCP listener loses the race for port 8000 and logs an `Error`, and one
  Error line is enough for UAT to report `Error_UnknownCookFailure`.
