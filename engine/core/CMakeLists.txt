add_library(ichor_core
    src/log.cpp
    src/file_watch.cpp
    src/input.cpp
)
target_include_directories(ichor_core PUBLIC include)
target_link_libraries(ichor_core PUBLIC ichor_warnings)
