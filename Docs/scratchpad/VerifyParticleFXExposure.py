"""Read-only check that the unlit effect materials carry the EmissiveNits scale and still compile.

Run headless (the editor must be closed, see AGENTS.md 6):
    UnrealEditor-Cmd.exe SimCopterRemake.uproject -run=pythonscript -script=<this file>

The materials are unlit, so their emissive is an absolute number of nits and has to be on the same
scale as the level's sun or the cards tonemap to black - the bug this checks for. See
Docs/memory/simcopter-exposure-scale.md.
"""
import unreal

for name in ("M_SimCopterParticleFX", "M_SimCopterParticleFXSoft", "M_SimCopterSpriteTexture"):
    material = unreal.EditorAssetLibrary.load_asset(f"/Game/Materials/{name}")
    if material is None:
        unreal.log_error(f"{name}: MISSING")
        continue

    params = [e.get_editor_property("parameter_name")
              for e in unreal.MaterialEditingLibrary.get_material_expressions(material)
              if isinstance(e, unreal.MaterialExpressionScalarParameter)]
    value = unreal.MaterialEditingLibrary.get_material_default_scalar_parameter_value(
        material, "EmissiveNits")
    unreal.log(f"{name}: scalar params={sorted(str(p) for p in params)} EmissiveNits default={value}")
    if value <= 1.0:
        unreal.log_error(f"{name}: EmissiveNits is {value} - the cards will be black in daylight.")

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.log(f"{name}: recompiled")
