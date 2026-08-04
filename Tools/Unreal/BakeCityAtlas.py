"""Bakes the SimCopter city atlas pages into local UTexture2D + MaterialInstance assets.

Run inside the editor (it needs the editor's asset/import APIs and the project's parent
materials). The decoded pages are ORIGINAL GAME ART, so they are written into a gitignored
folder (/Game/Generated/CityAtlas -> Content/Generated/...) rather than committed; only the
project-authored parent materials live in source control.

What it produces:
  * T_CityPage_<id>      one full 256x256 atlas page per referenced SIM3D.BMP image, nearest
                         filtered + no mips so the renderer's per-cell UV math samples cleanly.
                         Page 20 is the SKY.BMP image-4 exception. T_TerrainLow is TILED1 image 0.
  * MI_CityPage_<id>     MaterialInstanceConstant (parent M_SimCopterCityAtlas) per page, used by
                         building/road faces (in-cell UV in TexCoord0, cell col/row in TexCoord1).
  * T/MI_CityImage_<id>  Direct SIM3D image textures for the sprite cards (face type 2 - trees and
                         signs) and the rare face type 13 geometry. These carry the palette-index-0
                         alpha key, hang off the masked LIT card parent, and preserve Maxis'
                         repeating raw UVs.
  * MI_TerrainLow/High   MaterialInstanceConstant (parent M_SimCopterLitTexture) for the terrain
                         surface, which already bakes page UVs on the CPU.

Run from the editor:  py "<repo>/Tools/Unreal/BakeCityAtlas.py"
or headless:          UnrealEditor-Cmd <uproject> -ExecutePythonScript="<repo>/Tools/Unreal/BakeCityAtlas.py"
"""

import os
import struct
import zlib

import unreal

OUTPUT_DIR = "/Game/Generated/CityAtlas"
ATLAS_MATERIAL = "/Game/Materials/M_SimCopterCityAtlas"
TERRAIN_MATERIAL = "/Game/Materials/M_SimCopterLitTexture"
# Masked, so palette index 0 punches out of tree/sign sprite cards, and LIT, so they track the
# day/night sequence with the rest of the city. The unlit M_SimCopterSpriteTexture that used to be
# here holds one fixed brightness, which reads inverted under a moving sun - dark trees at noon,
# glowing trees at midnight. It is still the right parent for fire and particle kernels.
SPRITE_MATERIAL = "/Game/Materials/M_SimCopterLitSpriteTexture"
SKY_PAGE_ID = 20          # face TextureAtlasIndex 20 resolves to SKY.BMP image 4, not SIM3D image 20
SKY_IMAGE_INDEX = 4
TERRAIN_HIGH_PAGE_ID = 13  # SIM3D.BMP image 13 doubles as the high terrain page (0x0d)


def reference_root():
    proj = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
    return os.path.normpath(os.path.join(proj, "..", "Reference", "SimCopterOriginalGame"))


def read_palette(geo_path):
    """256 RGB entries from a GEO/*.MAX CMAP block (bytes are R,G,B)."""
    d = open(geo_path, "rb").read()
    color_data_offset = struct.unpack_from("<I", d, 57)[0]
    return [tuple(d[color_data_offset + i * 3: color_data_offset + i * 3 + 3]) for i in range(256)]


def decode_composite(path, palette):
    """Decodes a Maxis composite bitmap into a list of (w, h, top-down RGB bytes) images,
    mirroring FMaxisTextureReader (per-row offset table, bottom-up rows flipped to top-down)."""
    d = open(path, "rb").read()
    image_count = struct.unpack_from("<i", d, 8)[0]
    resolution_count = struct.unpack_from("<i", d, 12)[0]
    cursor = 16 + resolution_count * 12
    images = []
    for _ in range(image_count):
        w, h, unk = struct.unpack_from("<iii", d, cursor)
        row_table = cursor + 12
        data_offset = row_table + h * 4
        pixel_count = w * h
        rgb = bytearray(pixel_count * 3)
        # Palette index 0 is the Maxis transparency key - the original's sprite blitter skips it.
        # Direct SIM3D images are drawn as cards (face types 2 and 13), so without this mask a
        # tree sprite renders as an opaque black slab instead of foliage.
        alpha = bytearray(pixel_count)
        for row in range(h):
            row_offset = struct.unpack_from("<i", d, row_table + row * 4)[0]
            dest_row = h - 1 - row
            base = data_offset + row_offset
            dst = dest_row * w * 3
            adst = dest_row * w
            for col in range(w):
                index = d[base + col]
                r, g, b = palette[index]
                rgb[dst] = r
                rgb[dst + 1] = g
                rgb[dst + 2] = b
                alpha[adst + col] = 0 if index == 0 else 255
                dst += 3
        images.append((w, h, bytes(rgb), bytes(alpha)))
        cursor = data_offset + pixel_count
    return images


def write_png(path, width, height, rgb, alpha=None):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    if alpha is None:
        stride = width * 3
        for y in range(height):
            raw.append(0)  # filter type 0
            raw.extend(rgb[y * stride:(y + 1) * stride])
        color_type = 2  # 8-bit RGB
    else:
        stride = width * 3
        for y in range(height):
            raw.append(0)
            row = rgb[y * stride:(y + 1) * stride]
            arow = alpha[y * width:(y + 1) * width]
            for x in range(width):
                raw.extend(row[x * 3:x * 3 + 3])
                raw.append(arow[x])
        color_type = 6  # 8-bit RGBA
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def import_texture(png_path, asset_name):
    task = unreal.AssetImportTask()
    task.filename = png_path
    task.destination_path = OUTPUT_DIR
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = unreal.TextureFactory()
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    asset_path = f"{OUTPUT_DIR}/{asset_name}"
    texture = unreal.EditorAssetLibrary.load_asset(asset_path)
    # Uncompressed sRGB, nearest, no mips: the per-cell UV math relies on exact texels with no
    # DXT block bleed or mip averaging between neighbouring 32x32 cells.
    texture.set_editor_property("srgb", True)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    return texture


def create_material_instance(asset_name, parent_path, texture, identify_mesh_water_cells=False):
    asset_path = f"{OUTPUT_DIR}/{asset_name}"
    parent = unreal.EditorAssetLibrary.load_asset(parent_path)
    existing = unreal.EditorAssetLibrary.load_asset(asset_path)
    if existing:
        mic = existing
    else:
        mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset_name, OUTPUT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew()
        )
    mic.set_editor_property("parent", parent)
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mic, "Texture", texture)
    # Legacy parameter name retained so existing page-20 instances keep working. It now identifies
    # static pool/pond cells for the material's constant depth offset; it no longer animates UVs.
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(
        mic, "AnimateWaterCells", 1.0 if identify_mesh_water_cells else 0.0
    )
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)


def main():
    root = reference_root()
    palette = read_palette(os.path.join(root, "GEO", "sim3d1.max"))
    sim3d = decode_composite(os.path.join(root, "BMP", "SIM3D.BMP"), palette)
    sky = decode_composite(os.path.join(root, "BMP", "SKY.BMP"), palette)
    tiled1 = decode_composite(os.path.join(root, "BMP", "TILED1.BMP"), palette)

    temp_dir = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), "CityAtlasBake")
    os.makedirs(temp_dir, exist_ok=True)
    if not unreal.EditorAssetLibrary.does_directory_exist(OUTPUT_DIR):
        unreal.EditorAssetLibrary.make_directory(OUTPUT_DIR)

    # Every 256x256 SIM3D image is an 8x8 atlas page addressable by face TextureAtlasIndex.
    # Atlas pages stay opaque: they are wall/roof surfaces, not keyed sprites.
    page_sources = {}
    for index, (w, h, rgb, _alpha) in enumerate(sim3d):
        if w == 256 and h == 256:
            page_sources[index] = (w, h, rgb)
    # The SKY.BMP image-4 exception overrides page 20 regardless of SIM3D image 20.
    sky_w, sky_h, sky_rgb, _sky_alpha = sky[SKY_IMAGE_INDEX]
    page_sources[SKY_PAGE_ID] = (sky_w, sky_h, sky_rgb)

    baked_pages = []
    for page_id, (w, h, rgb) in sorted(page_sources.items()):
        png = os.path.join(temp_dir, f"page_{page_id}.png")
        write_png(png, w, h, rgb)
        texture = import_texture(png, f"T_CityPage_{page_id}")
        # SCHOOK: FUN_004814c0. SKY.BMP image 4 (the page-20 exception) contains the exact same
        # water cells 0..9 as TILED1. Mesh-object ponds/pools use base cell 0 or 5 and therefore
        # advance with the terrain water instead of staying on their first frame.
        create_material_instance(
            f"MI_CityPage_{page_id}",
            ATLAS_MATERIAL,
            texture,
            identify_mesh_water_cells=(page_id == SKY_PAGE_ID),
        )
        baked_pages.append(page_id)

    # Direct images are drawn as keyed cards (face types 2 and 13), so they carry the index-0
    # alpha mask and hang off the masked sprite material rather than the opaque lit one.
    baked_direct_images = []
    for image_id, (w, h, rgb, alpha) in enumerate(sim3d):
        png = os.path.join(temp_dir, f"image_{image_id}.png")
        write_png(png, w, h, rgb, alpha)
        texture = import_texture(png, f"T_CityImage_{image_id}")
        create_material_instance(f"MI_CityImage_{image_id}", SPRITE_MATERIAL, texture)
        baked_direct_images.append(image_id)

    # Terrain surfaces sample full pages with CPU-baked UVs through M_SimCopterLitTexture.
    tw, th, trgb, _talpha = tiled1[0]
    terrain_png = os.path.join(temp_dir, "terrain_low.png")
    write_png(terrain_png, tw, th, trgb)
    terrain_low = import_texture(terrain_png, "T_TerrainLow")
    create_material_instance("MI_TerrainLow", TERRAIN_MATERIAL, terrain_low)

    terrain_high = unreal.EditorAssetLibrary.load_asset(f"{OUTPUT_DIR}/T_CityPage_{TERRAIN_HIGH_PAGE_ID}")
    if terrain_high:
        create_material_instance("MI_TerrainHigh", TERRAIN_MATERIAL, terrain_high)

    unreal.log(
        f"CITY ATLAS BAKE DONE: pages={baked_pages} directImages={len(baked_direct_images)} "
        f"terrainHigh={'yes' if terrain_high else 'no'}"
    )


main()
