# Configures a tiny project that calls ichor_check_fp_flags (engine/fp_flags.cmake) and checks the verdict.
# Needs KIND, MODULE, SCRATCH, GENERATOR and GNU_FRONTEND; PLATFORM, TOOLSET, COMPILER and MAKE_PROGRAM
# repeat the real build's choices so the fixture finds the same compiler. The fixture is configured, never built.
#   KIND=clean   the engine's own setup plus harmless look-alikes (-fno-fast-math, /fp:precise): configures
#   KIND=dirty   one banned flag in every place the check reads: fails and names each one
string(RANDOM LENGTH 8 suffix)
set(root "${SCRATCH}/fp_${KIND}_${suffix}")
file(REMOVE_RECURSE "${root}")
file(WRITE "${root}/src/a.cpp" "int a() { return 1; }\n")
file(WRITE "${root}/src/b.cpp" "int b() { return 2; }\n")
file(WRITE "${root}/src/sub/c.cpp" "int c() { return 3; }\n")

set(head "cmake_minimum_required(VERSION 3.25)
project(fp_flags_fixture LANGUAGES CXX)
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()
include(\"${MODULE}\")
")
set(tail "ichor_check_fp_flags(\${CMAKE_CURRENT_SOURCE_DIR})\n")
set(cache_args "")

if(KIND STREQUAL "clean")
    file(WRITE "${root}/src/CMakeLists.txt" "${head}
add_library(lib STATIC a.cpp b.cpp)
target_compile_options(lib PRIVATE -fno-fast-math $<$<CONFIG:Release>:/fp:precise> /fp:strict -ffp-model=strict)
set_source_files_properties(b.cpp PROPERTIES COMPILE_OPTIONS \"-O2;-ffp-contract=off\")
add_library(iface INTERFACE)
target_compile_options(iface INTERFACE -fno-unsafe-math-optimizations)
add_subdirectory(sub)
${tail}")
    file(WRITE "${root}/src/sub/CMakeLists.txt" "add_library(sub_lib STATIC c.cpp)\n")
elseif(KIND STREQUAL "dirty")
    file(WRITE "${root}/src/CMakeLists.txt" "${head}
add_library(lib STATIC a.cpp b.cpp)
target_compile_options(lib PRIVATE $<$<CONFIG:Release>:/fp:fast>)
target_link_options(lib PRIVATE -ffast-math)
set_source_files_properties(b.cpp PROPERTIES COMPILE_OPTIONS -Ofast)
add_library(iface INTERFACE)
target_compile_options(iface INTERFACE -ffp-contract=fast)
add_library(ext::ext INTERFACE IMPORTED)
set_target_properties(ext::ext PROPERTIES INTERFACE_COMPILE_OPTIONS -funsafe-math-optimizations)
add_library(bare STATIC a.cpp)
set_property(TARGET bare PROPERTY COMPILE_OPTIONS \"\")
add_subdirectory(sub)
${tail}")
    file(WRITE "${root}/src/sub/CMakeLists.txt" "add_library(sub_lib STATIC c.cpp)
set_target_properties(sub_lib PROPERTIES COMPILE_FLAGS /fp:contract)
set_source_files_properties(c.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=fast-honor-pragmas)
")
    set(cache_args "-DCMAKE_CXX_FLAGS=-ffast-math" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-Ofast")
    set(expected
        "CMAKE_CXX_FLAGS: -ffast-math"
        "CMAKE_EXE_LINKER_FLAGS_RELEASE: -Ofast"
        "target lib COMPILE_OPTIONS: /fp:fast"
        "target lib LINK_OPTIONS: -ffast-math"
        "b.cpp of lib COMPILE_OPTIONS: -Ofast"
        "target iface INTERFACE_COMPILE_OPTIONS: -ffp-contract=fast"
        "imported target ext::ext INTERFACE_COMPILE_OPTIONS: -funsafe-math-optimizations"
        "target sub_lib COMPILE_FLAGS: /fp:contract"
        "c.cpp of sub_lib COMPILE_OPTIONS: -ffp-contract=fast-honor-pragmas")
    if(GNU_FRONTEND)
        list(APPEND expected "target bare COMPILE_OPTIONS: no -ffp-contract=off")
    endif()
else()
    message(FATAL_ERROR "unknown KIND ${KIND}")
endif()

set(args -S "${root}/src" -B "${root}/build" -G "${GENERATOR}" ${cache_args})
if(PLATFORM)
    list(APPEND args -A "${PLATFORM}")
endif()
if(TOOLSET)
    list(APPEND args -T "${TOOLSET}")
endif()
if(NOT GENERATOR MATCHES "^Visual Studio")
    list(APPEND args "-DCMAKE_CXX_COMPILER=${COMPILER}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" -DCMAKE_BUILD_TYPE=Release)
endif()
execute_process(COMMAND ${CMAKE_COMMAND} ${args} RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
set(log "${out}${err}")

if(KIND STREQUAL "clean")
    if(NOT code EQUAL 0 OR NOT log MATCHES "floating point flags checked on 3 targets")
        message(FATAL_ERROR "the clean fixture did not configure (exit ${code}):\n${log}")
    endif()
else()
    if(code EQUAL 0 OR NOT log MATCHES "Floating point flags that break cross-platform determinism")
        message(FATAL_ERROR "the dirty fixture configured (exit ${code}) or failed for another reason:\n${log}")
    endif()
    foreach(line IN LISTS expected)
        string(FIND "${log}" "${line}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "the check did not name \"${line}\":\n${log}")
        endif()
    endforeach()
endif()
file(REMOVE_RECURSE "${root}")
