# SPDX-License-Identifier: GPL-2.0-or-later
# Workers outside their own territory: the headquarters of a (passive) human player sends out miners and stonemasons
# although there is no other warehouse. They must stay inside: before, they left, wandered off (also into foreign
# land), came back and were sent out again one by one for hours. Workers at work (stonemasons, woodcutters,
# geologists) may leave the territory as in the original. No figure may walk to an object of another player.
# Run twice: same result.
# Variables: HEADLESS (program), DATA (game data), WORK (work folder)

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(MAKE_DIRECTORY "${WORK}")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")

function(run_watch outVar)
    execute_process(COMMAND "${HEADLESS}" --data "${DATA}" --map DATA/MAPS4/KARTE01.WLD --human --ais 2 --seed 7
                            --minutes 30 --foreign --send-out 11 5
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "weiler-headless failed (${rc}):\n${out}\n${err}")
    endif()
    set(${outVar} "${out}" PARENT_SCOPE)
endfunction()

run_watch(out1)
run_watch(out2)
string(REGEX MATCHALL "\\[(ausserhalb|fremd|fremdziel)-summe\\][^\n]*" sum1 "${out1}")
string(REGEX MATCHALL "\\[(ausserhalb|fremd|fremdziel)-summe\\][^\n]*" sum2 "${out2}")
foreach(line IN LISTS sum1)
    message(STATUS "${line}")
endforeach()
if(NOT sum1 STREQUAL sum2)
    message(FATAL_ERROR "Not deterministic:\n${sum1}\n${sum2}")
endif()
if(out1 MATCHES "\\[auszug\\][^\n]* P0 " OR out1 MATCHES "\\[fremd\\][^\n]* P0 [^\n]*irrt umher")
    message(FATAL_ERROR "Figures sent out of the headquarters wander off:\n${out1}")
endif()
if(NOT out1 MATCHES "Steinmetz / arbeitet")
    message(FATAL_ERROR "Expected stonemasons at work outside the territory (check of the watch itself):\n${out1}")
endif()
if(NOT out1 MATCHES "eines anderen Spielers: 0\n")
    message(FATAL_ERROR "A figure walks to an object of another player:\n${out1}")
endif()
