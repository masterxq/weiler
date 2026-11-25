# SPDX-License-Identifier: GPL-2.0-or-later
# Game data missing, chosen, remembered, medium removed and inserted again, folder with lower case names.
# Runs three offscreen scripts (ui/datasetup1..3.txt) with an own home folder; the game data is linked in.
# Variables: WEILER (program), DATA (game data), WORK (working folder), SCRIPTS (folder of the scripts)

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/shots" "${WORK}/home/Leer/Notizen" "${WORK}/home/Karte/Spiele/Gold" "${WORK}/xdg"
     "${WORK}/userdata")
file(WRITE "${WORK}/userdata/settings.ini" "tutorial.done=1\n")
# Medium "Karte" with the game folder somewhere below
file(CREATE_LINK "${DATA}/DATA" "${WORK}/home/Karte/Spiele/Gold/DATA" SYMBOLIC)
file(CREATE_LINK "${DATA}/GFX" "${WORK}/home/Karte/Spiele/Gold/GFX" SYMBOLIC)
# A copy with lower case names (links per file)
file(GLOB_RECURSE files RELATIVE "${DATA}" "${DATA}/DATA/*" "${DATA}/GFX/*")
foreach(f IN LISTS files)
    string(TOLOWER "${f}" lower)
    get_filename_component(dir "${WORK}/home/klein/s2/${lower}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(CREATE_LINK "${DATA}/${f}" "${WORK}/home/klein/s2/${lower}" SYMBOLIC)
endforeach()

set(ENV{HOME} "${WORK}/home")
set(ENV{XDG_DATA_HOME} "${WORK}/xdg")
set(ENV{WEILER_PACKAGE_DATA} "${WORK}/no-package")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")
set(ENV{WEILER_DATA} "")
set(ENV{SDL_VIDEO_DRIVER} offscreen)
set(ENV{SDL_AUDIO_DRIVER} dummy)

function(run n)
    execute_process(COMMAND "${WEILER}" --size 1080x2400 --scale 3 --script "${SCRIPTS}/datasetup${n}.txt"
                    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
                    TIMEOUT 300)
    file(WRITE "${WORK}/output${n}.log" "${out}\n${err}")
    string(REGEX MATCHALL "\\[script\\] FAIL[^\n]*" fails "${out}\n${err}")
    if(NOT rc EQUAL 0 OR fails)
        string(REPLACE ";" "\n" fails "${fails}")
        message(FATAL_ERROR "datasetup${n}.txt failed (exit code ${rc}):\n${fails}\nLog: ${WORK}/output${n}.log")
    endif()
endfunction()

function(expect_remembered path)
    file(STRINGS "${WORK}/userdata/settings.ini" line REGEX "^data\\.path=")
    if(NOT line STREQUAL "data.path=${path}")
        message(FATAL_ERROR "remembered folder: '${line}', expected '${path}'")
    endif()
endfunction()

# 1. Nothing found; a folder without data; no folder dialog -> own browser, choose "Karte"
run(1)
expect_remembered("${WORK}/home/Karte/Spiele/Gold")
# 2. Medium removed: message with the path, retry fails, medium back, retry starts the game
file(RENAME "${WORK}/home/Karte" "${WORK}/home/Karte-weg")
run(2)
expect_remembered("${WORK}/home/Karte/Spiele/Gold")
# 3. Medium removed again, another folder with lower case names: taken over into the user folder
file(RENAME "${WORK}/home/Karte" "${WORK}/home/Karte-weg")
run(3)
expect_remembered("${WORK}/xdg/weiler/S2")
if(NOT EXISTS "${WORK}/xdg/weiler/S2/DATA/MBOB/ROM_Z.LST" OR EXISTS "${WORK}/xdg/weiler/S2/VIDEO")
    message(FATAL_ERROR "game data not taken over as expected into ${WORK}/xdg/weiler/S2")
endif()
message(STATUS "datasetup: ok, screenshots in ${WORK}/shots")
