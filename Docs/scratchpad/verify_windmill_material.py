"""Report the material properties required by instanced wind-power-plant fan discs.

Run with UnrealEditor-Cmd and -ExecutePythonScript.  PP200's type-11 faces are built into an
InstancedStaticMeshComponent, so M_SimCopterRotorDisc must keep both translucency and the ISM
usage flag in the saved asset; otherwise Unreal substitutes its opaque default material.
"""

import unreal


MATERIAL_PATH = "/Game/Materials/M_SimCopterRotorDisc"

material = unreal.load_asset(MATERIAL_PATH)
if material is None:
    raise RuntimeError(f"Missing {MATERIAL_PATH}")

checks = {
    "blend_mode": material.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_TRANSLUCENT,
    "shading_model": material.get_editor_property("shading_model") == unreal.MaterialShadingModel.MSM_UNLIT,
    "two_sided": material.get_editor_property("two_sided"),
    "used_with_instanced_static_meshes": material.get_editor_property("used_with_instanced_static_meshes"),
}
for name, passed in checks.items():
    unreal.log(f"WINDMILL MATERIAL {name}: {'PASS' if passed else 'FAIL'}")

if not all(checks.values()):
    raise RuntimeError("Windmill material asset is missing required properties")

