# 2026-08-06 — why the packaged build lost the city and the menu sky

Evidence for `Docs/memory/simcopter-packaged-build.md`. The build examined was
`S:\SimCopter Stuff\Builds\pre-release 0.8\Windows` (Shipping, 2026-08-06 17:09).

## What the staged manifests said

`Manifest_UFSFiles_Win64.txt`, filtered to `^SimCopterRemake/`, listed 111 files. The whole
project's cooked content was:

    SimCopterRemake/Content/MainMenu.umap
    SimCopterRemake/Content/Materials/*.uasset            (9, pulled in via CDO ConstructorHelpers)
    SimCopterRemake/Content/Slate/*.png + *.uasset        (~92)
    SimCopterRemake/Content/Paks/SimCopterRemake-Windows.pak
    ...shader archives, AssetRegistry.bin, Config/*.ini

Not present, in either manifest:

- `Content/CityRender.umap` — the city level. `OpenLevel("/Game/CityRender")` cannot travel.
- `Content/Generated/CityAtlas/*` — 155 baked atlas assets.
- `Content/Generated/Movies/MENUSKY.mp4` — the front-end sky movie.

`Reference/SimCopterOriginalGame` at the package root was hand-copied by the user, not staged;
`../Reference/SimCopterOriginalGame` from `ProjectDir` is what resolved it.

## The backdrop

`render_skycool.py` renders `bmp/skycool.bmp` (400x66, 8-bit) scaled to 1280x720 the way
`SSimCopterMenuCloudBackdrop`'s missing-movie fallback stretches it over the viewport.
`skycool-stretched.png` is the result, and it matches the reported "broken clouds" screenshot
pixel for pixel — including the grey blob low and left. No media decode was involved: the movie
file simply was not there.
