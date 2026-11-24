# SPDX-License-Identifier: GPL-2.0-or-later
# Kopfloser Kerntest: neue Partie mit KI-Gegnern simulieren, speichern, laden, deterministisch weiterlaufen.
# Variablen: HEADLESS (Programm), DATA (Spieldaten), WORK (Arbeitsordner), MAP, MINUTES

if(NOT EXISTS "${DATA}/DATA/RESOURCE.DAT")
    message(STATUS "Game data not found in '${DATA}', skipping")
    return()
endif()
file(MAKE_DIRECTORY "${WORK}")
set(ENV{WEILER_USERDATA} "${WORK}/userdata")

function(run_headless outVar)
    execute_process(COMMAND "${HEADLESS}" --data "${DATA}" --hash ${ARGN}
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "weiler-headless ${ARGN} failed (${rc}):\n${out}\n${err}")
    endif()
    string(REGEX MATCH "statehash [0-9a-f]+" hash "${out}")
    if(NOT hash)
        message(FATAL_ERROR "No state hash in output:\n${out}")
    endif()
    string(REGEX MATCH "GF [0-9]+ \\([^\n]*" status "${out}")
    message(STATUS "${status}")
    set(${outVar} "${hash}" PARENT_SCOPE)
endfunction()

run_headless(h1 --map "${MAP}" --ais 3 --seed 42 --minutes ${MINUTES} --save "${WORK}/a.sav")
run_headless(h2 --map "${MAP}" --ais 3 --seed 42 --minutes ${MINUTES} --save "${WORK}/b.sav")
if(NOT h1 STREQUAL h2)
    message(FATAL_ERROR "New game not deterministic: ${h1} vs ${h2}")
endif()
# Loading restores exactly the saved state
run_headless(r1 --load "${WORK}/a.sav" --seed 7 --gfs 0)
if(NOT r1 STREQUAL h1)
    message(FATAL_ERROR "Savegame round trip changed the game state: ${h1} vs ${r1}")
endif()
run_headless(l1 --load "${WORK}/a.sav" --seed 7 --minutes ${MINUTES})
run_headless(l2 --load "${WORK}/b.sav" --seed 7 --minutes ${MINUTES})
if(NOT l1 STREQUAL l2)
    message(FATAL_ERROR "Loaded game not deterministic: ${l1} vs ${l2}")
endif()
# Without a seed the random generators are seeded from the savegame: loading twice continues the same way
run_headless(d1 --load "${WORK}/a.sav" --minutes 5)
run_headless(d2 --load "${WORK}/b.sav" --minutes 5)
if(NOT d1 STREQUAL d2)
    message(FATAL_ERROR "Loaded game without seed not reproducible: ${d1} vs ${d2}")
endif()
message(STATUS "Deterministic: ${h1}, after load: ${l1}")
