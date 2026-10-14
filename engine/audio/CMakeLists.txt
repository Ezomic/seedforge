find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)

# The one translation unit that holds miniaudio's implementation. The cooker
# decodes WAV through it and the device plays through it.
add_library(ichor_miniaudio
    src/miniaudio_impl.cpp
)
target_include_directories(ichor_miniaudio PUBLIC ${MINIAUDIO_INCLUDE_DIR})
target_compile_definitions(ichor_miniaudio PUBLIC MA_NO_ENCODING MA_NO_GENERATION)

add_library(ichor_audio
    src/mixer.cpp
    src/ducking.cpp
    src/reverb.cpp
    src/null_audio.cpp
    src/wading_splash.cpp
    src/rain_ambience.cpp
    src/weather_ambience.cpp
    src/ambience_layers.cpp
    src/miniaudio_device.cpp
)
target_include_directories(ichor_audio PUBLIC include)
target_link_libraries(ichor_audio
    PUBLIC ichor_core ichor_assets
    PRIVATE ichor_miniaudio ichor_data
)
