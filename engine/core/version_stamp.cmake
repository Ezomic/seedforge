# Sets <out> to the short commit hash of <source_dir>, with "-dirty" when tracked or untracked (not ignored)
# files differ from it, or to "unknown" when there is no git or no repository (a source tarball, bin/package output).
function(ichor_version_stamp out source_dir git)
    set(stamp unknown)
    if(git)
        execute_process(COMMAND ${git} rev-parse --short HEAD WORKING_DIRECTORY ${source_dir}
            RESULT_VARIABLE code OUTPUT_VARIABLE hash ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(code EQUAL 0 AND hash MATCHES "^[0-9a-f]+$")
            set(stamp ${hash})
            execute_process(COMMAND ${git} status --porcelain WORKING_DIRECTORY ${source_dir}
                RESULT_VARIABLE code OUTPUT_VARIABLE changes ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT code EQUAL 0 OR NOT changes STREQUAL "")
                string(APPEND stamp -dirty)
            endif()
        endif()
    endif()
    set(${out} ${stamp} PARENT_SCOPE)
endfunction()
