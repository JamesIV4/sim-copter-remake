"""Check that the material the figure heads now use is a LIT card, and carries the two parameters
the C++ writes on it.

The heads moved off the unlit `M_SimCopterSpriteTexture` (whose baked 26000-nit EmissiveNits default
had to be re-derived from the key light every frame, and which read as BLACK once that computed
value fell to zero after dark) and onto the city's lit sprite-card material. Nothing in C++ can tell
you whether that asset is actually Default Lit, so ask the editor.

    UnrealEditor-Cmd.exe <uproject> -unattended -nop4 -nosplash -stdout -FullStdOutLogOutput \
        -ExecutePythonScript="S:\\Repos\\sim-copter-remake\\Docs\\scratchpad\\verify_figure_head_material.py"
"""
import unreal

PATH = "/Game/Materials/M_SimCopterLitSpriteTexture"

material = unreal.EditorAssetLibrary.load_asset(PATH)
if material is None:
    unreal.log_error("HEAD MATERIAL: %s does not exist - the heads would fall back to no material." % PATH)
else:
    shading = material.get_editor_property("shading_model")
    blend = material.get_editor_property("blend_mode")
    unreal.log("HEAD MATERIAL: shading_model=%s blend_mode=%s two_sided=%s" % (
        shading, blend, material.get_editor_property("two_sided")))

    names = set()
    for expression in unreal.MaterialEditingLibrary.get_material_expressions(material):
        if isinstance(expression, (unreal.MaterialExpressionScalarParameter,
                                   unreal.MaterialExpressionTextureSampleParameter2D)):
            names.add(str(expression.get_editor_property("parameter_name")))
    unreal.log("HEAD MATERIAL: parameters = %s" % sorted(names))

    for required in ("Texture", "CardNormalUpBias"):
        if required in names:
            unreal.log("HEAD MATERIAL: OK - '%s' is present." % required)
        else:
            unreal.log_error("HEAD MATERIAL: MISSING '%s'; the C++ write would be silently ignored." % required)

    if shading != unreal.MaterialShadingModel.MSM_DEFAULT_LIT:
        unreal.log_error("HEAD MATERIAL: not Default Lit - heads would not shade with the city.")
    if "EmissiveNits" in names:
        unreal.log_error("HEAD MATERIAL: carries EmissiveNits; this was supposed to be the lit one.")
