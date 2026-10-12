# Reference content, cooked at build time. The runtime and the tests load the
# bundles from ${ICHOR_CONTENT_DIR}; nothing loads a glTF directly.
set(ICHOR_CONTENT_DIR ${CMAKE_BINARY_DIR}/content CACHE INTERNAL "cooked content output")
file(MAKE_DIRECTORY ${ICHOR_CONTENT_DIR})

set(sources ${CMAKE_SOURCE_DIR}/tests/data/kit.glb ${CMAKE_SOURCE_DIR}/tests/data/arm.glb)
set(ICHOR_ART_DIR ${CMAKE_SOURCE_DIR}/../art CACHE INTERNAL "art sources")
# The three telegraph cues doc 18 asks for travel with the kit.
set(kit_sounds ${ICHOR_ART_DIR}/audio/A_WindupLight.wav ${ICHOR_ART_DIR}/audio/A_WindupHeavy.wav ${ICHOR_ART_DIR}/audio/A_Tell.wav)
set(outputs)
foreach(src ${sources})
    get_filename_component(stem ${src} NAME_WE)
    set(out ${ICHOR_CONTENT_DIR}/${stem}.iscene)
    set(extra)
    if(stem STREQUAL kit)
        set(extra ${kit_sounds})
    endif()
    add_custom_command(
        OUTPUT ${out}
        COMMAND ichor_cook ${src} ${out} ${extra}
        DEPENDS ${src} ${extra} ichor_cook
        COMMENT "cook: ${stem}"
        VERBATIM
    )
    list(APPEND outputs ${out})
endforeach()
add_custom_target(ichor_content ALL DEPENDS ${outputs})
