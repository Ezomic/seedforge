# Screenshot checks for the ichor app. Needs ICHOR, SCRATCH and MODE ("hidden" or "window").
# Prints "no usable Vulkan device" (the test's skip pattern) when none can be opened, as on a runner without a GPU.
#
# hidden: the default screenshot path (a hidden window, frames unthrottled) must say so, render the same bytes twice,
#   and respond to --overlay. It never shows a window, so it runs in every ctest run.
# window: --show-window must give the same PNG bytes as the hidden render. It opens a visible window for a moment, so
#   it only runs when ICHOR_ALLOW_WINDOW=1 (ctest label needs_display) and is skipped otherwise.
if(MODE STREQUAL "window" AND NOT "$ENV{ICHOR_ALLOW_WINDOW}" STREQUAL "1")
    message(STATUS "no usable Vulkan device: the windowed comparison is skipped, set ICHOR_ALLOW_WINDOW=1 to open a visible window")
    return()
endif()

string(RANDOM LENGTH 8 suffix)
set(root "${SCRATCH}/offscreen_screenshot_${suffix}")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}")

execute_process(COMMAND ${ICHOR} --seed 7 --frames 3 --screenshot ${root}/hidden.png
    RESULT_VARIABLE hiddenResult OUTPUT_VARIABLE hiddenOut ERROR_VARIABLE hiddenErr TIMEOUT 120)
set(hiddenLog "${hiddenOut}${hiddenErr}")
if(NOT hiddenResult EQUAL 0 AND hiddenLog MATCHES "vulkan|SDL_Init|SDL_CreateWindow")
    message(STATUS "no usable Vulkan device")
    return()
endif()
if(NOT hiddenResult EQUAL 0)
    message(FATAL_ERROR "the hidden screenshot run failed (${hiddenResult}): ${hiddenLog}")
endif()
if(NOT hiddenLog MATCHES "screenshot: hidden window")
    message(FATAL_ERROR "the default screenshot run did not use a hidden window: ${hiddenLog}")
endif()

if(MODE STREQUAL "window")
    execute_process(COMMAND ${ICHOR} --seed 7 --frames 3 --screenshot ${root}/shown.png --show-window
        RESULT_VARIABLE shownResult OUTPUT_VARIABLE shownOut ERROR_VARIABLE shownErr TIMEOUT 120)
    set(shownLog "${shownOut}${shownErr}")
    if(NOT shownResult EQUAL 0)
        message(FATAL_ERROR "the windowed screenshot run failed (${shownResult}): ${shownLog}")
    endif()
    if(NOT shownLog MATCHES "screenshot: visible window")
        message(FATAL_ERROR "--show-window did not use a visible window: ${shownLog}")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${root}/hidden.png ${root}/shown.png RESULT_VARIABLE same)
    if(NOT same EQUAL 0)
        message(FATAL_ERROR "the hidden and the windowed screenshots differ: ${root}")
    endif()
    file(REMOVE_RECURSE "${root}")
    return()
endif()

execute_process(COMMAND ${ICHOR} --seed 7 --frames 3 --screenshot ${root}/hidden_again.png
    RESULT_VARIABLE againResult OUTPUT_VARIABLE againOut ERROR_VARIABLE againErr TIMEOUT 120)
if(NOT againResult EQUAL 0)
    message(FATAL_ERROR "the second hidden screenshot run failed (${againResult}): ${againOut}${againErr}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${root}/hidden.png ${root}/hidden_again.png RESULT_VARIABLE repeatable)
if(NOT repeatable EQUAL 0)
    message(FATAL_ERROR "two hidden screenshots of the same seed differ: ${root}")
endif()

# A screenshot shows the scene with no debug overlay unless --overlay asks for one: the default run must
# match an explicit --overlay off byte for byte, and the compact and full overlays must each change it.
foreach(mode off compact full)
    execute_process(COMMAND ${ICHOR} --seed 7 --frames 3 --screenshot ${root}/overlay_${mode}.png --overlay ${mode}
        RESULT_VARIABLE overlayResult OUTPUT_VARIABLE overlayOut ERROR_VARIABLE overlayErr TIMEOUT 120)
    if(NOT overlayResult EQUAL 0)
        message(FATAL_ERROR "the --overlay ${mode} screenshot run failed (${overlayResult}): ${overlayOut}${overlayErr}")
    endif()
endforeach()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${root}/hidden.png ${root}/overlay_off.png RESULT_VARIABLE sameAsOff)
if(NOT sameAsOff EQUAL 0)
    message(FATAL_ERROR "the default screenshot is not the same as --overlay off, so it draws an overlay: ${root}")
endif()
foreach(mode compact full)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${root}/hidden.png ${root}/overlay_${mode}.png RESULT_VARIABLE sameAsDefault)
    if(sameAsDefault EQUAL 0)
        message(FATAL_ERROR "--overlay ${mode} left the screenshot unchanged, so it draws nothing: ${root}")
    endif()
endforeach()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${root}/overlay_compact.png ${root}/overlay_full.png RESULT_VARIABLE sameCompactFull)
if(sameCompactFull EQUAL 0)
    message(FATAL_ERROR "the compact and full overlays render the same: ${root}")
endif()
file(REMOVE_RECURSE "${root}")
