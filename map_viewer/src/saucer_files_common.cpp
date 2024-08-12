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

#include "saucer_files_common.hpp"

#include <rigel/base/binary_io.hpp>


namespace saucer
{

TextureDef readTextureDef(std::istream& f)
{
  using namespace rigel::base;

  TextureDef def;

  def.uvs[0].u = read<uint8_t>(f);
  def.uvs[0].v = read<uint8_t>(f);
  def.bitmapIndex = read<uint16_t>(f);
  def.uvs[1].u = read<uint8_t>(f);
  def.uvs[1].v = read<uint8_t>(f);
  skipBytes(f, sizeof(uint8_t) * 2);
  def.uvs[2].u = read<uint8_t>(f);
  def.uvs[2].v = read<uint8_t>(f);
  skipBytes(f, sizeof(uint16_t));
  def.uvs[3].u = read<uint8_t>(f);
  def.uvs[3].v = read<uint8_t>(f);

  const auto flags = read<uint16_t>(f);
  def.isMasked = flags & 1;

  return def;
}

} // namespace saucer
