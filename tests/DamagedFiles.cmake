# SPDX-License-Identifier: GPL-2.0-or-later
# Robustness: damaged copies of a savegame and of a map are loaded; a crash fails the test.
# Variables: HEADLESS (program), DATA (game data), WORK (working folder), SAVE (savegame)

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(MAKE_DIRECTORY "${WORK}")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")
foreach(args IN ITEMS "--load;${SAVE}" "--map;DATA/MAPS4/KARTE01.WLD;--ais;1")
    execute_process(COMMAND "${HEADLESS}" --data "${DATA}" ${args} --damage-test 60
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "weiler-headless ${args} --damage-test failed (${rc}):\n${out}\n${err}")
    endif()
    string(REGEX MATCH "damage test[^\n]*" summary "${out}")
    message(STATUS "${summary}")
endforeach()
