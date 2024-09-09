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

#include "map_renderer.hpp"

#include <rigel/base/match.hpp>
#include <rigel/opengl/utils.hpp>

RIGEL_DISABLE_WARNINGS
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
RIGEL_RESTORE_WARNINGS

#include <algorithm>


using namespace rigel;


namespace saucer
{

namespace
{

constexpr auto CAMERA_MOVEMENT_SPEED = 8.0f;
constexpr auto CAMERA_ROTATION_SPEED = 2.0f;


const char* FRAGMENT_SOURCE = R"shd(
DEFAULT_PRECISION_DECLARATION
OUTPUT_COLOR_DECLARATION

IN HIGHP vec2 texCoordFrag;
uniform sampler2D textureData;
uniform bool alphaTesting;


void main() {
  vec4 color = TEXTURE_LOOKUP(textureData, texCoordFrag);

  if (alphaTesting && color.a != 1.0f) {
    discard;
  }

  OUTPUT_COLOR = color;
}
)shd";


const char* VERTEX_SOURCE = R"shd(
ATTRIBUTE HIGHP vec3 position;
ATTRIBUTE HIGHP vec2 texCoord;

OUT HIGHP vec2 texCoordFrag;

uniform mat4 transform;


void main() {
  gl_Position = transform * vec4(position, 1.0);
  texCoordFrag = texCoord;
}
)shd";


constexpr auto TEX_UNIT_NAMES = std::array{"textureData"};

constexpr auto ATTRIBUTE_SPECS = std::array<opengl::AttributeSpec, 2>{{
  {"position", opengl::AttributeSpec::Size::vec3},
  {"texCoord", opengl::AttributeSpec::Size::vec2},
}};


const opengl::ShaderSpec SHADER_SPEC{
  ATTRIBUTE_SPECS,
  TEX_UNIT_NAMES,
  VERTEX_SOURCE,
  FRAGMENT_SOURCE};


std::vector<int> determineTexturePagesUsed(const MapData& map)
{
  std::vector<int> pages;

  for (const auto& textureDef : map.mTextureDefs)
  {
    pages.push_back(textureDef.bitmapIndex);
  }

  std::sort(pages.begin(), pages.end());
  const auto iNewEnd = std::unique(pages.begin(), pages.end());

  pages.erase(iNewEnd, pages.end());

  return pages;
}


struct TexCoords
{
  float u, v;
};


struct Vertex
{
  float x, y, z;
  TexCoords uv;
};


struct MapVertex
{
  int x, y, verticalOffset;
};


Vertex makeVertex(int x, int y, int verticalOffset, const TexCoords& uv)
{
  // The game uses grid coordinates alongside vertical offsets. We map
  // grid X/Y coordinates to the X and Z axes in the OpenGL coordinate system,
  // and the vertical dimension to OpenGL's Y axis.
  // We also define a grid cell to be 1.0 in size, and the center of the map to
  // be at the origin (0, 0, 0). In the game's coordinate system, a perfect
  // cube is 256 units high, so we want to scale vertical values accordingly
  // so that a perfect cube is 1.0 units high in OpenGL coordinates.
  // We also need to invert the vertical axis, since OpenGL has positive Y
  // pointing up.
  const auto vX = float(x) - 32.0f;
  const auto vY = float(verticalOffset) / -256.0f;
  const auto vZ = float(y) - 32.0f;

  return Vertex{vX, vY, vZ, uv};
}


Vertex makeVertex(const MapVertex& v, const TexCoords& uv)
{
  return makeVertex(v.x, v.y, v.verticalOffset, uv);
}


} // namespace


MapRenderer::MapRenderer(const MapData& map, const WadData& wad)
  : mBackgroundColor(wad.lookupColorIndex(wad.mBackgroundColor))
  , mShader(SHADER_SPEC)
{
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  {
    auto guard = opengl::useTemporarily(mShader);
    mShader.setUniform("textureData", 0);
    mShader.setUniform("alphaTesting", false);
  }

  const auto pagesUsed = determineTexturePagesUsed(map);
  const auto textureAtlasImage = wad.buildTextureAtlas(pagesUsed);

  mTextureAtlas = opengl::createTexture(textureAtlasImage);
  mTextureAtlasWidth = float(textureAtlasImage.width());
  mTextureAtlasUvOffsets.assign(pagesUsed.back() + 1, 0.0f);

  {
    auto i = 0;
    for (const auto page : pagesUsed)
    {
      mTextureAtlasUvOffsets[page] = float(i) / float(pagesUsed.size());
      ++i;
    }
  }

  buildMeshes(map, wad);
}


MapRenderer::~MapRenderer() = default;


void MapRenderer::handleEvent(const SDL_Event& event, double dt) { }


void MapRenderer::updateAndRender(
  double dt,
  const rigel::base::Size& windowSize)
{
  moveCamera(dt);


  const auto view = glm::lookAt(
    mCameraPosition,
    mCameraPosition + mCameraDirection,
    glm::vec3(0.0f, 1.0f, 0.0f));

  const auto windowAspectRatio =
    float(windowSize.width) / float(windowSize.height);
  auto matrix =
    glm::perspective(glm::radians(90.0f), windowAspectRatio, 0.1f, 100.0f) *
    view;

  mShader.use();
  mShader.setUniform("transform", matrix);

  if (mCullFaces)
  {
    glEnable(GL_CULL_FACE);
  }
  else
  {
    glDisable(GL_CULL_FACE);
  }


  const auto clearColor = opengl::toGlColor(mBackgroundColor);
  glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  if (mShowTerrain)
  {
    mTerrainMesh.draw();
  }

  if (mShowGeometry)
  {
    if (mMaskedBlockFacesCount)
    {
      mBlocksMesh.drawSubRange(0, mMaskedBlockFacesStart);

      mShader.setUniform("alphaTesting", true);
      mBlocksMesh.drawSubRange(mMaskedBlockFacesStart, mMaskedBlockFacesCount);
      mShader.setUniform("alphaTesting", false);
    }
    else
    {
      mBlocksMesh.draw();
    }
  }
}


void MapRenderer::buildMeshes(const MapData& map, const WadData& wad)
{
  auto getTexCoords = [&](uint16_t textureDefIndex) {
    std::array<TexCoords, 4> texCoords;

    const auto& texDef = map.mTextureDefs[textureDefIndex];

    // The game's texture coordinates are relative to their respective page,
    // but we combine all pages into a single texture atlas. Adjust U
    // coordinates accordingly.
    const auto uOffset = mTextureAtlasUvOffsets[texDef.bitmapIndex];

    std::transform(
      texDef.uvs.begin(),
      texDef.uvs.end(),
      texCoords.begin(),
      [&](const UvPair& uv) {
        return TexCoords{
          (float(uv.u) + 0.5f) / mTextureAtlasWidth + uOffset,
          (float(uv.v) + 0.5f) / float(TEXTURE_PAGE_SIZE)};
      });

    return texCoords;
  };


  MeshBufferData<Vertex> terrainBuffer;

  for (auto y = 0; y < MAP_SIZE; ++y)
  {
    for (auto x = 0; x < MAP_SIZE; ++x)
    {
      const auto& tile = map.terrainAt(x, y);
      const auto& blockDef = map.mBlockDefs[tile.blockDefIndex];
      const auto texture = blockDef.texturesInside.bottom;

      if (texture == 0)
      {
        continue;
      }

      auto uvs = getTexCoords(texture);

      const auto vertOffset0 = tile.verticalOffset;
      const auto vertOffset1 =
        x < MAP_SIZE - 1 ? map.terrainAt(x + 1, y).verticalOffset : vertOffset0;
      const auto vertOffset2 =
        y < MAP_SIZE - 1 ? map.terrainAt(x, y + 1).verticalOffset : vertOffset0;
      const auto vertOffset3 = x < MAP_SIZE - 1 && y < MAP_SIZE - 1
        ? map.terrainAt(x + 1, y + 1).verticalOffset
        : vertOffset0;

      const auto rotation = 4 - tile.flags.rotation();

      // clang-format off
      terrainBuffer.addQuad(
        makeVertex(x,     y,     vertOffset0, uvs[(0 + rotation) % 4]),
        makeVertex(x + 1, y,     vertOffset1, uvs[(1 + rotation) % 4]),
        makeVertex(x + 1, y + 1, vertOffset3, uvs[(2 + rotation) % 4]),
        makeVertex(x,     y + 1, vertOffset2, uvs[(3 + rotation) % 4]));
      // clang-format on
    }
  }


  MeshBufferData<Vertex> blocksBuffer;
  MeshBufferData<Vertex> blocksBufferMasked;

  for (const auto item : map.mItems)
  {
    base::match(
      item,
      [&](const ExtraTerrainTile& tile) {
        const auto& blockDef = map.mBlockDefs[tile.blockDefIndex];
        const auto texture = blockDef.texturesInside.bottom;

        if (texture == 0)
        {
          return;
        }

        auto uvs = getTexCoords(texture);

        const auto rotation = 4 - tile.flags.rotation();

        const auto x = tile.x;
        const auto y = tile.y;
        const auto& verticalOffsets = tile.vertexCoordinatesY;

        // clang-format off
        terrainBuffer.addQuad(
          makeVertex(x,     y,     verticalOffsets[0], uvs[(0 + rotation) % 4]),
          makeVertex(x + 1, y,     verticalOffsets[1], uvs[(1 + rotation) % 4]),
          makeVertex(x + 1, y + 1, verticalOffsets[2], uvs[(2 + rotation) % 4]),
          makeVertex(x,     y + 1, verticalOffsets[3], uvs[(3 + rotation) % 4]));
        // clang-format on
      },

      [&](const BlockInstance& block) {
        const auto& blockDef = map.mBlockDefs[block.blockDefIndex];

        const auto x = block.x;
        const auto y = block.y;
        const auto baseOffset = block.verticalOffset;

        // clang-format off
        std::array<MapVertex, 8> vertices{{
          {x,     y,     0},
          {x + 1, y,     0},
          {x + 1, y + 1, 0},
          {x,     y + 1, 0},
          {x,     y,     0},
          {x + 1, y,     0},
          {x + 1, y + 1, 0},
          {x,     y + 1, 0},
        }};
        // clang-format on

        for (auto i = 0u; i < 4u; ++i)
        {
          vertices[i].verticalOffset = baseOffset +
            blockDef.vertexCoordinatesY[(i + block.flags.rotation()) % 4] +
            block.vertexOffsetsY[i] * 4;
          vertices[i + 4].verticalOffset = baseOffset +
            blockDef.vertexCoordinatesY[(i + block.flags.rotation()) % 4 + 4] +
            block.vertexOffsetsY[i + 4] * 4;
        }


        auto addFace = [&](
                         auto texture,
                         int vi0,
                         int vi1,
                         int vi2,
                         int vi3,
                         int textureRotation = 0) {
          if (!texture || texture >= map.mTextureDefs.size())
          {
            return;
          }

          const auto uvs = getTexCoords(texture);

          // Masked faces need to be kept separate, as we have to render them
          // with alpha-testing enabled.
          auto pBuffer = map.mTextureDefs[texture].isMasked
            ? &blocksBufferMasked
            : &blocksBuffer;

          pBuffer->addQuad(
            makeVertex(vertices[vi0], uvs[(0 + textureRotation) % 4]),
            makeVertex(vertices[vi1], uvs[(1 + textureRotation) % 4]),
            makeVertex(vertices[vi2], uvs[(2 + textureRotation) % 4]),
            makeVertex(vertices[vi3], uvs[(3 + textureRotation) % 4]));
        };


        addFace(
          blockDef.texturesOutside.top, 4, 5, 6, 7, block.flags.rotation());
        addFace(
          blockDef.texturesInside.top, 7, 6, 5, 4, block.flags.rotation());

        // clang-format off
        addFace(
          blockDef.texturesOutside.bottom,
          3, 2, 1, 0,
          4 - block.flags.rotation());
        addFace(
          blockDef.texturesInside.bottom,
          0, 1, 2, 3,
          4 - block.flags.rotation());
        // clang-format on

        const auto sides = std::array{
          blockDef.texturesOutside.left,
          blockDef.texturesOutside.front,
          blockDef.texturesOutside.right,
          blockDef.texturesOutside.back,
          blockDef.texturesInside.left,
          blockDef.texturesInside.front,
          blockDef.texturesInside.right,
          blockDef.texturesInside.back};
        const auto sidesRotation = 4 - block.flags.rotation();

        addFace(sides[(0 + sidesRotation) % 4], 4, 7, 3, 0);
        addFace(sides[(1 + sidesRotation) % 4], 7, 6, 2, 3);
        addFace(sides[(2 + sidesRotation) % 4], 6, 5, 1, 2);
        addFace(sides[(3 + sidesRotation) % 4], 5, 4, 0, 1);
        addFace(sides[(0 + sidesRotation) % 4 + 4], 7, 4, 0, 3);
        addFace(sides[(1 + sidesRotation) % 4 + 4], 6, 7, 3, 2);
        addFace(sides[(2 + sidesRotation) % 4 + 4], 5, 6, 2, 1);
        addFace(sides[(3 + sidesRotation) % 4 + 4], 4, 5, 1, 0);
      });
  }


  mTerrainMesh = terrainBuffer.createMesh(mShader.attributeSpecs());


  if (blocksBufferMasked.hasData())
  {
    mMaskedBlockFacesStart = uint16_t(blocksBuffer.mIndexBuffer.size());
    mMaskedBlockFacesCount = uint16_t(blocksBufferMasked.mIndexBuffer.size());
    blocksBuffer.append(blocksBufferMasked);
  }

  mBlocksMesh = blocksBuffer.createMesh(mShader.attributeSpecs());
}


void MapRenderer::moveCamera(double dt)
{
  const auto pKeyboardState = SDL_GetKeyboardState(nullptr);

  if (pKeyboardState[SDL_SCANCODE_UP])
  {
    if (SDL_GetModState() & KMOD_SHIFT)
      mCameraPosition.y += float(dt) * CAMERA_MOVEMENT_SPEED;
    else
      mCameraPosition += mCameraDirection * float(dt) * CAMERA_MOVEMENT_SPEED;
  }

  if (pKeyboardState[SDL_SCANCODE_DOWN])
  {
    if (SDL_GetModState() & KMOD_SHIFT)
      mCameraPosition.y -= float(dt) * CAMERA_MOVEMENT_SPEED;
    else
      mCameraPosition -= mCameraDirection * float(dt) * CAMERA_MOVEMENT_SPEED;
  }

  if (pKeyboardState[SDL_SCANCODE_LEFT])
  {
    mCameraDirection = glm::vec3(
      glm::rotate(
        glm::mat4(1.0f),
        CAMERA_ROTATION_SPEED * float(dt),
        glm::vec3(0.0f, 1.0f, 0.0f)) *
      glm::vec4(mCameraDirection, 1.0f));
  }

  if (pKeyboardState[SDL_SCANCODE_RIGHT])
  {
    mCameraDirection = glm::vec3(
      glm::rotate(
        glm::mat4(1.0f),
        -1.0f * CAMERA_ROTATION_SPEED * float(dt),
        glm::vec3(0.0f, 1.0f, 0.0f)) *
      glm::vec4(mCameraDirection, 1.0f));
  }
}

} // namespace saucer
