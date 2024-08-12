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

#include "saucer_files_common.hpp"

#include <rigel/base/array_view.hpp>
#include <rigel/base/image.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>


namespace saucer
{

struct BitmapInfo
{
  uint32_t offset;
  uint16_t width;
  uint16_t height;
};


using Palette = std::array<rigel::base::Color, 256>;


constexpr auto TEXTURE_PAGE_SIZE = 256;


struct WadData
{
  uint8_t mBackgroundColor;
  std::vector<BitmapInfo> mBitmaps;
  std::vector<TextureDef> mTextureDefs;
  std::unordered_map<std::string, uint32_t> mTexturePages;
  std::vector<uint8_t> mPackedData;

  // TODO: Implement model loading
  // std::unordered_map<std::string, ModelInfo> mModels;

  std::unique_ptr<Palette> loadPalette() const;
  rigel::base::Color lookupColorIndex(uint8_t index) const;
  rigel::base::Image buildTextureAtlas(rigel::base::ArrayView<int> pages) const;
};


std::optional<WadData> loadWadFile(const std::filesystem::path& path);

} // namespace saucer
