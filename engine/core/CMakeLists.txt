file(GLOB ichor_core_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_core ${ichor_core_sources})
target_include_directories(ichor_core PUBLIC include)
target_link_libraries(ichor_core PUBLIC ichor_warnings)

# Stamped at configure time (SEED-691); configure_file leaves the file untouched when the stamp is unchanged.
include(version_stamp.cmake)
find_program(ICHOR_GIT git)
ichor_version_stamp(ICHOR_VERSION_STAMP ${CMAKE_CURRENT_SOURCE_DIR} "${ICHOR_GIT}")
message(STATUS "Ichor version stamp: ${ICHOR_VERSION_STAMP}")
configure_file(version_stamp.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/version_stamp.cpp @ONLY)
target_sources(ichor_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version_stamp.cpp)

if(WIN32)
    target_link_libraries(ichor_core PRIVATE dbghelp) # MiniDumpWriteDump for the crash handler (SEED-698)
endif()
