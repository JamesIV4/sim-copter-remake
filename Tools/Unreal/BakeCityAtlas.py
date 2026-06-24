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
  * T/MI_CityImage_<id>  Direct SIM3D image textures for rare face type 13 geometry. These use the
                         regular lit texture parent and preserve Maxis' repeating raw UVs.
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
        for row in range(h):
            row_offset = struct.unpack_from("<i", d, row_table + row * 4)[0]
            dest_row = h - 1 - row
            base = data_offset + row_offset
            dst = dest_row * w * 3
            for col in range(w):
                r, g, b = palette[d[base + col]]
                rgb[dst] = r
                rgb[dst + 1] = g
                rgb[dst + 2] = b
                dst += 3
        images.append((w, h, bytes(rgb)))
        cursor = data_offset + pixel_count
    return images


def write_png(path, width, height, rgb):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter type 0
        raw.extend(rgb[y * stride:(y + 1) * stride])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))  # 8-bit RGB
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


def create_material_instance(asset_name, parent_path, texture):
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
    page_sources = {}
    for index, (w, h, rgb) in enumerate(sim3d):
        if w == 256 and h == 256:
            page_sources[index] = (w, h, rgb)
    # The SKY.BMP image-4 exception overrides page 20 regardless of SIM3D image 20.
    page_sources[SKY_PAGE_ID] = sky[SKY_IMAGE_INDEX]

    baked_pages = []
    for page_id, (w, h, rgb) in sorted(page_sources.items()):
        png = os.path.join(temp_dir, f"page_{page_id}.png")
        write_png(png, w, h, rgb)
        texture = import_texture(png, f"T_CityPage_{page_id}")
        create_material_instance(f"MI_CityPage_{page_id}", ATLAS_MATERIAL, texture)
        baked_pages.append(page_id)

    baked_direct_images = []
    for image_id, (w, h, rgb) in enumerate(sim3d):
        png = os.path.join(temp_dir, f"image_{image_id}.png")
        write_png(png, w, h, rgb)
        texture = import_texture(png, f"T_CityImage_{image_id}")
        create_material_instance(f"MI_CityImage_{image_id}", TERRAIN_MATERIAL, texture)
        baked_direct_images.append(image_id)

    # Terrain surfaces sample full pages with CPU-baked UVs through M_SimCopterLitTexture.
    tw, th, trgb = tiled1[0]
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
