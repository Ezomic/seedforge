find_package(RmlUi CONFIG REQUIRED)

add_library(ichor_ui
    src/ui_context.cpp
    src/hud.cpp
    src/ledger.cpp
    src/pause_menu.cpp
    src/recording_renderer.cpp
)
target_include_directories(ichor_ui PUBLIC include)
target_link_libraries(ichor_ui PUBLIC ichor_core RmlUi::RmlUi)
