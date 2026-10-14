# Reference content, cooked at build time. The runtime and the tests load the
# bundles from ${ICHOR_CONTENT_DIR}; nothing loads a glTF directly.
set(ICHOR_CONTENT_DIR ${CMAKE_BINARY_DIR}/content CACHE INTERNAL "cooked content output")
file(MAKE_DIRECTORY ${ICHOR_CONTENT_DIR})

set(sources ${CMAKE_SOURCE_DIR}/tests/data/kit.glb ${CMAKE_SOURCE_DIR}/tests/data/arm.glb)
set(ICHOR_ART_DIR ${CMAKE_SOURCE_DIR}/../art CACHE INTERNAL "art sources")
# A checkout without git-lfs holds pointer files, which only fail much later as a cook error.
foreach(lfs_file ${sources} ${CMAKE_SOURCE_DIR}/../art/audio/A_Tell.wav)
    file(READ ${lfs_file} lfs_head LIMIT 24)
    if(lfs_head MATCHES "^version https://git-lfs")
        message(FATAL_ERROR "${lfs_file} is a git-lfs pointer, not the real file. Run: git lfs pull")
    endif()
endforeach()
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
# A forged test world: 4 km at 4 m per sample in 512 m cells, seed 7, with its rivers carved.
set(ICHOR_WORLD_DIR ${ICHOR_CONTENT_DIR}/world CACHE INTERNAL "forged test world")
add_custom_command(
    OUTPUT ${ICHOR_WORLD_DIR}/manifest.iworld ${ICHOR_WORLD_DIR}/heightmap.r16 ${ICHOR_WORLD_DIR}/terrain.iscene
    # No lake of its own, and channels cut along every river that gathers 2000 samples of rain.
    COMMAND ichor_forge ${ICHOR_WORLD_DIR} 7 4096 4 512 0 2000
    DEPENDS ichor_forge
    COMMENT "forge: test world"
    VERBATIM
)
list(APPEND outputs ${ICHOR_WORLD_DIR}/manifest.iworld)
# The UI's typefaces, both under the SIL Open Font License, which asks that the licence
# travel with the fonts, so the texts are copied too.
set(ICHOR_FONT_DIR ${ICHOR_CONTENT_DIR}/fonts)
foreach(font AlegreyaSans-Regular.ttf AlegreyaSans-Bold.ttf Cinzel.ttf AlegreyaSans-OFL.txt Cinzel-OFL.txt)
    add_custom_command(
        OUTPUT ${ICHOR_FONT_DIR}/${font}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ICHOR_ART_DIR}/fonts/${font} ${ICHOR_FONT_DIR}/${font}
        DEPENDS ${ICHOR_ART_DIR}/fonts/${font}
        COMMENT "font: ${font}"
        VERBATIM
    )
    list(APPEND outputs ${ICHOR_FONT_DIR}/${font})
endforeach()
add_custom_target(ichor_content ALL DEPENDS ${outputs})
