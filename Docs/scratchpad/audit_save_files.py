import unreal
from pathlib import Path
for p in Path(unreal.Paths.project_saved_dir()).joinpath('SaveGames').glob('SimCopter_*.sav'):
    obj = unreal.GameplayStatics.load_game_from_slot(p.stem, 0)
    if obj is None:
        unreal.log('SAVE_AUDIT unreadable '+p.name)
        continue
    fields=['display_name','format_version','kind','saved_at_utc','has_time_of_day_state','time_of_day_hours','cash','score','has_runtime_world_state']
    values={}
    for field in fields:
        try: values[field]=str(obj.get_editor_property(field))
        except Exception as e: values[field]=str(e)
    unreal.log('SAVE_AUDIT '+p.name+' '+str(values))
