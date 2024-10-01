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

#include "map_file.hpp"
#include "mesh.hpp"
#include "wad_file.hpp"

#include <rigel/base/color.hpp>
#include <rigel/base/spatial_types.hpp>
#include <rigel/base/warnings.hpp>
#include <rigel/opengl/handle.hpp>
#include <rigel/opengl/opengl.hpp>
#include <rigel/opengl/shader.hpp>
#include <rigel/opengl/utils.hpp>

RIGEL_DISABLE_WARNINGS
#include <SDL.h>
#include <glm/vec3.hpp>
RIGEL_RESTORE_WARNINGS

#include <type_traits>


namespace saucer
{

struct TextureAtlas
{
  TextureAtlas() = default;
  TextureAtlas(
    const rigel::base::Image& image,
    rigel::base::ArrayView<int> pages);

  rigel::opengl::Handle<rigel::opengl::tag::Texture> mTexture;
  float mWidth = 0.0f;
  std::vector<float> mUvOffsets;
};


struct MaskedMesh
{
  Mesh mMesh;
  uint16_t mMaskedFacesStart = 0;
  uint16_t mMaskedFacesCount = 0;

  void draw(rigel::opengl::Shader& shader);
};


class MapRenderer
{
public:
  MapRenderer(const MapData& map, const WadData& wad);
  ~MapRenderer();

  void handleEvent(const SDL_Event& event, double dt);
  void updateAndRender(double dt, const rigel::base::Size& windowSize);

  bool mShowTerrain = true;
  bool mShowGeometry = true;
  bool mShowModels = true;
  bool mShowBillboards = true;
  bool mCullFaces = true;

  const glm::vec3& cameraPosition() const { return mCameraPosition; }

private:
  void buildMeshes(
    const MapData& map,
    const WadData& wad,
    const std::unordered_map<std::string, ModelData>& models);
  void moveCamera(double dt);

  rigel::base::Color mBackgroundColor;

  rigel::opengl::DummyVao mDummyVao;
  TextureAtlas mWorldTextures;
  TextureAtlas mModelTextures;
  rigel::opengl::Shader mShader;
  rigel::opengl::Shader mBillboardShader;

  glm::vec3 mCameraPosition{0.0f, 1.5f, 0.0f};
  glm::vec3 mCameraDirection{0.0f, 0.0f, -1.0f};

  Mesh mTerrainMesh;
  MaskedMesh mBlocksMesh;
  MaskedMesh mModelsMesh;
  Mesh mBillboardsMesh;
};

} // namespace saucer
