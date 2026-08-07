"""Editor Python: confirm the SimCopter.Shading.* collection parameters exist and are BOUND.

The failure this catches is silent and nasty: a MaterialExpressionCollectionParameter whose name
does not resolve against its collection compiles to a constant ZERO. A zero roughness is a mirror
and a zero albedo ceiling is a black surface, and neither says why. So this checks both halves -
the scalar exists in MPC_SimCopterDayNight, and each material actually carries a node bound to it.

  UnrealEditor-Cmd.exe <uproject> -unattended -nop4 -nosplash -stdout `
      -ExecutePythonScript="<repo>\\Docs\\scratchpad\\verify_surface_shading.py"
"""

import unreal

COLLECTION = "/Game/Materials/MPC_SimCopterDayNight.MPC_SimCopterDayNight"

# material -> the family whose three scalars it must be reading.
EXPECTED = {
    "/Game/Materials/M_SimCopterLitSpriteTexture.M_SimCopterLitSpriteTexture": "Tree",
    "/Game/Materials/M_SimCopterTerrain.M_SimCopterTerrain": "Terrain",
    "/Game/Materials/M_SimCopterCityAtlas.M_SimCopterCityAtlas": "City",
    "/Game/Materials/M_SimCopterLitTexture.M_SimCopterLitTexture": "City",
    "/Game/Materials/M_SimCopterLitVertexColor.M_SimCopterLitVertexColor": "City",
    "/Game/Materials/M_SimCopterWater.M_SimCopterWater": "Water",
}
SUFFIXES = ("MaxBrightness", "Roughness", "Specular")
FAMILIES = ("Tree", "Terrain", "City", "Water")
# Water alone fades its roughness/specular towards the shore, so it carries five more.
EXTRA = {
    # Only the atlas carries the painted window mask, so only it takes the glass pair.
    "/Game/Materials/M_SimCopterCityAtlas.M_SimCopterCityAtlas": (
        "CityWindowRoughness", "CityWindowSpecular",
    ),
    "/Game/Materials/M_SimCopterWater.M_SimCopterWater": (
        "WaterShoreRoughness", "WaterShoreSpecular", "WaterShoreFadeWidth",
        "WaterShoreEdgeNoiseStrength", "WaterShoreEdgeNoiseScale",
        "WaterShoreEdgeNoiseStrength2", "WaterShoreEdgeNoiseScale2",
        "WaterDetailNormalStrength", "WaterDetailNormalScale",
    ),
}

collection = unreal.EditorAssetLibrary.load_asset(COLLECTION)
if collection is None:
    unreal.log_error("SHADING: MPC_SimCopterDayNight not found")
else:
    declared = {
        str(entry.get_editor_property("parameter_name")): entry.get_editor_property("default_value")
        for entry in collection.get_editor_property("scalar_parameters")
    }
    expected_scalars = [f"{f}{s}" for f in FAMILIES for s in SUFFIXES]
    for extras in EXTRA.values():
        expected_scalars.extend(extras)
    for name in expected_scalars:
        if name in declared:
            unreal.log(f"SHADING: collection has {name} = {declared[name]:.3f}")
        else:
            unreal.log_error(f"SHADING: collection is MISSING {name}")

    for material_path, family in EXPECTED.items():
        material = unreal.EditorAssetLibrary.load_asset(material_path)
        if material is None:
            unreal.log_error(f"SHADING: {material_path} not found")
            continue
        bound = set()
        for expression in unreal.MaterialEditingLibrary.get_material_expressions(material):
            if isinstance(expression, unreal.MaterialExpressionCollectionParameter):
                if expression.get_editor_property("collection") == collection:
                    bound.add(str(expression.get_editor_property("parameter_name")))
        wanted = [f"{family}{s}" for s in SUFFIXES] + list(EXTRA.get(material_path, ()))
        missing = [name for name in wanted if name not in bound]
        if missing:
            unreal.log_error(
                f"SHADING: {material_path.split('.')[-1]} is NOT reading {missing} "
                f"(it binds {sorted(bound)})"
            )
        else:
            unreal.log(
                f"SHADING: {material_path.split('.')[-1]} reads all {len(wanted)} {family} scalars, "
                f"bound to the collection"
            )
