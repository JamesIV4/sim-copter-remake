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


def material_exists(name):
    return unreal.EditorAssetLibrary.does_asset_exist(f"{MATERIAL_DIR}/{name}")


def create_if_missing(name, create_func):
    if material_exists(name):
        unreal.log(f"{name} already exists; skipping material rebuild.")
        return
    create_func()


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


def create_city_atlas_material():
    """City building/road material that samples one full atlas page instead of per-cell slices.

    SimCopter mesh faces store an in-cell UV (which repeats outside 0..1) plus a cell index.
    Rather than slicing every 32x32 atlas cell into its own texture at load time, the renderer
    passes the in-cell UV in TexCoord0 and the cell column/row in TexCoord1, and this material
    maps them into one 8x8 page texture:
        pageUV = (TexCoord1 + frac(TexCoord0)) / 8
    frac() reproduces the per-cell wrap addressing; with the page texture set to nearest filter
    and no mips, this samples exactly the cell the original game used, with no bleed between
    neighbouring cells."""
    material = create_or_load_material("M_SimCopterCityAtlas")
    clear_expressions(material)

    in_cell_uv = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureCoordinate, -900, 0
    )
    in_cell_uv.set_editor_property("coordinate_index", 0)

    cell_index = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureCoordinate, -900, 200
    )
    cell_index.set_editor_property("coordinate_index", 1)

    frac = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionFrac, -700, 0
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(in_cell_uv, "", frac, "")

    cell_plus_frac = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAdd, -560, 100
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(cell_index, "", cell_plus_frac, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(frac, "", cell_plus_frac, "B")

    page_uv = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -420, 100
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(cell_plus_frac, "", page_uv, "A")
    page_uv.set_editor_property("const_b", 1.0 / 8.0)

    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -250, 0
    )
    texture.set_editor_property("parameter_name", "Texture")
    unreal.MaterialEditingLibrary.connect_material_expressions(page_uv, "", texture, "UVs")

    unreal.MaterialEditingLibrary.connect_material_property(
        texture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )
    add_shading_nodes(material, texture, "RGB")

    material.set_editor_property("two_sided", False)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterCityAtlas")


def create_rotor_disc_material():
    """Near-translucent grey disc for the spinning rotor blur (Maxis face type 11).

    The original game draws the rotor as a faint translucent disc over the thin opaque
    blades. This is an Unlit + Translucent material so the disc reads as a soft grey haze
    regardless of scene lighting; DiscColor/DiscOpacity are exposed for tuning. It is drawn
    on a ProceduralMeshComponent (not Nanite), so the Nanite usage flag is not required."""
    material = create_or_load_material("M_SimCopterRotorDisc")
    clear_expressions(material)

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    disc_color = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionVectorParameter,
        -400,
        0,
    )
    disc_color.set_editor_property("parameter_name", "DiscColor")
    disc_color.set_editor_property("default_value", unreal.LinearColor(0.3, 0.3, 0.3, 1.0))
    disc_color.set_editor_property("group", SHADING_GROUP)

    disc_opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionScalarParameter,
        -400,
        180,
    )
    disc_opacity.set_editor_property("parameter_name", "DiscOpacity")
    disc_opacity.set_editor_property("default_value", 0.18)
    disc_opacity.set_editor_property("group", SHADING_GROUP)

    unreal.MaterialEditingLibrary.connect_material_property(
        disc_color, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        disc_opacity, "", unreal.MaterialProperty.MP_OPACITY
    )

    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterRotorDisc")


def create_sprite_texture_material():
    """Masked unlit material for original 8-bit population sprites.

    PEOPLE1.BMP is decoded with its cyan chroma key written into texture alpha; the
    material only needs to preserve nearest pixels and drop alpha-zero texels."""
    material = create_or_load_material("M_SimCopterSpriteTexture")
    clear_expressions(material)

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.5)

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
        unreal.MaterialProperty.MP_EMISSIVE_COLOR,
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        texture,
        "A",
        unreal.MaterialProperty.MP_OPACITY_MASK,
    )

    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterSpriteTexture")


def add_custom_node(material, name, code, inputs, x, y, output_type=None):
    """Create a MaterialExpressionCustom with the given named inputs and HLSL body.
    Returns the node; connect sources into it by input name with connect_material_expressions."""
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionCustom, x, y
    )
    node.set_editor_property("description", name)
    node.set_editor_property("code", code)
    if output_type is not None:
        node.set_editor_property("output_type", output_type)
    custom_inputs = []
    for input_name in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", input_name)
        custom_inputs.append(ci)
    node.set_editor_property("inputs", custom_inputs)
    return node


# The undulating sea. Same TILED1 texturing + lit shading as the terrain, but the water tiles are
# displaced vertically in the vertex shader (World Position Offset) instead of on the CPU, so the
# animation is effectively free and runs identically in the editor and in game. A per-vertex weight
# (baked into vertex-color R by the city renderer: 0 = shoreline, 1 = open water) pins the coast so
# the water stays welded to the static land mesh. The surface normal is computed analytically from
# the derivative of the height field, so lighting ripples with the waves per-pixel.
WATER_WAVE_INPUTS = ["WorldPos", "Time", "Weight", "Amplitude", "WaveLength", "Speed"]

WATER_WAVE_PRELUDE = (
    "float K1 = 2.0 * 3.14159265 / max(WaveLength, 1.0);\n"
    "float K2 = K1 * 1.7;\n"
    "float P1 = K1 * (WorldPos.x * 0.7 + WorldPos.y * 0.7) + Time * Speed;\n"
    "float P2 = K2 * (WorldPos.x * 0.3 - WorldPos.y * 0.95) + Time * Speed * 0.8;\n"
)

WATER_WPO_CODE = (
    WATER_WAVE_PRELUDE
    + "float h = Weight * Amplitude * (0.6 * sin(P1) + 0.4 * sin(P2));\n"
    + "return float3(0.0, 0.0, h);"
)

WATER_NORMAL_CODE = (
    WATER_WAVE_PRELUDE
    + "float amp = Weight * Amplitude;\n"
    + "float dHdx = amp * (0.6 * cos(P1) * K1 * 0.7 + 0.4 * cos(P2) * K2 * 0.3);\n"
    + "float dHdy = amp * (0.6 * cos(P1) * K1 * 0.7 + 0.4 * cos(P2) * K2 * (-0.95));\n"
    + "return normalize(float3(-dHdx, -dHdy, 1.0));"
)


def create_water_material():
    material = create_or_load_material("M_SimCopterWater")
    clear_expressions(material)

    # Base color / lit shading, identical to the terrain-low material so the water reads the same.
    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0
    )
    texture.set_editor_property("parameter_name", "Texture")
    unreal.MaterialEditingLibrary.connect_material_property(
        texture, "RGB", unreal.MaterialProperty.MP_BASE_COLOR
    )
    add_shading_nodes(material, texture, "RGB")

    # Shared wave inputs.
    world_pos = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionWorldPosition, -1000, 700
    )
    time = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTime, -1000, 820
    )
    weight = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexColor, -1000, 920
    )
    amplitude = add_scalar_parameter(material, "WaveAmplitude", 28.0, 3, 1040)
    wavelength = add_scalar_parameter(material, "WaveLength", 1100.0, 4, 1140)
    speed = add_scalar_parameter(material, "WaveSpeed", 1.1, 5, 1240)

    def wire_wave_inputs(node):
        unreal.MaterialEditingLibrary.connect_material_expressions(world_pos, "", node, "WorldPos")
        unreal.MaterialEditingLibrary.connect_material_expressions(time, "", node, "Time")
        unreal.MaterialEditingLibrary.connect_material_expressions(weight, "R", node, "Weight")
        unreal.MaterialEditingLibrary.connect_material_expressions(amplitude, "", node, "Amplitude")
        unreal.MaterialEditingLibrary.connect_material_expressions(wavelength, "", node, "WaveLength")
        unreal.MaterialEditingLibrary.connect_material_expressions(speed, "", node, "Speed")

    wpo = add_custom_node(
        material, "WaterWPO", WATER_WPO_CODE, WATER_WAVE_INPUTS, -650, 750,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    wire_wave_inputs(wpo)
    unreal.MaterialEditingLibrary.connect_material_property(
        wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
    )

    normal = add_custom_node(
        material, "WaterNormal", WATER_NORMAL_CODE, WATER_WAVE_INPUTS, -650, 1050,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    wire_wave_inputs(normal)
    unreal.MaterialEditingLibrary.connect_material_property(
        normal, "", unreal.MaterialProperty.MP_NORMAL
    )

    # The analytic normal is world-space, so the Normal input must not be interpreted as tangent-space.
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", False)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterWater")


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
create_if_missing("M_SimCopterLitTexture", create_lit_texture_material)
create_if_missing("M_SimCopterLitVertexColor", create_lit_vertex_color_material)
create_if_missing("M_SimCopterRotorDisc", create_rotor_disc_material)
create_if_missing("M_SimCopterCityAtlas", create_city_atlas_material)
create_if_missing("M_SimCopterSpriteTexture", create_sprite_texture_material)
create_if_missing("M_SimCopterWater", create_water_material)
