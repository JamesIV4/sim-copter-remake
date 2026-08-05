"""Load CityRender and report what the city actor actually built, so "it compiles" is not the only
evidence that trees came out of the merged mesh.

    UnrealEditor-Cmd <uproject> -ExecutePythonScript="<repo>/Docs/scratchpad/verify_tree_instances.py"
"""

import unreal

LEVEL = "/Game/CityRender"


def main():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)

    actors = unreal.GameplayStatics.get_all_actors_of_class(
        unreal.EditorLevelLibrary.get_editor_world(), unreal.SimCity2000CityActor
    )
    if not actors:
        unreal.log_error("VERIFY: no ASimCity2000CityActor in the level")
        return

    for actor in actors:
        name = actor.get_name()
        buildings_models = actor.get_editor_property("last_building_model_count")
        buildings = actor.get_editor_property("last_building_instance_count")
        natural_models = actor.get_editor_property("last_natural_object_model_count")
        natural = actor.get_editor_property("last_natural_object_instance_count")
        triangles = actor.get_editor_property("last_original_mesh_triangle_count")
        unreal.log(
            f"VERIFY {name}: buildings={buildings_models} models / {buildings} placements, "
            f"naturalObjects={natural_models} models / {natural} placements, "
            f"mergedMeshTriangles={triangles}"
        )

        ism = [c for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent)]
        static_ism = [c for c in ism if c.get_editor_property("mobility") == unreal.ComponentMobility.STATIC]
        unreal.log(
            f"VERIFY {name}: {len(ism)} instanced components total, {len(static_ism)} of them STATIC "
            f"(static is what lets the virtual shadow map cache their pages)"
        )


main()
