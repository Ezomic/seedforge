# ichor_check_fp_flags(<dir>): fails the configure if any flag under <dir> lets the compiler
# reassociate or fuse floating point operations, since the simulation must round the same way
# on every platform (SEED-701). Call it after every target exists.
#
# It reads the text of the flags, before generator expressions are evaluated, so a flag behind
# any condition counts. It sees CMAKE_CXX_FLAGS and the per-config and linker variants, and the
# compile and link options of every target and of every source file of those targets, including
# imported targets' interface options. On clang and gcc every compiled target must also carry
# -ffp-contract=off.
#
# It does not see flags from outside CMake (the CL and _CL_ environment variables of MSVC, a
# compiler wrapper), pragmas and attributes in the source (#pragma STDC FP_CONTRACT,
# float_control, optimize("fast-math")), explicit std::fma calls, or how third-party libraries
# were compiled by vcpkg. test_fp_contract.cpp checks the emitted code for the first two.

set(ichor_fp_banned_flags
    "-ffast-math" "-Ofast" "-funsafe-math-optimizations" "-fassociative-math" "-freciprocal-math"
    "-ffp-contract=fast-honor-pragmas" "-ffp-contract=fast" "-ffp-contract=on"
    "-ffp-model=fast" "-ffp-model=aggressive" "-ffp-model=precise"
    "/fp:fast" "-fp:fast" "/fp:contract" "-fp:contract")

function(ichor_fp_scan what value out)
    set(found "")
    foreach(flag IN LISTS ichor_fp_banned_flags)
        if(value MATCHES "(^|[^A-Za-z0-9_=-])${flag}($|[^A-Za-z0-9_=-])")
            list(APPEND found "${what}: ${flag}")
        endif()
    endforeach()
    set(${out} ${found} PARENT_SCOPE)
endfunction()

function(ichor_fp_collect dir targets_out imported_out)
    get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    get_property(imported DIRECTORY ${dir} PROPERTY IMPORTED_TARGETS)
    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(sub IN LISTS subdirs)
        ichor_fp_collect(${sub} sub_targets sub_imported)
        list(APPEND targets ${sub_targets})
        list(APPEND imported ${sub_imported})
    endforeach()
    set(${targets_out} ${targets} PARENT_SCOPE)
    set(${imported_out} ${imported} PARENT_SCOPE)
endfunction()

function(ichor_check_fp_flags dir)
    set(problems "")
    set(configs DEBUG RELEASE RELWITHDEBINFO MINSIZEREL ${CMAKE_BUILD_TYPE} ${CMAKE_CONFIGURATION_TYPES})
    list(TRANSFORM configs TOUPPER)
    list(REMOVE_DUPLICATES configs)
    set(variables CMAKE_CXX_FLAGS)
    foreach(kind EXE SHARED MODULE STATIC)
        list(APPEND variables CMAKE_${kind}_LINKER_FLAGS)
    endforeach()
    foreach(variable IN LISTS variables)
        foreach(suffix "" ${configs})
            if(suffix STREQUAL "")
                set(name ${variable})
            else()
                set(name ${variable}_${suffix})
            endif()
            ichor_fp_scan("${name}" "${${name}}" found)
            list(APPEND problems ${found})
        endforeach()
    endforeach()

    ichor_fp_collect(${dir} targets imported)
    set(target_properties COMPILE_OPTIONS COMPILE_FLAGS LINK_OPTIONS LINK_FLAGS INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
    set(gnu_frontend OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(gnu_frontend ON)
    endif()
    foreach(tgt IN LISTS imported)
        foreach(property INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
            get_target_property(value ${tgt} ${property})
            if(value)
                ichor_fp_scan("imported target ${tgt} ${property}" "${value}" found)
                list(APPEND problems ${found})
            endif()
        endforeach()
    endforeach()
    foreach(tgt IN LISTS targets)
        foreach(property IN LISTS target_properties)
            get_target_property(value ${tgt} ${property})
            if(value)
                ichor_fp_scan("target ${tgt} ${property}" "${value}" found)
                list(APPEND problems ${found})
            endif()
        endforeach()
        get_target_property(type ${tgt} TYPE)
        if(NOT type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            continue()
        endif()
        if(gnu_frontend)
            get_target_property(options ${tgt} COMPILE_OPTIONS)
            if(NOT "-ffp-contract=off" IN_LIST options)
                list(APPEND problems "target ${tgt} COMPILE_OPTIONS: no -ffp-contract=off")
            endif()
        endif()
        get_target_property(source_dir ${tgt} SOURCE_DIR)
        get_target_property(sources ${tgt} SOURCES)
        foreach(source IN LISTS sources)
            if(source MATCHES "^\\$<")
                continue()
            endif()
            cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY ${source_dir})
            foreach(property COMPILE_OPTIONS COMPILE_FLAGS)
                get_source_file_property(value ${source} TARGET_DIRECTORY ${tgt} ${property})
                if(value)
                    ichor_fp_scan("source ${source} of ${tgt} ${property}" "${value}" found)
                    list(APPEND problems ${found})
                endif()
            endforeach()
        endforeach()
    endforeach()

    list(LENGTH targets target_count)
    if(problems)
        list(REMOVE_DUPLICATES problems)
        list(JOIN problems "\n  " text)
        message(FATAL_ERROR "Floating point flags that break cross-platform determinism (SEED-701):\n  ${text}")
    endif()
    message(STATUS "Ichor: floating point flags checked on ${target_count} targets")
endfunction()
