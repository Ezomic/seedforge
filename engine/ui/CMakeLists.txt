find_package(RmlUi CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(ichor_ui
    src/ui_context.cpp
    src/hud.cpp
    src/hud_layout.cpp
    src/ledger.cpp
    src/journal_screen.cpp
    src/map_screen.cpp
    src/new_game_screen.cpp
    src/pause_menu.cpp
    src/recording_renderer.cpp
)
target_include_directories(ichor_ui PUBLIC include)
target_link_libraries(ichor_ui PUBLIC ichor_core ichor_world ichor_data RmlUi::RmlUi PRIVATE nlohmann_json::nlohmann_json)
