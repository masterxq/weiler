# SPDX-License-Identifier: GPL-2.0-or-later
# Build of the frozen Return to the Roots core (see COMMITS and THIRD_PARTY.md)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/libutil/cmake)
set(BUILD_TESTING OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(RTTR_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

find_package(Boost 1.74 REQUIRED COMPONENTS filesystem iostreams locale nowide)
find_package(Lua 5.1 REQUIRED)
find_package(BZip2 1.0.6 REQUIRED)

add_subdirectory(external/libendian)
add_subdirectory(external/libutil)
add_subdirectory(external/mygettext)
add_subdirectory(external/libsiedler2)

add_library(kaguya INTERFACE)
target_include_directories(kaguya SYSTEM INTERFACE external/kaguya/include)

add_subdirectory(libs/common)

# rttrConfig: paths are set at runtime by the frontend (RttrConfig::overridePathMapping)
set(RTTR_SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})
set(RTTR_BINDIR "bin")
set(RTTR_EXTRA_BINDIR "bin")
set(RTTR_DATADIR "share/weiler")
set(RTTR_GAMEDIR "share/weiler/S2")
set(RTTR_LIBDIR "lib")
set(RTTR_DRIVERDIR "lib")
set(RTTR_VERSION ${PROJECT_VERSION})
set(RTTR_REVISION "weiler")
set(RTTR_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
add_library(rttrConfig STATIC
    libs/rttrConfig/src/RTTR_Version.cpp
    libs/rttrConfig/src/RttrConfig.cpp
)
target_include_directories(rttrConfig PUBLIC libs/rttrConfig/src PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/rttrConfig)
target_link_libraries(rttrConfig PUBLIC s25util::common Boost::filesystem PRIVATE s25Common)
configure_file(libs/rttrConfig/build_paths.h.cmake rttrConfig/build_paths.h @ONLY)
string(TIMESTAMP RTTR_BUILD_DATE "%Y%m%d")
configure_file(libs/rttrConfig/build_version_defines.h.cmake rttrConfig/build_version_defines.h @ONLY)

file(GLOB_RECURSE GAMEDATA_SRC CONFIGURE_DEPENDS libs/libGamedata/*.cpp)
add_library(gamedata STATIC ${GAMEDATA_SRC})
target_include_directories(gamedata PUBLIC libs/libGamedata)
target_include_directories(gamedata SYSTEM PUBLIC ${LUA_INCLUDE_DIR})
target_link_libraries(gamedata PUBLIC s25util::common mygettext s25Common kaguya ${LUA_LIBRARIES} ${CMAKE_DL_LIBS}
    PRIVATE rttrConfig Boost::nowide)

file(GLOB_RECURSE S25MAIN_SRC CONFIGURE_DEPENDS libs/s25main/*.cpp)
add_library(s25Main STATIC ${S25MAIN_SRC})
target_include_directories(s25Main PUBLIC libs/s25main PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/s25main)
target_link_libraries(s25Main PUBLIC siedler2 s25util::common s25util::log mygettext s25Common rttrConfig gamedata
    Boost::filesystem PRIVATE BZip2::BZip2 Boost::iostreams Boost::locale Boost::nowide)
set(RTTR_BACKTRACE_HAS_DBGHELP OFF)
set(RTTR_BACKTRACE_HAS_FUNCTION OFF)
configure_file(libs/s25main/backtrace_config.h.cmake s25main/backtrace_config.h @ONLY)
