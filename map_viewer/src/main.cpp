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

#include <rigel/base/warnings.hpp>
#include <rigel/bootstrap.hpp>

RIGEL_DISABLE_WARNINGS
#include <SDL.h>
#include <imgui.h>
#include <lyra/lyra.hpp>
RIGEL_RESTORE_WARNINGS

#include <optional>


int main(int argc, char** argv)
{
  std::string mapFile;

  const auto maybeErrorCode = rigel::parseArgs(
    argc,
    argv,
    [&mapFile](lyra::cli& argsParser) {
      argsParser |= lyra::arg(mapFile, "map file to load");
    },
    []() { return true; });

  if (maybeErrorCode)
  {
    return *maybeErrorCode;
  }


  rigel::WindowConfig windowConfig;
  windowConfig.windowTitle = saucer::BASE_WINDOW_TITLE;
  windowConfig.fullscreen = false;
  windowConfig.windowWidth = -1;
  windowConfig.windowHeight = -1;
  windowConfig.depthBufferBits = 24;

  std::optional<saucer::MapViewerApp> oMapViewer;

  return rigel::runApp(
    windowConfig,
    [&](SDL_Window* pWindow) {
      SDL_EnableScreenSaver();
      SDL_ShowCursor(SDL_ENABLE);

      oMapViewer.emplace(pWindow);

      if (!mapFile.empty())
      {
        oMapViewer->loadMap(mapFile);
      }
    },
    [&](SDL_Window*) { return oMapViewer->runOneFrame(); });
}
