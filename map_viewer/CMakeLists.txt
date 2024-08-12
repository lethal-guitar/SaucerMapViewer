cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(SaucerMapViewer VERSION 0.1.0)

include(RigelLib/setup.cmake)

rigel_standard_project_setup()


# Build targets
###############################################################################

add_subdirectory(3rd_party/imgui-filebrowser)

add_executable(SaucerMapViewer WIN32)

target_sources(SaucerMapViewer PRIVATE
    src/main.cpp
    src/map_file.cpp
    src/map_file.hpp
    src/map_renderer.cpp
    src/map_renderer.hpp
    src/map_viewer_app.cpp
    src/map_viewer_app.hpp
    src/mesh.cpp
    src/mesh.hpp
    src/saucer_files_common.cpp
    src/saucer_files_common.hpp
    src/wad_file.cpp
    src/wad_file.hpp
)
target_include_directories(SaucerMapViewer
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)
target_link_libraries(SaucerMapViewer PRIVATE
    RigelLib::RigelLib
    SDL2::Main
    imgui-filebrowser
)

rigel_enable_warnings(SaucerMapViewer)
