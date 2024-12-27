/* Copyright (C) 2024, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "wad_file.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>


namespace saucer
{

constexpr auto MAP_SIZE = 64;


struct BlockDef
{
  struct
  {
    uint16_t front;
    uint16_t top;
    uint16_t left;
    uint16_t back;
    uint16_t right;
    uint16_t bottom;
  } texturesInside;

  struct
  {
    uint16_t back;
    uint16_t top;
    uint16_t left;
    uint16_t front;
    uint16_t right;
    uint16_t bottom;
  } texturesOutside;

  std::array<int16_t, 8> vertexCoordinatesY;
};


struct MapItemCommon
{
  uint16_t x = 0;
  uint16_t y = 0;
};


struct Flags
{
  uint8_t raw = 0;

  Flags() = default;
  Flags(uint8_t raw_)
    : raw(raw_)
  {
  }

  int rotation() const { return (raw & 0x30) >> 4; }
};


struct TerrainTile : MapItemCommon
{
  uint32_t blockDefIndex;
  Flags flags;
  int16_t verticalOffset;
};


struct ExtraTerrainTile : MapItemCommon
{
  uint32_t blockDefIndex;
  Flags flags;
  std::array<int16_t, 4> vertexCoordinatesY;
};


struct BlockInstance : MapItemCommon
{
  uint32_t blockDefIndex;
  Flags flags;
  int16_t verticalOffset;
  std::array<int8_t, 8> vertexOffsetsY;
};


struct ModelInstance : MapItemCommon
{
  std::string modelName;
  uint8_t xOffset;
  uint8_t yOffset;
  int16_t verticalOffset;
  uint16_t rotationX;
  uint16_t rotationY;
  uint16_t rotationZ;
  uint16_t scale;
};


struct Billboard : MapItemCommon
{
  uint32_t texture;
  uint8_t xOffset;
  uint8_t yOffset;
  int16_t verticalOffset;
  uint16_t scale;
};


using TerrainGrid = std::array<TerrainTile, MAP_SIZE * MAP_SIZE>;
using MapItem =
  std::variant<ExtraTerrainTile, BlockInstance, ModelInstance, Billboard>;


struct MapData
{
  std::vector<TextureDef> mTextureDefs;
  std::vector<BlockDef> mBlockDefs;
  std::unique_ptr<TerrainGrid> mpTerrain = std::make_unique<TerrainGrid>();
  std::vector<MapItem> mItems;

  TerrainTile& terrainAt(int x, int y)
  {
    return (*mpTerrain)[x + y * MAP_SIZE];
  }

  const TerrainTile& terrainAt(int x, int y) const
  {
    return (*mpTerrain)[x + y * MAP_SIZE];
  }
};


std::optional<MapData>
  loadMapfile(const std::filesystem::path& path, const WadData& wad);

} // namespace saucer
