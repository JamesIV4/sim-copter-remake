import unreal
for path in ["/Game/Materials/M_SimCopterCityAtlas", "/Game/Materials/M_SimCopterLitVertexColor"]:
    m = unreal.load_asset(path)
    print("%s ISM=%s dirty=%s" % (path, m.get_editor_property("used_with_instanced_static_meshes"), unreal.EditorAssetLibrary.get_metadata_tag(m, "dirty")))
