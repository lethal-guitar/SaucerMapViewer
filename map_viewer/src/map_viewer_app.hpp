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

#include <rigel/base/clock.hpp>
#include <rigel/base/spatial_types.hpp>
#include <rigel/ui/fps_display.hpp>

#include <SDL_events.h>
#include <imfilebrowser.h>

#include <filesystem>
#include <memory>


namespace saucer
{

class MapRenderer;


constexpr const auto BASE_WINDOW_TITLE = "Attack of the Saucerman Map Viewer";


class MapViewerApp
{
public:
  MapViewerApp(SDL_Window* pWindow);
  ~MapViewerApp();

  bool runOneFrame();

  bool loadMap(const std::filesystem::path& mapFile);

private:
  void handleEvent(const SDL_Event& event, double dt);
  void updateAndRender(double dt, const rigel::base::Size& windowSize);

  SDL_Window* mpWindow;
  rigel::ui::FpsDisplay mFpsDisplay;
  rigel::base::Clock::time_point mLastTime{};

  std::unique_ptr<MapRenderer> mpMapRenderer;
  ImGui::FileBrowser mMapFileBrowser;
};

} // namespace saucer
