add_executable(ichor_cook
    cook/main.cpp
)
target_link_libraries(ichor_cook PRIVATE ichor_assets)

add_executable(ichor_forge
    forge/main.cpp
)
target_link_libraries(ichor_forge PRIVATE ichor_world)
