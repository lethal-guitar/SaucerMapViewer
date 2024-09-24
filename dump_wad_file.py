#!/usr/bin/env python3
# Copyright (C) 2024, Nikolai Wuttke-Hohendorf. All rights reserved.
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.


import base64
import io
import json
import os
import shutil
import struct
import sys

from dataclasses import dataclass
from enum import auto, Enum
from pathlib import Path

from PIL import Image


@dataclass
class BitmapHeader:
    offset: int
    width: int
    height: int


@dataclass
class TextureDefinition:
    uvs: tuple
    bitmap_index: int
    unknown1: int
    unknown2: int
    flags: int

    @property
    def is_masked(self):
        return (self.flags & 1) != 0


@dataclass
class ModelHeader:
    offset1: int
    offset2: int


class FaceType(Enum):
    triangle = auto()
    quad = auto()


@dataclass
class ModelFace:
    texture_index: int
    indices: tuple
    face_type: FaceType
    is_masked: bool


@dataclass
class ModelData:
    vertices: tuple
    faces: tuple
    matrix: tuple


@dataclass
class SoundHeader:
    offset_formatinfo: int
    offset_data: int
    data_len: int


@dataclass(init=False, eq=False)
class WadData:
    version: int
    background_color_index: int
    depth_near: int
    depth_far: int

    bitmap_table: list
    map_specific_texture_dict: dict
    texture_defs: list
    model_dict: dict
    sound_dict: dict
    named_texture_dict: dict

    distance_color_lut: tuple
    blend_color_lut: tuple

    language_data: list

    blob1: bytes
    blob2: bytes
    blob3: bytes

    color_palette: tuple
    packed_data: bytes

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.map_specific_texture_dict = {}
        self.named_texture_dict = {}
        self.model_dict = {}
        self.sound_dict = {}

    def read_packed_data(self, offset, size):
        return self.packed_data[offset:offset + size]

    def extract_bitmap(self, index):
        header = self.bitmap_table[index]

        num_pixels = header.width * header.height
        pixels = self.read_packed_data(header.offset, num_pixels)
        rgba_pixels = tuple(
            self.color_palette[index] + (255 if index > 0 else 0,)
            for index in pixels
        )

        image = Image.new('RGBA', (header.width, header.height))
        image.putdata(rgba_pixels)
        return image

    def extract_sound(self, name=None, header=None):
        if header is None:
            header = self.sound_dict[name]

        formatinfo = self.read_packed_data(header.offset_formatinfo, 16)
        data = self.read_packed_data(header.offset_data, header.data_len)

        # The packed data already contains complete and valid WAVE files, but
        # the header points to specific places within those instead of to the
        # beginning and end of the entire file. We could find the starting
        # position by subtracting a fixed number of bytes from the first offset,
        # but by reconstructing the surrounding pieces of data our code is more
        # robust. It's theoretically possible for the game data to omit parts
        # of the WAVE file header and still work, so by doing it this way we
        # also support that (although this is never the case in the actual game
        # files).
        wav_data = b"RIFF"
        wav_data += struct.pack("<I", header.data_len + 44)
        wav_data += b"WAVEfmt "
        wav_data += struct.pack("<I", 16)
        wav_data += formatinfo
        wav_data += b"data"
        wav_data += struct.pack("<I", header.data_len)
        wav_data += data

        return wav_data

    def get_model_data(self, name=None, model_header=None):
        if model_header is None:
            model_header = self.model_dict[name]

        model_data = self.read_packed_data(model_header.offset1 + 40, 16)
        num_vertices, vertices_offset, num_faces, faces_offset = \
            struct.unpack("<IIII", model_data)

        def parse_model_face(data):
            texture_index, i0, i1, i2, i3, face_type = \
                struct.unpack("<IHHHHH", data)

            is_triangle = face_type == 0x1000

            return ModelFace(
                texture_index=texture_index,
                indices=(i0, i1, i2) + ((i3,) if not is_triangle else ()),
                face_type=FaceType.triangle if is_triangle else FaceType.quad,
                is_masked=self.texture_defs[texture_index].is_masked,
            )

        vertices = tuple(
            struct.unpack(
                "<hhh",
                self.read_packed_data(vertices_offset + offset, 6)
            )
            for offset in range(0, num_vertices * 6, 6)
        )
        faces = tuple(
            parse_model_face(self.read_packed_data(faces_offset + offset, 14))
            for offset in range(0, num_faces * 32, 32)
        )

        matrix_data = self.read_packed_data(model_header.offset2, 24)
        matrix = struct.unpack("<12h", matrix_data)

        return ModelData(vertices=vertices, faces=faces, matrix=matrix)


def read_u32(f):
    return struct.unpack("<I", f.read(4))[0]


def read_string(f, max_size):
    string_raw = f.read(max_size)
    return string_raw.decode("ASCII").rstrip("\0")


def read_sized_blob(f):
    size = read_u32(f)
    return f.read(size)


def read_texture_def(f):
    raw_data = struct.unpack("<BBHBBHBBHBBH", f.read(16))
    uvs = tuple((raw_data[i], raw_data[i + 1]) for i in (0, 3, 6, 9))

    return TextureDefinition(
        uvs=uvs,
        bitmap_index=raw_data[2],
        unknown1=raw_data[5],
        unknown2=raw_data[8],
        flags=raw_data[11],
    )


def read_wad_file(file_path):
    data = WadData()

    with open(file_path, "rb") as f:
        # Color tables and WAD header
        data.distance_color_lut = tuple(f.read(256 * 1024))
        data.blend_color_lut = tuple(f.read(256**2 * 16))

        info = read_u32(f)
        packed_data_size = info & 0xFFFFFF

        data.version = info >> 24
        data.background_color_index = read_u32(f)
        data.depth_near = read_u32(f)
        data.depth_far = read_u32(f)


        # Unknown data blocks
        data.blob1 = read_sized_blob(f)
        data.blob2 = read_sized_blob(f)


        # Language data (text in different languages)
        def read_lang_table(num_entries):
            return struct.unpack(f"<{num_entries}H", f.read(num_entries * 2))

        lang_headers = [struct.unpack("<HHII", f.read(12)) for i in range(7)]
        lang_tables = [read_lang_table(header[1]) for header in lang_headers]
        lang_data_packed = read_sized_blob(f)

        data.language_data = []

        for i, header in enumerate(lang_headers):
            data.language_data.append((
                header[0],
                lang_tables[i],
                lang_data_packed[header[2]:header[3]],
            ))


        # Unknown data block
        data.blob3 = read_sized_blob(f)


        # Unknown data, seems unused - skip
        f.seek(4, 1)
        unknown_table_len = read_u32(f)
        f.seek(unknown_table_len * 4, 1)


        # Bitmap headers
        num_bitmaps = read_u32(f)
        data.bitmap_table = [
            BitmapHeader(*struct.unpack("<I4xHH", f.read(12)))
            for i in range(num_bitmaps)
        ]


        # Map-specific texture bitmap names (exported textures)
        num_map_specific_textures = read_u32(f)

        for i in range(num_map_specific_textures):
            index = read_u32(f)
            name = read_string(f, 16)
            data.map_specific_texture_dict[name] = index


        # Texture definitions
        num_texture_defs = read_u32(f)
        data.texture_defs = \
            [read_texture_def(f) for i in range(num_texture_defs)]


        # Unknown asset type headers
        num_unknown_assets = read_u32(f)
        f.seek(num_unknown_assets * 28, 1)


        # Model headers
        num_models = read_u32(f)
        model_names = [read_string(f, 16) for i in range(num_models)]

        for i in range(num_models):
            header_data = struct.unpack("<I8xI20x", f.read(36))
            data.model_dict[model_names[i]] = ModelHeader(
                offset1=header_data[0],
                offset2=header_data[1],
            )


        # Sound headers
        num_sounds = read_u32(f)
        sound_names = [read_string(f, 16) for i in range(num_sounds)]

        for i in range(num_sounds):
            header_data = struct.unpack("<III104x", f.read(116))

            data.sound_dict[sound_names[i]] = SoundHeader(
                offset_formatinfo=header_data[0],
                offset_data=header_data[1],
                data_len=header_data[2],
            )


        # Palette table (always [0, -1, -1, -1, -1, -1] - skip
        f.seek(5 * 4, 1)


        # Named texture table
        num_named_textures = read_u32(f)

        for i in range(num_named_textures):
            index = read_u32(f)
            name = read_string(f, 20)
            data.named_texture_dict[name] = index


        # Debug (?) names, unused by the game - skip
        num_debug_names = read_u32(f)
        f.seek(num_debug_names * 16, 1)


        # More unknown data - skip
        count = read_u32(f)
        f.seek((num_debug_names - count) * 4 + 8, 1)


        # Packed data and color palette
        data.packed_data = f.read(packed_data_size)
        data.color_palette = tuple(
            struct.unpack("<BBBx", data.packed_data[i:i+4])
            for i in range(0, 256 * 4, 4)
        )

    return data



TEXTURE_SIZE = 256


class Mesh:
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.vertices = bytes()
        self.indices = bytes()
        self.min_coords = [sys.float_info.max] * 3
        self.max_coords = [-sys.float_info.max] * 3
        self.min_uv = [1.0, 1.0]
        self.max_uv = [0.0, 0.0]
        self.next_index = 0
        self.buffer_offset_vertices = 0
        self.buffer_offset_indices = 0

    @property
    def max_index(self):
        return self.next_index - 1

    def add_data(self, vertices, uvs):
        max_before = list(self.max_coords)
        for vertex in vertices:
            for i in range(3):
                self.min_coords[i] = min(self.min_coords[i], vertex[i])
                self.max_coords[i] = max(self.max_coords[i], vertex[i])

        for uv in uvs:
            for i in range(2):
                self.min_uv[i] = min(self.min_uv[i], uv[i])
                self.max_uv[i] = max(self.max_uv[i], uv[i])

        for vertex, uv in zip(vertices, uvs):
            self.vertices += struct.pack("<5f", *vertex, *uv)

        # Output indices in counter-clockwise order for OpenGL
        if len(vertices) == 3:
            indices = (0, 2, 1)
        else:
            indices = (0, 3, 1, 1, 3, 2)

        indices_absolute = tuple(idx + self.next_index for idx in indices)
        self.indices += struct.pack(f"<{len(indices)}H", *indices_absolute)
        self.next_index += len(vertices)


def collect_bitmaps_used(wad_data, faces):
    bitmaps_used = set()

    for face in faces:
        bitmap_index = wad_data.texture_defs[face.texture_index].bitmap_index
        bitmaps_used.add(bitmap_index)

    return list(bitmaps_used)


def build_uv_offset_dict(wad_data, faces, bitmaps_list):
    bitmap_idx_dict = {}

    for i, bitmap_index in enumerate(bitmaps_list):
        bitmap_idx_dict[bitmap_index] = i


    uv_offset_dict = {}

    for face in faces:
        bitmap_index = wad_data.texture_defs[face.texture_index].bitmap_index

        uv_offset_dict[face.texture_index] = \
            bitmap_idx_dict[bitmap_index] / len(bitmaps_list)

    return uv_offset_dict


def build_texture_atlas(wad_data, bitmaps):
    atlas = Image.new('RGBA', (TEXTURE_SIZE * len(bitmaps), TEXTURE_SIZE))

    for index, bitmap_idx in enumerate(bitmaps):
        sub_image = wad_data.extract_bitmap(bitmap_idx)

        atlas.paste(sub_image, (index * TEXTURE_SIZE, 0))

    return atlas


def encode_matrix(matrix):
    return \
        [v / 512.0 for v in matrix[0:3]] + [0.0] + \
        [v / 512.0 for v in matrix[3:6]] + [0.0] + \
        [v / 512.0 for v in matrix[6:9]] + [0.0] + \
        [float(v) for v in matrix[9:12]] + [1.0]


def build_gltf(texture_atlas_image, solid_mesh, masked_meshes, matrix):
    all_meshes = [(solid_mesh, False)] + \
        [(mesh, True) for mesh in masked_meshes]


    # Encode buffer
    data_buffer = bytes()
    for mesh, _ in all_meshes:
        data_buffer += mesh.vertices

    vertex_buffer_size = len(data_buffer)

    for mesh, _ in all_meshes:
        data_buffer += mesh.indices

    index_buffer_size = len(data_buffer) - vertex_buffer_size

    encoded_buffer = base64.b64encode(data_buffer).decode("ASCII")


    # Encode texture atlas image
    image_buffer = io.BytesIO(bytes())
    texture_atlas_image.save(image_buffer, format="png")
    encoded_image = base64.b64encode(image_buffer.getvalue()).decode("ASCII")


    # Setup glTF structure
    gltf = {
        "asset": {
            "version": "2.0"
        },
        "scene": 0,
        "scenes": [
            {
                "nodes": [ 0 ]
            },
        ],
        "nodes": [
            {
                "mesh": 0,
                "matrix": encode_matrix(matrix),
            },
        ],
        "meshes": [
            {
                "primitives": [],
            },
        ],
        "materials": [
            {
                "pbrMetallicRoughness": {
                    "baseColorTexture": {
                        "index": 0
                    },
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0
                },
            },
        ],
        "textures": [
            {
                "sampler": 0,
                "source": 0
            },
        ],
        "samplers": [
            {
                "magFilter": 0x2600, # GL_NEAREST
                "minFilter": 0x2600, # GL_NEAREST
                "wrapS": 33648,
                "wrapT": 33648
            },
        ],
        "bufferViews": [
            {
                "buffer": 0,
                "byteOffset": 0,
                "byteLength": vertex_buffer_size,
                "byteStride": 5*4,
                "target": 34962
            },
            {
                "buffer": 0,
                "byteOffset": vertex_buffer_size,
                "byteLength": index_buffer_size,
                "target": 34963
            }
        ],
        "accessors": [],
        "images": [
            {
                "uri": "data:image/png;base64," + encoded_image,
            },
        ],
        "buffers": [
            {
                "uri": "data:application/gltf-buffer;base64," + encoded_buffer,
                "byteLength": len(data_buffer),
            },
        ],
    }

    if len(masked_meshes) > 0:
        gltf["materials"].append(
            {
                "pbrMetallicRoughness": {
                    "baseColorTexture": {
                        "index": 0
                    },
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0
                },
                "alphaMode": "MASK",
            }
        )


    # Add meshes
    mesh_num = 0
    buffer_offset_vertices = 0
    buffer_offset_indices = 0

    for mesh, is_masked in all_meshes:
        num_vertices = len(mesh.vertices) // (5 * 4)

        # The solid mesh can be empty in some cases
        if not num_vertices:
            continue

        offset = 3 * mesh_num
        gltf["meshes"][0]["primitives"].append({
            "attributes": {
                "POSITION": 0 + offset,
                "TEXCOORD_0": 1 + offset
                },
            "indices": 2 + offset,
            "material": 1 if is_masked else 0,
        })

        gltf["accessors"].append({
          "bufferView": 0,
          "byteOffset": buffer_offset_vertices,
          "componentType": 5126,
          "count": num_vertices,
          "type": "VEC3",
          "min": mesh.min_coords,
          "max": mesh.max_coords,
        })
        gltf["accessors"].append({
          "bufferView": 0,
          "byteOffset": buffer_offset_vertices + 3 * 4,
          "componentType": 5126,
          "count": num_vertices,
          "type": "VEC2",
          "min": mesh.min_uv,
          "max": mesh.max_uv,
        })
        gltf["accessors"].append({
          "bufferView": 1,
          "byteOffset": buffer_offset_indices,
          "componentType": 5123,
          "count": len(mesh.indices) // 2,
          "type": "SCALAR",
          "min": [ 0 ],
          "max": [ mesh.max_index ],
        })

        buffer_offset_vertices += len(mesh.vertices)
        buffer_offset_indices += len(mesh.indices)
        mesh_num += 1


    return gltf


def adjust_coordinates(vertex):
    # Convert from the game's coordinate system to OpenGL coordinates
    return (vertex[0], -vertex[1], -vertex[2])


def export_gltf(wad_data, model_data, file_name):
    bitmaps_list = collect_bitmaps_used(wad_data, model_data.faces)

    uv_offset_dict = \
        build_uv_offset_dict(wad_data, model_data.faces, bitmaps_list)


    def build_mesh(faces):
        mesh = Mesh()

        for face in faces:
            num_vertices = len(face.indices)
            vertices = tuple(
                adjust_coordinates(model_data.vertices[i])
                for i in face.indices
            )

            texture_def = wad_data.texture_defs[face.texture_index]
            uv_offset = uv_offset_dict[face.texture_index]

            def convert_uv(uv):
                normalized_uv = tuple(val / (TEXTURE_SIZE - 1.0) for val in uv)
                return (
                    normalized_uv[0] / len(bitmaps_list) + uv_offset,
                    normalized_uv[1]
                )

            uvs = tuple(
                convert_uv(texture_def.uvs[i])
                for i in range(num_vertices)
            )

            mesh.add_data(vertices, uvs)

        return mesh


    faces = [f for f in model_data.faces if not f.is_masked]
    masked_faces = [f for f in model_data.faces if f.is_masked]

    solid_mesh = build_mesh(faces)
    masked_meshes = [build_mesh((face,)) for face in masked_faces]

    atlas_image = build_texture_atlas(wad_data, bitmaps_list)
    gltf = build_gltf(atlas_image, solid_mesh, masked_meshes, model_data.matrix)

    with open(file_name, "w") as f:
        f.write(json.dumps(gltf))
        f.write("\n")


def dump_wad(wad_data, output_dir):
    num_bitmaps = len(wad_data.bitmap_table)

    texture_names = tuple(wad_data.map_specific_texture_dict.keys())
    num_textures = len(texture_names)
    num_non_texture_bitmaps = num_bitmaps - len(texture_names)

    print(f"Extracting {num_textures} textures...")

    os.mkdir(output_dir / "textures")

    for i in range(num_textures):
        bitmap = wad_data.extract_bitmap(i)
        bitmap.save(output_dir / "textures" / f"t{i}_{texture_names[i]}.png")


    print(f"Extracting {num_non_texture_bitmaps} bitmaps...")

    os.mkdir(output_dir / "bitmaps")

    for i in range(num_textures, num_bitmaps):
        bitmap = wad_data.extract_bitmap(i)
        num = i - num_textures
        bitmap.save(output_dir / "bitmaps" / f"bitmap_{num}.png")


    print(f"Extracting {len(wad_data.sound_dict)} sounds...")

    os.mkdir(output_dir / "sounds")

    for name, header in wad_data.sound_dict.items():
        wav_data = wad_data.extract_sound(header=header)

        with open(output_dir / "sounds" / f"{name}.wav", "wb") as f:
            f.write(wav_data)


    print(f"Extracting {len(wad_data.model_dict)} models...")

    os.mkdir(output_dir / "models")

    for name, header in wad_data.model_dict.items():
        model_data = wad_data.get_model_data(model_header=header)

        export_gltf(
            wad_data, model_data, output_dir / "models" / f"{name}.gltf")

    with open(output_dir / "blob1.bin", "wb") as f:
        f.write(wad_data.blob1)

    with open(output_dir / "blob2.bin", "wb") as f:
        f.write(wad_data.blob2)

    with open(output_dir / "blob3.bin", "wb") as f:
        f.write(wad_data.blob3)


def main():
    print("Attack of the Saucerman WAD file exporter")
    print("Copyright (C) 2024 Nikolai Wuttke-Hohendorf")
    print()


    try:
        wad_file_path = Path(sys.argv[1])
    except IndexError:
        print(f"Usage: {sys.argv[0]} <wad_file>")
        return 1


    print(f"Reading {wad_file_path}...")
    wad_data = read_wad_file(wad_file_path)

    print(f"WAD Version {wad_data.version}")


    output_dir = Path(wad_file_path.stem + "_wad_exported")

    if output_dir.exists():
        print(f"Output directory '{output_dir}' already exists - overwrite?")

        response = input("Enter 'y'/'n': ")

        if response == "y":
            shutil.rmtree(output_dir)
        else:
            print("Aborting")
            return 2

    os.mkdir(output_dir)


    print("Extracting assets...")
    dump_wad(wad_data, output_dir)


    print("All done!")
    return 0


if __name__ == "__main__":
    exit(main())
