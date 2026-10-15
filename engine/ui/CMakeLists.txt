find_package(RmlUi CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB ichor_ui_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_ui ${ichor_ui_sources})
target_include_directories(ichor_ui PUBLIC include)
target_link_libraries(ichor_ui PUBLIC ichor_core ichor_world ichor_data RmlUi::RmlUi PRIVATE nlohmann_json::nlohmann_json)
