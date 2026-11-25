# SPDX-License-Identifier: GPL-2.0-or-later
# Game recordings (RTTR replays): a recorded game played back without AI gives the same game state, from a new game
# and from a savegame; only the newest 5 recordings are kept.
# Variables: HEADLESS (program), DATA (game data), WORK (work folder)

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")

function(run outVar)
    execute_process(COMMAND "${HEADLESS}" --data "${DATA}" ${ARGN}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "weiler-headless ${ARGN} failed (${rc}):\n${out}\n${err}")
    endif()
    set(${outVar} "${out}" PARENT_SCOPE)
endfunction()

function(check_replay name recordOut)
    string(REGEX MATCH "statehash [0-9a-f]+" hash "${recordOut}")
    string(REGEX MATCH "recording ([^\n]+)" _ "${recordOut}")
    set(file "${CMAKE_MATCH_1}")
    if(NOT hash OR NOT EXISTS "${file}")
        message(FATAL_ERROR "${name}: no recording or hash:\n${recordOut}")
    endif()
    run(out --replay "${file}" --hash)
    string(REGEX MATCH "statehash [0-9a-f]+" replayHash "${out}")
    if(NOT out MATCHES "replay: matches the recording" OR NOT replayHash STREQUAL hash)
        message(FATAL_ERROR "${name}: playback differs (${hash} vs ${replayHash}):\n${out}")
    endif()
    message(STATUS "${name}: playback equal (${hash})")
endfunction()

# New game with commands of the local player (send out) and of 3 AI players
run(out --map DATA/MAPS4/KARTE01.WLD --ais 3 --seed 5 --minutes 20 --hash --record "${WORK}/rec" --send-out 11 5)
check_replay("new game" "${out}")
run(out --map DATA/MAPS4/KARTE02.WLD --ais 2 --seed 3 --minutes 10 --save "${WORK}/s.sav")
run(out --load "${WORK}/s.sav" --minutes 15 --hash --record "${WORK}/rec")
check_replay("savegame" "${out}")

foreach(i RANGE 1 5)
    run(out --map DATA/MAPS4/KARTE01.WLD --ais 2 --gfs 10 --record "${WORK}/rec")
endforeach()
file(GLOB files "${WORK}/rec/*.rpl")
list(LENGTH files n)
if(NOT n EQUAL 5)
    message(FATAL_ERROR "Expected the newest 5 recordings, found ${n}: ${files}")
endif()
