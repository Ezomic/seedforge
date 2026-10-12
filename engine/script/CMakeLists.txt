add_library(ichor_script
    src/script_host.cpp
)
target_include_directories(ichor_script PUBLIC include)
find_package(unofficial-lua CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
target_link_libraries(ichor_script PUBLIC ichor_core PRIVATE sol2::sol2 unofficial::lua::lua)
