"""Delete the `create_if_missing` materials so CreateSimCopterMaterials.py rebuilds them.

These four are NOT in the script's delete-and-recreate list, so editing their generator does
nothing until the asset is gone:

  M_SimCopterLitSpriteTexture   trees and signs   (MI_CityImage_* hard-reference it as parent)
  M_SimCopterLitTexture         direct-image building faces + the hangar shell
  M_SimCopterLitVertexColor     every flat palette-coloured city face - AND the vehicles
  M_SimCopterSpriteTexture      unlit effect cards (left alone by default; nothing here edits it)

The documented re-tune flow is: delete by hand, re-run the create script, then BakeCityAtlas.py.
After a delete, run repair_city_image_parents.py - the create script's re-parent pass only rescues
instances still on the OLD unlit parent, and a delete leaves them on null.
"""

import unreal

ASSETS = (
    "/Game/Materials/M_SimCopterLitSpriteTexture",
    "/Game/Materials/M_SimCopterLitTexture",
    "/Game/Materials/M_SimCopterLitVertexColor",
)

for asset in ASSETS:
    if not unreal.EditorAssetLibrary.does_asset_exist(asset):
        unreal.log(f"MATERIAL DELETE: {asset} already absent")
        continue
    if unreal.EditorAssetLibrary.delete_asset(asset):
        unreal.log(f"MATERIAL DELETE: deleted {asset}")
    else:
        unreal.log_error(f"MATERIAL DELETE: could not delete {asset}")

unreal.log("MATERIAL DELETE: now re-run CreateSimCopterMaterials.py, then BakeCityAtlas.py")
