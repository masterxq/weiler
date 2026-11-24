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
