add_executable(ichor_cook
    cook/main.cpp
)
target_link_libraries(ichor_cook PRIVATE ichor_assets)

add_executable(ichor_forge
    forge/main.cpp
)
target_link_libraries(ichor_forge PRIVATE ichor_world ichor_data)
get_filename_component(ICHOR_FORGE_DATA_PATH ../../data ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ichor_forge PRIVATE ICHOR_DATA_DIR="${ICHOR_FORGE_DATA_PATH}")
