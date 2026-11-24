# SPDX-License-Identifier: GPL-2.0-or-later

set(WEILER_TEST_DATA "$ENV{WEILER_DATA}" CACHE PATH "Original game data for the tests (tests are skipped without)")

add_test(NAME headless_determinism
    COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:weiler-headless> -DDATA=${WEILER_TEST_DATA}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/headless -DMAP=DATA/MAPS4/KARTE01.WLD -DMINUTES=30
            -P ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessDeterminism.cmake)
