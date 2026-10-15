find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB ichor_npc_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_npc ${ichor_npc_sources})
target_include_directories(ichor_npc PUBLIC include)
target_link_libraries(ichor_npc PUBLIC ichor_core ichor_ledger ichor_data ichor_save PRIVATE nlohmann_json::nlohmann_json)
