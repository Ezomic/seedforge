add_library(ichor_economy
    src/wallet.cpp
)
target_include_directories(ichor_economy PUBLIC include)
target_link_libraries(ichor_economy PUBLIC ichor_data ichor_core)
