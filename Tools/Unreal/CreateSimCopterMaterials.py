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


# Shared shading parameters. The city is a stylized low-poly remake that still needs to
# read well under dynamic night lighting (street lights, car headlights, the helicopter
# spotlight), so the materials are Default Lit with:
#   * a small SelfIllum floor so shadowed faces stay readable but stay dark enough for
#     dynamic lights to pop at night,
#   * a moderate Roughness so light falloff and speculars are visible without going glossy,
#   * a low Specular so spotlights still glint without looking like wet plastic.
# All three are exposed as scalar parameters so day<->night can be driven at runtime.
SELF_ILLUM_DEFAULT = 0.08
ROUGHNESS_DEFAULT = 0.65
SPECULAR_DEFAULT = 0.3
SHADING_GROUP = "City Shading"


def add_scalar_parameter(material, name, default_value, sort_priority, y):
    param = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionScalarParameter,
        -450,
        y,
    )
    param.set_editor_property("parameter_name", name)
    param.set_editor_property("default_value", default_value)
    param.set_editor_property("group", SHADING_GROUP)
    param.set_editor_property("sort_priority", sort_priority)
    return param


def add_shading_nodes(material, color_expression, color_output):
    """Wires SelfIllum emissive, Roughness, and Specular onto a material whose base
    color is driven by `color_expression`/`color_output`."""
    self_illum = add_scalar_parameter(material, "SelfIllum", SELF_ILLUM_DEFAULT, 0, 280)
    roughness = add_scalar_parameter(material, "Roughness", ROUGHNESS_DEFAULT, 1, 430)
    specular = add_scalar_parameter(material, "Specular", SPECULAR_DEFAULT, 2, 580)

    emissive = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionMultiply,
        -180,
        250,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        color_expression, color_output, emissive, "A"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        self_illum, "", emissive, "B"
    )

    unreal.MaterialEditingLibrary.connect_material_property(
        emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        specular, "", unreal.MaterialProperty.MP_SPECULAR
    )


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

    add_shading_nodes(material, texture, "RGB")

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

    # The VertexColor node's RGB output is its default (unnamed) output pin.
    unreal.MaterialEditingLibrary.connect_material_property(
        vertex_color,
        "",
        unreal.MaterialProperty.MP_BASE_COLOR,
    )

    add_shading_nodes(material, vertex_color, "")

    material.set_editor_property("two_sided", False)
    # See note in create_lit_texture_material: needed for the Nanite render path.
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterLitVertexColor")


ensure_directory(MATERIAL_DIR)
create_lit_texture_material()
create_lit_vertex_color_material()
