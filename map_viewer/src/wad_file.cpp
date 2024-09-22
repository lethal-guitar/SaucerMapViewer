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

#include "wad_file.hpp"

#include <rigel/base/binary_io.hpp>
#include <rigel/base/byte_buffer.hpp>

#include <fstream>
#include <numeric>


namespace saucer
{

namespace
{

struct LanguageHeader
{
  std::uint16_t unknown;
  std::uint16_t numEntries;
  std::uint32_t dataStart;
  std::uint32_t dataEnd;
};

} // namespace


std::unique_ptr<Palette> WadData::loadPalette() const
{
  using namespace rigel;

  auto pPalette = std::make_unique<Palette>();
  auto& palette = *pPalette;

  std::memcpy(
    palette.data(), mPackedData.data(), sizeof(base::Color) * palette.size());
  palette[0].a = 0;

  for (auto i = 1u; i < palette.size(); ++i)
  {
    palette[i].a = 255;
  }

  return pPalette;
}


rigel::base::Color WadData::lookupColorIndex(uint8_t index) const
{
  rigel::base::Color color;

  std::memcpy(
    &color, mPackedData.data() + index * sizeof(color), sizeof(color));
  color.a = 255;

  return color;
}


rigel::base::Image
  WadData::buildTextureAtlas(rigel::base::ArrayView<int> pages) const
{
  using namespace rigel;

  const auto pPalette = loadPalette();
  const auto& palette = *pPalette;

  const auto numPages = pages.size();

  base::PixelBuffer pixels;
  pixels.resize(TEXTURE_PAGE_SIZE * TEXTURE_PAGE_SIZE * numPages);

  const auto atlasWidth = TEXTURE_PAGE_SIZE * numPages;

  auto i = 0;
  for (auto bitmapIndex : pages)
  {
    const auto* pSourceData = mPackedData.data() + mBitmaps[bitmapIndex].offset;

    const auto destOffset = i * TEXTURE_PAGE_SIZE;

    for (auto row = 0; row < TEXTURE_PAGE_SIZE; ++row)
    {
      for (auto col = 0; col < TEXTURE_PAGE_SIZE; ++col)
      {
        pixels[destOffset + col + row * atlasWidth] = palette[*pSourceData];
        ++pSourceData;
      }
    }

    ++i;
  }

  return base::Image{
    std::move(pixels), TEXTURE_PAGE_SIZE * numPages, TEXTURE_PAGE_SIZE};
}


std::optional<WadData> loadWadFile(const std::filesystem::path& path)
{
  using namespace rigel::base;

  WadData wad;

  std::ifstream f(path, std::ios::binary);

  if (!f.is_open() || !f.good())
  {
    return {};
  }


  auto skipRecords = [&f](size_t itemSize = 1) {
    skipBytes(f, read<uint32_t>(f) * itemSize);
  };


  // Skip color lookup tables
  skipBytes(f, 256 * 64 * 16 + 256 * 256 * 16);

  const auto wadInfo = read<uint32_t>(f);
  const auto version = wadInfo >> 24;
  const auto packedDataSize = wadInfo & 0xFFFFFF;

  if (version != 1)
  {
    return {};
  }

  wad.mBackgroundColor = uint8_t(read<uint32_t>(f));

  // Skip rest of WAD header
  skipBytes(f, 2 * sizeof(uint32_t));

  skipRecords();
  skipRecords();

  // Skip language data
  {
    std::array<LanguageHeader, 7> languageHeaders;

    readArray(f, languageHeaders.data(), 7);

    const auto totalEntries = std::accumulate(
      languageHeaders.begin(),
      languageHeaders.end(),
      uint32_t(0),
      [](uint32_t sum, const LanguageHeader& header) {
        return sum + header.numEntries;
      });

    skipBytes(f, totalEntries * sizeof(uint16_t));
  }

  skipRecords();
  skipRecords();
  skipBytes(f, sizeof(uint32_t));
  skipRecords(sizeof(uint32_t));

  {
    const auto numBitmaps = read<uint32_t>(f);
    wad.mBitmaps.reserve(numBitmaps);

    for (auto i = 0u; i < numBitmaps; ++i)
    {
      const auto offset = read<uint32_t>(f);
      skipBytes(f, sizeof(uint32_t));
      const auto width = read<uint16_t>(f);
      const auto height = read<uint16_t>(f);

      wad.mBitmaps.push_back({offset, width, height});
    }
  }

  {
    const auto numExportedTextures = read<uint32_t>(f);

    for (auto i = 0u; i < numExportedTextures; ++i)
    {
      const auto index = read<uint32_t>(f);
      const auto name = readString(f, 16);

      wad.mTexturePages[name] = index;
    }
  }

  {
    const auto numTextureDefs = read<uint32_t>(f);
    wad.mTextureDefs.reserve(numTextureDefs);

    for (auto i = 0u; i < numTextureDefs; ++i)
    {
      wad.mTextureDefs.push_back(readTextureDef(f));
    }
  }

  skipRecords(28);

  {
    const auto numModels = read<uint32_t>(f);

    std::vector<std::string> modelNames;
    modelNames.reserve(numModels);

    for (auto i = 0u; i < numModels; ++i)
    {
      modelNames.push_back(readString(f, 16));
    }

    for (auto i = 0u; i < numModels; ++i)
    {
      const auto offsetData = read<uint32_t>(f);
      skipBytes(f, 8);
      const auto offsetParams = read<uint32_t>(f);
      skipBytes(f, 20);

      wad.mModels[modelNames[i]] = ModelInfo{offsetData, offsetParams};
    }
  }

  // Sound info table
  skipRecords(16 + 116);

  // Palette info table
  skipBytes(f, 5 * sizeof(int32_t));

  // Named texture table
  skipRecords(24);

  {
    const auto numDebugNames = read<uint32_t>(f);
    skipBytes(f, numDebugNames * 16);

    const auto count = read<uint32_t>(f);
    skipBytes(f, (numDebugNames - count) * 4 + 8);
  }

  wad.mPackedData.resize(packedDataSize);
  readArray(f, wad.mPackedData.data(), packedDataSize);

  return wad;
}


ModelData WadData::loadModel(const std::string& name) const
{
  ModelData model;

  const auto& entry = mModels.at(name);

  {
    const auto iData = mPackedData.begin() + entry.offsetData;

    auto headerReader = rigel::base::LeStreamReader(iData, iData + 80);
    headerReader.skipBytes(40);

    const auto numVertices = headerReader.readU32();
    const auto vertexListStart = headerReader.readU32();
    const auto numFaces = headerReader.readU32();
    const auto faceListStart = headerReader.readU32();

    model.vertices = rigel::base::ArrayView<ModelVertex>(
      reinterpret_cast<const ModelVertex*>(
        mPackedData.data() + vertexListStart),
      numVertices);

    model.faces.reserve(numFaces);

    const auto iFacesData = mPackedData.begin() + faceListStart;
    auto facesReader =
      rigel::base::LeStreamReader(iFacesData, iFacesData + numFaces * 32);

    for (auto i = 0u; i < numFaces; ++i)
    {
      ModelFace face;
      face.mTexture = facesReader.readU32();

      for (auto& index : face.mIndices)
      {
        index = facesReader.readU16();
      }

      const auto type = facesReader.readU16();

      face.mType =
        type == 0x8000 ? ModelFace::Type::Quad : ModelFace::Type::Triangle;

      facesReader.skipBytes(18);

      model.faces.push_back(face);
    }
  }

  {
    const auto iData = mPackedData.begin() + entry.offsetParams;

    auto reader = rigel::base::LeStreamReader(iData, iData + 60);

    for (auto& entry : model.transformationMatrix)
    {
      entry = reader.readS16();
    }
  }

  return model;
}

} // namespace saucer
