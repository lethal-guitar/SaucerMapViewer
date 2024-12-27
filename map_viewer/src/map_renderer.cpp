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

#include <rigel/base/image_loading.hpp>
#include <rigel/base/match.hpp>
#include <rigel/opengl/utils.hpp>

RIGEL_DISABLE_WARNINGS
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <imgui.h>
#include <loguru.hpp>
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


const char* BILLBOARD_VERTEX_SOURCE = R"shd(
ATTRIBUTE HIGHP vec2 position;
ATTRIBUTE HIGHP vec2 worldPosition;
ATTRIBUTE HIGHP vec2 texCoord;

OUT HIGHP vec2 texCoordFrag;

uniform mat4 transform;
uniform mat3 rotation;


void main() {
  vec3 pos = rotation * vec3(position, 0.0);
  pos += vec3(worldPosition.x, 0.0, worldPosition.y);

  gl_Position = transform * vec4(pos, 1.0);
  texCoordFrag = texCoord;
}
)shd";


constexpr auto TEX_UNIT_NAMES = std::array{"textureData"};

constexpr auto ATTRIBUTE_SPECS = std::array<opengl::AttributeSpec, 2>{{
  {"position", opengl::AttributeSpec::Size::vec3},
  {"texCoord", opengl::AttributeSpec::Size::vec2},
}};

constexpr auto BILLBOARD_ATTRIBUTE_SPECS = std::array<opengl::AttributeSpec, 3>{{
  {"position", opengl::AttributeSpec::Size::vec2},
  {"basePosition", opengl::AttributeSpec::Size::vec2},
  {"texCoord", opengl::AttributeSpec::Size::vec2},
}};


const opengl::ShaderSpec SHADER_SPEC{
  ATTRIBUTE_SPECS,
  TEX_UNIT_NAMES,
  VERTEX_SOURCE,
  FRAGMENT_SOURCE};

const opengl::ShaderSpec BILLBOARD_SHADER_SPEC{
  BILLBOARD_ATTRIBUTE_SPECS,
  TEX_UNIT_NAMES,
  BILLBOARD_VERTEX_SOURCE,
  FRAGMENT_SOURCE};


std::vector<int> determineWorldTexturePagesUsed(const MapData& map)
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


std::unordered_map<std::string, ModelData>
  loadUsedModels(const MapData& map, const WadData& wad)
{
  std::unordered_map<std::string, ModelData> models;

  for (const auto& item : map.mItems)
  {
    if (const auto pModelInstance = std::get_if<ModelInstance>(&item))
    {
      const auto& name = pModelInstance->modelName;

      if (models.count(name) == 0)
      {
        models.insert({name, wad.loadModel(name)});
      }
    }
  }

  return models;
}


std::vector<int> determineModelTexturePagesUsed(
  const std::unordered_map<std::string, ModelData>& models,
  const WadData& wad)
{
  std::vector<int> pages;

  for (const auto& [_, model] : models)
  {
    for (const auto& face : model.faces)
    {
      pages.push_back(wad.mTextureDefs.at(face.mTexture).bitmapIndex);
    }
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
  Vertex(float x_, float y_, float z_, TexCoords uv_)
    : x(x_)
    , y(y_)
    , z(z_)
    , uv(uv_)
  {
  }

  explicit Vertex(glm::vec3 vec, TexCoords uv_)
    : Vertex(vec.x, vec.y, vec.z, uv_)
  {
  }

  float x, y, z;
  TexCoords uv;
};


struct BillboardVertex
{
  float x, y;
  float worldX, worldZ;
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


std::vector<float> buildAtlasUvOffsetTable(rigel::base::ArrayView<int> pages)
{
  std::vector<float> table;
  if (pages.empty())
  {
    return table;
  }

  table.assign(pages.back() + 1, 0.0f);

  {
    auto i = 0;
    for (const auto page : pages)
    {
      table[page] = float(i) / float(pages.size());
      ++i;
    }
  }

  return table;
}


float convertRotation(const uint16_t rotation)
{
  return float(rotation) / (256.0f * 256.0f) * 360.0f;
}


glm::mat4 convertMatrix(const std::array<int16_t, 3 * 4>& matrix)
{
  auto convert = [](const int16_t value) {
    return float(value) / 512.0f;
  };

  return glm::mat4(
    convert(matrix[0]),
    convert(matrix[1]),
    convert(matrix[2]),
    0.0f,
    convert(matrix[3]),
    convert(matrix[4]),
    convert(matrix[5]),
    0.0f,
    convert(matrix[6]),
    convert(matrix[7]),
    convert(matrix[8]),
    0.0f,
    float(matrix[9]) / -256.0f,
    float(matrix[10]) / -256.0f,
    float(matrix[11]) / 256.0f,
    1.0f);
}


MaskedMesh createMaskedMesh(
  MeshBufferData<Vertex>&& solidFaces,
  MeshBufferData<Vertex>&& maskedFaces,
  const rigel::opengl::Shader& shader)
{
  MaskedMesh mesh;

  if (maskedFaces.hasData())
  {
    mesh.mMaskedFacesStart = uint16_t(solidFaces.mIndexBuffer.size());
    mesh.mMaskedFacesCount = uint16_t(maskedFaces.mIndexBuffer.size());
    solidFaces.append(maskedFaces);
  }

  mesh.mMesh = solidFaces.createMesh(shader.attributeSpecs());

  return mesh;
}

} // namespace


TextureAtlas::TextureAtlas(
  const rigel::base::Image& image,
  rigel::base::ArrayView<int> pages)
  : mTexture(opengl::createTexture(image))
  , mWidth(float(image.width()))
  , mUvOffsets(buildAtlasUvOffsetTable(pages))
{
}


void MaskedMesh::draw(rigel::opengl::Shader& shader)
{
  if (mMaskedFacesStart)
  {
    mMesh.drawSubRange(0, mMaskedFacesStart);

    shader.setUniform("alphaTesting", true);
    mMesh.drawSubRange(mMaskedFacesStart, mMaskedFacesCount);
    shader.setUniform("alphaTesting", false);
  }
  else
  {
    mMesh.draw();
  }
}


MapRenderer::MapRenderer(const MapData& map, const WadData& wad)
  : mBackgroundColor(wad.lookupColorIndex(wad.mBackgroundColor))
  , mShader(SHADER_SPEC)
  , mBillboardShader(BILLBOARD_SHADER_SPEC)
{
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);

  {
    auto guard = opengl::useTemporarily(mShader);
    mShader.setUniform("textureData", 0);
    mShader.setUniform("alphaTesting", false);
  }

  {
    auto guard = opengl::useTemporarily(mBillboardShader);
    mBillboardShader.setUniform("textureData", 0);
    mBillboardShader.setUniform("alphaTesting", true);
  }

  {
    const auto pagesUsed = determineWorldTexturePagesUsed(map);
    const auto textureAtlasImage = wad.buildTextureAtlas(pagesUsed);
    mWorldTextures = TextureAtlas(textureAtlasImage, pagesUsed);
  }

  const auto models = loadUsedModels(map, wad);

  {
    const auto pagesUsed = determineModelTexturePagesUsed(models, wad);
    const auto textureAtlasImage = wad.buildTextureAtlas(pagesUsed);

    mModelTextures = TextureAtlas(textureAtlasImage, pagesUsed);
  }

  buildMeshes(map, wad, models);
}


MapRenderer::~MapRenderer() = default;


void MapRenderer::handleEvent(const SDL_Event& event, double dt) { }


void MapRenderer::updateAndRender(
  double dt,
  const rigel::base::Size& windowSize)
{
  moveCamera(dt);

  const auto windowAspectRatio =
    float(windowSize.width) / float(windowSize.height);
  const auto cameraUpVector = glm::vec3(0.0f, 1.0f, 0.0f);
  const auto view = glm::lookAt(
    mCameraPosition,
    mCameraPosition + mCameraDirection,
    cameraUpVector);
  const auto projection =
    glm::perspective(glm::radians(90.0f), windowAspectRatio, 0.1f, 100.0f);


  mShader.use();
  mShader.setUniform("transform", projection * view);

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

  glBindTexture(GL_TEXTURE_2D, mWorldTextures.mTexture);

  if (mShowTerrain)
  {
    mTerrainMesh.draw();
  }

  if (mShowGeometry)
  {
    mBlocksMesh.draw(mShader);
  }

  if (mShowBillboards)
  {
    auto guard = opengl::useTemporarily(mBillboardShader);

    const auto normal = glm::normalize(mCameraDirection) * -1.0f;
    const auto rightVector = glm::normalize(glm::cross(cameraUpVector, normal));

    mBillboardShader.setUniform("transform", projection * view);
    mBillboardShader.setUniform("rotation", glm::mat3(rightVector, cameraUpVector, normal));

    mBillboardsMesh.draw();
  }

  if (mShowModels)
  {
    glBindTexture(GL_TEXTURE_2D, mModelTextures.mTexture);

    mModelsMesh.draw(mShader);
  }
}


void MapRenderer::buildMeshes(
  const MapData& map,
  const WadData& wad,
  const std::unordered_map<std::string, ModelData>& models)
{
  auto getTexCoords = [](
                        uint16_t textureDefIndex,
                        const TextureAtlas& atlas,
                        const rigel::base::ArrayView<TextureDef> textureDefs) {
    std::array<TexCoords, 4> texCoords;

    const auto& texDef = textureDefs[textureDefIndex];

    // The game's texture coordinates are relative to their respective page,
    // but we combine all pages into a single texture atlas. Adjust U
    // coordinates accordingly.
    const auto uOffset = atlas.mUvOffsets[texDef.bitmapIndex];

    std::transform(
      texDef.uvs.begin(),
      texDef.uvs.end(),
      texCoords.begin(),
      [&](const UvPair& uv) {
        return TexCoords{
          (float(uv.u) + 0.5f) / atlas.mWidth + uOffset,
          (float(uv.v) + 0.5f) / float(TEXTURE_PAGE_SIZE)};
      });

    return texCoords;
  };


  auto getWorldTexCoords = [&](uint16_t index) {
    return getTexCoords(index, mWorldTextures, map.mTextureDefs);
  };


  auto getModelTexCoords = [&](uint16_t index) {
    return getTexCoords(index, mModelTextures, wad.mTextureDefs);
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

      auto uvs = getWorldTexCoords(texture);

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

  MeshBufferData<Vertex> modelsBuffer;
  MeshBufferData<Vertex> modelsBufferMasked;

  MeshBufferData<BillboardVertex> billboardsBuffer;

  for (const auto& item : map.mItems)
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

        auto uvs = getWorldTexCoords(texture);

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

          const auto uvs = getWorldTexCoords(texture);

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
      },

      [&](const ModelInstance& model) {
        const auto& modelData = models.at(model.modelName);

        const auto baseX = float(model.x) - 32.0f;
        const auto baseZ = float(model.y) - 32.0f;

        const auto scale = float(model.scale) / 256.0f;

        auto transform = glm::mat4(1.0f);
        transform = glm::translate(
          transform,
          glm::vec3(
            baseX + model.xOffset / 256.0f,
            model.verticalOffset / -256.0f,
            baseZ + model.yOffset / 256.0f));
        transform = glm::scale(transform, glm::vec3(scale));
        transform = glm::rotate(
          transform,
          glm::radians(convertRotation(model.rotationY)),
          glm::vec3(0.0f, -1.0f, 0.0f));
        transform = glm::rotate(
          transform,
          glm::radians(convertRotation(model.rotationZ)),
          glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::rotate(
          transform,
          glm::radians(convertRotation(model.rotationX)),
          glm::vec3(-1.0f, 0.0f, 0.0f));
        transform *= convertMatrix(modelData.transformationMatrix);


        auto makeModelVertex = [&](const uint16_t index, const TexCoords& uv) {
          const auto& coords = modelData.vertices[index];

          const auto x = coords.x / -256.0f;
          const auto y = coords.y / -256.0f;
          const auto z = coords.z / 256.0f;

          const auto transformed = transform * glm::vec4(x, y, z, 1.0);

          return Vertex{glm::vec3(transformed), uv};
        };


        for (const auto& face : modelData.faces)
        {
          const auto uvs = getModelTexCoords(face.mTexture);
          const auto indices = face.indices();

          auto& buffer = wad.mTextureDefs[face.mTexture].isMasked
            ? modelsBufferMasked
            : modelsBuffer;

          if (indices.size() == 3)
          {
            buffer.addTriangle(
              makeModelVertex(indices[0], uvs[0]),
              makeModelVertex(indices[1], uvs[1]),
              makeModelVertex(indices[2], uvs[2]));
          }
          else
          {
            buffer.addQuad(
              makeModelVertex(indices[0], uvs[0]),
              makeModelVertex(indices[1], uvs[1]),
              makeModelVertex(indices[2], uvs[2]),
              makeModelVertex(indices[3], uvs[3]));
          }
        }
      },

      [&](const Billboard& billboard) {
        const auto& texture = map.mTextureDefs[billboard.texture];

        const auto widthPx = std::abs(texture.uvs[2].u - texture.uvs[0].u);
        const auto heightPx = std::abs(texture.uvs[2].v - texture.uvs[0].v);

        const auto baseX = float(billboard.x) - 32.0f;
        const auto baseY = float(billboard.verticalOffset) / -256.0f;
        const auto baseZ = float(billboard.y) - 32.0f + 1.0f;

        const auto scale = float(billboard.scale) / 256.0f;
        const auto width = widthPx / 64.0f * scale;
        const auto height = heightPx / 64.0f * scale;

        const auto x = float(texture.originX) / 64.0f * scale;
        const auto y = baseY - float(texture.originY) / 64.0f * scale;

        const auto worldX = baseX + float(billboard.xOffset) / 255.0f;
        const auto worldZ = baseZ - float(billboard.yOffset) / 255.0f;

        auto uvs = getWorldTexCoords(billboard.texture);

        // clang-format off
        billboardsBuffer.addQuad(
          BillboardVertex{x,         y,          worldX, worldZ, uvs[0]},
          BillboardVertex{x + width, y,          worldX, worldZ, uvs[1]},
          BillboardVertex{x + width, y - height, worldX, worldZ, uvs[2]},
          BillboardVertex{x,         y - height, worldX, worldZ, uvs[3]});
        // clang-format on
      });
  }


  mTerrainMesh = terrainBuffer.createMesh(mShader.attributeSpecs());
  mBlocksMesh = createMaskedMesh(
    std::move(blocksBuffer), std::move(blocksBufferMasked), mShader);
  mModelsMesh = createMaskedMesh(
    std::move(modelsBuffer), std::move(modelsBufferMasked), mShader);
  mBillboardsMesh = billboardsBuffer.createMesh(mBillboardShader.attributeSpecs());
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
