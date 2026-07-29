import unreal

# Buildings are drawn by UInstancedStaticMeshComponents. A material without the
# InstancedStaticMeshes usage flag is swapped for the default checkerboard material outside the
# editor (the editor sets missing flags on the fly, which is why this only showed up in -game).
# The flag lives on the base UMaterial, so every instance inherits it.
MATERIALS = [
    "/Game/Materials/M_SimCopterCityAtlas",      # parent of MI_CityPage_* / MI_CityImage_*
    "/Game/Materials/M_SimCopterLitVertexColor",  # palette-coloured building faces
    "/Game/Materials/M_SimCopterLitTexture",      # runtime-texture building faces
    "/Game/Materials/M_SimCopterSpriteTexture",
]

changed = []
for path in MATERIALS:
    mat = unreal.load_asset(path)
    if mat is None:
        print("MISSING %s" % path)
        continue
    if not isinstance(mat, unreal.Material):
        print("NOT A BASE MATERIAL %s (%s)" % (path, type(mat)))
        continue

    before = mat.get_editor_property("used_with_instanced_static_meshes")
    if before:
        print("already set: %s" % path)
        continue

    mat.set_editor_property("used_with_instanced_static_meshes", True)
    changed.append(mat)
    print("set used_with_instanced_static_meshes on %s" % path)

if changed:
    unreal.MaterialEditingLibrary.recompile_material(changed[0]) if False else None
    for mat in changed:
        unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
        print("saved %s" % mat.get_path_name())

print("DONE changed=%d" % len(changed))
