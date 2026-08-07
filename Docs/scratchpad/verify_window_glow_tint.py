"""Editor Python: confirm M_SimCopterCityAtlas carries WindowGlowTint and what it defaults to.

The window glow is `nightTexel * mask * WindowGlowNits * WindowGlowTint`, and the tint is the only
part of that which is the remake's rather than the art's - so it is the one worth asserting landed
after a CreateSimCopterMaterials.py + BakeCityAtlas.py pass.

  UnrealEditor-Cmd.exe <uproject> -unattended -nop4 -nosplash -stdout `
      -ExecutePythonScript="<repo>\\Docs\\scratchpad\\verify_window_glow_tint.py"
"""

import unreal

MATERIAL = "/Game/Materials/M_SimCopterCityAtlas.M_SimCopterCityAtlas"

material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
if material is None:
    unreal.log_error("WINDOW TINT: material not found")
else:
    # get_vector_parameter_names returns unreal.Name values, not info structs.
    names = [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(material)]
    unreal.log(f"WINDOW TINT: vector parameters = {sorted(names)}")
    if "WindowGlowTint" in names:
        value = unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(
            material, "WindowGlowTint"
        )
        luminance = 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b
        unreal.log(
            f"WINDOW TINT: WindowGlowTint = ({value.r:.3f}, {value.g:.3f}, {value.b:.3f}) "
            f"luminance {luminance:.3f} (1.0 means the tint changes hue, not exposure)"
        )
    else:
        unreal.log_error("WINDOW TINT: WindowGlowTint is MISSING from the atlas material")
