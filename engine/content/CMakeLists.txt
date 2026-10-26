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
set(kit_sound_names A_WindupLight A_WindupHeavy A_Tell)
# The footsteps, country beds, hour layers and settlement sounds the sound legend names, cut from CC0 sources (art/audio_sources.md).
list(APPEND kit_sound_names
    A_Step_Grass A_Step_Rock A_Step_Road A_Step_Snow
    A_Bed_Surf A_Bed_Lake A_Bed_Taiga A_Bed_Forest A_Bed_Grassland A_Bed_Marsh A_Bed_Rainforest A_Bed_Savanna A_Bed_Blight A_Bed_Farmland
    A_Dawn_Birds A_Day_Insects A_Dusk_Crickets A_Night_Insects A_Night_Wolves
    A_Settlement_Chatter A_Settlement_Bell A_Settlement_Mill A_Settlement_Surf A_Settlement_Machinery)
set(kit_sounds)
foreach(sound ${kit_sound_names})
    list(APPEND kit_sounds ${ICHOR_ART_DIR}/audio/${sound}.wav)
endforeach()
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
