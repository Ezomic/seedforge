find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB ichor_data_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_data ${ichor_data_sources})
target_include_directories(ichor_data PUBLIC include)
target_link_libraries(ichor_data PUBLIC ichor_core PRIVATE nlohmann_json::nlohmann_json)
