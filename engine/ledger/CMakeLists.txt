add_library(ichor_ledger
    src/consequence_ledger.cpp
)
target_include_directories(ichor_ledger PUBLIC include)
target_link_libraries(ichor_ledger PUBLIC ichor_core)
