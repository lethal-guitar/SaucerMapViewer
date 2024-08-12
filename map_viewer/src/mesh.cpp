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

#include "mesh.hpp"


namespace saucer
{

void Mesh::draw()
{
  glBindBuffer(GL_ARRAY_BUFFER, mVbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mEbo);
  submitVertexAttributeSetup(mAttributeSpecs);
  glDrawElements(GL_TRIANGLES, mNumIndices, GL_UNSIGNED_SHORT, nullptr);
}


void Mesh::drawSubRange(uint16_t start, uint16_t count)
{
  glBindBuffer(GL_ARRAY_BUFFER, mVbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mEbo);
  submitVertexAttributeSetup(mAttributeSpecs);
  glDrawElements(
    GL_TRIANGLES,
    count,
    GL_UNSIGNED_SHORT,
    rigel::opengl::toVoidPtr(start * sizeof(uint16_t)));
}

} // namespace saucer
