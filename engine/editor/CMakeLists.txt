file(GLOB ichor_editor_sources CONFIGURE_DEPENDS src/*.cpp)
# panels.cpp needs ImGui and is built into ichor_editor_ui below, only for the runtime.
list(FILTER ichor_editor_sources EXCLUDE REGEX "/panels[.]cpp$")
add_library(ichor_editor ${ichor_editor_sources})
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
