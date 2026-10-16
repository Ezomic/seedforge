file(GLOB ichor_ledger_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(ichor_ledger ${ichor_ledger_sources})
target_include_directories(ichor_ledger PUBLIC include)
target_link_libraries(ichor_ledger PUBLIC ichor_core ichor_factions)
