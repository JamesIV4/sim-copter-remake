import unreal


MATERIAL_DIR = "/Game/Materials"


def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def save(asset_path):
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)


def create_or_load_material(name):
    asset_path = f"{MATERIAL_DIR}/{name}"
    existing = unreal.EditorAssetLibrary.load_asset(asset_path)
    if existing:
        return existing

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.MaterialFactoryNew()
    return asset_tools.create_asset(name, MATERIAL_DIR, unreal.Material, factory)


def clear_expressions(material):
    for expression in unreal.MaterialEditingLibrary.get_material_expressions(material):
        unreal.MaterialEditingLibrary.delete_material_expression(material, expression)


def create_lit_texture_material():
    material = create_or_load_material("M_SimCopterLitTexture")
    clear_expressions(material)

    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionTextureSampleParameter2D,
        -400,
        0,
    )
    texture.set_editor_property("parameter_name", "Texture")

    unreal.MaterialEditingLibrary.connect_material_property(
        texture,
        "RGB",
        unreal.MaterialProperty.MP_BASE_COLOR,
    )

    material.set_editor_property("two_sided", False)
    # The city renderer draws these materials on generated Nanite static meshes,
    # so the usage flag has to be baked in; otherwise the engine patches it at
    # runtime and warns that the asset needs re-saving for cooked builds.
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterLitTexture")


def create_lit_vertex_color_material():
    material = create_or_load_material("M_SimCopterLitVertexColor")
    clear_expressions(material)

    vertex_color = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionVertexColor,
        -400,
        0,
    )

    unreal.MaterialEditingLibrary.connect_material_property(
        vertex_color,
        "RGB",
        unreal.MaterialProperty.MP_BASE_COLOR,
    )

    material.set_editor_property("two_sided", False)
    # See note in create_lit_texture_material: needed for the Nanite render path.
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterLitVertexColor")


ensure_directory(MATERIAL_DIR)
create_lit_texture_material()
create_lit_vertex_color_material()
