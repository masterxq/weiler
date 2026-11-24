# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Boost 1.74 REQUIRED COMPONENTS program_options)

# Engine: game session on top of the RTTR core (no user interface)
add_library(weiler_engine STATIC
    engine/DataPaths.cpp
    engine/Session.cpp
)
target_include_directories(weiler_engine PUBLIC engine)
target_link_libraries(weiler_engine PUBLIC s25Main)
target_compile_features(weiler_engine PUBLIC cxx_std_20)
target_compile_definitions(weiler_engine PRIVATE
    WEILER_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/third_party/rttr/data"
    WEILER_INSTALL_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/weiler")

add_executable(weiler-headless tools/HeadlessMain.cpp)
target_link_libraries(weiler-headless PRIVATE weiler_engine Boost::program_options)

# Game application
find_package(SDL3 REQUIRED CONFIG)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)

file(GLOB_RECURSE APP_SRC CONFIGURE_DEPENDS app/*.cpp app/*.h)
add_executable(weiler ${APP_SRC})
target_include_directories(weiler PRIVATE app)
target_link_libraries(weiler PRIVATE weiler_engine SDL3::SDL3 PkgConfig::FREETYPE PkgConfig::HARFBUZZ PkgConfig::GLES
    Boost::program_options)
target_compile_definitions(weiler PRIVATE WEILER_FONT_DIR="${PROJECT_SOURCE_DIR}/data/fonts")
