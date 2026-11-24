# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)
project(weiler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(WEILER_USE_CCACHE "Use ccache if available" ON)
set(WEILER_CCACHE_DIR "" CACHE PATH "Cache directory for ccache (default: <build>/ccache)")
find_program(CCACHE_PROGRAM ccache)
if(WEILER_USE_CCACHE AND CCACHE_PROGRAM)
    if(NOT WEILER_CCACHE_DIR)
        set(WEILER_CCACHE_DIR ${CMAKE_BINARY_DIR}/ccache)
    endif()
    set(_launcher ${CMAKE_COMMAND} -E env CCACHE_DIR=${WEILER_CCACHE_DIR} ${CCACHE_PROGRAM})
    set(CMAKE_C_COMPILER_LAUNCHER ${_launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_launcher})
endif()

include(CTest)

add_subdirectory(third_party/rttr)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
