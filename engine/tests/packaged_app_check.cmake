# Stages a package folder the way tools/package_linux.sh and bin/package do (the executable and what it links beside it, with content/,
# shaders/, scripts/, data/ and art/) and runs the staged executable headless from another directory. The run must log that it reads its
# assets from the staged folder: a fallback to the compiled-in build-tree paths would also run, but would not log that line.
# Needs ICHOR (the executable), BUILD (the build tree), REPO (the repository root) and SCRATCH.
set(stage "${SCRATCH}/packaged_app/ichor")
set(elsewhere "${SCRATCH}/packaged_app/elsewhere")
file(REMOVE_RECURSE "${SCRATCH}/packaged_app")
file(MAKE_DIRECTORY "${stage}/shaders" "${stage}/scripts" "${stage}/art" "${elsewhere}")

get_filename_component(exe_dir "${ICHOR}" DIRECTORY)
file(GLOB runtime_libs "${exe_dir}/*.dll")
file(COPY "${ICHOR}" ${runtime_libs} DESTINATION "${stage}")
file(COPY "${BUILD}/content" DESTINATION "${stage}")
file(GLOB spirv "${BUILD}/shaders/*.spv")
file(COPY ${spirv} DESTINATION "${stage}/shaders")
file(GLOB lua "${REPO}/engine/content/scripts/*.lua")
file(COPY ${lua} DESTINATION "${stage}/scripts")
file(COPY "${REPO}/data" DESTINATION "${stage}")
file(COPY "${REPO}/art/exile" "${REPO}/art/settlement_kit" DESTINATION "${stage}/art")

get_filename_component(exe_name "${ICHOR}" NAME)
execute_process(COMMAND "${stage}/${exe_name}" --headless --forged --seed 1 --grid 64 --frames 30
    WORKING_DIRECTORY "${elsewhere}" RESULT_VARIABLE code OUTPUT_VARIABLE text ERROR_VARIABLE text)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "the staged ${exe_name} exited with ${code}:\n${text}")
endif()
file(TO_NATIVE_PATH "${stage}" stage_native)
string(FIND "${text}" "packaged build: assets are read from ${stage_native}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the staged ${exe_name} did not read its assets from ${stage_native}:\n${text}")
endif()
if(NOT text MATCHES "exit after 30 frames")
    message(FATAL_ERROR "the staged ${exe_name} did not finish its frames:\n${text}")
endif()
file(REMOVE_RECURSE "${SCRATCH}/packaged_app")
message(STATUS "the staged ${exe_name} ran from ${elsewhere} on assets beside itself")
