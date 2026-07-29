import unreal

LEVEL_PATH = "/Game/MainMenu"
GAME_MODE = "/Script/SimCopterRemake.SimCopterMainMenuGameMode"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
    print("MainMenu level already exists; loading it")
    les.load_level(LEVEL_PATH)
else:
    print("Creating empty level at %s" % LEVEL_PATH)
    les.new_level(LEVEL_PATH)

actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actor_subsystem.get_all_level_actors()
print("level actors: %s" % [a.get_class().get_name() for a in actors])

game_mode_class = unreal.load_class(None, GAME_MODE)
print("game mode class: %s" % game_mode_class)

world_settings = None
for a in actors:
    if isinstance(a, unreal.WorldSettings):
        world_settings = a
        break

if world_settings is None:
    # WorldSettings is not always returned as a level actor; reach it through the world.
    world = les.get_current_level().get_outer()
    print("world: %s" % world)
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WorldSettings):
        world_settings = a
        break

if world_settings is None:
    print("FAILED: could not find WorldSettings")
else:
    world_settings.set_editor_property("default_game_mode", game_mode_class)
    print("set default_game_mode on %s" % world_settings.get_name())

# A PlayerStart keeps the engine from complaining and gives the front end a view point.
has_start = any(isinstance(a, unreal.PlayerStart) for a in actor_subsystem.get_all_level_actors())
if not has_start:
    start = actor_subsystem.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 200))
    print("spawned PlayerStart: %s" % start)

les.save_current_level()
print("SAVED %s" % LEVEL_PATH)
