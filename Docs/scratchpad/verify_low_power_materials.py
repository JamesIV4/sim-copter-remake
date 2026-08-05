"""Checks the Low Power Graphics wiring survived a CreateSimCopterMaterials.py run.

    UnrealEditor-Cmd <uproject> -ExecutePythonScript="<repo>/Docs/scratchpad/verify_low_power_materials.py"

What it answers, in order of what actually goes wrong:
  * MPC_SimCopterDayNight carries the LowPower scalar (without it every material below compiles
    the branch against a constant 0 and the mode does nothing).
  * The terrain and water materials each hold a CollectionParameter node bound to that collection
    AND named LowPower - a node whose Collection is set after its ParameterName silently binds to
    nothing, which is the trap CreateSimCopterMaterials.py's own comment warns about.
  * Their Custom nodes carry the early-out branch.
  * Every material still compiles, reported as its shader instruction counts.
  * The MI_CityPage_* instances are back on M_SimCopterCityAtlas - the atlas material is deleted and
    recreated on every run, which nulls their parent until BakeCityAtlas.py re-sets it.
"""

import unreal

MATERIALS = "/Game/Materials"
COLLECTION = f"{MATERIALS}/MPC_SimCopterDayNight"
BAKED_ATLAS_DIR = "/Game/Generated/CityAtlas"

failures = []


def check(condition, message):
    unreal.log(f"{'PASS' if condition else 'FAIL'}  {message}")
    if not condition:
        failures.append(message)


collection = unreal.EditorAssetLibrary.load_asset(COLLECTION)
check(collection is not None, "MPC_SimCopterDayNight loads")
if collection:
    names = {
        str(p.get_editor_property("parameter_name"))
        for p in collection.get_editor_property("scalar_parameters")
    }
    check("LowPower" in names, f"collection has LowPower (has: {sorted(names)})")


def expressions(path):
    material = unreal.EditorAssetLibrary.load_asset(path)
    if material is None:
        return None, []
    return material, unreal.MaterialEditingLibrary.get_material_expressions(material)


for name, node_names in (
    ("M_SimCopterTerrain", ["TerrainNormalNoise"]),
    ("M_SimCopterWater", ["WaterWPO", "WaterNormal"]),
):
    path = f"{MATERIALS}/{name}"
    material, exprs = expressions(path)
    check(material is not None, f"{name} loads")
    if material is None:
        continue

    bound = [
        e for e in exprs
        if isinstance(e, unreal.MaterialExpressionCollectionParameter)
        and str(e.get_editor_property("parameter_name")) == "LowPower"
        and e.get_editor_property("collection") == collection
    ]
    check(len(bound) == 1, f"{name} has one LowPower collection node bound to the collection")

    for node_name in node_names:
        node = next(
            (e for e in exprs
             if isinstance(e, unreal.MaterialExpressionCustom)
             and str(e.get_editor_property("description")) == node_name),
            None)
        check(node is not None, f"{name}: {node_name} exists")
        if node is None:
            continue
        code = str(node.get_editor_property("code"))
        check("LowPower > 0.5" in code, f"{name}: {node_name} branches on LowPower")
        inputs = [str(i.get_editor_property("input_name")) for i in node.get_editor_property("inputs")]
        check("LowPower" in inputs, f"{name}: {node_name} has a LowPower input ({inputs})")

# The atlas keeps the free daylight early-out but no LowPower branch: lighting every window would
# be worse than the two hashes it saves. See the note in CreateSimCopterMaterials.py.
_atlas, atlas_exprs = expressions(f"{MATERIALS}/M_SimCopterCityAtlas")
mask = next(
    (e for e in atlas_exprs
     if isinstance(e, unreal.MaterialExpressionCustom)
     and str(e.get_editor_property("description")) == "OriginalNightWindowMask"),
    None)
check(mask is not None, "M_SimCopterCityAtlas: OriginalNightWindowMask exists")
if mask is not None:
    check("if (Blend <= 0.0)" in str(mask.get_editor_property("code")),
          "M_SimCopterCityAtlas: window mask exits early in daylight")

# A material that failed to compile reports zero instructions, so this is both "did it build" and a
# record of what the graphs cost.
for name in ("M_SimCopterCityAtlas", "M_SimCopterTerrain", "M_SimCopterWater",
             "M_SimCopterLitVertexColor", "M_SimCopterLitSpriteTexture", "M_SimCopterParticleFX"):
    material = unreal.EditorAssetLibrary.load_asset(f"{MATERIALS}/{name}")
    if material is None:
        check(False, f"{name} loads")
        continue
    stats = unreal.MaterialEditingLibrary.get_statistics(material)
    ps = stats.get_editor_property("num_pixel_shader_instructions")
    vs = stats.get_editor_property("num_vertex_shader_instructions")
    check(ps > 0, f"{name} compiles (pixel {ps} / vertex {vs} instructions)")

# MI_CityPage_* are gitignored decoded art, so this only means anything on a machine that has baked.
if unreal.EditorAssetLibrary.does_directory_exist(BAKED_ATLAS_DIR):
    atlas_parent = unreal.EditorAssetLibrary.load_asset(f"{MATERIALS}/M_SimCopterCityAtlas")
    pages = [p for p in unreal.EditorAssetLibrary.list_assets(BAKED_ATLAS_DIR, recursive=False)
             if p.split("/")[-1].split(".")[0].startswith("MI_CityPage_")]
    orphans = [p for p in pages
               if unreal.EditorAssetLibrary.load_asset(p).get_editor_property("parent") != atlas_parent]
    check(len(pages) > 0 and not orphans,
          f"{len(pages)} MI_CityPage_* instances are on the rebuilt atlas material (orphans: {orphans})")

unreal.log(f"LOW POWER MATERIAL CHECK: {'ALL PASS' if not failures else str(len(failures)) + ' FAILED'}")
for failure in failures:
    unreal.log_error(f"FAILED: {failure}")
