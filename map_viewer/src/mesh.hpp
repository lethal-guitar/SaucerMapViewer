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

#include <rigel/base/array_view.hpp>
#include <rigel/opengl/opengl.hpp>
#include <rigel/opengl/shader.hpp>
#include <rigel/opengl/utils.hpp>

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <vector>


namespace saucer
{

struct Mesh
{
  rigel::base::ArrayView<rigel::opengl::AttributeSpec> mAttributeSpecs;
  rigel::opengl::Handle<rigel::opengl::tag::Buffer> mVbo;
  rigel::opengl::Handle<rigel::opengl::tag::Buffer> mEbo;
  uint16_t mNumIndices = 0;

  void draw();
  void drawSubRange(uint16_t start, uint16_t count);
};


template <typename Vertex>
struct MeshBufferData
{
  std::vector<Vertex> mVertexBuffer;
  std::vector<uint16_t> mIndexBuffer;

  void addQuad(
    const Vertex& topLeft,
    const Vertex& topRight,
    const Vertex& bottomRight,
    const Vertex& bottomLeft);
  void addTriangle(const Vertex& v0, const Vertex& v1, const Vertex& v2);

  Mesh createMesh(
    rigel::base::ArrayView<rigel::opengl::AttributeSpec> attributeSpecs) const;

  void append(const MeshBufferData<Vertex>& other);
  bool hasData() const;
};


template <typename Vertex>
void MeshBufferData<Vertex>::addQuad(
  const Vertex& v0,
  const Vertex& v1,
  const Vertex& v2,
  const Vertex& v3)
{
  const auto indexOffset = mVertexBuffer.size();

  mVertexBuffer.push_back(v0);
  mVertexBuffer.push_back(v1);
  mVertexBuffer.push_back(v2);
  mVertexBuffer.push_back(v3);

  for (const auto index : {0, 3, 1, 1, 3, 2})
  {
    mIndexBuffer.push_back(uint16_t(index + indexOffset));
  }
}


template <typename Vertex>
void MeshBufferData<Vertex>::addTriangle(
  const Vertex& v0,
  const Vertex& v1,
  const Vertex& v2)
{
  const auto indexOffset = mVertexBuffer.size();

  mVertexBuffer.push_back(v0);
  mVertexBuffer.push_back(v1);
  mVertexBuffer.push_back(v2);

  for (const auto index : {0, 2, 1})
  {
    mIndexBuffer.push_back(uint16_t(index + indexOffset));
  }
}


template <typename Vertex>
Mesh MeshBufferData<Vertex>::createMesh(
  rigel::base::ArrayView<rigel::opengl::AttributeSpec> attributeSpecs) const
{
  static_assert(std::is_trivially_copyable_v<Vertex>);

  using namespace rigel::opengl;

  Mesh mesh;

  mesh.mAttributeSpecs = attributeSpecs;
  mesh.mVbo = Handle<tag::Buffer>::create();
  mesh.mEbo = Handle<tag::Buffer>::create();
  mesh.mNumIndices = uint16_t(mIndexBuffer.size());

  glBindBuffer(GL_ARRAY_BUFFER, mesh.mVbo);
  glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(Vertex) * mVertexBuffer.size(),
    mVertexBuffer.data(),
    GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.mEbo);
  glBufferData(
    GL_ELEMENT_ARRAY_BUFFER,
    sizeof(GLushort) * mIndexBuffer.size(),
    mIndexBuffer.data(),
    GL_STATIC_DRAW);

  return mesh;
}


template <typename Vertex>
void MeshBufferData<Vertex>::append(const MeshBufferData<Vertex>& other)
{
  const auto indexOffset = uint16_t(mVertexBuffer.size());
  mVertexBuffer.insert(
    mVertexBuffer.end(),
    other.mVertexBuffer.begin(),
    other.mVertexBuffer.end());
  mIndexBuffer.reserve(mIndexBuffer.size() + other.mIndexBuffer.size());

  std::transform(
    other.mIndexBuffer.begin(),
    other.mIndexBuffer.end(),
    std::back_inserter(mIndexBuffer),
    [indexOffset](const uint16_t index) { return index + indexOffset; });
}


template <typename Vertex>
bool MeshBufferData<Vertex>::hasData() const
{
  return !mVertexBuffer.empty() && !mIndexBuffer.empty();
}

} // namespace saucer
