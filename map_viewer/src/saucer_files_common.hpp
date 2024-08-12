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

#include <array>
#include <cstdint>
#include <istream>
#include <string>


namespace saucer
{

struct UvPair
{
  uint8_t u;
  uint8_t v;
};


struct TextureDef
{
  std::array<UvPair, 4> uvs;
  uint16_t bitmapIndex;
  bool isMasked;
};


template <size_t MaxLen>
std::string readString(std::istream& stream)
{
  std::array<char, MaxLen> string;
  string.fill(0);

  stream.read(string.data(), string.size());

  if (string.back() != 0)
  {
    string.back() = 0;
  }

  return std::string(string.data());
}


TextureDef readTextureDef(std::istream& f);

} // namespace saucer
