"""(Re)imports the bundled full-colour Slate artwork in Content/Slate as UTexture2D assets.

The reconstructions under Content/Slate (the upscaled dashboard, main menu, hangar backdrops, gauge
needles) are modern RGBA PNGs rather than original paletted BMPs, and they are committed - unlike
the decoded original art, which BakeCityAtlas.py writes into a gitignored folder.

USimCopterHangarArt::GetBundledSlateImage loads the PNG off disk at runtime, so a refreshed PNG
takes effect in a development build the moment it is copied in. The .uasset beside it is what a
COOKED build would carry, so it still wants reimporting when the PNG changes, and a new PNG needs
one creating. Doing that by hand is how DHANGAR-upscaled.uasset ended up months older than its PNG.

**Takes the names to import.** A bare run sweeps every PNG in the folder, which rewrites assets that
did not change - and their derived data, and anything hand-tuned on them. Pass the ones you mean:

    py "<repo>/Tools/Unreal/ImportSlateArt.py" NHANGER-upscaled DHANGAR-upscaled
    UnrealEditor-Cmd <uproject> -ExecutePythonScript="<repo>/Tools/Unreal/ImportSlateArt.py NHANGER-upscaled"

Import settings are applied only to assets this script CREATES. An existing asset is reimported with
whatever compression, filter and LOD group it already carries, because that may have been tuned by
hand and a reimport has no business resetting it.
"""

import os
import sys

import unreal

SLATE_DIR = "/Game/Slate"


def slate_content_dir():
    return os.path.normpath(
        os.path.join(
            unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), "Slate"
        )
    )


def import_png(png_path, asset_name):
    asset_path = f"{SLATE_DIR}/{asset_name}"
    existed = unreal.EditorAssetLibrary.does_asset_exist(asset_path)

    task = unreal.AssetImportTask()
    task.filename = png_path
    task.destination_path = SLATE_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    texture = unreal.EditorAssetLibrary.load_asset(asset_path)
    if texture is None:
        unreal.log_error(f"SLATE ART: failed to import {png_path}")
        return None

    if not existed:
        # New asset only. These are UI pages drawn at arbitrary scale into a ScaleBox, so they want
        # bilinear filtering - the opposite of the city atlas pages, whose per-cell UV math needs
        # exact texels.
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)

    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    return "created" if not existed else "reimported"


def main():
    root = slate_content_dir()
    if not os.path.isdir(root):
        unreal.log_error(f"SLATE ART: {root} does not exist")
        return

    available = {
        os.path.splitext(entry)[0]: os.path.join(root, entry)
        for entry in sorted(os.listdir(root))
        if entry.lower().endswith(".png")
    }

    requested = [arg for arg in sys.argv[1:] if not arg.startswith("-")]
    if not requested:
        unreal.log_warning(
            "SLATE ART: no names given, sweeping ALL "
            f"{len(available)} PNGs. This rewrites every asset in Content/Slate."
        )
        requested = sorted(available)

    created, reimported, missing = [], [], []
    for name in requested:
        name = os.path.splitext(name)[0]
        png = available.get(name)
        if png is None:
            missing.append(name)
            continue
        result = import_png(png, name)
        if result == "created":
            created.append(name)
        elif result == "reimported":
            reimported.append(name)

    unreal.log(
        f"SLATE ART IMPORT DONE: created={created} reimported={reimported} missing={missing}"
    )


main()
