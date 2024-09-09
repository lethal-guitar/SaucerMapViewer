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

#include "map_viewer_app.hpp"

#include "map_file.hpp"
#include "map_renderer.hpp"
#include "wad_file.hpp"

#include <rigel/base/defer.hpp>
#include <rigel/base/string_utils.hpp>
#include <rigel/opengl/opengl.hpp>
#include <rigel/opengl/utils.hpp>
#include <rigel/ui/imgui_integration.hpp>

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>


namespace saucer
{

MapViewerApp::MapViewerApp(SDL_Window* pWindow)
  : mpWindow(pWindow)
  , mFpsDisplay(
      {ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().WindowPadding.y})
  , mMapFileBrowser(ImGuiFileBrowserFlags_CloseOnEsc)
{
  mMapFileBrowser.SetTitle("Choose map file");
  mMapFileBrowser.SetTypeFilters({".map"});
}


MapViewerApp::~MapViewerApp() = default;


bool MapViewerApp::runOneFrame()
{
  using namespace rigel;
  using rigel::base::defer;


  const auto now = base::Clock::now();
  const auto dt = std::chrono::duration<double>(now - mLastTime).count();
  mLastTime = now;


  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    if (event.type == SDL_QUIT)
    {
      return false;
    }

    if (!rigel::ui::imgui_integration::handleEvent(event))
    {
      handleEvent(event, dt);
    }
  }


  int windowWidth = 0;
  int windowHeight = 0;
  SDL_GL_GetDrawableSize(mpWindow, &windowWidth, &windowHeight);
  const auto windowSize = base::Size{windowWidth, windowHeight};


  {
    ui::imgui_integration::beginFrame(mpWindow);
    auto imGuiFrameGuard = defer([]() { ui::imgui_integration::endFrame(); });

    updateAndRender(dt, windowSize);

    mFpsDisplay.updateAndRender(dt);
  }

  SDL_GL_SwapWindow(mpWindow);


  return true;
}


bool MapViewerApp::loadMap(const std::filesystem::path& mapFile)
{
  const auto mapName = mapFile.stem().u8string();
  const auto correspondingWadFilename = rigel::strings::toLowercase(mapName);
  const auto wadFile = mapFile.parent_path().parent_path() / "LEVELS" /
    (correspondingWadFilename + ".wad");

  if (auto oWad = loadWadFile(wadFile))
  {
    if (auto oMap = loadMapfile(mapFile, *oWad))
    {
      mMapFileBrowser.SetPwd(mapFile.parent_path());
      mpMapRenderer = std::make_unique<MapRenderer>(*oMap, *oWad);

      const auto windowTitle =
        std::string(BASE_WINDOW_TITLE) + " - " + mapFile.filename().u8string();
      SDL_SetWindowTitle(mpWindow, windowTitle.c_str());
      return true;
    }
  }

  return false;
}


void MapViewerApp::handleEvent(const SDL_Event& event, double dt)
{
  if (mpMapRenderer)
  {
    mpMapRenderer->handleEvent(event, dt);
  }
}


void MapViewerApp::updateAndRender(
  double dt,
  const rigel::base::Size& windowSize)
{
  using namespace rigel;

  const auto& io = ImGui::GetIO();
  const auto toolbarHeight =
    int(ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeight() * 3);

  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({io.DisplaySize.x, float(toolbarHeight)});
  ImGui::Begin(
    "Toolbar",
    nullptr,
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNavInputs |
      ImGuiWindowFlags_NoNavFocus);

  ImGui::NewLine();
  ImGui::Spacing();

  mMapFileBrowser.Display();

  if (mMapFileBrowser.HasSelected())
  {
    const auto newMapFile = mMapFileBrowser.GetSelected();
    mMapFileBrowser.Close();

    const auto success = loadMap(newMapFile);

    if (!success)
    {
      const auto errorMsg =
        "Failed to load map '" + newMapFile.u8string() + "'!";
      SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, "Error", errorMsg.c_str(), nullptr);
    }
  }

  if (ImGui::Button("Load map"))
  {
    mMapFileBrowser.Open();
  }

  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1.0f);

  if (mpMapRenderer)
  {
    ImGui::SameLine();
    ImGui::Checkbox("Terrain", &mpMapRenderer->mShowTerrain);
    ImGui::SameLine();
    ImGui::Checkbox("Geometry", &mpMapRenderer->mShowGeometry);
    ImGui::SameLine();
    ImGui::Checkbox("Backface culling", &mpMapRenderer->mCullFaces);

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1.0f);
    ImGui::SameLine();

    ImGui::Text(
      "Camera: %f,%f,%f",
      mpMapRenderer->cameraPosition().x,
      mpMapRenderer->cameraPosition().y,
      mpMapRenderer->cameraPosition().z);
  }
  else
  {
    ImGui::SameLine();
    ImGui::Text("No map loaded!");
  }

  ImGui::End();


  // Clear toolbar portion of the window
  glViewport(
    0, windowSize.height - toolbarHeight, windowSize.width, toolbarHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);


  if (mpMapRenderer)
  {
    glViewport(0, 0, windowSize.width, windowSize.height - toolbarHeight);
    mpMapRenderer->updateAndRender(
      mMapFileBrowser.IsOpened() ? 0.0 : dt, windowSize);
  }
}

} // namespace saucer
