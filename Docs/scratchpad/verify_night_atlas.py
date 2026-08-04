"""Prints what every MI_CityPage_* actually resolved to, so "the bake said DONE" is not the only
evidence that the night pages got wired up.

    UnrealEditor-Cmd <uproject> -ExecutePythonScript="<repo>/Docs/scratchpad/verify_night_atlas.py"
"""

import unreal

ATLAS_DIR = "/Game/Generated/CityAtlas"
COLLECTION = "/Game/Materials/MPC_SimCopterDayNight"
MATERIAL = "/Game/Materials/M_SimCopterCityAtlas"


def texture_name(mic, parameter):
    value = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mic, parameter)
    return value.get_name() if value else "<none>"


def main():
    collection = unreal.EditorAssetLibrary.load_asset(COLLECTION)
    unreal.log(f"VERIFY collection {COLLECTION}: {'OK' if collection else 'MISSING'}")
    if collection:
        names = [str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property("scalar_parameters")]
        unreal.log(f"VERIFY collection scalars: {names}")

    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    unreal.log(f"VERIFY material {MATERIAL}: {'OK' if material else 'MISSING'}")
    if material:
        params = unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)
        unreal.log(f"VERIFY material scalars: {[str(p) for p in params]}")
        textures = unreal.MaterialEditingLibrary.get_texture_parameter_names(material)
        unreal.log(f"VERIFY material textures: {[str(p) for p in textures]}")

    for asset_path in unreal.EditorAssetLibrary.list_assets(ATLAS_DIR, recursive=False):
        name = asset_path.split("/")[-1].split(".")[0]
        if not name.startswith("MI_CityPage_"):
            continue
        mic = unreal.EditorAssetLibrary.load_asset(asset_path)
        parent = mic.get_editor_property("parent")
        unreal.log(
            f"VERIFY {name}: parent={parent.get_name() if parent else '<NULL>'} "
            f"day={texture_name(mic, 'Texture')} night={texture_name(mic, 'NightTexture')}"
        )


main()
