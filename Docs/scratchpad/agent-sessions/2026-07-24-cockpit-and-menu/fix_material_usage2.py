import unreal

# Buildings render through UInstancedStaticMeshComponents. A base material without the
# InstancedStaticMeshes usage flag is replaced by the default checkerboard material outside the
# editor, so the flag has to be saved on the asset (and its shader map recompiled for the ISM
# vertex factory). set_editor_property alone does not stick: UMaterial only picks the change up
# through PostEditChange, which is also what queues the recompile.
MATERIALS = [
    "/Game/Materials/M_SimCopterCityAtlas",
    "/Game/Materials/M_SimCopterLitVertexColor",
    "/Game/Materials/M_SimCopterLitTexture",
    "/Game/Materials/M_SimCopterSpriteTexture",
]

for path in MATERIALS:
    mat = unreal.load_asset(path)
    if mat is None:
        print("MISSING %s" % path)
        continue

    mat.modify(True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    # RecompileMaterial runs PreEditChange/PostEditChange for us, which is what makes the flag
    # stick and rebuilds the shader map for the instanced vertex factory.
    unreal.MaterialEditingLibrary.recompile_material(mat)
    saved = unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    print("RESULT %s ISM=%s saved=%s" % (
        path, mat.get_editor_property("used_with_instanced_static_meshes"), saved))

print("DONE")
