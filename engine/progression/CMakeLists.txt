find_package(nlohmann_json CONFIG REQUIRED)

add_library(ichor_progression
    src/capabilities.cpp
)
target_include_directories(ichor_progression PUBLIC include)
target_link_libraries(ichor_progression PUBLIC ichor_core PRIVATE nlohmann_json::nlohmann_json)
