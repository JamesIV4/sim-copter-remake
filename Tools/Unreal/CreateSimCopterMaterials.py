import unreal


MATERIAL_DIR = "/Game/Materials"
# Where BakeCityAtlas.py writes the decoded original art and its material instances.
BAKED_ATLAS_DIR = "/Game/Generated/CityAtlas"


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


def ensure_instanced_static_mesh_usage(name):
    """Persist the ISM shader permutation without rebuilding a hand-tuned material graph."""
    asset_path = f"{MATERIAL_DIR}/{name}"
    material = unreal.EditorAssetLibrary.load_asset(asset_path)
    if material is None:
        raise RuntimeError(f"Cannot set instanced-static-mesh usage on missing {asset_path}")
    if material.get_editor_property("used_with_instanced_static_meshes"):
        return

    material.modify(True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    # RecompileMaterial supplies the PostEditChange that makes the usage flag stick and builds the
    # shader map for the instanced vertex factory; setting the Python property alone is not enough.
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)


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


# --- per-family surface shading -----------------------------------------------------------------
#
# The three shared numbers above are a reasonable average for buildings and roads, and they are
# WRONG for the three surfaces that read as material rather than as architecture:
#
#   * Trees blew out to white in bright sun. Two causes stack. The palette texel can be near 1.0
#     albedo, which under a physically scaled 120,000-lux sun tonemaps to white all by itself; and
#     the sprite card's normal is biased to world up (CardNormalUpBias), so at noon EVERY card in
#     the city faces the sun at once and the 0.3 specular puts one broad highlight across the whole
#     canopy. Real foliage is dark and almost non-specular and never does either.
#   * The ground was too bright and too shiny for the same albedo reason plus the same 0.3.
#   * Water was not shiny ENOUGH, because it was sharing the dirt's matte numbers.
#
# So those three take their roughness, specular and an ALBEDO CEILING from the day/night collection
# instead of the shared constants, published live by USimCopterDayNightSubsystem::PublishSurfaceShading
# off the SimCopter.Shading.* console variables. Live because these are look knobs: they want to be
# turned while looking at the city at noon, not edited here and re-baked.
#
# The ceiling is on ALBEDO because a material cannot see its own lit result - there is nothing to
# clamp after the lighting. Bounding albedo bounds the diffuse, which is what "too bright" is.
#
# **These names must match SurfaceShadingParameterNames in SimCopterDayNight.cpp.** A collection
# parameter whose name does not resolve compiles to a constant zero, and a zero albedo ceiling is a
# black surface, not an obviously-broken one.
SURFACE_SHADING_FAMILIES = ("Tree", "Terrain", "City", "Water")

# Defaults for the collection itself. Only what the material shows with no subsystem running; the
# real values come from SimCopterDayNight.h. Keep the two in step.
SURFACE_SHADING_DEFAULTS = (
    ("TreeMaxBrightness", 0.42),
    ("TreeRoughness", 0.92),
    ("TreeSpecular", 0.02),
    ("TerrainMaxBrightness", 0.55),
    ("TerrainRoughness", 0.88),
    ("TerrainSpecular", 0.04),
    # Buildings and roads. Started at the terrain's exact numbers on purpose - the ground was tuned
    # first and looked right, and the city standing out beside it was the whole complaint - but they
    # are their own family so painted concrete and asphalt can be pulled apart from dirt later.
    ("CityMaxBrightness", 0.55),
    ("CityRoughness", 0.88),
    ("CitySpecular", 0.04),
    # Glass, where the painted window mask says there is a pane. Only the atlas material has the
    # mask, so only it uses these; the rest of the city stays on the matte pair above.
    ("CityWindowRoughness", 0.10),
    ("CityWindowSpecular", 0.85),
    ("WaterMaxBrightness", 0.75),
    ("WaterRoughness", 0.12),
    ("WaterSpecular", 0.9),
    # Shoreline fade. Matte enough to read as wet sand rather than as a mirror ending, faded across
    # a bit over half the wave-weight ramp so it has no visible start of its own.
    ("WaterShoreRoughness", 0.72),
    ("WaterShoreSpecular", 0.12),
    ("WaterShoreFadeWidth", 0.4),
    # World-space warp on the fade's contour, so it stops tracing the tile grid. Strength is in
    # weight units; scale is a wavelength in cm. Both layers ended up FAR BELOW the 400 cm tile on
    # screen, which dissolves the boundary rather than moving it - see the note in
    # SimCopterDayNight.h, the tile-scale reasoning was tested and lost.
    ("WaterShoreEdgeNoiseStrength", 0.25),
    ("WaterShoreEdgeNoiseScale", 25.0),
    # Second, independent layer - its own amplitude AND wavelength, not an octave of the first.
    ("WaterShoreEdgeNoiseStrength2", 0.2),
    ("WaterShoreEdgeNoiseScale2", 4.0),
    # Noise FRAMES PER SECOND for each layer - the field is re-rolled at this rate and smoothly
    # blended between rolls, so it churns in place instead of scrolling. 0 freezes a layer.
    ("WaterShoreEdgeNoiseSpeed", 0.8),
    ("WaterShoreEdgeNoiseSpeed2", 3.0),
    # Fine normal-only ripple that keeps open water off being one flat mirror. Wavelength in cm.
    ("WaterDetailNormalStrength", 0.35),
    ("WaterDetailNormalScale", 140.0),
)

# Hue preserving, which is the whole point of doing this with a custom node rather than a Min: a
# per-channel clamp walks a bright colour towards the ceiling GREY, so a vivid green tree would go
# pale instead of merely darker. Scaling by the brightest channel keeps the hue and only takes the
# brightness out.
ALBEDO_CEILING_CODE = (
    "float Brightest = max(Color.r, max(Color.g, Color.b));\n"
    "float Limit = max(Ceiling, 0.0);\n"
    "if (Brightest <= Limit || Brightest <= 0.0001f) { return Color; }\n"
    "return Color * (Limit / Brightest);"
)


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


def add_shading_nodes(material, color_expression, color_output, connect_emissive=True):
    """Wires SelfIllum emissive, Roughness, and Specular onto a material whose base
    color is driven by `color_expression`/`color_output`.

    Returns the (self_illum_multiply, self_illum_param) pair. Pass connect_emissive=False when the
    caller has something else to add to Emissive - the city atlas adds its night window glow - and
    wire the returned multiply into that sum instead."""
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

    if connect_emissive:
        unreal.MaterialEditingLibrary.connect_material_property(
            emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        )
    unreal.MaterialEditingLibrary.connect_material_property(
        roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        specular, "", unreal.MaterialProperty.MP_SPECULAR
    )
    return emissive, self_illum


# Every UNLIT surface in the remake is a flat palette colour written straight into Emissive,
# because that is literally what the original did: the software renderer stamped the palette entry
# into the frame buffer and there was no lighting model to consult. Emissive is a number of nits,
# though, and a fixed number of nits is only a fixed *brightness* while the exposure holds still.
#
# It stopped holding still. The level's CelestialVaultDaySequenceActor runs a physically scaled sun
# at 120,000 lux; the day/night actor it replaced ("New TOD system", 2026-08-03) ran it at 4 lux
# (SimCopterDayNightCycleActor::SunIntensityDay). Auto exposure follows the sun, so the exposure now
# sits ~30,000x higher than everything here was tuned against, and an emissive of 1.4 tonemaps to
# BLACK next to a road at ~23,000 nits. That is what turned the fire, the water spray, the dust, the
# tear gas and the blinking marker cards into black quads.
#
# So the emissive has to be a real number of nits, on the same scale as the sun lighting the city.
# `EmissiveNits` is that number, and `USimCopterEffectExposureSubsystem` writes it every frame from
# the level's actual key light: a card sits EffectBrightness times as bright as white ground under
# the same sun, which holds at noon, at dusk and at midnight without anything being retuned.
#
# **The obvious fix does not work here.** A `MaterialExpressionEyeAdaptationInverse` divides out the
# exposure in the shader and would need no C++ at all - it is what this was written with first. It
# had no effect whatsoever in game (verified: the rebuilt material was live, the material compiled
# clean, the cards stayed black), and the node is documented as experimental access to the eye
# adaptation RT for post process materials. Do not "simplify" back to it without testing on screen.
#
# The lights are the same problem with a different fix: `ULocalLightComponent::InverseExposureBlend`
# reads the exposure on the RENDERER side, not out of a shader buffer, and that one does work.
# See USimCopterFlashingLightsComponent.
EMISSIVE_NITS_DEFAULT = 26000.0


def add_scene_scaled_emissive(material, color_expression, color_output, x, y):
    """Wire `color_expression * EmissiveNits` into Emissive.

    Returns the EmissiveNits parameter. The default alone is a sane sunlit value, so the cards read
    correctly even with nothing driving the parameter (the material preview, or a level with no day
    sequence); the subsystem only makes it track the sun."""
    nits = add_scalar_parameter(material, "EmissiveNits", EMISSIVE_NITS_DEFAULT, 4, y + 200)
    scaled = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, x, y
    )
    # Each of these returns False rather than raising when a pin name is wrong, and a dropped
    # emissive connection looks exactly like the bug this is here to fix, so they are checked.
    connections = (
        ("A", unreal.MaterialEditingLibrary.connect_material_expressions(
            color_expression, color_output, scaled, "A")),
        ("B", unreal.MaterialEditingLibrary.connect_material_expressions(nits, "", scaled, "B")),
        ("EmissiveColor", unreal.MaterialEditingLibrary.connect_material_property(
            scaled, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)),
    )
    failed = [name for name, ok in connections if not ok]
    if failed:
        unreal.log_error(
            f"{material.get_name()}: EmissiveNits pins not connected: {', '.join(failed)}. "
            "Emissive will render BLACK."
        )
    else:
        unreal.log(f"{material.get_name()}: emissive scaled by EmissiveNits.")
    return nits


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

    # City family - this carries the direct-image building faces and the hangar shell, which are
    # building surface like any other and have to sit under the same ceiling as the atlas pages.
    add_family_shading_nodes(material, texture, "RGB", "City")

    material.set_editor_property("two_sided", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    # The city renderer draws these materials on generated Nanite static meshes,
    # so the usage flag has to be baked in; otherwise the engine patches it at
    # runtime and warns that the asset needs re-saving for cooked builds.
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterLitTexture")


# Where USimCopterDayNightSubsystem publishes the night blend every frame. A collection scalar,
# because MI_CityPage_* are MaterialInstanceConstants and cannot be animated at runtime at all.
DAY_NIGHT_COLLECTION = f"{MATERIAL_DIR}/MPC_SimCopterDayNight"

# Everything USimCopterDayNightSubsystem drives per frame. Defaults here are only what the material
# shows with no subsystem running (the material preview, a level with no day sequence); the real
# values come from SimCopterDayNight.h and the SimCopter.NightWindows.* console variables.
DAY_NIGHT_COLLECTION_SCALARS = (
    ("NightBlend", 0.0),
    # Re-rolled at every sunset. The material hashes it per window, so one float reshuffles the
    # whole skyline with no geometry work.
    ("WindowSeed", 1.0),
    ("WindowLitFraction", 0.30),
    ("WindowRowLitFraction", 0.05),
    # NOT scaled to the sun the way the unlit effect cards are: a window has a bulb behind it, so
    # its brightness is absolute. But it is metered against a NIGHT exposure, which is why this is
    # tens of nits and not thousands - the first pass at 2500 bloomed the skyline into one halo.
    ("WindowGlowNits", 25.0),
    # 1 while the Settings screen's Low Power Graphics is on. The terrain drops its four octaves of
    # value noise and the water stops displacing; both are per-pixel work over most of the screen and
    # both are the kind of detail a 1996 software renderer never had anyway. Published by
    # USimCopterDayNightSubsystem::PublishLowPower.
    ("LowPower", 0.0),
) + SURFACE_SHADING_DEFAULTS
NIGHT_BLEND_PARAMETER = "NightBlend"

# How far BaseColor leans onto the night art. See the long note at the blend itself: the night pages
# are already-darkened *images* from a renderer with no lighting model, so a full lerp darkens the
# city twice. 1.0 reproduces the original's exact night pixels if that trade is wanted.
NIGHT_ALBEDO_STRENGTH_DEFAULT = 0.25

# --- authored window masks ---------------------------------------------------------------------
#
# The three wall pages (2, 39, 40) are now HAND PAINTED - SimCopterRemake/Content/NightWindows/
# windows_page_<page>.png, drawn in Tools/WindowLayoutEditor.html and imported by BakeCityAtlas.py.
# R marks a lit-window texel, G is a per-window byte and B a per-row byte, both from connected-
# component labelling of the painted shapes, so a "window" is whatever was drawn and a "row" is one
# floor of that atlas cell.
#
# The derived mask below stays, and is not dead code: it is what a page with no painted file falls
# back to, and it is what made the feature possible before there was a painter. `HasWindowMask` (0
# on the parent, 1 on an instance the bake found a file for) picks between them.
WINDOW_MASK_PARAMETER = "WindowTexture"
HAS_WINDOW_MASK_PARAMETER = "HasWindowMask"

# Luminance a night texel has to clear before it counts as a lit window rather than a dark wall.
# Measured, not guessed: at 0.55 this picks 7-16% of the three wall pages (2, 39, 40) and *nothing*
# at all from the two terrain pages (13, 20), whose night art is uniformly darkened with no bright
# texels in it. See Docs/scratchpad/analyse_night_windows.py. Only the fallback path uses it.
WINDOW_GLOW_THRESHOLD_DEFAULT = 0.55

# How hard the mask's edges are. 8 gives roughly an eighth of a luminance unit of ramp, enough to
# stop the 8-bit palette steps banding into a hard cutout.
WINDOW_GLOW_CONTRAST_DEFAULT = 8.0

# --- window glow tint ---------------------------------------------------------------------------
#
# The glow below is literally the night texel times the brightness, which is why a window keeps the
# colour it was painted. The catch is that several of skydark.bmp's lit texels are painted at or
# near white - a 256-entry VGA palette shared with the whole city has no headroom to spend on warm
# bulbs - and a white texel gives a white window. A lit window at night is tungsten, so the whole
# set is pushed amber here while the art's per-window variation survives: a warm texel gets warmer,
# a white one stops reading as a fluorescent panel.
#
# NORMALISED TO LUMINANCE 1 (Rec.709: 0.2126 R + 0.7152 G + 0.0722 B), so this changes the hue and
# not the exposure. The chroma it stands for is (1.0, 0.78, 0.45); dividing by that colour's 0.803
# luminance is where the >1 red comes from. Re-normalise if you re-tune it, or WindowGlowNits stops
# meaning what its comment says.
WINDOW_GLOW_TINT_PARAMETER = "WindowGlowTint"
WINDOW_GLOW_TINT_DEFAULT = (1.25, 0.97, 0.56)

# How finely the random-lit roll is diced, per repeat of the atlas cell across a wall.
#
# The night art paints several windows into one 32x32 cell, and the cell tiles along the face - so
# floor(InCellUV) is already "which bay of the building am I on", and subdividing that by this gives
# roughly one hash bucket per window. It does not have to be exact: the glow mask has ALREADY
# restricted the effect to window texels, so a bucket landing across two windows just lights both,
# which reads as one flat with the lights on. Too FINE is the failure that looks wrong - buckets
# smaller than a window speckle a single pane half-lit.
WINDOW_RANDOM_GRID_DEFAULT = 2.0

# NO DISTANCE FADE ON THE WINDOW MASK. An earlier attempt blended the mask toward its statistical
# average as the texel-to-pixel ratio climbed, on the theory that the flicker was minification
# aliasing. It stopped the flicker and looked far worse: the average was applied to EVERY texel of a
# building, walls included, so a distant tower emitted uniformly instead of showing lit windows -
# the whole skyline came up bright. Reverted 2026-08-05. If the flicker is worth attacking again,
# whatever the cure is must keep the emissive confined to window texels; the cause is more likely
# geometric (LOD/cluster swaps changing the interpolated UV) than a sampling average.


def create_day_night_parameter_collection():
    """The one scalar the city materials read to know what time it is.

    SCHOOK: FUN_004606d0 0x004606d0. The original had no blend - it loaded sky.bmp or skydark.bmp
    and memcpy'd their pages straight over the live atlas, so the whole city flipped between day and
    night art in a single frame. The remake's sun moves continuously, so the same two page sets are
    cross-faded instead, and this is the fade position."""
    if unreal.EditorAssetLibrary.does_asset_exist(DAY_NIGHT_COLLECTION):
        collection = unreal.EditorAssetLibrary.load_asset(DAY_NIGHT_COLLECTION)
    else:
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MPC_SimCopterDayNight",
            MATERIAL_DIR,
            unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew(),
        )

    existing = list(collection.get_editor_property("scalar_parameters"))
    present = {str(p.get_editor_property("parameter_name")) for p in existing}
    wanted = dict(DAY_NIGHT_COLLECTION_SCALARS)
    changed = False

    # Refresh the defaults of parameters that already exist, IN PLACE. Retuning a value here used to
    # do nothing to an asset that already had the parameter, so the collection drifted away from
    # this file and from SimCopterDayNight.h and read as a stale third opinion. Only the value is
    # touched - the Guid is what materials bind to and it stays on the existing struct.
    for parameter in existing:
        name = str(parameter.get_editor_property("parameter_name"))
        default = wanted.get(name)
        if default is None:
            continue
        if abs(parameter.get_editor_property("default_value") - default) > 1e-6:
            parameter.set_editor_property("default_value", default)
            changed = True

    for name, default in DAY_NIGHT_COLLECTION_SCALARS:
        if name in present:
            continue
        # FCollectionParameterBase's constructor mints the Guid, so a default-constructed struct is
        # already uniquely identified - which is what materials bind to, not the name. Existing
        # parameters keep theirs above, or every material referencing them would have to be
        # re-pointed.
        parameter = unreal.CollectionScalarParameter()
        parameter.set_editor_property("parameter_name", name)
        parameter.set_editor_property("default_value", default)
        existing.append(parameter)
        changed = True

    if changed:
        collection.set_editor_property("scalar_parameters", existing)

    save(DAY_NIGHT_COLLECTION)
    return collection


def add_family_shading_nodes(
    material,
    color_expression,
    color_output,
    family,
    connect_roughness_specular=True,
    connect_emissive=True,
):
    """Shading for one of SURFACE_SHADING_FAMILIES.

    Same shape as add_shading_nodes, except the roughness and specular come from the day/night
    collection rather than from the shared constants, and the base colour goes through the family's
    albedo ceiling on the way to MP_BASE_COLOR. Connects BaseColor itself - callers must NOT also
    wire the raw texture into it, or the clamp is bypassed.

    Pass connect_emissive=False when the caller has something else to add to Emissive (the city
    atlas adds its night window glow) and sum the returned emissive node into that instead;
    connect_roughness_specular=False when it wants to blend the two before the pins (the water
    fades them out towards the shore).

    Returns (clamped_color, emissive, roughness, specular).
    """
    assert family in SURFACE_SHADING_FAMILIES, family

    ceiling = add_collection_parameter(material, f"{family}MaxBrightness", -700, 120)
    clamped = add_custom_node(
        material,
        f"{family}AlbedoCeiling",
        ALBEDO_CEILING_CODE,
        ["Color", "Ceiling"],
        -520,
        60,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(color_expression, color_output, clamped, "Color")
    unreal.MaterialEditingLibrary.connect_material_expressions(ceiling, "", clamped, "Ceiling")
    unreal.MaterialEditingLibrary.connect_material_property(
        clamped, "", unreal.MaterialProperty.MP_BASE_COLOR
    )

    # The self-illum floor stays a per-material constant: it is a readability floor for shadowed
    # faces, not a look knob, and it is shared with every other city surface.
    self_illum = add_scalar_parameter(material, "SelfIllum", SELF_ILLUM_DEFAULT, 0, 280)
    emissive = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -180, 250
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(clamped, "", emissive, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(self_illum, "", emissive, "B")
    if connect_emissive:
        unreal.MaterialEditingLibrary.connect_material_property(
            emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
        )

    roughness = add_collection_parameter(material, f"{family}Roughness", -700, 430)
    specular = add_collection_parameter(material, f"{family}Specular", -700, 580)
    # The water blends these against a matte shoreline pair before they reach the pins, so it takes
    # the nodes and wires them itself.
    if connect_roughness_specular:
        unreal.MaterialEditingLibrary.connect_material_property(
            roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
        )
        unreal.MaterialEditingLibrary.connect_material_property(
            specular, "", unreal.MaterialProperty.MP_SPECULAR
        )
    return clamped, emissive, roughness, specular


def add_collection_parameter(material, parameter_name, x, y):
    """A CollectionParameter node reading one scalar out of MPC_SimCopterDayNight."""
    collection = unreal.EditorAssetLibrary.load_asset(DAY_NIGHT_COLLECTION)
    node = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionCollectionParameter, x, y
    )
    # Collection FIRST: UMaterialExpressionCollectionParameter::PostEditChangeProperty resolves
    # ParameterId from ParameterName *through* Collection, so setting the name against a null
    # collection leaves the node bound to nothing and the material compiles to a constant 0.
    node.set_editor_property("collection", collection)
    node.set_editor_property("parameter_name", parameter_name)
    return node


def create_city_atlas_material():
    """City building/road material that samples one full atlas page instead of per-cell slices.

    SimCopter mesh faces store an in-cell UV (which repeats outside 0..1) plus a cell index.
    Rather than slicing every 32x32 atlas cell into its own texture at load time, the renderer
    passes the in-cell UV in TexCoord0 and the cell column/row in TexCoord1, and this material
    maps them into one 8x8 page texture. Mesh-page water remains on its authored atlas cell; only
    the dedicated terrain-water material below runs the five-frame texture animation.
    frac() reproduces the per-cell wrap addressing; with the page texture set to nearest filter
    and no mips, this samples exactly the cell the original game used, with no bleed between
    neighbouring cells.

    NIGHT. The original swapped five atlas pages for their skydark.bmp equivalents after dark
    (FUN_004606d0 copies skydark images 1..5 over live pages 2, 39, 40, 20 and 13), and those night
    pages are where the lit building windows are painted. Here both pages are sampled and blended by
    NightBlend, and the window texels are additionally pushed into Emissive so they read as lights
    rather than as yellow paint. Which texels those are comes from the hand-painted
    Content/NightWindows/windows_page_<page>.png where one exists, and is inferred from the art
    otherwise."""
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

    # The legacy page-20 flag now identifies pool/pond cells only for the constant render lift
    # below. It deliberately has no effect on their texture UVs.
    animate_water = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -900, 400
    )
    animate_water.set_editor_property("parameter_name", "AnimateWaterCells")
    animate_water.set_editor_property("default_value", 0.0)
    animate_water.set_editor_property("group", "Mesh Water")

    page_uv = add_custom_node(
        material,
        "OriginalMeshTextureUV",
        "return (CellIndex + frac(InCellUV)) / 8.0;",
        ["InCellUV", "CellIndex"],
        -560,
        0,
        unreal.CustomMaterialOutputType.CMOT_FLOAT2,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(in_cell_uv, "", page_uv, "InCellUV")
    unreal.MaterialEditingLibrary.connect_material_expressions(cell_index, "", page_uv, "CellIndex")

    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -260, -20
    )
    texture.set_editor_property("parameter_name", "Texture")
    unreal.MaterialEditingLibrary.connect_material_expressions(page_uv, "", texture, "UVs")

    # The skydark.bmp page for this atlas slot. BakeCityAtlas.py sets it on every MI_CityPage_*,
    # falling back to the day texture for any page without a night variant, so the blend below needs
    # no per-page special case.
    night_texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -260, 240
    )
    night_texture.set_editor_property("parameter_name", "NightTexture")
    unreal.MaterialEditingLibrary.connect_material_expressions(page_uv, "", night_texture, "UVs")

    night_blend = add_collection_parameter(material, "NightBlend", -560, 480)

    # BaseColor only leans PART of the way onto the night art, and that is deliberate.
    #
    # The original had no lighting model - a texel was stamped into the frame buffer as authored -
    # so skydark's pages ARE the night image, walls already darkened. Here they are albedo under a
    # physically scaled sun that has already gone below the horizon, so lerping all the way to them
    # darkens the city twice and the buildings go to mud. Albedo is a property of the paint, not of
    # the hour; what should change after dark is the light, and the light already does.
    #
    # A little of it is still worth having: skydark is cooler and greyer as well as darker, and that
    # tint reads as night. 0.25 picks that up without the double darkening. 1.0 reproduces the
    # original's exact night pixels if that is what is wanted - at the cost above.
    albedo_strength = add_scalar_parameter(
        material, "NightAlbedoStrength", NIGHT_ALBEDO_STRENGTH_DEFAULT, 8, 1450
    )
    albedo_blend = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -320, 480
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(night_blend, "", albedo_blend, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(albedo_strength, "", albedo_blend, "B")

    blended = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, -60, 40
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(texture, "RGB", blended, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(night_texture, "RGB", blended, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(albedo_blend, "", blended, "Alpha")

    # City family: buildings and roads. The albedo ceiling goes on the DAY/NIGHT-BLENDED colour, not
    # on the day page, so the clamp is on what actually reaches BaseColor at any hour. Roughness and
    # specular are wired further down instead of here, because the windows take their own.
    _clamped, self_illum_emissive, city_roughness, city_specular = add_family_shading_nodes(
        material, blended, "", "City", connect_roughness_specular=False, connect_emissive=False
    )

    # --- night window glow -------------------------------------------------------------------
    #
    # Which texels are windows is not recorded anywhere in the original data, so there are two ways
    # to know, and the material carries both. The painted mask (WindowTexture, from
    # Tools/WindowLayoutEditor.html) is exact and knows where one window ends and the next begins;
    # the derived one below infers it from the art and is what an unpainted page falls back to.
    window_mask_texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -260, 500
    )
    window_mask_texture.set_editor_property("parameter_name", WINDOW_MASK_PARAMETER)
    unreal.MaterialEditingLibrary.connect_material_expressions(page_uv, "", window_mask_texture, "UVs")

    # Left on the DEFAULT (Color) sampler type on purpose, even though the mask is data rather than
    # colour. The two lookups are byte-identical - ProcessMaterialColorTextureLookup and
    # ProcessMaterialLinearColorTextureLookup both `return TextureValue` - so the only thing the type
    # changes is which default texture the PARENT validates against, and the parent's default is the
    # engine's sRGB grey checker. Declaring Linear Color here fails that check and breaks the whole
    # material for the sake of no shader difference at all. What actually matters is on the texture:
    # BakeCityAtlas.py imports these with srgb=False and nearest/no-mips, so G and B arrive as the
    # exact bytes the painter wrote.
    has_window_mask = add_scalar_parameter(material, HAS_WINDOW_MASK_PARAMETER, 0.0, 3, 880)

    glow_threshold = add_scalar_parameter(
        material, "WindowGlowThreshold", WINDOW_GLOW_THRESHOLD_DEFAULT, 5, 1000
    )
    glow_contrast = add_scalar_parameter(
        material, "WindowGlowContrast", WINDOW_GLOW_CONTRAST_DEFAULT, 6, 1150
    )
    random_grid = add_scalar_parameter(
        material, "WindowRandomGrid", WINDOW_RANDOM_GRID_DEFAULT, 9, 1600
    )

    # Live, so the brightness knob and the nightly re-roll cost one collection write between them
    # rather than touching every MI_CityPage_*.
    glow_nits = add_collection_parameter(material, "WindowGlowNits", -560, 620)
    window_seed = add_collection_parameter(material, "WindowSeed", -560, 700)
    lit_fraction = add_collection_parameter(material, "WindowLitFraction", -560, 780)
    row_lit_fraction = add_collection_parameter(material, "WindowRowLitFraction", -560, 860)

    # ObjectPositionWS separates one building from the next: the city places each model as its own
    # runtime static mesh instance, so this is per-building and two identical towers do not light
    # identically.
    object_position = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionObjectPositionWS, -560, 940
    )

    window_mask = add_custom_node(
        material,
        "OriginalNightWindowMask",
        (
            "// Withheld from Lumen. The surface cache captures Emissive through this very material,\n"
            "// and the window glow is the worst possible thing to hand it: a few bright texels per\n"
            "// building, re-captured at whatever card resolution the distance happens to allow, so\n"
            "// the indirect contribution pulses as cards change resolution or get evicted - which is\n"
            "// the distant flicker. LUMEN_CARD_CAPTURE is defined at the top of LumenCardPixelShader\n"
            "// BEFORE it includes the generated material, so it reaches this node; the base pass has\n"
            "// it undefined and draws the windows exactly as before. Net effect: the windows look\n"
            "// identical and simply stop lighting anything indirectly.\n"
            "#ifdef LUMEN_CARD_CAPTURE\n"
            "	return 0.0;\n"
            "#endif\n"
            "\n"
            "// Broad daylight. The last line multiplies everything by saturate(Blend), so this is the\n"
            "// same answer for free - and it is the answer for most of the cycle, on every wall in the\n"
            "// city, which is what makes skipping the two hashes below worth a branch.\n"
            "if (Blend <= 0.0)\n"
            "{\n"
            "	return 0.0;\n"
            "}\n"
            "\n"
            "// Which texels are windows, and which window each one belongs to. Two sources.\n"
            "float isWindow;\n"
            "float2 windowCell;\n"
            "if (HasAuthored > 0.5)\n"
            "{\n"
            "	// PAINTED (Tools/WindowLayoutEditor.html): R marks the texel, G is a per-window byte\n"
            "	// and B a per-row byte, both from connected-component labelling of the drawn shapes -\n"
            "	// so a window is whatever was painted and a row is one floor of that atlas cell. No\n"
            "	// dependence on the art's luminance at all, and exact edges.\n"
            "	isWindow = step(0.5, Authored.r);\n"
            "	// ONE TILE, ONE DRAW. floor(InCellUV) is which repeat of the atlas cell we are on\n"
            "	// across this wall, so every window the tile paints lights together - a lit flat, not\n"
            "	// a scatter of separate panes. The mask's per-window (G) and per-row (B) bytes are\n"
            "	// deliberately NOT in the key: rolling each painted blob on its own reads as speckle at\n"
            "	// city distance, where a whole window unit reads as a room with the light on. They stay\n"
            "	// in the file, so a finer unit is a change to this line and nothing else.\n"
            "	windowCell = floor(InCellUV);\n"
            "}\n"
            "else\n"
            "{\n"
            "	// DERIVED, for a page nobody has painted: the night art darkens the whole page EXCEPT\n"
            "	// the lit windows, so 'brighter at night than by day' plus 'bright in absolute terms'\n"
            "	// isolates them. Both tests are needed - the first alone catches nothing on a page that\n"
            "	// was only darkened, the second alone lights pale daylit stone. InCellUV's INTEGER part\n"
            "	// is which repeat of the cell we are on, so a scaled floor is a stable per-bay id.\n"
            "	float dayLum = dot(DayColor, float3(0.2126, 0.7152, 0.0722));\n"
            "	float nightLum = dot(NightColor, float3(0.2126, 0.7152, 0.0722));\n"
            "	float gotBrighter = saturate((nightLum - dayLum) * Contrast);\n"
            "	float isBright = saturate((nightLum - Threshold) * Contrast);\n"
            "	isWindow = min(gotBrighter, isBright);\n"
            "	windowCell = floor(InCellUV * max(Grid, 0.001));\n"
            "}\n"
            "\n"
            "// Not every window is occupied. The building's own origin keeps two identical towers\n"
            "// from lighting identically.\n"
            "float2 building = floor(BuildingOrigin.xy * 0.01);\n"
            "float buildingId = building.x + building.y * 71.0;\n"
            "\n"
            "// Cheap hash: fract(sin(dot)*k) is coarse but this only has to look unpatterned, and\n"
            "// the seed shifts the whole field so every night draws a different set.\n"
            "float3 windowKey = float3(windowCell, buildingId) + Seed;\n"
            "float windowRoll = frac(sin(dot(windowKey, float3(12.9898, 78.233, 37.719))) * 43758.5453);\n"
            "\n"
            "// A row is the same hash with the horizontal index dropped, so an entire floor of the\n"
            "// same building shares one draw - offices left on for the night. windowCell.y is the\n"
            "// tile row up the facade, so that is a band of tiles all the way across it.\n"
            "float3 rowKey = float3(0.0, windowCell.y, buildingId) + Seed * 1.7;\n"
            "float rowRoll = frac(sin(dot(rowKey, float3(39.3468, 11.135, 83.155))) * 24634.6345);\n"
            "\n"
            "float lit = max(step(windowRoll, LitFraction), step(rowRoll, RowLitFraction));\n"
            "return isWindow * lit * saturate(Blend);"
        ),
        [
            "DayColor", "NightColor", "Threshold", "Contrast", "Blend",
            "InCellUV", "BuildingOrigin", "Grid", "Seed", "LitFraction", "RowLitFraction",
            "Authored", "HasAuthored",
        ],
        -60,
        900,
        unreal.CustomMaterialOutputType.CMOT_FLOAT1,
    )
    for source, source_output, pin in (
        (texture, "RGB", "DayColor"),
        (night_texture, "RGB", "NightColor"),
        (glow_threshold, "", "Threshold"),
        (glow_contrast, "", "Contrast"),
        (night_blend, "", "Blend"),
        (in_cell_uv, "", "InCellUV"),
        (object_position, "", "BuildingOrigin"),
        (random_grid, "", "Grid"),
        (window_seed, "", "Seed"),
        (lit_fraction, "", "LitFraction"),
        (row_lit_fraction, "", "RowLitFraction"),
        (window_mask_texture, "RGB", "Authored"),
        (has_window_mask, "", "HasAuthored"),
    ):
        if not unreal.MaterialEditingLibrary.connect_material_expressions(source, source_output, window_mask, pin):
            unreal.log_error(f"M_SimCopterCityAtlas: window mask pin '{pin}' not connected.")

    # --- glass ------------------------------------------------------------------------------------
    #
    # The painted masks say exactly which texels are windows, and that is a fact about the BUILDING,
    # not about the hour - so the same data that lights a window at night can make it reflective at
    # noon. Glass was the one surface in the city with no way to tell itself apart from the wall it
    # is set into; now it has one, and windows take their own roughness/specular while the masonry
    # around them stays matte.
    #
    # This is a SEPARATE node from the glow mask above, and it has to be. That one answers "is this a
    # window that is LIT RIGHT NOW": it early-outs to zero in daylight and multiplies by the per
    # window occupancy roll, because only about a third of windows have anyone home. Reflection is a
    # property of the pane, so it wants neither gate - every window is glass, all day, whether or not
    # there is a light on behind it.
    window_glass_mask = add_custom_node(
        material,
        "OriginalWindowGlassMask",
        (
            "// Painted first, derived as the fallback - the same two sources the glow uses, minus\n"
            "// the time gate and the occupancy roll.\n"
            "if (HasAuthored > 0.5)\n"
            "{\n"
            "	return step(0.5, Authored.r);\n"
            "}\n"
            "\n"
            "// For an unpainted page: the night art darkens everything EXCEPT the lit windows, so\n"
            "// 'brighter at night than by day' and 'bright in absolute terms' together isolate them.\n"
            "// Weaker than the painted mask - it can only see windows that were lit in the art - but\n"
            "// all three wall pages (2, 39, 40) are painted, so this is effectively dead in retail.\n"
            "float dayLum = dot(DayColor, float3(0.2126, 0.7152, 0.0722));\n"
            "float nightLum = dot(NightColor, float3(0.2126, 0.7152, 0.0722));\n"
            "float gotBrighter = saturate((nightLum - dayLum) * Contrast);\n"
            "float isBright = saturate((nightLum - Threshold) * Contrast);\n"
            "return min(gotBrighter, isBright);"
        ),
        ["DayColor", "NightColor", "Threshold", "Contrast", "Authored", "HasAuthored"],
        -60,
        1300,
        unreal.CustomMaterialOutputType.CMOT_FLOAT1,
    )
    for source, source_output, pin in (
        (texture, "RGB", "DayColor"),
        (night_texture, "RGB", "NightColor"),
        (glow_threshold, "", "Threshold"),
        (glow_contrast, "", "Contrast"),
        (window_mask_texture, "RGB", "Authored"),
        (has_window_mask, "", "HasAuthored"),
    ):
        if not unreal.MaterialEditingLibrary.connect_material_expressions(
            source, source_output, window_glass_mask, pin
        ):
            unreal.log_error(f"M_SimCopterCityAtlas: glass mask pin '{pin}' not connected.")

    window_roughness = add_collection_parameter(material, "CityWindowRoughness", -560, 1300)
    window_specular = add_collection_parameter(material, "CityWindowSpecular", -560, 1380)

    glass_roughness = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, 240, 1300
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(city_roughness, "", glass_roughness, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(window_roughness, "", glass_roughness, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(window_glass_mask, "", glass_roughness, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        glass_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )

    glass_specular = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, 240, 1420
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(city_specular, "", glass_specular, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(window_specular, "", glass_specular, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(window_glass_mask, "", glass_specular, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        glass_specular, "", unreal.MaterialProperty.MP_SPECULAR
    )

    glow_strength = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 100, 900
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(window_mask, "", glow_strength, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(glow_nits, "", glow_strength, "B")

    # Tinted by the night art itself, so a window keeps the colour it was painted rather than all of
    # them turning the same white.
    glow_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 240, 780
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(night_texture, "RGB", glow_color, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(glow_strength, "", glow_color, "B")

    # ...and then warmed, because a good part of that art is painted white. See the note on
    # WINDOW_GLOW_TINT_DEFAULT: luminance-normalised, so this is a hue change, not a brightness one.
    glow_tint = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, 100, 1000
    )
    glow_tint.set_editor_property("parameter_name", WINDOW_GLOW_TINT_PARAMETER)
    glow_tint.set_editor_property(
        "default_value",
        unreal.LinearColor(
            WINDOW_GLOW_TINT_DEFAULT[0], WINDOW_GLOW_TINT_DEFAULT[1], WINDOW_GLOW_TINT_DEFAULT[2], 1.0
        ),
    )
    glow_tint.set_editor_property("group", SHADING_GROUP)

    tinted_glow = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 300, 860
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(glow_color, "", tinted_glow, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(glow_tint, "RGB", tinted_glow, "B")

    emissive_sum = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionAdd, 380, 500
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(self_illum_emissive, "", emissive_sum, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(tinted_glow, "", emissive_sum, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        emissive_sum, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )

    # Some original pool/pond faces are authored exactly on a terrain or pad plane. Lift only the
    # page-20 water cells in the vertex shader so the lower surface cannot win the depth test.
    # This offset is constant: mesh water texture animation is deliberately disabled, and geometry
    # and collision remain unchanged.
    mesh_water_render_lift = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -900, 820
    )
    mesh_water_render_lift.set_editor_property("parameter_name", "MeshWaterRenderLiftCm")
    mesh_water_render_lift.set_editor_property("default_value", 2.0)
    mesh_water_render_lift.set_editor_property("group", "Mesh Water")
    mesh_water_wpo = add_custom_node(
        material,
        "OriginalMeshWaterRenderLift",
        (
            "float sourceIndex = (7.0 - CellIndex.y) * 8.0 + CellIndex.x;\n"
            "float isWaterBase = (abs(sourceIndex) < 0.1 || abs(sourceIndex - 5.0) < 0.1) ? 1.0 : 0.0;\n"
            "float useLift = step(0.5, Enabled) * isWaterBase;\n"
            "return float3(0.0, 0.0, max(LiftCm, 0.0) * useLift);"
        ),
        ["CellIndex", "Enabled", "LiftCm"],
        -420,
        760,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        cell_index, "", mesh_water_wpo, "CellIndex"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        animate_water, "", mesh_water_wpo, "Enabled"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        mesh_water_render_lift, "", mesh_water_wpo, "LiftCm"
    )
    unreal.MaterialEditingLibrary.connect_material_property(
        mesh_water_wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
    )

    material.set_editor_property("two_sided", False)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterCityAtlas")


def create_rotor_disc_material():
    """Near-translucent grey disc for the spinning rotor blur (Maxis face type 11).

    The original game draws the rotor as a faint translucent disc over the thin opaque
    blades. This is an Unlit + Translucent material so the disc reads as a soft grey haze
    regardless of scene lighting; DiscColor/DiscOpacity are exposed for tuning. It is drawn
    on a ProceduralMeshComponent (not Nanite), so the Nanite usage flag is not required.

    Face type 11 is not helicopter-only, though: PP200 (the SC2000 wind power plant) models
    its entire fan wheel as type-11 triangles, and the city draws its buildings as INSTANCED
    static meshes - hence the instanced-static-mesh usage flag below. Translucent materials
    are not a Nanite path, so that flag still stays off."""
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

    # The city's instanced BUILDING components draw the windmill wheel with this material, so the
    # flag has to be baked in - same reasoning as create_lit_vertex_color_material's. The editor
    # patches it on at runtime, which is exactly why a missing flag here goes unnoticed until a
    # cooked build renders the wheel with the default material.
    material.set_editor_property("used_with_instanced_static_meshes", True)
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


# Sprite cards (Maxis face type 2 - trees, signs) and the rare direct-image polygons (face type 13)
# used to hang off M_SimCopterSpriteTexture, which is UNLIT: it writes the decoded palette colour
# straight into Emissive. That is right for fire and particle kernels, which are their own light
# source, but it is wrong for a tree. An unlit surface holds one fixed brightness while everything
# around it tracks the sun, so under the day/night sequence the trees read INVERTED - washed-out
# dark cards at noon next to sunlit ground, then glowing cards at midnight next to a dark city.
#
# This is the same material for the same geometry, but Default Lit and sharing the city's SelfIllum
# / Roughness / Specular parameters, so a tree now brightens and darkens with the rest of the city.
#
# The shading normal is the catch. A card is a crossed pair of VERTICAL quads (AppendMaxisSpriteCard),
# each quad emitted with both windings, so its geometric normals are horizontal and point opposite
# ways on the two halves. Lit off those, a tree would go black under a high sun (N.L ~ 0) and would
# show a seam down the middle where the crossed quads meet. So the normal is biased towards world up:
# at the default 1.0 the card shades exactly like the flat ground it stands on, which is the whole
# point - the tree tracks the sun in step with the tile underneath it. Lower it to let the geometric
# normal back in (the face-type-13 polygons are the only geometry here with meaningful normals).
SPRITE_CARD_NORMAL_CODE = (
    "float3 up = float3(0.0, 0.0, 1.0);\n"
    "float3 geo = normalize(BaseNormal);\n"
    "float3 blended = lerp(geo, up, saturate(UpBias));\n"
    "return dot(blended, blended) < 1e-6 ? up : normalize(blended);"
)


def create_lit_sprite_texture_material():
    """Masked, LIT card material for the city's original sprite art (trees, signs).

    Same chroma-keyed sampling as M_SimCopterSpriteTexture - the bake writes palette index 0 into
    texture alpha and the mask drops those texels - but shaded by the scene instead of self-lit.
    See SPRITE_CARD_NORMAL_CODE above for why the normal is pushed towards world up."""
    material = create_or_load_material("M_SimCopterLitSpriteTexture")
    clear_expressions(material)

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.5)
    # Kept two-sided to match the unlit material it replaces, so nothing that was visible before
    # disappears. The card geometry already emits both windings; this only covers the type-13 faces.
    material.set_editor_property("two_sided", True)

    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0
    )
    texture.set_editor_property("parameter_name", "Texture")

    unreal.MaterialEditingLibrary.connect_material_property(
        texture, "A", unreal.MaterialProperty.MP_OPACITY_MASK
    )
    # Tree/sign family: BaseColor goes through the albedo ceiling (add_family_shading_nodes wires
    # MP_BASE_COLOR itself), and roughness/specular come from the live collection. See
    # SURFACE_SHADING_FAMILIES for why the cards need their own numbers.
    add_family_shading_nodes(material, texture, "RGB", "Tree")

    base_normal = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexNormalWS, -900, 760
    )
    up_bias = add_scalar_parameter(material, "CardNormalUpBias", 1.0, 3, 880)
    normal = add_custom_node(
        material, "SpriteCardNormal", SPRITE_CARD_NORMAL_CODE, ["BaseNormal", "UpBias"], -560, 780,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(base_normal, "", normal, "BaseNormal")
    unreal.MaterialEditingLibrary.connect_material_expressions(up_bias, "", normal, "UpBias")
    unreal.MaterialEditingLibrary.connect_material_property(
        normal, "", unreal.MaterialProperty.MP_NORMAL
    )

    # The blended normal is world-space, so it must not be interpreted as tangent-space.
    material.set_editor_property("tangent_space_normal", False)
    # The trees ride on the merged city mesh AND on the per-model instanced building meshes, which
    # are Nanite; both usage flags have to be baked in or the engine patches them at runtime.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterLitSpriteTexture")


def upgrade_sprite_texture_emissive():
    """Give the already-built M_SimCopterSpriteTexture an EmissiveNits scale, in place.

    The particle KERNELS (the original's FIREPTS point sprites) draw on this material, and so do the
    pedestrian sprites and the privanim figure heads, so it went black under the day sequence with
    everything else unlit. It is not in the delete-and-recreate list - a material instance may hold
    it as a parent, and deleting the asset would null that out - so this edits the existing graph
    instead: add the two nodes, re-point Emissive at them, leave everything else alone.

    Idempotent: a material that already has the parameter is skipped, so re-running is free."""
    path = f"{MATERIAL_DIR}/M_SimCopterSpriteTexture"
    material = unreal.EditorAssetLibrary.load_asset(path)
    if material is None:
        return

    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    if any(isinstance(e, unreal.MaterialExpressionScalarParameter) and
           e.get_editor_property("parameter_name") == "EmissiveNits" for e in expressions):
        unreal.log("M_SimCopterSpriteTexture already carries EmissiveNits; skipping.")
        return

    texture = next(
        (e for e in expressions if isinstance(e, unreal.MaterialExpressionTextureSampleParameter2D)),
        None)
    if texture is None:
        unreal.log_error("M_SimCopterSpriteTexture has no texture sample to scale; left alone.")
        return

    add_scene_scaled_emissive(material, texture, "RGB", -180, -160)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(path)


def reparent_baked_direct_image_instances():
    """Point already-baked MI_CityImage_* instances at the lit card material.

    The MI_CityImage_* / T_CityImage_* assets are decoded original art, so they are gitignored and
    only exist on a machine that has run BakeCityAtlas.py. Re-baking them costs a full per-pixel
    Python decode of SIM3D.BMP; re-parenting is the same result in a second, and it is idempotent -
    an instance already on the lit parent is left alone."""
    lit_parent = unreal.EditorAssetLibrary.load_asset(f"{MATERIAL_DIR}/M_SimCopterLitSpriteTexture")
    old_parent = unreal.EditorAssetLibrary.load_asset(f"{MATERIAL_DIR}/M_SimCopterSpriteTexture")
    if lit_parent is None:
        return

    if not unreal.EditorAssetLibrary.does_directory_exist(BAKED_ATLAS_DIR):
        unreal.log(f"No {BAKED_ATLAS_DIR}; nothing to re-parent (run BakeCityAtlas.py first).")
        return

    reparented = []
    for asset_path in unreal.EditorAssetLibrary.list_assets(BAKED_ATLAS_DIR, recursive=False):
        name = asset_path.split("/")[-1].split(".")[0]
        if not name.startswith("MI_CityImage_"):
            continue
        instance = unreal.EditorAssetLibrary.load_asset(asset_path)
        if instance is None or instance.get_editor_property("parent") != old_parent:
            continue
        instance.set_editor_property("parent", lit_parent)
        unreal.EditorAssetLibrary.save_asset(f"{BAKED_ATLAS_DIR}/{name}", only_if_is_dirty=False)
        reparented.append(name)

    unreal.log(f"SPRITE CARD RE-PARENT: {len(reparented)} MI_CityImage_* instances now lit.")


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


def add_water_frame_index(material, time, frames_per_second, name, x, y):
    """Return the discrete current frame of the original five-cell water cycle."""
    frame_index = add_custom_node(
        material,
        name,
        (
            "float framePosition = max(Time, 0.0) * max(FramesPerSecond, 0.0);\n"
            "float wrappedPosition = framePosition - 5.0 * floor(framePosition / 5.0);\n"
            "return min(floor(wrappedPosition), 4.0);"
        ),
        ["Time", "FramesPerSecond"],
        x,
        y,
        unreal.CustomMaterialOutputType.CMOT_FLOAT1,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", frame_index, "Time")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        frames_per_second, "", frame_index, "FramesPerSecond"
    )
    return frame_index


# The undulating sea. Same TILED1 texturing + lit shading as the terrain, but the water tiles are
# displaced vertically in the vertex shader (World Position Offset) instead of on the CPU, so the
# animation is effectively free and runs identically in the editor and in game. A per-vertex weight
# (baked into vertex-color R by the city renderer: 0 = shoreline, 1 = open water) pins the coast so
# the water stays welded to the static land mesh. The surface normal is analytic: the wave slope is
# added ON TOP OF the mesh's rest normal (BaseNormal), which the renderer smooths and welds to the
# land normals. So at the shore (weight 0) the water shades like the sloped ground it meets - no
# lighting seam even where the coastline is angled - while offshore the base slope eases out to level
# and the wave ripple takes over, so open water reads flat instead of a tilted sheet.
WATER_WAVE_INPUTS = ["WorldPos", "Time", "Weight", "Amplitude", "WaveLength", "Speed", "LowPower"]
WATER_NORMAL_INPUTS = WATER_WAVE_INPUTS + ["BaseNormal", "DetailStrength", "DetailScale"]
WATER_UV_INPUTS = ["BaseUV", "Frame"]

# SCHOOK: FUN_004814c0 advances DAT_00503f68 once its accumulator, fed by the fixed-time delta
# DAT_005039a0, exceeds 4000. The earlier port incorrectly read that threshold as milliseconds.
# Preserve the five frames and expose their playback cadence; 4 Hz is the presentation default.
# The selected frame is added to terrain types 0 (coastal water) and 5 (open water).
# TILED1 is an 8x8 atlas whose raw rows were flipped when decoded into Unreal, so move by cell
# index rather than adding U: the open-water sequence 5..9 crosses from the first row into the
# second. This is texture animation only; the WPO wave below remains an independent effect.
def water_uv_code():
    return (
        "float2 atlasUV = BaseUV * 8.0;\n"
        "float2 inCellUV = frac(atlasUV);\n"
        "float sourceColumn = floor(atlasUV.x);\n"
        "float sourceDecodedRow = floor(atlasUV.y);\n"
        "float sourceIndex = (7.0 - sourceDecodedRow) * 8.0 + sourceColumn;\n"
        "float targetIndex = sourceIndex + Frame;\n"
        "float targetColumn = fmod(targetIndex, 8.0);\n"
        "float targetDecodedRow = 7.0 - floor(targetIndex / 8.0);\n"
        "return (float2(targetColumn, targetDecodedRow) + inCellUV) / 8.0;"
    )

WATER_WAVE_PRELUDE = (
    "float K1 = 2.0 * 3.14159265 / max(WaveLength, 1.0);\n"
    "float K2 = K1 * 1.7;\n"
    "float P1 = K1 * (WorldPos.x * 0.7 + WorldPos.y * 0.7) + Time * Speed;\n"
    "float P2 = K2 * (WorldPos.x * 0.3 - WorldPos.y * 0.95) + Time * Speed * 0.8;\n"
)

# Shoreline vertices carry Weight 0, which already multiplies both waves out to nothing - so the
# early-out below is the SAME answer, not a simplification, and it is free in both graphics modes.
# Low Power takes the same exit for the whole surface: the original's sea was a five-frame texture
# cycle on a flat plane and nothing else, so this loses a wave the 1996 game never had.
WATER_WPO_CODE = (
    "if (Weight <= 0.0 || LowPower > 0.5)\n"
    "{\n"
    "	return float3(0.0, 0.0, 0.0);\n"
    "}\n"
    + WATER_WAVE_PRELUDE
    + "float h = Weight * Amplitude * (0.6 * sin(P1) + 0.4 * sin(P2));\n"
    + "return float3(0.0, 0.0, h);"
)

# Wave height gradient (dHdx, dHdy) added onto the rest surface's gradient, recovered from BaseNormal
# (an up-facing world normal): a height field z=B has normal ~ (-dB/dx, -dB/dy, 1), so dB/dx = -Nx/Nz.
# The base slope is faded out with distance offshore (baseFade = 1 - Weight): at the shoreline
# (Weight 0) the full base slope is used so the water shades like the land it is welded to, but a few
# tiles out (Weight -> 1) the base eases to flat so open water reads level instead of a tilted sheet -
# the near-shore geometry is genuinely sloped (a height-averaging artifact), and this hides that in the
# lighting without touching the geometry or the welded shoreline edge.
#
# The early-out is exact, not an approximation: at Weight 0 the wave gradient is zero and baseFade is
# 1, so the expression below collapses to normalize(float3(Nx/Nz, Ny/Nz, 1)), which is BaseNormal's
# own direction. It is gated on an up-facing normal because the Nz clamp is what would make the two
# disagree. Low Power takes it for the whole surface, matching the flat WPO above.
WATER_NORMAL_CODE = (
    "if ((Weight <= 0.0 || LowPower > 0.5) && BaseNormal.z > 0.0)\n"
    "{\n"
    "	return normalize(BaseNormal);\n"
    "}\n"
    + WATER_WAVE_PRELUDE
    + "float amp = Weight * Amplitude;\n"
    + "float dHdx = amp * (0.6 * cos(P1) * K1 * 0.7 + 0.4 * cos(P2) * K2 * 0.3);\n"
    + "float dHdy = amp * (0.6 * cos(P1) * K1 * 0.7 + 0.4 * cos(P2) * K2 * (-0.95));\n"
    + "float bz = max(BaseNormal.z, 1e-3);\n"
    + "float baseFade = 1.0 - Weight;\n"
    + "float baseX = (BaseNormal.x / bz) * baseFade;\n"
    + "float baseY = (BaseNormal.y / bz) * baseFade;\n"
    # A fine per-pixel ripple on top, NORMAL ONLY - it never touches the WPO, so the geometry and
    # the welded shoreline are exactly as before. Its whole job is to stop the surface being a
    # mirror over large flat spans: at roughness 0.12 anything that survives as a flat area
    # reflects the sky as one sheet, and every seam in the coarse per-vertex data (the 400 cm
    # quads, the tile-quantised coast) then reads as a hard edge in that sheet. Breaking the
    # normal up at a few tens of centimetres scatters those edges into something that reads as
    # water. Scaled by Weight so the pinned shoreline keeps shading like the land it welds to.
    + "float detail = DetailStrength * Weight;\n"
    + "if (detail > 0.0)\n"
    + "{\n"
    + "	float kd = 6.2831853 / max(DetailScale, 1.0);\n"
    + "	float D1 = kd * (WorldPos.x * 0.86 - WorldPos.y * 0.51) + Time * Speed * 2.7;\n"
    + "	float D2 = kd * (WorldPos.x * 0.42 + WorldPos.y * 0.91) + Time * Speed * 1.9;\n"
    + "	dHdx += detail * (cos(D1) * kd * 0.86 + cos(D2) * kd * 0.42);\n"
    + "	dHdy += detail * (cos(D1) * kd * -0.51 + cos(D2) * kd * 0.91);\n"
    + "}\n"
    + "return normalize(float3(baseX - dHdx, baseY - dHdy, 1.0));"
)


def create_water_material():
    material = create_or_load_material("M_SimCopterWater")
    clear_expressions(material)

    # Base color / lit shading, identical to the terrain-low material so the water reads the same.
    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -400, -100
    )
    texture.set_editor_property("parameter_name", "Texture")

    # Keep the original five-frame TILED1 animation on the dedicated terrain-water surface. The
    # source mesh already carries either cell 0 or cell 5 UVs, and the material hard-switches to
    # the selected cell without crossfading adjacent source frames.
    base_uv = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureCoordinate, -1000, -160
    )
    texture_time = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTime, -1000, -40
    )
    texture_fps = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionScalarParameter, -1000, 80
    )
    texture_fps.set_editor_property("parameter_name", "WaterTextureFramesPerSecond")
    texture_fps.set_editor_property("default_value", 4.0)
    texture_fps.set_editor_property("group", "Texture Animation")
    frame_index = add_water_frame_index(
        material, texture_time, texture_fps, "OriginalWaterTextureFrame", -850, 300
    )
    animated_uv = add_custom_node(
        material, "OriginalWaterTextureUV", water_uv_code(), WATER_UV_INPUTS, -700, -180,
        unreal.CustomMaterialOutputType.CMOT_FLOAT2,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        base_uv, "", animated_uv, "BaseUV"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(
        frame_index, "", animated_uv, "Frame"
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(animated_uv, "", texture, "UVs")
    # Water family - the one surface here that wants to be GLOSSY. It was sharing the shared matte
    # 0.65/0.3 with the dirt, which is why it had no sun glint. Roughness and specular are wired
    # below instead of here, because they are faded out towards the shore.
    _, _, open_roughness, open_specular = add_family_shading_nodes(
        material, texture, "RGB", "Water", connect_roughness_specular=False
    )

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
    # A collection scalar, not a material parameter: the Settings screen has to be able to move it at
    # runtime and this material is used through instances that cannot be animated.
    low_power = add_collection_parameter(material, "LowPower", -1000, 1340)

    def wire_wave_inputs(node):
        unreal.MaterialEditingLibrary.connect_material_expressions(world_pos, "", node, "WorldPos")
        unreal.MaterialEditingLibrary.connect_material_expressions(time, "", node, "Time")
        unreal.MaterialEditingLibrary.connect_material_expressions(weight, "R", node, "Weight")
        unreal.MaterialEditingLibrary.connect_material_expressions(amplitude, "", node, "Amplitude")
        unreal.MaterialEditingLibrary.connect_material_expressions(wavelength, "", node, "WaveLength")
        unreal.MaterialEditingLibrary.connect_material_expressions(speed, "", node, "Speed")
        unreal.MaterialEditingLibrary.connect_material_expressions(low_power, "", node, "LowPower")

    wpo = add_custom_node(
        material, "WaterWPO", WATER_WPO_CODE, WATER_WAVE_INPUTS, -650, 750,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    wire_wave_inputs(wpo)
    unreal.MaterialEditingLibrary.connect_material_property(
        wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET
    )

    # --- shoreline fade ---------------------------------------------------------------------------
    #
    # The reflection used to stop DEAD at the coast: open water at roughness 0.12 met the land at a
    # tile-quantised boundary, so the mirror ended on a hard stair-stepped line and every 400 cm quad
    # seam near it read as part of a grid.
    #
    # Vertex-colour R is already the mask for this - it is the wave weight, 0 at the welded shoreline
    # and 1 offshore - so the fade needs no new vertex data and no re-bake. Roughness and specular
    # ease from a matte shoreline pair (wet sand, not a mirror) out to the open-water pair across
    # WaterShoreFadeWidth of that ramp, which is what turns the hard edge into a beach.
    shore_roughness = add_collection_parameter(material, "WaterShoreRoughness", -1000, 1460)
    shore_specular = add_collection_parameter(material, "WaterShoreSpecular", -1000, 1540)
    shore_fade_width = add_collection_parameter(material, "WaterShoreFadeWidth", -1000, 1620)
    shore_edge_noise = add_collection_parameter(material, "WaterShoreEdgeNoiseStrength", -1000, 1860)
    shore_edge_scale = add_collection_parameter(material, "WaterShoreEdgeNoiseScale", -1000, 1940)
    shore_edge_noise2 = add_collection_parameter(material, "WaterShoreEdgeNoiseStrength2", -1000, 2020)
    shore_edge_scale2 = add_collection_parameter(material, "WaterShoreEdgeNoiseScale2", -1000, 2100)
    shore_edge_speed = add_collection_parameter(material, "WaterShoreEdgeNoiseSpeed", -1000, 2180)
    shore_edge_speed2 = add_collection_parameter(material, "WaterShoreEdgeNoiseSpeed2", -1000, 2260)
    shore_fade = add_custom_node(
        material,
        "WaterShoreFade",
        # The fade's SHAPE is the problem this solves, not its softness.
        #
        # The weight is a per-vertex value interpolated bilinearly across 400 cm quads, so whatever
        # contour you threshold out of it follows the grid: straight runs along tile edges, 45-degree
        # cuts across quad diagonals, and a visible kink wherever two quads meet. Softening the ramp
        # does not help - a soft grid is still a grid.
        #
        # Warping the weight in WORLD SPACE does. Displacing the value before the ramp displaces the
        # contour laterally, by roughly strength / |grad(weight)| - so a couple of octaves of the
        # same value noise the terrain uses turns the stair-step into a line that wanders across the
        # tile boundaries instead of tracing them. It is not literally a spline through the corners,
        # but it is what removes the read of a grid.
        #
        # Scaled well above a tile (WaterShoreEdgeNoiseScale) on purpose: at tile scale it would just
        # add fizz to the same stair-step, and the wander has to be BIGGER than the 400 cm step it is
        # hiding to hide it.
        # TWO INDEPENDENT LAYERS, not two octaves of one. An octave chain would lock the second
        # layer's amplitude and wavelength to the first's; these each carry their own pair so a big
        # lazy wander and a fine raggedness can be dialled separately. They are sampled at different
        # world offsets so the two do not line up and reinforce into one bigger wave.
        #
        # Only layer 1 has to be coarser than a tile - that is the one hiding the 400 cm step. Layer
        # 2 is free to be finer, because by then there is no straight edge left to hide, only a
        # curve to make less regular.
        "float w = max(Width, 0.001);\n"
        "float v = saturate(Weight);\n"
        # ANIMATION IS A THIRD AXIS ON THE HASH, NOT A SCROLL.
        #
        # Sliding the sample position would move the pattern across the water, and at these
        # wavelengths that reads as a conveyor belt - the whole shoreline visibly travelling one way.
        # What is wanted is the field CHANGING while staying put, so the hash takes an integer frame
        # number as its z. Speed is therefore in noise frames per second: 0.5 draws a new random
        # field every two seconds and spends those two seconds moving into it.
        #
        # CATMULL-ROM THROUGH FOUR FRAMES, and the reason is worth keeping.
        #
        # The first version blended two adjacent frames with a smoothstep, which is C1 and looked
        # obviously wrong: it read as FRAMEY. Smoothstep is ease-in-ease-out, so the field arrives at
        # every keyframe with zero velocity, holds, and then rushes to the next one - still, fast,
        # still, fast, once per frame. Making the easing higher-order (smootherstep) flattens the
        # ends further and makes the hold LONGER, so the obvious next step is the wrong one.
        #
        # What is actually wanted is a curve that passes THROUGH each frame without stopping on it,
        # which is the ordinary keyframe-interpolation problem and has the ordinary answer: a cubic
        # through four control points, taking its velocity at each frame from that frame's
        # neighbours. The field then evolves at a near-constant rate and there is no beat to see.
        #
        # It costs four spatial taps per layer instead of two. That is the price of the fix.
        "#define SHORE_HASH(px, py, pz) frac(sin(dot(float3(px, py, pz), float3(127.1, 311.7, 74.7))) * 43758.5453)\n"
        "float strengths[2] = { NoiseStrength, NoiseStrength2 };\n"
        "float scales[2] = { max(NoiseScale, 1.0), max(NoiseScale2, 1.0) };\n"
        "float speeds[2] = { Speed, Speed2 };\n"
        "float2 offsets[2] = { float2(0.0, 0.0), float2(37.3, 11.9) };\n"
        "float warp = 0.0;\n"
        "const float M = 256.0;\n"
        "[unroll] for (int o = 0; o < 2; o++)\n"
        "{\n"
        "	if (strengths[o] <= 0.0) { continue; }\n"
        "	float2 p = WorldPos.xy / scales[o] + offsets[o];\n"
        "	float2 ip = floor(p);\n"
        "	float2 f = p - ip;\n"
        "	float2 u = f * f * (3.0 - 2.0 * f);\n"
        "	float2 i0 = ip - floor(ip / M) * M;\n"
        "	float2 i1 = i0 + 1.0; i1 = i1 - floor(i1 / M) * M;\n"
        "\n"
        "	float ft = Time * speeds[o];\n"
        "	float f0 = floor(ft);\n"
        "	float tb = ft - f0;\n"
        "\n"
        # The four control points: the frame before, the two we are between, and the one after. The
        # outer two exist only to give the curve its slope at the inner two - that is what stops the
        # field halting on a keyframe.
        "	float frames[4];\n"
        "	[unroll] for (int k = 0; k < 4; k++)\n"
        "	{\n"
        # Wrapped like the xy indices are: Time climbs without bound and sin() of a large argument
        # loses the precision the hash depends on, so without this the water quietly stops changing
        # after a while. floor() handles the negative k = 0 case correctly.
        "		float zf = f0 + (float)(k - 1);\n"
        "		float z = zf - floor(zf / M) * M;\n"
        "		frames[k] = lerp(lerp(SHORE_HASH(i0.x, i0.y, z), SHORE_HASH(i1.x, i0.y, z), u.x),\n"
        "		                 lerp(SHORE_HASH(i0.x, i1.y, z), SHORE_HASH(i1.x, i1.y, z), u.x), u.y);\n"
        "	}\n"
        "\n"
        # Catmull-Rom. It can overshoot [0,1] slightly between frames, which is left alone on
        # purpose - clamping it would put back a flat spot of exactly the kind being removed, and
        # the warp is saturated at the end anyway.
        "	float c3 = -0.5 * frames[0] + 1.5 * frames[1] - 1.5 * frames[2] + 0.5 * frames[3];\n"
        "	float c2 = frames[0] - 2.5 * frames[1] + 2.0 * frames[2] - 0.5 * frames[3];\n"
        "	float c1 = -0.5 * frames[0] + 0.5 * frames[2];\n"
        "	float n = ((c3 * tb + c2) * tb + c1) * tb + frames[1];\n"
        "	warp += (n - 0.5) * 2.0 * strengths[o];\n"
        "}\n"
        "#undef SHORE_HASH\n"
        "v = saturate(v + warp);\n"
        # Smootherstep rather than smoothstep: one more order of continuity, which takes out the
        # faint crease where the ramp starts and stops. Free, and it is the remaining straight line.
        "float t = saturate(v / w);\n"
        "return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);",
        [
            "Weight", "Width", "WorldPos", "Time",
            "NoiseStrength", "NoiseScale", "Speed",
            "NoiseStrength2", "NoiseScale2", "Speed2",
        ],
        -820,
        1500,
        unreal.CustomMaterialOutputType.CMOT_FLOAT1,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(weight, "R", shore_fade, "Weight")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_fade_width, "", shore_fade, "Width")
    unreal.MaterialEditingLibrary.connect_material_expressions(world_pos, "", shore_fade, "WorldPos")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_edge_noise, "", shore_fade, "NoiseStrength")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_edge_scale, "", shore_fade, "NoiseScale")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_edge_noise2, "", shore_fade, "NoiseStrength2")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_edge_scale2, "", shore_fade, "NoiseScale2")
    unreal.MaterialEditingLibrary.connect_material_expressions(time, "", shore_fade, "Time")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_edge_speed, "", shore_fade, "Speed")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_edge_speed2, "", shore_fade, "Speed2")

    faded_roughness = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, -600, 1460
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_roughness, "", faded_roughness, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(open_roughness, "", faded_roughness, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_fade, "", faded_roughness, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        faded_roughness, "", unreal.MaterialProperty.MP_ROUGHNESS
    )

    faded_specular = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionLinearInterpolate, -600, 1620
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_specular, "", faded_specular, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(open_specular, "", faded_specular, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(shore_fade, "", faded_specular, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        faded_specular, "", unreal.MaterialProperty.MP_SPECULAR
    )

    normal = add_custom_node(
        material, "WaterNormal", WATER_NORMAL_CODE, WATER_NORMAL_INPUTS, -650, 1050,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    wire_wave_inputs(normal)
    detail_strength = add_collection_parameter(material, "WaterDetailNormalStrength", -1000, 1700)
    detail_scale = add_collection_parameter(material, "WaterDetailNormalScale", -1000, 1780)
    unreal.MaterialEditingLibrary.connect_material_expressions(detail_strength, "", normal, "DetailStrength")
    unreal.MaterialEditingLibrary.connect_material_expressions(detail_scale, "", normal, "DetailScale")
    # The mesh's (smoothed, land-welded) rest normal is the base slope the waves ride on.
    base_normal = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexNormalWS, -1000, 1360
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(base_normal, "", normal, "BaseNormal")
    unreal.MaterialEditingLibrary.connect_material_property(
        normal, "", unreal.MaterialProperty.MP_NORMAL
    )

    # The analytic normal is world-space, so the Normal input must not be interpreted as tangent-space.
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", False)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterWater")


# Ground detail material: same TILED1 texturing + lit shading as MI_TerrainLow/High, but it perturbs
# the surface normal with four octaves of procedural value noise (fine / medium / large / extra large),
# each with its own amplitude and scale, so the otherwise smooth terrain lighting gets organic relief.
# The perturbation is added on top of the mesh's smoothed rest normal (BaseNormal = VertexNormalWS)
# and scaled by a per-vertex weight (vertex-color R) the renderer bakes: 0 near the land/water
# shoreline (and ramping in away from flat building/road pads) so it never disturbs the water weld or
# the crisp pads, ramping to 1 over open ground. Value noise gives an analytic gradient (one 4-corner
# hash per octave, no finite differences); cell coords are wrapped to keep the sin-hash stable far
# from the origin.
#
# Two exits before any of that runs, and both give exactly `normalize(BaseNormal)` - which is what
# the last line returns when the gradient is zero:
#
#   * Weight 0 is the shoreline and the flat building/road pads, where the whole point is that the
#     noise contributes nothing. Free in both graphics modes.
#   * Low Power skips it everywhere. Four octaves is four hashed 4-corner lookups per pixel across
#     the entire ground plane, which is the most expensive material in the city by area, and what it
#     buys is relief the original's flat-shaded terrain never had.
TERRAIN_NOISE_INPUTS = [
    "WorldPos", "BaseNormal", "Weight", "LowPower",
    "AmpFine", "ScaleFine", "AmpMed", "ScaleMed",
    "AmpLarge", "ScaleLarge", "AmpXLarge", "ScaleXLarge",
]

TERRAIN_NOISE_CODE = (
    "if (Weight <= 0.0 || LowPower > 0.5)\n"
    "{\n"
    "	return normalize(BaseNormal);\n"
    "}\n"
    "float amps[4] = { AmpFine, AmpMed, AmpLarge, AmpXLarge };\n"
    "float scales[4] = { max(ScaleFine, 1.0), max(ScaleMed, 1.0), max(ScaleLarge, 1.0), max(ScaleXLarge, 1.0) };\n"
    "float gx = 0.0;\n"
    "float gy = 0.0;\n"
    "const float M = 256.0;\n"
    "[unroll] for (int o = 0; o < 4; o++)\n"
    "{\n"
    "    float s = scales[o];\n"
    "    float2 p = WorldPos.xy / s;\n"
    "    float2 ip = floor(p);\n"
    "    float2 f = p - ip;\n"
    "    float2 u = f * f * (3.0 - 2.0 * f);\n"
    "    float2 du = 6.0 * f * (1.0 - f);\n"
    "    float2 i0 = ip - floor(ip / M) * M;\n"
    "    float2 i1 = i0 + 1.0; i1 = i1 - floor(i1 / M) * M;\n"
    "    float a = frac(sin(dot(float2(i0.x, i0.y), float2(127.1, 311.7))) * 43758.5453);\n"
    "    float b = frac(sin(dot(float2(i1.x, i0.y), float2(127.1, 311.7))) * 43758.5453);\n"
    "    float c = frac(sin(dot(float2(i0.x, i1.y), float2(127.1, 311.7))) * 43758.5453);\n"
    "    float d = frac(sin(dot(float2(i1.x, i1.y), float2(127.1, 311.7))) * 43758.5453);\n"
    "    float abcd = a - b - c + d;\n"
    "    float dvx = ((b - a) + abcd * u.y) * du.x;\n"
    "    float dvy = ((c - a) + abcd * u.x) * du.y;\n"
    "    gx += amps[o] * dvx / s;\n"
    "    gy += amps[o] * dvy / s;\n"
    "}\n"
    "return normalize(BaseNormal + Weight * float3(-gx, -gy, 0.0));"
)


def create_terrain_material():
    material = create_or_load_material("M_SimCopterTerrain")
    clear_expressions(material)

    # Base color / lit shading, identical to MI_TerrainLow so the ground reads the same.
    texture = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0
    )
    texture.set_editor_property("parameter_name", "Texture")
    # Ground family: matte, and albedo-capped so a sunlit hillside stops reading as bare highlight.
    add_family_shading_nodes(material, texture, "RGB", "Terrain")

    world_pos = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionWorldPosition, -1000, 700
    )
    base_normal = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexNormalWS, -1000, 820
    )
    weight = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexColor, -1000, 940
    )
    amp_fine = add_scalar_parameter(material, "NoiseAmpFine", 12.0, 3, 1060)
    scale_fine = add_scalar_parameter(material, "NoiseScaleFine", 350.0, 4, 1140)
    amp_med = add_scalar_parameter(material, "NoiseAmpMed", 45.0, 5, 1220)
    scale_med = add_scalar_parameter(material, "NoiseScaleMed", 1000.0, 6, 1300)
    amp_large = add_scalar_parameter(material, "NoiseAmpLarge", 150.0, 7, 1380)
    scale_large = add_scalar_parameter(material, "NoiseScaleLarge", 3000.0, 8, 1460)
    amp_xlarge = add_scalar_parameter(material, "NoiseAmpXLarge", 400.0, 9, 1540)
    scale_xlarge = add_scalar_parameter(material, "NoiseScaleXLarge", 8000.0, 10, 1620)
    low_power = add_collection_parameter(material, "LowPower", -1000, 1060)

    noise = add_custom_node(
        material, "TerrainNormalNoise", TERRAIN_NOISE_CODE, TERRAIN_NOISE_INPUTS, -650, 800,
        unreal.CustomMaterialOutputType.CMOT_FLOAT3,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(world_pos, "", noise, "WorldPos")
    unreal.MaterialEditingLibrary.connect_material_expressions(base_normal, "", noise, "BaseNormal")
    unreal.MaterialEditingLibrary.connect_material_expressions(weight, "R", noise, "Weight")
    unreal.MaterialEditingLibrary.connect_material_expressions(low_power, "", noise, "LowPower")
    unreal.MaterialEditingLibrary.connect_material_expressions(amp_fine, "", noise, "AmpFine")
    unreal.MaterialEditingLibrary.connect_material_expressions(scale_fine, "", noise, "ScaleFine")
    unreal.MaterialEditingLibrary.connect_material_expressions(amp_med, "", noise, "AmpMed")
    unreal.MaterialEditingLibrary.connect_material_expressions(scale_med, "", noise, "ScaleMed")
    unreal.MaterialEditingLibrary.connect_material_expressions(amp_large, "", noise, "AmpLarge")
    unreal.MaterialEditingLibrary.connect_material_expressions(scale_large, "", noise, "ScaleLarge")
    unreal.MaterialEditingLibrary.connect_material_expressions(amp_xlarge, "", noise, "AmpXLarge")
    unreal.MaterialEditingLibrary.connect_material_expressions(scale_xlarge, "", noise, "ScaleXLarge")
    unreal.MaterialEditingLibrary.connect_material_property(
        noise, "", unreal.MaterialProperty.MP_NORMAL
    )

    # The perturbed normal is world-space, so the Normal input must not be treated as tangent-space.
    material.set_editor_property("tangent_space_normal", False)
    material.set_editor_property("two_sided", False)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterTerrain")


# 4x4 ordered (Bayer) dither, keyed by absolute screen pixel position. This reproduces the
# original SimCopter effect look: a 1996 8-bit palettized renderer faked transparency by dithering
# solid palette pixels in an ordered pattern, so the spray/fire/water are clouds of hard-edged
# dithered specks, not alpha-blended blobs. Alpha (0..1) sets the fraction of pixels kept.
PARTICLE_DITHER_CODE = (
    "float2 sp = floor(Parameters.SvPosition.xy);\n"
    "float col = fmod(sp.x, 4.0);\n"
    "float row = fmod(sp.y, 4.0);\n"
    "int idx = (int)(row * 4.0 + col);\n"
    "float bayer[16] = {0.5,8.5,2.5,10.5, 12.5,4.5,14.5,6.5, 3.5,11.5,1.5,9.5, 15.5,7.5,13.5,5.5};\n"
    "float t = bayer[idx] / 16.0;\n"
    "return Alpha >= t ? 1.0 : 0.0;"
)


def create_particle_fx_material():
    """Default effect card: unlit, MASKED, ordered-dither transparency (the authentic original look).

    Reproduces the original SimCopter effect primitives (FIREPTS point sprites + the Maxis face
    type 0x17 effect cards): flat, palette-coloured, camera-facing, and made transparent by ORDERED
    DITHERING in the palettized software renderer (see FUN_0046edb0 / FUN_0048e0b0). There is no
    sprite atlas - the colour is a SIM3D palette index - so the remake writes that palette colour
    (plus a per-card alpha) into vertex colour and the material keeps a Bayer-dithered fraction of
    pixels (Masked blend), giving the characteristic hard dithered specks. Soft alpha blending is
    available separately as M_SimCopterParticleFXSoft (off by default)."""
    material = create_or_load_material("M_SimCopterParticleFX")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    vertex_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexColor, -700, 0
    )

    boost = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -400, -160
    )
    boost.set_editor_property("const_b", 1.4)
    # The VertexColor node's RGB comes from its default (unnamed) output; "RGB" is NOT a valid pin
    # name and silently fails to connect, leaving emissive black.
    unreal.MaterialEditingLibrary.connect_material_expressions(vertex_color, "", boost, "A")
    # ...and the boost reaches Emissive scaled into real nits, or the card is black under the day
    # sequence's 120,000 lux sun. See add_scene_scaled_emissive.
    add_scene_scaled_emissive(material, boost, "", -180, -160)

    dither = add_custom_node(
        material,
        "Bayer4x4Dither",
        PARTICLE_DITHER_CODE,
        ["Alpha"],
        -380,
        300,
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(vertex_color, "A", dither, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        dither, "", unreal.MaterialProperty.MP_OPACITY_MASK
    )

    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterParticleFX")


def create_particle_fx_soft_material():
    """Optional soft-edged effect card (translucent, radial alpha falloff). NOT the default - the
    original used dithering, not alpha blending - but available for a softer, more modern look by
    assigning it on the fire/particle components."""
    material = create_or_load_material("M_SimCopterParticleFXSoft")

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    vertex_color = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVertexColor, -700, 0
    )
    boost = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -400, -160
    )
    boost.set_editor_property("const_b", 1.4)
    # Use the VertexColor node's default (unnamed) RGB output; "RGB" is not a valid pin name and
    # silently fails, which is what left the soft particles emitting black.
    unreal.MaterialEditingLibrary.connect_material_expressions(vertex_color, "", boost, "A")
    # Scene-scaled for the same reason as the masked card above.
    add_scene_scaled_emissive(material, boost, "", -180, -160)

    tex_coord = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionTextureCoordinate, -700, 260
    )
    center = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant2Vector, -700, 400
    )
    center.set_editor_property("r", 0.5)
    center.set_editor_property("g", 0.5)
    distance = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionDistance, -520, 300
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(tex_coord, "", distance, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(center, "", distance, "B")
    scaled = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, -380, 300
    )
    scaled.set_editor_property("const_b", 2.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(distance, "", scaled, "A")
    inner = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionOneMinus, -250, 300
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(scaled, "", inner, "")
    mask = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionClamp, -130, 300
    )
    mask.set_editor_property("min_default", 0.0)
    mask.set_editor_property("max_default", 1.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(inner, "", mask, "")
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionMultiply, 20, 200
    )
    unreal.MaterialEditingLibrary.connect_material_expressions(mask, "", opacity, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(vertex_color, "A", opacity, "B")
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY
    )

    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterParticleFXSoft")


def create_lit_vertex_color_material():
    material = create_or_load_material("M_SimCopterLitVertexColor")
    clear_expressions(material)

    vertex_color = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionVertexColor,
        -400,
        0,
    )

    # City family, and the VertexColor node's RGB output is its default (unnamed) output pin.
    #
    # This is the untextured half of the city - every flat palette-coloured building and road face -
    # so leaving it out would have left buildings standing out exactly as reported, just on fewer
    # faces. NOTE that it is ALSO the vehicles' material
    # (Docs/memory/simcopter-vehicle-material.md), so cars now sit under the same albedo ceiling as
    # the buildings. Roughness and specular were already shared with the city; only the ceiling is
    # new to them, and a stylised city reads better with its traffic on the same footing. The
    # vehicles' own MID still overrides Metallic on top, which is the one thing that separated them.
    add_family_shading_nodes(material, vertex_color, "", "City")

    material.set_editor_property("two_sided", False)
    # The instanced BUILDING components draw with this material, so the flag has to be baked in.
    # It was missing here for a long time without anyone noticing, because the engine patches the
    # flag on at runtime and the asset had been saved with it - which only holds until somebody
    # deletes and regenerates the material, at which point the map check starts warning and a
    # cooked build renders it wrong. Same reasoning as create_lit_texture_material's.
    material.set_editor_property("used_with_instanced_static_meshes", True)
    # See note in create_lit_texture_material: needed for the Nanite render path.
    material.set_editor_property("used_with_nanite", True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(f"{MATERIAL_DIR}/M_SimCopterLitVertexColor")


ensure_directory(MATERIAL_DIR)
# FIRST, before any material that reads a collection scalar - which is now most of them.
#
# A CollectionParameter node resolves its ParameterId from the name THROUGH the collection, so if
# the collection is missing the parameter at the moment the node is created, the node keeps a null
# name and compiles to a constant 0. It is not enough for the collection asset to exist: adding a
# new scalar here and reading it from a material built earlier in this same script binds nothing,
# silently, and a zero albedo ceiling is a black building. That is exactly what happened when the
# City family went in with this call still further down.
create_day_night_parameter_collection()

create_if_missing("M_SimCopterLitTexture", create_lit_texture_material)
create_if_missing("M_SimCopterLitVertexColor", create_lit_vertex_color_material)
create_if_missing("M_SimCopterRotorDisc", create_rotor_disc_material)
# PP200's fan disc uses the existing rotor material on an InstancedStaticMeshComponent. This must
# run even when create_if_missing preserves the hand-tuned graph: the first city-side implementation
# only put the flag in create_rotor_disc_material, so the already-existing asset never received it
# and Unreal replaced the fan with its opaque default material.
ensure_instanced_static_mesh_usage("M_SimCopterRotorDisc")
create_if_missing("M_SimCopterSpriteTexture", create_sprite_texture_material)
# Not in the delete-and-recreate list below: MI_CityImage_* instances hold a hard reference to this
# parent, and deleting the asset would null it out. To re-tune it, delete it by hand and re-run -
# the re-parent pass at the end re-attaches the instances.
create_if_missing("M_SimCopterLitSpriteTexture", create_lit_sprite_texture_material)
# The water material's shader is fully defined here and still being tuned, so rebuild it every run.
# Delete any existing asset first and recreate it fresh: reloading an existing material and clearing
# its expressions asserts (!IsRooted in DeleteMaterialExpression) in this engine build, whereas a
# freshly created material has no expressions to clear. The asset keeps the same /Game path, so the
# renderer's ConstructorHelpers reference still resolves.
for _tuned in ("M_SimCopterCityAtlas", "M_SimCopterWater", "M_SimCopterTerrain", "M_SimCopterParticleFX", "M_SimCopterParticleFXSoft"):
    if unreal.EditorAssetLibrary.does_asset_exist(f"{MATERIAL_DIR}/{_tuned}"):
        unreal.EditorAssetLibrary.delete_asset(f"{MATERIAL_DIR}/{_tuned}")
create_city_atlas_material()
create_water_material()
create_terrain_material()
create_particle_fx_material()
create_particle_fx_soft_material()
upgrade_sprite_texture_emissive()
reparent_baked_direct_image_instances()
