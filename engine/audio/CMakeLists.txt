find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)

# The one translation unit that holds miniaudio's implementation. The cooker
# decodes WAV through it and the device plays through it.
add_library(ichor_miniaudio
    src/miniaudio_impl.cpp
)
target_include_directories(ichor_miniaudio PUBLIC ${MINIAUDIO_INCLUDE_DIR})
target_compile_definitions(ichor_miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION)

file(GLOB ichor_audio_sources CONFIGURE_DEPENDS src/*.cpp)
# miniaudio_impl.cpp belongs to ichor_miniaudio above, not to this library.
list(FILTER ichor_audio_sources EXCLUDE REGEX "/miniaudio_impl[.]cpp$")
add_library(ichor_audio ${ichor_audio_sources})
target_include_directories(ichor_audio PUBLIC include)
target_link_libraries(ichor_audio
    PUBLIC ichor_core ichor_assets
    PRIVATE ichor_miniaudio ichor_data
)
