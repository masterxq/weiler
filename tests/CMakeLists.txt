# SPDX-License-Identifier: GPL-2.0-or-later

set(WEILER_TEST_DATA "$ENV{WEILER_DATA}" CACHE PATH "Original game data for the tests (tests are skipped without)")

add_test(NAME headless_determinism
    COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:weiler-headless> -DDATA=${WEILER_TEST_DATA}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/headless -DMAP=DATA/MAPS4/KARTE01.WLD -DMINUTES=30
            -P ${CMAKE_CURRENT_SOURCE_DIR}/HeadlessDeterminism.cmake)
set_tests_properties(headless_determinism PROPERTIES FIXTURES_SETUP savegame)

# Damaged savegames and maps must be rejected with an error (or play), never crash
add_test(NAME damaged_files
    COMMAND ${CMAKE_COMMAND} -DHEADLESS=$<TARGET_FILE:weiler-headless> -DDATA=${WEILER_TEST_DATA}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/damaged -DSAVE=${CMAKE_CURRENT_BINARY_DIR}/headless/a.sav
            -P ${CMAKE_CURRENT_SOURCE_DIR}/DamagedFiles.cmake)
set_tests_properties(damaged_files PROPERTIES FIXTURES_REQUIRED savegame)

# weiler-pack-data with a fake game folder (needs no game data)
add_test(NAME pack_data
    COMMAND ${CMAKE_COMMAND} -DPACKER=${PROJECT_SOURCE_DIR}/tools/weiler-pack-data
            -DLIST=${PROJECT_SOURCE_DIR}/data/gamedata.list -DWORK=${CMAKE_CURRENT_BINARY_DIR}/pack_data
            -P ${CMAKE_CURRENT_SOURCE_DIR}/PackData.cmake)

# Interface scripts in the offscreen test mode (label "ui")
function(add_ui_test name size)
    cmake_parse_arguments(UI "FIRST_START" "SCRIPT" "" ${ARGN})
    if(NOT UI_SCRIPT)
        set(UI_SCRIPT ${name})
    endif()
    string(REPLACE ";" "|" args "${UI_UNPARSED_ARGUMENTS}")
    add_test(NAME ui_${name}
        COMMAND ${CMAKE_COMMAND} -DWEILER=$<TARGET_FILE:weiler> -DDATA=${WEILER_TEST_DATA}
                -DWORK=${CMAKE_CURRENT_BINARY_DIR}/ui/${name} -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/ui/${UI_SCRIPT}.txt
                -DSIZE=${size} -DFIRST_START=${UI_FIRST_START} "-DARGS=${args}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/ui/RunScript.cmake)
    set_tests_properties(ui_${name} PROPERTIES LABELS ui)
endfunction()

set(_skirmish --map DATA/MAPS4/KARTE01.WLD --ais 1)
add_ui_test(tut 1080x2400 FIRST_START)
add_ui_test(sheets 1080x2400 ${_skirmish})
add_ui_test(misc 1080x2400 ${_skirmish})
add_ui_test(build 1080x2400 ${_skirmish})
add_ui_test(taps 1080x2400 ${_skirmish})
add_ui_test(save 1080x2400 ${_skirmish})
add_ui_test(camp 1080x2400)
add_ui_test(land 2400x1080)
add_ui_test(land_hd 1920x1080 SCRIPT land)
add_ui_test(attack 1080x2400 --map DATA/MAPS3/AKARTE01.WLD --ais 1 --warmup 60000 --autoplay)
add_ui_test(goal 1080x2400 --map DATA/MAPS3/AKARTE01.WLD --ais 1 --warmup 60000 --autoplay --objective conquer)
add_ui_test(names 1080x2400 --map DATA/MAPS4/KARTE01.WLD --ais 1 --warmup 36000 --autoplay)

# Game data missing: choosing a folder, remembering it, removable medium gone and back, lower case file names
add_test(NAME ui_datasetup
    COMMAND ${CMAKE_COMMAND} -DWEILER=$<TARGET_FILE:weiler> -DDATA=${WEILER_TEST_DATA}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/ui/datasetup -DSCRIPTS=${CMAKE_CURRENT_SOURCE_DIR}/ui
            -P ${CMAKE_CURRENT_SOURCE_DIR}/ui/DataSetup.cmake)
set_tests_properties(ui_datasetup PROPERTIES LABELS ui)
