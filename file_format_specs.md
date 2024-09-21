# Attack of the Saucerman file format specs (Windows version)

These specs are reverse engineered, and currently still work in progress (i.e., incomplete and possibly inaccurate in places.)

All files use little-endian encoding.

## Package file (`Saucerdata.pak`)

All of the game's asset are stored in a single package file `Saucerdata.pak`. This is just a Zip file, and can be unpacked using standard tools. Something seems slightly unusual about it, as 7zip specifically wasn't able to unpack all contained files correctly, reporting "headers error". Standard Linux `unzip` (ran via WSL) had no problems though, and Windows explorer's built-in zip support also works fine when changing the extension to `.zip`. I haven't tested other tools yet.

If the package file is absent (or renamed), the game will try loading files directly from the filesystem, expecting the same directory structure as found in the package file. It can also be forced to bypass the zip file by launching with the command line switch `-Z0`. This makes it easy to modify some of the game's files without needing to repackage them, allowing for quicker iteration when testing things.

Within the package file, we find the following directory structure:

```
Saucerdata.pak
  |
  | - Bitmaps
  | - Demos
  | - LEVELS
  | - maps
```


#### Bitmaps

This contains a number of image files in standard PCX format. All the files are 320x240 in size. These files are only used for the game's loading screens, all other image data is stored elsewhere.

#### Demos

A small number of `.dem` files containing data needed for the game's demos (played back when the user is inactive on the main menu for a while.) Most likely these contain a sequence of recorded input events, but I haven't investigated the exact format yet.

#### Levels

Contains a number of `.wad` files. These have nothing in common with Doom's WAD format, aside from also being container files that store multiple data assets (more on the format below). There's one `wad` file per level, and a matching `.map` file in the `maps` directory (see below).

Each `wad` file stores all assets required for a specific level. A pair of `wad` and `map` files provides the complete set of data needed to run the corresponding level. Assets used in multiple levels are stored redundantly across several `wad` files, there's no system for reusing data.

The bulk of the assets found in a `wad` file are textures, sprites/images, 3D models, and sound effects. There's also text in multiple languages, and a bunch of other data whose purpose is currently unknown. I suspect that most of it is related to scripting and game logic, but this needs further investigation.


#### Maps

Here we find `.map` files describing the game's levels. As mentioned above, there's one `map` file for each `wad` file. `map` files reference resources from the `wad` file by name. More on the `map` file format below.


## `wad` file format

Doom WADs and many other container formats used in games usually start with a dictionary that lists a name, size, and file offset for each asset. This game's `wad` files don't work like that. They are more like a loose concatenation of various different blocks of data. Many assets are stored in a shared data block and identified by offset and size, but there are dedicated dictionaries for each type of asset instead of a single common dictionary.


### Overall layout

Many of the data blocks are preceded by a 32-bit integer denoting the size in bytes, or the number of records. These cases are indicated in the table below by "Prefix" and "Prefix * N", respectively.

| Block | Size | Description |
--- | --- | ---
| LUT 1 | 256 * 64 * 16 | Color remapping table for distance fog effect and lighting |
| LUT 2 | 256 * 256 * 16 | Lookup table for blending two colors together |
| WAD header | 16 | See below |
| Block 1 | Prefix | purpose unknown |
| Block 2 | Prefix | purpose unknown |
| Language data header | Variable | See below |
| Language data | Prefix | See below |
| Block 3 | Prefix | purpose unknown |
| Unknown dword | 4 | purpose unknown |
| Block 4 | Prefix * 4 | purpose unknown, seems to be ignored by the game |
| Bitmap table | Prefix * 12 | Identifies bitmap data in packed block, see below |
| Exported texture name table | Prefix * 20 | Names and indices of texture bitmaps referenced by map file. Each entry is a `u32` followed by a `char[16]` (0-terminated and 0-padded) |
| Texture table | Prefix * 16 | Texture definitions, see below |
| Unknown asset table | Prefix * 28 | Each record starts with an `u32` offset into the packed block. Exact format and purpose of the referenced data currently unknown. |
| Model table | Prefix * (16 + 36) | First a list of names, each name is a `char[16]`. After that, a list of 36-byte records containing pointers into the packed block. See below |
| Sound effect table | Prefix * (16 + 116) | First a list of names, each name is a `char[16]`. After that, a list of 116-byte records containing pointers into the packed block. See below |
| Palette table | 5 * 4 | List of `i32` offsets into packed block, identifying color palettes. Invalid entries have a value of `-1`/`0xFFFFFFFF`. All files in the shipping game have this set to `0, -1, -1, -1, -1` |
| Named texture table | Prefix * 24 | Each record is a `u32` offset into the texture table followed by a `char[20]` name for the texture. The offset is essentially a relative pointer, divide it by 16 to get an index into the texture table. |
| Unknown name table | Prefix * 16 | List of `char[16]`, seems to be ignored by the game. Maybe data used for debugging or by the original editing tools? |
| Unknown data | Variable | Read one u32, subtract its value from the previous table's count, then read that many 4-byte values. Finally, read two more 4-byte values. Purpose of this is unknown, also seems to be mostly ignored by the game |
| Packed data block | Variable | Size specified in WAD header. Usually this takes up the remainder of the file. Contains a color palette, bitmaps, 3D models, sound effects, and some unknown data |


### WAD header

| Offset | Type | Description
--- | --- | ---
| 0 | u32 | Size and version. Highest byte is version (always 1 in the shipping game's files), lower 3 bytes indicate the size in bytes of the packed data block |
| 4 | u32 | Background color. An index into the color palette, specifies the color to use for clearing the screen. This is essentially the distance fog/darkness color. |
| 8 | u32 | Near depth value for fog effect
| 12 | u32 | Far depth value for fog effect

### Language data header and language data

This section starts with a language header table, an array containing 7 records of 12 bytes each. Each record represents one of the languages the game supports, with the last entry representing some kind of fallback (it's referred to as "generic string" in a debug message left in the game's code). The game supports only 5 languages, so one of the entries is always empty (all 0s).

The format of a language header:

| Offset | Type | Description
--- | --- | ---
| 0 | u16 | unknown |
| 2 | u16 | count |
| 4 | u32 | start offset |
| 8 | u32 | end offset |

The start and end offsets are relative to the beginning of the language data itself.

After the language headers is a list of tables of 16-bit integers, one table per language with as many entries as specified in the `count` value of the corresponding language header.

It's currently not known what the exact meaning of the entries in these language-specific tables is. I don't believe they are offsets, as the language data itself also appears to be in a structured format with an additional header/dictionary at the beginning, and some of the values are larger than the size of the data. My best guess is that they are some sort of string IDs.

The language data itself starts with some binary data of unknown format, and then contains various strings interspersed with other binary data. All of this needs further investigation in order to fully understand it.

In order to simply skip over the language section, we have to read all 7 headers, sum up their count fields, and then skip `sum * 2` bytes forward. This will put as right at the start of the language data itself, which has a size prefix and thus can be skipped by reading one `u32` and then skipping that many bytes.

### Bitmaps

Bitmaps are uncompressed, tightly packed (no scanline padding) arrays of 8-bit color palette indices, in left-to-right, top-to-bottom order. The maximum size seems to be 256x256.

The color palette is found at the very start of the packed data block, and contains 256 entries. Each entry is 4 (!) bytes, the first three specify red, green, and blue respectively, while the 4th byte is unused<sup>1</sup>.

Color index 0 marks transparent parts of the image (alpha = 0).

Each entry in the bitmap table specifies the size and starting offset for the corresponding bitmap:

| Offset | Type | Description
--- | --- | ---
| 0 | u32 | Bitmap data start offset, relative to beginning of packed data block |
| 4 | u32? | Unknown |
| 8 | u16 | Width in pixels |
| 10 | u16 | Height in pixels |

<sup>1</sup> The 4th value is always 5. The game directly passes the loaded palette data to DirectX functions, which tells us that the data must be an array of `PALETTEENTRY` structs from `Windows.h`. These have a 4th member `peFlags`, and the number 5 would be the combination (binary OR) of `PC_NOCOLLAPSE` and `PC_RESERVED`.

### Textures

Textures used for 3D models and level geometry are packed into a few 256x256 atlas bitmaps. The texture definitions in the WAD file's texture table provide texture coordinates to display specific pieces of an atlas. Note that `map` files have their own set of texture definitions, which also reference bitmaps stored in the WAD file, whereas 3D models make use of the texture definitions stored in the WAD file itself.

Each texture definition consists of 4 pairs of u/v coordinates given as 8-bit integer pixel coordinates, a bitmap index, and various flags. The u/v coordinates usually describe a rectangle and are given in order top-left, top-right, bottom-right, bottom-left, but they can deviate from that scheme in some cases (e.g. they might be rotated).

The structure is as follows:

| Offset | Type | Description
--- | --- | ---
0 | u8 | u1
1 | u8 | v1
2 | u16 | Bitmap table index
4 | u8 | u2
5 | u8 | v2
6 | u8 | Unknown
7 | u8 | Blend mode
8 | u8 | u3
9 | u8 | v3
10 | u16 ? | Unknown
12 | u8 | u4
13 | u8 | v4
14 | u16 | Flags

Bit 0 (LSB) of `flags` indicates if the texture is masked (contains transparent pixels).

The blend mode specifies how to apply blending (via the blend table aka `LUT2`) when rendering this texture. Valid blend mode values are `0`, `0x20`, `0x40`, `0x60`, `0x80`, `0xA0,` `0xE0`. Any other value will be quantized to the next higher valid value, i.e. `1` is equivalent to `0x20`, `0x61` is the same as `0x80` etc. Values higher than `0xE0` are treated like `0`.

The effect of the blend mode value also depends on bits 1 and 2 of the flags.
If neither of these bits are set, a `0` means no blending, and values `0x20` through `0xE0` select blend tables 0 through 6, respectively. This results in 6 different levels of increasing opacity, resembling alpha blending.

If bit 1 in flags is set, the blend mode selects tables 7 through 14 instead, with `0` now meaning table 7.

Tables 8 through 14 are opacity with increasing levels of a brightness increase effect, with table 7 being the brightest.

If bit 2 in flags is set, a blend mode value greater or equal to `0x80` results in table 15, whereas a smaller value results in no blend. Table 15 is close to full opacity.


### 3D models

Entries in the model table provide two `u32` pointers into the packed data block, one at the start and one at offset 12. The remaining 28 bytes are unknown at the moment and need further investigation.

The 1st pointer locates a fixed-size block of 80 bytes, which contains up to 7 additional packed data pointers. The 2nd pointer seems to locate a fixed-size block of 60 bytes, which has unknown content.

#### First subheader

All fields are `u32`, unless noted otherwise.

| Offset | Description
--- |  ---
|  0 | Unknown, always 0s |
| 40 | Number of vertices |
| 44 | Vertex list pointer |
| 48 | Number of faces (triangles or quads) |
| 52 | Face list pointer |
| 56 | Unknown count 1 |
| 60 | Unknown data pointer 1 |
| 64 | Unknown count 2 |
| 68 | Unknown data pointer 2 |
| 72 | Unknown data pointer 3 (optional, often 0) |
| 76 | Unknown, possibly 2 i16 offsets |

#### Vertices

Vertices are specified as 3 signed 16-bit integers, denoting `x`, `y`, and `z` respectively.

The coordinate system has positive X pointing right, positive Y pointing down, and positive Z pointing away from the camera.

#### Faces

Faces are defined using 32-byte records of the following format:

| Offset | Type | Description
--- | --- | ---
|  0 | u32 | texture ID (index into the WAD file's texture table) |
|  4 | u16 | vertex index 0 (top-left) |
|  6 | u16 | vertex index 1 (top-right) |
|  8 | u16 | vertex index 2 (bottom-right) |
| 10 | u16 | vertex index 3 (bottom-left) - only used for quads |
| 12 | u16 | face type: `0x8000` = quad, `0x1000` = triangle |
| 14 | ?? | Unknown |

Using only the vertex and face lists seems to be enough information to recreate the models as they appear in the game, so it's not clear yet what all the additional data is needed for.

### Sound effects

Sound effects are stored in standard WAVE file format, embedded in the packed data block. Each entry in the sound header table contains two packed data pointers and a size value in the first 12 bytes (each value is a `u32`). The remaining 104 bytes appear to always be 0.

Although the packed data contains complete WAVE files, the data pointers actually point at specific offsets within those files, skipping past the beginning of the file. The first pointer gives the location of the [`WAVEFORMAT`](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/ns-mmeapi-waveformatex) header describing the format of the audio data. The 2nd pointer locates the raw audio data itself. The `RIFF` header as well as the `WAVE`, `fmt `, and `data` chunk headers are ignored by the game. This means the game could also work with proprietary sound data that omits those headers entirely, which would save some space, but all the sounds in the shipping game's files use complete standard WAVE files. Some even have additional metadata appended to the end of the file.


## `map` file format

The game generally supports true 3D, with rooms above rooms. But the way maps are constructed is largely based on a 2D grid. The grid is always 64x64 tiles in size. Each grid cell defines a terrain tile, which can be textured or invisible. Additional objects can then be placed on top.

The majority of a map's geometry is defined by blocks, which are essentially textured cubes/cuboids. They can be textured from the outside and/or inside, making for a total of 12 possible textured faces per block. Blocks can also have their individual vertices moved up or down to create more varied shapes (e.g. ramps), they can be placed at different heights, and rotated (by 90° only). Multiple blocks can be placed onto a single grid cell.

The terrain also allows varying the height of each grid cell, which creates slopes/ramps as the surrounding terrain adapts. Curiously, the height value only applies to the top-left corner of each terrain tile. The remaining 3 corners take their height from their adjacent tiles' height values. It's also possible to add additional terrain tiles on top of the base layer.

To add further detail to the world, 3D models and 2D billboards can be added to each grid cell. And finally, there are objects that spawn entities into the world (like the player, enemies, items etc.) and a few currently unknown object types.

Maps also have their own list of texture definitions which extend the list found in the WAD file, and it's possible to define texture animations.

### Overall file structure

| Part | Size | Description |
--- | --- | ---
| Header | 36 | See below, defines the number of various elements found in the file |
| Imported texture names | Variable | Indexed string list (`i32` + `char[14]`), listing texture bitmap names found in the WAD file. Terminated by a single `-1` value. |
| Block definitions | N * 60 | Defines blocks that can be placed into the map. Count (`N`) specified in header. |
| Texture definitions | N * 16 | Identical format to texture definitions in WAD file. Count (`N`) specified in header. |
| Texture animations | N * 40 | See below. Count (`N`) specified in header. |
| Imported entity or script (?) names | Variable | Indexed string list (`i32` + `char[16]`). Terminated by a single `-1` value. Needs more investigation. |
| Imported model names | Variable | Indexed string list (`i32` + `char[16]`), listing 3D models found in the WAD file. Terminated by a single `-1` value. |
| Unknown | N * 8 | Looks like a group of four `u16` values. The header specifies a number, but it is always 1 and the values are always `[64, 64, 6, 6]`. Purpose currently unclear. |
| Map data | Variable | See below |

For all three imported name lists, the game cross-references the names with what the WAD file provides, and prepares an index remapping table in case the order in the WAD file is different than in the map file. If a name is not found in the map file, map items referencing its index will be ignored.

### Map header

| Offset | Type | Description
--- | --- | ---
| 0 | `char[4]` | File signature `"SUCK"` (interesting choice..) |
| 4 | u32 | File format version, always 40 |
| 8 | u32 | Number of texture definitions |
| 12 | u32 | Number of block definitions |
| 16 | u32 | Number of unknown records, always 1 |
| 20 | u32 | Number of map data items |
| 24 | u32 | Number of texture animations |
| 28 | u32 | Number of imported texture bitmaps |
| 32 | u16 | Number of entities |
| 34 | u16 | Number of model instances |

### Block definitions

Block definitions provide configurations of map geometry that can be placed into the map multiple times - kind of like a palette of map elements. The same structure is used for flat terrain tiles as well as cubes/cuboids, but most of the fields are only relevant for the latter.

| Offset | Type | Description
--- | --- | ---
0 | u16 | Front face texture, inside
2 | u16 | Top face texture, inside
4 | u16 | Left face texture, inside
6 | u16 | Back face texture, inside
8 | u16 | Right face texture, inside
10 | u16 | Bottom face texture, inside
12 | u16 | Back face texture, outside
14 | u16 | Top face texture, outside
16 | u16 | Left face texture, outside
18 | u16 | Front face texture, outside
20 | u16 | Right face texture, outside
22 | u16 | Bottom face texture, outside
24 | `i16[8]` | Vertex Y coordinates, bottom 4 first, then top 4
40 | ?? | 20 bytes, unknown |

The vertex coordinates are in order top-left, top-right, bottom-right, bottom-left
for each group of 4.
For terrain tiles, only the "bottom inside" face texture is used, and the vertex Y coordinates are ignored.

### Texture animation

TODO

### Map data

The map data is a list of records of varying type. Each record starts with a common header specifying the map item's position on the grid and its type. The amount of data following this header depends on the type.

The header consists of 3 `u32` values: X, Y, and type/flags. The high bits of X sometimes store additional information, so both X and Y should be `AND`-ed with `0xFFFF` to get usable coordinates. The type value can be determined by `AND`-ing the 3rd `u32` with `0x1BFC0000` and shifting down by 16 bits. The meaning of other bits in the third value is currently unknown.

Here's an overview of the possible types:

| Type | Data size | Description |
--- | --- | ---
| `0x4` | 20 | Terrain tile |
| `0x8` | 24 | Additional terrain tile |
| `0x10` | 12 | Billboard texture |
| `0x20` | 8 | Unknown |
| `0x40` | 24 | Block (textured cuboid) instance |
| `0x100` | 8 | Unknown |
| `0x200` | 32 | Entity (strat) |
| `0x800` | 24 | Camera position ? |
| `0x1000` | 16 | 3D model instance |

### Terrain tiles

Every map contains exactly 64 * 64 = 4096 of these, and they must be the first map objects defined. The game copies all map items into a single buffer, and it expects the first 4096 items to all be terrain tiles so that it can access them like a grid.

Unlike other types, X and Y are always 0, as the position is given implicitly by ordering. The first terrain tile goes to X/Y grid location `0,0`, the 2nd tile goes to `1,0`, the 65th tile goes to `0,1`, etc.


| Offset | Type | Description |
--- | --- | ---
| 0 | u32 | Unknown |
| 4 | u32 | Block definition index |
| 8 | u8 | Flags |
| 9 | u8 | Unused |
| 10 | i16 | Texture brightness adjustment |
| 12 | i16 | Unknown |
| 14 | i16 | Vertical offset for top-left vertex |
| 16 | i16 ? | Unknown |
| 18 | i16 ? | Unknown |

The `flags` byte contains two values:

| Bits | Value range | Description |
--- | --- | ---
| 6 - 5 (mask: `0x30`) | 0..3 | Rotation. Rotate the tile's texture by 90° counter-clockwise, `value` times |
| 4 - 0 (mask: `0xf`) | 0..15 | Sprite brightness. Determines which part of LUT 1 to use for adjusting the color of sprites on top of this terrain tile. Used to implement different lighting conditions in different parts of a map. |


### Additional terrain tile

| Offset | Type | Description |
--- | --- | ---
| 0 | u32 | Unknown |
| 4 | u32 | Block definition index |
| 8 | u8 | Flags |
| 9 | u8 | Unused |
| 10 | i16 | Texture brightness adjustment |
| 12 | i16 | Unknown |
| 14 | i16 | Top-left vertex Y |
| 16 | i16 | Top-right vertex Y |
| 18 | i16 | Bottom-right vertex Y |
| 20 | i16 | Bottom-left vertex Y |

The `flags` field works exactly the same as for regular terrain tiles.

### Billboard texture

TODO

### Block instance

| Offset | Type | Description |
--- | --- | ---
| 0 | u32 | Unknown |
| 4 | u32 | Block definition index |
| 8 | u8 | Flags |
| 9 | u8 | Unused |
| 10 | i16 | Texture brightness adjustment |
| 12 | i16 | Unknown |
| 14 | i16 | Overall vertex Y adjustment |
| 16 | `i8[8]` | Individual vertex Y displacements (bottom 4 first, then top 4) |

The `flags` field is the same as for terrain tiles.

A block instance generates 4 vertices forming a cube/cuboid, which can have
up to 12 textured faces. The X and Z coordinates for each vertex are determined
by the grid location. When instancing a block, it can be moved up or down as a whole,
and individual vertices can also be moved up or down to deform the shape.

The full process for determining the final vertex Y values is as follows:

* Start with the Y values from the referenced block definition
* Rotate the vertices according to the rotation specified in `flags`, counter-clockwise
* Add the overall vertex Y adjustment to all vertices, this moves the whole cuboid up or down
* Add the individual vertex displacements, this deforms the cuboid. :warning: Multiply the displacement values by 4 to get the same unit as for other vertex coordinate values.

In other words, the rotation is applied before applying individual displacements.

After generating the vertex coordinates, generate textured quads for each face that
has a non-zero texture specified in the referenced block definition.

The side faces should be rotated the same way as the vertices, i.e. if a rotation of
1 is specified (90° counter-clockwise), and the block definition has a texture on the front face, that texture should be displayed on the right face instead.

For the top and bottom faces, the texture itself should be rotated. :warning: Interestingly, the top faces do a clockwise rotation, unlike the bottom faces which are counter-clockwise like everything else.

### 3D model instance

| Offset | Type | Description |
--- | --- | ---
| 0 | u8 | X offset |
| 1 | u8 | Y offset |
| 2 | i16 | Vertical offset |
| 4 | 6 bytes | Unknown |
| 10 | u32 | Model index |
| 14 | 2 bytes | Unknown |

TODO
