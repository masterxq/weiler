# SPDX-License-Identifier: GPL-2.0-or-later
# Workers outside their own territory: the headquarters of player 0 sends out miners and stonemasons although
# there is no other warehouse. They must stay inside (they used to wander off into foreign land). Workers at work
# (stonemasons, woodcutters, geologists) may leave the territory as in the original. Run twice: same result.
# Variables: HEADLESS (program), DATA (game data), WORK (work folder)

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(MAKE_DIRECTORY "${WORK}")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")

function(run_watch outVar)
    execute_process(COMMAND "${HEADLESS}" --data "${DATA}" --map DATA/MAPS4/KARTE01.WLD --ais 3 --seed 7
                            --minutes 15 --foreign --send-out 11 5
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "weiler-headless failed (${rc}):\n${out}\n${err}")
    endif()
    set(${outVar} "${out}" PARENT_SCOPE)
endfunction()

run_watch(out1)
run_watch(out2)
string(REGEX MATCHALL "\\[fremd-summe\\][^\n]*" sum1 "${out1}")
string(REGEX MATCHALL "\\[fremd-summe\\][^\n]*" sum2 "${out2}")
foreach(line IN LISTS sum1)
    message(STATUS "${line}")
endforeach()
if(NOT sum1 STREQUAL sum2)
    message(FATAL_ERROR "Not deterministic:\n${sum1}\n${sum2}")
endif()
if(out1 MATCHES "wandert / aus Lager")
    message(FATAL_ERROR "Figures sent out of a warehouse wander off:\n${out1}")
endif()
if(NOT out1 MATCHES "Steinmetz / arbeitet")
    message(FATAL_ERROR "Expected stonemasons at work outside the territory (check of the watch itself):\n${out1}")
endif()
