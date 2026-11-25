# SPDX-License-Identifier: GPL-2.0-or-later
# Every game is recorded: build a quarry and a woodcutter over the interface, simulate, quit; the recording in the
# user data plays back with weiler-headless exactly like the game (checksums of all command frames).
# Variables: WEILER, HEADLESS, DATA, WORK, SCRIPT

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/shots" "${WORK}/userdata")
file(WRITE "${WORK}/userdata/settings.ini" "tutorial.done=1\n")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")
set(ENV{SDL_VIDEO_DRIVER} offscreen)
set(ENV{SDL_AUDIO_DRIVER} dummy)
execute_process(COMMAND "${WEILER}" --data "${DATA}" --size 1080x2400 --scale 3 --script "${SCRIPT}"
                        --map DATA/MAPS4/KARTE01.WLD --ais 1
                WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 900)
file(WRITE "${WORK}/output.log" "${out}\n${err}")
if(NOT rc EQUAL 0 OR out MATCHES "\\[script\\] FAIL")
    message(FATAL_ERROR "${SCRIPT} failed (exit code ${rc}), log: ${WORK}/output.log")
endif()
file(GLOB recordings "${WORK}/userdata/REPLAYS/*.rpl")
list(LENGTH recordings n)
if(NOT n EQUAL 1)
    message(FATAL_ERROR "Expected one recording, found ${n}: ${recordings}")
endif()
file(GLOB logs "${WORK}/userdata/LOGS/*.log")
file(READ "${logs}" log)
if(NOT log MATCHES "Spielaufzeichnung: ")
    message(FATAL_ERROR "The log does not name the recording: ${logs}")
endif()
execute_process(COMMAND "${HEADLESS}" --data "${DATA}" --replay "${recordings}" --foreign
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
file(WRITE "${WORK}/replay.log" "${out}\n${err}")
string(REGEX MATCH "replay: [^\n]*" result "${out}")
string(REGEX MATCH "GF [0-9]+ \\([^\n]*" status "${out}")
if(NOT rc EQUAL 0 OR NOT result MATCHES "matches the recording \\(end GF [1-9]")
    message(FATAL_ERROR "Playback of the recording failed: ${result}\nLog: ${WORK}/replay.log")
endif()
message(STATUS "${status}; ${result}")
