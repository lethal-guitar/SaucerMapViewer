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

#include "map_file.hpp"

#include <rigel/base/binary_io.hpp>

#include <fstream>
#include <string_view>


namespace saucer
{

std::optional<MapData>
  loadMapfile(const std::filesystem::path& path, const WadData& wad)
{
  using namespace rigel::base;

  MapData map;

  std::ifstream f(path, std::ios::binary);

  if (!f.is_open() || !f.good())
  {
    return {};
  }


  {
    char signature[4];
    f.read(signature, sizeof(signature));

    if (std::string_view(signature, sizeof(signature)) != "SUCK")
    {
      return {};
    }

    const auto version = read<uint32_t>(f);

    if (version != 40)
    {
      return {};
    }
  }

  const auto numTextureDefs = read<uint32_t>(f);
  const auto numBlockDefs = read<uint32_t>(f);
  const auto numUnknown = read<uint32_t>(f);
  const auto numMapItems = read<uint32_t>(f);
  const auto numTextureAnimations = read<uint32_t>(f);
  skipBytes(f, sizeof(uint32_t) + 2 * sizeof(uint16_t));


  std::vector<int> texturePageRemapTable;
  texturePageRemapTable.assign(wad.mTexturePages.size(), -1);

  {
    auto i = 0u;

    for (;;)
    {
      const auto index = read<int32_t>(f);

      if (index == -1 || f.eof())
      {
        break;
      }

      const auto name = readString<14>(f);

      if (i < texturePageRemapTable.size())
      {
        if (const auto iWadIndex = wad.mTexturePages.find(name);
            iWadIndex != wad.mTexturePages.end())
        {
          texturePageRemapTable[i] = iWadIndex->second;
        }

        ++i;
      }
    }
  }


  map.mBlockDefs.resize(numBlockDefs);

  for (auto& def : map.mBlockDefs)
  {
    def.texturesInside.front = read<uint16_t>(f);
    def.texturesInside.top = read<uint16_t>(f);
    def.texturesInside.left = read<uint16_t>(f);
    def.texturesInside.back = read<uint16_t>(f);
    def.texturesInside.right = read<uint16_t>(f);
    def.texturesInside.bottom = read<uint16_t>(f);
    def.texturesOutside.back = read<uint16_t>(f);
    def.texturesOutside.top = read<uint16_t>(f);
    def.texturesOutside.left = read<uint16_t>(f);
    def.texturesOutside.front = read<uint16_t>(f);
    def.texturesOutside.right = read<uint16_t>(f);
    def.texturesOutside.bottom = read<uint16_t>(f);
    readArray(f, def.vertexCoordinatesY.data(), 8);
    skipBytes(f, 20);
  }


  map.mTextureDefs.reserve(numTextureDefs);

  for (auto i = 0u; i < numTextureDefs; ++i)
  {
    auto def = readTextureDef(f);

    if (def.bitmapIndex >= texturePageRemapTable.size())
    {
      continue;
    }

    const auto newIndex = texturePageRemapTable[def.bitmapIndex];

    if (newIndex != -1)
    {
      def.bitmapIndex = uint16_t(newIndex);
      map.mTextureDefs.push_back(def);
    }
  }


  // TODO: Implement texture animation
  skipBytes(f, 40 * numTextureAnimations);


  // Skip Strat name list
  for (;;)
  {
    const auto index = read<int32_t>(f);

    if (index == -1)
    {
      break;
    }

    skipBytes(f, 16);
  }


  // TODO: Implement models
  for (;;)
  {
    const auto index = read<int32_t>(f);

    if (index == -1)
    {
      break;
    }

    skipBytes(f, 16);
  }


  skipBytes(f, 8 * numUnknown);


  map.mItems.reserve(numMapItems - MAP_SIZE * MAP_SIZE);

  auto numTerrainTilesRead = 0u;

  for (auto i = 0u; i < numMapItems; ++i)
  {
    const auto x = read<uint32_t>(f) & 0xFFFF;
    const auto y = read<uint32_t>(f) & 0xFFFF;
    const auto typeFlags = read<uint32_t>(f);

    const auto type = (typeFlags & 0x1BFC0000) >> 16;

    switch (type)
    {
      case 0x4:
        {
          auto& tile = (*map.mpTerrain)[numTerrainTilesRead];

          skipBytes(f, sizeof(uint32_t));
          tile.blockDefIndex = read<uint32_t>(f);
          tile.flags = read<uint8_t>(f);
          skipBytes(f, 5);
          tile.verticalOffset = read<int16_t>(f);
          skipBytes(f, 4);

          ++numTerrainTilesRead;
        }
        break;

      case 0x8:
        {
          ExtraTerrainTile tile;
          tile.x = x;
          tile.y = y;

          skipBytes(f, sizeof(uint32_t));
          tile.blockDefIndex = read<uint32_t>(f);
          tile.flags = read<uint8_t>(f);
          skipBytes(f, 5);
          readArray(f, tile.vertexCoordinatesY.data(), 4);
          skipBytes(f, 2);

          map.mItems.push_back(tile);
        }
        break;

      case 0x10:
        skipBytes(f, 12);
        break;

      case 0x20:
        skipBytes(f, 8);
        break;

      case 0x40:
        {
          BlockInstance block;
          block.x = x;
          block.y = y;

          skipBytes(f, sizeof(uint32_t));
          block.blockDefIndex = read<uint32_t>(f);
          block.flags = read<uint8_t>(f);
          skipBytes(f, 5);
          block.verticalOffset = read<int16_t>(f);
          readArray(f, block.vertexOffsetsY.data(), 8);

          map.mItems.push_back(block);
        }
        break;

      case 0x100:
        skipBytes(f, 8);
        break;

      case 0x200:
        skipBytes(f, 32);
        break;

      case 0x800:
        skipBytes(f, 24);
        break;

      case 0x1000:
        skipBytes(f, 16);
        break;

      default:
        return {};
    }
  }

  return map;
}

} // namespace saucer
