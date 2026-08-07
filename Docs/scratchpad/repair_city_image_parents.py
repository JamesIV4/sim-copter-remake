"""Check (and repair) the parent of every baked MI_CityImage_* instance.

Deleting M_SimCopterLitSpriteTexture to re-tune it NULLS the parent of any instance already pointing
at it, and CreateSimCopterMaterials.py's re-parent pass only rescues instances still on the OLD
unlit parent (M_SimCopterSpriteTexture) - so a delete-and-rebuild leaves those instances parentless
and their cards render as the default material. This puts them back.

  UnrealEditor-Cmd.exe <uproject> -unattended -nop4 -nosplash -stdout `
      -ExecutePythonScript="<repo>\\Docs\\scratchpad\\repair_city_image_parents.py"
"""

import unreal

MATERIAL_DIR = "/Game/Materials"
BAKED_ATLAS_DIR = "/Game/Generated/CityAtlas"

lit_parent = unreal.EditorAssetLibrary.load_asset(f"{MATERIAL_DIR}/M_SimCopterLitSpriteTexture")
if lit_parent is None:
    unreal.log_error("CITY IMAGE PARENTS: M_SimCopterLitSpriteTexture is missing")
elif not unreal.EditorAssetLibrary.does_directory_exist(BAKED_ATLAS_DIR):
    unreal.log(f"CITY IMAGE PARENTS: no {BAKED_ATLAS_DIR} on this machine; nothing to check")
else:
    total = 0
    repaired = []
    already = 0
    for asset_path in unreal.EditorAssetLibrary.list_assets(BAKED_ATLAS_DIR, recursive=False):
        name = asset_path.split("/")[-1].split(".")[0]
        if not name.startswith("MI_CityImage_"):
            continue
        total += 1
        instance = unreal.EditorAssetLibrary.load_asset(asset_path)
        if instance is None:
            continue
        parent = instance.get_editor_property("parent")
        if parent == lit_parent:
            already += 1
            continue
        instance.set_editor_property("parent", lit_parent)
        unreal.EditorAssetLibrary.save_asset(f"{BAKED_ATLAS_DIR}/{name}", only_if_is_dirty=False)
        repaired.append(name)

    unreal.log(
        f"CITY IMAGE PARENTS: {total} instance(s); {already} already lit, {len(repaired)} repaired"
    )
