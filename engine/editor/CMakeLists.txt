add_library(ichor_editor
    src/nudge.cpp
    src/picking.cpp
    src/session.cpp
)
target_include_directories(ichor_editor PUBLIC include)
target_link_libraries(ichor_editor PUBLIC ichor_sim)

# The ImGui panels need ImGui, which only the runtime build has.
if(ENGINE_RUNTIME)
    find_package(imgui CONFIG REQUIRED)
    add_library(ichor_editor_ui
        src/panels.cpp
    )
    target_link_libraries(ichor_editor_ui PUBLIC ichor_editor ichor_world imgui::imgui)
endif()
