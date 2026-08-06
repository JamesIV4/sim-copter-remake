"""Editor-side check that the painted window masks actually reached the shader.

Run AFTER CreateSimCopterMaterials.py and BakeCityAtlas.py:

    UnrealEditor-Cmd <uproject> -run=pythonscript
        -script="Docs/scratchpad/agent-sessions/2026-08-06-night-windows/verify_window_wiring.py"

Reports, for the atlas material and every MI_CityPage_*, whether the material compiled, which pages
carry a painted mask, and what the mask texture's import settings ended up as - srgb=False and
nearest/no-mips are what make `Authored.gb * 255.0` recover the painter's exact bytes.
"""

import unreal

ATLAS = "/Game/Materials/M_SimCopterCityAtlas"
BAKED = "/Game/Generated/CityAtlas"
PAGES = (2, 13, 20, 39, 40)
WALL_PAGES = (2, 39, 40)


def main():
    material = unreal.EditorAssetLibrary.load_asset(ATLAS)
    if material is None:
        unreal.log_error("M_SimCopterCityAtlas is missing - run CreateSimCopterMaterials.py first.")
        return

    stats = unreal.MaterialEditingLibrary.get_statistics(material)
    unreal.log(
        f"WINDOWCHECK material: pixel={stats.num_pixel_shader_instructions} "
        f"vertex={stats.num_vertex_shader_instructions} samplers={stats.num_samplers}"
    )

    failures = []
    for page in PAGES:
        instance = unreal.EditorAssetLibrary.load_asset(f"{BAKED}/MI_CityPage_{page}")
        if instance is None:
            unreal.log_warning(f"WINDOWCHECK page {page}: no MI_CityPage_{page}")
            continue

        has_mask = unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(
            instance, "HasWindowMask"
        )
        mask = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            instance, "WindowTexture"
        )
        night = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(
            instance, "NightTexture"
        )
        mask_name = mask.get_name() if mask else "<parent default>"
        night_name = night.get_name() if night else "<parent default>"
        detail = ""
        if mask:
            detail = (
                f" srgb={mask.get_editor_property('srgb')}"
                f" filter={mask.get_editor_property('filter')}"
                f" mips={mask.get_editor_property('mip_gen_settings')}"
                f" size={mask.blueprint_get_size_x()}x{mask.blueprint_get_size_y()}"
            )
        unreal.log(
            f"WINDOWCHECK page {page:>2}: HasWindowMask={has_mask} mask={mask_name} "
            f"night={night_name}{detail}"
        )

        if page in WALL_PAGES:
            if has_mask < 0.5 or mask is None or "T_CityWindowPage_" not in mask_name:
                failures.append(f"page {page} has no painted mask bound")
            elif mask.get_editor_property("srgb"):
                failures.append(f"page {page} mask imported sRGB - the G/B bytes will not survive")
        elif has_mask >= 0.5:
            failures.append(f"page {page} is a terrain page but claims a window mask")

    unreal.log("WINDOWCHECK RESULT: " + ("OK" if not failures else "; ".join(failures)))


main()
