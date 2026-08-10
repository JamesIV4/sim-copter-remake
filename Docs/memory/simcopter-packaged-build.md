# Packaging SimCopter Remake (the Shipping build)

What a `Package Project` build gets wrong that a standalone-from-editor run never shows, decoded
2026-08-06 from `S:\SimCopter Stuff\Builds\pre-release 0.8\Windows`. The faults had the same
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

## 3. Required original game data ships automatically in `<package root>/SimCopter`

Every reader used to carry its own copy-pasted candidate list (one of them hard-coded a developer's
`S:/Repos/...`), so a packaged build found some subsystems' data and not others. That is now one
list in `Formats/SimCopterOriginalGamePaths.h`.

The candidates are relative to `FPaths::ProjectDir()`, which is `SimCopterRemake/` in a checkout
and `<Package>/SimCopterRemake/` when staged - so **`../SimCopter` means the repo root to a
developer and the folder beside `SimCopterRemake.exe` in a package**, one path for both.

UAT refuses to stage a source outside the project or engine root. `SimCopterRemake.Build.cs`
therefore declares the runtime data as source-to-target `RuntimeDependencies` for Game targets:
the source is the repo's gitignored `Reference/SimCopterOriginalGame`, and the target is the
project's gitignored `Intermediate/OriginalGameStaging`. The `[Staging]` remap in
`Config/DefaultGame.ini` then moves the staged target from
`SimCopterRemake/Intermediate/OriginalGameStaging` to package-root `SimCopter`.

Only the six trees the remake reads ship: `bmp`, `cities`, `geo`, `sound`, `tweak`, and `x`
(about 237 MB in the current source install). `tweak` is deliberately filtered to `*.twk` because
that directory also contains the original editor executable. The original `SimCopter.exe`, other
executables/DLLs, manuals, help, saved cities, and Smacker `.smk` files are not runtime inputs and
do not ship. Smacker is not a UE runtime media format; the supported transcoded menu movie is staged
separately from `Content/Generated/Movies`.

The Build.cs rule validates all six source directories plus representative city, bitmap, mesh,
sound, tuning, people and animation files, and fails every Game-target build when one is absent.
Do not soften that to a warning: a package with incomplete city/model/tuning data is not a valid
build. Editor targets skip the 237 MB intermediate copy and continue reading `Reference` directly.

**A candidate is only accepted when it looks like an install** (`IsOriginalGameRoot`: it has a
`cities`/`bmp`/`geo`/`tweak` subfolder). That still matters for custom or damaged packages:
`EnsurePlayerRootFolder` may create an empty package-root folder plus a recovery note, and a plain
`DirectoryExists` check would resolve that empty folder before the developer/reference fallbacks.
The same check gates the *authored* `OriginalGameRoot` on the city/traffic/helicopter/hangar actors,
whose level-saved `../Reference/SimCopterOriginalGame` is a developer path that means nothing in a
package.

## Watch out

- `DirectoriesToAlwaysStageAs*` paths are relative to **`Content/`**, not the project dir, and land
  at `<Staged>/SimCopterRemake/Content/<path>`. `MapsToCook`/`DirectoriesToAlwaysCook` take
  `/Game/...` package paths. Mixing the two conventions up silently stages nothing but a warning.
- Repo-level data cannot be a bare runtime dependency because `DeploymentContext.StageFile`
  rejects files outside the project and engine roots. Keep the Build.cs source-to-Intermediate
  mapping and the `[Staging]` remap together. Verify that the NonUFS manifest contains entries such
  as `SimCopter/cities/career/city0.sc2`, `SimCopter/BMP/SIM3D.BMP`,
  `SimCopter/tweak/career.twk`, and `SimCopter/x/privanim.df` (case follows the source checkout).
- The editor rewrites `Config/DefaultGame.ini` when packaging settings are touched in the UI and
  **has dropped the whole `[/Script/UnrealEd.ProjectPackagingSettings]` section on the floor**
  doing it (it did, uncommitted, before this work). It can also drop `[Staging]`; if a build
  regresses to "only the main menu ships" or nests data under `SimCopterRemake/Intermediate`, diff
  that file first.
- `AdditionalCookerOptions=-ModelContextProtocolPort=8123` is load-bearing: with the editor open,
  the cook commandlet's MCP listener loses the race for port 8000 and logs an `Error`, and one
  Error line is enough for UAT to report `Error_UnknownCookFailure`.
