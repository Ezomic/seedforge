# Reference content, cooked at build time. The runtime and the tests load the
# bundles from ${ICHOR_CONTENT_DIR}; nothing loads a glTF directly.
set(ICHOR_CONTENT_DIR ${CMAKE_BINARY_DIR}/content CACHE INTERNAL "cooked content output")
file(MAKE_DIRECTORY ${ICHOR_CONTENT_DIR})

set(sources ${CMAKE_SOURCE_DIR}/tests/data/kit.glb)
set(outputs)
foreach(src ${sources})
    get_filename_component(stem ${src} NAME_WE)
    set(out ${ICHOR_CONTENT_DIR}/${stem}.iscene)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ichor_cook ${src} ${out}
        DEPENDS ${src} ichor_cook
        COMMENT "cook: ${stem}"
        VERBATIM
    )
    list(APPEND outputs ${out})
endforeach()
add_custom_target(ichor_content ALL DEPENDS ${outputs})
