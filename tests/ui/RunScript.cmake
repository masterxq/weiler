# SPDX-License-Identifier: GPL-2.0-or-later
# Runs one interface test script offscreen with a fresh user data folder.
# Variables: WEILER (program), DATA (game data), WORK (working folder), SCRIPT, SIZE, FIRST_START (ON: no settings),
# ARGS (further options, "|"-separated)

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/shots/long" "${WORK}/userdata")
if(NOT FIRST_START)
    file(WRITE "${WORK}/userdata/settings.ini" "tutorial.done=1\n")
endif()
string(REPLACE "|" ";" ARGS "${ARGS}")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")
set(ENV{SDL_VIDEO_DRIVER} offscreen)
set(ENV{SDL_AUDIO_DRIVER} dummy)
execute_process(COMMAND "${WEILER}" --data "${DATA}" --size ${SIZE} --scale 3 --script "${SCRIPT}" ${ARGS}
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
                TIMEOUT 900)
file(WRITE "${WORK}/output.log" "${out}\n${err}")
string(REGEX MATCHALL "(\\[script\\] FAIL|Error during saving|Could not start the game)[^\n]*" fails "${out}\n${err}")
if(NOT rc EQUAL 0 OR fails)
    string(REPLACE ";" "\n" fails "${fails}")
    message(FATAL_ERROR "${SCRIPT} failed (exit code ${rc}):\n${fails}\nLog: ${WORK}/output.log")
endif()
message(STATUS "${SCRIPT}: ok, screenshots in ${WORK}/shots")
