# Runs the app headless with --forged --campaign: the first run forges and saves the world, a relaunch with
# the same campaign (even under another --seed, which the stored seed overrides) gets the same world hash and scene state, and a damaged world is refused with a message
# and a failing exit code rather than a crash or a silent reforge. Needs ICHOR and SCRATCH.
string(RANDOM LENGTH 12 ALPHABET "abcdefghijklmnopqrstuvwxyz0123456789" suffix)
set(saves "${SCRATCH}/forged_campaign_${suffix}")
file(REMOVE_RECURSE "${saves}")
file(MAKE_DIRECTORY "${saves}")

function(campaign_run seed out_code out_text)
    execute_process(COMMAND "${ICHOR}" --headless --forged --grid 64 --seed ${seed} --frames 20 --saves "${saves}" --campaign alpha
        RESULT_VARIABLE code OUTPUT_VARIABLE text ERROR_VARIABLE text)
    set(${out_code} "${code}" PARENT_SCOPE)
    set(${out_text} "${text}" PARENT_SCOPE)
endfunction()

function(world_hash text out_var)
    string(REGEX MATCH "forged world ([0-9a-f]+)," hit "${text}")
    if(NOT hit)
        message(FATAL_ERROR "no forged world hash in the log:\n${text}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

campaign_run(1 code1 text1)
if(NOT code1 EQUAL 0 OR NOT text1 MATCHES "the forged world is saved")
    file(REMOVE_RECURSE "${saves}")
    message(FATAL_ERROR "the first run did not save the world (exit ${code1}):\n${text1}")
endif()
world_hash("${text1}" hash1)

campaign_run(9 code2 text2)
if(NOT code2 EQUAL 0 OR NOT text2 MATCHES "loading campaign 'alpha'" OR NOT text2 MATCHES "the world matches the stored one" OR text2 MATCHES "is saved")
    file(REMOVE_RECURSE "${saves}")
    message(FATAL_ERROR "the relaunch did not load the saved world (exit ${code2}):\n${text2}")
endif()
world_hash("${text2}" hash2)
string(REGEX MATCH "state ([0-9a-f]+)" s1 "${text1}")
set(state1 "${CMAKE_MATCH_1}")
string(REGEX MATCH "state ([0-9a-f]+)" s2 "${text2}")
set(state2 "${CMAKE_MATCH_1}")
if(NOT hash1 STREQUAL hash2 OR NOT state1 STREQUAL state2 OR state1 STREQUAL "")
    file(REMOVE_RECURSE "${saves}")
    message(FATAL_ERROR "the relaunch gave another world: ${hash1}/${state1} then ${hash2}/${state2}")
endif()

file(WRITE "${saves}/alpha/world/forged_world.bin" "this is not a forged world")
campaign_run(1 code3 text3)
if(code3 EQUAL 0 OR NOT text3 MATCHES "damaged world" OR text3 MATCHES "forging seed")
    file(REMOVE_RECURSE "${saves}")
    message(FATAL_ERROR "a damaged world was not refused cleanly (exit ${code3}):\n${text3}")
endif()

file(REMOVE_RECURSE "${saves}")
message(STATUS "campaign save and load agree: world ${hash1}, state ${state1}")
