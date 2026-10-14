# Runs ichor_forge against a data directory built for one case and checks the
# outcome. Needs FORGE, KIND, REPO_DATA and SCRATCH.
#   KIND=repo        the repo data/ (trailing slash) forges and reports its digest
#   KIND=default     no --data: the baked-in repo data/ gives the same digest as the explicit path
#   KIND=missing     no such directory
#   KIND=empty       a directory with no tables
#   KIND=file        the path is a file
#   KIND=malformed   one good table and one file that is not JSON
#   KIND=badrecord   a record without an id
# Every KIND but repo must exit non-zero, print a data error and leave no world behind.
string(RANDOM LENGTH 8 suffix)
set(root "${SCRATCH}/forge_data_${KIND}_${suffix}")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")
set(out "${root}/world")

if(KIND STREQUAL "repo" OR KIND STREQUAL "default")
    set(data "${REPO_DATA}/")
elseif(KIND STREQUAL "missing")
    set(data "${root}/nothing")
elseif(KIND STREQUAL "empty")
    set(data "${root}/data")
    file(MAKE_DIRECTORY "${data}")
elseif(KIND STREQUAL "file")
    set(data "${root}/data.json")
    file(WRITE "${data}" "[]")
elseif(KIND STREQUAL "malformed")
    set(data "${root}/data")
    file(WRITE "${data}/good/a.json" "[{\"id\": \"one\"}]")
    file(WRITE "${data}/bad/b.json" "[{\"id\": ")
elseif(KIND STREQUAL "badrecord")
    set(data "${root}/data")
    file(WRITE "${data}/good/a.json" "[{\"name\": \"no id\"}]")
else()
    message(FATAL_ERROR "unknown KIND ${KIND}")
endif()

set(forge_args "${out}" 7 256 4 64)
if(NOT KIND STREQUAL "default")
    list(APPEND forge_args --data "${data}")
endif()
execute_process(
    COMMAND "${FORGE}" ${forge_args}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(log "${stdout}${stderr}")

if(KIND STREQUAL "repo" OR KIND STREQUAL "default")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "forge failed on the repo data (${result}): ${log}")
    endif()
    if(NOT log MATCHES "data digest ([0-9a-f]+)")
        message(FATAL_ERROR "forge did not report the data digest: ${log}")
    endif()
    set(digest "${CMAKE_MATCH_1}")
    if(KIND STREQUAL "default")
        execute_process(COMMAND "${FORGE}" "${out}-explicit" 7 256 4 64 --data "${REPO_DATA}"
            OUTPUT_VARIABLE explicit_out ERROR_VARIABLE explicit_err)
        if(NOT "${explicit_out}${explicit_err}" MATCHES "data digest ${digest} ")
            message(FATAL_ERROR "default data and --data disagree: ${log} vs ${explicit_out}${explicit_err}")
        endif()
    endif()
    if(NOT EXISTS "${out}/manifest.iworld")
        message(FATAL_ERROR "forge wrote no world: ${log}")
    endif()
else()
    if(result EQUAL 0)
        message(FATAL_ERROR "forge accepted bad data (${KIND}): ${log}")
    endif()
    if(NOT log MATCHES "data: .*${EXPECT_ERROR}")
        message(FATAL_ERROR "forge gave no data error matching '${EXPECT_ERROR}' (${KIND}): ${log}")
    endif()
    if(EXISTS "${out}")
        message(FATAL_ERROR "forge left a partial world behind (${KIND})")
    endif()
endif()
file(REMOVE_RECURSE "${root}")
